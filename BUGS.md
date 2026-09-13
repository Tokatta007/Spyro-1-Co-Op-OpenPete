# Spyro Co-Op on OpenPete: open bug list

What is wrong, what is missing, and what has been decided. When an item is
fixed and confirmed, delete it here and record the fix in
`docs/PORT-INVENTORY.md`.

Last reviewed: 2026-09-12.

---

## 1. Active

### A1. A camera flies off after a ram hit, and the guards start refusing coordinates

**Seen 2026-09-12**, on one screen, the same symptom as PS1 bug P1 ("a camera
flies stratosphere-high and springs back"). So it was never a split-screen
artefact.

**Measured in the same session:** the collision guards refused **46** impossible
coordinates (probe 34, query 12). Every earlier session refused zero. On PS1
those refusals were each an averted freeze, and the coordinates came from a
camera gone wild after a ram hit.

**Leading suspect, from the game's own assembly, not yet tested.** PS1's
`CLAUDE.md` named this as "next suspect" on 2026-08-27 and it was never tried.

`D_80077798` is a single global 3-vector, not part of `g_Camera` and not
swapped per player. Two places point the camera's focus at it:

- **Camera mode 6** (`func_80035FB4`, `0x800368EC`): if `g_Camera.m_Focus` is
  not already `&D_80077798`, copy the current focus into it and set
  `m_Focus = &D_80077798`. The camera then follows **that global**. Spyro
  state 29, "charge interrupted", which is what a ram inflicts, maps to this
  mode.
- **`func_8003FE40`** (`0x80040528`), in Spyro's tick, for the moby Spyro is
  using: set `m_Focus = &D_80077798` and copy that moby's position into it.

`m_Focus` is per player, because it lives inside the swapped `g_Camera`. The
vector it points to is **shared**. And the mod writes that vector itself: the
tick override restores player 1's value after player 2's tick, and the moby
override sets it to player 2's position during his pass so his Sparx follows
him. So a camera frozen on `D_80077798` can have its target moved to the
**other dragon's** position from one frame to the next, and springs toward it.

**Proposed fix:** make `D_80077798` per player by swapping its 12 bytes with the
camera, and delete both of the mod's manual writes, which the swap then makes
redundant (each player's Sparx homes on his own copy). Behind a setting, so the
same savestate can be replayed with it on and off. Add counters for "camera
focused on the shared vector" and "camera more than N units from its dragon",
so the result is measured rather than judged by eye.

### A2. The ram does not settle after its first charge in two-player

**Seen 2026-09-12.** After charging and returning to its start, a ram should
turn round and wait. In two-player it keeps turning left and right at random.
Only after the first charge.

**PS1 history:** the same symptom existed and was closed on 2026-08-30 without
a fix aimed at it, credited (unproven) to the `g_PadBackup` offset typo that was
zeroing enemy model pointers. That typo cannot exist here, so that explanation
was either wrong or incomplete.

**Candidates, in order:**

1. **The shared camera focus vector (A1).** The ram's own level code calls
   camera functions directly (`func_level_20_8007E3A0` calls `func_800342F8`
   and `func_80033F08`), so its behaviour and the camera are coupled. Fixing A1
   first is cheap and may change this.
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

**Deliberately after A2:** loosening the hysteresis increases ownership flips,
which is candidate 2 for the ram.

---

## 2. Missing, planned

### M1. Player 2 in the portal fly-in and exit: BUILT, awaiting test

Built 2026-09-12 in `coop_draw.c` (`on_spyro_model`). Ported from
`Sp1x2DrawPortalSpyro`, and extended: PS1 covered the level transition and the
entrance landing (gamestates 1 and 9, one call at `0x8001A0D8`), and missed
the level exit (gamestate 10, `0x8001C964`), which is now included. Draws a
second copy of player 1's dragon along his wing line using the same offset as
seeding, restoring his position and flame matrix afterwards.

**Also an experiment:** unlike the gameplay draw, this calls `base()` twice
inside the model renderer's own override. Check whether the wingman stays
visible with interpolation **on**. If he does, the gameplay draw can be moved
to the same shape and X1 may go away.

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
