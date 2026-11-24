"""Requests per second of one process's threads in a slice of a +server trace.
slice.py LOG FROM_LINE TO_LINE SECONDS UNIX_PID"""
import re, sys
from collections import Counter
log, start, end, seconds, pid = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
tids, counts = set(), Counter()
with open(log, errors='replace') as f:
    for n, line in enumerate(f, 1):
        m = re.match(r'(\w{4}): init_(?:first_)?thread\( unix_pid=(\d+)', line)
        if m and int(m.group(2)) == pid: tids.add(m.group(1))
        if start < n <= end:
            m = re.match(r'(\w{4}): (\w+)\(', line)
            if m and m.group(1) in tids and ') =' not in line[:80]: counts[m.group(2)] += 1
print(f'{sum(counts.values()) / seconds:.1f} requests/s (threads {sorted(tids)}):',
      ', '.join(f'{k} {v / seconds:.1f}' for k, v in counts.most_common(6)))
