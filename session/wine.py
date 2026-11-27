#!/usr/bin/env python3
"""win4wine-wine [WINE ARGUMENTS...]

Run "wine" as the session runs it: in the shell's prefix, with the session's
Wine and its opt-ins (WIN4WINE_EXTERNAL_SHELL and the rest, session/wineenv.py),
so that a program started from outside the session, for example over ssh or
from another desktop, looks and behaves as one started from the shell.
Without them Wine runs the program with its own look (no Luna
frames), and plain "wine" uses ~/.wine and the first Wine in PATH.

The session is the one set up with setup.sh (~/.config/win4wine/session.json).
In a terminal of the session, plain "wine" has all this already; this runs it
unchanged there.
"""
import json
import os
import sys

from wineenv import CONFIGS, SHELLS, config_dir, environment

SHELL = SHELLS['xp']


def fail(message):
    print(f'win4wine-wine: {message}', file=sys.stderr)
    return 2


def main(argv):
    if argv and argv[0] in ('-h', '--help'):
        print(__doc__.strip())
        return 0

    if os.environ.get('WIN4WINE_EXTERNAL_SHELL') is not None and os.environ.get('WINEPREFIX'):
        # Started from the session: its environment is the session's own.
        os.execvp('wine', ['wine', *argv])

    path = config_dir() / CONFIGS['xp']
    if not path.is_file():
        return fail(f'no session set up in {config_dir()} (scripts/setup.sh)')
    config = json.loads(path.read_text())
    if config.get('shell', 'xp') != 'xp':
        return fail(f'{path} is for the {config["shell"]} shell, which LunaticOS no longer has')
    base = {k: v for k, v in os.environ.items() if not k.startswith('WIN4WINE_')}
    try:
        env = environment(base, config, SHELL)
    except OSError as e:
        return fail(e)
    if not os.path.isfile(os.path.join(env['WINEPREFIX'], SHELL['marker'])):
        return fail(f'{env["WINEPREFIX"]} is not the XP shell\'s prefix')
    # Wine's fixmes stay out of the terminal, as in the session's logs.
    env.setdefault('WINEDEBUG', 'fixme-all')
    wine = os.path.realpath(config['wine'])
    os.execve(wine, [wine, *argv], env)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
