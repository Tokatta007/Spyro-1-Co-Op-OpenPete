# Spyro Co-Op on OpenPete — working notes

Started 2026-09-11, when the project moved from a Mac laptop to a Windows PC
and OpenPete v0.3.0 was installed. This file is the working log: how things
were found, what was tried, and what failed. Read `docs/PORTING.md` for the
plan and the blockers.

---

## Who you're working with

The user is **new to coding**. Please:

- Explain commands before running them, in plain language.
- Walk through things step by step. One stage at a time, not a wall of steps.
- Don't assume familiarity with git, compilers, or build systems.
- When something fails, explain what the error means before proposing a fix.
- Never dump a long script with no explanation.
- Asking "basic" questions is expected and fine. Answer them properly.

They have a little terminal experience and are **on Windows 11, using
PowerShell**. Use PowerShell commands, not bash: `Get-FileHash` rather than
`shasum`, `winget` rather than Homebrew. PowerShell 5.1 has no `&&`, no
ternary, and no `??` — chain with `;` and `if ($?)`.

**This is a change from the PS1 project**, which was built entirely on macOS.
Its `CLAUDE.md` still contains macOS toolchain notes; they are history now, not
instructions.

## Environment, as of 2026-09-11

```
D:\Games\OpenPete\                     the engine, kept at a short path
                                       on purpose (its README warns that deep
                                       folders hit Windows' path limit during
                                       first run and mod compilation)
  mods\spyro-coop  --junction-->       this repo's mods\spyro-coop
  sdk\                                 headers, docs\index.html, examples\
  logs\openpete.log                    every run mirrors diagnostics here
  library\<hash>\                       ingested disc + compiled module

D:\Documents\!Video Game Stuff\Spyro 1 Modding\
  Spyro-1-CoOp-OpenPete\               this repo
  Spyro-1-CoOp-PS1\                    the finished PS1 mod; reference now
  shared\Roms\                         disc images, shared by both projects
```

Installed 2026-09-11: Git 2.55.0.3, GitHub CLI 2.100.0. Python 3.14.6 was
already present. Docker, make and a MIPS cross-compiler are **not** installed
and are only needed to rebuild the PS1 version.

Both repos have `core.autocrlf false` and `core.filemode false` set. Do not
change these. Without the first, Windows rewrites every line ending in a repo
authored on macOS; without the second, every shell script shows as modified
because Windows cannot represent the executable bit.

## There is no build step

OpenPete compiles `mods/spyro-coop/src/*.c` at startup against the `sdk/`
folder beside the executable. The loop is: edit, relaunch OpenPete, read
`D:\Games\OpenPete\logs\openpete.log`. A start with mods to compile shows
"COMPILING MODS" on the splash. A compile error appears in that log.

The mod also publishes live status into the Mods panel: Escape, then Mods.

## The move from macOS, 2026-09-11 — what it cost

Copying the project from the Mac brought **4,243 AppleDouble metadata files**
(`._*` and `.DS_Store`). 615 of them landed inside `.git` directories, and two
in `.git/objects/pack/` made git print `error: non-monotonic index` on every
single command, because git tried to read `._pack-*.idx` as a pack index.

Deleting them fixed it. All 4,243 were verified as genuine AppleDouble files
(magic bytes `00 05 16 07`, max 4 KB) before deletion, not by name alone.

Both `.gitignore` files now carry `._*`. If anything is ever copied from a Mac
again, check for these first — the symptom looks like repository corruption
and is not.

## Open questions, in priority order

These are tracked properly in `docs/PORTING.md` §3. In short:

1. **Is the second controller's guest pad buffer fed?** `mods/spyro-coop` is a
   read-only probe that answers this. Nothing else proceeds until it reads
   "IS fed". Run with two pads, press something on the second.
2. **Does the engine tolerate running the tick twice per frame?** Determinism
   is its headline promise. The manifest declares the finished mod's contract
   rather than the probe's, so a refusal surfaces early.
3. **Can a mod drive two scene builds through `api->call`?**
4. **Per-pass rendering** — not ours, no date. Four-player depends on it.

## Rules

- **Never commit ROMs, .bin, .cue, or extracted game assets.** Source and
  manifests only. Check `.gitignore` before adding files.
- `docs/private/` is gitignored and holds correspondence. **Nothing from it
  goes into a public file unless the other party has said it publicly
  themselves.** That includes undocumented command-line flags he mentioned in
  private and anything about his roadmap or unreleased work — let him announce
  it. The specifics are listed in the private notes, not restated here, because
  this file is itself public.
- **This nearly went wrong on 2026-09-11.** The first draft of `docs/PORTING.md`
  quoted his private statements, named an undocumented flag, and described his
  roadmap, and was one command away from being pushed to a public repository. It
  was caught by `git grep`-ing the staged file list before the first push, not
  by remembering the rule. **Run that check before any first push of a new
  repo**, and prefer citing our own measurements over anything he said in
  private — the measurements are stronger evidence anyway.
- Spyromain's code is MIT — preserve attribution in any derived file.
- OpenPete is PolyForm Noncommercial. This repo contains none of its code, only
  a mod compiled against its published SDK headers. Keep it that way.

## Reference

- PS1 version: `../Spyro-1-CoOp-PS1` — `CHANGES.md` is the canonical record of
  every hook, memory allocation and swapped per-player region; `CLAUDE.md` is
  the investigation log. Read both before reimplementing anything.
- SDK docs: `D:\Games\OpenPete\sdk\docs\index.html`, start at "Getting started"
- SDK examples: `D:\Games\OpenPete\sdk\examples\` — `hello-anchor` is the
  minimal override, `hud-stopwatch` shows the tick/present split, `natural-gems`
  shows a post-hook.
- Spyro 1 decomp: https://github.com/TheMobyCollective/spyro-1
- Community: "Mod the Dragon" Discord
