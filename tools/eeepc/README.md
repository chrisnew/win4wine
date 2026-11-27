# EeePC tools

Helpers for testing and measuring win4wine on the target machine, an Asus EeePC 1015PEM. Most of them run **on the EeePC**,
inside its X session (`DISPLAY=:0 XAUTHORITY=~/.Xauthority`). Copy them over with `scp`. Several expect to sit in `/tmp` under a
`w4w-` name: `drive.py` calls `/tmp/w4w-shot.py`, and `wmbench.sh` calls `/tmp/w4w-wmbench.py`.

## The machine

Atom N550 (2 cores, 4 threads), Intel GMA 3150 (the intel DDX with SNA, gen3, so accelerated), 2 GB RAM, 1024x600, Debian 13,
schedutil governor. English XP with Luna, in the X11 session. It is far slower and smaller than what upstream tested (VMs at 1280x800),
so expect bugs from timing (e.g. the supervisor's `readiness_timeout` of 30 s), from the 600 px screen height and from the old GPU.

What was changed on the machine itself (the backports kernel with ntsync, the patched X driver, `/etc`, the prefix) and why is
in [LOG.md](LOG.md). Keep it up to date with every change made there.

## OpenGL on the GMA 3150

Mesa 25 has no DRI2 client any more, and the intel DDX (SNA) enables DRI3 only from gen4 on, whatever `Option "DRI"` says. So
every OpenGL program on X used llvmpipe (AoE 2: four llvmpipe threads at about 190% of the four logical CPUs), although Mesa's i915
driver works on the device (`i915 (chipset: Pineview M)`, OpenGL 2.1, through GBM). `xorg-intel/build.sh` builds Debian's package
with `sna-gen3-dri3.patch`, which lets gen3 have DRI3 when the configuration asks for it, into `.local/xorg-intel/`. On the EeePC
it goes with `/etc/X11/xorg.conf.d/20-intel.conf`:

```
Section "Device"
    Identifier "Intel"
    Driver     "intel"
    Option     "DRI" "3"
EndSection
```

Measured 2026-11-26, AoE 2 in a game, 10 s with `top`: with llvmpipe the game took 295% (llvmpipe threads 192%, main thread 75%,
wined3d 13%), wineserver 7.5%, Xorg 13%. With the patched DDX (and Wine with ntsync, kernel 7.3.4): the game 124% (main thread 68%,
wined3d 45%), wineserver 17%, Xorg 21%, PulseAudio 15%; the CPUs 48% idle against 17%.

**Half-Life Alpha (2026-11-26): the cost is Wine's WoW64 crossing, not the GPU.** The game runs on the GPU (it holds
`renderD128`, no llvmpipe threads), but its render thread uses a whole logical CPU: 69% user, 32% kernel. `perf record -g` over
15 s of play: of the thread's samples, about 59% go to getting each call from the 32-bit game into 64-bit Unix code (the
kernel's `arch_prctl` path, `__wine_unix_call_dispatcher`, `wow64cpu.dll`, both halves of opengl32) and about 37% to the real
work (Mesa's i915 and its compiled vertex code, libGL, the i915 kernel driver at 1–2%). The game draws in immediate mode:
`glBegin`, then `glTexCoord2f`/`glVertex3fv`/`glColor4f` for each vertex, then `glEnd`, and every one of those calls is a
separate unix call.

Each unix call of a WoW64 thread must set FS back to glibc's thread pointer (`signal_x86_64.c`, `__wine_unix_call_dispatcher`).
With `wrfsbase` that is one instruction, but the Atom N550 has no FSGSBASE, so Wine makes an `arch_prctl(ARCH_SET_FS)` system
call each time. `perf record -e syscalls:sys_enter_arch_prctl -a` for 5 s: 181,000 a second from the game, a few hundred from
everything else (xp-explorer about 360 a second), so the shell does not suffer from it. The CPU reports "Not affected" for every
vulnerability, so no mitigations add to the cost. Wine 0157 does that: opengl32 collects the vertex and attribute calls and sends
them in one unix call before the next other call.

**With Wine 0157 (2026-11-26).** Half-Life Alpha started from ssh in the session, once with `WINE_OPENGL_BATCH=0` and once
without, played for a minute each:

| | batching off | batching on |
|---|---|---|
| render thread | 70% user, 31% kernel | 79% user, 21% kernel |
| `arch_prctl` calls (`perf stat`, 5 s) | 361,000 a second | 10,000 a second |
| Mesa, its compiled vertex code, libGL, i915 (`perf record -t`, 10 s) | about 37% of the thread | about 60% |
| Wine's own share | about 59% (unix call path) | about 18% (`opengl32.dll` recording 10%, `opengl32.so` replay and thunks 8%) |

The game renders as fast as it can, so the thread stays at a whole CPU and the gain goes into frames: about 1.6 times as many
if the work per frame is unchanged (no frame counter was read). It felt clearly smoother. `glbench.exe` (2000 textured quads,
26,001 GL calls a frame, Mesa-bound by its texture switch per quad) draws the same image either way (checksum `32910d11`) in
100–107 ms against 117–125 ms. One crash in the game's own code (a write to address 0 at 0x414E54, about 20 s after start) came
in the first run with batching; it did not come again in the batching run above, and the game had never crashed before.
What is left: `set_context_attribute` and `replay_batch` 2.3% each, and `read_hpet` 1.5% (the clock source is the HPET).

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
- **Fixed 2026-11-26:** xptray reads the volume from a helper on `pactl subscribe` instead of starting processes every 2 s
  (52 → 19 wakeups a second), and Wine 0155 stops each Unix program start from leaking two files (xptray held 6224 fds after two days).
- **Live, 2026-11-26, with the C WM (`wm.c`):** a 60 Hz drag by the XP caption costs proxy 13-16%, wineserver 13%, Xorg 8.5%,
  xp-explorer 4.5%, WM 3.7% (wm.py: 40%), still 1-2 px behind. A Linux window that moves itself (GTK's header bar, Alt+drag;
  `w4w-selfmove`: the client configures itself 480 times) cost proxy 53%, wineserver 44%, Xorg 13%, xp-explorer 12%, WM 8%: three or
  four whole proxy passes a step. Since 7d3fe2a the proxy runs only `frame_geometry` for `geometry.tsv` and moved frames:
  proxy 27-31%, wineserver 22%. Writing any file 60 times a second into the IPC directory alone cost proxy 53% and wineserver 47%.
- **Idle wakeups still open** (see [BUGS.md](../../BUGS.md)): the taskbar's
  notification area repaints whenever xptray's network icon lights up for traffic, and each repaint makes 27
  `GetProp("UxSubclassInfo")` server round trips (comctl32 subclassing).

## Screen savers and the lock screen

Measured 2026-11-27 (repo 6b3f9bc): CPU and context switches ("wakeups") from `/proc` over the user's processes and Xorg, in
percent of one of the four logical CPUs (the sampling script is in the [plan](../../plans/screensaver-and-lock.md)).

**The idle session**, 60 s without input:

| | CPU | wakeups/s |
|---|---|---|
| before the deployment (da48e71) | 3.9% | 140 |
| after, no screen saver chosen | 4.0% | 142 |
| after, a screen saver set to 10 minutes, right after unlocking | 5.3% | 168 |
| the same, a few minutes later (an `htop` and its SSH session taken out) | 3.2% | 118 |

`idle-watch`, `logind-watch` and the supervisor were at 0 in all of them. The higher sample was mostly xp-explorer (3.2%).

**Locking:** from the request until the lock window is mapped, 2.21, 2.26 and 2.21 s (the request file), 2.28 s
(`loginctl lock-session`), and 2.29 s until a killed lock screen was back. With the password field's caret blinking, `xp-lock.py`
takes 2.0% and 2.5 wakeups a second, with 65 MB resident (the whole machine 7.0%, 160).

**Screen savers**, started with `/s`, a 10 s sample from 15 s after the start. The totals include the shell underneath. All covered
the screen and the taskbar. Frame rates were not measured.

| Screen saver | all | the saver | Xorg | saver's RSS |
|---|---|---|---|---|
| Windows XP (`logon`, shows only black) | 5.5% | 0% | 0.1% | 27 MB |
| Blank | 6.6% | under 0.5% | 0.2% | |
| Starfield | 17% | 8% | 5% | 26 MB |
| Marquee | 20% | 8% | 5% | 28 MB |
| My Pictures (no pictures, only the message) | 8% | under 0.5% | 1% | |
| Beziers | 49% | 23% | 15% | 26 MB |
| Mystify | 66% | 34% | 20% | 26 MB |
| 3D Pipes (OpenGL) | 33% | 16% | 8% | 165 MB |
| 3D Text (OpenGL, `htop` running) | 64% | 48% | 8% | 167 MB |
| 3D Flying Objects (OpenGL) | 79% | 64% | 9% | 170 MB |
| 3D FlowerBox (OpenGL, `htop` running) | 147% | 129% | 6% | 169 MB |

Mouse movement ended each one. 3D Pipes did not end on three XTEST jumps 0.3 s apart, but did on real movement; the others
measured after it ended within 0.4 s of continuous XTEST movement.

**With a lock screen waiting** (fc82c2e, `xp-lock.py --standby`, the same day): 0.57 and 0.57 s with the request file, 0.61 s
with `loginctl lock-session`, and it feels immediate at the machine. The waiting process takes 54-56 MB, no CPU and no wakeups
(the idle session after the relogin: 4.8%, 159, mostly xp-explorer at 2.8%). A profile of the start before, off screen: Python
0.18 s, `import welcome` (cairo, Pango) 0.49, other imports and the X connections 0.5, the first drawing of the screen 0.78, a
second one 0.13, the first with the password box 0.44, the upload 0.06.

## Linux columns

Measured 2026-11-27 (repo d2619cd) with no session running: a 32-bit program calling `linuxcols.dll` for each of the 1248
entries of `Z:\usr\bin`, with the file system's caches warm (not measured cold).

| | time | per entry |
|---|---|---|
| all four columns, entry by entry (as the view asks) | 134-141 ms | 0.11 ms |
| one column over all entries (as a sort asks) | 102-106 ms | 0.08 ms |
| Wine listing the folder (`FindFirstFile`), first and second time | 903 and 52 ms | |

**In the folder window**, the same day, maximized, Details view: `Z:\usr\bin` opened from `Z:\usr` by a double click, three
runs each, the list captured frame by frame (`capture.py`, 7 frames a second, so ±0.15 s; the capturing loads Xorg too). CPU
time is over the 8.8 s of a run.

| | without the columns | with the four |
|---|---|---|
| rows drawn after | 2.0, 2.1, 2.0 s | 2.1, 2.2, 2.4 s |
| xp-explorer | 1.9-2.0 s | 1.9-2.1 s |
| wineserver | 0.63-0.83 s | 0.74-0.95 s |

The Linux values were there in the same frame as the rows. Sorting by Linux Permissions was drawn 0.66 s after the click (one
run). Idle for 30 s with the four columns on screen: xp-explorer 2.3% of one logical CPU, wineserver 0.8%; with a folder window
without them (`Z:\usr`): 3.3% and 1.8%. `Z:\run\user\1000`, with its sockets, showed them and nothing hung.

