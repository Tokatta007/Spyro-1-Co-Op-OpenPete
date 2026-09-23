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

### M6. Three and four players: WORKS (confirmed by the user, v0.8.0)

Built 2026-09-13 at the user's request. The mod was written around exactly one
extra dragon; it now runs up to three shadows (slots 1..3), with a table of
which player is in which slot, so colors, moby ownership and Sparx follow the
person through the view key and handovers. PLAYERS on the Multiplayer page
and in the M panel goes from 1 to 4.

- Tick, camera, moby pass, drawing and sound loop over the shadows; every moby
  goes to its nearest dragon with the same hysteresis; each shadow keeps his
  own Sparx and carries his own health across a level.
- Body separation checks every pair.
- Individual respawn works while ANY other dragon is alive; a death with the
  rest already down runs the stock respawn and charges a life per dragon.
- The portal formation: slot 1 on the right, slot 2 on the left, slot 3
  outside slot 1, at the usual spacing.
- The extra players still copy player 1's input, and are still invisible with
  interpolation on (X1).

**Found while testing, and fixed:** the extra dragons were drawn three times
per tick, because OpenPete runs the composer's (and the portal's) Spyro draw
three times per tick. With four players the draw list outgrew the engine's
DrawOTag scratch and the overflow corrupted game state within a tick. They are
now drawn once per tick, and all four stay visible in both renderers.

Confirmed by the user 2026-09-13: enemies target the nearest dragon, and
deaths, dragon rescues, the balloonist, the view key and a flight level all
work with four. The session log had no errors or warnings. In a flight level a
crash still ends the run for everyone; the user would prefer the crasher sits
out, but accepts it for now.

v0.8.1: spacing back to the PS1 640 at the user's request, and a respawned
dragon blinks in for 45 ticks (M7).

### M7. Respawn arrival: blink (v0.8.1) and layered effects (v0.9.1)

A dragon who respawns on his own blinks in for 45 ticks. On top of that the
user wants an arrival effect, chosen by trying them: "Respawn effect" in the
M panel picks one of eight, and O plays it on the camera's dragon without
dying (`coop_effects.c`). All are the game's own particles or crystal pieces,
work in every homeworld and level, and were each seen drawing in the native
renderer headless (`shots/respawn-effects-v090.png`): smoke puff, white
sparks, orange sparks, dust ring, color flash in the player's color, chest
break, magic pop, and the crystal burst (the rescue crystal's pieces; only in
levels with dragons, which is every place a respawn happens).

**v0.9.1, the user's choice:** effects are layers that combine, a box each in
the M panel, defaulting to the crystal burst with orange sparks, white sparks
and the dust ring. Everything but the dust ring was coming out of the top of
Spyro's head; an "Effect height" slider now sets where they start, default
-200 from his position.

**v0.9.2:** the color flash is gone, and the rescue star is a new layer: the
flat star that grows out of a frozen dragon when Spyro touches it, drawn with
the rescue's own renderer and animation (`coop_effects.c`). The default mix is
now crystal burst, orange sparks, white sparks, dust ring, smoke puff and the
star, at height -300. Seen in the native renderer headless
(`shots/rescue-star-v092.png`).

The user's crystal-dragon idea as a whole is not possible: the crystal's
shake is its own moby's animation, and spawning that moby starts the rescue
cutscene.

**v0.11.0:** the mix is final. The bench is gone: no O key, no layer boxes or
height slider; a respawn always plays every layer at -300.

### M8. Flight levels: a crash sits that player out: BUILT (v0.10.3), awaiting play

User request 2026-09-13: flight levels have no respawns, and one crash ended
the run for everyone. Found in the executable: a flight crash never reaches
the death trigger. Spyro's code calls the level's Flight1 through D_80075694
instead (three call sites), which starts the results (gamestate 7). The mod
points that pointer at an empty function it overrides (`coop_flight.c`): a
crash while another dragon still flies sits the crasher out (not ticked, not
drawn, the camera moves on if it was his); the last crash, the timer and the
finish run the real Flight1.

Checked headless in Sunny Flight: with player 2 still flying, player 1's crash
sat him out and the camera moved to player 2; with both crashed, the results
ran as retail. Not seen yet: a whole run finished by the remaining players,
or three or four players.

The user's first play of v0.10.0 found two bugs, both fixed in v0.10.1:

- **After "Try again" player 2 was hidden and frozen.** Who sat out was never
  forgotten on the retry's reseed, since the level does not change. Every
  seed now clears it.
