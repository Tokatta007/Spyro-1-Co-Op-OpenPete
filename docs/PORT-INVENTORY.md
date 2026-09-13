# Port inventory: every piece of the PS1 mod, and what it becomes

Built 2026-09-12 from the PS1 repository's `CHANGES.md` (the record of every
hook, allocation and swapped region) and the decomp build's `src/coop/`
source. Each item below says what it did on the PlayStation, what it becomes
on OpenPete, and which phase it belongs to.

**Phases.**

| Phase | Goal | Blocked on |
| --- | --- | --- |
| **A** | Two Spyros tick and two cameras update, player 2 borrowing player 1's input. No drawing. | nothing |
| **B** | Gameplay correctness: nearest-player enemies, individual death and respawn, Sparx, body separation, sound. | A |
| **C** | Player 2's own controller. | B1 in `PORTING.md` (upstream) |
| **D** | Split-screen rendering and HUD. | B3 / B4 in `PORTING.md` |
| **E** | Settings UI (players, split, colour). | D, mostly |
| drop | PS1-only machinery with no job left on OpenPete. | |

---

## 1. Facts this inventory rests on, all checked

**OpenPete runs the retail executable.** Its log reports
`PSX EXE: pc=0x8005B8E0 load=0x80010000 size=0x65800`, the retail
`SCUS_942.28`. So the addresses in the PS1 `CHANGES.md` (which are retail
addresses) apply directly. The decomp build's addresses do **not**: that build
lengthened the executable and moved `.data` and `.bss`.

**The two gameplay call sites, read from the retail executable**
(SHA-1 `84e3728a...`, matching `CHANGES.md`):

