# Documentation

| Document | Contents |
|---|---|
| [architecture.md](architecture.md) | How it works: what each part does, the WM, the proxy and their IPC, XP frames for Linux windows, the tray, the session, the login screen, DPI |
| [design.md](design.md) | The design in brief: which parts were taken from XP, the rules the project keeps and why |
| [patches.md](patches.md) | The list of Wine's 165 patches, the opt-in check, their order and format, how to add one |
| [development.md](development.md) | How to develop: building in the chroot, tests, installing by hand, updating an existing setup |
| [wayland.md](wayland.md) | The Wayland session (in testing): the patched labwc, per-monitor scaling, how it differs from the X11 session |
| [original-features.md](original-features.md) | Features XP does not have (off by default): window snapping (`window_snap`) |

How to use and install it is in the repository's [README](../README.md) and [INSTALL.md](../INSTALL.md).

## Records (history/)

Notes written during development. They are kept as they were written, in Japanese; the documents above describe the current state.

| Document | Contents |
|---|---|
| [history/investigations.md](history/investigations.md) | Investigations and decisions, in the order they were made: causes of bugs, values measured on a real XP, approaches that were rejected |
| [history/patch-history.md](history/patch-history.md) | How patches were added and renumbered, with a table from old numbers to new ones |
| [history/wayland-study.md](history/wayland-study.md) | A comparison of Wayland compositors and the results of the first prototype |

"Stage N" in the records is a unit of development work (stages 1–11, October 2026).
