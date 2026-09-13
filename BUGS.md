# Spyro Co-Op on OpenPete: open bug list

What is wrong, what is missing, and what has been decided. When an item is
fixed and confirmed, delete it here and record the fix in
`docs/PORT-INVENTORY.md`.

Last reviewed: 2026-09-13.

---

## 1. Active

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

The ram bug that made loosening this risky is fixed (`PORT-INVENTORY.md` §9),
so the margin can now be tuned by feel. The on-screen bias is not addressed and
cannot be on one screen.

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

### M2. Individual death and respawn: BUILT, awaiting test

Built 2026-09-13 (v0.4.0) in `coop_respawn.c`, ported from `Sp1x2Die`,
`Sp1x2Ground`, `Sp1x2CaptureSpawn`, `Sp1x2SparxHeal` and `Sp1x2FairyMute`.
Setting **"Separate respawn"**, on by default.

When one dragon dies while the other is alive and a life remains, the game's
death trigger is not called. One shared life is spent, the lives counter opens
and shows the new total, and he is placed at the checkpoint (or the level's
true start, or where the level was entered), stood on the floor with the
game's own probe, and reset with `ResetSpyroState(1)` plus 90 frames of
invulnerability. A double death, or a death with no lives left, runs the stock
sequence, and a double death charges both lives.

**Changed from PS1, deliberately:**

- **The teleport detector is resampled only for player 1's respawn.** PS1 wrote
  it for either player. It tracks player 1, so writing player 2's respawn
  point into it would read as a level restart on the next frame whenever the
  two were more than `0x4000` apart, and snap the pair together.
- **Player 1's Sparx is healed only after one of our respawns**, not whenever
  `g_Sparx` is null, which is also true while a dragon at zero health waits for
  a butterfly to bring Sparx back. Player 2's Sparx is respawned only once he
  has health.

**To test:** let each dragon die on his own, in a level and on a homeworld,
before and after touching a checkpoint; die near a rescued dragon's pedestal
(the save fairy should stay quiet until you walk away); die together; die on
the last life.

---

## 3. Accepted for now

### X1. Player 2 is invisible while frame interpolation is on

The engine drops the second dragon from in-between frames
(`PORT-INVENTORY.md` §7). Workaround: interpolation off, or 30 FPS. Needs
engine support to fix properly.

### X3. A ram can hesitate when both dragons are near it after a charge

Seen 2026-09-13, after the pod fix. If both dragons are close to a ram that
has already charged once and is returning to its spot, or turning in place, it
can hesitate before attacking again. It does attack eventually, and it is hard
to reproduce. Likely its pod changing owner as the two distances cross the
switch margin. Accepted by the user as not worth chasing for now.

### X2. Player 2 copies player 1's controls

Until OpenPete fills the second controller buffer (`docs/PORTING.md`, B1).
