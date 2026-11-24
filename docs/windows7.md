# The Windows 7 shell (experimental)

A session that runs Windows 7 SP1 (x64)'s `explorer.exe` and the DLLs around it instead of the XP shell. It looks like
"Windows 7 Basic" (the Aero theme drawn without desktop composition). Aero effects such as translucent glass are not there yet.
It runs on the same Wine, the same bridge (window manager, stand-in windows, tray) and the same supervisor as the XP shell.

```sh
scripts/setup.sh --shell win7 --media /path/to/Windows7SP1x64.iso
```

Choose the session "**Windows 7 Shell (Wine, X11)**" on the login screen. If the XP or ReactOS shells are installed too, the sessions are
listed side by side. With `--greeter`, LightDM's login screen becomes Windows 7's logon screen and leads straight into the Windows 7 shell.

> [!WARNING]
> This is a prototype, newer than the XP shell and checked less. Read "Limits" below before trying it.
> Bring your own Windows 7 ISO, and handle Microsoft's files according to Microsoft's licence terms.

## The Windows 7 files used

`extract/win7.py` extracts only the following from the ISO's `sources/install.wim` into `.local/assets-win7`.
Of the per-edition images it uses Professional (or the last one, if there is none). The shell files are the same in every edition.
The language is the image's default language, and that language's MUI files (`ja-JP\shell32.dll.mui` and so on) are extracted too.

