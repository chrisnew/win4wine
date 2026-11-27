# Plan: "Open in Linux Terminal" on folders

Status (2026-11-27): phases 1 and 2 built and checked in the session in Docker (Xvfb, 1024x600); deployed to the EeePC the same
day, not yet checked in a session there (phase 1 step 5). Phase 3 is not started. Found there, and different from what is written below:

- **Environment:** only `WINEDEBUG` and `FONTCONFIG_FILE` are dropped. Without `WIN4WINE_EXTERNAL_SHELL` and `WINEDLLOVERRIDES`,
  `wine notepad` gets a broken frame, and Linux programs do not read them.
- **More folders get the message box:** names ending with a dot (or a path ending with a space), which Wine strips from the
  working directory, and folders that may not be entered, where Wine starts the program in its own directory.
- **`Directory\shell` and `Drive\shell`** did not exist in the prefix, so `extras.py` sets their default verb to `none`.
- A name with `"` shows in Explorer but cannot be opened by Windows programs: message box.
- **XP asks `Directory\Background` handlers**, in a folder window and on the desktop, so the static verbs are gone.
- **The File menu** with a folder selected asks the handler twice, into one menu: it adds nothing the second time.
- **The handler also opens the drive's root** (to find its Linux path, for the `gvfs` rule), besides `GetFileAttributesW`.
- A drive root that XP covers with "These files are hidden" has no background menu until its contents are shown.

Goal: in XP's Explorer, right-clicking a folder or a drive that exists on the Linux file system offers **Open in Linux
Terminal**. It opens a Linux terminal (xterm on the EeePC) in that folder, with an XP frame and a taskbar button from the WM. A
visitor goes to `Z:\usr\share` in XP's Explorer, right-clicks `doc`, and types `ls` in a terminal that is already there. It must
open quickly on the Atom. A folder with an odd name (spaces, quotes, `$`, non-ASCII) must never be run through a shell. Folders
that have no Linux path (`\\server\share`, Control Panel) must not show a terminal that ends up in the wrong directory.

## What exists

- **`linux-launch.exe`** ([session/linux-launch.c](../session/linux-launch.c)) already starts Linux programs from XP: a
  GUI-subsystem program (so no console opens), which `CreateProcess`es `Z:\usr\bin\env -u WINEDEBUG PROGRAM ...`. Wine runs a
  Unix executable named in `CreateProcess` natively. The Start menu's Linux shortcuts and My Network Places use it. Today the
  session modules copy it into `C:\xp\linux-apps` and `C:\xp\network-places`, but not into `C:\xp\bin`.
- **The working directory carries over**: ntdll's `fork_and_exec` (`dlls/ntdll/unix/process.c`) `fchdir`s to the Unix form of
  `lpCurrentDirectory` before `execv`. So the folder reaches the terminal as its working directory, not as text on a command
  line, and no quoting can break it.