| RAM | Word | Instruction | Return address an override sees |
| --- | --- | --- | --- |
| `0x80033AD8` | `0x0C012880` | `jal func_8004A200` (Spyro's tick) | `0x80033AE0` |
| `0x80033B4C` | `0x0C00DEF5` | `jal CameraUpdate` | `0x80033B54` |

**An override may run the original more than once.** From the SDK's code mod
guide: *"base() ... may be called any number of times; each call runs the
continuation once."* That is the whole technique.

**An override can tell who called it.** From the CPUState reference: *"ra is
the return address, written by the caller before the call."* This matters
because the PS1 mod patched **one call site**, while an override catches
**every call** to a function. Spyro's tick and the camera update both have
other callers, listed in section 2, and they must get stock behaviour.

**Registers change across `base()`.** The same reference: *"expect a0..a3,
t0..t9, v0, v1 and ra to have changed across it."* An override that calls
`base()` twice must restore them in between.

**Per-player state belongs in `guest_alloc`.** Arena bytes are carried by
savestates, rewind and runahead like guest RAM. Host statics are not.

---

## 2. The 24 hooks

### Logic

| PS1 hook | What it did | OpenPete | Phase |
| --- | --- | --- | --- |
| `0x80033AD8` `jal TickSpyroGameplayFrame` → `Sp1x2TickPlayer2Spyro` | Spyro's tick once per player, P2's state swapped in. Seeding, level-change reseed, handover, substep budget, anchor restore. | `override_name("func_8004A200")`, acting only when `ra == 0x80033AE0`. | **A** |
| `0x80033B4C` `jal UpdateCameraFrame` → `Sp1x2UpdateCameras` | Camera update once per player. | `override_name("CameraUpdate")`, acting only when `ra == 0x80033B54`. | **A** |
| `0x80033AA4` `jalr g_UpdateMoby` → `Sp1x2UpdateMobys` | Moby megafunction twice, each pass masking the other player's mobys, so every moby sees its nearest player. | Override the megafunction by the same call site. Owner and mask tables move to the arena. | B |
| `0x80042F10`, `0x8004A4D8` `jal TriggerRespawnOrGameOver` → `Sp1x2Die` | Individual respawn while a partner lives. Life HUD, double-death lives, save-fairy mute. | One `override_name("func_8002C85C")`, filtered to those two return addresses. | B |
| `0x80056528` `jal VecMagnitude` → `Sp1x2SoundListenerDistance` | Voice distance = nearest of both cameras. | Override `VecMagnitude` filtered by return address. It has many callers, so the filter is essential. | B |

**Other callers the overrides must leave alone:**

| Function | Caller | Gamestate |
| --- | --- | --- |
| `func_8004A200` (tick) | `func_8002E000` | 9 |
| `func_8004A200` (tick) | `func_8002F3E4`, dragon rescue cutscene | 8 |
| `CameraUpdate` | `func_8002DF9C` | 1 |
| `CameraUpdate` | `func_8002E000` | 9 |
| `CameraUpdate` | `func_8002E084` | 10 |
| `CameraUpdate` | `func_8002F3E4`, dragon rescue cutscene | 8 |

### Input

| PS1 hook | What it did | OpenPete | Phase |
| --- | --- | --- | --- |
| `0x80012444` `jal InstallVSyncCallback` → `Sp1x2InstallVSyncCallback` | Installed a callback running `PadVSync` twice, swapping raw buffers and derived state, protecting three clocks, merging P2's buttons in menus. | Override `PadVSync` directly; no install hook needed. | **C** |

**Phase A stand-in:** before player 1's tick, snapshot `g_Pad` (164 bytes) and
`g_ActivePad`, and write them into player 2's pad shadow before his tick. The
buffered input frames live inside the `Gamepad` struct and `g_ActivePad`
points into it, so this is a complete copy. `g_PadBackup` and `g_PadSwapFlag`
stay per-player. When B1 lands, only this stand-in changes; the swap
machinery is already the real one.

**The deferred poll** (`SP1X2_PAD_HOLD`, `Sp1x2PadRelease`) existed because the
VSync *interrupt* could fire while player 2 was swapped in. Whether that can
happen on OpenPete is unknown. Phase A observes `PadVSync` and counts any call
that lands inside the swap window, so this is measured rather than assumed.

### Rendering

| PS1 hook | What it did | OpenPete | Phase |
| --- | --- | --- | --- |
| `0x8001227C` `jal GamestateDraw` → `Sp1x2Graphics` | Scene twice, viewport and view squash per player. | Depends on per-pass rendering. The squash is **drop** on the pane path (the engine owns projection). | D |
| `0x8001A0D8` `jal RasterizePairedActor` → `Sp1x2DrawPortalSpyro` | Second dragon in the portal fly-in. | Depends on how the native renderer draws Spyro. | D |
| `0x80058BC0` `jal TickSparkles` → `Sp1x2TickSparkles` | Age particles once per frame, not once per pass. | Only needed if the scene is built twice. | D |

### Pause menu (9 hooks)

`0x8001A7F8`, `0x8001A894` (box background and border), `0x8001A980`
(`Sp1x2PauseDraw`), `0x800338B8` (`Sp1x2PauseUpdate`), and five
`BuildTextSprites` sites suppressing CONTINUE, OPTIONS, INVENTORY and the three
row-4 variants.

**OpenPete:** replaced for now by `[[config]]` rows in `mod.toml`, which the
engine renders in its own Mods panel with no guest writes. An in-game page can
come back later if it is still wanted. Phase **E**.

### Guards and retail fixes

| PS1 hook | What it did | OpenPete | Phase |
| --- | --- | --- | --- |
| `0x8004AE38` `j Sp1x2ProbeGate` | Refuse a segment probe whose end has a coordinate negative or `>= 0x400000`. Averted a freeze. | `override_addr` reading `a1`, returning `v0 = 0` without `base()`. Counted. | **A** |
| `0x8004BE4C` `j Sp1x2QueryGate` | Same rule on the sphere query centre. | Same, reading `a0`. | **A** |
| `0x80017228`, `0x8001722C` `add` → `addu` | Retail "Baruti crash": signed overflow trap in `VecMagnitude`. | Probably moot: a recompiler is unlikely to emulate the overflow exception. **Not verified.** | check in B |

The guards are in phase A deliberately. They defended against exactly the kind
of impossible coordinate a brand-new second Spyro can produce, and a freeze
during the experiment would hide the answer it exists to give.

---

## 3. Swapped per-player state

All addresses retail. Every one except `0x800770BC` has an SDK name.

### Spyro: 15 regions, 1,092 bytes

| Retail | Size | SDK name | What |
| --- | --- | --- | --- |
| `0x80078A58` | 676 | `OP_GADDR_g_Spyro` | the whole struct; `m_Position` is at +0 |
| `0x800786C8` | 312 | `OP_GADDR_g_SpyroFlame` | flame |
| `0x8007AA10` | 40 | `OP_GADDR_D_8007AA10` | drop shadow |
| `0x80075718` | 4 | `OP_GADDR_g_SurfaceBelowFlags` | collision scratch |
| `0x80075788` | 4 | `OP_GADDR_D_80075788` | idle anim timeout |
| `0x80075804` | 4 | `OP_GADDR_D_80075804` | contact actor |
| `0x80075808` | 4 | `OP_GADDR_g_CollisionTriangleIndex` | collision scratch |
| `0x80075814` | 4 | `OP_GADDR_g_IsSpyroHidden` | draw suppressed |
| `0x800758A0` | 8 | `OP_GADDR_D_800758A0` | turn rate accum, flame timer save |
| `0x800758C0` | 4 | `OP_GADDR_D_800758C0` | fall reference Z |
| `0x80075960` | 4 | `OP_GADDR_D_80075960` | pitch rate accum |
| `0x80075970` | 4 | `OP_GADDR_D_80075970` | idle anim cursor |
| `0x80076B80` | 4 | `OP_GADDR_g_CollisionPoint` | collision scratch |
| `0x800770BC` | 4 | `OP_GADDR_g_DragonCutscene + 0x8C` | gem pickup mirror actor |
| `0x80077368` | 16 | `OP_GADDR_g_CollisionNormal` | collision normal |

### Camera: 5 regions, 288 bytes

| Retail | Size | SDK name | What |
| --- | --- | --- | --- |
| `0x80076DD0` | 272 | `OP_GADDR_g_Camera` | the struct |
| `0x800756B8` | 4 | `OP_GADDR_D_800756B8` | forced-to-destination flag |
| `0x80075894` | 4 | `OP_GADDR_D_80075894` | camera static |
| `0x80075924` | 4 | `OP_GADDR_D_80075924` | L2/R2 rotate speed |
| `0x80075938` | 4 | `OP_GADDR_D_80075938` | camera static |

### Pad: 4 regions, 333 bytes

| Retail | Size | SDK name |
| --- | --- | --- |
| `0x80077378` | 164 | `OP_GADDR_g_Pad` |
| `0x800776D8` | 164 | `OP_GADDR_g_PadBackup` |
| `0x80075944` | 1 | `OP_GADDR_g_PadSwapFlag` |
| `0x800757E0` | 4 | `OP_GADDR_g_ActivePad` |

### Saved and restored, never swapped

| Retail | SDK name | Why |
| --- | --- | --- |
| `0x80075760` | `OP_GADDR_g_UnprocessedFrames` | physics substeps owed. Player 1's tick consumes it, so player 2 is handed the same budget and the frame is left consumed once. |
| `0x80077798` | `OP_GADDR_D_80077798` | player anchor, restored to player 1's after player 2's tick so followers track player 1. |
| `0x800758C8` | `OP_GADDR_g_LevelTicks` | clock, protected around P2's pad poll. Phase C. |
| `0x8007588C` | `OP_GADDR_g_CDReadTime` | clock, protected around P2's pad poll. Phase C. |

### Also written per tick

`g_Spyro + 0x21C` is armed with `&g_Spyro.m_Position` when null, before each
tick. `func_8003FE40` copies it unchecked into `g_Camera.m_Focus`, and nothing
in the main executable initialises it. This was the real null-focus bug on PS1.

---

## 4. The PS1 memory map, and where each entry goes

| PS1 location | OpenPete |
| --- | --- |
| `LOADER`, `BIOS2`, `BIOS2B` code regions | **drop**: the engine compiles and loads the mod |
| boot stub, `t_size`, payload arithmetic | **drop** |
| P2 Spyro shadow `0x8000E800` | arena (A) |
| ready, last level, handover, substeps owed, last gamestate | arena (A) |
| P2 camera shadow + extras `0x8000EE00` | arena (A) |
| P2 pad state `0x8000F200` | arena (A) |
| teleport detector sample `0x8000F1C0` | arena (A) |
| which-player-is-ticking `0x8000ED50` | arena (A) |
| gate refusal counters `0x8000ED60/64` | host counters, display only (A) |
| P2 Sparx, rebuild detector | arena (B) |
| arrival capture, true-start cache | arena (B) |
| fairy mute | arena (B) |
| moby owner, mask stash, `m_WasDrawn` sync | arena (B) |
| region visibility tables, particle snapshot, flame chains, render pass | only if the scene is built twice (D) |
| split, widescreen, view fit, players, colour | `[[config]]` (E); players and a phase A enable switch are in now |
| menu cursor, wobble mobys, menu active | **drop** unless the in-game page returns |
| dead diagnostic slots `0x8000ED70..7C`, markers `0x8000F000` | **drop** |

Every "zero headroom", "4 bytes free" and byte-golf comment in the PS1 source is
now history. The arena has no such limit.

---

## 5. Lessons that carry over unchanged

These cost the PS1 project real time and apply to any construction:

- **Either both players' state is swapped, or neither.** A guard on one swap
  and not another left the live camera and live Spyro belonging to different
  players, and the camera measured itself against the wrong dragon.
- **A missing global produces a specific, recognisable bug.** Walking in place
  (idle cursor), endless jump and glide loop (control flags), flame at the
  wrong dragon (flame struct). Complete the table; do not patch symptoms.
- **Snapshot before the first consumer.** The substep budget and the input ring
  index were both saved too late once, which restored an empty value.
- **Anything that moves a live dragon updates the teleport detector's sample in
  the same breath**, or the next frame reads the move as a level restart.
- **Code running while identities are swapped must not compare live state
  against player 1 samples.**
- **When player 2's tick starts a global sequence** (death, portal, dragon,
  balloonist), leave Spyro and camera live and swap only the pad back. Swapping
  the rest back stranded the sequence and crashed.

