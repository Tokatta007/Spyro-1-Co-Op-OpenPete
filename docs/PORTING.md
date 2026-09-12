# Porting the co-op mod to OpenPete

Assessed against **OpenPete v0.3.0 (Windows)**, read from the shipped `sdk/`
folder and from this project's own measurements on 2026-09-11 and 2026-09-12.
The earlier assessment against v0.2.0 is kept alongside as
[openpete-v0.2-assessment.md](openpete-v0.2-assessment.md); this file
supersedes it.

Everything here is either read from files that ship in the public download or
measured by our own probe mod, so anyone with the same download can check it.
Strategy that depends on private correspondence lives in `docs/private/` and is
deliberately not in this file.

---

## 1. What changed between v0.2.0 and v0.3.0

The mod API grew from 28 functions to roughly 34. New since the last
assessment:

| Added | What it gives us |
| --- | --- |
| `material_register` / `_enable` / `_set_params` / `_set_selector` / `_register_refine` | re-shading what the engine draws, selected per render channel |
| `shader_register` | custom GLSL, loaded from the mod folder |
| `sfx_replace` / `sfx_play` | sound replacement and playback |
| `read_level_data` | level geometry and object data, by level id |
| `moby_classes` | enumerate the moby classes present |
| `binding_down` | poll a named key the player can rebind |
| `vstream_handle`, `notify` | stream handles, and cross-mod messaging |
| render channels (`OP_CHAN_PLAYER`, `OP_CHAN_*`) | address the renderer's feeder paths individually |

**The thing split-screen needs is still not there.** Nothing in v0.3.0's public
API renders the scene a second time, sets a per-pass camera, or sets a viewport
rectangle. That is the definition of split-screen and it remains absent.

## 2. Blockers, in dependency order

### B1: Is the second controller fed? MEASURED 2026-09-12. Partly.

This is the one that gates everything, and the probe answered it precisely. The
answer is more encouraging than a plain "no".

**The engine has a live, independent second pad slot.** Its own input log, from
a session with a keyboard and one DualSense connected:

```
input-log: pad slots present (VBLs):               p1=3503 p2=2933 p3=8 p4=0
input-log: pad slot independence (VBLs differing): p1/p2=275 ...
```

Slot 2 was present for 2,933 vertical blanks and **differed from slot 1 for 275
of them**. A slot that merely echoed player 1 would differ zero times. So the
engine reads the second controller as a genuinely separate input. Four slots
exist; slot 3 flickered on briefly (a keyboard that SDL maps as a gamepad) and
slot 4 was unused.

**The guest's second pad buffer receives none of it.** Same session, our probe
reading guest RAM directly:

```
mod:spyro-coop: tick   1: p1 conn (0xFFFF), p2 abs (0x0000)
mod:spyro-coop: tick 300: p1 conn (0xFFFF), p2 abs (0x0000)
```

`p1 conn (0xFFFF)` is a healthy buffer: status byte 0, and buttons all-high
because the pad buffer is active low and nothing was pressed at that instant.
`p2 abs (0x0000)` is an untouched buffer: status non-zero and the button word
flat zero, which is what memory that has never been written looks like, not
what an idle pad looks like.

**So the gap is precisely the engine-to-guest plumbing for buffer 2**, and
nothing else. It is not controller detection, not SDL mapping, not the gamepad
database, and not our addresses. That is a narrow, well-localised gap, and it
is the single concrete thing to ask upstream for.

**And it is a hardware-fidelity gap, not a missing feature.** The obvious
objection is that Spyro 1 is a single-player game, so nothing should be writing
controller 2 in the first place. The game itself says otherwise. From the
decompilation's `GamepadInitialize`:

```c
PadInitDirect((u_char *)&g_PadBuffer, (u_char *)&g_PadBufferSecondController);
```

and the PSYQ signature it calls, `void PadInitDirect(unsigned char *, unsigned
char *)`, port 1 and port 2. **The game explicitly registers both buffers with
the pad service at startup.** It then never reads the second one: scanning every
instruction that references either buffer gives 3 references for port 1 (the
init, `0x8003354C`, and the pad handler at `0x80053F00`) and exactly **1** for
port 2: the init, and nothing else in the game.

