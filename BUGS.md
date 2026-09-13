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

### M5. In-game Multiplayer and Color menus: stages 1 to 3 BUILT (v0.6.0), awaiting test

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
2. **Built (v0.6.0):** MULTIPLAYER row in the pause list, above QUIT / EXIT
   LEVEL, and the Multiplayer page: PLAYERS, RESPAWN, COLORS, SPLIT (grey, with
   a "not available yet" hint), DONE. TRIANGLE or DONE goes back one level.
3. **Built (v0.6.0):** the Colors page, as on PS1: RED, GREEN, BLUE, STRENGTH
   for P1 and P2, LEFT/RIGHT by one, L2/R2 by 16, SQUARE resets the column,
   and a swatch per player showing the blended result.
4. Spinning Spyro previews. Not started.

Checked headless 2026-09-13 with screenshots (`--skip-to-level artisans` and a
scripted movie): the list with five rows and a grown box, the shimmer on
MULTIPLAYER, both pages, a value change, backing out, and moving past
MULTIPLAYER in both directions. Not yet seen: the EXIT LEVEL and flight-level
QUIT variants of the list. Two looks differ between the renderers: the grey
"disabled" shade shows pale purple in the native renderer, and the swatch
borders draw only in PsyCross.

### C1. Colour follows the slot, not the dragon: FIXED in v0.5.1, awaiting test

Seen 2026-09-13 with player 1 green and player 2 red: after the view key the
camera's dragon was still green, and player 2 talked to the balloonist and
freed a dragon as a green dragon. Colour was chosen by slot, and both the view
key and a handover put player 2's dragon in slot 0. `coop_physical_player`
now answers which person is in each slot, from the view-key state and the
handover flag. Known gap: if player 2 dies in a way that runs the stock death
sequence, the death animation still shows player 1's colour.

### C2. The portal wingman's colour: FIXED in v0.5.2

Confirmed by the user 2026-09-13: two different colours through the portal,
the camera's dragon keeps its own colour with interpolation on, and colours
follow their dragons across the view key. Cause and fix: OpenPete's native
rebuild of Spyro takes one colour per renderer call, from the last dragon drawn
in it, so every extra dragon is its own call, drawn before the camera's dragon.

### C3. Spyro is purple in a dragon's dialogue: ENGINE SIDE, for the author

Measured 2026-09-13 (v0.5.3 diagnostic). During a dragon rescue the model
renderer was called from three sites, `0x8001D180`, `0x8001D4B8` and
`0x8001D5D4`, every time with the player's filter going in (`9700FF00`,
green) and the retail renderer's GTE far colour coming out green. The user
still saw purple in the conversation. So the mod's colour reaches the game's
renderer, and OpenPete's native rebuild of Spyro does not apply the filter in
these scenes, although it does in gameplay and the portal sequences. Nothing
the mod can reach; include it in the note to the OpenPete author.

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

### M2. Individual death and respawn: WORKS

Confirmed by the user 2026-09-13 (v0.4.0): each dragon respawns on his own at
the right place, before and after a checkpoint; after leaving a level he
returns to the homeworld's true start rather than the portal, which the user
prefers; the save fairy stays quiet until he walks away; a double death costs
two lives. A death on the last life is a normal game over (confirmed the same
day).

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

### X4. Dragons overlap on the portal transition screen: PARKED, ENGINE SIDE, for the author

Screenshots 2026-09-13 on "Entering Stone Hill", then reproduced headless from a
savestate at the portal (see "Headless screenshots" in `CLAUDE.md`).

The camera starts facing the pair, then swings to a side view and stays there.
Logged in that pose: camera to lead (+2695, +58, +818), lead to wingman
(+1024, 0, 0), so the wingman is directly behind the lead.

**The cause is the native renderer.** On the same frame, PsyCross (the reference
renderer) follows draw order as a PS1 does: with the wingman drawn first the
nearer dragon correctly covers him, and with the lead first the far dragon
paints over. The native renderer draws parts of the far dragon over the near
one in both orders. Draw order was the PS1 fix; on OpenPete a mod cannot reach
the native renderer's choice.

| Version | Tried | Result |
| --- | --- | --- |
| PS1, v0.5.2 | wing line, level | the look the user wants; overlaps side-on |
| v0.5.3 | square to the camera's view | nose to tail side-on; rejected |
| v0.5.4 | behind, out along the wing line, and lower | one big dragon and one small; rejected |
| v0.5.5 | lead drawn first | no change in the native renderer, and both dragons took one colour |
| v0.5.6 | dropping below the lead as the camera swings side-on | clear, but looked wrong at the landing; rejected, user wants one level plane |
| **v0.5.7** | **back to the wing line, level** | parked |

For the author note: two Spyro model draws in one frame are not depth-ordered
by the native renderer the way PsyCross orders them.

### X2. Player 2 copies player 1's controls

Until OpenPete fills the second controller buffer (`docs/PORTING.md`, B1).
