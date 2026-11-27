# The ReactOS shell

A session that runs [ReactOS](https://reactos.org/)'s `explorer.exe` and the DLLs around it instead of the XP shell, so that LunaticOS can
be tried without an XP ISO. It looks like ReactOS's "Classic" (the colours of Windows 2000's "Windows Standard").
It runs on the same Wine, the same bridge (window manager, stand-in windows, tray) and the same supervisor as the XP shell.

```sh
scripts/setup.sh --shell reactos --lang ja    # for English, --lang en
```

Choose the session "**ReactOS Shell (Wine, X11)**" on the login screen. If the XP shell is installed too, both sessions are listed.

## The ReactOS files used

`extract/reactos.py` downloads ReactOS's official release (`ReactOS-0.4.16-i386.zip` from `0.4.16-release` on GitHub), checks its
SHA-256 and extracts only the following from the ISO into `.local/assets-reactos`. The downloaded zip stays in `.local/dl`.
`--zip` uses a zip you already have.

| File | Use |
|---|---|
| `explorer.exe` | The shell, placed as `C:\ros\ros-explorer.exe` |
| `shell32`, `browseui`, `shdocvw`, `shlwapi`, `comctl32` (5 and 6), `zipfldr` | DLLs used only by ros-explorer.exe |
| `msgina` | The "Shut Down" and "Log Off" dialogs |
| `tahoma`, `tahomabd`, `Marlett`, `DroidSansFallback` | ReactOS's free fonts (Japanese characters come from DroidSansFallback) |
| `user32.dll` | Only its cursors are read, to make the X cursor theme `win4wine-reactos` |
| `Nebula.bmp` | ReactOS's default wallpaper |
| The `.msstyles` of `lautus`, `lunar` and `mizu` | Optional themes (the default is Classic) |

The 32-bit version (i386) is used: it runs on the same WoW64 32-bit path as the XP shell, and 64-bit ReactOS only exists as nightly builds.

### Licence

ReactOS as a whole is GPL-2.0-or-later. Most of the source of the DLLs used is LGPL-2.1-or-later (including what comes from Wine), and
some files are GPL-2.0. The fonts have their own licences (Bitstream Vera family, GPL with the font exception, Apache-2.0).
LunaticOS does not bundle ReactOS's files; it only downloads them from the official release. If you distribute the extracted files,
follow the GPL and LGPL and point to the corresponding source (`source` in `manifest.json`, ReactOS's `0.4.16-release` tag).

## The prefix (`prefix/reactos.py`)

`.local/prefix-reactos` is a WoW64 prefix, built on the same ideas as the XP prefix.

- ReactOS's DLLs go in `syswow64` (shell32 and others are KnownDlls, so the exe's directory is not searched). The prefix as a whole keeps
  Wine's DLLs (`builtin` in DllOverrides), and only `ros-explorer.exe` uses `native` through AppDefaults.
  Common Controls 6 goes in `C:\ros\comctl32.dll` and only ros-explorer.exe uses it (Wine patch 0143; the same mechanism as Windows 7's
  0126). Wine's assembly in `winsxs` stays as it is, and other programs use Wine's Common Controls 6 (Wine's files used to be replaced,
  and other 32-bit apps then could not create buttons or edit controls).
