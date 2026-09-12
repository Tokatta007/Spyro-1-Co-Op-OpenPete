# Spyro Co-Op, on OpenPete

Split-screen co-op for **Spyro the Dragon**, built as a mod for
[OpenPete](https://openpete.com/) — the native PC port of the game.

**Status: early. This does not play yet.** What is here is a probe mod that
establishes whether the engine can feed a second controller, which is the one
thing that gates everything else. See [docs/PORTING.md](docs/PORTING.md) for
the plan and the honest state of each blocker.

## Why this repository exists

There is already a finished version of this mod for the original PlayStation:
**[Spyro-1-Co-Op-Mod](https://github.com/Tokatta007/Spyro-1-Co-Op-Mod)**. It
works, it ships as an `.xdelta` patch, and it does two-player split-screen on
real hardware. It is also at the end of what the console can give: the code
lives in about 11 KB of BIOS scratch RAM because the game's address space is
full end to end, and drawing the scene twice already costs half the framerate
at a 300% overclock.

OpenPete removes both walls. Native geometry and a host GPU make more than two
viewports a question of API rather than budget, and the SDK's `guest_alloc`
makes the memory problem disappear entirely. So the PS1 version is now the
proof of concept and the reference implementation, and this is where the work
continues.

The goal is **four-player split-screen**.

## Why a separate repository rather than a branch

Almost nothing ports at the file level. What carries over is the *design* —
the per-player state model, the swap tables, moby ownership with its
hysteresis, handover, individual death and respawn, Sparx, dual pad polling.
What does not carry over is every PS1-specific thing the other repository is
mostly made of: `DRAWENV`/`DISPENV`, the ordering table, the GTE view-matrix
squash, the entry-patch technique, the boot payload, and the byte-golf that
paid for all of it.

The build systems share nothing either. That one compiles MIPS with a
cross-compiler and repacks a disc image; this one is C compiled at startup by
the engine against `sdk/`.

## Layout

```
mods/spyro-coop/   the mod: manifest and source
docs/PORTING.md    the plan, the blockers, and what each depends on
docs/private/      correspondence and notes not ours to publish (untracked)
```

## Building and running

There is no build step. OpenPete compiles `src/*.c` at startup against the
`sdk/` folder beside the executable, so the loop is: edit, relaunch, read the
log.

The mod folder is linked into the OpenPete install with a directory junction,
so the source is edited here and runs there with nothing to copy:

```
D:\Games\OpenPete\mods\spyro-coop  ->  this repo's mods\spyro-coop
```

Recreate that link after reinstalling OpenPete:

```powershell
New-Item -ItemType Junction -Path "D:\Games\OpenPete\mods\spyro-coop" -Target "<this repo>\mods\spyro-coop"
```

Diagnostics land in `D:\Games\OpenPete\logs\openpete.log`, and the mod also
publishes live status into the Mods panel (Escape, then Mods).

## Credits

- **The OpenPete author**, for the port itself, for reading this mod's source
  unprompted, and for scoping a multi-pane renderer API partly to support this
  use case.
- **Spyromain**: [Spyro2x2](https://github.com/Spyromain/Spyro2x2), the
  reference implementation the PS1 version was ported from.
- **TheMobyCollective**: [spyro-1](https://github.com/TheMobyCollective/spyro-1)
  decompilation, which both OpenPete and the PS1 version are built on, and the
  source of nearly every struct and function name in either.
- The **Mod the Dragon** community.

## License

MIT, matching the PS1 version. See [LICENSE](LICENSE).

OpenPete itself is licensed PolyForm Noncommercial 1.0.0; this repository
contains no OpenPete code, only a mod that links against its published SDK
headers at build time.
