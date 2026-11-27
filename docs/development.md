# Development

To just use win4wine, `scripts/setup.sh` from [INSTALL.md](../INSTALL.md) is enough. This document covers how to change and test the patches or the bridge.

## Repository layout

| Path | Contents |
|---|---|
| `resources/` | Our own icons (`.ico`, with their SVG), drawn after [XP-Style Icon Guide.md](../XP-Style%20Icon%20Guide.md) |
| `wine/` | The Wine it is based on (`upstream.env`), the patches (`patches/` and `series`), the build (`build.sh`), applying the patches (`patch-state.py`) |
| `bridge-wm/` | The X11 WM (`wm.c`, and `wm.py` that behaves the same), the Win32 proxy (`proxy.c`), the tray broker (`tray.py`), a tool for investigations (`inspect.c`) |
| `wayland/` | The Wayland session: labwc and wlroots versions (`upstream.env`), patches (`patches/`), the build (`build.sh`), labwc's configuration (`labwc/`), startup (`session.sh`) |
| `session/` | Starting and watching the session (`supervisor.py`), the Start menu of Linux apps (`linux_apps.py`), My Network Places (`network_places.py`), `win4wine-wine` for use outside the session (`wine.py`; the Wine environment is in `wineenv.py`), registering the session |
| `prefix/` | Creating the prefix (`create.py`), updating it (`stage.py`), appearance and DPI (`display.py`), registry settings (registry profiles) |
| `extract/` | Taking only the needed files from the ISO (`extract.py`), detecting the XP language (`media.py`) |
| `greeter/` | The XP Welcome screen for LightDM, the log on/log off screens, XP's cursors |
| `scripts/` | Installing everything (`setup.sh`), the development chroot (`debian.sh`), the pre-commit check |
| `tests/` | Integration tests, and probes of public APIs (C) that run both on Wine and on a real XP |
| `localres/`, `.local/` | ISOs, extracted files, prefixes, builds, logs, screenshots. **Not tracked by Git** |

## Git setup

```sh
git config core.hooksPath .githooks
```

The pre-commit check (`scripts/check-staged.py`) refuses files under `localres/` and `.local/`, PE files, registry hives, fonts
and binaries containing NUL bytes (even with `git add -f`). The only exceptions are PNGs in `docs/images/` (the README
screenshot) and our own icons, `.ico` files in `resources/`.

## Development environment in a chroot

The host needs git, sudo and debootstrap, and at least 15 GB free. The chroot is created in `.local/debian`; `/work` inside it is this repository.

```sh
scripts/debian.sh init
scripts/debian.sh run /bin/bash
```

Inside the chroot, build like this:

```sh
cd /work
./wine/build.sh > .local/wine-build.log 2>&1    # fetch, patch and build Wine
# to watch progress: scripts/progress.py .local/wine-build.log Wine -- ./wine/build.sh
./bridge-wm/build.sh                            # .local/bridge (proxy.exe, linux-launch.exe, ...)
./tests/build.sh                                # the probes
./extract/extract.py localres/WindowsXPSP3.iso .local/assets
Xvfb :95 -screen 0 1280x800x24 -nolisten tcp > .local/xvfb.log 2>&1 &
export DISPLAY=:95 WINE=/work/.local/wine-install/bin/wine
./prefix/create.py .local/assets .local/prefix-dev --home /home/builder --wine "$WINE"
```

- `wine/build.sh` takes `WINE_SOURCE`, `WINE_BUILD`, `WINE_INSTALL` and `JOBS` for the locations and the number of parallel jobs.
  Wine is built in the new WoW64 mode (`--enable-archs=i386,x86_64`), so no i386 libraries are needed.
- After changing a patch, check it with a build **from configure, in a fresh source and build directory**. A reused build
  directory keeps old objects, and you will miss omissions (such as a header that was not registered).
