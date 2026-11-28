# Architecture

What LunaticOS is made of and how the parts connect. Why it was done this way is in [design.md](design.md).

## Overview

```text
Display manager (LightDM)
 └─ session/supervisor.py ............. starts and watches the session
      ├─ bridge-wm/wm.c (or wm.py) .... the X11 window manager (WM)
      ├─ bridge-wm/tray.py ............ broker for Linux tray icons
      ├─ fcitx5 ....................... Japanese input
      ├─ session/idle-watch.c ......... says when the user is idle (screen savers)
      ├─ greeter/xp-lock.py ........... the lock screen, while the screen is locked
      ├─ session/logind-watch.py ...... locks on logind's request and before sleep
      ├─ session/network_places.py .... devices on the local network in My Network Places
      ├─ proxy.exe (on Wine) .......... stand-ins for Linux windows, and their XP frames
      └─ xp-explorer.exe (on Wine) .... the XP shell (explorer.exe extracted from the ISO)
                    ↑ ↓  files in the IPC directory (WM ⇄ proxy ⇄ tray.py)
```

Preparation builds the following:

| Part | Role |
|---|---|
| `extract/extract.py` | Takes only the needed files from the ISO (or an XP Windows directory) into `.local/assets`. No network is used. `extract/media.py` decides XP's language (Japanese or English) from the language of shell32.dll's resources; for English XP it also extracts the fonts only the English edition has. XP in other languages is treated as English (untested). |
| `wine/build.sh` | Fetches Wine 11.0, applies `wine/patches/` and builds it ([patches.md](patches.md)). |
| `prefix/create.py`, `prefix/stage.py` | Create the Wine prefix for XP and install XP's DLLs, the COM registrations and the registry settings (registry profiles). Settings follow XP's language (Japanese: the MS UI Gothic font profile and MS Gothic for the console; English: classic Tahoma, Microsoft Sans Serif for MS Shell Dlg, Terminal for the console). The language is recorded as `language` in `xp-shell.json`. |
| `bridge-wm/build.sh` | Builds proxy.exe and the other Win32 helper programs (`.local/bridge`). |
| `prefix/extras.py` | Installs into the XP session's prefix the additions taken from the XP Explorer of [AshesTom's fork](https://github.com/AshesTom/win4wine) (XP icons in the notification area, Display Properties, sounds, Internet Explorer 6, Windows Media Player 9 and more), XP's `actxprxy.dll` in XP Explorer (the COM proxies that Taskbar Properties' Customize Notifications needs) and this fork's [Linux columns](#linux-columns-in-xps-folder-windows-xp-explorerhelperslinuxcolsc), [Open in Linux Terminal](#open-in-linux-terminal-sessionlinux-launchc) and [Mount for disc images](#disc-images-sessioniso_mountpy), with the code in `xp-explorer/scripts/`. The colour schemes of Display Properties (New Schemes), which XP creates at the first logon, are built by `prefix/xp_schemes.py` from the ISO's HIVEDEF.INF and themeui.dll. When they change, bump `VERSION` and setup.sh installs them again. |

## How the XP shell runs

- XP's `explorer.exe` is placed as `C:\xp\xp-explorer.exe`, a separate process from Wine's own explorer (which manages the desktop).
- XP's shell32, browseui, shdocvw, shlwapi, comctl32 and others are made native, through AppDefaults, **only for the
  xp-explorer.exe process**. Other Wine apps keep Wine's DLLs.
- Most Wine patches only take effect when the environment variable `WIN4WINE_EXTERNAL_SHELL` is set (the opt-in; [patches.md](patches.md)).
- The prefix is 64-bit (WoW64) and XP's DLLs go in `syswow64`. The XP shell's COM registrations are written on the 32-bit side only.
- Upstream also ran ReactOS's and Windows 7's shells this way, each in its own prefix. LunaticOS removed them; their Wine patches
  are still in the series but only act with `WIN4WINE_SHELL=reactos` or `win7`, which nothing sets ([patches.md](patches.md)).

## The WM (`bridge-wm/wm.c`, `bridge-wm/wm.py`)

An X11 window manager. It draws no frames itself (it does not reparent). There are two versions that behave the same: `wm.c` (libxcb),
which `bridge-wm/build.sh` builds as `.local/bridge/wm` and the session runs, and `wm.py` (python-xlib), which the session runs when
`wm.c` is not built or `session.json` has `"wm": "python"`. wm.py spent about 5 ms of an Atom N550's CPU on each step of a window drag,
nearly all of it in python-xlib; wm.c takes under a fifth of that. A change to one goes into the other too, and `tests/wm_parity.py`
checks that they still give the same state (see [development.md](development.md)). It:

- Tells apart XP's desktop (Progman), the taskbar (Shell_TrayWnd), the Start menu (DV2ControlHost), proxy frames, ordinary Wine windows
  and Linux windows by the `_WIN4WINE_CLASS` and `_WIN4WINE_HWND` that Wine's X11 driver sets (patch 0025).
- Keeps the desktop at the bottom and the taskbar and Start menu on top. A top-level WorkerW is not treated as the desktop: XP's and
  Windows 7's "Show Desktop" minimizes the windows, moves the desktop icons into a WorkerW and raises that above the windows (checked on
  real hardware). Win32 topmost windows (`_NET_WM_STATE_ABOVE`) and the windows they own go above the taskbar.
