# Spyro Co-Op, on OpenPete

Split-screen co-op for **Spyro the Dragon**, built as a mod for
[OpenPete](https://openpete.com/), the native PC port of the game.

**Status: playable, 1 to 4 players, on one shared screen.** Split-screen
needs multi-view rendering in OpenPete, so for now everyone shares one camera
and the **P** key moves it from dragon to dragon. See
[docs/PORTING.md](docs/PORTING.md) for what is still blocked and
[BUGS.md](BUGS.md) for the details of everything below.

## What works

- Up to four dragons, each in their own color.
- A **Multiplayer** page in the pause menu: number of players, respawn style,
  and a Colors page where every player sets their own color.
- Respawns: a dragon who dies respawns on his own with a crystal burst, while
  the others play on. The original "everyone restarts" style is an option.
- Flight levels: a dragon who crashes sits out while the others finish.
- Enemies go after the nearest dragon, every dragon has his own Sparx, and
  portals, lifts, whirlwinds, dragon rescues and the balloonist all work with
  four.

## Controls

**Everyone gets their own controller.** OpenPete 0.4 hands a mod all four pad
slots, so player 2 plays on the first extra controller, player 3 on the
second and player 4 on the third, buttons and analog stick both. Player 1
uses the game's own controls, set up in OpenPete as usual — keyboard or the
pad in his own slot.

Set "Players 2-4 controls" in the M overlay to *Controller* (the default), or
to *Copy player 1* to have every dragon follow player 1. On OpenPete 0.3 and
older there are no extra pad slots, so the extra players copy player 1 and the
log says so.

**Rumble** works for everyone: each controller buzzes for its own dragon,
from the game's own vibration, and the game's vibration option still turns it
off.

Which physical controller lands in which pad slot is up to OpenPete and SDL,
and some keyboards and mice announce themselves as gamepads, so the M overlay
lists what each slot is doing and lets you say which slot each player reads.
Hold a button and watch the list to find yours.

If your only controller lands in slot 0, it is player 1's and no mod can
reach it. The slots follow SDL's own order, so launching with the
environment variable `SDL_JOYSTICK_HIDAPI=0` set can reorder them and push
the controller down to slot 1, where player 2 can have it.

To keep player 1 on the keyboard while the controllers play the others, give
each of his buttons one keyboard key and no `pad:` entry in `openpete.toml`,
for example:

```toml
[game.spyro-1.keys.pad]
cross      = "K"
square     = "J"
triangle   = "I"
circle     = "L"
dpad_up    = "W"
dpad_down  = "S"
dpad_left  = "A"
dpad_right = "D"
start      = ["Return", "pad:start"]   # lets a controller pause too
```

## Known limits

- One shared screen, until OpenPete can draw more than one view.
- Needs **OpenPete 0.4 or newer** for a controller per player.

## Different from the PS1 mod

The original is
**[Spyro-1-Co-Op-Mod](https://github.com/Tokatta007/Spyro-1-Co-Op-Mod)**, which
ships as an `.xdelta` patch and does two-player split-screen on real hardware.
It stays up, and it stays the reference implementation.

It is also at the limit of the console. Its code lives in about 11 KB of BIOS
scratch RAM because the game's address space is full end to end, and drawing
the scene twice costs half the framerate at a 300% overclock.

Neither limit applies here. Native geometry and a host GPU make more than two
viewports a question of API rather than budget, and the SDK's `guest_alloc`
removes the memory problem outright.

The goal is **four-player split-screen**.

## Saves

When a mod is enabled and a memory card holds progress from unmodded play,
OpenPete warns at startup that saving will mix modded progress into that card.
Saving while playing co-op does exactly that. To keep an existing save
untouched, save your co-op game to the other memory card slot.

## Layout

```
mods/spyro-coop/   the mod: manifest and source
docs/PORTING.md    the plan, the blockers, and what each depends on
BUGS.md            what is wrong, what is missing, what is accepted
```

## Credits

- **[OpenPete](https://openpete.com/)**, the native PC port this is built on,
  and its author, who read this mod's source unprompted and has been generous
  with his time.
- **Spyromain**: [Spyro2x2](https://github.com/Spyromain/Spyro2x2), the
  reference implementation the PS1 version was ported from.
- **TheMobyCollective**: [spyro-1](https://github.com/TheMobyCollective/spyro-1)
  decompilation, which both OpenPete and the PS1 version are built on, and the
  source of nearly every struct and function name in either.
- The **Mod the Dragon** community.

## License

MIT, matching the PS1 version. See [LICENSE](LICENSE).

OpenPete itself is licensed PolyForm Noncommercial 1.0.0. This repository
contains no OpenPete code, only a mod that links against its published SDK
headers at build time.
