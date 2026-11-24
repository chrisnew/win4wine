# Design

win4wine's main decisions and the reasons for them, by topic. What was investigated and the details of the tests are in the
chronological record [history/investigations.md](history/investigations.md) (section names are given in parentheses).

## Rules the project keeps

- **Binaries that come from Windows are not modified.** XP's explorer.exe and DLLs are used exactly as extracted from the ISO;
  differences are absorbed by Wine (the patches) and by the project's own parts around it (WM, proxy, session).
- **Microsoft's binaries are not disassembled.** Behaviour is studied through traces of public API calls (Wine's `+relay` and the
  like), public documentation and the project's own probes running on a real XP (the C programs in `tests/`).
- **No Microsoft files go into the repository.** Not even XP's pixels or strings: only rules and measured numbers.
  Strings and images are read at run time from the user's ISO.
- **Ordinary Wine apps behave as before.** Changes for the XP shell are limited to the opt-in (`WIN4WINE_EXTERNAL_SHELL`)
  ([patches.md](patches.md)).

## Which parts come from XP

| Part | Source | Reason |
|---|---|---|
| explorer.exe | XP | It is the taskbar, the Start menu and the desktop |
| shell32, browseui, shdocvw, shlwapi | XP | Wine's browseui lacks features XP's explorer uses (ordinal 107 and others); they have to be used as a set from the same generation (investigation 1) |
| browselc, shdoclc, linkinfo, msgina, mydocs | XP (resources, some features) | Japanese menus and text, shortcuts, the log off/shut down dialogs, icons (investigations 8, 14, 36, 39) |
| comctl32 | XP's v6 (only in the XP shell's process) | XP's explorer's toolbar does not work with Wine's comctl32. Buttons and the like use Wine's themed implementation under another name (investigation 7, patches 0066, 0067) |
| uxtheme | Wine | The user's Luna theme file is drawn by Wine's uxtheme. XP's theme service is not installed |
| user32.dll (as data) | XP (icons only) | Gives apps' default icons and message box icons XP's look. Placed as `C:\xp\res\user32-xp.dll` and only its resources are read (patch 0141; the cursors are extracted from it too) |
| kernel32, ntdll, user32, gdi32 etc. | Wine | The base that runs on Linux |
| mshtml, mscoree | Disabled | IE and .NET are out of scope |

XP's DLLs are used **only in the xp-explorer.exe process** (AppDefaults). If the whole session used XP's DLLs, even Wine's
own management processes and the recovery path would depend on them ("Override の隔離", isolating the overrides).

## The Wine prefix

- A dedicated prefix is created for the XP shell. COM registrations and registry values are written by the project itself, only
  those confirmed to be needed (the registry profiles in `prefix/registry.py`); a real XP registry is never copied wholesale
  (investigation 6).
- When its version changes, Wine updates the prefix and writes its registrations back, so `prefix/stage.py` looks for the update
  marker and reinstalls XP's DLLs and registrations (investigation 13).
- The prefix is 64-bit (WoW64). XP's DLLs go in `syswow64`, and the XP shell's registrations are written on the 32-bit side only
  ("64bit（WoW64）対応", 64-bit support).

## Other shells (ReactOS, Windows 7)

Other Windows-like shells can run on the same Wine, bridge and supervisor as the XP shell. Each shell gets its own prefix, and
that shell's DLLs are used only in that shell's processes (the same override isolation as XP).

- **ReactOS** ([reactos.md](reactos.md)): free software, so it is downloaded from the official release. Registrations are written
  by the ReactOS DLLs' own `DllRegisterServer`.
- **Windows 7** ([windows7.md](windows7.md), experimental): the 64-bit shell is used (the prefix is WoW64, so it can stay 64-bit).
  There are too many registrations to check and write by hand one by one as for XP (DirectUI, the property system, known folders
  and more), so what **the component manifests of the files used** (WinSxS) register is installed. The registry of an installed
  Windows is not copied. Wine behaviour just for Windows 7 only takes effect with `WIN4WINE_SHELL=win7` and does not affect the
  XP and ReactOS shells. The exception: so that apps' file dialogs take Windows 7's form, the DLLs the dialog loads (shell32 and
  9 others) are used by apps too. Wine's own programs keep Wine's DLLs, and apps that do not work well can be switched back to
  Wine's DLLs ("Which programs use Windows 7's DLLs" in [windows7.md](windows7.md)).

## Integrating Linux apps (the bridge)

