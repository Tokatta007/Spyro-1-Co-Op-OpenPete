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
