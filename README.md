# Spyro Co-Op, on OpenPete

Split-screen co-op for **Spyro the Dragon**, built as a mod for
[OpenPete](https://openpete.com/), the native PC port of the game.

**Status: early. This does not play yet.** What is here is a probe mod that
establishes whether the engine can feed a second controller, which is the one
thing that gates everything else. See [docs/PORTING.md](docs/PORTING.md) for
the plan and the honest state of each blocker.

## Why this repository exists

There is already a finished version of this mod for the original PlayStation:
**[Spyro-1-Co-Op-Mod](https://github.com/Tokatta007/Spyro-1-Co-Op-Mod)**. It
works, it ships as an `.xdelta` patch, and it does two-player split-screen on
real hardware. It is also at the end of what the console can give. The code
lives in about 11 KB of BIOS scratch RAM because the game's address space is
full end to end, and drawing the scene twice already costs half the framerate
at a 300% overclock.

OpenPete removes both walls. Native geometry and a host GPU make more than two
viewports a question of API rather than budget, and the SDK's `guest_alloc`
makes the memory problem disappear entirely. So the PS1 version is now the
proof of concept and the reference implementation, and this is where the work
continues.

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