- **The WM does not draw frames.** The XP frame of a Linux window is drawn by Wine on a Win32 window that proxy.exe creates.
  The point is to draw a real XP frame with Wine and the user's theme instead of imitating XP's look
  ([architecture.md](architecture.md)).
- **XP's taskbar treats Linux windows as Win32 windows.** For that, the proxy keeps a stand-in window for each one.
- **The WM and the proxy run on events only.** They used to poll, and the frames lagged behind. Waking on X events, inotify and
  Win32 WinEvents brought the lag down to about 11 ms ("40. WM と proxy のイベント駆動化", making the WM and proxy event-driven).
- The WM stays in Python. Changing the language would not remove the causes of the lag (polling and round trips to X).
- **The WM moves a frame and its Linux window together.** Going through the proxy, at each step of a drag the frame moved first
  and the uncovered area showed black. Whichever of the two the WM gets a move request for, it moves the other in the same pass.
- **The X server keeps the contents of Wine's windows (Composite).** So that a window that becomes visible does not show black
  until its process repaints it. Linux windows' frames get the same, but only while they move (re-creating the image on every
  resize makes resizing several times slower). No compositing manager (shadows, transparency) is used, since it adds CPU load in
  virtual machines.
- **In virtual machines, X draws into memory (ShadowFB).** Without a GPU, X reads back from the virtual graphics card's memory on
  every window move, which is slow (`scripts/setup.sh`).

## Japanese input

- fcitx5 and Mozc are used. They connect to Wine through XIM, and to GTK and Qt through their input modules.
- Startup and environment variables are managed by the supervisor, not im-config, so that other sessions are not affected
  ("日本語入力（段階 6）", Japanese input, stage 6).
- The input mode is shown by putting fcitx5's icon in XP's notification area.
- Text being composed is shown inside the input field, as with XP's IME (XIM on-the-spot). fcitx5 only gives XIM clients
  on-the-spot when `UseOnTheSpot=True` is set in `~/.config/fcitx5/conf/xim.conf`, so the supervisor writes it (unless the user
  has set it). Wine passes the bottom of the caret's line to fcitx5, and the candidate window appears below it (patch 0035).
  xterm and the like stay over-the-spot.

## The login screen

- The greeter is the project's own (Python, GTK 3, cairo, Pango). XP's logonui.exe and msgina.dll are not run; only their
  resources (images and strings) are read, so that no Microsoft code runs where passwords are handled.
- Text is drawn imitating GDI's placement, and the layout matches numbers measured on a real XP's screen
  ("ログイン画面（段階 9）", the login screen, stage 9).

## DPI

- As in XP, a single `LogPixels` decides it. XP has no DPI virtualization and every app scales its own drawing, so Wine's bitmap
  scaling (which blurs) is turned off and the XP shell's processes are made system DPI aware (patch 0061).
- Things for which XP has its own rules, such as icon and cursor sizes, were measured on a real XP set to 96–216 DPI
  ("DPI の拡大", DPI scaling).
- Linux apps get the same DPI through `Xft.dpi` and the like.

## Wayland

- X11 cannot scale per monitor, so a second session was added: a Wayland compositor made by modifying labwc (in testing).
  The approaches compared and the prototype's results are in [history/wayland-study.md](history/wayland-study.md).
- labwc was modified rather than writing a compositor from scratch because Xwayland handling (XWM), outputs, input and the
  Wayland protocols can be left to wlroots and labwc, and only bridge-wm's role (XP frames, stacking order, IPC) has to be added.
- The XP shell and Wine stay X11 clients of Xwayland. XP's Explorer places windows at screen coordinates it chooses itself,
  which a Wayland client (winewayland.drv), unable to choose its position, cannot reproduce.
- The XP shell is shown one pixel per output pixel and is not scaled by the compositor (scaling blurs). DPI is decided by a single
  `LogPixels`, as in the X11 session.
- The X11 session is unchanged. What the Wine patches add for Wayland only takes another path when X's root window cannot be read.
- How it works and what is left is in [wayland.md](wayland.md).

## Comparing with a real XP

- Real XP VMs (Japanese SP3 and English SP3) serve as the reference; public API values and screen pixels are compared.
- Only numbers (dimensions, colours, rules) are measured; XP's pixels themselves never go into the repository.
- The tests are in `tests/`; the same probe runs on a real XP and on Wine and the results are compared
  ([development.md](development.md)).
- Controls and message boxes are compared pixel by pixel by showing the same window on a real XP and in the XP session, and
  Wine's side is adjusted to match XP (patches 0063, 0138–0142; for Windows 7, 0129–0135).
