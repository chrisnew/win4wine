# Architecture

What win4wine is made of and how the parts connect. Why it was done this way is in [design.md](design.md).

## Overview

```text
Display manager (LightDM)
 └─ session/supervisor.py ............. starts and watches the session
      ├─ bridge-wm/wm.c (or wm.py) .... the X11 window manager (WM)
      ├─ bridge-wm/tray.py ............ broker for Linux tray icons
      ├─ fcitx5 ....................... Japanese input
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
| `prefix/extras.py` | Installs into the XP session's prefix the additions taken from the XP Explorer of [AshesTom's fork](https://github.com/AshesTom/win4wine) (XP icons in the notification area, Display Properties, sounds, Internet Explorer 6, Windows Media Player 9 and more), with the code in `xp-explorer/scripts/`. The colour schemes of Display Properties (New Schemes), which XP creates at the first logon, are built by `prefix/xp_schemes.py` from the ISO's HIVEDEF.INF and themeui.dll. When they change, bump `VERSION` and setup.sh installs them again. |

## How the XP shell runs

- XP's `explorer.exe` is placed as `C:\xp\xp-explorer.exe`, a separate process from Wine's own explorer (which manages the desktop).
- XP's shell32, browseui, shdocvw, shlwapi, comctl32 and others are made native, through AppDefaults, **only for the
  xp-explorer.exe process**. Other Wine apps keep Wine's DLLs.
- Most Wine patches only take effect when the environment variable `WIN4WINE_EXTERNAL_SHELL` is set (the opt-in; [patches.md](patches.md)).
- The prefix is 64-bit (WoW64) and XP's DLLs go in `syswow64`. The XP shell's COM registrations are written on the 32-bit side only.
- The ReactOS shell (`C:\ros\ros-explorer.exe`) and the Windows 7 shell (`C:\w7\w7-explorer.exe`, 64-bit) run the same way, each in
  its own prefix ([reactos.md](reactos.md), [windows7.md](windows7.md)). `shell` in session.json chooses one, and the supervisor uses
  that prefix's `stage` (`prefix/reactos.py`, `prefix/win7.py`). For the Windows 7 shell it also sets `WIN4WINE_SHELL=win7`, and the
  proxy gives Linux windows an AppUserModelID per `WM_CLASS` (Windows 7's taskbar groups buttons by it). For apps in the Start menu it
  also sets the jump list's title and icon and the command of a pinned button (`relaunch.tsv` from `linux_apps.py`).
  In the Wayland session labwc writes `classes.tsv` (each window's app_id or `WM_CLASS`) instead of the WM.

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
- Builds the work area (`_NET_WORKAREA`) from the taskbar's position.
- Activates windows on click. Clicking the desktop or the taskbar gives it the keyboard.
  Windows owned by a raised window (`WM_TRANSIENT_FOR`; modal dialogs and the like) are kept above it, as in Windows.
- Keeps each Linux window's frame (the proxy's window) just below that window and moves them together ("XP frames for Linux windows" below).
- Takes Alt+Tab, the Windows key and Ctrl+Esc and passes them to the XP shell ("Keyboard" below).
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
| `geometry.tsv` | WM → proxy | `XID x y width height maximized(0/1) workarea-x workarea-y workarea-width workarea-height` (the position of the Linux window's contents) |
| `frames.tsv` | proxy → WM | `HWND XID left right top bottom maximized minimized border-x border-y` (the size of the frame's margins) |
| `PID-NUMBER.cmd` | proxy → WM | `activate`, `minimize`, `close`, `maximize`, `restore` and an XID, or `geometry XID x y width height`, or `switch XID`. The WM reads each process's files in number order |
| `switch.tsv` | WM → proxy | Alt+Tab state: `number tab-count state` (state: o = showing, c = confirmed, x = cancelled) |
| `shellkeys.tsv` | WM → proxy | The Windows key and Ctrl+Esc pressed over a Linux window: `number windows VK...` or `number ctrl-escape` |
| `icon-XID.argb` | WM → proxy | A Linux window's icon ("Icons" below) |
| `tray.tsv`, `tray-ID.argb`, `menu-ID.tsv` | tray.py → proxy | Tray icons ("Tray icons" below) |
| `*.tray` | proxy → tray.py | Clicks on tray icons and menu choices |
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
- Transient (dialog) Linux windows get no frame and no taskbar button. Ones larger than the work area (GTK's file chooser on a small
  screen) are shrunk to fit it, and ones with no requested position (GTK 4) are centred on their parent.

## Icons

- A Linux window's icon is read from `_NET_WM_ICON` (or, failing that, the pixmap in `WM_HINTS`) and written to `icon-XID.argb`.
  The format is `W4I1`, width and height (32-bit, little endian), then straight ARGB pixels. The proxy builds an HICON from it.
- XP's taskbar reads the icons of other processes' windows with `WM_GETICON` and `GetClassLongPtr`. Wine does not allow cross-process
  reads, so patch 0028, with the opt-in only, registers the icon's pixels with the wineserver so they can be read.

## Keyboard

- **Alt+Tab**: while Alt is held the WM grabs the keyboard and writes `switch.tsv`. The proxy draws XP's switcher window, and on
  confirmation sends `switch XID`; the WM activates that window. If the proxy is not running, the WM cycles through windows itself.
  The window's look depends on the shell (`WIN4WINE_SWITCHER`): XP (the default), ReactOS or Windows 7. The Windows 7 look is drawn with
  the parts of the `AltTab` class of the theme (aero.msstyles), and its items include "Desktop" (choosing it minimizes all windows).
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

## The Start menu of Linux apps (`session/linux_apps.py`)

- Reads the XDG `applications` desktop entries and puts a shortcut for each app to show into the prefix's common Programs folder.
  Names get ` (Linux)` appended, and apps are put into XP folders (Accessories, Internet and so on) by their Categories.
- Shortcuts point to `linux-launch.exe` (`session/linux-launch.c`), which starts the Linux program through `/usr/bin/env`.
  Icons are made into .ico files from the icon theme.
- What it created is recorded in `C:\xp\linux-apps\state.json`, and only the entries of apps that disappeared are cleaned up. During the
  session it watches with inotify and follows packages being installed and removed.

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
