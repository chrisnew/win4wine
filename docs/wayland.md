# The Wayland session

This describes the second session on the login screen, "XP Shell (Wine, Wayland)" ("ReactOS Shell (Wine, Wayland)" for the ReactOS
shell, "Windows 7 Shell (Wine, Wayland)" for the Windows 7 shell). Instead of the X11 window manager (bridge-wm), a patched labwc (a
wlroots compositor) takes the same role. The X11 session stays as it is and behaves the same.

**This is even more experimental than the X11 session.** It is new, it has been checked less than the X11 session, and it has the limits
listed under "Differences from the X11 session, and what is left" below. Which compositor was chosen, the comparison and the first
prototype's results are in [history/wayland-study.md](history/wayland-study.md).

## What it makes possible

- **Per-monitor scaling**: a 100% monitor and a 150% monitor can be used at the same time. On X11 there was a single DPI for the whole screen.
- **Linux apps at fractional scales**: GTK, Qt, Chromium, Firefox and others can draw at 125% or 150% directly.
- **The XP shell is not blurred**: the XP shell and Wine apps stay X11 clients of Xwayland, and what they draw at their DPI (LogPixels) is
  shown one pixel per output pixel. The compositor never scales it.

XP has no per-monitor DPI, so the XP shell has a single DPI. Match it to the scale of the output you mainly use (144 for a scale of 1.5).

## Usage

- `scripts/setup.sh --media ISO --wayland` builds labwc and wlroots (`wayland/build.sh`, `.local/labwc-install`) and registers the session
  (`session/install.sh --wayland`). Together with `--shell reactos` or `--shell win7`, it registers that shell's Wayland session.
  Together with `--greeter`, the logon screen's default session becomes the Wayland one too.
- The outputs' scale and position are set by `"outputs"` in `session.json`, and Win32's primary monitor by `"primary"` (an output name).
  Example: `"outputs": {"HDMI-A-1": {"scale": 1.5}, "DP-1": {"scale": 1, "position": "2560,0"}}, "primary": "HDMI-A-1"`.
  `mode` (a value for `wlr-randr --mode`) can be given too. Output names are shown by `wlr-randr`.

## Parts