---

## 6. What phase A measures

1. Does the engine accept a mod that runs Spyro's tick and the camera update
   twice per frame, or does it warn or refuse?
2. Is the game stable with two ticks running: no crash, no freeze, sane frame
   rate?
3. Does player 2 move as a separate body, colliding with the world on his own?
   Measured as the distance between the two dragons changing even though both
   follow the same input.
4. Do the overrides see the return addresses this document predicts?
5. Does `PadVSync` ever run inside the swap window? (Decides whether phase C
   needs the deferred poll.)
6. Do the collision guards ever refuse anything?

### Results, first session, 2026-09-12

About 400 seconds and 23,791 frames, in the Artisans homeworld (level 10),
through a portal into level 11, with the mod toggled off and on once.

| Question | Answer |
| --- | --- |
| 1. Engine accepts it? | **Yes.** No warning, refusal or divergence report from the engine. Its only warning was a startup frame-pacing slip that also appeared before the mod existed. |
| 2. Stable? | **Yes.** No crash or freeze, 100 FPS against a 100 FPS target. |
| 3. Separate body? | **Yes.** The dragons started 640 apart and reached **55,996** apart while following identical input. Heard: both dragons' wall-bump sounds on a charge. Seen: the invisible dragon killed an enemy. |
| 4. Return addresses as predicted? | **Yes.** Every gameplay call matched. The other callers seen were the tick at `ra 0x8002E010` (35 calls) and the camera update at `ra 0x8002E018`, both inside `func_8002E000`, gamestate 9, exactly as section 2 lists. They got stock behaviour. |
| 5. `PadVSync` inside the swap window? | **Never.** 0 of about 24,000 calls. Phase C very likely does not need the deferred poll. |
| 6. Collision guards refuse anything? | **No.** 0 and 0. |

