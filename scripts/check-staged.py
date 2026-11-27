#!/usr/bin/env python3
"""Reject executable/font/media/hive material from the Git index.

The only binaries allowed are PNG images under docs/images/ (the README screenshot) and
our own icons, .ico files under resources/ (drawn after "XP-Style Icon Guide.md")."""
from pathlib import Path
import subprocess
import sys

paths = subprocess.check_output(['git', 'diff', '--cached', '--name-only', '-z', '--diff-filter=ACMR']).split(b'\0')
for raw in paths:
    if not raw:
        continue
    name = raw.decode()
    content = subprocess.check_output(['git', 'show', ':' + name])
    image = (Path(name).parent == Path('docs/images') and content[:8] == b'\x89PNG\r\n\x1a\n' or
             Path(name).parent == Path('resources') and Path(name).suffix == '.ico' and content[:4] == b'\0\0\1\0')
    if (Path(name).parts[0] in ('localres', '.local', 'runtime', 'build', 'vendor') or
            Path(name).suffix.lower() in ('.exe', '.dll', '.msstyles', '.ttf', '.ttc', '.fon', '.iso', '.cab', '.hiv') or
            content[:2] == b'MZ' or content[:4] == b'regf' or (b'\0' in content and not image)):
        print(f'Refusing local or binary artifact in index: {name}', file=sys.stderr)
        sys.exit(1)
