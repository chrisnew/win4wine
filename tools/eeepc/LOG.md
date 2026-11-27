# EeePC log

What was changed on the EeePC itself, outside the win4wine checkout, and why: packages, the kernel, `/etc`, services, the
prefix. The machine has to be set up again one day (a new SSD, a reinstall, a second machine for the event), and this is what
turns a working machine back into a recipe.

Changes to win4wine's own code are in Git, not here. Only their deployment is noted here, when it needed something on the
machine (a new Wine install, a relogin).

## How to keep this log

- **Every change goes in, in the same session it is made.** If it was worth doing on the machine, it is worth one entry. That
  includes experiments that were undone: a failed attempt saves someone from trying it again.
- **Newest first**, under a heading `### YYYY-MM-DD: what changed`.
- **Each entry answers four questions:**
  - **What:** the exact commands or file contents, so the step can be done again by copying them.
  - **Why:** the problem it solves, as seen on the machine (a log line, a `top` reading, a symptom).
  - **Result:** what was measured afterwards, with the numbers and how they were taken. If nothing was measured, say so.
  - **Undo:** how to go back.
- **Link the commit** when the change comes with one in the repo (a build script, a patch, a measurement in the
  [README](README.md)).
- **Keep "Current state" true.** When an entry adds, changes or undoes something lasting, update the table as well. The table
  is what a reinstall follows; the entries are why.
- **Unknown reasons stay marked.** If a setting is found on the machine and nobody remembers why, it goes under "Not yet
  explained", not under a guessed reason.
- To check what the machine really has: `apt-mark showhold`, `dpkg -l | grep w4w`, `cat /proc/cmdline`, `ls /etc/X11/xorg.conf.d/`,
  `cat /etc/modules`, `/var/log/apt/history.log*` (every apt command, with its date), and `journalctl -k -b` / `dmesg` for what
  the kernel made of it.

## Current state

The machine against a fresh Debian 13 (trixie) install. Last checked on the machine: 2026-11-27.