- `extract.py` and `create.py` stop if the output already exists. To redo one, create it under a different name.
- `wayland/build.sh` fetches labwc, fetches wlroots and libsfdo as subprojects, patches them and links them statically
  (`LABWC_SOURCE`, `LABWC_BUILD`, `LABWC_INSTALL`, `JOBS`). The chroot needs the packages of `scripts/setup.sh --wayland`.
  To test it nested, run `wayland/session.sh LABWC --config CONFIG` on the chroot's Xvfb with
  `WLR_BACKENDS=x11 WLR_X11_OUTPUTS=2 WLR_RENDERER=pixman` (the outputs are named `X11-1` and `X11-2`).

## Patches

```sh
python3 wine/patch-state.py SOURCE wine/patches --apply   # do they all apply to an unmodified Wine 11.0?
```

The labwc and wlroots patches can be checked with the same tool (`wine/patch-state.py SOURCE wayland/patches/labwc --apply`;
for wlroots, apply `wayland/patches/wlroots` to `SOURCE/subprojects/wlroots`).

The list, order, format and the opt-in check are in [patches.md](patches.md). Never ship a change made directly in
`.local/wine-upstream`: always turn it into a patch.

## Tests

| Test | What it covers |
|---|---|
| `tests/wm_integration.py` | The WM alone (Alt+Tab, focus, minimizing, passing on the Windows key). Runs against either WM |
| `tests/wm_parity.py --wm CMD` | Drives a WM through frames, commands, icons, maximizing and stacking and prints its state after each step; the output of `wm.py` and `wm.c` must be identical (run each on a fresh Xvfb and `diff`) |
| `tests/alt-tab.c` | Windows for the proxy's Alt+Tab rules (modal dialog, hidden owner, tool window) in a running session; its header says which are listed |
| `tests/wine_restore.py` | A Wine window minimized and restored from Win32 is shown again (pass `WINE` and `WINEPREFIX`) |
| `tests/bridge_integration.py` | The WM, the proxy and the XP shell together |
| `tests/native_frames.py` | XP frames on Linux windows (Luna and Classic) |
| `tests/xterm_redraw.py`, `tests/wm_latency.py` | Redrawing and latency |
| `tests/wayland_compositor.py` | The Wayland session's labwc without Wine (nested, scale 1.5): X space, work area, IPC, frame mapping, stacking order, maximizing, always on top. Pass `DISPLAY` (Xvfb) and `LABWC` |
| `tests/tray_host.py` | The tray broker (StatusNotifierItem and XEmbed) |
| `tests/screen_guard.py` | When the supervisor starts the screen saver and the lock screen (`session/screen.py`), with stand-ins; no X, no Wine |
| `tests/idle_watch.py IDLE_WATCH` | `session/idle-watch.c` on an Xvfb: idle and active, `XResetScreenSaver`, DPMS, no wakeups while idle |
| `tests/logind_watch.py` | `session/logind-watch.py` with a stand-in for logind on a private bus: `Lock` and the delayed sleep |
| `tests/linux_apps.py` | The Start menu of Linux apps |
| `tests/iso_mount.py` | Mounting a disc image (`session/iso_mount.py`) with a stand-in for `udisksctl`; no Wine, no loop devices |
| `tests/network_places.py` | My Network Places: names, URLs, mDNS, SSDP and UPnP descriptions (hostile ones too), the shortcuts per device, and which files are touched; no Wine, no network |
| `tests/linuxcols.py` | The plain C part of XP Explorer's Linux columns: the permissions as `ls -l` writes them, user and group names, the paths left alone (host gcc; no Wine, no X) |
| `tests/recovery_integration.py CONFIG.json` | Switching over when the shell crashes |
| `tests/reactos_session.py CONFIG.json` | The ReactOS shell session: taskbar and desktop, folders opened with `explorer.exe` (0090), log off (0024) |
| `tests/reactos_greeter.py ASSETS [OUTDIR]` | The ReactOS login screen's layout (English and Japanese). ASSETS is what `extract/reactos.py` extracted |
| `tests/greeter_layout.py ASSETS`, `tests/cursor_theme.py ASSETS` | The Welcome screen's layout (96, 120 and 144 DPI), the cursors (32 and 64) |
| `tests/patch_state.py`, `tests/font_*.py`, `tests/workarea.py`, `tests/dpi_settings.py` | Unit tests |
| `tests/xp_language.py [ja=ASSETS] [en=ASSETS]` | The registry settings that differ between Japanese and English XP, and detecting the language of extracted files |
| `tests/stat-lx-probe.c` | `NtQueryInformationByName` (Wine patch 0160) in the session's Wine, to compare with `stat`; 32-bit and 64-bit |
| `tests/ole-clipboard-thread.c` | The OLE clipboard after the thread that used it first has ended (Wine patch 0161): `ole-clipboard-thread-test.exe` prints PASS; 32-bit and 64-bit |
| `tests/screen-saver.c` | The screen saver setting, its `system.ini` mapping and `LockWorkStation`, in the session's prefix |
| `tests/*.c` | Probes of public APIs. The same program runs on Wine and on a real XP and the numbers are compared (DPI: `dpi-metrics.c`, `char-widths-dpi.c`, `caption-glyph-dpi.c`, `text-bits-dpi.c`) |