- **Player 2 hovered over the water and could not crash.** He had left the
  previous flight mid-crash with health -1, and the mod carried that home and
  back in. Water only harms a dragon with health 0 or more, so he landed on
  the surface. Retail keeps nothing from a flight level (it saves health on
  entry and restores it after), and the shadows now do the same. A negative
  health is also never carried. Reproduced headless before the fix: forcing
  player 2 to -1 in Sunny Flight stopped him at z 2423, as in the user's log.

The second play (v0.10.1) confirmed both fixes and found three more, all
reproduced headless and fixed in v0.10.2:

- **Sat-out dragons stayed frozen through a retry** (log, 281 s). A retry was
  caught only by the live dragon jumping more than 0x4000 in a frame, and a
  crash close under the start point jumps less. Now the flight results screen
  itself marks a reseed for when play resumes, whether the player retries or
  leaves. When a shadow's crash opens the results, there is no handover
  either: swapping slot 0's old state back over the reloaded level would put
  every dragon back where it crashed.
- **With four players, player 4 flew away after leaving a level** (in a
  straight line, through the scenery, for good). Every dragon glides out of
  the exit portal in formation (Spyro state 15, walking state 9) until it
  finds the landing. Slot 3, 1280 units out, misses it at the Sunny Flight
  portal. A shadow still in that glide a second after slot 0 lands is now set
  down beside him (`land_strays`).
- **A portal acted as a wall** until another dragon arrived. Touching a portal
  switches on its path moby, which carries in whichever dragon is live when
  it updates, and it updated in its nearest dragon's pass. The dragon whose
  tick touched the portal now owns its path moby, and that moby's whole pod,
  until the transition ends. Headless, player 1 now enters on the same frame
  as a solo Spyro; before the fix he pushed against the frame for 2.3 seconds.

The third play (v0.10.2): the portal and the flight level itself were fine.
Two portal-exit problems remained, fixed in v0.10.3:

- **Player 4 still flew off, sometimes for twenty seconds.** The set-down
  only acted once slot 0 had landed, and never on slot 0 itself, so after a
  view swap onto the stray nothing happened. Now any dragon still in the exit
  glide 45 ticks after any other has landed is set down beside that one.
- **Stone Hill's exit came out in single file.** The formation's wing line
  was (sin, cos) of the yaw, "established by observation" at portals that all
  faced along an axis, where it agrees with the true (-sin, cos). Stone Hill
  faces about 228 degrees; measured headless, the old offset ran parallel to
  the flight path. It is perpendicular now, in the fly-out draw and the seed.

**Fixed in v0.10.4: the lift up to the Dark Hollow portal dropped riders**
1/2 or 3/4 of the way up, varying run to run (user, with a savestate; not
the whirlwind first suspected). The lift carries Spyro only while it updates
with him live, and ownership is by distance with height included: once the
rider was high enough, a dragon near the base became "nearer" and the lift
moved to his pass. Reproduced headless from the savestate by forcing the
lift's owner away mid-ride, which dropped the rider exactly so. A ridden moby
(Spyro state 17, or ControlFlags bit 31, with m_mobyInUseBySpyro pointing at
it) now belongs to its rider (`coop_mobys.c`, RIDES); with the owner forced
away the whole ride now completes.

v0.10.5, at the user's request: everyone can ride at once. A lift being
ridden (state 17) also updates in the pass of every other rider and of any
dragon at its foot, so they step on too (`coop_mobys.c`, SHARED LIFTS).
Headless from the savestate, two dragons rode up together and one took the
Dark Hollow portal. The lift's sound and sparkles repeat per rider. Cannons
use other flags and are left one at a time.

### M5. In-game Multiplayer and Color menus: stages 1 to 3 BUILT; stage 4 blocked on the engine

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

1. **Built:** settings core, the M panel, and per-player color applied to both
   dragons (including the portal wingman) through the game's own tint.
