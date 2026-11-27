#!/usr/bin/env python3
"""Make one edit both in a file and in its staged copy, leaving the file's other uncommitted
changes unstaged. For files the user keeps notes in (IDEAS.md, tools/eeepc/LOG.md): your
line goes into the commit, theirs stay in the working tree.

  tools/stage-edit.py FILE OLD NEW     replace OLD, which must occur once in each copy
  tools/stage-edit.py FILE '' NEW      append NEW to both copies

OLD and NEW are text, or @PATH to read the text from a file (for backslashes and long entries).
"""
from pathlib import Path
import subprocess
import sys


def text(arg):
    return Path(arg[1:]).read_text() if arg.startswith('@') else arg


def edit(content, old, new, what):
    if not old:
        return content + new
    if content.count(old) != 1:
        sys.exit(f'{what}: the old text occurs {content.count(old)} times, not once')
    return content.replace(old, new)


if len(sys.argv) != 4:
    sys.exit(__doc__)
path, old, new = sys.argv[1], text(sys.argv[2]), text(sys.argv[3])
staged = subprocess.run(['git', 'show', f':{path}'], capture_output=True, text=True)
index = edit(staged.stdout if staged.returncode == 0 else '', old, new, 'staged copy')
work = edit(Path(path).read_text(), old, new, 'working copy')
blob = subprocess.run(['git', 'hash-object', '-w', '--stdin', '--path', path], input=index,
                      capture_output=True, text=True, check=True).stdout.strip()
subprocess.run(['git', 'update-index', '--add', '--cacheinfo', f'100644,{blob},{path}'], check=True)
Path(path).write_text(work)
print(f'{path}: staged with this edit only; the other changes stay unstaged')
