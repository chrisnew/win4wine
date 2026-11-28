# Plan: a working Recycle Bin (BUGS.md "Recycle bin not working")

Status (2026-11-28): done and on the EeePC (repo 05e7ff0). Pictures: [docs/showcase.md](../docs/showcase.md#recycle-bin).
Not checked: a USB stick (under [Risks](#risks)), items surviving a logoff on the EeePC (checked on the bench), and the
Office XP and Visual Studio 6 installs.

Goal: a file deleted in Explorer or on the desktop shows in the Recycle Bin, the icon turns full, and Restore and Empty work,
with no warning, on drive C: and drive H:.

## What the checks showed

- **On the EeePC** (read 2026-11-28, nothing changed) deletes do land in a bin: `C:\RECYCLER\S-1-5-21-0-0-0-1000\Dc1.doc`
  and `~/RECYCLER\S-1-5-21-0-0-0-1000\Dh1.lnk` (drive H: is the home folder). Both `INFO2` files, the bin's index, are
  **corrupt**: the 800-byte record was written at offset 0 instead of after the 20-byte header, and the header was then
  written over it. The record-size field reads `Desk` and `to B` (text from the record). The view finds no entries, so the bin
  looks empty.
- **On the bench**, the first delete through XP's shell32 (probe [tests/recycle.c](../tests/recycle.c)) shows
  **"The Recycle Bin on C:\ is corrupted. Do you want to empty the Recycle Bin for this drive?"**
- **The check behind the message** (relay trace of the probe): XP reads the owner and DACL of `C:\RECYCLER\<SID>`
  (`GetFileSecurityW`, `EqualSid`). It compares them with the DACL it gives the folder when it creates it: six ACEs, full
  access (`0x1F01FF`) plus an inherit-only `GENERIC_ALL` for the user, SYSTEM (S-1-5-18) and Administrators (S-1-5-32-544).
  Wine does not keep a file's DACL. It makes up two ACEs from the Unix mode, so the comparison always fails.
- It is **the same cause as Windows 7's warning**, which patch 0120 already fixes: in the wineserver it keeps a DACL that is
  set in the `user.wine.dacl` extended attribute and returns it when the security is read. 0120 only acts with
  `WIN4WINE_SHELL=win7`.
- **The fix, tried once**: the probe run in a wineserver started with `WIN4WINE_SHELL=win7` (on a fresh `C:\RECYCLER`) brings
  no warning. `INFO2` is sound (header, then records of 800 bytes), and the Recycle Bin folder lists both deleted files.
- Not yet explained: how exactly the corrupt `INFO2` on the EeePC came about (most likely XP's way of going on after the
  warning). With the DACL kept, that path is not taken, which the bench run shows. It is checked again in step 4.

## Decisions

- **Widen 0120 to the XP session** rather than writing a new patch: the condition becomes "`WIN4WINE_EXTERNAL_SHELL` set and
  the shell is XP or Windows 7" (`WIN4WINE_SHELL` unset or `win7`; ReactOS stays as it is). The patch is renamed to
  `0120-server-external-stored-dacl.patch` and its header names the XP check as well. Nothing goes upstream, so changing the
  patch in place is the clean way.
- **No change to XP's binaries or to the bin's format.** XP's own bitbucket code keeps doing the work (`RECYCLER`, `INFO2`,
  `Dc<n>` names).
- What 0120 changes beyond the bin: every program in the XP session that creates a file or folder with an explicit DACL, or
  sets one, now reads that DACL back, as on Windows. The Unix mode is set as before. Files created without a security
  descriptor (almost all) are not touched and get no extended attribute.

## The existing bins on the EeePC

The folders XP made so far carry no stored DACL, so after the fix XP still shows the warning once per drive. Answering Yes
empties that drive's bin and makes the folder again, this time with the DACL kept. **Decided (2026-11-28): A**, done.

| | What happens | Lost |
|---|---|---|
| **A (recommended)** | Before the first logon with the fix, remove `~/win4wine/.local/prefix/drive_c/RECYCLER` and `~/RECYCLER` on the EeePC (after asking, with a LOG.md entry) | `New Microsoft Word Document.doc` (10 KB) and `Shortcut to Book1.lnk`, unless they are copied out first |
| **B** | Leave them; answer Yes to the warning once per drive | the same, and the warning shows up once in the session |
| **C** | `prefix/stage.py` writes the expected DACL into `user.wine.dacl` of existing `RECYCLER\<SID>` folders before each session | nothing, but the corrupt `INFO2` stays corrupt, so the two old items still don't show |

## H:\RECYCLER in the home folder

H: is the home folder and Wine reports it as a fixed drive, so XP keeps a bin there: `~/RECYCLER` is visible in Linux file
managers and in `ls ~`. That is how XP works, and it keeps deletes in My Documents (which is `H:\`) quick, with no copy
across file systems. Taken as recommended: **leave it**, noted in [docs/architecture.md](../docs/architecture.md). If it
bothers the visitors' view of the home folder, option B or C of the XDG folders plan moves My Documents to C:.

## What the bench showed

- Probe, headless, in a wineserver with only `WIN4WINE_EXTERNAL_SHELL=1`: two deletes on C: bring no warning, `INFO2` is sound
  and both are listed; the same on H:. On `Z:\tmp` no bin can be made: the file is deleted for good (the probe asks for no
  confirmation), and Explorer there asks "Are you sure you want to delete", not "send to the Recycle Bin".
- In the session: deletes from the desktop and My Documents are listed with original location and date and survive a restart
  of the session; Restore this item, Restore all items, Empty (menu, task and icon) work; Shift+Delete asks to delete.
- **Found in step 4**: the prefix lacked XP's registry entries for the bin (no Empty Recycle Bin on the icon's menu, a dead
  task link, no settings under Properties, an icon that never changed). Added to `prefix/registry.py` (profile 27), which the
  session's staging applies at the next start.
- With bins removed by hand, the icon stays as it was until the next logon, delete or Empty.

## What the EeePC showed (2026-11-28)

- Deletes from the desktop and from H:\ show in the bin with original location and date, Restore all items brings both back,
  Empty on the icon's menu removes them, and the icon follows. Both new bins carry the stored access list.
- Step 5, measured differently: instead of timing Explorer, [tests/security-time.c](../tests/security-time.c) reads the
  security of all of `system32` with and without 0120 acting: about 19 µs (3%) more per read
  ([numbers](../tools/eeepc/README.md#recycle-bin)).
- Seen again, not part of this fix: My Documents on the desktop does not open (an older bug, in the local BUGS.md).

## Steps

1. **Probe** (done, uncommitted): [tests/recycle.c](../tests/recycle.c), built by `tests/build.sh` as `recycle-test.exe`. Run
   as `startmenu.exe` to get XP's shell32. It deletes a file to the bin, checks `INFO2` and counts the items the Recycle Bin
   folder lists. Exit code 0 when both are right. Add a line on it to the `bench` skill.
2. **Wine 0120** (skill `wine-patch`): change `stored_dacl_enabled()` in `server/fd.c` as decided above, rename the patch,
   rewrite its header (Purpose, Windows behaviour for XP and 7, Evidence: this plan's trace and probe), and in
   [docs/patches.md](../docs/patches.md) move the row out of "The Windows 7 shell session" to the XP session's patches with
   opt-in "yes (XP and Windows 7 sessions)", and fix the opt-in list at the top (lines 25–34). Build with
   `wine/container-build.sh`.
3. **Bench, headless**: on a prefix without `C:\RECYCLER`, run the probe twice in a wineserver started **only** with
   `WIN4WINE_EXTERNAL_SHELL=1`: exit code 0, no warning, two items listed. Also run it with `Z:\tmp` as the folder (Z: is
   `/`, where `RECYCLER` cannot be made) and with `H:\`, and write down what XP does on each.
4. **Bench, in the session** (skill `bench`): delete a file on the desktop and one in My Documents (H:). Check that the
   desktop icon turns full, that the bin window lists both with their original folder and date, that Restore puts a file back,
   and that Empty Recycle Bin removes the files and turns the icon empty. Also check Shift+Delete (no bin) and
   that the items survive a logoff. Take the screenshots (skill `screenshots`): a full bin window and the full desktop icon,
   with a section in [docs/showcase.md](../docs/showcase.md) linked from here.
5. **Cost on the Atom**: reading a file's security now also reads an extended attribute. Time opening a big folder
   (`C:\windows\system32`) in Explorer before and after on the EeePC, and keep the numbers in
   [tools/eeepc/README.md](../tools/eeepc/README.md).
6. **EeePC** (after asking): `container-build.sh --deploy`, the decision on the old bins, log out and in, repeat step 4 there,
   replace the bench screenshots with EeePC ones, and write the [tools/eeepc/LOG.md](../tools/eeepc/LOG.md) entry (the new Wine,
   the removed folders; undo: the previous Wine install).
7. **Commit**: `Wine 0120: keep DACLs in the XP session too, so the Recycle Bin works`, with BUGS.md's entry set to fixed in
   the same commit, the probe, the docs and the pictures.

## Risks

- **File systems without user xattrs** (FAT USB sticks, some network mounts): 0120 falls back to the made-up DACL, so XP
  would call a bin there corrupted. XP only keeps bins on fixed drives, so sticks and discs should not be affected. Check with
  a USB stick in step 6.
- **Programs that set restrictive DACLs** now get them back, as on Windows. They could lock themselves out of a file where Wine
  used to be lenient. Watch the Office XP and Visual Studio 6 installs on the EeePC once after the change.
- **Copying files out of the prefix** (backups, `cp` without `--xattrs`) loses the stored DACL, and the bin then shows the
  warning once. That is harmless: Yes rebuilds it.

## Docs to update

[docs/patches.md](../docs/patches.md) (step 2), [docs/architecture.md](../docs/architecture.md) (a short Recycle Bin section:
XP's bitbucket, 0120, `RECYCLER` on C: and H:), [BUGS.md](../BUGS.md), and the `bench` skill (the probe).
