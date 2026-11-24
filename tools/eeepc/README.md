# EeePC tools

Helpers for testing and measuring win4wine on the target machine, an Asus EeePC 1015PEM. Most of them run **on the EeePC**,
inside its X session (`DISPLAY=:0 XAUTHORITY=~/.Xauthority`). Copy them over with `scp`. Several expect to sit in `/tmp` under a
`w4w-` name: `drive.py` calls `/tmp/w4w-shot.py`, and `wmbench.sh` calls `/tmp/w4w-wmbench.py`.

## The machine

Atom N550 (2 cores, 4 threads), Intel GMA 3150 (the intel DDX with SNA, gen3, so accelerated), 2 GB RAM, 1024x600, Debian 13,
schedutil governor. English XP with Luna, in the X11 session. It is far slower and smaller than what upstream tested (VMs at 1280x800),
so expect bugs from timing (e.g. the supervisor's `readiness_timeout` of 30 s), from the 600 px screen height and from the old GPU.

## Driving the session and looking at it

| Tool | What it does |
|---|---|
| `drive.py ACTION...` | Drive the X display with XTest: `move X Y`, `click X Y`, `rclick X Y`, `dblclick X Y`, `key KEYSYM`, `sleep S`, `shot NAME` |
| `shot.py OUT.ppm` | A screenshot that stacks each viewable top-level window's own image (`xwd -root` misses redirected windows) |
| `capture.py` | Capture a screen region frame by frame after a trigger, stacking each window's own image |
| `mapcap.py` | Capture a window's own image as fast as possible from the moment a window of class CLASS maps |
| `monitor.py` | Log the taskbar's X window state, the screen size and the session status while something happens |
| `stackwatch.py` | Watch the X stacking order |
| `timeline.py IPC COMMAND...` | Timeline of a Linux window getting its XP frame |

## Measuring CPU

| Tool | What it does |
|---|---|
| `dragbench.py` | Drag a native client's XP frame by its caption with XTest, at 60 Hz, and report each process's CPU and the client's lag |
| `wmbench.sh WM_PY [STEPS] [HZ]` + `wmbench.py` | Profile a WM on Xvfb while a fake frame is dragged (`PROFILE=1` for cProfile) |
| `traybench.sh BRIDGE_DIR` | tray.py's CPU while `geometry.tsv` is renamed into its IPC directory 600 times at 60 Hz |
| `miniwm.c` | `wm.py`'s drag path in C with libxcb, to measure what a compiled WM would cost per step |
| `servertrace.py` | Summarise a wineserver `+server` trace: requests per thread in the last N lines, with each thread's process |
| `slice.py` | Requests per second of one process's threads in a slice of a `+server` trace |

For a `+server` trace, add `+server` to `"debug"` in `session.json`. The wineserver traces every process.

## Measured so far

On 2026-11-23, a 60 Hz drag of an xterm by its XP caption cost about 40% of a core in `wm.py`, 23% in wineserver, 22% in the proxy,
16% in Xorg, 12% in tray.py and 9% in supervisor.py. The window kept up (2 px lag), and single events took about 13 ms
(`tests/wm_latency.py`).

- **Fixed since then:** the supervisor watches only the taskbar window, and tray.py wakes only for renames (14% → 5% in `traybench.sh`).
  xptray keeps its sysfs/procfs files open and lists the directories every 10 s, which took it from 95 to 35 server requests a second.
- **Rejected:** skipping the WM's synthetic ConfigureNotify. ICCCM (and `tests/wm_integration.py`) require it for native clients,
  and skipping it for Wine windows only gained nothing measurable.
- **Rewriting wm.py in C:** `miniwm.c` does the same drag-step work in 0.68 ms against 5.2–5.4 ms for `wm.py` (2026-11-24),
  about 8 times less. That would take drags from roughly 38% to 5% of a core. Most of `wm.py`'s cost is python-xlib;
  `write_geometry` runs on every step and also wakes the proxy.
- **Idle wakeups still open** (see [BUGS.md](../../BUGS.md)): xptray's volume check starts a process every 2 s. The taskbar's
  notification area repaints whenever xptray's network icon lights up for traffic, and each repaint makes 27
  `GetProp("UxSubclassInfo")` server round trips (comctl32 subclassing).
