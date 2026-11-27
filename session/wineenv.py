"""The shells a session can start, and the environment its Wine programs run in.

Shared by the session (supervisor.py) and win4wine-wine (wine.py), so that a
program started with "win4wine-wine" outside the session gets the session's
prefix, Wine and opt-ins, and looks and behaves as one started from the shell."""
import os
from pathlib import Path

# The shell a session starts: XP's explorer, in a prefix made for it
# (prefix/create.py); its staging step runs before each start. XP's Welcome
# screen uses images extracted from the XP media.
SHELLS = {
    'xp': dict(marker='xp-shell.json', module='xp-explorer', command=r'C:\xp\xp-explorer.exe',
               stage=('prefix/stage.py',), welcome='greeter/xp-splash.py'),
}
# The session's settings, in ~/.config/win4wine (setup.sh)
CONFIGS = {'xp': 'session.json'}


def config_dir():
    return Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config') / 'win4wine'


def environment(base, config, shell):
    """BASE with the variables every Wine program of SHELL's session needs
    (CONFIG: its session.json; SHELL: an entry of SHELLS)."""
    wine = Path(config['wine']).resolve(strict=True)
    prefix = Path(config['prefix']).resolve(strict=True)
    # WIN4WINE_EXTERNAL_SHELL is the opt-in of win4wine's Wine changes: without
    # it a program gets Wine's own behaviour (no XP frames, for one).
    # No winemenubuilder: for each link an installer saves on the desktop it
    # writes a Linux launcher (NAME.desktop) beside it, into the prefix's
    # Desktop folder, which the shell shows as a desktop item of its own.
    # No Mono and no Gecko. In XP's prefix the registry disables mshtml (for
    # every program but iexplore.exe, which gets XP's): an override here would
    # beat it and leave Internet Explorer 6 without its HTML engine.
    disabled = 'mscoree' if shell['module'] == 'xp-explorer' else 'mscoree,mshtml'
    env = dict(base, WINEPREFIX=str(prefix), WIN4WINE_EXTERNAL_SHELL='1',
               WINEDLLOVERRIDES=f'explorer=b;{shell["module"]}=n;{disabled}=;uxtheme=b;winemenubuilder.exe=d')
    # The prefix decides its architecture; the recovery prefix takes Wine's default.
    env.pop('WINEARCH', None)
    # "wine" in the session's prefix must be this Wine, not another one in
    # PATH, which would update the prefix to its own version.
    env['PATH'] = os.pathsep.join(filter(None, (str(wine.parent), env.get('PATH'))))
    # Wine derives its user locale and ANSI code page from the Unix locale.
    # A Japanese XP shell expects Japanese formats and code page 932 even
    # when the Linux session itself runs in another locale.
    if config.get('locale'):
        env.update(LANG=config['locale'], LC_ALL=config['locale'])
    return env