On original hardware the BIOS fills a registered buffer every frame whether the
game reads it or not, which is why the PS1 co-op mod worked at all: it read a
buffer hardware was already populating and the game was already ignoring.

So the comparison is:

| | Original PlayStation | OpenPete v0.3.0 |
| --- | --- | --- |
| Game calls `PadInitDirect(buf1, buf2)` | yes | yes |
| Pad service fills buffer 1 | yes | yes |
| Pad service fills buffer 2 | **yes** | **no, flat zeros** |

Against a stated promise of gameplay bit-identical to original hardware, the
second argument to `PadInitDirect` not being honoured is a divergence worth
fixing on its own terms, independently of whether anyone ever writes a co-op
mod.

Worth recording which renderer this was measured under: the log reported
`renderer=1 native=1`, the default. Not retested under other configurations.

### B2: Does the engine tolerate a mod that runs the tick twice?

OpenPete's headline promise is gameplay bit-identical to PlayStation hardware,
and its SDK is built around savestates, rewind, runahead and replays. Running
the game's own tick twice per frame with a second state set swapped in diverges
from that by construction.

The manifest declares the contract, and the documented values are narrower than
they first look:

| key | values | meaning |
| --- | --- | --- |
| `sim_mutative` | `true` / `false` | writes guest RAM. **A mod with `src/` is assumed mutative unless it sets `false`**, so this is our default either way. Affects the modset tag written to memory cards. |
| `state` | `"none"` / `"rebuildable"` | undeclared **refuses rewind and timeline jumps**. `"rebuildable"` means every host-side value derived from guest history can be re-derived from guest RAM after a jump. |
| `runahead` | `"pure"` / `"host-fx"` | undeclared **disables runahead**. `"host-fx"` declares tick-hook side effects outside guest RAM, which the engine suppresses during speculative ticks. |

Partly answered already: the probe loads and runs with `sim_mutative = true`
declared, and the engine's only response was a fair warning that the memory
cards hold vanilla progress while a mod is active. Nothing refused the mod.

The good news for the finished mod: if player 2's entire state lives in guest
RAM via `guest_alloc`, which is the natural design here and the one the SDK
pushes toward, then `state = "rebuildable"` is honest and rewind keeps
working. That is a strong reason to put *everything* in guest allocations
rather than in mod-side statics.

### B3: Can a mod drive two scene builds through `api->call`?

Unsettled. It decides whether a first split-screen built on the PS1 mod's own
rendering approach works at all. Answer it after B1, by overriding the draw
function and calling it twice.

### B4: Per-pass rendering.

Not available in the public v0.3.0 API, and not ours to build. Four-player
split-screen depends on it.

## 3. How players 3 and 4 would reach the game

Separate from rendering, and unsolved. **The guest only has two pad buffers**,
`OP_GADDR_g_PadBuffer` and `OP_GADDR_g_PadBufferSecondController`, because the
original game supported two controllers. Engine slots 3 and 4 existing does not
mean anything in guest RAM receives them, and the mod API has no general
host-pad read, only `binding_down` for named keys.

Two plausible shapes, neither chosen:

1. more guest buffers, and the mod routes all four through guest RAM as the PS1
   mod does today;
2. players 3 and 4 never touch guest pad RAM, and the mod drives their state
   directly from host input.

Also relevant: the engine's own diagnostics mention `[keys.pad2..4]` rows being
*parsed but not yet remapping that player*, with players 2-4 decoding through
the shared controller mapping. So keyboard-per-player is not available yet
either; players 2 and up need real gamepads.

## 4. What the SDK makes easier than the PS1 build

Worth stating plainly, because it changes how much of the old code is worth
carrying:

