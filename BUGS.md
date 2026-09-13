# Spyro Co-Op on OpenPete: open bug list

What is wrong, what is missing, and what has been decided. When an item is
fixed and confirmed, delete it here and record the fix in
`docs/PORT-INVENTORY.md`.

Last reviewed: 2026-09-13.

---

## 1. Active

### A1. A camera runs away from its dragon, most visibly after a ram hit

**Seen 2026-09-12 and 2026-09-13**, on one screen, the same family as PS1 bug P1.

**Suspect 1, the shared frozen-focus vector: RULED OUT 2026-09-13.** Camera
mode 6 and `func_8003FE40` can aim `g_Camera.m_Focus` at the global
`D_80077798`, and a per-player copy was built and tested. It changed nothing,
and the measurement says why: in every runaway logged that session the vector
read **(0, 0, 0)** and no camera was ever focused on it. Replaying one savestate
gave identical events with the setting on and off. The setting remains, off
by effect; it can be removed once A1 is solved.

**What the measurements show instead** (one session, savestates replayed):

- Every event is **deterministic**: the same savestate produced the same event
  at the same distance, 16,464, eight times.
- Distances are **16,000 to 18,000**, not millions. PS1's single 54 million
  reading came from a null focus, which the arming fix already covers.
- Two kinds:
  - **Camera state `0x8000000A`, focus on a moby's position** (`0x8017xxxx`,
    a moby's `m_Position`). This is the mode `func_8003FE40` sets from
    `g_Spyro + 0x21C`. The camera looks at an object, probably the ram, from
    about 17,000 away.
  - **Camera state `0x80000010`, focus on the dragon itself**, yet placed 16,464
    away from him.
- **At the second kind the dragons were 16,025 apart**, almost exactly the
  camera's distance from its own dragon. That is the PS1 signature of a camera
  built around the other dragon. Not proven: the camera's position relative to
  player 2 was not logged.

**Now logged (v0.3.0, unreleased build of 2026-09-13):** each runaway reports
the camera's distance to the other dragon, how far apart the dragons are, the
camera's and both dragons' positions, and what the focus pointer points at, and
flags `CLOSER TO THE OTHER DRAGON` when that is the case.

**2026-09-13, second session, with the detailed log: every runaway was camera
state `0x8000000A` focused on a nearby moby, probably the ram hitting that
dragon**, and two were flagged closer to the other dragon, about 7,000 units up.

**Leading explanation: A2.** A ram's own code steers the camera it hits
(`func_level_20_8007E3A0` calls `func_800342F8` and `func_80033F08`) and, by
the look of the focus pointers, writes itself into `g_Spyro + 0x21C`, which
`func_8003FE40` copies into `m_Focus` with state `0x8000000A`. A ram pulled into
both passes does all of that to player 2's camera while attacking player 1. PS1
recorded the same mechanism for a mid-reaction ownership flip; the pod rule
makes it happen every frame. **Test A1 again once the A2 fix has run.** If the
camera still runs away, compare the same savestate with Player 2 off.

### A2. Rams run at double speed and turn between the dragons: ROOT CAUSE FOUND, fix built

**Seen 2026-09-12 and 2026-09-13.** A ram runs in place toward the dragon the
camera is not on, moves at roughly twice its normal speed when aggressive, and
fails to settle after a charge. With player 2 switched off, on the same
savestate, it immediately turns and charges player 1 at normal speed.

**Double speed means double updates.** Each frame one pass shows it player 1
and the other shows it player 2, so it turns back and forth and moves twice.

**Root cause, read from `func_80051FEC`, the moby update list builder.** The
mod hides a moby from a pass by zeroing `m_WasDrawn` and `m_UpdateDistance`,
which the builder's first loop honours. But every moby also has a pod index,
`m_Pod` (`0x43`). Adding any moby marks its pod, and a second loop then adds
**every member of every marked pod** from `g_MobyPods`, without looking at
either field. A moby whose podmate belongs to the other dragon is pulled into
both passes. The PS1 build had the same flaw; its ram trouble was probably
this all along, not the `g_PadBackup` typo it was credited to.

**Fix built 2026-09-13 (v0.3.0, not yet run in play).** Ownership is decided per
pod group: membership is read from the `g_MobyPods` lists exactly as the
builder reads them, pods linked by a moby naming a different pod are merged,
and every member shares one owner, the dragon nearest any member, with the
usual hysteresis. The readout shows how many mobys are in pods.

**Also fixed 2026-09-13: the view-swap key reassigned every moby.** Ownership
is stored per slot and the key trades slots, so each press handed every moby to
the other physical dragon (1,374 changes in a session with 16 presses). It now
swaps the owner values and the two Sparx.

### A3. Enemies seem to prefer player 1

**Seen 2026-09-12.** Enemies attack both dragons, but lean toward player 1.

**Two known contributors:**

- **On screen means player 1's camera.** Many enemies only attack while
  `m_WasDrawn` is set, and with one screen that is computed from player 1's
  view. An enemy near player 2 but outside player 1's view stays passive.
  Split-screen resolved this on PS1 with a second camera.
- **The hysteresis starts from player 1.** An enemy keeps its owner until the
  other dragon is at least 25% closer, and a fresh table starts every enemy as
  player 1's.

**Partly addressed 2026-09-12 (v0.3.0), awaiting test.**

- **Fresh table per level.** Every moby now starts owned by its truly nearest
  dragon on entering a level, instead of inheriting player 1.
- **The margin is a setting**, "Enemy switch margin (%)", 25 by default (the
  PS1 value), 0 to 50. Owner changes are counted and shown in the readout.

**Change the margin only after A2 is understood:** a lower margin means more
ownership flips, which is candidate 2 for the ram. The on-screen bias is not
addressed and cannot be on one screen.

---

## 2. Missing, planned

### M1. Player 2 in the portal fly-in and exit: WORKS

Confirmed by the user 2026-09-13. `coop_draw.c`, `on_spyro_model`: the level
transition and entrance landing (gamestates 1 and 9, `0x8001A0D8`) and the
level exit (gamestate 10, `0x8001C964`, which the PS1 build never covered).

**The interpolation experiment it carried failed:** the wingman disappears when
interpolation is on, just like the gameplay draw. Calling `base()` twice inside
the renderer's own override does not get the second dragon into the engine's
per-path bookkeeping either. X1 stands.

### M2. Individual death and respawn

Phase B item 5. Currently any death reloads the checkpoint for both dragons, as
retail does.

---

## 3. Accepted for now

### X1. Player 2 is invisible while frame interpolation is on

The engine drops the second dragon from in-between frames
(`PORT-INVENTORY.md` §7). Workaround: interpolation off, or 30 FPS. Needs
engine support to fix properly.

### X2. Player 2 copies player 1's controls

Until OpenPete fills the second controller buffer (`docs/PORTING.md`, B1).