Also confirmed: the level change was caught by the teleport detector and
player 2 was reseeded in level 11 (`teleports=1`, `seeds=3`); toggling the mod
off and on reseeded him cleanly; the swap_view key traded identities 67 times
without incident.

**Not yet exercised:** a death (`deaths=0`), a sequence started by the
*shadow* dragon (`handovers=0`; the portal was entered by the live one), and
the dragon rescue caller (gamestate 8). Those are the next things to provoke.

---

## 7. Visibility experiment, 2026-09-12

`coop_draw.c` calls the game's model, shadow and flame renderers a second time
with player 2's Spyro state swapped in, the way the PS1 build drew him.

### What was seen

- **Normal play: player 2 is not visible.** Toggling "Draw player 2" makes no
  difference. The readout confirms the code runs (`draws=299` in the log).
- **Holding Tab (fast-forward): player 2 appears.**
- **Also under Tab: the camera's dragon sometimes stutters**, replaying the
  current animation over and over until it finishes. Not seen in normal play.
- **Player 2's flame particles are visible in normal play**: nostril smoke and
  the puff at the end of each flame. Particles are a shared world system his
  tick spawns into, drawn by a different path.
- **Sparx flies to whichever dragon the swap_view key makes live.** Expected:
  Sparx follows the live dragon, and player 2 has no Sparx of his own until
  phase B.

### What the engine said

One line, and it names the problem:

```
[render-paths] DEFECT f=2527 path=spyro fed 203 native tris with no polybuf
interval and no render_path_mark_drawn; sub-tick presents drop it
```