- Gives full screen (`_NET_WM_STATE_FULLSCREEN`): a Linux window that asks for it (or maps with it) covers the whole screen without a
  border, and leaves it for the rectangle it had. Its XP frame stays at that rectangle under it (`geometry.tsv` gives the proxy that
  one), so its taskbar button stays, and frame moves, the proxy's `geometry` commands and the window's own requests do not move it
  meanwhile; maximizing or restoring it then changes only the rectangle it returns to. Wine sizes its own full screen windows. As on
  XP, the active full screen window (Linux or Wine), with the windows it owns, covers the taskbar; topmost windows stay above it.
- Builds the work area (`_NET_WORKAREA`) from the taskbar's position.
- Activates windows on click. Clicking the desktop or the taskbar gives it the keyboard.
  Windows owned by a raised window (`WM_TRANSIENT_FOR`; modal dialogs and the like) are kept above it, as in Windows.
- Keeps each Linux window's frame (the proxy's window) just below that window and moves them together ("XP frames for Linux windows" below).
- Takes Alt+Tab, the Windows key and Ctrl+Esc and passes them to the XP shell ("Keyboard" below).
- Asks for the lock screen on Win+L, keeps its window (`_WIN4WINE_LOCK`) above everything but a screen saver, and gives the keyboard
  back to the window that had it when the screen is unlocked ("Screen savers and the lock screen" below).
- Publishes an XSETTINGS with `Gtk/DecorationLayout` set to `:`, which hides the close button and the like in GTK header bars (only when
  no other settings daemon is running).
- Automatically redirects Wine's windows (except proxy frames) with Composite, so the X server keeps their contents (`composite` in
  `session.json`, default `true`). When moving or closing a window uncovers a Wine window, the X server shows its contents as they were,
  so it never shows black until that window's process repaints (Wine makes the background of its X windows black). Linux windows repaint
  quickly by themselves, and Linux windows and frames change size often (a redirected window re-creates its image at every resize), so
  they are not redirected. Frames are redirected only while they move, though (and put back when the size changes): at every drag step
  the Linux window, which moves first, covered the frame's leading edge, the frame could not copy that edge when it moved, and it showed
  black until Wine repainted (about 5 ms); this was noticeable with Windows 7's thick borders. There are no effects such as shadows or
  transparency.

The WM does not wake periodically. It wakes on X events, inotify on the IPC directory and signals, and writes only files whose contents
changed. It remembers window positions and titles from X events to cut down on round trips to X. A window drag brings an X event per
step; moving a Wine window does not re-check the files, and when only a Linux window's position changed only `geometry.tsv` is rewritten.

Wine windows get no frame and no stand-in. Wine decides the size of its own maximized windows (patch 0031).

## The proxy (`bridge-wm/proxy.c`)

A Win32 process running on Wine. For each Linux window it creates a Win32 window (a stand-in) of class `Win4WineProxy`.

- XP's taskbar treats the stand-in as an ordinary Win32 window (title, minimized state, icon, shell hook notifications).
- The stand-in has `WS_OVERLAPPEDWINDOW`, and Wine draws an XP (Luna) frame on it. The WM stacks it just below the Linux window, so the
  Linux window appears to have an XP frame.
- Actions on the frame's buttons and on the taskbar (minimize, maximize, close, move) are passed to the WM in command files.
- Shell hooks (HSHELL_WINDOWCREATED and so on) are sent by the proxy to the recipients that Wine patch 0026 recorded.
- It does not wake periodically. It wakes on change notifications of the IPC directory and on Win32 WinEvents (foreground, minimize,
  window creation and destruction, title changes).
- Once, just after it starts, it sends the taskbar a `WM_SIZE` of its current size, so that the XP shell lays out its children itself.

## IPC (files between the WM, the proxy and tray.py)

Each session gets a directory under `$XDG_RUNTIME_DIR` that only the user can read (0700). Files are written as a temporary file in the
same directory and then renamed, so readers never see a half-written file. Each is written only when its contents change.

