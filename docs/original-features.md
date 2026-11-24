# Original features

win4wine's baseline is reproducing XP's look and behaviour as it is, but it adds a few features XP does not have.
All of them are off by default and only work when chosen in `session.json` (`reactos-session.json` for the ReactOS shell).
When they are off, behaviour is the same as XP's.

## Window snapping (`window_snap`)

Like Windows 7's Aero Snap, windows can be snapped to the left or right half of the screen, or maximized.

```json
{
  "window_snap": true
}
```

It takes effect from the next login. It works in the XP shell and the ReactOS shell, in both the X11 and the Wayland session,
and the same way for Wine windows and for Linux apps' windows (the ones with an XP frame).

### Controls

| Action | Effect |
|---|---|
| Win+← / Win+→ | Snaps the foreground window to the left/right half of the work area (the screen minus the taskbar). The opposite direction restores the position and size from before the snap. The same direction again moves it to the near half of the next monitor |
| Win+↑ | Maximizes |
| Win+↓ | Restores a maximized window, returns a snapped window to its pre-snap state, otherwise minimizes |
| Dragging the title bar to the screen's left/right edge | Snaps to that half. While at the edge, the target is shown with the same hatched outline as XP's drag frame |
| Dragging the title bar to the screen's top edge | Maximizes |
| Dragging a snapped window's title bar | Returns to the pre-snap size and keeps moving |
| Dragging a maximized window's title bar | After a small move it returns to its normal size and keeps moving (in XP, maximized windows cannot be moved) |

Only the outer edges of the whole screen count as left and right edges; nothing snaps at the border between monitors.

### How it works

- **Keys**: the proxy (`bridge-wm/proxy.c`) receives Win+arrow with `RegisterHotKey`. When a Linux window has focus, the WM
  (labwc on Wayland) passes the arrow keys to the proxy the same way it passes Win+R and the like.
- **Dragging on X11**: title bar drags are done by Wine's move loop. Wine patch 0038 makes the loop send
  EVENT_SYSTEM_MOVESIZESTART/END (the same notifications as Windows); the proxy receives them, shows the target outline and snaps
  on release. Restoring a snapped window's size when it is pulled away, and restoring a maximized window to its normal size, are
  done inside Wine's loop, because a window being dragged cannot be changed from outside.
- **Wayland**: dragging a normal-sized window is done by labwc, using labwc's own snapping. Only labwc remembers which windows are
  snapped, and the proxy asks labwc to handle Win+arrow. During a drag labwc tells the proxy the target, and the proxy shows it
  with the same hatched outline as on X11.

Details are in "Keyboard" in [architecture.md](architecture.md) and 0038 in [patches.md](patches.md).

### Limits

- A prototype. Moving to the next monitor has only been checked with a single monitor.
- Resizing a snapped window ends its snapped state (as in Windows 7).
