# Plan: mount ISO images as CD drives

Status (2026-11-27): phases 0 to 2 are done and on the EeePC. AutoRun is open (phase 3). The probes changed phase 2 and
parts of phase 1: read [As built](#as-built-2026-11-27) first, the sections after it are the plan as it was made.

Goal: double-clicking an `.iso` file in XP's Explorer mounts it on the Linux side, and a few moments later it is a CD-ROM drive
in My Computer (`D:`, the first free letter), with the disc's label and a CD icon. If the disc has an `autorun.inf`, its setup
screen starts, as when a CD goes into a real drive. Right-clicking the drive and choosing XP's own **Eject** unmounts the image,
and the drive goes away. For the demo, a visitor double-clicks `Office XP.iso` on the desktop and the Office XP setup appears,
or opens `D:\` of the Age of Empires II image. The mounting is done by Linux (the kernel's loop device and iso9660 driver),
not by Wine reading the file.

## As built (2026-11-27)

How it works is in [docs/architecture.md](../docs/architecture.md#disc-images-sessioniso_mountpy). Where it differs from the plan:

- **Phase 0.** `udisksctl` works from the session without a password (`loop-setup` 0.90 s, `mount` 0.33 s). A drive appears
  with the label and a CD icon, and Explorer opens a window of a disc that arrives. `cr` cannot open the loop device, but
  XP's Eject does not need to: **no udev rule**.
- **XP's Eject sends `IOCTL_DISK_EJECT_MEDIA` to the volume** (`\\?\Volume{…}`) and nothing else: no lock, no dismount.
  `mountmgr.sys` handles that device, so nothing would have reached the planned `ntdll` hook. Phase 2 is **Wine 0164 in
  `mountmgr`**, with `udisksctl unmount` and `loop-delete`. The `ntdll` unmount fix was not made: XP does not use that path.
- **`mountmgr` announced arrivals wrongly** (every change was one, and a removal after an unmount was not said), which would
  have left D: in My Computer after an Eject: **Wine 0163**. The D-Bus risk below was real: **Wine 0162**.
- **No pipe from a Unix program.** Wine gives a Windows program no process handle for it, and cannot pass an anonymous
  pipe, so the `udisksctl` steps are in **`session/iso_mount.py`**, which writes its result to a file.
  `loop-setup` always gets `--no-partition-scan`, which settles hybrid images.
- **Opening the drive** starts `xp-explorer.exe` with the path. The default verb goes over DDE, which fails from the launcher.
- **A wait of 2 s** before the window of a disc mounted just now, or Explorer shows it without its label and address.
- **Found while testing:** an Eject froze My Computer, by a race in unmodified Wine's Uniscribe font cache: **Wine 0165**.
- **Eject with a window of the drive open** does nothing and says nothing (the plan expected "the drive is in use").
- **Unmounting from Linux** leaves an empty drive; deleting the loop device removes it.
- **Not tested:** a hybrid ISO (there is none on the machine).
- **No AutoRun.** XP looks for `autorun.inf` but does not read it, whether the disc was there at logon or arrives. Setup
  screens therefore do not start by themselves. The launcher could run the `open=` line itself (phase 3).

## What exists

- **Wine already turns mounted discs into drive letters.** `mountmgr.sys` (`dlls/mountmgr.sys/dbus.c`) listens to UDisks2 on
  the system bus. A block device with an `iso9660` file system becomes a removable `DEVICE_CDROM` (`udf`: `DEVICE_DVD`), and
  `add_dos_device` gives it the first free letter from `D:` to `Z:` (`add_drive` in `unixlib.c`: `dosdevices/d:` is the mount
  point, `d::` the device). It reads the label (`IdLabel`) and the UUID too. When the block device disappears
  (`InterfacesRemoved`), the letter is removed. Arrival and removal are broadcast as `WM_DEVICECHANGE`
  (`DBT_DEVICEARRIVAL` / `DBT_DEVICEREMOVECOMPLETE`), which XP's shell listens to.
- **The EeePC's Wine has D-Bus support** (`SONAME_LIBDBUS_1` in the build's `config.h`, `libdbus-1-dev` in
  `wine/container-build.sh`), and `winedevice.exe` (which hosts mountmgr) runs in the session (checked 2026-11-27).
- **The EeePC has no UDisks2** (checked 2026-11-27): `udisks2` is not installed (Debian 13 has 2.10.1), `polkitd` 126 is
  installed and running. The X11 session from lightdm is on `seat0` and active, so polkit's "active local session" defaults apply
  to programs started from it. The prefix's drives are only `C:`, `H:` (home) and `Z:` (`/`); there is no optical drive, so
  `D:` is free. The loop driver is a module; `/dev/loop-control` is `root:disk 0660`, and `cr` is not in `disk` (it is in
  `cdrom` and `plugdev`).
- **Wine already unmounts before ejecting.** `FSCTL_DISMOUNT_VOLUME` (which programs send before
  `IOCTL_STORAGE_EJECT_MEDIA`) runs `umount MOUNTPOINT` and then, for any loop device, `LOOP_CLR_FD` (`unmount_device` in
  `dlls/ntdll/unix/file.c`). Three things are wrong with it for us:
  - As a normal user, `umount` of a mount UDisks2 made fails (it is not in `fstab`).
  - The exit status is ignored (`__wine_unix_spawnvp`'s result is dropped), so the dismount reports success even then.
  - `LOOP_CLR_FD` runs whatever the device holds and whether or not the unmount worked. On a busy device the kernel only
    marks it to detach when it is last closed.

  The mount point is found in `/etc/mtab` (`get_device_mount_point`), whose entries also carry the file system type
  (`mnt_type`), so the loop handling can be limited to `iso9660` and `udf`. `IOCTL_STORAGE_EJECT_MEDIA` then goes to
  `cdrom_DeviceIoControl` (`dlls/ntdll/unix/cdrom.c`), which sends `CDROMEJECT`: a loop device does not support it. So
  **mounting needs no Wine change, ejecting does.**
- **`linux-launch.exe`** ([session/linux-launch.c](../session/linux-launch.c)) already starts Unix programs from XP, converts paths
  with `wine_get_unix_file_name`, cleans the environment and shows XP-style message boxes (`--terminal`). It is in `C:\xp\bin`,
  and [prefix/extras.py](../prefix/extras.py) (`VERSION` 13) writes the registry for the helpers there.

## Decisions

- **UDisks2 does the mounting** (`udisksctl loop-setup` and `udisksctl mount`), not `mount -o loop` directly: plain `mount` of a
  loop device needs root, and the alternatives (a `sudo` rule, `fstab` entries, a setuid helper) are worse than a daemon that
  Debian ships with a polkit policy for exactly this. It is still the kernel's `mount` underneath (`/media/cr/LABEL`, read-only
  iso9660), visible to Linux programs. And because mountmgr already follows UDisks2, the drive letter, its label, its removal
  and the `WM_DEVICECHANGE` broadcasts come from Wine's own code.
- **The image is attached read-only** (`loop-setup -r`), so a mistake can never write into an `.iso`.
- **The drive letter is the first free one from `D:`**, as Wine chooses it. No fixed letter.
- **Opening the drive after mounting uses the drive's default verb** (`ShellExecute` with no verb), so a disc with an
  `autorun.inf` runs its AutoRun, as XP does when a CD is inserted, and other discs open in an Explorer window. If phase 1
  shows that XP's default verb never runs AutoRun, open an Explorer window and leave AutoRun to phase 3.
- **An image that is already mounted is not mounted twice.** The helper first looks for a loop device backed by the same file
  (`/sys/block/loop*/loop/backing_file`, readable as a user) and, if it is mounted, opens its drive.
- **Eject removes the drive, not only the disc, for mounted images only.** A real XP CD drive stays as an empty drive; a
  virtual drive on a machine with no CD drive is clearer if it goes away. Eject = unmount + delete the loop device, and the
  letter disappears with it. The device is deleted only when it is a loop device with a mounted `iso9660` or `udf` file system.
  Other loop devices, and real drives and USB sticks, are never deleted: they keep Wine's behaviour, apart from the unmount
  fix.
- **Images stay mounted across a logout**, like a CD left in a drive: mountmgr finds them again at the next logon. A reboot
  clears them.
- **Eject is XP's own menu item**, not a second one of ours: Wine is changed so that ejecting a mounted image asks UDisks2 to
  unmount and delete it (phase 2). XP's Eject cannot be hidden, so a handler of our own would show next to it.
- **Only `.iso`.** `.img`, `.nrg`, `.bin/.cue` are out of scope (`.img` is often a hard disk image, which mountmgr would make a
  fixed drive, not a CD).

## Phase 0: probes on the EeePC (after asking; installing `udisks2` goes in `tools/eeepc/LOG.md`)

1. `sudo apt install udisks2`. Note what else it pulls in, and `udisksd`'s memory (RSS) and idle wakeups with the existing
   tools in [tools/eeepc/](../tools/eeepc/README.md): it stays running once D-Bus has started it.
2. In an xterm in the session (Open in Linux Terminal, so in the `seat0` session, not over ssh):
   `udisksctl loop-setup --no-user-interaction -r -f ~/Images/en_office_xp_professional_cd_x10-29102.iso`, then
   `udisksctl mount --no-user-interaction -b /dev/loopN`. Both must work without a password prompt. Note the mount point and
   the time each takes on the Atom.
3. Check what XP does: does `D:` appear in an open My Computer window by itself? With which label and icon? Does the desktop
   or Explorer react to the arrival on its own (AutoRun)? `WINEDEBUG=+mountmgr` in the session log shows mountmgr's side.
4. **Can `cr` open the loop device?** `ls -l /dev/loopN`. XP's Eject has to open `\\.\D:` (that is `dosdevices/d::`, the
   device) before it can send anything. If it is `root:disk 0660`, opening fails and Eject reports an error before Wine sees
   any request. Then phase 2 needs a udev rule (below).
5. **What XP's Eject sends**: choose Eject on `D:` with `WINEDEBUG=+cdrom,+file` (scoped to `explorer.exe` through the
   session's debug setting) and note the sequence of `FSCTL_LOCK_VOLUME`, `FSCTL_DISMOUNT_VOLUME`, `IOCTL_STORAGE_MEDIA_REMOVAL`
   and `IOCTL_STORAGE_EJECT_MEDIA` that arrives, and which of them fails. Phase 2 hooks into the one that arrives.
6. Undo: `udisksctl unmount -b /dev/loopN && udisksctl loop-delete -b /dev/loopN`. Check that the letter goes away in XP.

## Phase 1: mounting by double-click

1. **`linux-launch.exe --mount-iso PATH`** (`session/linux-launch.c`), with the path taken from the command line by hand, as
   `--terminal` does:
   - `wine_get_unix_file_name`; it must be a regular file. Otherwise an XP-style message box: "*File* is not on the Linux file
     system, so it cannot be mounted."
   - Look for a loop device already backed by this file (see Decisions). If there is none:
     `Z:\usr\bin\udisksctl loop-setup --no-user-interaction -r -f UNIXPATH`, run with `CreateProcess` and a pipe for its output,
     and read `/dev/loopN` from "Mapped file ... as /dev/loopN.". Then
     `udisksctl mount --no-user-interaction -b /dev/loopN`. The Unix path goes to `udisksctl` as one argument through
     `/usr/bin/env` as `--terminal` does it, never through a shell.
   - If either fails (no UDisks2, polkit refuses, not an ISO 9660 image): delete the loop device again if it was made, and a
     message box with `udisksctl`'s error text, e.g. "*File* could not be mounted: ...".
   - **Wait for the letter**: up to 5 seconds, ask mountmgr (`IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE` for `D:` to `Z:`, as winecfg
     does) which letter has the device `/dev/loopN`. Then `ShellExecuteW(NULL, NULL, L"D:\\", ...)`. If no letter shows up,
     open the mount point (`/media/cr/LABEL` through `Z:`) in Explorer and log why.
2. **Install** (`prefix/extras.py`, `VERSION` 14):
   - `HKCR\.iso` = `win4wine.isoimage` (only if `.iso` has no class yet, so an installed burning program keeps its own), and
     `HKCR\win4wine.isoimage` = "Disc Image File", with a CD icon from XP's `shell32.dll` (pick the one XP uses for a CD drive
     when testing) as `DefaultIcon`.
   - Verb `mount` = "&Mount", the default (bold, double-click), with the command
     `"C:\xp\bin\linux-launch.exe" --mount-iso "%1"`.
3. **Test on the EeePC** (mounting cannot be tried in the Docker session: it has no system bus, UDisks2 or loop devices; the
   association and the message boxes can be). Double-click each image in `~/Images` (XP SP3, Whistler 2296, Office XP, Age of
   Empires II): the right label, a CD icon, AutoRun where the disc has one. Double-click the same image again: no second drive.
   A file that is not an ISO (rename a `.txt`): message box, no loop device left (`losetup -a` is empty). Time from the
   double-click to the drive on the Atom goes into `tools/eeepc/README.md`.

## Phase 2: XP's Eject unmounts (Wine patch 0161)

1. **The patch** (`ntdll`), in two parts:
   - **Unmounting (every device), in `unmount_device`:** keep `umount MOUNTPOINT`. If it fails, try
     `udisksctl unmount --no-user-interaction -b DEVICE`. If both fail (files still open on the drive), return
     `STATUS_DEVICE_BUSY` instead of success, so XP says the drive is in use, as for a real CD. This also helps a USB stick that
     UDisks2 mounted, and changes nothing where `umount` already works.
   - **Deleting the device (mounted images only):** only when the device is a loop device (`major == LOOP_MAJOR`) and its
     `mtab` entry has `mnt_type` `iso9660` or `udf`. That is read before unmounting, because afterwards there is no entry.
     After a successful unmount, run `udisksctl loop-delete --no-user-interaction -b /dev/loopN`. Our own handle to the device
     does not stop it going away: the kernel detaches it when the last handle closes. Wine's unconditional `LOOP_CLR_FD` is
     limited to the same case, and only after a successful unmount. It is the fallback if `loop-delete` fails, because UDisks2
     notices a detach either way. Other loop devices are left alone.
   - Phase 0 step 5 shows whether XP's Eject sends `FSCTL_DISMOUNT_VOLUME` at all. If it sends only
     `IOCTL_STORAGE_EJECT_MEDIA`, the same steps also run there, before `CDROMEJECT`, for a loop device with a mounted
     `iso9660`/`udf`, and `CDROMEJECT` is skipped for it. A loop device that is no longer mounted (dismounted earlier) is
     still recognised by its backing file (`/sys/block/loopN/loop/backing_file`) and deleted.
   - Opt-in: none needed. The deleting only touches mounted images, and the unmounting only adds a fallback where Wine failed
     silently. Document it in [docs/patches.md](../docs/patches.md) with the table row.
2. **If phase 0 step 4 showed that `cr` cannot open the loop device**: a udev rule, `/etc/udev/rules.d/70-win4wine-iso.rules`,
   `SUBSYSTEM=="block", KERNEL=="loop*", ENV{ID_FS_TYPE}=="iso9660|udf", GROUP="cdrom", MODE="0660"`: only loop devices that
   hold a disc image, and only for the `cdrom` group, as a real CD drive is. It goes into `INSTALL.md`'s setup script and,
   on the EeePC, into `tools/eeepc/LOG.md`.
3. **Test**: Eject on `D:` in My Computer, in the Folders tree, and with a window still open on `D:\` (it must say the drive
   is in use, and work after closing it). `losetup -a` is empty afterwards and `/media/cr/LABEL` is gone. A loop device that
   is not an image (`udisksctl loop-setup` on an ext4 file, mounted) must not be deleted by a dismount from XP. Unmounting
   from Linux (`udisksctl unmount` in a terminal) also removes the drive in XP (that is mountmgr, no patch).

## Phase 3: optional, for the demo

- **AutoRun on arrival**, if phase 1 shows that XP only runs it when the helper opens the drive and not on its own: nothing to
  do as long as mounting always goes through the helper.
- **"Mount" from Linux too**: `xdg-open` on an `.iso` in a Linux file manager could use the same `udisksctl` steps, and the
  drive appears in XP all the same, because mountmgr follows UDisks2 whoever mounts.
- Microsoft's own "Virtual CD-ROM Control Panel" for XP (an unsupported tool of the time) is the reference for how it looked
  then, if a mount dialog is ever wanted. Not planned.

## Risks

- **Every D-Bus property change wakes winedevice.** mountmgr's match rule takes `PropertiesChanged` from any sender on the
  system bus, and each one makes it fetch UDisks2's whole object tree (`GetManagedObjects`). Without UDisks2 that fails at
  once; with it, every NetworkManager or UPower change (battery level, Wi-Fi signal) costs a round trip and a parse in
  `winedevice.exe` on the Atom. Measure it in phase 0 step 1 next to `udisksd`. If it shows, a small patch adds
  `sender='org.freedesktop.UDisks2'` to the match rule.
- **Other removable devices get letters too.** With UDisks2 running, mountmgr also adds USB sticks and SD cards as removable
  drives, even before they are mounted (a drive that cannot be opened). That is the start of the "Handling mass storage media"
  idea in [IDEAS.md](../IDEAS.md), but it must be checked in phase 0 that nothing unexpected appears in My Computer (the
  EeePC's card reader with no card).
- **Hybrid ISOs** (Linux live images) have a partition table: UDisks2 makes `loopNp1` and so on, and mountmgr may make more
  than one drive. The XP-era images in `~/Images` are plain ISO 9660; a hybrid image is tested once in phase 1, and if it
  misbehaves, `loop-setup --no-partition-scan` is the fix.
- **polkit over ssh**: `udisksctl` from an ssh shell is not in the active `seat0` session and may ask for a password, so the
  probes run in the session's own terminal.
- **A disc whose AutoRun is a 16-bit program** (some old setup screens) may not run under this Wine; that is the disc, not
  this feature. Phase 1 notes which of the images in `~/Images` start their AutoRun.

## Docs to update

- Phase 1: [docs/architecture.md](../docs/architecture.md) (`linux-launch.exe --mount-iso`, the `.iso` class that `extras.py`
  writes, and that drive letters come from mountmgr's UDisks2 support), [INSTALL.md](../INSTALL.md) (`udisks2` in the
  requirements), and the IDEAS.md entry.
- Phase 2: [docs/patches.md](../docs/patches.md) (0161 and its row in the table), and the udev rule in INSTALL.md if it is
  needed.