## Trying it in a window (`xp.sh`)

An entry point for trying the XP session on Arch / CachyOS and similar systems without leaving your current desktop (KDE etc.).
It does not touch system files (it tells you which packages are missing).

```sh
./xp.sh setup /path/to/WindowsXP-SP3.iso   # Wine, bridge, extraction, prefix (in the same .local/ as setup.sh)
./xp.sh run 1280x800                       # run the real session (supervisor.py) inside a Xephyr window
./xp.sh log proxy                          # the running session's log (default: xp-shell)
./xp.sh kill                               # stop the session and the prefix's processes
```

A `run` session has its own D-Bus (so tray.py does not take the desktop's StatusNotifierWatcher), and "Turn Off Computer"
in the Start menu only ends that window's session (`shutdown_command` is set to `true`). `XP_PREFIX`, `XP_LOG`, `XP_DISPLAY`
and `XP_WINEDEBUG` choose another prefix, log location, X display and Wine debug channels.

## Installing by hand

To install without `setup.sh`, build Wine, the bridge, the extracted files and the prefix as above, and write
`~/.config/win4wine/session.json` (the keys are in [INSTALL.md](../INSTALL.md#settings-configwin4winesessionjson)).

```sh
./session/install.sh                         # /usr/local/bin/xp-shell-session and the xsessions entry
./session/install.sh --wayland .local/labwc-install/bin/labwc   # optional: the Wayland session too (labwc from wayland/build.sh)
./greeter/install.sh /absolute/path/assets   # optional: the Welcome screen
```

The launcher scripts that `session/install.sh` installs point at this checkout, so reinstall them if you move the checkout.

## Updating an existing setup

1. Stop the session and make sure wineserver has exited.
2. Replace the Wine install.
3. Run `./prefix/stage.py --wine "$WINE" "$WINEPREFIX"`. It checks whether wine.inf changed and runs wineboot, re-registers
   XP's COM servers, applies the registry profiles and places XP's DLLs. It also runs automatically when the session starts.
4. Rebuild the bridge with `bridge-wm/build.sh` and start the session again.

To change appearance and DPI, stop the session and run `prefix/display.py --theme luna|classic --menu xp|classic --dpi N`
(`--theme` also changes the wallpaper, as XP's themes do: none for classic, Bliss for luna). N is, as in XP, 96 (Normal),
120 (Large size), or a custom 19–480. The session passes the same DPI to Linux apps (`Xft.dpi`, fontconfig, the cursor size,
and GTK's `GDK_SCALE` at 192 and above; `session/dpi.py`). The Welcome screen's DPI is set by `"dpi"` in
`/etc/win4wine/greeter.json` (in XP, too, it is a machine-wide setting).

## Importing from an existing XP hive

Install `python3-hivex` and convert read-only. Only the namespace CLSIDs and a limited set of NLS values are handled;
services, drivers, logon and product information are not carried over.

```sh
./prefix/hive-to-reg.py --software .../SOFTWARE --system .../SYSTEM --output .local/shell-hive.reg
```