| Area | Setting | Why | Since |
|---|---|---|---|
| Kernel | `linux-image-amd64` from trixie-backports, now 7.3.4 (`7.3.4-1~bpo13+1`). 6.12 stays installed as a fallback in GRUB | ntsync (`/dev/ntsync`); Debian 13's 6.12 has no usable ntsync | 2026-11-26 |
| Kernel module | `ntsync` in `/etc/modules` | Loads ntsync at boot. `/dev/ntsync` comes up `crw-rw-rw-`, so no udev rule is needed | 2026-11-26 |
| APT | `/etc/apt/sources.list.d/debian-backports.sources` (trixie-backports, main non-free-firmware) | The kernel | 2026-11-26 |
| X driver | `xserver-xorg-video-intel 2:2.99.917+git20210115-1+w4w1`, built by [xorg-intel/build.sh](xorg-intel/build.sh) with `sna-gen3-dri3.patch`; **held** with `apt-mark hold` | DRI3 on the GMA 3150, so OpenGL runs on the GPU instead of llvmpipe | 2026-11-26 |
| X config | `/etc/X11/xorg.conf.d/20-intel.conf`: `Driver "intel"`, `Option "DRI" "3"` | The patched driver gives gen3 DRI3 only when asked | 2026-11-26 |
| X config | `/etc/X11/xorg.conf.d/10-nodpms.conf`: blanking, standby, suspend and off times 0; `DPMS` false on LVDS1 | The screen never blanks (see "Not yet explained") | ? |
| Wine | `.local/wine-install` built with ntsync (repo 0a46acc), patches up to Wine 0160, with the changes to 0066 and 0079 (repo 49f4a46) | Wine uses `/dev/ntsync` for its synchronization instead of asking the wineserver; 0157 batches immediate mode GL calls; 0158 and 0159 are for the screen savers and the lock screen; 0160 gives the Linux columns each file's mode and owner | 2026-11-27 |
| Display manager | LightDM, `/etc/lightdm/lightdm.conf.d/95-xp-greeter.conf`: `greeter-session=xp-greeter`, `user-session=xp-shell`; the greeter's code and XP files in `/usr/local/share/win4wine/greeter` | The XP logon screen and the win4wine session (win4wine's install) | 2026-11-23, reinstalled 2026-11-27 |
| PAM | `/etc/pam.d/win4wine-lock` (`@include common-auth`, `@include common-account`), installed by `greeter/install.sh` | The session's lock screen checks the password with it | 2026-11-27 |
| Session config | `~/.config/win4wine/session.json`: wine, prefix, `en_US.UTF-8`. `session.json.before-trace` is the copy kept while a `+server` trace was on | | 2026-11-23 |
| Prefix | `prefix/extras.py` VERSION 12: "Open in Linux Terminal" (`C:\xp\bin\linux-launch.exe` and the context menu handler in `linuxcols.dll`). Since VERSION 9: XP's `actxprxy.dll`, native in XP Explorer only. Since VERSION 8: the Linux columns (`C:\xp\bin\linuxcols.dll`, registered as a column handler, and the `Details` property list on `HKCR\*` and `HKCR\Directory`). Since VERSION 7: XP's screen savers in `drive_c/windows/syswow64`, and the `IniFileMapping` entry for `SCRNSAVE.EXE`. Set by hand ahead of VERSION 13: IE's `NoUpdateCheck`. In Display Properties (the user's choice, changes freely): 3D Flying Objects, 5 minutes, password protect | [Screen savers and the lock screen](../../plans/screensaver-and-lock.md) | 2026-11-27 |
| Prefix | Office's `MDM` and `MOSEARCH` services `Start=3` (manual) instead of automatic | MOSEARCH failed at every login (missing `TQUERY.DLL`); neither is needed by the session | 2026-11-26 |
| Boot | `GRUB_CMDLINE_LINUX_DEFAULT="zswap.enabled=1 zswap.compressor=zstd"` | Compressed swap cache, for 2 GB of RAM. **zstd is not actually used**, see "Open" | ? |
| Disk | `/` mounted `noatime,nodiratime`; 9.5 GB swap partition `/dev/sda2` | ? (the disk is a spinning HDD, Toshiba MK2552GSX) | ? |
| Power | `tlp` installed, configuration untouched | | 2026-11-23 |
| Colour temperature | `redshift-gtk` (brings `redshift`, `geoclue-2.0` and `avahi-daemon`, which is enabled); `~/.config/redshift.conf` with `location-provider=manual`; autostart switched off in `~/.config/autostart/redshift-gtk.desktop` | Trying whether it works as installed | 2026-11-27 |
| Remote access | `openssh-server` | Driving and measuring the machine from the development machine | 2026-11-23 |
| Profiling | `linux-perf` 7.3.4 (trixie-backports, matches the kernel) and `strace` | Profiling Wine programs on the machine itself; `perf_event_paranoid` stays 3, so perf runs with sudo | 2026-11-26 |
| Build tools | build-essential, MinGW, Xvfb, python3-xlib and the rest of win4wine's build dependencies | Building Wine and the bridge on the machine (Wine is now built in a container on the development machine) | 2026-11-23 |
| Network | `avahi-daemon` (first installed with `redshift-gtk`), marked as manually installed | mDNS for My Network Places (`session/network_places.py`) | 2026-11-27 |

## Open

- **zswap runs with lzo, not zstd.** The kernel log at every boot says `zswap: compressor zstd not available, using default lzo`:
  zswap starts before the zstd module is loaded, and the initramfs does not have it. Fix to try: add `zstd` to
  `/etc/initramfs-tools/modules`, run `sudo update-initramfs -u`, reboot, and check
  `cat /sys/module/zswap/parameters/compressor`. Or set `zswap.compressor=lz4`, which may be cheaper on an Atom anyway. Not
  done yet; nothing measured.
- **ntsync is live but not measured on its own.** The AoE 2 numbers of 2026-11-26 changed the driver and ntsync together.
- **The DRI3 driver's rendering is not confirmed yet.** It is fast; whether everything draws correctly (AoE 2, other GL
  programs, the session itself) still needs a look.
- **An SSD** instead of the HDD is being considered.

## Not yet explained

Found on the machine with no reason recorded. Add the reason, or undo the setting.

- `10-nodpms.conf` (the screen never blanks). Probably for the event, so the screen stays on while it is shown.
- zswap on the kernel command line, and `noatime,nodiratime` in `/etc/fstab`.
- `bluetooth.service` and `ModemManager.service` are disabled, although Debian enables them when they are installed.
- Packages from before win4wine: Window Maker with its dockapps, sway, pasystray, network-manager-gnome, hexchat, Firefox ESR.
  Left from the first desktop setup; not needed by the session.

