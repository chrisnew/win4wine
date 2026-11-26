# Plan: screen savers and a lock screen

Status: planned (2026-11-26). Phase 0 (extracting the screen savers) is done.

Goal: XP's own screen savers start after the idle time set in Display Properties, and Win+L (and "On resume, display Welcome
screen") shows XP's Welcome screen as a lock screen inside the session, without a VT switch. On the EeePC it must be instant and must
not add wakeups while the machine is idle.

## What exists, and what is missing

- XP's Display Properties (desk.cpl, themeui.dll) run in the session, Screen Saver tab included. Until now only `logon.scr`
  ("Windows XP") was extracted, into `C:\xp\native`, where the tab does not look.
- Wine stores `ScreenSaveActive` and `ScreenSaveTimeOut` (win32u `sysparams.c`), but nothing starts a screen saver.
  - winex11 answers `SPI_GETSCREENSAVEACTIVE` and `SPI_SETSCREENSAVEACTIVE` with the X server's own blanking timer
    (`XGetScreenSaver`/`XSetScreenSaver`, `x11drv_main.c`). The tab would show and switch Xorg's blanker, not ours.
  - `DefWindowProc`'s `SC_SCREENSAVE` does nothing (`win32u/defwnd.c`). `SetThreadExecutionState` is a stub.
  - Only dinput posts `WM_WINE_NOTIFY_ACTIVITY`, which calls `XResetScreenSaver` (a joystick counts as activity in Wine games).
- `LockWorkStation` is a stub that returns FALSE (`user32/user_main.c`).
- `greeter/welcome.py` draws XP's Welcome screen. `greeter/xp-splash.py` shows it in the session as an override-redirect window
  that stays on top. `greeter/xp-greeter.py` handles the input (user tile, password field, blinking caret, keys), wired to LightDM.
- The supervisor sleeps in `select()` on the display, a wakeup pipe and pidfds (`Session.wait`), so a pipe from a helper fits in.
  `session/keyboard.py` already reads values from the prefix's `user.reg`.

## Decisions

- **Lock screen: inside the session (option B)**, not LightDM's `dm-tool lock`. A VT switch with a second X server and a Python/GTK
  greeter would take seconds on the Atom and flicker, while XP's lock is instant. The cost is the usual X lock-screen limit: if it
  crashes, the session is open. The supervisor restarts it while the lock is on, and the WM keeps it on top.
- **No Microsoft code handles the password**, as with the greeter: PAM checks it. msgina and logonui only supply images and strings.
- **Idle detection in a small C helper** (`session/idle-watch.c`), not in the WM, so `wm.c` and `wm.py` do not both have to do it.
  It uses XSync's `IDLETIME` counter with alarms, so there are no wakeups while the machine is idle.
- **XP's look of Fast User Switching**: the Welcome screen with the user's tile and "N programs running", not the classic
  "Computer Locked" dialog.

## Phase 0: the screen saver files (done)

- `extract/extract.py`: new group `screensavers` with `scrnsave.scr` (Blank), `ss3dfo.scr` (3D Flying Objects), `ssbezier.scr`
  (Beziers), `ssflwbox.scr` (3D FlowerBox), `ssmarque.scr` (Marquee), `ssmypics.scr` (My Pictures Slideshow), `ssmyst.scr`
  (Mystify), `sspipes.scr` (3D Pipes), `ssstars.scr` (Starfield) and `sstext3d.scr` (3D Text). It is in `ADDED_GROUPS`, so
  `setup.sh` adds it to an earlier extraction by itself (all ten are in I386 as `.SC_` on the XP SP2 CD as well).
- `prefix/extras.py`: copies them and `cpl/logon.scr` into `drive_c/windows/syswow64`, the 32-bit system directory the tab lists.
  `VERSION` 5 → 6, so `setup.sh` applies it to the existing prefix.
- To check on the EeePC: `scripts/setup.sh` adds the group, the files are in syswow64, and the tab lists them. Then preview each
  with the Preview button (`/s`) and in the small monitor (`/p HWND`, a child window in another process).