Player 2's 203 triangles **did** reach the native renderer on its `spyro` path.
But at a render rate above the sim rate, most presented frames are sub-tick
frames the engine builds between ticks, and those are assembled from bracketed
intervals of each render path. The second dragon arrives outside any bracket,
so every sub-tick frame drops him and only tick-aligned frames keep him. Tab
presents far fewer sub-tick frames, which is why he shows up there.

The same log reports `interp: initialised: 5 regions, 1200 bytes/snapshot` and
`capture enabled (render-fps=100 > sim ~30 Hz)`: the engine snapshots some
guest regions to interpolate between ticks. A snapshot taken while player 2 is
swapped into `g_Spyro` would blend the two dragons' animation state, which fits
the stutter under Tab. **That is a hypothesis, not a measurement.**

No `RESTORE MISMATCH` or sub-tick write was reported
(`cpu_writes_while_open=0`), so the swap does not perturb the engine's own
sub-tick machinery.

### What it means

Calling the renderers twice is **correct**: the geometry is produced and
accepted. What is missing is the engine's per-path bookkeeping for a second
dragon, which a mod cannot supply (`render_path_mark_drawn` is not in the API).
Smooth visibility at high frame rates therefore needs engine support, and this
log line is the precise thing to ask about.

### Follow-up tests, same day. All three predictions held.

| Test | Prediction | Result |
| --- | --- | --- |
| Render FPS 30, matching the sim | player 2 visible, no stutter | **Visible, fine** |
| Render FPS 100, interpolation toggled off | the same | **Visible, fine** |
| Fast-forward with "Draw player 2" off | he disappears | **He disappears** |

So the diagnosis is confirmed, not inferred: the draw is correct, and the only
thing hiding player 2 is sub-tick interpolation, which assembles its frames
from bookkeeping a second dragon does not get.

**Working setup until the engine supports it:** turn interpolation off once
per launch (there is no startup setting for it, only the runtime toggle), or
run at 30 FPS. Both dragons are visible either way.

---

## 8. Phase B, part 1: built 2026-09-12, not yet run

Three of the five phase B items, ported together. Individual death and
respawn is deliberately left for last, as a separate piece.

| Item | File | Ported from |
| --- | --- | --- |
| Dragons push apart when they overlap | `coop_players.c`, `separate_players` | `Sp1x2SeparatePlayers` |
| Every moby updates against its nearest dragon | `coop_mobys.c` | `Sp1x2UpdateMobys`, `Sp1x2AssignMobys`, `Sp1x2MaskWalk` |
| Player 2's own Sparx | `coop_mobys.c`, `p2_sparx_keep` | `Sp1x2P2SparxKeep` (v4) |

**One design difference forced by OpenPete.** The PS1 build patched the one
`jalr g_UpdateMoby` instruction. Here, `g_UpdateMoby` points into level code,
every level's code loads at `0x8007AA38`, and so one address holds different
functions in different levels. The mod reads the pointer each frame, attaches
an override the first time it sees a new address, and acts only when called
from `GamestateUpdate`'s call site (return address `0x80033AAC`, read from the
retail executable). Called from anywhere else, that address runs stock.

**Deliberately not ported yet:**

- `Sp1x2SparxHeal` (respawning player 1's Sparx). It exists because the PS1
  individual respawn skips the level reload that normally brings Sparx back.
  It belongs with individual respawn.
- `Sp1x2SyncMobyFlags`. It merged "was this moby on screen" across two
  viewports. With one screen there is one camera, so nothing to merge. A
  limitation follows: a moby player 1's camera cannot see reads as off screen,
  and many enemies do not attack while off screen.
- `Sp1x2FairyMute`. Only needed once a dragon can respawn onto a save-fairy
  pedestal on his own.

**Allocation ledger.** The moby tables are a second `guest_alloc`, not a larger
first one, because OpenPete accepts a savestate only if its allocation list is
a prefix of the live one. Appending keeps earlier savestates loadable.

**A cap of 4 Sparx spawns per level**, logged when reached, so a dragonfly that
keeps dying for an unknown reason cannot fill the level.

### What to look for

- Enemies near the invisible dragon should react to **him**: chase, attack,
  flee.
- A second Sparx following player 2, and no more bouncing between dragons on
  the view swap key.
- Walking into each other should push the dragons apart instead of overlapping.
- In the readout: `Moby passes: two-player` climbing in levels, `single` in
  flight levels and menus; `update functions hooked` about one per level
  visited; `P2 Sparx spawns` about one per level.