## Entries

### 2026-11-27: Internet Explorer opens web pages

- **What** (at the logon screen, no session running), in `~/win4wine`: `git fetch fork && git checkout -B eeepc fork/eeepc`
  (2cfaad2 to dba2ad6), then in the prefix:

  ```sh
  python3 session/wine.py reg add 'HKCU\Software\Microsoft\Internet Explorer\Main' /v NoUpdateCheck /t REG_DWORD /d 1 /f
  ```

  `prefix/extras.py` (VERSION 13 sets the same value) was not applied, so `.xp-extras` still says 12 and the next
  `scripts/setup.sh` runs it.
- **Why:** IE offered every page as a download (39082ae: the session disabled its HTML engine), and started without an
  address it went to a microsoft.com update page (dba2ad6).
- **Result:** the value is in `user.reg`, the session's environment now has `mscoree=` without `mshtml`, and the
  wineserver ended by itself. In IE itself not yet checked on the machine; both fixes were checked in the container session.
- **Undo:** `python3 session/wine.py reg delete 'HKCU\Software\Microsoft\Internet Explorer\Main' /v NoUpdateCheck /f`, and
  check out 2cfaad2.

### 2026-11-27: Open in Linux Terminal on folders

- **What** (logged off, repo 2cfaad2), in `~/win4wine`: `git fetch fork && git checkout -B eeepc fork/eeepc` (6d7dcb8 to
  2cfaad2) and `bridge-wm/build.sh`; the registry files, `.xp-extras` and `.xp-defaults` copied to
  `.local/backup-before-extras12/`; then the prefix additions as `scripts/setup.sh` runs them, and `wineserver -k` and `-w`:

  ```sh
  xvfb-run -a -s '-screen 0 1280x800x24' sh -c 'p=$PWD/.local/prefix w=$PWD/.local/wine-install/bin/wine
      python3 prefix/extras.py $p --wine $w && python3 session/linux_apps.py $p --wine $w &&
      python3 xp-explorer/scripts/pins.py $p --wine $w' > .local/extras12.log 2>&1
  ```

- **Why:** folders and drives open in a Linux terminal from their context menu ([plan](../../plans/linux-terminal.md)).
- **Result:** additions 270 s, exit status 0, `extras.py --check` passes (VERSION 12), `tests/linuxcols.py` passes, and both
  helpers in `C:\xp\bin` are the ones just built. Added to `system.reg`: the handler's class, its `Approved` value and its
  three `ContextMenuHandlers` keys; `user.reg` and `userdef.reg` have the same keys and values as before. The display
  adapter reads "Wine Adapter" after the run under Xvfb. One other change: IE6's registration took back
  `HKCR\htmlfile\shell\Print\command`, which was Office XP's
  `"C:\Program Files (x86)\Microsoft Office\Office10\msohtmed.exe" /p %1`; not put back. After logging in, it works on
  the machine; the time from the click to the prompt is not measured.
- **Undo:** with `WINEPREFIX` set to the prefix, `wine reg delete 'HKCR\<class>\shellex\ContextMenuHandlers\LinuxTerminal' /f`
  for `Directory`, `Drive` and `Directory\Background` (until `extras.py` is applied again). Or copy the backed up `*.reg` and
  `.xp-extras` back with no wineserver running, and check out 6d7dcb8 and build again.

### 2026-11-27: My Network Places lists the local network

- **What** (logged off, repo 6d7dcb8), in `~/win4wine`: `git fetch fork && git checkout -B eeepc fork/eeepc` (8858f43 to
  6d7dcb8) and `bridge-wm/build.sh`; `sudo apt-mark manual avahi-daemon`; registry files, `.xp-extras`, `.xp-defaults` and
  the user's `Network Shortcuts` folder copied to `.local/backup-before-extras10/`; then the prefix additions as
  `scripts/setup.sh` runs them, and `wineserver -k` and `-w`:

  ```sh
  xvfb-run -a -s '-screen 0 1280x800x24' sh -c 'p=$PWD/.local/prefix w=$PWD/.local/wine-install/bin/wine
      python3 prefix/extras.py $p --wine $w && python3 session/linux_apps.py $p --wine $w &&
      python3 xp-explorer/scripts/pins.py $p --wine $w' > .local/extras10.log 2>&1
  ```

