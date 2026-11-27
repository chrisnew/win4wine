# Plan: network scans in My Network Places

Status (2026-11-27): **built and tested on the development machine** (XP session in Docker with Xvfb at 1024x600, with other
containers announcing over mDNS and UPnP). **Not yet on the EeePC** ([deploying](#deploying-on-the-eeepc)). Windows shares
are not done. How it works now:
[docs/architecture.md](../docs/architecture.md#my-network-places-sessionnetwork_placespy).

Goal: XP's own My Network Places shows what is on the local network: computers, web interfaces, printers and other devices,
found with mDNS/DNS-SD (Avahi) and UPnP (SSDP). A double click does something sensible.

## What the checks showed

On the development machine, with a fresh prefix:

- **The blank item** was HIVESFT.INF's `NetworkNeighborhood\NameSpace\{3c5c43a3-9ce9-4a9b-9699-2ac0cf6cc4bf}`: removing
  the key removed the item. The ID appears in no other extracted file. `prefix.py` now leaves the key out and deletes it
  from existing prefixes.
- Plain `.lnk` files show in My Network Places, and an open window follows files appearing and disappearing, also when Linux
  deletes them.
- XP's icons from `shell32.dll` work (`linux-links.exe` can now set an icon's index).
- A web shortcut through `C:\xp\bin\iexplore.exe` delivers the URL unchanged, `%PATH%` and `$HOME` included.
- A shortcut to `\\HOST` does nothing when double clicked, so bare computers get XP's "is not accessible" message from
  `linux-launch.exe --unreachable`.
- Writing four shortcuts took 0.09 s on the development machine. **Not measured on the Atom.**
- The helper used no CPU time and had no context switch in 120 s of idling (41 MB resident). **Not measured on the Atom.**

## Grouping and columns

The folder has four columns, and Choose Details offers no others: Name, Comments, Computer and Network Location. There is
no Type column, so **the entries cannot be grouped by type**. It groups by Network Location, and everything is "Unspecified".

- A plain shortcut shows "This computer" under Computer and nothing under Comments (its description is not used) or
  Network Location.
- A folder shortcut (a folder with the **system** attribute, `Desktop.ini` and `target.lnk`) fills Network Location ("Local
  Network" for `http:`, `ftp:` and local targets) and shows "Unknown" under Computer for an unreachable UNC target. Comments
  stayed empty, with a description in `target.lnk` and with `InfoTip` in `Desktop.ini`. A folder shortcut opens its target as
  a folder, so it cannot start the browser or a terminal.
- Column handlers such as `linuxcols.dll` add nothing here.

Arranged by Name, entries of one type sit together because their names start alike ("Printer on", "SSH on", "Web page on"),
except bare computers. Real grouping by type needs a namespace extension of our own: a DLL registered under
`NetworkNeighborhood\NameSpace`, with its own columns. It is several times the work of what is built.

## Decisions taken while building

- Windows shares are left out (Wine has no SMB client). A computer that announces `_smb._tcp` shows as a computer.
- Grace period 60 s, at most 30 shortcuts; both and the kinds shown are settings (`network_places` in session.json).
- Only private and link-local IPv4 addresses are listed.
- A printer without a web page also shows "not accessible".
- XP's UPnP folder was not tried.

## Deploying on the EeePC

Logged off, and each step with its entry in [tools/eeepc/LOG.md](../tools/eeepc/LOG.md):

1. `git fetch fork && git checkout -B eeepc fork/eeepc`, then `bridge-wm/build.sh`.
2. `sudo apt-mark manual avahi-daemon`: it came only with `redshift-gtk`.
3. Apply the prefix additions again (`prefix/extras.py`, then `session/linux_apps.py` and `xp-explorer/scripts/pins.py`, as
   `setup.sh` does under `xvfb-run`). This removes the blank item.
4. Log in, then measure the helper when idle and one `linux-links.exe` run.

"My Web Sites on MSN" is the user's own file in `Network Shortcuts`; it can be deleted like any shortcut.

## Open

- What will announce itself at the event? Venue Wi-Fi often isolates clients or blocks multicast, and then the folder is
  empty. A network of our own (a travel router and a second machine publishing a web page or a share) would make the demo
  predictable; best decided early.
- If the venue network does pass multicast, visitors' devices appear under personal names on a screen in public. `show` and
  `limit` can narrow the list; there is no "only our own network" setting.
- Windows shares, if wanted; they belong with "Network Drives" in [IDEAS.md](../IDEAS.md).
- A namespace extension, if grouping by type is worth it.
