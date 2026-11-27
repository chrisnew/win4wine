#!/usr/bin/env python3
"""Add XP Explorer's additions (xp-explorer/) to the XP shell session's prefix.

XP Explorer runs the same XP shell in a Wine virtual desktop and added to it:
the notification area's network, battery and volume icons with XP's own
flyouts (xptray.exe), Network Connections, the folder task pane (DirectUser),
XP's Display Properties, image thumbnails and the picture viewer, XP's sound
scheme and Explorer defaults, Internet Explorer 6, Windows Media Player 9, and
the Start menu's full left column (Internet, E-mail and the most used
programs); XP's proxies for the shell's COM interfaces, which Taskbar
Properties' Customize Notifications needs; Linux permissions, owner, group and
path as columns and in the task pane's Details section of folder windows, and
"Open in Linux Terminal" on folders, drives and folder windows' backgrounds
(linuxcols.dll, linux-launch.exe); "Mount" on .iso files, which makes a disc
image a CD drive (linux-launch.exe, iso_mount.py); and XP's Regional and
Language Options, whose input languages the session's keyboard layout follows
(session/keyboard.py). This applies them to a prefix made by prefix/create.py, with
xp-explorer/scripts/prefix.py's own code, and again when they change.

  extras.py PREFIX --wine WINE      apply (scripts/setup.sh runs it)
  extras.py PREFIX --check WINE     exit 1 when they need applying again
  extras.py PREFIX --stage          put XP's files back after a Wine update
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import xp_schemes

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / '.local/bridge'
# Bumped when apply() changes; setup.sh applies it again on existing prefixes.
VERSION = '15'
MARKER = '.xp-extras'
# What goes from .local/bridge to C:\xp\bin.
HELPERS = ('xptray.exe', 'startmenu.exe', 'iexplore.exe', 'linux-links.exe', 'linux-launch.exe',
           'linuxcols.dll', 'iso_mount.py', 'iso-disc.ico')
# linuxcols.dll's column handler (xp-explorer/helpers/linuxcols.c), and the
# property set of its columns.
LINUX_COLUMNS = '{48E91554-B8E3-45CE-A5C9-E7E65E654FF0}'
LINUX_PROPERTIES = '{9EFE7827-D3D3-4E38-BE40-BBFF8B12F657}'
# Its context menu handler, "Open in Linux Terminal", and the classes that ask
# it: folders, drives and the background of a folder window or the desktop.
LINUX_TERMINAL = '{8524E83F-A4B4-4B25-8120-520029CC6172}'
TERMINAL_CLASSES = ('Directory', 'Drive', r'Directory\Background')
# The static verbs of version 11, which would show the item a second time.
OLD_VERBS = ('Directory', 'Drive')
# The task pane's Details section for files and folders: XP's own lines, and
# between them the handler's hidden columns with a name in their values (IDs 6
# to 9). XP reads a property ID after a space; after a comma it takes 0.
LINUX_DETAILS = 'prop:Name;Type;' + ''.join(f'{LINUX_PROPERTIES} {n};' for n in range(6, 10)) + 'Write;Size'
# XP's Regional and Language Options and what it loads (assets/intl), native
# in rundll32 and in XP Explorer, whose Control Panel lists it.
INTL = ('intl.cpl', 'input.dll', 'syssetup.dll', 'sfcfiles.dll')
INTL_NATIVE = ('intl.cpl', 'input', 'syssetup', 'sfcfiles')
# XP's actxprxy.dll (assets/com): the proxies with which a COM call crosses
# apartments, native in XP Explorer only. It covers every interface of Wine's
# actxprxy and has the same class ID. Wine's lacks ITrayNotify and
# INotificationCB, which Customize Notifications and the notification area use
# to talk to each other.
COM = ('actxprxy.dll',)
COM_PROXY = '{B8DA6310-E19B-11D0-933C-00A0C90DCAA9}'
TRAY_INTERFACES = (('{FB852B2C-6BAD-4605-9551-F15F87830935}', 'ITrayNotify'),
                   ('{D782CCBA-AFB0-43F1-94DB-FDA3779EACCB}', 'INotificationCB'))
# The class of .iso files, whose "Mount" starts linux-launch.exe --mount-iso.
ISO_CLASS = 'win4wine.isoimage'
LAYOUTS_KEY = r'System\CurrentControlSet\Control\Keyboard Layouts'


def xp_explorer():
    """xp-explorer/scripts/prefix.py, with its helpers taken from .local/bridge."""
    spec = importlib.util.spec_from_file_location('xp_explorer_prefix', ROOT / 'xp-explorer/scripts/prefix.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.BIN = BRIDGE
    return module


def registry(xpx):
    """What XP Explorer's registry profile adds to the session's (prefix/registry.py)."""
    r = xpx.Reg()
    cu = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\'
    advanced = cu + r'Explorer\Advanced'
    # XP's Internet Explorer 6 (and the Linux browser stand-in, also named
    # iexplore.exe); IE6's engine is native in it only.
    xpx.ie_registry(r)
    # DirectUser (the folder task pane), mydocs and XP's Display Properties,
    # native in XP Explorer and in rundll32, which XP's shell32 starts
    # Control Panel items with.
    cpl = tuple(c.rsplit('.', 1)[0] if c.endswith('.dll') else c for c in xpx.CPL)
    for dll in ('duser', 'mydocs') + cpl:
        r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', dll, 'native')
    for dll in xpx.NATIVE + cpl + INTL_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\rundll32.exe\DllOverrides', dll, 'native,builtin')
    for dll in INTL_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', dll, 'native')
    # Everywhere else Wine's own proxies, though XP's file is in syswow64. An
    # extraction without it leaves XP Explorer with Wine's too.
    r.add(r'HKCU\Software\Wine\DllOverrides', 'actxprxy', 'builtin')
    r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', 'actxprxy', 'native,builtin')
    for iid, name in TRAY_INTERFACES:
        r.add('HKCR\\Interface\\' + iid, None, name)
        r.add('HKCR\\Interface\\' + iid + '\\ProxyStubClsid32', None, COM_PROXY)
    # startmenu.exe drives XP's own Start panel settings and pins.
    for dll in ('shell32', 'shlwapi', 'comctl32', 'linkinfo', 'shdocvw', 'browseui'):
        r.add(r'HKCU\Software\Wine\AppDefaults\startmenu.exe\DllOverrides', dll, 'native')
    # XP's task pane on the left of folder windows ("Use common tasks in folders").
    r.add(advanced, 'WebView', '1', 'REG_DWORD')
    r.add(advanced, 'Start_ShowRun', '1', 'REG_DWORD')
    r.add(advanced, 'Start_ShowNetPlaces', '1', 'REG_DWORD')
    # XP's own list of most used programs stays empty under Wine; the pins of
    # startmenu.exe recent stand in for it, without empty rows. StartMenuInit:
    # XP's first logon would clear the usage data and the pins.
    r.add(advanced, 'Start_MinMFU', '0', 'REG_DWORD')
    r.add(advanced, 'StartMenuInit', '2', 'REG_DWORD')
    # The notification area's icons start with the shell, as XP's systray.exe did.
    r.add(cu + 'Run', 'XPTray', r'C:\xp\bin\xptray.exe')
    # XP's shell32 registers this key itself (Open With's lists); the Start menu reads it too.
    associations = r'HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\FileAssociation'
    r.add(associations, 'KillList', '%1;explorer.exe;dvdplay.exe;mplay32.exe;msohtmed.exe;quikview.exe;'
          'rundll.exe;rundll32.exe;taskman.exe;bck32api.dll;')
    r.add(associations, 'CutList', ['Application File', 'MFC Application'], 'REG_MULTI_SZ')
    # XP's .theme files name their visual style with %ResourceDir%.
    r.add(r'HKLM\System\CurrentControlSet\Control\Session Manager\Environment', 'ResourceDir',
          r'%SystemRoot%\resources', 'REG_EXPAND_SZ')
    # The folder task pane, Display Properties' pages and the Start menu's
    # other classes (xp-explorer/scripts/prefix.py SERVERS).
    for clsid, dll in xpx.SERVERS:
        r.add('HKCR\\CLSID\\' + clsid + '\\InprocServer32', None, dll)
        r.add('HKCR\\CLSID\\' + clsid + '\\InprocServer32', 'ThreadingModel', 'Apartment')
    # Linux permissions, owner, group and path in folder windows' Details view.
    linux_columns = 'HKCR\\CLSID\\' + LINUX_COLUMNS
    r.add(linux_columns, None, 'Linux columns')
    r.add(linux_columns + '\\InprocServer32', None, r'C:\xp\bin\linuxcols.dll')
    r.add(linux_columns + '\\InprocServer32', 'ThreadingModel', 'Apartment')
    r.add('HKCR\\Folder\\shellex\\ColumnHandlers\\' + LINUX_COLUMNS, None, 'Linux columns')
    # Where XP lists its own extensions (checked with EnforceShellExtensionSecurity).
    r.add(r'HKLM\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved', LINUX_COLUMNS,
          'Linux columns')
    # XP's setup gives these keys no Details list in this prefix, and a file
    # type's own list comes first.
    for key in ('*', 'Directory'):
        r.add('HKCR\\' + key, 'Details', LINUX_DETAILS)
    # "Open in Linux Terminal" (linuxcols.dll's second class), which starts
    # linux-launch.exe (session/linux-launch.c).
    linux_terminal = 'HKCR\\CLSID\\' + LINUX_TERMINAL
    r.add(linux_terminal, None, 'Open in Linux Terminal')
    r.add(linux_terminal + '\\InprocServer32', None, r'C:\xp\bin\linuxcols.dll')
    r.add(linux_terminal + '\\InprocServer32', 'ThreadingModel', 'Apartment')
    for key in TERMINAL_CLASSES:
        r.add('HKCR\\' + key + '\\shellex\\ContextMenuHandlers\\LinuxTerminal', None, LINUX_TERMINAL)
    r.add(r'HKLM\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved', LINUX_TERMINAL,
          'Open in Linux Terminal')
    iso = 'HKCR\\' + ISO_CLASS
    r.add(iso, None, 'Disc Image File')
    r.add(iso + '\\DefaultIcon', None, r'C:\xp\bin\iso-disc.ico')
    r.add(iso + '\\shell', None, 'mount')
    r.add(iso + '\\shell\\mount', None, '&Mount')
    r.add(iso + '\\shell\\mount\\command', None, r'"C:\xp\bin\linux-launch.exe" --mount-iso "%1"')
    return r


def iso_class(wine, prefix):
    """Make .iso files ISO_CLASS's, unless a program has given them a class of its own."""
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='mscoree,mshtml=')
    env.pop('WINEARCH', None)
    query = subprocess.run([wine, 'reg', 'query', r'HKCR\.iso', '/ve'], env=env, capture_output=True,
                           text=True, errors='replace', check=False)
    value = re.search(r'REG_(?:EXPAND_)?SZ[ \t]+(.*)', query.stdout) if not query.returncode else None
    # "(value not set)": a key without a default value.
    if not value or value[1].strip() in ('', '(value not set)', ISO_CLASS):
        regedit(wine, prefix, 'Windows Registry Editor Version 5.00\r\n\r\n'
                f'[HKEY_CLASSES_ROOT\\.iso]\r\n@="{ISO_CLASS}"\r\n')


