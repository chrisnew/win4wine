# What there is to see

A picture of each thing this fork added, newest first, so that you can see what the XP session on the EeePC can do without
starting it, and what to show at the event. Every feature that changes what is on the screen adds its picture here in the commit
that builds it (how: [`.claude/skills/screenshots`](../.claude/skills/screenshots/SKILL.md)).

Each picture says where it was taken: **EeePC** (the real machine, 1024x600) or **bench** (the session in Docker on Xvfb at
1024x600, [`tools/bench`](../tools/bench/bench.sh); the same Wine and shell, but another user name, no sound, no network devices
and few Linux apps). The pictures are in [images/showcase/](images/showcase/). The XP screens in them are copyrighted by
Microsoft.

## Alt+F4 on the desktop

With the desktop active (clicked, or after the last window was closed), Alt+F4 opens XP's "Turn off computer", as on XP.

<img src="images/showcase/desktop-alt-f4.png" alt="XP's Turn off computer dialog with Stand By, Turn Off and Restart over the faded desktop" width="500" />

*Bench, 2026-11-28 (repo 8034448 and this change).*

## The desktop on the EeePC

The whole desktop on the machine it is made for: an xterm with screenfetch on Debian 13, Excel 2002 from Office XP, XP's folder
window on `/usr/bin` with the Linux details, and Wine's About box, side by side on XP's taskbar. It is the README's main picture.

<img src="images/showcase/desktop-eeepc.png" alt="LunaticOS on the EeePC 1015PEM: screenfetch on Debian 13 in an xterm with an XP frame, Excel 2002, an XP folder window on /usr/bin with Linux permissions, and Wine's About box" width="800" />

*EeePC, 2026-11-27 (repo dc08256).*

## Open in Linux Terminal

The context menu of a folder's background opens a Linux terminal in that folder. The terminal gets an XP frame and a taskbar
button like any Windows program ([plans/linux-terminal.md](../plans/linux-terminal.md)).

<img src="images/showcase/linux-terminal-menu.png" alt="The context menu of /usr/bin's background with Open in Linux Terminal" width="624" />

<img src="images/showcase/linux-terminal.png" alt="An xterm with an XP Luna frame in /usr/bin, over the XP folder window" width="800" />

*Bench, 2026-11-27 (repo f21ec45).*

## Disc images

An `.iso` file has its own icon, and a double click mounts it as a CD drive; Eject takes it away again
([plans/iso-mount.md](../plans/iso-mount.md)). The icon is ours, drawn after the
[icon guide](../XP-Style%20Icon%20Guide.md).

<img src="images/showcase/iso-icon.png" alt="An .iso file named Office XP on the desktop, with a disc icon" width="110" />

*Bench, 2026-11-27 (repo f21ec45): the icon only, because the bench cannot mount (no UDisks2 or loop devices).*

## Linux columns

XP's folder windows show a Linux file's permissions, owner, group and path (the header's context menu adds them)
([plans/linux-columns.md](../plans/linux-columns.md)).

<img src="images/showcase/linux-columns.png" alt="XP Explorer on Z:\usr\bin in Details view with the Linux Permissions, Owner and Group columns" width="800" />

*Bench, 2026-11-27 (repo f21ec45).*

## Not pictured yet

These are built and on the EeePC, and still need their picture, best taken there: My Network Places with the home network's
devices ([plan](../plans/network-places.md)), the screen savers and the lock screen ([plan](../plans/screensaver-and-lock.md)),
Internet Explorer 6, Customize Notifications and the notification area's chevron, and the Start menu's Linux apps.
