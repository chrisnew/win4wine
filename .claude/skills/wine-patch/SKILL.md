---
name: wine-patch
description: Change Wine for LunaticOS, either a new patch in wine/patches/ or a fix to an existing one. Covers editing the patched source, compiling one module in seconds, writing the patch and its docs, building the install in Docker and testing it. Use for any change to Wine's behaviour.
---

# Changing Wine

Rules first. They come from upstream and stay:
- Every Wine change ships as a patch in `wine/patches/`, never as an edit in `.local/wine-upstream`. The format, the order of
  the groups, the opt-in (`win4wine_external_shell()`) and the WoW64 rules are in [docs/patches.md](../../../docs/patches.md).
  Read its "Format" and "Adding a patch" sections before writing one.
- A fix to what an existing patch does goes into that patch. A new patch goes where it fits in its group (renumber the later
  ones), not at the end.
- Microsoft binaries are never disassembled. Find out XP's behaviour from traces of public APIs, public docs and probes
  (`tests/*.c`).

## The loop

`.local/wine-upstream` holds every patch as uncommitted changes, and `.local/wine-build` is its build. Both are made by
`wine/container-build.sh` (Debian 13 in Docker; the first build takes long).

```sh
wine/patch-edit.py sync                      # the source matches the series (fails if not: see below)
wine/patch-edit.py mark                      # the base of the change
$EDITOR .local/wine-upstream/dlls/...        # a new file: git -C .local/wine-upstream add -N FILE
wine/patch-edit.py make dlls/user32/all      # compiles and links both architectures, in seconds
wine/patch-edit.py diff                      # the change so far
wine/patch-edit.py fold 0079                 # into an existing patch: its header stays, the diffstat is redone
# or: wine/patch-edit.py new 0166-user32-what.patch --after 0165   (writes a header to fill in, adds it to series)
wine/patch-edit.py sync                      # checks that the series gives the edited files, records it
```

- `make` targets are Wine's: `dlls/NAME/all`, `programs/NAME/all`, `server/all`, `loader/all`.
- `fold` and `new` rebuild the patch from upstream's files and the patches before it. If a later patch touches the same lines
  they refuse: the change then belongs in that later patch.
- After `sync`, `wine/container-build.sh` recompiles only what changed: 22 s for one changed file (2026-11-27). After a
  change to `wine/patches/` without `sync` it starts from upstream again (about 15 minutes).
- `container-build.sh` may also run between `mark` and `fold`: the series is unchanged then, so it builds the edited source
  as it is, and the bench can try the change before it is a patch.
- Then the docs, in the same commit: the patch's header (Purpose, Wine before, Change, Evidence, Scope), its row in the table
  of [docs/patches.md](../../../docs/patches.md), the count at the top ("holds N patches") and the opt-in ranges if it needs
  none. Renumbering also means fixing the numbers cited elsewhere (`grep -rn 0123 docs plans tests`).
- `python3 wine/patch-state.py SOURCE wine/patches --apply` against an unmodified Wine checks the whole series. After adding
  files or reordering, build once from configure in a fresh source and build directory (docs/development.md).

## Testing

- A probe in `tests/` (a small Win32 program that prints PASS, 32-bit and 64-bit) is the evidence a patch cites. Write it
  so it also runs on a real XP. Build and run it in the bench (`bench` skill, "Win32 probes").
- The bench sees a new `.local/wine-install` at once: `bench.sh stop`, `wine/container-build.sh`, `bench.sh start`.
- On the EeePC: `wine/container-build.sh --deploy HOST` with the user logged off (`eeepc` skill), then the prefix additions,
  and a `LOG.md` entry.

## Committing

One commit per patch, subject `Wine NNNN: what changes now` (the effect a user sees), and a body with the cause, the fix and the
measurement. The patch, `series`, `docs/patches.md` and any probe go in together.
