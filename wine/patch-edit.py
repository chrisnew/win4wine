#!/usr/bin/env python3
"""Change Wine in this checkout's patched source and turn the change into a patch.

The source (.local/wine-upstream, or WINE_SOURCE) holds every patch of the series
as uncommitted changes. Edit it there, compile only what changed, and write the
change into wine/patches/ (never ship the edit itself):

  wine/patch-edit.py mark              note the source as it is now: the base of a change
  wine/patch-edit.py diff              the change since mark
  wine/patch-edit.py make TARGET...    compile and link in the Debian 13 build container,
                                       e.g. dlls/gdi32/all server/all (Wine's make targets)
  wine/patch-edit.py fold NNNN         put the change into patch NNNN (its header is kept,
                                       the diffstat redone)
  wine/patch-edit.py new FILE.patch [--after NNNN]
                                       write the change as a new patch with a header to fill
                                       in, and add it to the series (at the end, or after NNNN)
  wine/patch-edit.py sync              check that the series gives the files changed since
                                       mark as they are, and record that, so that wine/build.sh and
                                       wine/container-build.sh do not start from upstream
                                       again (a few minutes instead of a quarter of an hour)

Files the change adds must be known to git first: git -C .local/wine-upstream add -N FILE.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
source = Path(os.environ.get('WINE_SOURCE', root / '.local/wine-upstream'))
build = Path(os.environ.get('WINE_BUILD', root / '.local/wine-build'))
patches = root / 'wine/patches'
base_file = source / '.git/win4wine-edit-base'
upstream = dict(line.split('=', 1) for line in (root / 'wine/upstream.env').read_text().split('\n')
                if '=' in line and not line.startswith('#'))
commit = upstream['WINE_COMMIT'].strip().strip('"\'')


def git(*args, cwd=source, data=None, env=None, check=True):
    return subprocess.run(['git', '-C', str(cwd), *args], input=data, capture_output=True, text=True,
                          env=env, check=check).stdout


def snapshot():
    """The tree of every file in the source, changed or new (not ignored), as a git tree id."""
    with tempfile.NamedTemporaryFile(dir=source / '.git', prefix='win4wine-index.') as index:
        index.write((source / '.git/index').read_bytes()); index.flush()
        env = dict(os.environ, GIT_INDEX_FILE=index.name)
        git('add', '-A', env=env)
        return git('write-tree', env=env).strip()


def change():
    if not base_file.exists():
        sys.exit('no mark: run "wine/patch-edit.py mark" before editing')
    return git('diff', '--binary', base_file.read_text().strip(), snapshot())


def series():
    return [line.strip() for line in (patches / 'series').read_text().splitlines()
            if line.strip() and not line.lstrip().startswith('#')]


def paths_of(diff):
    return sorted({m for m in re.findall(r'^(?:\+\+\+ b|--- a)/(\S+)', diff, re.M)})


def find(number):
    names = [n for n in series() if n.startswith(number + '-')]
    if len(names) != 1:
        sys.exit(f'no patch {number} in the series')
    return names[0]


def rebuild(before, after_patch, delta):
    """The diff of after_patch with delta added, on top of the patches before it."""
    paths = sorted(set(paths_of(delta)) | set(paths_of(after_patch)))
    with tempfile.TemporaryDirectory(prefix='win4wine-edit-') as temp:
        scratch = Path(temp)
        git('init', '-q', cwd=scratch)
        for path in paths:
            data = subprocess.run(['git', '-C', str(source), 'show', f'{commit}:{path}'], capture_output=True)
            if data.returncode == 0:
                (scratch / path).parent.mkdir(parents=True, exist_ok=True)
                (scratch / path).write_bytes(data.stdout)
        include = [f'--include={p}' for p in paths]

        def apply(text, what):
            if set(paths_of(text)) & set(paths):
                result = subprocess.run(['git', '-C', str(scratch), 'apply', *include, '-'], input=text,
                                        capture_output=True, text=True)
                if result.returncode:
                    sys.exit(f'{what} does not apply here: {result.stderr.strip()}')
        for name in before:
            apply((patches / name).read_text(), name)
        git('add', '-A', cwd=scratch)
        git('-c', 'user.name=x', '-c', 'user.email=x@x', 'commit', '-q', '--allow-empty', '-m', 'base', cwd=scratch)
        apply(after_patch, 'the patch')
        apply(delta, 'the change (a later patch touches the same lines: put it there instead)')
        git('add', '-A', cwd=scratch)
        stat = git('diff', '--cached', '--stat=80', 'HEAD', cwd=scratch)
        diff = git('diff', '--cached', 'HEAD', cwd=scratch)
        return stat, diff


def expected(paths, scratch):
    """The files at paths as the whole series makes them, in scratch."""
    git('init', '-q', cwd=scratch)
    for path in paths:
        data = subprocess.run(['git', '-C', str(source), 'show', f'{commit}:{path}'], capture_output=True)
        if data.returncode == 0:
            (scratch / path).parent.mkdir(parents=True, exist_ok=True)
            (scratch / path).write_bytes(data.stdout)
    for name in series():
        text = (patches / name).read_text()
        if set(paths_of(text)) & set(paths):
            subprocess.run(['git', '-C', str(scratch), 'apply', *[f'--include={p}' for p in paths], '-'],
                           input=text, text=True, capture_output=True, check=True)
    return scratch


def header_of(text):
    lines = text.split('\n')
    for i, line in enumerate(lines):
        if line == '---' and i + 1 < len(lines) and '|' in lines[i + 1]:
            return '\n'.join(lines[:i]) + '\n'
    sys.exit('no "---" before a diffstat in the patch')


command = sys.argv[1] if len(sys.argv) > 1 else '--help'
args = sys.argv[2:]
if command == 'mark':
    base_file.write_text(snapshot() + '\n')
    print('marked; edit the source, then: diff, make, fold or new')
elif command == 'diff':
    sys.stdout.write(change())
elif command == 'make':
    if not args:
        sys.exit('make what? e.g. dlls/gdi32/all')
    user = [] if 'rootless' in subprocess.run(['docker', 'info', '--format', '{{.SecurityOptions}}'],
                                              capture_output=True, text=True).stdout else ['--user', f'{os.getuid()}:{os.getgid()}']
    sys.exit(subprocess.run(['docker', 'run', '--rm', '--init', *user, '-e', 'HOME=/tmp', '-v', f'{root}:{root}', '-w', str(build),
                             'win4wine-wine-build:trixie', 'make', f'-j{os.cpu_count()}', *args]).returncode)
elif command == 'fold':
    name = find(args[0] if args else sys.exit('fold into which patch? NNNN'))
    order = series()
    old = (patches / name).read_text()
    delta = change()
    if not delta:
        sys.exit('no change since mark')
    stat, diff = rebuild(order[:order.index(name)], old, delta)
    (patches / name).write_text(header_of(old) + '---\n' + stat + '\n' + diff)
    print(f'{name}: change folded in; check its header, then "sync"')
elif command == 'new':
    if not args or not args[0].endswith('.patch'):
        sys.exit('new NNNN-dll-what.patch [--after NNNN]')
    name = Path(args[0]).name
    order = series()
    at = order.index(find(args[2])) + 1 if args[1:2] == ['--after'] else len(order)
    delta = change()
    if not delta:
        sys.exit('no change since mark')
    stat, diff = rebuild(order[:at], '', delta)
    dll = name.split('-')[1] if name.count('-') else 'dll'
    (patches / name).write_text(
        'From: Claude <noreply@anthropic.com>\n'
        f'Subject: [PATCH] {dll}: WHAT IT DOES NOW\n\n'
        'Purpose: \nXP behavior: \nWine before: \nChange: \nEvidence: \nScope: \n'
        '---\n' + stat + '\n' + diff)
    order.insert(at, name)
    lines = (patches / 'series').read_text().splitlines()
    anchor = order[at - 1] if at else None
    position = lines.index(anchor) + 1 if anchor else 0
    lines.insert(position, name)
    (patches / 'series').write_text('\n'.join(lines) + '\n')
    print(f'wrote {name} and added it to the series; fill in its header (docs/patches.md, "Format"),\n'
          'add its row to the table in docs/patches.md, then "sync"')
elif command == 'sync':
    if base_file.exists() and (delta := change()):
        # Each file changed since mark must be what the series makes of it.
        with tempfile.TemporaryDirectory(prefix='win4wine-edit-') as temp:
            made = expected(paths_of(delta), Path(temp))
            left = [p for p in paths_of(delta)
                    if (source / p).exists() != (made / p).exists()
                    or (source / p).exists() and (source / p).read_bytes() != (made / p).read_bytes()]
        if left:
            sys.exit('not in the series yet (fold or new first): ' + ', '.join(left))
    subprocess.run([sys.executable, str(root / 'wine/patch-state.py'), str(source), str(patches), '--apply'], check=True)
    # A patch that was missing has just been applied, and the source then differs from what the series gives
    # only if a later patch conflicts: patch-state.py has stopped with an error in that case.
    digest = subprocess.run([sys.executable, str(root / 'wine/patch-state.py'), '--digest', str(patches)],
                            capture_output=True, text=True, check=True).stdout.strip()
    (source / '.git/win4wine-patches').write_text(digest + '\n')
    base_file.unlink(missing_ok=True)
    print('the source matches the series; wine/container-build.sh now compiles only what changed')
else:
    sys.exit(__doc__)
