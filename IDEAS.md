# Ideas

High level ideas to be implemented.

## Whistler 2296 aesthetics

I got the beta ISO laying around, we can play around with that.

## Handling mass storage media

Seamless integration of USB storage etc. Attaching media will show it up in explorer using the Windows API. Removing will do the same.

Planned: [plans/usb-drives.md](plans/usb-drives.md).

## Mounting ISOs natively

Some helper tool that will mount/unmount .iso files and show them as drives.

Done and on the EeePC, except AutoRun: [plans/iso-mount.md](plans/iso-mount.md).

## Notification manager

Linux apps pushing notifications need to be handled somehow.

## Network Neighborhood

Maybe it’s possible to not just show Windows shares, but also do a network scan using avahi to show uPnP devices of any kind.

Done and on the EeePC, except Windows shares: [plans/network-places.md](plans/network-places.md).

## Make Internet Explorer working again

Right now it seems it’s broken.

## Wayland session on par with X11

The Wayland session (labwc) should do what the X11 session does, and no more than it. Known gaps and leftovers are in
[docs/wayland.md](docs/wayland.md) ("Differences from the X11 session"). Its labwc patch still writes `classes.tsv`, which only
the removed Windows 7 taskbar read: drop it with the next change to that patch, or use it for something the X11 WM does too.

## Make Thumb drives work

Right now, nothing happens when you connect a USB stick etc.

Planned, with Safely Remove Hardware, Found New Hardware and AutoPlay (also for disc images): [plans/usb-drives.md](plans/usb-drives.md).