| Path | Contents |
|---|---|
| `wayland/upstream.env`, `wayland/build.sh` | Builds labwc 0.8.3 with wlroots 0.18.2 and libsfdo linked statically as subprojects (the system's wlroots is not used) |
| `wayland/patches/wlroots/0001` | Shows scene surfaces at 1/scale size, and cropped to a part. On an output of that scale they are drawn one pixel per pixel |
| `wayland/patches/wlroots/0002` | The XWM keeps `_NET_WM_STATE_ABOVE` (Win32 topmost windows) instead of dropping it, and reports changes |
| `wayland/patches/wlroots/0003` | Scene surfaces are drawn only inside their shape (the X window's SHAPE), and take input only inside it. X windows are drawn at the right output pixel even on outputs with a non-integer scale |
| `wayland/patches/wlroots/0004` | The XWM always writes the activated window into `_NET_ACTIVE_WINDOW` (even when that window's client had already moved the X focus) |
| `wayland/patches/labwc/0001` | `xwayland-scale.c` (running Xwayland in output pixels) and `win4wine.c` (bridge-wm's role) |
| `wayland/labwc/` | labwc's configuration (Alt+F4 as the only key, an empty menu, frames in colours close to XP's Luna) |
| `wayland/session.sh` | Chooses the IPC location (`WIN4WINE_IPC`), starts labwc and runs the supervisor inside it. When the supervisor exits, the session ends |
| `session/supervisor.py` | Inside labwc it does not start bridge-wm; it sets up the outputs with `wlr-randr` and writes the primary monitor's name to `primary` in the IPC directory. Wine is given `WIN4WINE_COMPOSITED` ("The Windows 7 shell" below) |
| Wine 0033, 0088 | Under Xwayland the screen cannot be read from the root window, so XP's shadows are drawn as translucent black windows (0033), and the grey log off screen is built from each window's contents (0088) |

## Running Xwayland in output pixels (`xwayland-scale.c`)

- Xwayland is shown xdg-outputs of labwc's own, with every output laid out side by side at its pixel size ("X space"). The primary monitor
  goes first (X's 0,0) and is also made Xwayland's RandR primary. Win32's 0,0 is the primary monitor's top left, and XP's desktop icons and
  wallpaper are on the primary monitor.
- X windows are placed shrunk to 1/scale of the output they are on (one X pixel is one output pixel). A window spanning outputs with
  different scales is shown as parts cropped per output, side by side.
- Window position and size requests, size hints, the work area (`_NET_WORKAREA` and the per-output `_GTK_WORKAREAS_D0`) and the position
  of a pointer that left a window while a button was held are converted between X space and layout coordinates.

## bridge-wm's role (`win4wine.c`)

Only active when `WIN4WINE_IPC` is set.

- The IPC with proxy.exe is the same as bridge-wm's (`windows.tsv`, `geometry.tsv`, `frames.tsv`, `*.cmd`, `switch.tsv`, `shellkeys.tsv`,
  `icon-*.argb`; "IPC" in [architecture.md](architecture.md)). IPC files are read with inotify; window changes come from inside labwc and
  are written together at the next idle.
- Linux windows (Wayland windows, and X11 windows other than Wine's) get XP frames. The frame is stacked just below the window, and move,
  resize, maximize, minimize, close and drag are tied to the window. The maximized size is the work area minus the frame. Icons come from
  the app's desktop entry for Wayland windows and from `_NET_WM_ICON` for X11 windows.
- The kind of a Wine window (`_WIN4WINE_CLASS`, `_WIN4WINE_HWND`) is read over labwc's own X connection. XP's desktop goes at the bottom,
  the taskbar, the Start menu and topmost windows (and the windows they own) on top, and X's stacking order is kept in step (Xwayland
  decides the window under the pointer by X's stacking order).
- Alt+Tab goes to XP's switcher window, and the Windows key and Ctrl+Esc go to the XP shell even over Linux windows. The taskbar's area is
  taken out of the work area.
- Activation follows Wine. When a Wine window goes away, Wine chooses the next window to activate. Windows Wine marks as not to be
  activated (`_NET_WM_USER_TIME` 0) and topmost full-screen windows (the grey log off screen) are not activated when shown.
  When a Wine window has the keyboard, the X focus is also given to it directly (Wine ignores WM_TAKE_FOCUS to windows that are not in the
  foreground). If wlroots taking the X focus away over its own X connection arrives late and leaves no window with the X focus, it is given
  again. While a Wine window has the keyboard, an unmanaged (override-redirect) window shown by the same Wine process does not take the
  keyboard; Wine moves the X focus within it itself (ReactOS's Start menu works through the keys of its owner, the taskbar).
  When a Linux window is active, its frame is written into `_NET_ACTIVE_WINDOW` so the XP frame gets the active colour. The X focus is not
  moved (if it were, fcitx5 would mistake the frame for the input target through XIM).
- Xwayland is not shown a relative pointer. Xwayland sends buttons from that device, and its position does not move with absolute motion
  (tablets, virtual machine pointers), so the first click would land somewhere else.
- When a window grows, a frame callback is sent to the X window. A frame hidden under a Linux window by maximizing gets no callback, and
  Xwayland would never draw the new size.
- An X window's shape (SHAPE bounding) is read over its own X connection and only the inside is drawn (wlroots 0003). Xwayland also shows
  what is outside the shape, so Wine's window regions (SetWindowRgn: the round corners of XP's and Windows 7's frames, the unused part of
  Windows 7's Start menu) showed black.
- Linux windows with no position (Wayland windows never have one) are placed, as in bridge-wm, at Windows' default window position (the
  next one, shifted down and right). A window maximized before its frame size is known is fitted again once it is.
- The work area follows the taskbar's size and position when they change (Windows 7's "Use small icons"). Maximized Linux windows follow
  it too. Wine windows are left out of labwc's re-arranging: Wine places them itself and also refits its maximized windows (moving the
  taskbar to fit it into the work area changed the work area again, endlessly).
- Windows outside Wine's management (tooltips and the like) do not take the keyboard when shown (as with the X11 window manager).
  After closing Windows 7's Start menu with Esc, the Start button's tooltip took the keyboard from a Linux window and the Windows key stopped
  working.
- On outputs with a non-integer scale, X windows are drawn at the right output pixel (wlroots 0003). Layout coordinates are integers, so
  multiplying the position by the scale and rounding put windows one pixel off depending on their X position. The edge of the work area
  that the taskbar decides is likewise taken straight from the taskbar's X position (through layout coordinates it overlapped the taskbar by
  one pixel at a scale of 1.5).
- `_NET_ACTIVE_WINDOW` always points to the activated window (wlroots 0004). When a window to which Wine had already moved the X focus was
  activated later, the XWM did nothing ("it already has focus") and `_NET_ACTIVE_WINDOW` kept the previous window. Wine took that for the
  foreground window and re-activated the previous window, so in Windows 7's taskbar the first jump list opened turned inactive at once and
  stayed black.
- When labwc activates the taskbar on a taskbar click, the other Wine processes (Notepad and so on) follow and bring the taskbar to the
  foreground, notifying the shell's thread. That notification, arriving while the shell showed a jump list or the Start menu, re-activated
  the taskbar and made what had just been shown inactive (the jump list stayed black, XP's Start menu closed at once). With Wine's 0089 the
  notification is ignored if, after it was sent, that thread brought one of its own windows to the foreground.
- A Linux window's app (Wayland's app_id, X11's `WM_CLASS`) is written into `classes.tsv`, as by the X11 WM. Windows 7's taskbar uses it
  to group an app's windows into one button and to title the jump list with the app's name (they used to be separate buttons per window).
- Shapes are remembered only for shown windows (shape changes of hidden or destroyed windows are not tracked; they are read again when
  shown).
- A newly shown X window is revealed once the client has drawn it (at the next commit after mapping, or after 100 ms). The first contents
  Xwayland sends on mapping are a pure black background, so Windows 7's folder windows and Start menu showed black for one or two frames
  when opening. Frame-done is still sent while waiting, so Xwayland can send the next contents. Sometimes the next commit comes before
  Wine has drawn, and the Start menu still occasionally shows black for one frame.

## The Windows 7 shell

- Windows 7's Start button and the user picture in its Start menu are translucent windows. The X11 session has no compositing, so Wine
  blends them with the taskbar's or Start menu's image itself (Wine's 0123). In the Wayland session the compositor blends them with the
  real image beneath, so the supervisor gives Wine `WIN4WINE_COMPOSITED` and Wine passes them on unblended.
  The Start button's edge differs from the reference machine (Windows 7) in only 4 pixels (by at most 2 levels); in the X11 session, 199.
- Taskbar settings (small icons, combining buttons) take effect at once, and the work area and maximized windows follow.

## What was checked

Checked in a nested labwc (chroot, 2 outputs, scales 1 and 1.5, 144 DPI) and on a test VM's real output (DRM, 1280×800, scale 1 at 96 DPI
and scale 1.5 at 144 DPI).

- XP shell: desktop, taskbar, Start menu, menu shadows, Run, My Computer, the log off dialog and the grey screen. Maximizing Wine windows
  (they fit the work area), restoring, dragging. No blur at a scale of 1.5.
- Linux windows: foot (Wayland), mousepad (GTK3, Wayland) and xterm (X11) get XP frames, taskbar buttons and icons. Dragging the frame
  (from the first time), resizing, maximizing (fits the work area including the frame), restoring, minimizing, restoring from the taskbar,
  closing. The frame's active colour follows the window's activation.
- Keys: Alt+Tab (XP's switcher), Win+R, Win+E and Ctrl+Esc over Linux windows.
- Japanese input: composing and committing works in foot (text-input-v3, fcitx5 + Mozc) and in Wine's Run dialog (XIM).
- Two outputs: XP's desktop is on the primary monitor, and Wine windows can be on either output. The primary monitor can be chosen with
  `"primary"`.
- Ending the session: logging off XP ends labwc and returns to the login screen.
- X11 session: switched back to the X11 session on the same VM and checked it works as before.
- Windows 7 shell (2026-10-17): in a nested labwc (one output, scales 1 and 1.5), compared screenshots with the X11 session doing the same:
  desktop, Start menu, Notepad (normal and maximized), Mousepad (Wayland) with Windows 7 frames and maximized, Alt+Tab, the desktop's and
  taskbar's context menus, jump lists, Run, Taskbar Properties, the folder window from Win+E, taskbar settings (small icons, combining). The
  only differences are the clock, the cursor, Mousepad's text (GTK draws differently on Wayland and X11), and where the X11 session differs
  from the real machine (the edges of the Start button and the user picture). The same at scale 1.5 (144 DPI). For the XP and ReactOS shells,
  compared with labwc before the change, the only differences are round corners and the placement of Linux windows.
  On a test VM's real output (DRM, 1280×800), from `setup.sh --shell win7 --wayland --greeter`: the logon screen, Start menu, Notepad and
  Mousepad (Wayland), Japanese input (text-input-v3 in Mousepad, XIM in Notepad), Win+E, Alt+Tab, small icons through the taskbar's context
  menu → Properties, and returning to the logon screen on log off.
- Automated test: `tests/wayland_compositor.py` (without Wine, it checks the compositor's rules with X windows carrying Wine's window
  marks, including the work area following the taskbar's size, default window positions, X window shapes and pixel positions at a scale of
  1.5 (reading output pixels), and unmanaged windows not taking the keyboard).
  Checks that need the XP shell and the proxy (the counterparts of X11's `tests/native_frames.py` and the like) are still done by hand.

## Differences from the X11 session, and what is left

- The grey log off/shut down screen: Wayland apps' windows are not X windows, so they do not appear on it (the Wine windows or desktop
  beneath them do).
- Outputs other than the primary: XP's and ReactOS's desktop window extends over them, but the wallpaper is drawn only on the primary monitor
  and the others show the background colour (the Windows 7 shell draws the wallpaper per output, as on real hardware).
- In X space the primary monitor comes first, so while dragging a Wine window to an output left of the primary monitor, the part that
  spills onto the neighbouring output is drawn at the original output's scale (smaller). Once moved, it is drawn at that output's scale.
- On outputs with a non-integer scale, X windows are drawn at the right pixels, but the pointer's position relative to a window can be off by
  less than a pixel (layout coordinates are integers).
- Text spacing in GTK apps differs a little from the X11 session (GTK itself draws differently on Wayland and X11).
- Dialogs of Linux apps (windows with a parent) get labwc's frame instead of an XP frame (colours close to XP's Luna;
  `wayland/labwc/themerc-override`); in the X11 session they get no frame.
- GTK4 and libadwaita apps draw client-side decorations, so they should end up with a double frame together with the XP frame (not yet checked).
- The labwc patch is one large patch (`wayland/patches/labwc/0001`), not split by purpose like the Wine patches.