| File | Use |
|---|---|
| `explorer.exe` | The shell (x64), placed as `C:\w7\w7-explorer.exe` |
| `explorerframe`, `shell32`, `shlwapi`, `propsys`, `shdocvw`, `dui70`, `duser`, `profapi`, `thumbcache`, `linkinfo` | The Windows 7 shell's DLLs, used by the shell and by the file dialogs of the session's programs ("Which programs use Windows 7's DLLs" below) |
| `sndvolsso`, `winbrand`, `shacct`, `authui` | DLLs only the shell uses (volume in the notification area; the Start menu's edition name, user picture and shut down) |
| `actxprxy` | The proxies of Windows 7's interfaces, placed as `w7actxprxy.dll` (Wine's actxprxy.dll stays as the proxy for Wine's DLLs) |
| `imageres`, `sud`, `DeviceCenter` | DLLs only read for strings and icons (no override) |
| `Cursors\aero_*.cur`, `aero_*.ani`, `user32.dll` | Windows 7's Aero cursors (made into the X cursor theme `win4wine-win7`; the I-beam and the cross come from user32.dll) |
| `Branding\Basebrd\basebrd.dll` | The edition name on the logon screen |
| 32-bit versions (`SysWOW64`) of the DLLs above and `imageres` | File dialogs, icons and names in 32-bit programs |
| Common Controls 6 (amd64 and x86) | The comctl32 used only by Windows 7's DLLs: `C:\w7\comctl32.dll` (32-bit: `C:\w7\wow64\comctl32.dll`) |
| user32.dll (data) | Only read for system icons: `C:\w7\res\user32-w7.dll` (Wine's 0132) |
| `ja-JP\notepad.exe.mui` (data) | Only read for the text file type's name ("Text Document", and the name for New): `C:\w7\res\notepad.exe.mui`. Notepad itself is Wine's |
| Segoe UI, Tahoma, Microsoft Sans Serif, Meiryo and MS UI Gothic (for Japanese) | Windows 7's UI and dialog fonts |
| `Resources\Themes\Aero` | The Aero theme (format 4 msstyles, read with Wine's 0113) |
| `img0.jpg`, User Account Pictures, the Start menu's `desktop.ini` | The wallpaper, user pictures, the Japanese names of Start menu folders |

The extracted files are not modified. In the prefix, as in Windows 7, MUI files are placed in the language folder next to the DLL
(`system32\ja-JP\shell32.dll.mui`) and Wine reads them (Wine patch 0102: resources missing from a language-neutral DLL are looked up in that
DLL's MUI file). Files placed under another name (`w7-explorer.exe`, `w7actxprxy.dll`) get MUI files of that name too.

### The registry

Windows 7 records files and registry values in per-component manifests (`WinSxS\Manifests`). `extract/manifests.py` turns the manifests of
the components of the files above into a `.reg`: registry keys and values, and the defaults of settings (the configuration schema). The
registry of an installed Windows is never copied wholesale; only what the components of the files used register is installed. Values that
point to an icon by index (`shell32.dll,3`) are changed to the resource ID (`shell32.dll,-4`) (the IDs are the same in Windows 7's and Wine's
shell32, but the order is not). 32-bit components have their own manifests (`wow64_`), so the 32-bit registrations (`registry-wow64.reg`)
are made the same way and imported with the 32-bit regedit. The text file type and its icon are taken from the notepad component (Wine's
Notepad opens them).

The `FileOpenDialog` registered by Windows 7's comdlg32 component (the dialog the shell's "Browse" opens) is pointed by prefix/win7.py to
Wine's comdlg32 dialog (that component's files are Wine's, so they are not extracted).

## The prefix (`prefix/win7.py`)

`.local/prefix-win7` is a WoW64 prefix, built on the same ideas as the XP prefix.

- The Windows 7 shell's DLLs go in `C:\w7\native` (64-bit) and `C:\w7\wow64` (32-bit) and are copied into `system32` and `syswow64`.
  Which program uses which is described in "Which programs use Windows 7's DLLs" below.
- Windows 7's Common Controls 6 goes in `C:\w7\comctl32.dll` (32-bit: `C:\w7\wow64\comctl32.dll`). In the Windows 7 shell session the
  shell (`w7-explorer.exe`) and the Windows 7 DLLs inside other programs use it, while the programs themselves keep Wine's Common Controls 6
  (Wine patch 0126; making programs use Windows 7's comctl32 breaks Wine's apps). `dllhost.exe`, which makes thumbnails, keeps Wine's
  Common Controls 6.
- `Shell` in `HKLM\...\Winlogon` is set to `w7-explorer.exe`.
- The user folders (Documents, Pictures, Music, Videos and so on) and libraries show the same Japanese names as in Windows 7 (desktop.ini,
  and the system attribute in place of Windows 7's read-only; nothing is written into ones that are links to Linux folders).
- Font linking (FontLink) and substitution (FontSubstitutes) are the same as in Japanese Windows 7 (MS Shell Dlg is MS UI Gothic,
  MS Shell Dlg 2 is Tahoma, Segoe UI gets Tahoma and Meiryo; values read on the reference machine). Text in old-style dialogs uses the same
  fonts as Windows 7. With files extracted earlier (without MS UI Gothic and Tahoma), Meiryo UI stands in as before.
- Appearance: ClearType (Windows 7's default, with the same colour fringes as Windows 7: Wine's 0134), the Aero theme
  (`prefix/theme-settings.c`). The theme's font (Meiryo 9 pt) is saved as the user's font (`prefix/theme-fonts.c`, as when Windows applies a
  theme; the Japanese font names are in the theme's MUI, so it runs in the prefix's language). The wallpaper is converted to
  `TranscodedWallpaper.bmp`, as in Windows 7.
- "Documents" is the Linux home (`H:`), as in the XP shell.
- `win7.py stage` runs before every session and reinstalls DLLs and registrations overwritten by a Wine update (the names of "Computer",
  "Control Panel", "Network" and so on).

### Which programs use Windows 7's DLLs

To give Windows apps' open and save dialogs Windows 7's form, the DLLs the dialog loads are used by apps too. All other DLLs are for the
shell only.

| Program | Windows 7 DLLs | Setting |
|---|---|---|
| The shell (`w7-explorer.exe`) | All the shell DLLs from the table above (14) and Windows 7's Common Controls 6 | `native` through AppDefaults |
| Windows apps (64-bit and 32-bit) | The file dialog's 10 (`explorerframe`, `shell32`, `shlwapi`, `propsys`, `shdocvw`, `dui70`, `duser`, `profapi`, `thumbcache`, `linkinfo`) | `native,builtin` in DllOverrides |
| Wine's own programs (the desktop's `explorer.exe`, services, `start`, `winemenubuilder`, `winebrowser`, `control`, `rundll32`, `winecfg` and others) | None | `builtin` through AppDefaults |
| `dllhost.exe`, which makes thumbnails | The file dialog's 10 (Common Controls 6 is Wine's) | Only comctl32 `builtin` through AppDefaults |

Inside apps, Windows 7's DLLs use Windows 7's Common Controls 6 and the app itself uses Wine's (Wine patch 0126).

Apps that do not work well with Windows 7's DLLs can be made to use Wine's (their file dialogs become Wine's). They are named by
executable, and the choice is recorded in the prefix, so it sticks.

```sh
prefix/win7.py builtin .local/prefix-win7 app.exe          # use Wine's DLLs
prefix/win7.py builtin .local/prefix-win7 app.exe --undo   # back to Windows 7's DLLs
```

To try it once, start it from a terminal in the session like
`WINEDLLOVERRIDES="$WINEDLLOVERRIDES;explorerframe,shell32,shlwapi,propsys,shdocvw,dui70,duser,profapi,thumbcache,linkinfo,comctl32=b" wine app.exe`
(adding to the value the session sets).

## Cursors and the logon screen

- Cursors: `greeter/cursors.py --win7` makes the `.cur` and `.ani` files of Windows 7's Aero scheme (the default assignment in the
  shell-cursors component's manifest) into the X cursor theme `win4wine-win7`. The shadows are the ones in Windows 7's files. Busy
  (`aero_busy.ani`) and Working in Background (`aero_working.ani`) are animated. Wine apps and Linux apps alike use this theme's cursors.
- Logon screen: `greeter/win7-greeter.py` (a LightDM greeter) draws Windows 7's LogonUI screen (`greeter/w7logon.py`). The background
  (imageres.dll, the one closest to the screen's aspect ratio), the user's picture and its frame, the password field and the arrow button,
  the buttons and text at the bottom (authui.dll and its MUI, Meiryo) and the edition name (basebrd.dll) come from the media's files. Sizes
  and positions come from authui's layout file (UIFILE) and from measurements on a real Windows 7 SP1 (1024x768 and 1280x800). There is the
  message for a wrong password, the shut down button and the ▲ menu beside it (restart, sleep/hibernate where possible, shut down).
  The user picture is the one set in LightDM (AccountsService), or Windows 7's default `user.bmp`.
- With several users, as in Windows 7, the list of users comes first (small framed pictures and names, sorted by name, one centred row),
  and a "Switch User" button appears below the chosen user's picture and password field. Choose with a click or the arrow keys and Enter;
  "Switch User", Esc or Alt+W go back to the list. The frame image (authui.dll), spacing, positions and button sizes were measured on real
  hardware (1280x800, 2 and 4 users, long names). Wrapping when the users do not fit in one row has not been measured.
- After logon and when ending: until the shell starts, "Welcome"; while logging off, "Logging off..."; for shut down and restart,
  "Shutting down...", shown with the spinning mark on the logon screen's background (`greeter/w7-splash.py`). The text comes from the MUIs
  of winlogon.exe and wininit.exe, the spinning mark from imageres.dll's BITMAPs 5000–5004 (18 frames), and the arrangement from authui's
  layout (the status display) and measurements on real hardware. It uses the same files as the logon screen, so it appears when installed
  with `--greeter` (`welcome_screen` in session.json turns it off).

## The session

With `"shell": "win7"` in session.json (`~/.config/win4wine/win7-session.json`) the supervisor:

- Starts `C:\w7\w7-explorer.exe` and uses `prefix/win7.py stage`. The Welcome screen is Windows 7's ("Cursors and the logon screen" above).
- Sets `WIN4WINE_SHELL=win7`. The Wine patches just for Windows 7 (0120, 0121, parts of 0123–0125) only take effect with it.
- Linux windows are grouped into taskbar buttons per app (X11's `WM_CLASS`, Wayland's app_id) (the proxy gives windows an AppUserModelID).
  For apps in the Start menu, the jump list's title and icon are the app's, the app can be pinned with "Pin this program to taskbar", and
  it can be started from the pinned button (`session/linux_apps.py` writes names, commands and icons into `C:\xp\linux-apps\relaunch.tsv`,
  and the proxy sets the same window properties as Windows 7's shell32 (AppUserModel's RelaunchCommand, RelaunchIconResource,
  RelaunchDisplayNameResource); the title used to be "proxy").
- Linux tray icons (StatusNotifierItem and XEmbed) appear in Windows 7's notification area (Wine's 0124).
- Japanese input is the same fcitx5 + Mozc as in the XP shell. The input mode ("あ" and so on) is shown in the notification area, and in
  Wine apps composition happens inside the window.
- Shortcuts that Windows apps create in the Start menu are listed under "All Programs".
- Open and save dialogs: Windows apps (64-bit and 32-bit) get Windows 7's dialog (the list is drawn by Windows 7's shell32 and
  explorerframe; the dialog's frame is Wine's comdlg32, given Windows 7's size, layout and font, and the top row's back/forward, the
  breadcrumb address bar (▸ on each folder lists its subfolders, a click on the empty part lets you type), refresh and the search box are
  drawn with the same parts as on real hardware: Wine's 0137). Search filters the current folder by name (real hardware searches the
  Windows Search index, which the prefix does not have). It first opens the "Documents" folder (real hardware opens the Documents library;
  listing libraries needs Windows Search's components). Apps that use the old-style dialog, like Notepad, also get Windows 7's list
  (Wine's 0106, 0108). Linux apps (GTK 3, GTK 4, Qt) get their own dialogs. In both, "Documents" (`H:`) is the Linux home, and files with
  Japanese names can be opened and saved.
- The Windows key, Ctrl+Esc (from Linux windows too), Win+R, Win+E and Alt+Tab work.
- Right-clicking the taskbar and opening "Properties" lets you change "Use small icons" (a 30 px tall taskbar) and "Taskbar buttons" (Always
  combine, hide labels / Combine when taskbar is full / Never combine), as on real hardware. "Apply" or "OK" changes the taskbar at once,
  and maximized windows (Wine's and Linux's alike) are refitted to the new work area (Wine's 0030, 0111, 0112). The settings stay in the
  prefix's registry (`TaskbarSmallIcons` and `TaskbarGlomLevel` in `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced`) and
  carry over to the next session. Linux windows' buttons become labelled buttons too.
- Quick Launch: as on real hardware, right-click the taskbar → "Toolbars" → "New toolbar" and choose
  `%APPDATA%\Microsoft\Internet Explorer\Quick Launch`, and it appears on the taskbar (untick "Show Text" and "Show title", unlock the
  taskbar and move it left for the pre-Vista layout). Right after moving it left, as on real hardware, the Quick Launch band takes the rest
  of the width and running apps are pushed to the right edge. Dragging the running apps band's grip to the left returns it to a normal width
  (saved on log off). The folder contains "Show desktop" and "Switch between windows", as for Windows 7's default user (the prefix places
  them once, extracted from the ISO; EXTRACT_VERSION 6). Toolbars are saved on log off and carry over to the next session. The second
  "Show desktop" restores the windows, and the window last in use comes back to the foreground (Wine's 0051). The folder chooser dialog has
  the same "Folder:" and "Select Folder" as on real hardware, expands typed values such as `%APPDATA%`, and accepts a typed folder
  (Wine's 0105, 0137).
- Pinning: right-click a taskbar button and choose "Pin this program to taskbar", and Windows apps and Linux apps alike stay on the taskbar
  and can be started from there (they survive log off). Real hardware's default pins (Internet Explorer and so on) are not created.
- The bright glow in the empty part of the task list (with large icons) is the same as on real hardware (Windows 7's TaskBand2 part; not
  shown with small icons).
- Folders open in Windows 7's folder window both from Windows apps and from Wine's own programs (`wine explorer C:\path`,
  `wine start C:\path`) (Wine's 0128). The heights of the folder window's address bar and search box are the same as on real hardware
  (Wine's 0119).
- The folder window's navigation pane is the same as on real hardware: Favorites with Downloads, Desktop and Recent Places (links and
  Japanese names that the prefix's `user-profile.exe` creates in the Links folder), and per-library icons (Wine's 0100).
  `user-profile.exe` also has Windows 7's shell32 create the default libraries (Documents, Pictures, Videos, Music).
  When Downloads is a link to a Linux folder, no desktop.ini is placed there, so its icon is an ordinary folder.
- Where icons are shown at other than their own size, such as the details pane, they are shrunk by area averaging, as in Windows 7
  (Wine's 0110).
- List views (Explorer theme) and the menu bar highlight the item under the mouse, as on real hardware. Menu bar text of disabled windows
  is grey (Wine's 0131, 0117).
- Japanese text in Tahoma (MS Shell Dlg 2) and the like has the same size and glyphs as on real hardware (8 pt dialog text is MS UI
  Gothic's 12 px bitmap; Wine's 0135).
- Text files are "Text Document" in the Type column (a string from Windows 7's Notepad MUI; Notepad itself is Wine's).
- Alt+Tab has Windows 7 Basic's form. It is drawn with the Windows 7 theme's `AltTab` class (background, the frame of the chosen item, the
  title's font and colour), and its size and arrangement match a real Windows 7 SP1 (7 per row, as many rows as fit the work area).
  "Desktop" goes at the end of the first row; choosing it minimizes all windows (Windows 7 can restore them after showing the desktop;
  here they are restored one by one).
- Window frames and caption buttons are the same size as in Windows 7 Basic (8 px border, 31x17 minimize, maximize and close buttons;
  Wine's 0116; compared pixel by pixel with a Notepad of the same position and size on a real Windows 7 SP1).
- Menus (menu bar and popups) are drawn with the Windows 7 theme (Wine's 0117, 0129). Item sizes, text positions and shadows are
  pixel-for-pixel the same as on real hardware (except for ClearType's colours). As Windows 7's default, keyboard cues are hidden, and
  access key underlines only show while the menu is used with Alt or F10.
- System icons (apps' default icon; message boxes' error, warning, information, question; the shield) are Windows 7's. Windows 7's
  user32.dll is placed, as data only, in `C:\w7\res\user32-w7.dll`, and Wine's user32 reads from it (Wine's 0132; renamed so it is not
  confused with Wine's user32).
- Message boxes have Windows 7's form (white with a button band at the bottom, buttons aligned right), and the default button's pulse and
  the progress bar's glow are also the same as in Windows 7 (Wine's 0133).
- The text in the Start menu's right column is drawn only once, as on real hardware (Wine's 0127).
- Windows apps' controls (Wine's Common Controls 6) match Windows 7 for drop-down lists, status bars, tabs, progress bars, edit borders,
  track bars, list views (details view, the Explorer theme's selection and column lines), the dotted focus rectangle and text positions
  (Wine's 0130, 0131).
- "Shut down" in the Start menu, and "Log off" and "Restart" in the menu beside it, end the session (as in the XP shell, shut down and
  restart run `shutdown_command` and `reboot_command` from session.json).

- The wallpaper is drawn per monitor when there are several, as on real hardware (PaintMonitor in Wine's 0094).
- In a Japanese prefix, the default font of Notepad (Wine's) is set to "ＭＳ ゴシック" (MS Gothic) 12 pt, the same as Japanese Windows 7's
  Notepad (a font the user chose is not changed). Wine's default (System) has no Japanese characters, and on Linux without Japanese fonts
  they showed as boxes.
- The window list shown when pointing at a taskbar button looks the same as on a real Windows 7 Basic (the shell is told there is no
  desktop composition; Wine's 0125).
- There is also a Wayland session, "Windows 7 Shell (Wine, Wayland)" (`setup.sh --shell win7 --wayland`). It can scale per monitor, and the
  compositor draws the Start button's edge with real transparency ([wayland.md](wayland.md)).

The Wine patches added for the Windows 7 shell are 0094–0136 ([patches.md](patches.md)).

## Limits

- No Aero effects (glass, live thumbnails, Aero Peek), since there is no desktop composition (DWM).
- The "Ease of Access" button on the logon screen (`--greeter`) does nothing.
  There is no input language button at the top left.
- What was checked is Japanese Windows 7 Professional SP1 (x64). English was checked by replacing the extracted files' ja-JP MUIs with the
  en-US ones from Microsoft's English language pack (KB2483139) (logon screen, "Welcome" and "Logging off", Start menu, folder windows,
  file dialogs; X11 and Wayland). The font is Segoe UI and the locale `en_US.UTF-8`. English media itself has not been tried yet.
- Networking, Help and Support, and the contents of the Control Panel cannot be used, since Wine lacks those parts.
- "Lock", "Sleep" and "Hibernate" in the Start menu do nothing.
- Wine runs the user as an administrator, so apps that show whether they run as administrator in their title (Notepad++'s
  "[Administrator]" and the like) say so (the same with Wine on any prefix that runs as Windows Vista or later; it is not caused by
  Windows 7's DLLs).