def removals():
    """The keys of earlier versions that are in the way (Reg only adds)."""
    keys = ''.join(f'[-HKEY_CLASSES_ROOT\\{key}\\shell\\linuxterminal]\r\n\r\n' for key in OLD_VERBS)
    return 'Windows Registry Editor Version 5.00\r\n\r\n' + keys


def keyboard_layouts(assets):
    """XP's keyboard layouts (HKLM's Keyboard Layouts), as XP's setup
    registers them: HIVESYS.INF's US layouts and INTL.INF's language groups.
    input.dll lists them; without the key it reports an incompatible
    keyboard driver. Input methods (IME) are left out: they need their own
    files."""
    import xpreg
    intl = assets / 'intl'
    texts = [xpreg.hive_entries(xpreg.read_inf(intl / 'hivesys.inf'), 'HKLM', (LAYOUTS_KEY,))]
    raw = (intl / 'intl.inf').read_bytes()
    text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('latin-1')
    # Its strings and the sections that add the layouts of each language
    # group, named [AddReg] for xpreg (not the ones that remove them).
    lines, section = [], None
    for line in text.splitlines():
        if line.strip().startswith('['):
            section = line.strip().strip('[]').lower()
            if section == 'strings' or section.startswith('kbd.lgreg.install.'):
                lines.append('[Strings]' if section == 'strings' else '[AddReg]')
        elif section == 'strings' or (section or '').startswith('kbd.lgreg.install.'):
            lines.append(line)
    texts.append(xpreg.hive_entries('\n'.join(lines), 'HKLM', (LAYOUTS_KEY,)))
    return texts