## Phase 1: screen savers start when the machine is idle

1. **Measure first** (decides how much work comes next):
   - Does the tab list the files, and does its small preview work under Wine? If not, find out with traces of public APIs (which
     directory it lists, what `/p` gets). A Wine patch is likely needed here.
   - Run each `.scr /s` on the EeePC. Record CPU and frame rate (GDI ones: Starfield, Marquee, Mystify, Beziers; OpenGL ones: Pipes,
     Flying Objects, Text, FlowerBox on the GMA 3150). Check that mouse or key input ends each one and that its window covers the
     taskbar. Add the numbers to `tools/eeepc/`.
2. **Wine patch** (opt-in, XP session): winex11 no longer answers `SPI_GET/SETSCREENSAVEACTIVE` with the X server's timer. win32u's
   registry entry answers instead, so the tab shows and stores `ScreenSaveActive`. In the patch table of `docs/patches.md`.
3. **`session/idle-watch.c`** (built by `bridge-wm/build.sh` as `.local/bridge/idle-watch`):
   - Turns off the X server's own blanking (`XSetScreenSaver` timeout 0), since we replace it.
   - Reads timeouts on stdin (`idle SECONDS`, `dpms SECONDS`, `off`) and writes `idle` and `active` lines on stdout.
   - XSync alarm on `IDLETIME` ≥ timeout → `idle`; then an alarm for the counter going back down → `active`.
   - Linux players that call `XResetScreenSaver` (mpv, VLC) reset `IDLETIME`, so they hold the screen saver off without extra work.
4. **Supervisor** (`session/supervisor.py`):
   - Reads `HKCU\Control Panel\Desktop`: `ScreenSaveActive`, `ScreenSaveTimeOut`, `SCRNSAVE.EXE` and `ScreenSaverIsSecure` (reuse
     the reader from `session/keyboard.py`). Watches `user.reg` with inotify and sends the new timeout to idle-watch when it changes.
     wineserver writes `user.reg` a few seconds after a change, which is soon enough.
   - On `idle`: if no screen saver is running, start `wine <SCRNSAVE.EXE> /s` with the session's Wine environment and remember the
     process. If it exits with idle-watch still reporting idle, do not start it again until the next `active`.
   - Skip the screen saver while a full-screen window is active (a game). Decide this after measuring: XP itself does not skip them.
5. **WM** (`wm.c` and `wm.py`, `tests/wm_parity.py`): the screen saver's full-screen topmost Wine window is above the taskbar and
   has focus. Check what the WM already does with it before changing anything.
6. **Monitor off**: `dpms` in `session.json` (seconds, default off). idle-watch sets the X server's DPMS timeouts, so the X server
   does it with no wakeups. XP's Power Options tab is not used for it.

## Phase 2: the lock screen

1. **Shared input code**: move the Welcome screen's input handling out of `greeter/xp-greeter.py` into a module that both use. The
   greeter keeps LightDM as the way to check the password; the lock screen uses PAM.
2. **`greeter/xp-lock.py`**:
   - A full-screen override-redirect window drawn by `welcome.py`, like `xp-splash.py`. It shows only the session's user, with
     "N programs running" under the tile (the string from logonui's resources; the number from the taskbar buttons, i.e. the WM's
     list of windows plus the Wine windows with taskbar buttons; check what is available).
   - It grabs the keyboard and the pointer. If another client holds a grab (an open Wine menu), it retries for a short time and stays
     drawn meanwhile.
   - It checks the password with PAM, through a new `/etc/pam.d/win4wine-lock` (`@include common-auth`, `@include common-account`),
     installed by `greeter/install.sh`. As a normal user, `pam_unix` checks through `unix_chkpwd`, so no root is needed. Python
     needs `python3-pam` or a small ctypes binding to `libpam`; choose when writing it.
   - XP's wrong-password balloon and the password hint come from logonui's strings, as in the greeter.
   - "Turn off computer" on the lock screen goes to the supervisor's shut-down path (later; at first it is hidden).