| File | Writer → reader | Contents |
|---|---|---|
| `windows.tsv` | WM → proxy | The list of Linux windows. One line per window: `XID(hex) TAB minimized(0/1) TAB active(0/1) TAB title` |
| `owners.tsv` | WM → proxy | The owner of each owned (transient) Linux window in `windows.tsv`: `XID TAB OWNER-XID`. Written before `windows.tsv` |
| `geometry.tsv` | WM → proxy | `XID x y width height maximized(0/1) workarea-x workarea-y workarea-width workarea-height` (the position of the Linux window's contents) |
| `frames.tsv` | proxy → WM | `HWND XID left right top bottom maximized minimized border-x border-y` (the size of the frame's margins) |
| `PID-NUMBER.cmd` | proxy → WM | `activate`, `minimize`, `close`, `maximize`, `restore` and an XID, or `geometry XID x y width height`, or `switch XID`. The WM reads each process's files in number order |
| `switch.tsv` | WM → proxy | Alt+Tab state: `number tab-count state` (state: o = showing, c = confirmed, x = cancelled) |
| `shellkeys.tsv` | WM → proxy | The Windows key and Ctrl+Esc pressed over a Linux window: `number windows VK...` or `number ctrl-escape` |
| `icon-XID.argb` | WM → proxy | A Linux window's icon ("Icons" below) |
| `tray.tsv`, `tray-ID.argb`, `menu-ID.tsv` | tray.py → proxy | Tray icons ("Tray icons" below) |
| `*.tray` | proxy → tray.py | Clicks on tray icons and menu choices |
| `lock-request` | WM (Win+L) and Wine's LockWorkStation → supervisor | `lock`; the supervisor removes it and locks the screen |
| `lock` | supervisor | There while the screen is locked |
| `proxy.lock` | proxy | While the proxy runs it holds the first byte with LockFile. The WM checks the proxy is alive with F_GETLK |

## XP frames for Linux windows

1. When the WM writes a Linux window into `windows.tsv`, the proxy creates its stand-in.
2. The proxy writes the stand-in's frame margins (measured with the public APIs GetWindowRect and GetClientRect) into `frames.tsv`.
3. The WM pairs the frame's X window with the Linux window through `_WIN4WINE_HWND` and the HWND in `frames.tsv`, and stacks the frame
   just below the Linux window. The margins are also published as `_NET_FRAME_EXTENTS`.
4. Dragging the frame's title bar or corners moves the frame through ordinary Win32 handling (Wine's move loop). In the same pass in
   which the WM receives the frame's move request, it also moves the Linux window to the position and size worked out from the margins
   (it moves the Linux window first, so the empty area inside the frame is never visible). The proxy writes no `geometry` command during
   the loop, only once when it ends, for the case the WM could not follow yet (a window that was maximized when the drag began). Late
   commands for a position the WM already matched are ignored.
   When the Linux window moves itself (dragging a GTK header bar, for instance), the WM likewise moves the frame in the pass that
   handles that request. Wine receives the X window's new position just as when another WM moves it. The WM then writes
   `geometry.tsv`, and the proxy checks the frame's position.

- When a window first gets a frame and has no requested position, it is placed at the next cascade position, as in XP. A window with a
  requested position is corrected so its title bar does not leave the work area.
- To maximize, the proxy really maximizes the stand-in with `ShowWindow(SW_MAXIMIZE)`, and the WM sizes the Linux window to "the work
  area minus the frame's margins" (as in XP, the outer border goes off screen).
- Minimizing hides the frame and the window together.
- The frame looks active only while the Linux window has the X keyboard ("active" in `windows.tsv`). The proxy switches the frame's look
  directly with `WM_NCACTIVATE`: like Windows, Wine refuses SetForegroundWindow from a process that did not receive the last input,
  and input goes to the Linux window, so the stand-in often cannot become the Win32 foreground window.
  As in XP, Luna frames too are drawn by their `WM_NCACTIVATE` state rather than by which window is in the foreground (patch 0069).
- Transient (dialog) Linux windows get a frame owned by their owner's frame, as XP's owned windows: no taskbar button, no Alt+Tab
  entry, only a close button. The proxy creates them after their owners' frames (`owners.tsv`); one whose owner has no frame gets an
  ordinary frame. They keep the place they mapped at instead of cascading. Ones larger than the work area (GTK's file chooser on a
  small screen) are shrunk to fit it, and ones with no requested position (GTK 4) are centred on their parent.

## Icons

- A Linux window's icon is read from `_NET_WM_ICON` (or, failing that, the pixmap in `WM_HINTS`) and written to `icon-XID.argb`.
  The format is `W4I1`, width and height (32-bit, little endian), then straight ARGB pixels. The proxy builds an HICON from it.
- XP's taskbar reads the icons of other processes' windows with `WM_GETICON` and `GetClassLongPtr`. Wine does not allow cross-process
  reads, so patch 0028, with the opt-in only, registers the icon's pixels with the wineserver so they can be read.

## Keyboard

- **Alt+Tab**: while Alt is held the WM grabs the keyboard and writes `switch.tsv`. The proxy draws XP's switcher window, and on
  confirmation sends `switch XID`; the WM activates that window. If the proxy is not running, the WM cycles through windows itself.
  As on XP, windows related by ownership share one entry, the first of them that shows: an application with a modal dialog is
  listed and choosing it activates the dialog, and a dialog whose owners are hidden (a file's properties, Run) is listed although it
  has no taskbar button. Tool windows and dialogs beside their visible owner have no entry of their own.
- **The Windows key and Ctrl+Esc**: when a Wine window has focus they go straight to Wine (patch 0029 opens the Start menu, and Wine
  handles hotkeys such as Win+R). When a Linux window has focus, the WM takes the keys until the Windows key is released, writes them
  into `shellkeys.tsv`, and the proxy feeds them to Wine with `SendInput`.
- **Window snapping (only with `window_snap`)**: the proxy takes Win+←/→/↑/↓ with `RegisterHotKey` and snaps the foreground window (or,
  if a Linux window has focus, its frame) to the left or right half of the work area, or maximizes, restores or minimizes it. The proxy
  remembers the position from before the snap and returns to it on Win+↓ or the opposite Win+arrow. In the X11 session it learns of title
  bar drags from EVENT_SYSTEM_MOVESIZESTART/END sent by Wine's move loop (patch 0038); when the pointer is at the screen's left, right or
  top edge it shows the target with the same hatched outline as XP's drag frame, and snaps on release. Snapped windows are marked with
  `Win4WineSnapRestore`, and when one is pulled away by a drag Wine restores its size. Dragging the title bar of a maximized window also
  makes Wine restore its size and keep moving it (with `WIN4WINE_WINDOW_SNAP=1`).
  In the Wayland session labwc drags normal-sized windows, so `wayland/session.sh` writes an `rc.xml` with labwc's snapping enabled into
  the IPC directory and uses it. Only labwc remembers which windows are snapped. The proxy asks labwc to handle Win+arrow with the
  commands `snap HWND side` and `unsnap HWND`, and learns which windows are snapped from `tiled.tsv`. During a drag labwc writes the
  target into `snapping.tsv`, and the proxy shows it with the same hatched outline as on X11.

## Tray icons (`bridge-wm/tray.py`)

- **StatusNotifierItem**: owns `org.kde.StatusNotifierWatcher` on the session bus and writes registered icons to `tray.tsv` and
  `tray-ID.argb`. Context menus are read from `com.canonical.dbusmenu` and written to `menu-ID.tsv`.
  If another watcher exists it does nothing.
- **XEmbed**: if there is no other X tray it owns `_NET_SYSTEM_TRAY_S0`, puts the icon windows into an off-screen container and reads
  their images with Composite. Clicks are injected as real presses with XTest.
- The proxy shows the icons in XP's notification area with `Shell_NotifyIconW`. That they arrive in a form XP's Explorer can read is
  thanks to patch 0034.

## The session (`session/supervisor.py`)

- Startup order: XP's logon screen → WM → tray.py → fcitx5 → updating the prefix (`prefix/stage.py`) → the Start menu of Linux apps
  → proxy → the XP shell.
- The session counts as started once the taskbar appears. If the XP shell or the proxy crashes, Wine is stopped and started again.
  After 3 failures in 120 seconds it starts Wine's standard explorer and xterm in a separate prefix that uses no XP DLLs (for recovery).
  `WIN4WINE_EXTERNAL_SHELL` and `WIN4WINE_SHELL` are removed then, giving a plain Wine on which the patches' changes do not take effect.
- XP's log off and shut down (patch 0024 writes the request to a file) show the log off screen and end the session.
  Shut down and restart run `shutdown_command` and `reboot_command` from `session.json`.
- Environment: the session's Wine goes first in the `PATH` of Wine and of Linux apps. The Japanese input variables (`XMODIFIERS` and
  others), the font settings (`linux_font`'s fontconfig), the DPI settings ("DPI" below) and the cursor theme are set as well.
  The variables for Wine programs (the prefix, opt-ins such as `WIN4WINE_EXTERNAL_SHELL`, `WINEDLLOVERRIDES`, the locale) are put
  together by `session/wineenv.py`. `win4wine-wine` (`session/wine.py`, installed into `~/.local/bin`) uses the same, so Windows programs
  started from outside the session look and behave as in the session (without the opt-in they get Wine's standard look). Inside the
  session, plain `wine` is started.
- It does not wake periodically (only while waiting for the taskbar at startup does it check once a second). Logs go to
  `~/.local/state/win4wine/`; at each start the previous one becomes `*.log.1`. Wine logs only errors by default
  (`WINEDEBUG=+timestamp,fixme-all`, inherited by every Windows program in the session). To investigate, choose Wine channels with
  `"debug"` in `session.json` (e.g. `"+timestamp,+seh,+loaddll"`).

## Screen savers and the lock screen (`session/screen.py`)

Only in the XP shell's X11 session. The supervisor starts it once the shell is up, and sleeps on its pipes and processes with
everything else: nothing polls, so an idle machine is not woken up.

- **Settings**: XP's own, from Display Properties' Screen Saver tab: `ScreenSaveActive`, `ScreenSaveTimeOut`, `SCRNSAVE.EXE`
  and `ScreenSaverIsSecure` in `HKCU\Control Panel\Desktop`. The supervisor reads them from the prefix's `user.reg` and watches it
  with inotify (the wineserver writes it within half a minute of a change). Wine patch 0158 makes "active" the user's setting
  instead of the X server's blanking timer. XP's tab writes `SCRNSAVE.EXE` to `system.ini` and reads it from the registry;
  `prefix/extras.py` sets up Windows' `IniFileMapping` for that entry, so that both are the registry.
- **Idle time**: `session/idle-watch.c` (built as `.local/bridge/idle-watch`) sets XSync alarms on the server's `IDLETIME` counter
  and writes `idle` and `active`. It turns the X server's own blanking off and sets the monitor's DPMS off time (`dpms` in
  `session.json`). Players that call `XResetScreenSaver` (mpv, VLC, and Wine's dinput for a joystick) hold the screen saver off.
- **The screen saver** is the chosen `.scr` run with `/s`. The path the tab stores names `system32`, which for that 32-bit program
  is `syswow64`; the supervisor translates it. One that ends by itself is not started again until the user has been active.
- **The lock screen** (`greeter/xp-lock.py`) is XP's Welcome screen as Fast User Switching shows it: the session's user with "N
  programs running" (the X windows that would have a taskbar button). It is an override-redirect window that holds the keyboard
  and pointer grabs; PAM checks the password (`/etc/pam.d/win4wine-lock`, installed by `greeter/install.sh`; without that file the
  session does not lock). It is started when:
  - **Win+L**: XP's Explorer does not register it (a `+relay` trace of `RegisterHotKey`), so the WM does, writes `lock-request`
    and passes the key on; patch 0159 keeps it from Windows programs.
  - **LockWorkStation** (patch 0159), e.g. `rundll32.exe user32.dll,LockWorkStation`.
  - **A screen saver starts with "On resume, password protect"**: the lock screen waits below the screen saver without the grabs,
    and takes them when the screen saver's window goes. Within 5 seconds of its start, no password is asked for (XP's grace period).
  - **logind** (`session/logind-watch.py`): the session's `Lock` signal (`loginctl lock-session`), and before sleep (a closed lid),
    which it delays with an inhibitor until the screen is locked (`lock_on_suspend`).
- **One lock screen waits** (`--standby`), started with the guard and again after each unlock: starting one and its first
  drawing take about 2 s on the EeePC. It loads the XP files, draws the screen once off screen and waits for `start` on its
  stdin. It holds no X connection while it waits, so nothing wakes it, and it reads the keyboard layout, the screen's size and
  the number of programs when it locks. If it dies, a lock screen is started when one is asked for.
- The supervisor and the lock screen talk over its stdin and stdout (`session/screen.py` lists the words). While the screen is
  locked a screen saver may start over it: the lock screen lets go of the grabs and takes them back when it ends.
- A lock screen that dies without unlocking (exit status 0) is started again. One that fails three times without ever locking is
  given up, so that a broken one does not restart for ever.

## The Start menu of Linux apps (`session/linux_apps.py`)

- Reads the XDG `applications` desktop entries and puts a shortcut for each app to show into the prefix's common Programs folder.
  Names get ` (Linux)` appended, and apps are put into XP folders (Accessories, Internet and so on) by their Categories.
- Shortcuts point to `linux-launch.exe` (`session/linux-launch.c`), which starts the Linux program through `/usr/bin/env`.
  Icons are made into .ico files from the icon theme.
- What it created is recorded in `C:\xp\linux-apps\state.json`, and only the entries of apps that disappeared are cleaned up. During the
  session it watches with inotify and follows packages being installed and removed.

## My Network Places (`session/network_places.py`)

XP's own My Network Places lists the devices on the local network as plain shortcuts in the user's `Network Shortcuts`
folder, which the folder shows and follows while it is open. The supervisor starts the helper once the shell is up (XP shell).

| Announced | Shortcut | A double click |
|---|---|---|
| `_http._tcp`, `_https._tcp`; a UPnP device with a presentation URL | Web page on NAME | `C:\xp\bin\iexplore.exe` opens the Linux browser |
| `_ipp._tcp`, `_ipps._tcp`, `_printer._tcp`, `_pdl-datastream._tcp`; a UPnP printer | Printer on NAME | its web page; without one, "not accessible" |
| `_ssh._tcp`, `_sftp-ssh._tcp` (with `ssh` and a terminal installed) | SSH on NAME | a terminal running `ssh` |
| `_workstation._tcp`, `_smb._tcp`, a UPnP device without a page, and nothing else | NAME | XP's "\\NAME is not accessible" (`linux-launch.exe --unreachable`) |

- **Sources**: Avahi's D-Bus API for mDNS (needs `avahi-daemon`; without it only UPnP is shown), IPv4, without this machine's
  own services. For UPnP it listens for SSDP `NOTIFY` on 239.255.255.250:1900 and sends an `M-SEARCH` at start, when the
  machine's addresses change (netlink) and every 10 minutes, then reads each root device's description.
- **A device** is what shares an address or a host name. Its name is the UPnP friendly name, else the mDNS host name. One page
  announced twice is one shortcut, and of the same page over http and https the http one stays.
- **Departures**: mDNS removals, SSDP `byebye` and an expired `max-age` take effect after a grace period (60 s). After a
  change of addresses, UPnP devices that do not answer again go too.
- **Writing**: changes are collected for 3 s (at most 10) and written by one `linux-links.exe` run, since each starts Wine.
  Removing a file needs no Wine; Explorer notices it.
- **Only its own files** are touched: they are listed in `C:\xp\network-places\state.json` before they are written, and a
  name the user already has gets ` (2)`. All of them are removed at start and at the session's end, so that none stay behind
  from a crash or another network.
- **Announcements are not trusted**: only private and link-local addresses count; names lose control, formatting and file name
  characters; URLs are rebuilt from the announced address (http or https, percent encoded), and a UPnP or printer page gets the
  device's own address whatever host it names; SSH gets the address after `--`, never the name; an SSDP location must be on
  the sender's own address; a description is read with a time and
  size limit, without redirects, and refused if it has a DTD. The numbers of devices tracked and shortcuts shown are capped.
- **Settings**: `network_places` in session.json: `false`, or `{"show": ["web", "printer", "ssh", "computer"], "limit": 30,
  "grace": 60}`.
- **Grouping**: XP puts every shortcut under "Unspecified", and its folder has no Type column (what was tried:
  [plans/network-places.md](../plans/network-places.md#grouping-and-columns)). Windows shares are not listed as such (Wine has
  no SMB client).
- XP's `HIVESFT.INF` also adds a namespace item whose class nothing here registers, an item without a name; the prefix leaves
  it out (`xp-explorer/scripts/prefix.py`).

## Linux columns in XP's folder windows (`xp-explorer/helpers/linuxcols.c`)

XP's Explorer can show **Linux Permissions** (`-rwsr-xr-x`), **Linux Owner**, **Linux Group** and **Linux Path** as columns in
Details view (right click on the column header, or View → Choose Details), and the same four in the **Details** section of the
task pane. Wine's own shell32 does not use column handlers, so only XP's Explorer has them. It only reads.

- **The handler** is a 32-bit in-process COM server, `C:\xp\bin\linuxcols.dll`, with `IColumnProvider`, listed under
  `HKCR\Folder\shellex\ColumnHandlers`. `prefix/extras.py` copies and registers it. The DLL's second class is the context menu
  handler of [Open in Linux Terminal](#open-in-linux-terminal-sessionlinux-launchc).
- **Mode, user and group** come from `NtQueryInformationByName` with `FileStatLxInformation` (Wine patch 0160): a path conversion
  and `lstat()`. A symbolic link shows itself (`l`, its own owner), as `ls -l` does. Without the patch only the path is filled in.
  Names are read from `\\?\unix\etc\passwd` and `group`, again when they change; users only NSS knows show as numbers.
- **No item is opened.** `wine_get_unix_file_name` opens the file in the wineserver, so it is asked only for a drive's root
  (it returns the resolved directory), and the rest of the path is appended in the folder window's spelling. Items under
  `/proc` and `/run/user/*/gvfs` are left empty. (Wine hides `/dev`, `/proc` and `/sys` from folder listings anyway.)
- **Filled in the background** (`SHCOLSTATE_SLOW`): XP's own task asks from its thread, for the visible rows, and again after
  scrolling or F5. Nothing wakes afterwards. Sorting by one of these columns asks for every item on the view's thread.
- **The Details section** reads a property list from the value `Details` of the item's type; `extras.py` sets it on `*` and
  `Directory`. XP reads a property ID only after a space (`{FMTID} 6`; after a comma it takes 0), and it shows a property it
  does not know without a name. So the handler has the four columns twice: the second set is hidden (`SHCOLSTATE_HIDDEN`,
  not in the column menus), and its values carry the name (`Owner: root`). The path there has zero width spaces after `/`, `_`,
  `-` and `.`, and in long runs, where the narrow pane can wrap it.
- **To see XP's calls** (the columns' and the menu's): `HKCU\Software\win4wine\LinuxColumns`, DWORD `Debug` = 1, and
  `"debug": "warn+seh"` in `session.json` (Wine prints `OutputDebugString` on the `seh` channel).

## Open in Linux Terminal (`session/linux-launch.c`)

Folders and drives that exist on the Linux file system offer **Open in Linux Terminal** (access key T): a Linux terminal
(`x-terminal-emulator`, else `xterm`) in that folder, with an XP frame and a taskbar button from the WM.

- **The menu item** comes from a context menu handler (`IShellExtInit` and `IContextMenu`), the second class of
  `C:\xp\bin\linuxcols.dll` (`xp-explorer/helpers/linuxcols.c`), registered by `prefix/extras.py` under
  `ContextMenuHandlers` of `Directory`, `Drive` and `Directory\Background`. So it is on a folder or drive (in a folder window,
  the Folders tree, the desktop and the File menu), and on the background of a folder window or the desktop, for that folder.
  It starts `linux-launch.exe --terminal "PATH"`.
- **Where it is missing**: anything but one directory on a drive (a file, several items, My Computer and the other virtual
  folders), and `/proc` and `/run/user/*/gvfs`, where asking could hang the menu (the [Linux columns](#linux-columns-in-xps-folder-windows-xp-explorerhelperslinuxcolsc)'
  rule). The check opens the drive's root and asks for the folder's attributes.
- **Once per menu**: with a folder selected, XP's File menu asks the handler for the selection and then, into the same menu,
  for the window's folder, so the handler adds nothing to a menu that has its item.
- **The folder reaches the terminal as its working directory only.** Wine changes to `CreateProcess`'s current directory before
  it runs the Unix program, so no folder name is ever part of a command line or read by a shell.
- **No terminal in another directory.** The launcher shows a message box instead for: a path without a drive letter
  (`\\server\share`); a path that is not a directory; a name that ends with a dot (or the path with a space), which Wine would
  strip from the working directory; and a folder that may not be entered, where Wine would silently keep its own directory.
  The last is found by looking a name up in the folder (`FindFirstFile`): opening the folder succeeds for one that may be
  listed but not entered.
- **The environment** is the session's, without `WINEDEBUG` and `FONTCONFIG_FILE`. `WINEPREFIX`, the `PATH` with the session's
  Wine, `WIN4WINE_*` and `WINEDLLOVERRIDES` stay, so `wine notepad` typed there opens Notepad in the session with its XP frame
  (without them its frame is broken). Linux programs do not read them.

## Disc images (`session/iso_mount.py`)

A double click on an `.iso` file mounts it on the Linux side, and it becomes a CD drive in My Computer (the first free letter
from `D:`) with the disc's label. Linux does the mounting (the kernel's loop device and iso9660 or UDF driver, through
UDisks2); Wine does not read the file.

- **The association** is written by `prefix/extras.py`: `.iso` becomes the class `win4wine.isoimage` ("Disc Image File",
  the icon [resources/iso-disc.ico](../resources/iso-disc.ico)) unless a program has given it a class of its own. Its default verb, **Mount**, runs
  `linux-launch.exe --mount-iso "PATH"`.
- **The work** is done by `C:\xp\bin\iso_mount.py`, which the launcher starts with the Unix names of the image and of a result
  file. A script, because Wine gives a Windows program neither the exit status of a Unix program nor its output through a
  pipe: the script writes `ok`, the device and the mount point, or `error` and a reason, and the launcher waits for that file
  (up to a minute) and shows an error in a message box.
- **The script** attaches the image to a loop device read-only and without partitions (`udisksctl loop-setup -r
  --no-partition-scan`; a hybrid image would otherwise become several drives) and mounts it (`udisksctl mount`, at
  `/media/USER/LABEL`). polkit allows both without a password to the user of the active local session. An image that already
  is on a loop device is not attached again, and a lock keeps two double clicks from making two drives. A file that is not a
  disc image leaves no loop device behind.
- **The drive letter** comes from Wine's own `mountmgr.sys`, which follows UDisks2: an iso9660 or UDF block device becomes a
  CD or DVD drive, and goes when the device does. `mountmgr` only follows UDisks2 if the service is there when it starts, so
  after installing `udisks2`, log off and on. The launcher asks `mountmgr` for up to 5 s which letter has the device
  (`IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE`) and opens it; without one it opens the mount point through `Z:`. It starts
  `xp-explorer.exe` with the path for that: opening a folder as a document goes over DDE, which fails from another program.
  For a disc mounted just now it waits 2 s first: a window asked for before Explorer has taken the new drive in is titled
  `D:\`, without the label, and its address bar stays empty (on the EeePC after 0.3 and 0.8 s, not after 1.5 s).
- **Eject** is XP's own, on the drive's menu and in My Computer's task pane. XP's shell32 sends `IOCTL_DISK_EJECT_MEDIA` to
  the volume, with no lock or dismount before it, and patch 0164 makes `mountmgr` unmount the image and delete its loop device
  (`udisksctl`, which needs no password for a device the user set up). The drive goes with the device (0163 tells Explorer).
  Only loop devices that are CD or DVD drives are touched. Real drives and USB sticks keep Wine's behaviour.
  While a folder window shows the drive (the wineserver holds the folder open) or a program has a file or its working
  directory on it, the unmount fails and the disc stays. XP shows **no message** then; close the window and eject again.
- **Arrivals**: `mountmgr` announces a drive once, when it can be read, as a new drive (0163): Explorer adds it to My
  Computer and opens nothing, so the one window is the launcher's. An image mounted from Linux appears without a window.
  Unmounting from Linux (`udisksctl unmount`) leaves the drive with nothing in it; it goes when the loop device does.
  A disc's `autorun.inf` is **not** run: XP's Explorer looks for the file and does not read it (as far as traced, with
  no Shell Hardware Detection service here).
- **Images stay mounted across a log off**, like a disc left in a drive. A restart of the machine clears them.

## The Recycle Bin

XP's own shell32 does all of it: a deleted file moves to `RECYCLER\<user SID>` on its drive under a new name (`D`, the drive's
letter, a number), and the index `INFO2` there keeps where it came from and when. XP trusts a bin only if the folder still has
the owner and access list it was made with. Wine on its own makes an access list up from the Unix mode, so Wine 0120 keeps the
one XP set in the extended attribute `user.wine.dacl`.

The prefix's registry profile (`prefix/registry.py`) adds what XP's setup registers for the bin and Wine's prefix lacks: the
bin's own context menu and property sheet handlers (Empty Recycle Bin, its settings) and the `empty` and `full` icons that
the shell switches the desktop icon between.

- Drive H: is the home folder, which Wine reports as a fixed drive, so files deleted there (My Documents) go to `~/RECYCLER`,
  visible from Linux. That keeps such a delete on one file system.
- A copy of the prefix or the home folder without extended attributes (`cp` without `--xattrs`) loses the access list. XP then
  asks once to empty that bin, and Yes makes it again.

## The login screen (`greeter/`)

- `greeter/xp-greeter.py` is a LightDM greeter. It reads images and strings as PE resources from the user's XP logonui.exe, msgina.dll,
  winlogon.exe and others (no code is run). `greeter/welcome.py` draws the screen with cairo and Pango.
- If needed files are missing, it switches to the ordinary greeter (lightdm-gtk-greeter) before connecting to LightDM.
- XP's screens while logging on and logging off are shown inside the session by `greeter/xp-splash.py`. Until the shell has started it is
  the screen with just the large "welcome" that XP shows while starting. Without the greeter installed, it draws from this checkout's
  `.local/assets`.
- `greeter/cursors.py` makes XP's cursors into an X cursor theme (`win4wine-xp`) from the user's user32.dll.

## DPI

- As in XP, DPI is set by `HKCU\Control Panel\Desktop\LogPixels` in the prefix (`prefix/display.py --dpi`).
- The XP shell and Wine apps draw at that DPI themselves (patch 0061 makes them system DPI aware).
- The supervisor passes the same value to Linux apps (`session/dpi.py`): `Xft.dpi`, fontconfig's dpi, `XCURSOR_SIZE`, and at 192 and
  above GTK's `GDK_SCALE`.
- The Welcome screen's DPI is set by `dpi` in `/etc/win4wine/greeter.json` (in XP, too, it is a machine-wide setting).

## Font smoothing (ClearType)

- XP's "Standard" and "ClearType" for "Use the following method to smooth edges of screen fonts" are set by `FontSmoothing` and
  `FontSmoothingType` (1 or 2) in the prefix's `HKCU\Control Panel\Desktop` (`prefix/display.py --font-smoothing`). The default is
  "Standard", as in XP.
- With ClearType, patch 0062 draws the XP shell's and Wine apps' text the same way XP does.
  Sizes with embedded bitmaps, such as MS UI Gothic's (up to 22 pixels), do not get ClearType, as in XP.
- The supervisor also tells Linux apps to draw with RGB subpixels (`session/dpi.py`: `Xft.rgba`, fontconfig's `rgba`).
  Wayland apps (GTK and others) follow their own settings (GNOME's `font-rgba-order` and the like).
- The Welcome screen does not change.

## The Wayland session (`wayland/`)

- `wayland/session.sh` creates the IPC directory (`WIN4WINE_IPC`), starts the patched labwc and runs the supervisor inside it (labwc's
  `-S`; when the supervisor exits, so does labwc).
- labwc takes the WM's role; the IPC files and proxy.exe are the same as in the X11 session. The supervisor does not start the WM; it
  sets the outputs' scale and position with `wlr-randr` and writes the primary monitor's name to `primary` in the IPC directory.
- Wine and the XP shell stay X11 clients of Xwayland. labwc shows Xwayland the outputs in real pixels, so the XP shell, drawn at its DPI,
  is shown one pixel per pixel even on an output scaled 1.5. Wayland apps draw at each output's scale.
- Details are in [wayland.md](wayland.md).