def regedit(wine, prefix, text):
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'extras.reg'
        reg.write_text(text, encoding='utf-16')
        # The 32-bit regedit writes the view that the 32-bit XP shell reads.
        subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                       env=env, check=True)


def screen_saver_mapping(wine, prefix):
    """Display Properties writes the chosen screen saver to system.ini's [boot]
    and reads it from the registry: Windows keeps that entry in the registry
    (IniFileMapping), which Wine follows once it is set up."""
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='mscoree,mshtml=')
    env.pop('WINEARCH', None)
    # Wine reads the mapping from the 64-bit view only.
    subprocess.run([wine, 'reg', 'add',
                    r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\IniFileMapping\system.ini\boot',
                    '/v', 'SCRNSAVE.EXE', '/d', r'USR:Control Panel\Desktop', '/f', '/reg:64'], env=env, check=True)


def stop(wine, prefix):
    env = dict(os.environ, WINEPREFIX=str(prefix))
    server = str(Path(shutil.which(wine) or wine).resolve().with_name('wineserver'))
    subprocess.run([server, '-k'], env=env, check=False, timeout=30)
    subprocess.run([server, '-w'], env=env, check=False, timeout=120)


def copy_files(assets, prefix):
    xp = prefix / 'drive_c/xp'
    (xp / 'bin').mkdir(parents=True, exist_ok=True)
    for helper in HELPERS:
        shutil.copyfile(BRIDGE / helper, xp / 'bin' / helper)
    if (assets / 'tray').is_dir():
        shutil.copytree(assets / 'tray', xp / 'tray', dirs_exist_ok=True)
    native = xp / 'native'
    for path in [assets / 'system32/duser.dll', *(assets / 'cpl').glob('*'), *(assets / 'shell').glob('*')]:
        if path.is_file():
            shutil.copyfile(path, native / path.name)
    # XP's screen savers where the Screen Saver tab looks for them: the 32-bit
    # system directory (syswow64), with logon.scr ("Windows XP") from the cpl group.
    system = prefix / 'drive_c/windows/syswow64'
    for path in [assets / 'cpl/logon.scr', *(assets / 'screensavers').glob('*.scr')]:
        if path.is_file() and system.is_dir():
            shutil.copyfile(path, system / path.name)
    # XP's Windows Classic theme for Display Properties. A prefix has both
    # Wine's windows/resources/themes and win4wine's Resources/Themes; on
    # Linux they are two folders, and Display Properties lists one of them.
    windows = prefix / 'drive_c/windows'
    for name in ('Resources/Themes', 'resources/themes'):
        if (assets / 'themes').is_dir() and (windows / name).is_dir():
            shutil.copytree(assets / 'themes', windows / name, dirs_exist_ok=True)
    for name in COM:
        if (assets / 'com' / name).is_file():
            shutil.copyfile(assets / 'com' / name, native / name)
    if (assets / 'intl/input.dll').is_file():
        for name in INTL:
            shutil.copyfile(assets / 'intl' / name, native / name)
        (windows / 'inf').mkdir(exist_ok=True)
        shutil.copyfile(assets / 'intl/intl.inf', windows / 'inf/intl.inf')
    # Ours, so that XP Explorer's stage() keeps the common controls 6 assembly
    # in winsxs and puts XP's comctl32 beside rundll32 (Wine patch 0149).
    (prefix / '.patched-wine').write_text('1\n')