- **Why:** My Network Places showed nothing of the network, and one item without a name. The new
  `session/network_places.py` needs `avahi-daemon`, which had come only with `redshift-gtk`
  ([plan](../../plans/network-places.md)).
- **Result:** additions 270 s, exit status 0. The blank item's key is gone from `system.reg`; `user.reg` and `userdef.reg`
  have the same keys and values as before. The display adapter that Wine had noted under Xvfb was "Intel" again after the
  login. In the session at home the helper wrote two shortcuts (the router's web page over UPnP, one computer over mDNS);
  its cost is in the [README](README.md#my-network-places).
- **Undo:** `sudo apt-mark auto avahi-daemon`; `"network_places": false` in `session.json` stops the helper and removes its
  shortcuts. The blank item comes back by adding the key
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\NetworkNeighborhood\NameSpace\{3c5c43a3-9ce9-4a9b-9699-2ac0cf6cc4bf}`
  (`/reg:32`).

### 2026-11-27: XP's actxprxy.dll in the prefix, for Customize Notifications

- **What** (logged off, repo 8858f43), in `~/win4wine`: `git fetch fork && git checkout -B eeepc fork/eeepc` (a11d911 to
  8858f43); registry files and `.xp-extras` copied to `.local/backup-before-extras9/`; then the new extraction group and
  the prefix additions, the latter as `scripts/setup.sh` runs them:

  ```sh
  TMPDIR=$PWD/.local/tmp python3 extract/extract.py --only=com \
      ~/Images/en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso .local/assets
  xvfb-run -a -s '-screen 0 1280x800x24' sh -c 'p=$PWD/.local/prefix w=$PWD/.local/wine-install/bin/wine
      python3 prefix/extras.py $p --wine $w && python3 session/linux_apps.py $p --wine $w &&
      python3 xp-explorer/scripts/pins.py $p --wine $w' > .local/extras9.log 2>&1
  ```

- **Why:** Taskbar Properties » Customize showed an empty list: Wine's `actxprxy` lacks the proxies for the notification
  area's COM interfaces.
- **Result:** extraction 35 s, additions 262 s, exit status 0. XP's DLL is in `drive_c/xp/native` and `syswow64`, the two
  interfaces are registered, and in `user.reg` the only values that differ from the backup are the two `actxprxy` DLL
  overrides. The dialog was then confirmed to work on the machine.
- **Undo:** `wine reg delete 'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides' /v actxprxy /f` with
  `WINEPREFIX` set to the prefix; Explorer then loads Wine's DLL again (until `extras.py` is applied again).

### 2026-11-27: Wine without the grey square on the tray's chevron button installed

- **What** (logged off): `git fetch fork && git checkout -B eeepc fork/eeepc` (4a956d9 to 49f4a46); on the development machine
  `wine/container-build.sh --deploy 192.168.1.9`, which went straight into `.local/wine-install` (the Wine up to d2619cd is
  now `.local/wine-install-old`). The installed one before had the patch series of 4a956d9 (compared by its digest), so only
  0066 and 0079 changed.
- **Why:** a grey square showed around the round button that opens the hidden notification icons.
- **Result:** checked before in the container session. After logging in, a screenshot shows the button without the square;
  the session runs the new `wineserver` and the prefix has the new `win4wine-controls.dll`.
- **Undo:** logged off, `mv .local/wine-install .local/wine-install-chevron && mv .local/wine-install-old .local/wine-install`.

### 2026-11-27: Linux columns deployed

- **What:** `git fetch fork && git checkout -B eeepc fork/eeepc` (f503652 to d2619cd) and `bridge-wm/build.sh` with the session
  running; on the development machine `wine/container-build.sh --deploy 192.168.1.9`, run again after logging off (the Wine up
  to 0159 is now `.local/wine-install-old`). Logged off: copies of the prefix's `*.reg`, `system.ini` and `.xp-extras` in
  `.local/backup-2026-11-27-linuxcols/`, then, as `setup.sh` does after a Wine build, in `xvfb-run`:
  `prefix/extras.py .local/prefix --wine W`, `session/linux_apps.py .local/prefix --wine W` and
  `xp-explorer/scripts/pins.py .local/prefix --wine W` (415 s), then `wineserver -k` and `-w`.
- **Why:** Linux permissions, owner, group and path in XP's folder windows ([plan](../../plans/linux-columns.md)).
- **Result:** `extras.py --check` passes (VERSION 8). `tests/linuxcols.py` passes, and `tests/stat-lx-probe.c`, 32-bit and
  64-bit, has no failed checks and agrees with `stat`. After logging in, the columns and the sidebar's Details section work in
  XP's folder windows; opening and sorting `Z:\usr\bin` and the idle use were measured (the [README](README.md#linux-columns)).
  A long file name without a `/` was cut off in the Details section (fixed in the repo, not installed here yet). Left in the
  prefix's view settings: the four columns on `Z:\usr\bin` and `Z:\run\user\1000`.
- **Undo:** logged off, `mv .local/wine-install .local/wine-install-0160 && mv .local/wine-install-old .local/wine-install`,
  copy the backup's `*.reg`, `.xp-extras` back into `.local/prefix` and `system.ini` into `drive_c/windows`,
  `git checkout -B eeepc f503652` and `bridge-wm/build.sh`.

### 2026-11-27: lock screen started ahead deployed

- **What:** `git fetch fork && git checkout -B eeepc fork/eeepc` (5e20db8 to fc82c2e), then log off and in. Nothing outside
  the checkout changed.
- **Why:** locking took 2.2 s.
- **Result:** 0.57-0.61 s, immediate at the machine; the waiting lock screen takes 54-56 MB and no wakeups, and a new one
  waits after each unlock (numbers in the [README](README.md#screen-savers-and-the-lock-screen)). `tests/screen_guard.py`
  passes.
- **Undo:** `git checkout -B eeepc 5e20db8`, then log off and in.

### 2026-11-27: redshift-gtk installed

- **What:** `apt install redshift-gtk` (00:17; with `redshift`, `geoclue-2.0`, `avahi-daemon` and their libraries).
  `~/.config/redshift.conf` has `[redshift] location-provider=manual` and a `[manual]` section with the location (not copied
  here). `~/.config/autostart/redshift-gtk.desktop` has `Hidden=true` and `X-GNOME-Autostart-enabled=false`.
- **Why:** to see whether redshift-gtk works as installed.
- **Result:** it does. Checked later the same day: `avahi-daemon` is running; a `redshift -v` started at 00:24 was still running
  after that session had been logged off (its parent gone); it was then ended by hand.
- **Undo:** `sudo apt remove redshift-gtk redshift geoclue-2.0 avahi-daemon && sudo apt autoremove`, and delete the two files.

### 2026-11-27: screen savers and the lock screen deployed

- **What** (logged off): `git fetch fork && git checkout -B eeepc fork/eeepc` (da48e71 to 6b3f9bc); on the development machine
  `wine/container-build.sh --deploy 192.168.1.9` (the Wine of 0157 is now `.local/wine-install-old`); copies of the prefix's
  `*.reg`, `system.ini` and `.xp-extras` in `.local/backup-2026-11-27/`; `scripts/setup.sh --media
  ~/Images/en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso --greeter`, which installed no packages,
  rebuilt the bridge (now with `idle-watch`), applied `extras.py` VERSION 7, installed `/etc/pam.d/win4wine-lock` and
  reinstalled the greeter; `sudo systemctl restart lightdm`.
- **Why:** XP's screen savers did not start and nothing locked the screen ([plan](../../plans/screensaver-and-lock.md)).
- **Result:** the password check answers right, the login screen works, and the plan's manual tests passed: Win+L (also from
  Notepad and xterm), the wrong-password balloon, no keys past the lock screen, `LockWorkStation`, the saver chosen in the tab,
  the 5 s grace of "password protect", a saver over the lock screen and over VS Code, lid close and wake, log off.
  `loginctl lock-session` locks, and a killed lock screen is back after 2.3 s. Idle use did not go up; locking takes 2.2 s
  (numbers in the [README](README.md#screen-savers-and-the-lock-screen)). The Windows XP logo screen saver shows only black.
  `tests/logind_watch.py` passes; `tests/screen_guard.py` had a race of its own on the Atom (fixed in 9e39fba). Each unlock
  logs `pam_unix(win4wine-lock:account): setuid failed: Operation not permitted`; the unlock works all the same.
  The probe `tests/screen-saver.c` was not run.
- **Undo:** the plan, "The way back". Only the new features: `"lock_screen": false` and `"screen_saver": false` in
  `~/.config/win4wine/session.json`, then log in again.

### 2026-11-27: XP's screen savers copied into the prefix

- **What:** `python3 extract/extract.py --only screensavers ~/Images/en_windows_xp_professional_with_service_pack_3_x86_cd_vl_x14-73974.iso .local/assets`
  (10 files), then `cp .local/assets/cpl/logon.scr .local/assets/screensavers/*.scr .local/prefix/drive_c/windows/syswow64/`
  with the session running. `prefix/extras.py` was not run, because it stops the wineserver; the next `setup.sh` does the same
  copy (VERSION 6, repo c4374ca).
- **Why:** to try XP's Screen Saver tab and the screen savers under Wine ([plan](../../plans/screensaver-and-lock.md), phase 0).
- **Result:** 11 `.scr` files in syswow64 (`logon.scr` was there already). XP's Screen Saver tab, opened again after the copy (it
  lists the files when it opens), shows all of them by name: (None), 3D FlowerBox, 3D Flying Objects, 3D Pipes, 3D Text, Beziers,
  Blank, Marquee, My Pictures Slideshow, Mystify, Starfield, Windows XP. The small preview (`/p`) of Windows XP draws, after a
  moment in which the monitor shows a stale piece of the screen. The screen savers themselves were not run yet.
- **Undo:** delete `scrnsave.scr` and `ss*.scr` from `drive_c/windows/syswow64`.

### 2026-11-26: Wine with 0157 (batched immediate mode GL) installed

- **What:** on the development machine, `wine/container-build.sh --deploy 192.168.1.9` (no session was running, so it went
  straight into `.local/wine-install`; `.local/wine-install-old` is the ntsync install of 21:18). The checkout was moved to
  4815ba5 with `git fetch fork && git checkout -B eeepc fork/eeepc`.
- **Why:** Wine 0157 sends glBegin/glVertex/... across in one unix call per batch instead of one each. Half-Life Alpha spent
  about 59% of its render thread on those calls (see the 2026-11-26 linux-perf entry).
- **Result:** Half-Life Alpha's render thread makes 10,000 `arch_prctl` calls a second instead of 361,000, and the share of it
  in Mesa and the driver rose from about 37% to 60% (numbers in the [README](README.md#opengl-on-the-gma-3150)). One
  unexplained crash in the game's code in the first run with it; not seen again.
- **Undo:** move `.local/wine-install-old` back to `.local/wine-install`, then log out and in. Or keep the install and set
  `WINE_OPENGL_BATCH=0` for one program.

### 2026-11-26: linux-perf and strace installed

- **What:** `sudo apt-get install -t trixie-backports linux-perf strace` (perf 7.3.4, the same version as the kernel; trixie's
  own is 6.12). `/proc/sys/kernel/perf_event_paranoid` is left at 3, so perf runs with `sudo`.
- **Why:** OpenGL games are slow although they run on the GPU: Half-Life Alpha's render thread used a whole logical CPU
  (69% user, 32% kernel) and needed a profile to say where.
- **Result:** both run (`perf --version`, `strace -V`). The profile itself is in the [README](README.md#opengl-on-the-gma-3150).
- **Undo:** `sudo apt-get remove linux-perf strace`, then `sudo apt-get autoremove`.

### 2026-11-26: Wine with ntsync and patches up to 0156 installed

- **What:** on the development machine, `wine/container-build.sh --deploy 192.168.1.9`. Then a logout and login. The install is
  in `.local/wine-install` (files dated 21:18); `.local/wine-install-old` is the one before it.
- **Why:** to use ntsync (built in since 0a46acc), Wine 0155 (each Unix program start leaked two files: xptray had 6224 fds
  after two days) and Wine 0156 (GetProp answered from a cache).
- **Result:** `lsmod` shows `ntsync` in use 921 times with the session and AoE 2 running. AoE 2 holds ntsync files (the game 12,
  the wineserver 884). Wine 0156 is not measured yet.
- **Undo:** move `.local/wine-install-old` back to `.local/wine-install`, then log out and in.

### 2026-11-26: patched intel X driver, for DRI3 on the GMA 3150

- **What:** on the development machine, `tools/eeepc/xorg-intel/build.sh` (2a1b72a). Then on the EeePC:
  ```
  sudo apt-get install -y ./xserver-xorg-video-intel_2.99.917+git20210115-1+w4w1_amd64.deb
  sudo apt-mark hold xserver-xorg-video-intel
  ```
  plus `/etc/X11/xorg.conf.d/20-intel.conf`:
  ```
  Section "Device"
      Identifier "Intel"
      Driver     "intel"
      Option     "DRI" "3"
  EndSection
  ```
  and a logout, which restarts X.
- **Why:** every OpenGL program used llvmpipe, software rendering. Mesa 25 has no DRI2 client any more, and SNA enables DRI3
  only from gen4 on, whatever `Option "DRI"` says. Mesa's i915 driver itself works on the device (through GBM). AoE 2 took
  295% of the four logical CPUs, 192% of it in llvmpipe threads.
- **Result:** `glxinfo -B` shows `i915 (chipset: Pineview M)`, OpenGL 2.1. AoE 2 in a game, 10 s of `top`: the game 124% (main
  thread 68%, wined3d 45%), wineserver 17%, Xorg 21%, PulseAudio 15%. The CPUs are 48% idle instead of 17% (3ca9f1e, details in
  the [README](README.md#opengl-on-the-gma-3150)).
- **Undo:** `sudo apt-mark unhold xserver-xorg-video-intel`,
  `sudo apt install --reinstall xserver-xorg-video-intel=2:2.99.917+git20210115-1`, remove `20-intel.conf`, log out.

### 2026-11-26: kernel 7.3 from trixie-backports, for ntsync

- **What:**
  - `/etc/apt/sources.list.d/debian-backports.sources`:
    ```
    Types: deb
    URIs: http://deb.debian.org/debian
    Suites: trixie-backports
    Components: main non-free-firmware
    Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
    ```
  - `sudo apt update && sudo apt install -t trixie-backports linux-image-amd64 linux-headers-amd64`
  - `ntsync` added to `/etc/modules`, then a reboot.
  - The installed kernel is 7.3.4 (`7.3.4-1~bpo13+1`). The meta package now follows backports, so `apt upgrade` brings
    newer 7.x kernels.
- **Why:** ntsync gives Wine the NT synchronization calls in the kernel, instead of a round trip to the wineserver for each
  wait. Games and Office wait a lot, and the wineserver is one of the biggest CPU users on the Atom. Debian 13's 6.12 kernel is
  too old for it, and its `linux/ntsync.h` lacks `NTSYNC_IOC_EVENT_READ`, so Wine is built against the backports headers
  (0a46acc).
- **Result:** `/dev/ntsync` exists, mode 0666, no udev rule needed. Not measured apart from the X driver change (see above).
- **Undo:** choose 6.12 in GRUB's "Advanced options". To stay there: `sudo apt install linux-image-amd64/trixie` and remove the
  7.3 packages. Wine needs no change: without `/dev/ntsync` it falls back to the wineserver's synchronization.

### 2026-11-26: Office's MDM and MOSEARCH services set to manual

- **What:** in the prefix's registry, `HKLM\System\CurrentControlSet\Services\MDM` and `...\MOSEARCH`: `Start` from 2
  (automatic) to 3 (manual). Only in this prefix, not in win4wine's prefix setup.
- **Why:** MOSEARCH failed at every login (it needs `TQUERY.DLL`, which is missing), and neither is needed by the session.
- **Result:** not measured.
- **Undo:** set `Start` back to 2.

### 2026-11-26: htop, Xwayland and wlr-randr installed

- **What:** `apt install htop`, `apt-get install xwayland wlr-randr`.
- **Why:** htop for watching threads during measurements; Xwayland and wlr-randr for trying the Wayland session.
- **Undo:** `apt remove` them.

### 2026-11-23: win4wine set up

- **What:** `openssh-server`, `git`, `tmux`, win4wine's build dependencies (MinGW, Xvfb, python3-xlib, LightDM and the GTK
  greeter, …), the checkout in `~/win4wine` on branch `eeepc`, and win4wine's install (the XP greeter, the `xp-shell`
  session, `~/.config/win4wine/session.json`). Also `tlp`, with its default configuration.
- **Why:** to run and develop win4wine on the machine.

### 2026-11-22: Debian 13 installed

- Debian 13 with the laptop task, `intel-microcode`, then Xorg with Window Maker and xdm for a first desktop.
