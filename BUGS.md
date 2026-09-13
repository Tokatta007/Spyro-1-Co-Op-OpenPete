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

**The deciding test still to run: the same savestate with Player 2 off.** If the
camera does the same thing in the unmodded game, it is retail behaviour and the
runaway threshold is too low. If it does not, player 2 causes it.

### A2. The ram does not settle after its first charge in two-player

**Seen 2026-09-12.** After charging and returning to its start, a ram should
turn round and wait. In two-player it keeps turning left and right at random.
Only after the first charge.

**PS1 history:** the same symptom existed and was closed on 2026-08-30 without
a fix aimed at it, credited (unproven) to the `g_PadBackup` offset typo that was
zeroing enemy model pointers. That typo cannot exist here, so that explanation
was either wrong or incomplete.

**Found and fixed 2026-09-13: the view-swap key reassigned every moby.**
Ownership is stored per slot, and the key trades which dragon is in which
slot, so every press handed each moby to the other physical dragon. That session
logged 1,374 owner changes with 16 presses, and the user saw a ram change target
right after swapping views. The key now swaps the owner values and the two
Sparx, so each stays with its dragon. This removes a test artefact; it is not
expected to be the root of A2, which appears without pressing the key.

Also seen: a ram running in place toward player 2 while player 1 was closer.
Consistent with the hysteresis, which keeps a moby's owner until the other
dragon is 25% closer, but not proven for that ram.

**Candidates, in order:**

1. **The camera runaway (A1).** The ram's own level code calls
   camera functions directly (`func_level_20_8007E3A0` calls `func_800342F8`
   and `func_80033F08`), so its behaviour and the camera are coupled.
2. **Ownership flipping during its return.** The ram moves a long way between
   the dragons on a charge, so it can cross the 25% hysteresis margin and be
   updated against the other dragon mid-behaviour. PS1 established that a flip
   in the middle of a reaction does real damage. Measure the ram's owner
   changes before changing anything.

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