def themes(assets, prefix, wine):
    """Display Properties' Windows XP and Windows Classic themes."""
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='themeui=n')
    env.pop('WINEARCH', None)
    # The Windows XP theme's Bliss.bmp: stage.py's stage_media, at each start.
    # themeui's own registration: Theme.Manager, the visual style folders,
    # .theme and .msstyles files.
    subprocess.run([wine, r'C:\windows\syswow64\regsvr32.exe', '/s', r'C:\windows\system32\themeui.dll'],
                   env=env, check=False)
    # The Windows Classic style's color schemes and the themes' visual style
    # settings, which XP's themeui writes at first logon and Wine's does not:
    # made from the user's HIVEDEF.INF and themeui.dll (prefix/xp_schemes.py).
    hivedef, themeui = assets / 'inf/hivedef.inf', assets / 'cpl/themeui.dll'
    if hivedef.is_file() and themeui.is_file():
        regedit(wine, prefix, xp_schemes.registry(xp_schemes.xpreg.read_inf(hivedef), themeui))


def stage(prefix, xpx=None):
    """XP's files back in syswow64 after Wine updates the prefix: XP
    Explorer's, the COM proxies over Wine's, and the Regional and Language
    Options over Wine's intl.cpl."""
    (xpx or xp_explorer()).stage(prefix)
    # The helpers as bridge-wm/build.sh last built them.
    for helper in HELPERS:
        source, target = BRIDGE / helper, prefix / 'drive_c/xp/bin' / helper
        if source.is_file() and target.parent.is_dir() and \
                (not target.is_file() or target.read_bytes() != source.read_bytes()):
            temporary = target.with_name(f'.{helper}.new')
            shutil.copyfile(source, temporary)
            temporary.replace(target)
    system = prefix / 'drive_c/windows/syswow64'
    for name in INTL + COM:
        source, target = prefix / 'drive_c/xp/native' / name, system / name
        if source.is_file() and (not target.is_file() or target.read_bytes() != source.read_bytes()):
            temporary = target.with_name(f'.{name}.xp')
            shutil.copyfile(source, temporary)
            temporary.replace(target)


