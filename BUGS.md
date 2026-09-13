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

### M2. Individual death and respawn: WORKS, one case untested

Confirmed by the user 2026-09-13 (v0.4.0): each dragon respawns on his own at
the right place, before and after a checkpoint; after leaving a level he
returns to the homeworld's true start rather than the portal, which the user
prefers; the save fairy stays quiet until he walks away; a double death costs
two lives. **Not yet tried: a death on the last life**, which should be a
normal game over.

**Now a choice, "Respawn style", at the user's request:** *modern* (the above)
or *original* (every death reloads both, as retail).

**Fixed 2026-09-13 (v0.4.1):** pressing the view-swap key while standing on
the pedestal let the fairy talk at once. The mute named a player slot, which
the key trades; it now moves with the dragon, like moby ownership and Sparx.

### A4. Engine errors from hooking level code: FIXED in v0.4.1, awaiting test

The v0.4.0 session log held **74,705** lines of `override frame stack overflow
at 0x8007DA78`, from the moment the game returned from level 11 to level 10.
The mod had overridden level 11's moby megafunction at that address, and in
level 10 the same address holds other code that is entered repeatedly, each
entry nesting through the override until the engine gave up and bypassed it.
Harmless to play, but about 680 errors a second.

The moby passes no longer hook level code at all. Player 1's pass is the
game's own megafunction call, with the update list filtered as soon as the
main-executable builder `func_80051FEC` writes it; player 2's pass runs at
the start of Spyro's tick. Every hook is now on main-executable code, and the
engine's "raw-tier addresses" warning is gone with it. See the header of
`coop_mobys.c`. **Retest the ram, Sparx and nearest-player enemies**, since
this replaces the mechanism under all three.

### A5. Collision guards refusing coordinates again

The same v0.4.0 session logged **826** probe refusals, after sessions of zero.
Each is an impossible coordinate a collision routine was asked to use, and
averted. Possibly related to A4, since much of the session ran with the engine
bypassing hooks. v0.4.1 logs the first five refusals of each kind with the
coordinates, the caller and the level.

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