- ReactOS's explorer only becomes the shell if `Shell` in `HKLM\...\Winlogon` holds its own file name. It is set to `ros-explorer.exe`.
- COM registrations and the like are written by the `DllRegisterServer` of ReactOS's own shell32, browseui and shdocvw (32-bit view).
- "Open" and "Explore" on folders open in ros-explorer.exe (the same form as ReactOS's hivecls.inf).
- In Japanese, DroidSansFallback is linked to UI fonts such as Tahoma with FontLink. Wine rebuilds FontLink when started in a different
  locale, so the prefix is created in that locale and the `stage` before each session reinstalls it.
  For places FontLink does not reach (such as a folder window's address bar) and for Linux apps, `setup.sh` also installs
  `fonts-noto-cjk` for Japanese.
- In Japanese, the Start menu's "Startup" and "Administrative Tools" folders get the same Japanese names as in ReactOS (Wine creates them
  with English names). Wine uses the locations in User Shell Folders, so the renamed folders are used as they are.
- Appearance (only when created): theming off (Classic), ReactOS's default colours (`win32ss/gdi/ntgdi/stockobj.c`), metrics and fonts
  (`boot/bootdata/hivedef.inf`), the Nebula wallpaper (stretched), "Log Off" in the Start menu.
  Wine's namespace items "Internet" and "/" are not shown on the desktop. "Help and Support" is not shown (Wine has no help).
- `reactos.py stage` runs before every session and reinstalls DLLs and registrations overwritten by a Wine update.

## The session

With `"shell": "reactos"` in session.json (`~/.config/win4wine/reactos-session.json`) the supervisor:

- Starts `C:\ros\ros-explorer.exe` and uses `prefix/reactos.py stage`. XP's Welcome screen is not shown.
- Sets `WIN4WINE_FOLDER_SHELL`. The ReactOS shell opens the Control Panel, Search, Win+E and a shortcut's "Open File Location" by starting
  `explorer.exe`, but this prefix's explorer.exe is Wine's desktop manager. With patch 0090, Wine's explorer hands such starts to
  ros-explorer.exe.
- Sets `WIN4WINE_SWITCHER=reactos`, and the proxy draws the Alt+Tab window in ReactOS's form (`appswitch.c`).

The bridge and the patches for XP work for the ReactOS shell as they are (taskbar buttons, frames for Linux windows, Alt+Tab, the Windows
key and Ctrl+Esc, the notification area, log off and shut down).

Quick Launch is shown with a right click on the taskbar → "Toolbars" → "Quick Launch". The prefix creates the Quick Launch folder
(`%APPDATA%\Microsoft\Internet Explorer\Quick Launch`; without it you get "Cannot create toolbar."). ReactOS 0.4.16's explorer does not
save the toolbar layout, though (a limit of ReactOS itself; it does not save folder toolbars either), so it has to be shown again in each
session.

## Themes (optional)

The default is Classic, but ReactOS's three themes can be chosen: `scripts/setup.sh --shell reactos --theme lautus` (or `lunar`, `mizu`),
or `python3 prefix/reactos.py theme PREFIX lautus`. Themes are drawn by Wine's uxtheme (applied with `msstyles` in
`prefix/theme-settings.c`). The Start button of ReactOS's themes has negative margins, so Wine patch 0091 makes negative margin values
readable.

## The login screen (optional)

`scripts/setup.sh --shell reactos --greeter` makes LightDM's login screen ReactOS's (`greeter/reactos-greeter.py`). The dialogs of
ReactOS's msgina.dll ("Press Ctrl-Alt-Delete" → "Log On", "Shut Down") are drawn with their template layout and text (Japanese and
English) and banner image, in the Classic look, over ReactOS's wallpaper (`greeter/rosdialog.py`). Authentication is done by LightDM and PAM.

- On "Welcome", Ctrl+Alt+Delete goes on to "Log On". `"ctrl_alt_del": false` in `/etc/win4wine/reactos-greeter.json` shows "Log On"
  straight away.
- The user name in "Log On" is the user LightDM chooses (the last one). "Log on to" is this machine's name.
- "Shut Down" offers shut down and restart (and sleep, where possible).
- The layout matches positions measured on ReactOS 0.4.16 at 1024×768 (`tests/reactos_greeter.py`).

## Differences from the XP shell, and limits

- The look is close to ReactOS but not matched pixel by pixel as the XP shell is. Text in particular: ReactOS synthesizes bold from
  regular Tahoma, while Wine uses `tahomabd.ttf`, so bold text has a different width.
- "Help and Support", networking and printer settings cannot be used, since Wine lacks those parts.
- The "Folders" list of folder windows also shows Wine's Unix root (`/`) (it is not shown on the desktop).
- Linux apps start in the prefix's `C:\windows` if their `.desktop` has no working directory (`Path`) (as in the XP shell).
- The Wayland session (`--wayland`, "ReactOS Shell (Wine, Wayland)") runs on the same labwc as the XP shell's Wayland session.
  As for the XP shell, it is more experimental than the X11 session ([wayland.md](wayland.md)).

The test is `tests/reactos_session.py` ([development.md](development.md)).