def state(prefix):
    try:
        return (prefix / MARKER).read_text().strip()
    except OSError:
        return ''


def wanted(xpx, wine):
    # IE6 and Windows Media Player register again after each Wine build:
    # wineboot registers Wine's own mshtml and wmp over them.
    return f'{VERSION} {xpx.wine_build(wine)}'


def apply(prefix, wine):
    config = json.loads((prefix / 'xp-shell.json').read_text())
    assets = Path(config['assets'])
    missing = [h for h in HELPERS if not (BRIDGE / h).is_file()]
    if missing:
        raise SystemExit(f'Missing {", ".join(missing)}: run bridge-wm/build.sh first')
    xpx = xp_explorer()
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='mscoree,mshtml=')
    env.pop('WINEARCH', None)
    print('* XP Explorer additions: files', flush=True)
    stop(wine, prefix)
    copy_files(assets, prefix)
    print('* XP Explorer additions: registry', flush=True)
    regedit(wine, prefix, removals())
    regedit(wine, prefix, registry(xpx).text())
    regedit(wine, prefix, xpx.xp_registrations(assets))
    iso_class(wine, prefix)
    screen_saver_mapping(wine, prefix)
    if (assets / 'intl/input.dll').is_file():
        for text in keyboard_layouts(assets):
            regedit(wine, prefix, text)
    # The Linux system's layout becomes XP's input language, once.
    subprocess.run([sys.executable, str(ROOT / 'session/keyboard.py'), 'seed', str(prefix), '--wine', wine],
                   check=False)
    # With the wineserver stopped (KnownDlls), XP's files go in place.
    stop(wine, prefix)
    stage(prefix, xpx)
    # SHGetSetSettings writes the task pane setting where XP's shell32 reads it.
    subprocess.run([wine, r'C:\xp\bin\startmenu.exe', 'webview'], env=env, check=False)
    print("* XP's sounds, Explorer settings, image previews and Network Connections", flush=True)
    xpx.xp_defaults(assets, prefix, wine, folders=False)
    stop(wine, prefix)
    print("* XP's Windows XP and Windows Classic themes", flush=True)
    themes(assets, prefix, wine)
    stop(wine, prefix)
    print('* Internet Explorer 6 and Windows Media Player 9', flush=True)
    xpx.install_ie(assets, prefix, wine)
    # Its wineboot registers XP's shell32 again, whose My Computer icon is
    # %SystemRoot%\Explorer.exe,0: Wine's explorer.exe, a blank page. Ours is
    # XP's explorer under its own name (prefix/registry.py).
    regedit(wine, prefix, 'Windows Registry Editor Version 5.00\r\n\r\n'
            '[HKEY_CLASSES_ROOT\\CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\DefaultIcon]\r\n'
            '@="C:\\\\xp\\\\xp-explorer.exe,0"\r\n')
    stop(wine, prefix)
    stage(prefix, xpx)
    (prefix / MARKER).write_text(wanted(xpx, wine) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--wine')
    parser.add_argument('--check', metavar='WINE')
    parser.add_argument('--stage', action='store_true')
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    if not (prefix / 'xp-shell.json').is_file():
        raise SystemExit('Not a dedicated XP prefix')
    if args.check:
        sys.exit(state(prefix) != wanted(xp_explorer(), args.check))
    if args.stage:
        if state(prefix):
            stage(prefix)
        raise SystemExit
    apply(prefix, args.wine or json.loads((prefix / 'xp-shell.json').read_text())['wine'])