- **Every address is a named constant.** `OP_GADDR_g_Spyro`, `OP_GADDR_g_Camera`,
  `OP_GADDR_g_Pad`, `OP_GADDR_g_PadBuffer`,
  `OP_GADDR_g_PadBufferSecondController`, and function addresses for every hook
  site. Struct layouts are pinned with `_Static_assert`, so a layout change
  upstream becomes a compile error instead of silent corruption, the class of
  fault that cost the PS1 project the `g_PadBackup` offset bug.
- **`override_addr` / `override_name` intercept the function, not the call
  site.** The PS1 mod patches 24 individual `jal` instructions, so a call from
  anywhere unpatched is missed. An override catches every call, direct or
  indirect. Confirmed working: the probe's `override_name("CameraUpdate", ...)`
  resolved to `0x80037BD4` and fired every tick.
- **`guest_alloc` gives state that savestates and rewind carry correctly.** The
  BIOS scratch RAM, the boot payload, the `t_size` arithmetic and every
  byte-golf compromise in the PS1 codebase simply go away. About 11 KB of
  hard-won address-space trickery becomes an allocation call.
- **`override_name` survives renames**, so binding to `"CameraUpdate"` rather
  than a raw address keeps working when the decompilation renames things.
- **There is no build step.** The engine compiles `src/*.c` at startup; edit,
  relaunch, read the log. The probe's first compile took under a second.

## 5. What does not port

Everything PS1-specific: `DRAWENV`/`DISPENV`, the ordering table plumbing, the
GTE view-matrix squash (`Sp1x2SquashView`), the entry-patch technique, the boot
stub, and the executable-size arithmetic.

The HUD work largely does not port either. Both panes of the PS1 mod already
draw their own HUD, so the per-pane render-path separation that might seem
necessary is not. What *is* worth knowing, and is the one thing this project
understands better than anyone working from outside:

**There are two kinds of screen-space content in this game and only one of them
is the HUD.** `g_Hud`'s own mobys are the obvious set. The flight levels'
collectible icons and countdown timer are ordinary LEVEL mobys carrying
screen-space positions, written by five hand-written per-level asm functions.
Anything that knows only about the HUD struct misses them, and they draw at
stock coordinates in every pane. We identify them by coordinate range.

Plus two positioning details that cost the PS1 version rounds: the digit sits a
different distance from its icon in each group (+44 gems, +34 dragons, +42
lives), so translating whole groups leaves the numbers ragged; and the life orbs
are placed relative to the lives moby and only recomputed on reset, so they must
travel with it.

## 6. Known bugs inherited from the PS1 version

- **The camera spasm (P1).** A camera occasionally flies away and springs back
  on some hits. Roughly ten reasoned fixes missed it on PS1. The null focus was
  real, was fixed, and was *not* the cause. Worth retrying here mainly because
  the debugging story is better than reading BIOS counters through a memory
  viewer.
- **Flight-level pitch (P2).** Framerate-sensitive: a heavy scene raises
  `g_DeltaTime` and vertical steering dies. Spyro2x2 did not solve this either.
  **This one may simply evaporate on OpenPete**, since the cause is the PS1
  being unable to keep up. Test it early. It would be the first bug the port
  fixes for free.

Three accepted cosmetic issues (widescreen edge blinking, the shared "+3"
pickup text, speedway counter layout) are documented in the PS1 repository's
`BUGS.md` and should be re-evaluated rather than ported.

## 7. Order of work

1. ~~Run the probe. Settle **B1**.~~ **Done 2026-09-12.** The engine slot is
   live and independent; guest buffer 2 is not fed. Blocked on upstream
   plumbing.
2. Settle **B2** properly: does anything complain once the mod starts
   *writing* guest RAM, not merely declaring that it will.
3. Port the per-player state model and swap tables. No rendering yet: one
   viewport, two players, prove the second Spyro ticks and responds. This can
   proceed the moment B1's plumbing lands, and much of it can be written before.
4. Settle **B3**, and if it holds, a first split-screen.
5. **B4** for four-player.

Steps 3 and 5 are where the PS1 repository earns its keep: its `CHANGES.md` is
a complete record of every hook, every swapped region, and every per-player
allocation, and its `CLAUDE.md` records what was tried and failed on the way.
Read both before reimplementing anything.
