"""CPU and wakeups per process, from /proc: the user's processes and Xorg.

  sample.py SECONDS [DELAY]     sample SECONDS, after waiting DELAY
  sample.py -- COMMAND...       sample while COMMAND runs (e.g. drive.py dragging a window)

CPU is in percent of one logical CPU (the EeePC has four), wakeups are context switches a
second over all of a process's threads. Processes under 0.5% and 2 wakeups/s are left out.
"""
import os, subprocess, sys, time

HZ, PAGE = os.sysconf('SC_CLK_TCK'), os.sysconf('SC_PAGE_SIZE')


def snapshot():
    found = {}
    for pid in filter(str.isdigit, os.listdir('/proc')):
        if int(pid) == os.getpid():
            continue
        base = f'/proc/{pid}'
        try:
            command = open(base + '/cmdline', 'rb').read().split(b'\0')[0].decode(errors='replace')
            if os.stat(base).st_uid != os.getuid() and 'Xorg' not in command:
                continue
            # Wine programs: the .exe's name, not the full Windows path
            name = command.replace('\\', '/').rsplit('/', 1)[-1] or open(base + '/comm').read().strip()
            args = open(base + '/cmdline', 'rb').read().split(b'\0')[1:2]
            if name.startswith('python') and args:
                name += ' ' + args[0].decode(errors='replace').rsplit('/', 1)[-1]
            ticks = switches = 0
            for task in os.listdir(base + '/task'):
                stat = open(f'{base}/task/{task}/stat').read().rsplit(')', 1)[1].split()
                ticks += int(stat[11]) + int(stat[12])
                with open(f'{base}/task/{task}/status') as status:
                    switches += sum(int(line.split()[1]) for line in status if 'ctxt_switches' in line)
            rss = int(open(base + '/statm').read().split()[1]) * PAGE // 1048576
            found[pid] = (ticks, switches, rss, name)
        except (OSError, IndexError, ValueError):
            pass
    return found


args = sys.argv[1:]
if not args or args[0] in ('-h', '--help'):
    sys.exit(__doc__)
command = args[1:] if args[0] == '--' else None
if not command:
    time.sleep(float(args[1]) if len(args) > 1 else 0)
before, start = snapshot(), time.monotonic()
if command:
    subprocess.run(command)
else:
    time.sleep(float(args[0]))
seconds, after = time.monotonic() - start, snapshot()
rows = [((after[p][0] - before[p][0]) * 100 / HZ / seconds, (after[p][1] - before[p][1]) / seconds,
         after[p][2], p, after[p][3]) for p in after if p in before]
print(f'over {seconds:.1f} s: {sum(r[0] for r in rows):.1f}% of one CPU, {sum(r[1] for r in rows):.0f} wakeups/s')
print('  CPU%  wakes/s  RSS MB     pid  process')
for row in sorted(rows, reverse=True):
    if row[0] >= 0.5 or row[1] >= 2:
        print('{:6.1f} {:8.1f} {:7} {:>7}  {}'.format(*row[:4], row[4][:60]))
