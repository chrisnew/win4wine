# Plan: Linux columns in XP's folder windows

Status: phases 1 and 2, and the Details section of the task pane (asked for during the work), built and tested on the
development machine (2026-11-27, session in Docker with Xvfb at 1024x600). **Not yet on the EeePC**: the deployment, the
check there and the measurements (phase 2, steps 4 and 5) are open. How it works now:
[docs/architecture.md](../docs/architecture.md#linux-columns-in-xps-folder-windows-xp-explorerhelperslinuxcolsc).

## As built: where it differs from the plan

The sections after this one are the plan as written. These points turned out differently:

- The patch is **0160** (0158 and 0159 were taken in the meantime). Its system call has number 0x102, and the 32-bit
  `NtWow64*` calls after it moved up by one.
- **`wine_get_unix_file_name` does open the file** (`NtOpenFile`, then the server's name for the handle), so the handler calls
  it only on a drive's root, which gives the resolved directory, and appends the rest. The server opens with `O_NONBLOCK`, so
  a FIFO would not block it as the plan says; devices and hung mounts remain the reason not to open items.
- A name relative to a directory handle needs the Unix side to change into that directory (as directory listing does).
- Column widths are 20, 15, 15 and 40: at 10 and 8 the titles were cut off.
- `OutputDebugString` appears with `WINEDEBUG=warn+seh`, not `+debugstr`.
- **XP's background filling works**: the view's thread initializes the handler, another thread asks for the visible rows only,
  again after scrolling and F5. The fallback worker was not needed. F5 shows changed modes and groups.
- `Z:\dev`, `Z:\proc` and `Z:\sys` cannot be opened: Wine hides them from directory listings. FIFOs and sockets were checked in
  `/tmp`. Wine does not list dangling links either.
- **The Details section** (not in the plan): the value `Details` on `*` and `Directory`, `prop:...;{FMTID} 6;...`. XP's
  property syntax takes the ID after a space, and shows unknown properties without a name, hence four hidden columns whose
  values carry the name. Infotips (phase 3) can probably use the same.
- Sorting by a Linux column in a folder of 2000 files asked for all of them on the view's thread within 0.1 s here, with the
  debug output on. Nothing called the handler while the session was idle, and selecting all 2000 files asked for nothing.
- Not checked: the Details section in the Recycle Bin and in search results.

Goal: in XP's own folder windows (Details view), files and folders can show their Linux permissions, owner, group and path as extra
columns, chosen like any other column (right click on the header, or View → Choose Details). A visitor opens `Z:\usr\bin` in XP's
Explorer and sees `-rwsr-xr-x  root  root  /usr/bin/passwd`. On the EeePC a folder of 2000 files must still open as fast as without the
columns, and the columns must never be able to hang the session.

## What exists, and what is missing

- XP's shell32 is native in `xp-explorer.exe` (AppDefaults, [docs/architecture.md](../docs/architecture.md)), so XP's own folder view
  runs, including its column handlers. XP's file system folder loads every class listed under
  `HKCR\Folder\shellex\ColumnHandlers\{CLSID}` and asks it through `IColumnProvider` (public, documented in the Platform SDK's
  `shlobj.h`; MinGW-w64's `shlobj.h` has `IColumnProvider`, `SHCOLUMNINIT`, `SHCOLUMNINFO` and `SHCOLUMNDATA`). Wine's own shell32 does
  not use column handlers, so only XP's Explorer shows the columns. That is the window that matters for the demo.
- Paths: `wine_get_unix_file_name` (kernel32, Wine only) turns `C:\...` into the Unix path. In Wine 11 it is a call into ntdll's Unix
  side with no wineserver request.
- Mode, uid and gid: **no Win32 or NT call returns them in Wine 11.**
  - Windows 10 has one: `FileStatLxInformation` (class 70, `FILE_STAT_LX_INFORMATION` in the WDK's `ntifs.h`: the
    `FILE_STAT_INFORMATION` fields plus `LxFlags`, `LxUid`, `LxGid`, `LxMode`, `LxDeviceIdMajor`, `LxDeviceIdMinor`; the
    `LX_FILE_METADATA_HAS_UID/GID/MODE/DEVICE_ID` flags). WSL uses it. Wine lists the class (`include/winternl.h`) but leaves its size 0
    in `dlls/ntdll/unix/file.c`, so it is refused.
  - Asking for it through a handle (`NtCreateFile` + `NtQueryInformationFile`) **is not safe**: the wineserver itself `open()`s the
    file (`server/fd.c` `open_fd`, `O_RDONLY` even with only `FILE_READ_ATTRIBUTES`). On a FIFO (in `/run`, `/tmp`, `~/.cache`) that
    blocks the single-threaded wineserver, which freezes every Windows program in the session. On a device it can have side effects. It
    also costs a server request for every file.
  - Windows 10's `NtQueryInformationByName` (ntdll, 1709 and later; documented in the WDK) asks by path, for `FileStatInformation`,
    `FileStatLxInformation` and a few other classes. Wine has no such function. On Wine's Unix side it is a path conversion
    (`nt_to_unix_file_name`) plus `lstat()`: no file is opened and the wineserver is not involved.
- User and group names: `/etc/passwd` and `/etc/group` can be read from Windows code as `\\?\unix\etc\passwd` (Wine maps `\??\unix\`
  to the Unix root, with or without a `Z:` drive).
- Own Windows binaries are built on the EeePC with MinGW by `bridge-wm/build.sh` into `.local/bridge`. `prefix/extras.py` copies the
  XP Explorer helpers to `C:\xp\bin` and writes their 32-bit registrations (the XP shell is 32-bit, under WoW64).

## Decisions

- **The Wine side is `NtQueryInformationByName` with `FileStatInformation` and `FileStatLxInformation`**, a real (Windows 10) API with
  a documented structure. It is not a new Wine-only export. It needs no opt-in, because it is Windows behaviour that Wine lacks
  (handled like 0001–0011). Other classes return `STATUS_INVALID_INFO_CLASS`, as Windows does for classes it does not take by name.
- **Symlinks are not followed** (`lstat`): a link shows `l` in its mode and its own owner, like `ls -l`. This matches Windows, where
  `NtQueryInformationByName` reports the reparse point itself. A target column comes in phase 3.
- **The column handler only reads.** It does not change modes or owners (that would be phase 3's property page, if ever).
- **Columns** (one property set, our own FMTID):

  | Title | Content | Width (chars) |
  |---|---|---|
  | Linux Permissions | `ls -l` form: `drwxr-xr-x`, with `s`/`S`, `t`/`T`, and `l`, `p`, `c`, `b`, `s` for other types | 10 |
  | Linux Owner | user name, or the uid if there is no name | 8 |
  | Linux Group | group name, or the gid | 8 |
  | Linux Path | the Unix path from `wine_get_unix_file_name` | 40 |

  Owner and group are two columns rather than one `owner:group` column: each can be sorted, and each stays narrow on a 1024-pixel
  screen. Permissions are shown as text, not as octal (`0755`), because the text is what people recognize at a glance. An octal
  column would be a cheap fifth column if wanted.
- **The columns are filled in the background from the start** (`SHCOLSTATE_SLOW` on all four). This is XP's own mechanism for
  slow columns: the view shows the rows at once with the Linux columns empty, and its background task asks the handler for them and
  fills them in, visible rows first. Nothing in the handler waits on the UI thread, so a slow mount or a cold disk cache on the
  EeePC delays the columns, not the folder. Sorting by a Linux column still needs every value first, so that sort takes as long as
  the stats do (XP shows the wait cursor).
  - If XP's background task does not work for a third-party handler under Wine (see Risks), the fallback is still in the background,
    but done by the handler itself: `GetItemData` answers `S_FALSE` for values not yet known and queues the path for a worker thread
    in the DLL. When a value comes in, `SHChangeNotify(SHCNE_UPDATEITEM)` for that path makes the view ask again, and this time the
    value comes from the cache. The notifications are batched (one per folder every ~100 ms) so the view does not redraw for every
    file.
- **The handler degrades** on a Wine without the patch: it looks up `NtQueryInformationByName` with `GetProcAddress`. Without it, only
  Linux Path is filled in.

## Phase 1: `NtQueryInformationByName` in Wine (patch 0158)

1. **`wine/patches/0158-ntdll-query-information-by-name.patch`** (no opt-in):
   - `dlls/ntdll/ntdll.spec`: `NtQueryInformationByName` and `ZwQueryInformationByName`, with a free `-syscall=` number (check it
     against every number in the spec). Regenerate `dlls/ntdll/ntsyscalls.h` with `tools/make_specfiles` in the container and put the
     result in the patch.
   - `dlls/ntdll/unix/file.c`: `NtQueryInformationByName( attr, io, ptr, len, class )`. It checks the class and length,
     `nt_to_unix_file_name( attr, &unix_name, FILE_OPEN )`, then `lstat`, and fills the structure:
     - `FileStatInformation`: through the same helpers `NtQueryInformationFile` uses (`fill_file_info`, the attributes from
       `get_file_info`/`DOSATTRIB` as patch 0121 keeps them), so both calls give the same values.
     - `FileStatLxInformation`: the same fields, plus `LxUid`, `LxGid`, `LxMode` (the full `st_mode`, type bits included, as WSL
       does), `LxDeviceIdMajor/Minor` for devices, and `LxFlags` = `HAS_UID | HAS_GID | HAS_MODE` (`| HAS_DEVICE_ID` for devices).
     - Also give `FileStatLxInformation` its size in the `info_sizes` table, so `NtQueryInformationFile` checks the length the same
       way. Through a handle it can be filled from the `fstat` the function already does (cheap, and Windows has it too).
   - `include/winternl.h` (or `include/ddk/ntifs.h`, where the WDK has it): `FILE_STAT_LX_INFORMATION`, the `LX_FILE_*` flags and the
     prototype.
   - `dlls/wow64/file.c` + `dlls/wow64/syscall.h`: `wow64_NtQueryInformationByName` (convert `OBJECT_ATTRIBUTES32`, as
     `wow64_NtQueryFullAttributesFile` does). Both structures contain only fixed-size fields, so no layout conversion is needed.
2. Add it to `wine/patches/series` and to the patch table in [docs/patches.md](../docs/patches.md) as the last entry, with "It belongs
   with 0001–0011 and has not been renumbered yet", as 0155–0157 do.
3. Check: `wine/patch-state.py ... --apply` on a clean 11.0, and the host `gcc -fsyntax-only` check (CLAUDE.md) on `unix/file.c`
   and `wow64/file.c`. Then `container-build.sh` (ntdll's spec changed, so the build does more than the usual incremental rebuild),
   and `--deploy` **after asking**.
4. **Probe** `tests/stat-lx-probe.c` (MinGW, run with the session's Wine on the EeePC). It prints both classes for regular files, a
   directory, a symlink, a dangling symlink, a FIFO (`mkfifo`; it must return at once), `/dev/null`, a setuid binary
   (`/usr/bin/passwd`), a root-owned file, a file under `C:` (in the prefix), under `H:`, a name in the wrong case (Wine's
   case-insensitive lookup), a missing file (`STATUS_OBJECT_NAME_NOT_FOUND`), a too-short buffer and an unsupported class. Compare
   with `stat -c '%f %u %g %t %T'`.

## Phase 2: the column handler (`xp-explorer/helpers/linuxcols.c`)

1. **A 32-bit in-process COM server** `linuxcols.dll`: `DllGetClassObject`, `DllCanUnloadNow` (a `.def` file), one class factory,
   one object with `IColumnProvider`.
   - `Initialize(SHCOLUMNINIT)`: keep nothing but the folder path (empty for folders that are not on a drive → every `GetItemData`
     returns `S_FALSE`).
   - `GetColumnInfo(index)`: the four columns above (`VT_BSTR`, `LVCFMT_LEFT`, `SHCOLSTATE_TYPE_STR | SHCOLSTATE_SLOW`), `S_FALSE`
     after the last one. Titles in English (the EeePC's XP is English; Japanese strings can follow from `extract/media.py`'s
     language if ever needed).
   - `GetItemData(scid, SHCOLUMNDATA)`: `S_FALSE` for other FMTIDs. XP asks once per column and item, so the last item's result is
     cached (path → `FILE_STAT_LX_INFORMATION`), and one stat serves all three columns. `SHCDF_UPDATEITEM` drops the cache entry (the
     view asks again after F5 or a change notification).
   - The path goes in as `\??\` + the DOS path in `OBJECT_ATTRIBUTES` (what `RtlDosPathNameToNtPathName_U` gives).
   - Names: `/etc/passwd` and `/etc/group` read once into a uid/gid → name table, reread when their last write time changes
     (checked at most once per `Initialize`, so not per file). Users that only NSS knows (LDAP) show as numbers, which is fine on the
     EeePC.
   - Linux Path is computed only when that column is asked for (`wine_get_unix_file_name`, `HeapFree` of its result, UTF-8 → UTF-16).
   - Thread safety: with `SHCOLSTATE_SLOW`, XP's background task calls `GetItemData` from its own thread while the UI thread may call
     `Initialize` or `GetColumnInfo`. The cache and the name table sit behind one critical section, held only around lookups and
     inserts, never while a stat runs.
     `ThreadingModel` is `Apartment`, as for the other XP Explorer classes in `extras.py`.
   - Debug output with `OutputDebugStringW` behind a registry value, so `WINEDEBUG=+debugstr` shows which columns XP asks for
     and when (the way to study XP's calls without disassembling).
2. **Build**: in `bridge-wm/build.sh` next to the other helpers: `i686-w64-mingw32-gcc -shared ... linuxcols.def -lole32 -loleaut32
   -luuid`. Check whether the shared `compile()` helper's `-municode` works for a DLL; if not, call the compiler directly for it.
3. **Install** (`prefix/extras.py`, bump `VERSION` to 7):
   - Copy `linuxcols.dll` to `C:\xp\bin` (add it to `HELPERS`, or a separate list if `HELPERS` means programs).
   - 32-bit registry: `HKCR\CLSID\{ours}\InprocServer32` = `C:\xp\bin\linuxcols.dll` with `ThreadingModel=Apartment`,
     `HKCR\Folder\shellex\ColumnHandlers\{ours}`, and the class in `HKLM\...\Shell Extensions\Approved` (XP checks it only when
     `EnforceShellExtensionSecurity` is set, but it is where XP's own extensions are listed).
4. **On the EeePC** (after asking; the install reruns `extras.py` for the prefix, so it goes in `tools/eeepc/LOG.md`): open
   `Z:\usr\bin`, `Z:\dev`, `Z:\run\user\1000` (FIFOs and sockets), `C:\` and `H:\` in Details view, add the columns, sort by each one.
   Check first that the background filling works: the rows appear before the Linux columns, the columns fill in without clicks or
   scrolling, scrolling down fills the newly visible rows, and F5 fills them again. Use the debug output to see which thread asks.
   If it does not work, switch to the handler's own worker (Decisions) before going on.
5. **Measure** on the EeePC, with and without the columns: the time until `Z:\usr\bin` (about 2000 entries) shows its first rows and
   until the columns are filled, the CPU of `xp-explorer.exe` while they fill, and that nothing keeps waking up afterwards
   (`tools/eeepc/` has the helpers). Add the numbers to `tools/eeepc/`.

## Phase 3: optional, for the demo

- **Linux Link Target** column (needs `readlink`: one more class in the patch, or `FileStatLxInformation` with a Wine-only class
  next to it; decide when needed).
- **A "Linux" page in Properties** (`IShellPropSheetExt` on `*` and `Directory`, in the same DLL): mode as XP-style check boxes
  (read/write/execute for owner, group, others), owner and group, the Unix path with a "Copy" button, and the file system it is on.
  Read-only first. Changing the mode would also need a way to call `chmod`, which no Windows API offers.
- **The columns already on for the demo folders**: XP keeps view settings per folder in `ShellNoRoam\Bags`. The simplest way is to set
  them by hand on the EeePC once (View → Choose Details, then Folder Options → "Apply to All Folders"), rather than writing XP's binary
  bag format from `extras.py`.
- **Infotips**: check whether XP's `InfoTip` (`prop:` lists in `HKCR\<type>`) can name a column handler's columns. If it can, hovering
  over a file in `Z:\` shows its owner and mode.

## Tests

- `tests/stat-lx-probe.c` (phase 1) is kept as the patch's `Evidence:`.
- The mode formatting (`st_mode` → `drwxr-sr-t` and the rest) is a pure function in `linuxcols.c`. A host test compiles it with the
  host's gcc and checks the special bits and file types against `ls -l` output on the dev machine.

## Risks

- **The syscall number.** A number already used by another patch, or the wrong one in `ntsyscalls.h`, breaks ntdll in both 32-bit
  and 64-bit processes. The probe runs in both bitnesses.
- **XP's background filling under Wine.** It depends on XP's shell32 task scheduler and its messages back to the view, which no
  part of win4wine has used yet. If the columns stay empty, fill in only after a click, or flicker, use the handler's own worker
  thread with `SHChangeNotify` (Decisions). That is more code, but it is still asynchronous and does not depend on XP's scheduler.
- **Network and FUSE mounts** (gvfs under `/run/user/1000/gvfs`): `lstat` can be slow there. The handler skips paths under
  `/run/user/*/gvfs` and `/proc`, or the view must otherwise wait for a slow mount.

## Docs to update with each phase

- Phase 1: the patch table in [docs/patches.md](../docs/patches.md).
- Phase 2: [docs/architecture.md](../docs/architecture.md) (what `extras.py` installs, and that XP's Explorer has Linux columns),
  [docs/original-features.md](../docs/original-features.md) if it lists what the fork adds, and the IDEAS.md entry turned into a
  pointer to this plan (or removed once done).
