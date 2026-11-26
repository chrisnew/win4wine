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

The machine against a fresh Debian 13 (trixie) install. Last checked on the machine: 2026-11-26.

| Area | Setting | Why | Since |
|---|---|---|---|
| Kernel | `linux-image-amd64` from trixie-backports, now 7.3.4 (`7.3.4-1~bpo13+1`). 6.12 stays installed as a fallback in GRUB | ntsync (`/dev/ntsync`); Debian 13's 6.12 has no usable ntsync | 2026-11-26 |
| Kernel module | `ntsync` in `/etc/modules` | Loads ntsync at boot. `/dev/ntsync` comes up `crw-rw-rw-`, so no udev rule is needed | 2026-11-26 |
| APT | `/etc/apt/sources.list.d/debian-backports.sources` (trixie-backports, main non-free-firmware) | The kernel | 2026-11-26 |
| X driver | `xserver-xorg-video-intel 2:2.99.917+git20210115-1+w4w1`, built by [xorg-intel/build.sh](xorg-intel/build.sh) with `sna-gen3-dri3.patch`; **held** with `apt-mark hold` | DRI3 on the GMA 3150, so OpenGL runs on the GPU instead of llvmpipe | 2026-11-26 |
| X config | `/etc/X11/xorg.conf.d/20-intel.conf`: `Driver "intel"`, `Option "DRI" "3"` | The patched driver gives gen3 DRI3 only when asked | 2026-11-26 |
| X config | `/etc/X11/xorg.conf.d/10-nodpms.conf`: blanking, standby, suspend and off times 0; `DPMS` false on LVDS1 | The screen never blanks (see "Not yet explained") | ? |
| Wine | `.local/wine-install` built with ntsync (repo 0a46acc), patches up to Wine 0157 (repo fd9ec4a) | Wine uses `/dev/ntsync` for its synchronization instead of asking the wineserver; 0157 batches immediate mode GL calls | 2026-11-26 22:16 |
| Display manager | LightDM, `/etc/lightdm/lightdm.conf.d/95-xp-greeter.conf`: `greeter-session=xp-greeter`, `user-session=xp-shell` | The XP logon screen and the win4wine session (win4wine's install) | 2026-11-23 |
| Session config | `~/.config/win4wine/session.json`: wine, prefix, `en_US.UTF-8`. `session.json.before-trace` is the copy kept while a `+server` trace was on | | 2026-11-23 |
| Prefix | XP's screen savers (`logon.scr`, `scrnsave.scr`, `ss*.scr`) in `drive_c/windows/syswow64`, copied by hand ahead of `prefix/extras.py` VERSION 6 | Trying the Screen Saver tab ([plan](../../plans/screensaver-and-lock.md)) | 2026-11-27 |
| Prefix | Office's `MDM` and `MOSEARCH` services `Start=3` (manual) instead of automatic | MOSEARCH failed at every login (missing `TQUERY.DLL`); neither is needed by the session | 2026-11-26 |
| Boot | `GRUB_CMDLINE_LINUX_DEFAULT="zswap.enabled=1 zswap.compressor=zstd"` | Compressed swap cache, for 2 GB of RAM. **zstd is not actually used**, see "Open" | ? |
| Disk | `/` mounted `noatime,nodiratime`; 9.5 GB swap partition `/dev/sda2` | ? (the disk is a spinning HDD, Toshiba MK2552GSX) | ? |
| Power | `tlp` installed, configuration untouched | | 2026-11-23 |
| Remote access | `openssh-server` | Driving and measuring the machine from the development machine | 2026-11-23 |
| Profiling | `linux-perf` 7.3.4 (trixie-backports, matches the kernel) and `strace` | Profiling Wine programs on the machine itself; `perf_event_paranoid` stays 3, so perf runs with sudo | 2026-11-26 |
| Build tools | build-essential, MinGW, Xvfb, python3-xlib and the rest of win4wine's build dependencies | Building Wine and the bridge on the machine (Wine is now built in a container on the development machine) | 2026-11-23 |

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
