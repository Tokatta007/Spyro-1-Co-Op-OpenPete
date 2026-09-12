# Spyro Co-Op, on OpenPete

Split-screen co-op for **Spyro the Dragon**, built as a mod for
[OpenPete](https://openpete.com/), the native PC port of the game.

**Status: early. This does not play yet.** A second Spyro runs alongside the
first, but he borrows player 1's controls and is not drawn yet. See
[docs/PORTING.md](docs/PORTING.md) for the blockers and
[docs/PORT-INVENTORY.md](docs/PORT-INVENTORY.md) for the plan.

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

## Layout

```
mods/spyro-coop/   the mod: manifest and source
docs/PORTING.md    the plan, the blockers, and what each depends on
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