- **The environment is the calling Wine process's Unix `environ`** (`execv`, with no `envp`), so the shell inherits the session's
  variables: `WINEPREFIX`, the `PATH` with the session's Wine first (`session/wineenv.py`), `WIN4WINE_*`, and
  `FONTCONFIG_FILE` (the session's font configuration, `supervisor.py`).
- **The terminal**: `linux_apps.py` already uses `x-terminal-emulator`, or `xterm` if it is missing. On the EeePC,
  `x-terminal-emulator` is `lxterm`, which is xterm (checked 2026-11-27). xterm starts its shell in its own working directory.
  gnome-terminal would not (its server starts the shell), but the EeePC does not have it.
- **XP's shell32 is native in Explorer**, so XP's own static verbs (`HKCR\<class>\shell\<verb>\command`) and context menu
  handlers (`HKCR\<class>\shellex\ContextMenuHandlers`) work as on XP. `Directory` is the class of file system folders, and
  `Drive` the class of drive roots. XP has no `AppliesTo` and no `Directory\Background\shell` (those came with Vista), so a static
  verb cannot be hidden for some folders, and it cannot appear on a folder window's background.
- `prefix/extras.py` installs the XP Explorer helpers into `C:\xp\bin` (`HELPERS`) and writes their registry keys
  (`VERSION` 10 now).

## Decisions

- **Phase 1 uses a static verb, phase 2 a context menu handler.** The verb needs only the registry and a new option for
  `linux-launch.exe`, and it covers the common case: right-clicking a folder or drive in a folder window, the Folders tree or the
  desktop, and the File menu while a folder is selected. The handler (phase 2) adds what a static verb cannot: the folder
  window's background ("here"), and hiding the item where there is no Linux path.
- **The folder is passed only as the working directory.** `linux-launch.exe --terminal "%1"` resolves the path itself and passes
  it to `CreateProcess` as `lpCurrentDirectory`. The terminal's command line is fixed, so the name never goes through a shell or
  through `-e`.
- **A "real folder" is one that `wine_get_unix_file_name` maps to an existing Unix directory.** That covers `C:` (the prefix's
  `drive_c`), `H:`, `Z:` and a mounted CD. Anything else (UNC paths, a path that is gone) gets an XP-style message box:
  "*Folder* is not on the Linux file system, so it cannot be opened in a Linux terminal." No terminal opens.
- **The environment is cleaned for a Linux user, but Wine stays usable**: drop `WINEDEBUG`, `FONTCONFIG_FILE`, `WIN4WINE_*` and
  `WINEDLLOVERRIDES`; keep `WINEPREFIX` and the `PATH` with the session's Wine. Then `wine notepad` typed in the terminal opens
  Notepad in the XP session with an XP frame, which is worth showing too. Step 4 of phase 1 checks the list with a real `env`
  dump.
- **The terminal**: `x-terminal-emulator`, or `xterm` if `Z:\usr\bin\x-terminal-emulator` is missing (as `linux_apps.py`
  does). Terminals that ignore their working directory are out of scope until one is installed.
- **Title**: "Open in Linux &Terminal", with T as the access key (unused in XP's folder menu: Open, Explore, Search, Sharing
  and Security...).

## Phase 1: the static verb

1. **`linux-launch.exe --terminal PATH`** (`session/linux-launch.c`):
   - Take PATH from the rest of the command line by hand, and strip the outer quotes. `CommandLineToArgvW` would read `"C:\"` (a
     drive root through `"%1"`) as an escaped quote.
   - `GetFullPathNameW`, then `wine_get_unix_file_name` (from kernel32 through `GetProcAddress`, `HeapFree` the result), then
     `GetFileAttributesW` on the Windows path: it must be a directory. Otherwise show the message box above and return.
   - `CreateProcessW(L"Z:\\usr\\bin\\env", L"/usr/bin/env -u WINEDEBUG -u FONTCONFIG_FILE ... x-terminal-emulator", ...,
     lpCurrentDirectory = PATH)`. `env -u` takes one name at a time, so the `WIN4WINE_*` names are listed one by one (from
     `wineenv.py` and `supervisor.py`).
   - Keep the existing error box if `CreateProcess` fails.
2. **Install** (`prefix/extras.py`, `VERSION` 11):
   - Add `linux-launch.exe` to `HELPERS` (into `C:\xp\bin`). The session modules keep their own copies, because their shortcuts
     point there.
   - For `Directory` and `Drive`: `HKCR\<class>\shell\linuxterminal` = `Open in Linux &Terminal`, and
     `...\linuxterminal\command` = `"C:\xp\bin\linux-launch.exe" --terminal "%1"`.
3. **Build**: nothing new. `bridge-wm/build.sh` already builds `linux-launch.exe`.
4. **Test in the session in Docker with Xvfb** at 1024x600, as for the Linux columns, before the EeePC:
   - Right-click `C:\`, `Z:\usr\bin`, `H:\`, `C:\Documents and Settings`, and a folder named `it's $HOME & "ünï" (1)`. Each
     terminal's `pwd` must match. Use the Folders tree, the desktop and the File menu too.
   - The verb is absent on My Computer, Control Panel, Recycle Bin and My Network Places' items (not `Directory`). A UNC folder
     gives the message box.
   - Run `env | sort` in the terminal, settle the list of variables to drop, and check that `wine notepad` opens in the session.
   - The xterm gets an XP frame, a taskbar button and the focus, and comes to the front above Explorer.
5. **On the EeePC** (after asking; `extras.py` reruns for the prefix, so it goes in `tools/eeepc/LOG.md`): the same checks, plus
   the time from the click to the terminal's first prompt. Add that number to `tools/eeepc/README.md`.

## Phase 2: a context menu handler

1. **`IShellExtInit` + `IContextMenu` in `linuxcols.dll`** (Explorer already loads it, so no new DLL to load), as a second class
   with its own CLSID:
   - `Initialize(pidlFolder, pdtobj)`: from the selection (one folder, `CF_HDROP`), or from `pidlFolder` when the menu is for the
     background. Keep the path only if `wine_get_unix_file_name` maps it and it is a directory. Otherwise `QueryContextMenu`
     adds nothing.
   - `QueryContextMenu`: one item. `InvokeCommand` starts `C:\xp\bin\linux-launch.exe --terminal "PATH"`, so the launching code
     stays in one place. `GetCommandString` gives the verb `linuxterminal` and a status bar text ("Opens a Linux terminal in this
     folder.").
   - Registered under `Directory\shellex\ContextMenuHandlers`, `Drive\shellex\...` and `Directory\Background\shellex\...`, plus
     the `Approved` list. The static verbs from phase 1 are removed in the same `extras.py` version, so the item never shows twice.
2. **Check first** that XP's shell32 under Wine asks `Directory\Background` handlers, both in a folder window and on the
   desktop. Drivers of the XP era used that key (graphics properties on the desktop), but nothing in win4wine has relied on it
   yet. If it does not work, keep the static verbs and drop the background item.
3. Debug output behind the same registry value as the columns, to see when XP creates the handler (it must not slow down every
   right-click: the handler does no file access in `Initialize` beyond one `GetFileAttributesW`).

## Phase 3: optional, for the demo

- **"Open Command Window Here"** next to it (XP's PowerToy of the same name, `cmd.exe /k cd /d "%1"`), so the two shells can
  be shown side by side.
- A terminal icon for xterm's taskbar button, if `bridge-wm/icons.py` has none for its class.

## Risks

- **Focus**: a Linux window started from a Wine process may open behind Explorer, or without the focus, on the WM's rules for
  new windows. Check this in phase 1 step 4. If it happens, it is a WM fix (in `wm.c` and `wm.py`), not a launcher one.
- **The session's environment leaking**: a variable we forget can change how Linux programs in that shell behave. The `env`
  dump in step 4 is the check. Any variable the session adds later must be added to the list.
- **Slow mounts**: `GetFileAttributesW` on a hung network mount blocks the right-click. Only phase 2 runs it in the menu, so
  skip `/run/user/*/gvfs` there, as the column handler does.

## Docs to update

- Phase 1: [docs/architecture.md](../docs/architecture.md) (`linux-launch.exe --terminal`, and the verb that `extras.py`
  writes), and the IDEAS.md entry.
- Phase 2: the same section, and the `linuxcols.dll` paragraph (it now also holds the menu handler).
