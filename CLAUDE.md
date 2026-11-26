# win4wine (EeePC fork)

A Windows XP shell on Linux: XP's own explorer.exe running on a patched Wine 11.0, with a WM and a proxy that give Linux windows
real XP frames and taskbar buttons. It is a personal fork of [mikuta0407/win4wine](https://github.com/mikuta0407/win4wine),
and it now goes its own way. **Nothing is sent upstream any more**, so there is no need to keep changes small for review or in
upstream's shape.

## The goal

A weird, Frankenstein-like OS on an old Asus EeePC 1015PEM, to show off at a **retro computing event by the end of 2026**.
What counts is that it looks and feels convincing **on that machine**: it starts reliably, responds quickly on an Atom N550,
fits a 1024x600 screen, and has things worth showing. Ideas for features are in [IDEAS.md](IDEAS.md) and known bugs in
[BUGS.md](BUGS.md). Work that only helps other setups (VMs, Wayland, HiDPI, Japanese XP) is low priority unless asked for.

## Where things are

- How it works: [docs/architecture.md](docs/architecture.md). Why: [docs/design.md](docs/design.md).
- The Wine patches, their opt-in, order, format and how to add one: [docs/patches.md](docs/patches.md).
- Building, tests and updating an install: [docs/development.md](docs/development.md).
- Records from upstream's development are in [docs/history/](docs/history/), **in Japanese**. Read them there when a design choice
  needs its background (the other docs cite their section names). Don't translate them unless asked.
- The EeePC itself, the helper tools for driving and measuring it, and the numbers measured so far: [tools/eeepc/](tools/eeepc/README.md).

## Rules kept from upstream

- Windows binaries are used as extracted; they are never modified. Differences are handled in Wine patches and in our own parts.
- Microsoft binaries are never disassembled. Behaviour is studied with traces of public APIs, public docs and our own probes.
- No Microsoft files, extracted files or prefixes in Git: they live in `localres/` and `.local/`. The pre-commit hook
  (`scripts/check-staged.py`, enabled with `git config core.hooksPath .githooks`) refuses them.
- Never ship a change made directly in `.local/wine-upstream`: every Wine change is a patch in `wine/patches/`, in the format and
  place [docs/patches.md](docs/patches.md) describes, and the patch table there is updated with it.

## Git

- Work on branch `eeepc`, one commit per fix, and push it to `origin` (the fork, `chrisnew/win4wine`). `upstream` is only a
  remote to read from; never push or open PRs there.
- Commit messages in English: `area: what changes now` as the subject (`wm:`, `proxy:`, `xptray:`, `supervisor:` …, or
  `Wine NNNN: …` for a new or changed patch), then a body that explains the cause, the fix and, where there is one, the measurement.
- When a bug in [BUGS.md](BUGS.md) is fixed, update its status in the same commit.

## The development machine (here) and the EeePC

- This machine has Docker but no Wine, Xvfb or MinGW, so the session can't run here.
- Wine is built here in a Debian 13 container: `wine/container-build.sh` (`--deploy HOST` copies the install to the EeePC's
  checkout). Builds after the first only recompile what changed patches touch.
- To check a Wine patch's syntax without MinGW, copy the patched tree and the EeePC's `wine-build/include` and
  `compile_commands.json`, and run the compile command with the host's `gcc -fsyntax-only` (add `-fshort-wchar`; drop `-mcx16
  -mcmodel=small` for PE files). It catches real errors, but it does not link.
- The EeePC runs the real session, from a checkout of branch `eeepc`. How to reach it is in `CLAUDE.local.md`.
  **It is the user's live machine: ask before changing anything on it**, and before killing processes there.
- On the EeePC: logs are in `~/.local/state/win4wine/`, the IPC directory is `/run/user/1000/win4wine-1000-*`, and
  `DISPLAY=:0 XAUTHORITY=~/.Xauthority xwd -root -silent` takes a screenshot (convert it here; `tools/eeepc/shot.py` also shows
  redirected windows).
- Deploying a commit: push to `origin`, then on the EeePC `git fetch fork && git checkout -B eeepc fork/eeepc`.
  - Changes to the session (the WM, supervisor.py) only take effect after logging out and in, because the session ends
    when the WM exits. The session runs the C WM (`bridge-wm/wm.c`, built by `bridge-wm/build.sh` as `.local/bridge/wm`);
    a WM change goes into both `wm.c` and `wm.py`, checked with `tests/wm_parity.py`.
  - To reload the proxy without a relogin: `pkill -x proxy.exe`. The supervisor then restarts the shell, **which ends every
    Windows program**, so check that none are open first. Never use `pkill -f` with a pattern that is also in the ssh command
    line: it kills the ssh shell.
  - Bridge changes need `bridge-wm/build.sh` there, and Wine changes need a new Wine install (`container-build.sh --deploy`).