2. **Confirmed by the user (v0.6.0), revised in v0.6.1:** MULTIPLAYER row in
   the pause list, above QUIT / EXIT LEVEL, and the Multiplayer page: PLAYERS,
   RESPAWN, SPLIT (gray, with a "not available yet" hint), then COLORS and
   DONE, settings above and ways out below at the user's request. The big
   title reads MULTIPLAYER in place of PAUSED. Hints under the box for
   RESPAWN ("death causes individual respawn" / "death restarts both
   dragons") and SPLIT. TRIANGLE or DONE goes back one level.
3. **Built (v0.6.0), widened in v0.6.1:** the Colors page: RED, GREEN, BLUE,
   STRENGTH for P1 to P4, LEFT/RIGHT by one, L2/R2 by 16, SQUARE resets the
   column. Columns for players beyond the PLAYERS setting are gray and
   skipped by the cursor. Settings now keep four colors.
4. **Spinning dragon previews: built in v0.7.0, switched off in v0.7.1.**
   ENGINE SIDE, for the author. In play the native renderer draws the pause
   screen over a frozen snapshot of the last gameplay frame and draws no 3D
   model on top, so the user saw none; they appeared only in headless
   screenshots, which have no snapshot. The code stays in `coop_menu.c`
   behind `COOP_PREVIEW_DRAGONS`, standing in the neutral pose (animation 0,
   frame 0) at the user's request, and the color swatches are back in their
   row. Ask for: model draws over the pause backdrop.

Checked headless 2026-09-13 with screenshots (`--skip-to-level artisans` and a
scripted movie): the list with five rows and a grown box, the shimmer on
MULTIPLAYER, both pages, a value change, backing out, and moving past
MULTIPLAYER in both directions. The EXIT LEVEL variant confirmed by the user;
the flight-level QUIT variant not yet seen. The gray "disabled" shade shows pale purple in the
native renderer; the user is fine with it.

**Fixed in v0.6.2, at the user's report:** text was centered on
length x spacing, but the builder positions letter CENTERS, moves a space on by
three quarters, and moves the first letter (and any after a digit or space) on
by the size; lines sat half a letter left, and lines with spaces further. And
in a wide window the native renderer stretches a plain flat quad over the whole
window while the text, box and lines stay in the centered 4:3 area, so the
swatches slid out of their frames on an ultrawide screen. Their corners are now
squeezed by that stretch, from the window aspect the present hook reports.
Gradient quads and TILE sprites were tried as swatches and do not draw in the
native pause view at all. (The swatches were then replaced by the dragons in
v0.7.0, which, being 3D models, need no squeeze.)

### C1. Color follows the slot, not the dragon: FIXED in v0.5.1, awaiting test

Seen 2026-09-13 with player 1 green and player 2 red: after the view key the
camera's dragon was still green, and player 2 talked to the balloonist and
freed a dragon as a green dragon. Color was chosen by slot, and both the view
key and a handover put player 2's dragon in slot 0. `coop_physical_player`
now answers which person is in each slot, from the view-key state and the
handover flag. Known gap: if player 2 dies in a way that runs the stock death
sequence, the death animation still shows player 1's color.

### C2. The portal wingman's color: FIXED in v0.5.2

Confirmed by the user 2026-09-13: two different colors through the portal,
the camera's dragon keeps its own color with interpolation on, and colors
follow their dragons across the view key. Cause and fix: OpenPete's native
rebuild of Spyro takes one color per renderer call, from the last dragon drawn
in it, so every extra dragon is its own call, drawn before the camera's dragon.

### C3. Spyro is purple in a dragon's dialogue: FIXED in v0.12.0, confirmed by the user

The Spyro in a dragon rescue cutscene is not g_Spyro but a moby of class 511
(`g_DragonCutscene + 0x8C` points at it), so the color filter written into
g_Spyro never reached him. Found headless by painting classes 510 and 511 in
two colors with materials during a rescue: 511 is Spyro, 510 the dragon.

The fix is an OpenPete material (`shaders/spyro_tint.frag`, registered in
`coop_draw.c`, RESCUE) on class 511, whose refine callback returns the
rescuer's color: the player in slot 0 during the cutscene, since a shadow who
touches a statue is handed over. The shader scales the color by each pixel's
brightness before mixing, since a flat mix erased all shading at full
strength. Headless: player 1 rescuing gave a green Spyro, player 2 (moved
onto player 1's path) a red one, both shaded like the gameplay dragons.

### C4. Color only the purple parts of Spyro: POSSIBLE, needs an engine feature for per-player colors

The user asked whether the tint could leave horns, wings and belly their own
color. The PS1 attempt failed because the model's color palette encodes
lighting, not material. A material shader sees each pixel's final color
instead, and a hue test separates the parts cleanly: headless, with the
filter off, purple pixels went green while horns (gold), crest (orange), wing
membranes (red), belly and eyes stayed stock, on both the gameplay Spyro and
the cutscene Spyro. The shader is kept in `docs/experiments/purple_only.frag`.

The blocker is per-player color in gameplay: the "player" channel takes one
uniform block per frame for every Spyro draw, and has no per-instance refine,
so every dragon would wear the same color. The cutscene Spyro (moby channel,
per instance) could use it today. Needed from the engine: a per-instance key
on the player channel, so each Spyro draw can get its own block.

**User's review of the experiment (2026-09-14):** looks surprisingly good,
but the nose, the cheeks, the edge where the body meets the yellow belly, and
probably the underside of the tail stay gray (the same areas were awkward on
PS1). Likely cause: Spyro's colors are blended across each face, so where a
purple corner meets a yellow one the colors in between pass through gray.
Those pixels have too little saturation, or a hue outside the purple range,
and the hue test leaves them stock. The cheeks and nose may also be a paler
lavender. To try when this comes back: grade the recolor by how purple a
pixel is along the purple-to-yellow axis, rather than an on/off hue window,
so blend pixels get a matching share of the new color. The rest is kept for
the next message to the OpenPete author.

**Planned design, agreed with the user 2026-09-14 (waits for the engine
feature; the RGB page ships until then):**

- Two recolored groups per player: SKIN (the purple body) and ACCENT (the
  yellow horns, crest and belly; whether the red wing membranes join ACCENT
  or stay stock is to be decided by looking). Everything else stays stock.
- Recolor by hue, keeping each pixel's own brightness, so shading stays the
  game's and no strength setting is needed. Blend pixels between the groups
  take a proportional share of each (see the gray fringe above).
- Colors page rows, per player column: SKIN, SKIN SHADE, ACCENT, ACCENT
  SHADE, then DONE (four rows, as today). SKIN and ACCENT cycle the color
  wheel in about 24 named steps (RED, ORANGE, GOLD, GREEN, TEAL, BLUE,
  PURPLE, PINK...) plus WHITE, GRAY and BLACK at the end; L2/R2 jump faster.
  Each SHADE is its own row (for example DARK, NORMAL, LIGHT, PASTEL), so dark
  skin with bright horns is possible. Two swatches per column show the
  result. The M panel gets color-wheel pickers for the same settings.
- Existing RGB colors convert to the nearest SKIN color on first load.

### M3. Sounds from player 2's side: BUILT in v0.4.2, awaiting test

`coop_sound.c`, from `Sp1x2SoundListenerDistance`: a 3D sound's distance is
the nearer of the two cameras, so enemies and pickups near player 2 are heard.
The readout counts voices measured from player 2's camera.

### M4. Player 2's health across levels: BUILT in v0.4.2, awaiting test

Seen 2026-09-13 in Dark Hollow: player 2 arrived with player 1's Sparx color,
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

### X1. Player 2 is invisible while frame interpolation is on: FIXED ENGINE SIDE in OpenPete 0.4

The engine dropped the second dragon from in-between frames
(`PORT-INVENTORY.md` §7); the workaround was interpolation off, or 30 FPS.
Reported to the author with a minimal repro mod, fixed in his WIP build and
released in 0.4. Confirmed by the user 2026-09-14: at 100 FPS with
interpolation on the extra dragons stay drawn and the flame and shadows look
right.

### X3. A ram can hesitate when both dragons are near it after a charge

Seen 2026-09-13, after the pod fix. If both dragons are close to a ram that
has already charged once and is returning to its spot, or turning in place, it
can hesitate before attacking again. It does attack eventually, and it is hard
to reproduce. Likely its pod changing owner as the two distances cross the
switch margin. Accepted by the user as not worth chasing for now.

### X4. Dragons overlap on the portal transition screen: PARKED, ENGINE SIDE, for the author

Screenshots 2026-09-13 on "Entering Stone Hill", then reproduced headless from a
savestate at the portal (the recipe is in the private notes; see `CLAUDE.md`).

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
| v0.5.5 | lead drawn first | no change in the native renderer, and both dragons took one color |
| v0.5.6 | dropping below the lead as the camera swings side-on | clear, but looked wrong at the landing; rejected, user wants one level plane |
| **v0.5.7** | **back to the wing line, level** | parked |

For the author note: two Spyro model draws in one frame are not depth-ordered
by the native renderer the way PsyCross orders them.

### X2. Extra players' controls: a controller each, v0.12.1 (headless proof; awaiting play test)

OpenPete still never fills the game's second pad buffer (`docs/PORTING.md`,
B1), so the mod reads the extra players' controller from the host and builds
a pad record like the game's (`coop_players.c`, CONTROLS). "Players 2-4
controls" in the M panel switches back to copying player 1. Headless: with
"Controller" and no device, player 1 walks and the others stand; with "Copy"
they follow.

**v0.11.0, first real test (DualSense, player 1 on the keyboard): failed.**
Face buttons and d-pad moved nobody; the left stick and Options moved player
1. Two causes in the log: the engine accepts at most **8 [[binding]] rows**
per mod, so 9 of the 16 were refused (and each refused lookup logged an
error, 14,895 of them); and the 7 accepted "pad:" rows never read as held.
The log lists three SDL pads: the DualSense, an "XInput Controller #1" and
the Razer keyboard. openpete.toml's `left_x = "none"` did not stop the stick.

**v0.11.1 measures both routes** (`coop_controls.c`): ImGui's gamepad keys,
read in an always-on UI section every present (no row limit), and four probe
bindings putting cross on pad:, pad2:, pad3: and pad4:. The first input on
each is logged and the M panel shows them live. The settings moved to their
own "Spyro Co-Op" window, since an always section runs outside the Mods
panel. Player 1's sticks now point at the empty pad4 slot.

**v0.11.1 test: ImGui works.** D-pad, face and shoulder buttons moved players
2 to 4 only. No probe binding ever read, so the probes are gone. The left
stick still moved player 1 as well, and Options plus the stick were the only
controller inputs reaching the menus: OpenPete feeds the controller's stick
into player 1's pad buffer whatever openpete.toml says ("none" and an empty
pad4 slot both failed; reproduced headless with a movie's stick).

**v0.11.2:** just before the game decodes player 1's buffer, the mod reports
it as a digital pad, so the game ignores its sticks (`coop_pad.c`). Headless:
the movie's stick no longer moves player 1, the d-pad still does. Outside
gameplay the controller's buttons are added to player 1's, so the controller
can drive menus and dialogue. The "XInput Controller #1" in the log predates
the DualSense; its vendor ID is 046D (Logitech), most likely the user's mouse.

**v0.11.2 test:** the controller moved only players 2 to 4 and worked in the
pause menu, but WASD stopped moving player 1 (it drives the left stick, now
hidden), and after a view swap the keyboard moved the camera's dragon while
the controller moved player 1's.

**v0.11.3:** input follows the player, not the slot: the slot holding player
1 gets the game's pad, every other slot the controller's (slot 0 by writing
the controller's record over g_Pad before his tick). WASD is on the d-pad in
openpete.toml. The pause menu is player 1's again; on the Colors page player
1 edits his column and DONE, and the controller has its own cursor over the
other active players' columns (`coop_menu.c`, TWO CURSORS). Headless: the
walk test still moves only player 1 with "Controller" and everyone with
"Copy"; the Colors page draws.

**v0.11.3 test:** the view swap and the second cursor worked; WASD did not.
OpenPete keeps one keyboard key per game button ("the extra source is
ignored"), so the arrows won, and WASD is also the keyboard stick's default.

**v0.11.4:** WASD alone on the d-pad, the keyboard stick on the arrows (no
key warnings at load). On the Colors page every extra player has his own
cursor in his own column, fed by `coop_controls_player(p)`; with one
controller they all move and change together until more controllers exist.
ImGui merges every gamepad into one set of keys, so separate controllers per
player will need OpenPete's multi-controller support, not this route.

**v0.11.4 test: all working** (WASD, the per-player Colors cursors, input
following the player through a view swap). One bug: stepping PLAYERS onto 1
stepped twice (4 -> 2). Hiding the stick was tied to "players >= 2", so
landing on 1 turned player 1's pad back into a DualShock mid-press; the
game recalibrates on that change and PadCaliReset clears m_Held, so the held
key read as a new press. **v0.11.5** keys it to the controls setting alone.

**v0.12.1: OpenPete 0.4 (mod api 12) reads four pads and hands a mod every
slot.** `pad_read(slot)` gives slot 0, player 1's, and slots 1 to 3, which
"nothing writes into guest RAM, so a mod that wants extra players decodes
libpad into a pad buffer it allocates itself" - this mod's exact shape. So
player n now reads pad slot n-1 with his own buttons AND his own analog
stick, and `coop_players.c` builds one pad record per player instead of one
shared one. The bytes come from the tick's input-log record, so input
reproduces under replay and rewind, which the ImGui route did not. The always
-on UI section existed only to sample ImGui every present and is gone; the
settings are back inside the Mods panel.

**Headless proof 2026-09-22** (v0.4.1, `--skip-to-level artisans`, a two
-player bk2 whose P1 column holds Up and P2 column holds Left from frame
2300): the movie's second player lands on pad slot 1, the log says "player 2
has a controller on pad slot 1", and the two dragons end 20,000 units apart
(P1 84992,54382 against a wall, P2 83643,74320). A movie carries two players,
so slots 2 and 3 read absent under replay.

**Slot assignment with a real device 2026-09-22:** with the user's DualSense
plugged in and player 1's buttons keyboard-only in `openpete.toml`, the
engine's own summary reports `pad slots present (VBLs): p1=900 p2=717` - the
controller drives slot 1, so one controller plays player 2 while the keyboard
plays player 1. More controllers fill slots 2 and 3 for players 3 and 4.

**v0.12.2: which slot is which, and rumble.** Two things the first test
turned up. First, what counts as a gamepad is SDL's business: the user's log
lists his Razer keyboard and (through an XInput shim) his Logitech mouse as
pads beside the real controller, so the pad a player means is not always slot
1. "Player n plays on" in the M panel picks the slot, saved as `p2_pad` and
friends, and the panel lists what every slot is doing so a player can hold a
button and find his own. The panel draws on the present thread, where
`pad_read` is refused, so `coop_controls_scan()` caches all four slots once
per VBL from the PadVSync hook.

Second, **rumble** (`coop_rumble.c`). The game keeps four vibration globals
and decodes them once per VBL into an actuator pair - normal {1,120},
electric {1,0}, custom {0,amount}, else {0,0} - then counts each timer down;
`pad_rumble(slot, small, large)` takes exactly that pair. The globals are one
set for one dragon, so the mod parks them at zero around each player's tick
and keeps whatever that tick left: an extra player's goes to his own pad,
player 1's is merged back into the globals so the game drives his pad as
always, in whichever slot his dragon ticks. Headless in Artisans, player 2
walking into something logged "rumble: player 2's own buzz (normal 0, shock
0, custom 15) goes to pad slot 1", and both dragons ended exactly where they
did before the change, so the parking is gameplay-neutral. Motors are refused
headless ("0 motor command(s) issued"), so the buzz itself needs the user.

**v0.12.2 test: the controller moved nobody.** Pause worked (Options is bound
to player 1's start in openpete.toml) and the keyboard played player 1, but no
other button did anything. The log says why. OpenPete opens three "gamepads"
on the user's machine - the DualSense, his Razer keyboard, and his Logitech
mouse through an XInput shim - and fills the pad slots in that enumeration
order, so:

    slot 0  DualSense            <- player 1's slot, the game's own
    slot 1  XInput Controller #1 (the mouse)
    slot 2  Razer Huntsman V3 Pro (the keyboard)
    slot 3  absent

The mod reported players 2 and 3 as "connected" because slots 1 and 2 really
do have devices; they are just a mouse and a keyboard, which never press a
gamepad button. Meanwhile the one real controller is in player 1's slot,
where only what openpete.toml binds reaches the game - `pad:start`, hence the
pause and nothing else. The engine's own census agrees: `pad slots present
(VBLs): p1=7724 p2=6139 p3=6139 p4=0`.

Nothing in openpete.toml or the flags assigns a device to a slot (searched
the executable's strings: `[keys.pad2..4]` rows exist but "are parsed and do
not re-map that player yet"), so a mod cannot move the controller off slot 0.

**v0.12.3** shows all four slots in the M panel, slot 0 included, with a
marker on whichever slot is pressing something, and logs once per slot that
has a device. Finding your own controller is then a matter of pressing a
button and reading the list.

**The ask for the author:** a way to say which device is which pad slot -
even just "player 1 is the keyboard, give the pads to slots 1 and up", or
skipping devices that are really a keyboard or a mouse. Without it, one
controller plus a keyboard cannot be two players, which is the commonest
couch setup there is.

**One engine-side noise item for the author:** OpenPete 0.4 checks guest pad
RAM against the record it staged, and this mod deliberately writes that RAM
(player 1's pad reports itself digital, and outside gameplay the controller's
buttons are merged into his so either can answer dialogue). That trips
`input-log: slot-0 staging first mismatch ... staged bits=0xFFEF readback
bits=0x7FEF` as an [error], 1414 times in a 5400-frame run. It is a false
positive for a mod that writes the buffer from the same staged record, but it
is loud.
Headless, the same 15-frame press: 4 -> 2 before, 4 -> 1 after.