3. **Supervisor**:
   - Starts `xp-lock.py` on a lock request and writes a lock file in the IPC directory. While that file exists, it starts the lock
     screen again if it exits without unlocking (exit status 0 = unlocked).
   - Plays XP's sounds if they exist (there is no sound for locking; unlocking has none in XP either, so check on real XP).
4. **WM**: the lock window is the topmost window, whatever is mapped or raised after it, including override-redirect menus, Windows 7's
   overlays (`raise_overlays`) and full-screen windows. Recognise it by a property (`_WIN4WINE_LOCK`) instead of relying only on the
   window restacking itself as `xp-splash.py` does. Both `wm.c` and `wm.py`.
5. **Optional hardening** (only if wanted at the event): `DontVTSwitch` and `DontZap` in Xorg's config, so Ctrl+Alt+F1 and
   Ctrl+Alt+Backspace cannot get past the lock. This is a change on the EeePC (`/etc`): ask first, and log it in `tools/eeepc/LOG.md`.

## Phase 3: ways to lock

1. **Win+L**: first find out, with a trace of public APIs (`+relay` limited to `user32.LockWorkStation`), whether XP's Explorer
   calls `LockWorkStation` for Win+L.
   - If it does: a Wine patch makes `LockWorkStation` (opt-in, XP session) write `lock` to a file named by an environment variable,
     like patch 0024 does for log off. The supervisor watches it with inotify, since log off's file is only read when the shell exits.
   - If not: the WM, which already grabs the Windows key, handles Win+L itself and writes the request.
2. **"On resume, display Welcome screen"** (`ScreenSaverIsSecure` = 1): when the screen saver starts, also start the lock screen,
   stacked just below the screen saver's window and without its grabs. When the screen saver exits, the lock screen grabs. This way
   the desktop never shows in between, and the screen saver still gets the mouse movement that ends it.
3. **Lid and suspend**: listen for logind's `Lock` signal on the session (D-Bus) and lock. Check that the lid's suspend on the
   EeePC locks before sleeping, so the Welcome screen is there on waking.
4. **Log Off dialog → Switch User**: only with Fast User Switching shown, and its API is not documented. Leave it out for now.

## Phase 4: programs that hold the screen saver off (only if needed)

- Windows sends `WM_SYSCOMMAND` `SC_SCREENSAVE` to the foreground window, and programs that do not pass it to `DefWindowProc` stop
  the screen saver. Programs also use `SetThreadExecutionState(ES_DISPLAY_REQUIRED)`.
- Check first whether this matters for what will be shown (Windows Media Player 9 playing a video, games). If it does: a small Wine
  helper sends `SC_SCREENSAVE` to the foreground window before the supervisor starts the screen saver, a Wine patch makes
  `DefWindowProc` answer it, and `SetThreadExecutionState` resets the X idle counter as dinput's activity notice does.

## Tests

- `tests/`: idle-watch's protocol with a fake X (or Xvfb in the container: `xdotool` input resets `IDLETIME`), the supervisor's
  state changes (idle → screen saver → active → lock), and the lock window above other windows in the WM's tests, with `wm_parity.py`.
- On the EeePC: wakeups at idle with `tools/eeepc` before and after (no increase allowed), the time from Win+L until the lock screen
  shows, and a screenshot of each screen saver and of the lock screen.

## Changes on the EeePC

- The extraction and prefix update in phase 0 happen through `setup.sh` in the checkout, inside `.local/`.
- `/etc/pam.d/win4wine-lock` and possibly the Xorg options: outside the checkout, so ask first and log them in
  `tools/eeepc/LOG.md` (what, why, result, undo).

## Docs to update with each phase

- `docs/architecture.md`: idle-watch, the lock screen, the lock file in the IPC table.
- `docs/design.md`: why the lock screen is inside the session and why PAM checks the password.
- `docs/patches.md`: each new patch.
- `IDEAS.md`: point "Screensaver / Lockscreen" to this plan.
