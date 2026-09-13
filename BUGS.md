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

### M5. In-game Multiplayer and Color menus: stage 1 of 4 BUILT (v0.5.0)

Decided with the user 2026-09-13. The mod should be fully playable from a couch
with controllers, so every player-facing setting must be reachable in game.

**Design:**

- One set of settings (`coop_settings.c`), edited from two places, the in-game
  menu and a panel in the M overlay, and saved to
  `mods/spyro-coop/data/settings.txt`, so both agree and it persists. A mod
  cannot write `[[config]]` values, which is why these are not config rows.
- **Pause menu:** a new MULTIPLAYER row in the pause list (the user's choice
  over the PS1 SQUARE hint).
- **Multiplayer page rows:** Players (1 / 2), Respawn (Modern / Original),
  Colors (opens the Color page), Split screen (Vertical / Horizontal). Split
  screen is shown as not available yet: rendering it needs multi-view support
  in OpenPete.
- **Color page:** per player red, green, blue and strength, like the PS1 COLOR
  page, plus an attempt at spinning Spyro previews (abandoned on PS1 for lack
  of memory; untried here).
- **Development settings** (Draw player 2, the view-swap key, the enemy switch
  margin) stay in the M panel only, and are removed at release.

**Stages:**

1. **Built:** settings core, the M panel, and per-player colour applied to both
   dragons (including the portal wingman) through the game's own tint.
2. Pause menu row and Multiplayer page.
3. Color page.
4. Spinning Spyro previews.

### M3. Sounds from player 2's side: BUILT in v0.4.2, awaiting test

`coop_sound.c`, from `Sp1x2SoundListenerDistance`: a 3D sound's distance is
the nearer of the two cameras, so enemies and pickups near player 2 are heard.
The readout counts voices measured from player 2's camera.

### M4. Player 2's health across levels: BUILT in v0.4.2, awaiting test

Seen 2026-09-13 in Dark Hollow: player 2 arrived with player 1's Sparx colour,
because seeding copies player 1's whole state, health included. Player 2 now
keeps his own health through a level change. A death still gives both full
health.

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

### X4. The two dragons overlap on the portal loading screen when entering a level

Seen 2026-09-13: in the level transition tunnel, when entering a level (and
perhaps only the first time), one dragon's wings pass through the other's.
Not seen when returning home. Noted as a callout.

**Two candidates, not yet measured.** The wingman is placed 640 units along
the wing line computed from Spyro's physics yaw (`g_Spyro + 0x11C`). Either the
flying pose's wingspan is wider than 640, or that yaw does not match the
orientation the dragon is drawn with during this sequence, which would put the
wingman partly in front or behind instead of beside. The body rotation byte
(`g_Spyro + 0x0E`) is what the model is drawn with, and would settle the
second. A wider gap only in gamestate 1 would fix the first without changing
the spacing when play begins.

### X2. Player 2 copies player 1's controls

Until OpenPete fills the second controller buffer (`docs/PORTING.md`, B1).
