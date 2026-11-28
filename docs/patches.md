# Wine patches

`wine/patches/` holds 170 patches, applied in order to an unmodified Wine 11.0 (the commit in `wine/upstream.env`).
`wine/build.sh` applies them, and the actual applying is done by `wine/patch-state.py` (on a partly patched source it applies only
what is missing).

Renumberings and a table from earlier numbers are in [history/patch-history.md](history/patch-history.md).
Patch numbers in old records are the numbers at the time of writing, so translate them with that table.

## The opt-in (`WIN4WINE_EXTERNAL_SHELL`)

Changes for the XP shell only take effect in processes that have the environment variable `WIN4WINE_EXTERNAL_SHELL`. The session
(`session/supervisor.py`) and the prefix's helper tools set it. Ordinary Windows apps run on the same Wine behave as before.

- The check is gathered in one place, `win4wine_external_shell()` in `include/wine/win4wine.h`, added by 0019.
  ntdll reads the same variable with Rtl functions, and wow64.dll (0023) with ntdll functions.
- Without the opt-in, these take effect: fixes of documented Win32 behaviour (0001–0018, 0084, 0085, 0086, 0091, 0144–0146, 0155–0157, 0160–0170), 0025 and 0026,
  which do not change the look or behaviour of ordinary apps, most of the APIs and Wine fixes for the Windows 7 shell (0094–0112), and the
  theme fixes that only apply to version 4 styles (0113–0119).
- 0024 only takes effect with `WIN4WINE_SESSION_REQUEST`, and 0159's `LockWorkStation` only with `WIN4WINE_LOCK_REQUEST`.
- 0090 additionally needs `WIN4WINE_FOLDER_SHELL`, which upstream's ReactOS shell session set; LunaticOS sets it nowhere.
- `WIN4WINE_SHELL=reactos` (upstream's ReactOS shell session; nothing sets it here) makes 0067 leave out the parts meant for XP.
- What is marked "XP session" only takes effect with `WIN4WINE_EXTERNAL_SHELL` set and `WIN4WINE_SHELL` unset (the XP shell session, the
  only one LunaticOS has).
- 0120 takes effect with `WIN4WINE_EXTERNAL_SHELL` set and `WIN4WINE_SHELL` unset or `win7` (the XP and Windows 7 sessions). It is in the
  wineserver, so it decides from the environment the wineserver was started with.
- Most of upstream's Windows 7 shell session and look (0121–0137) only takes effect with `WIN4WINE_EXTERNAL_SHELL` set and `WIN4WINE_SHELL=win7`
  (how far is given in the table's opt-in column). The shell type check is gathered in one place, `win4wine_shell()` in 0019's header.
  0122 then skips 0079's and 0080's replacements meant for XP.

## Patches for the removed shells

LunaticOS removed upstream's ReactOS and Windows 7 shells, but their patches are still in the series:

- 0093, 0090 and 0143 (ReactOS) and 0121–0137 (Windows 7) only act with `WIN4WINE_SHELL=reactos` or `win7`, or with
  `WIN4WINE_FOLDER_SHELL`, and nothing sets those any more. They are dormant.
- 0094–0119 apply without them (APIs, Wine fixes and version 4 visual styles that the Windows 7 shell needed). They are harmless for
  XP but no longer needed.
- XP's own patches build on some of them: 0139, 0141 and 0142 reuse code from 0130, 0132 and 0133.

They go when the series is next rebuilt; that needs the XP patches that build on them rewritten, a full Wine build and a new Wine on
the EeePC.

## The list

Patches whose "opt-in" column says "yes" only take effect with the environment variables above.

### General Wine fixes (0001–0011)

Fixes towards documented behaviour; they apply to every Wine app.

| No. | Contents | Opt-in |
|---|---|---|
| 0001 | uxtheme: map GetThemeSysColor's colour indices to the theme's colours | no |
| 0002 | uxtheme: allow GetThemeFont without a DC | no |
| 0003 | uxtheme: apply theme fonts to the system by character height (pixels) | no |
| 0004 | uxtheme: read space-separated theme font attributes | no |
| 0005 | uxtheme: draw the arrows of a scroll bar disabled on one side in their disabled form | no |
| 0006 | uxtheme: accept empty strings in SetWindowTheme | no |
| 0007 | uxtheme: load and transfer shared theme images one thread at a time | no |
| 0008 | win32u: hit-test where a right click on the caption was released with WM_NCHITTEST | no |
| 0009 | win32u: TrackMouseEvent's tracking looks for the window under the cursor among all windows (moving onto another window stacked above gives WM_MOUSELEAVE; before, tracking never ended while the cursor was on a taskbar above the desktop's icon list, and the Windows 7 shell woke 25–175 times a second even when idle) | no |
| 0010 | wineboot: do not start hidden system files in the Startup folder (desktop.ini, which holds the folder's name). Wine enumerated hidden files too and opened desktop.ini in Notepad; after a prefix update (`wineboot -u`) the wineserver never exited and preparing the Windows 7 shell session timed out | no |
| 0011 | ntdll: FileAttributes 0 in FILE_BASIC_INFORMATION leaves the attributes unchanged (as documented; SetFileTime and others call it so). Wine wrote 0, and just setting a time cleared the hidden, system and read-only attributes. The Windows 7 shell sets a known folder's time after its attributes, so the Links marker was lost and desktop.ini was no longer read | no |

### APIs XP's parts call (0012–0018)

| No. | Contents | Opt-in |
|---|---|---|
| 0012 | services: signal the SCM ready event XP's explorer waits for | no |
| 0013 | setupapi: answer that there is no docking station | no |
| 0014 | wuaueng: answer "no updates" to the update check at shut down | no |
| 0015 | user32: add DestroyReasons | no |
| 0016 | user32: SetSysColorsTemp (temporary system colours and brushes for that process only; GetSysColor and GetSysColorBrush return them). XP's Display Properties uses it to draw scheme previews (AshesTom) | no |
| 0017 | winmm: expand environment variables in PlaySound's sound paths (XP's sound settings are `%SystemRoot%\media\...`). XP's startup, log off, error and other sounds play (AshesTom) | no |
| 0018 | kernelbase: return the country's English name for GetGeoInfo's GEO_FRIENDLYNAME and GEO_OFFICIALNAME. XP's "Regional and Language Options" opens (AshesTom) | no |

### External shell foundation (0019–0024)

| No. | Contents | Opt-in |
|---|---|---|
| 0019 | include: add the opt-in check and the check of the session's shell type (XP, Windows 7, ReactOS) | — |
| 0020 | explorer: leave the shell windows and endpoints to the external shell | yes |
| 0021 | kernelbase: resolve the name explorer.exe to the renamed XP shell | yes |
| 0022 | ntdll: load comctl32 from next to the executable | yes |
| 0023 | wow64: do not tell the XP shell it runs under WOW64 | yes (XP's explorer only) |
| 0024 | user32: tell the session about log off and shut down requests | `WIN4WINE_SESSION_REQUEST` |

### Working with the bridge (WM and proxy), X11 display, Japanese input (0025–0043)

| No. | Contents | Opt-in |
|---|---|---|
| 0025 | winex11: put the Win32 class and HWND on X windows (the WM reads them) | no |
| 0026 | user32: record shell hook registrations for the proxy | no |
| 0027 | win32u: return another process's standard system menu to the taskbar | yes |
| 0028 | server, win32u: let the taskbar read the icons of other processes' windows | yes |
| 0029 | win32u: open the Start menu with Ctrl+Esc and the Windows key. Whether the Windows key was alone or with other keys is told by the server's key toggle bit (when another thread handled Win+R's R release later, the dialog that opened was not activated) | yes |
| 0030 | winex11, win32u: update the work area without a display mode change. When the work area changes, refit maximized windows to it (as Windows does; switching Windows 7's taskbar between small and large icons left the bottom of maximized windows hidden behind the taskbar) | yes |
| 0031 | winex11: the window's owner decides its maximized size | yes |
| 0032 | winex11: keep the screen below popups that do not draw themselves | yes |
| 0033 | winex11: draw XP's shadow under CS_DROPSHADOW windows (on Xwayland, where the screen cannot be read, as a translucent black window) | yes |
| 0034 | shell32: send notification area icons in XP's format | yes |
| 0035 | winex11: remember the composition position given before the XIC is created, and pass the bottom of the caret's line to the input method in on-the-spot input too (so the candidate window appears below the input field). Xlib does not take an on-the-spot XIC's position at creation, so it is set again right after (before, the first candidate window in Notepad and others appeared at the window's bottom left) | yes |
| 0036 | imm32: recreate IME UI windows that went away (from the second time on, XP's Run dialog showed no composition text) | yes |
| 0037 | win32u: show parts of a window that X uncovered on screen at once (the window under a dragged window stayed black until the next message) | yes |
| 0038 | win32u: report window drags with EVENT_SYSTEM_MOVESIZESTART/END and `Win4WineSizeMove`, and restore a snapped window's size when it is pulled away. With `WIN4WINE_WINDOW_SNAP=1` maximized windows are also restored by a drag (the proxy's window snapping; only used with `window_snap`) | yes |
| 0039 | winex11: do not flush to X every time a memory DC is created (the request that creates the GC goes out with the request that uses it. Theme drawing creates many memory DCs and woke the X server each time) | yes |
| 0040 | winex11: when the X focus is lost, give the window holding the mouse capture an outside click at the pointer's position (Windows gives clicks outside the foreground app to the capture window, and a combo box's list closes). Clicking the app's own taskbar button did not close an open list | yes |
| 0041 | riched20: show IME composition text with the same underlines as Windows instead of the selection colour (dotted for unconverted, thick for the clause being converted, thin for other clauses; the cursor after the clause being converted; pixel-identical to Windows 7's search box) | yes |
| 0042 | winex11: the external shell's desktop window (the shell window) is not raised to the top in X when it gets focus. Closing a window hid XP's taskbar, and the desktop looked as if it had closed and reopened (AshesTom) | yes |
| 0043 | win32u: read the XP session's input languages from HKCU\Keyboard Layout\Preload (and Substitutes) (GetKeyboardLayoutList, the default layout, ActivateKeyboardLayout to other languages). Input languages can be changed in XP's "Text Services and Input Languages", and the session keeps them (session/keyboard.py sets up XKB from the same values). The first Preload value is only written when missing (all of Wine) | yes (XP session) |

### Window placement, UI state, menus, WM_PRINT, wallpaper (0044–0052)

| No. | Contents | Opt-in |
|---|---|---|
| 0044 | win32u: cascade windows with no requested position, as XP does | yes |
| 0045 | win32u: keep the UI state of keyboard cues (underlines and focus rectangles) | yes |
| 0046 | win32u: draw popup menus laid out as in XP | yes |
| 0047 | user32: draw descendant windows into WM_PRINT's image too | yes |
| 0048 | user32: make the ReBar's default layout explicit | yes |
| 0049 | user32: draw stretched wallpapers (also the ReactOS shell's "Fit" and "Fill") | yes |
| 0050 | win32u: answer that "Fade out menu items after clicking" is off (window opacity cannot be shown, so the chosen item stayed on the grey log off screen) | yes |
| 0051 | win32u: SetWindowPlacement on another process's window (sent to the window's thread to set). XP's and Windows 7's "Show Desktop" (the taskbar, the button at the right end, Quick Launch, Win+D) restores the minimized windows the second time. If the calling process is in the foreground, the restored window is brought to the foreground (in Windows the caller restores it with its foreground right; in the Wayland session the Start button stayed in the foreground) (checked on real hardware) | yes |
| 0052 | user32, win32u: XP's title bar animation on minimize, maximize and restore (DrawAnimatedRects' IDANI_CAPTION, moving to/from the task band in 200 ms). SPI_GETANIMATION and SPI_SETANIMATION read and write WindowMetrics\MinAnimate (on by default, as in Windows; all of Wine) (AshesTom) | yes (XP session) |

### Text and GDI pixels (0053–0064)

| No. | Contents | Opt-in |
|---|---|---|
| 0053 | win32u: use measured advance widths for monochrome text | yes |
| 0054 | user32: match DrawText's centring, wrapping and underlines to XP | yes |
| 0055 | win32u: keep HALFTONE's coordinate mapping while clipping | yes |
| 0056 | win32u: match TransparentBlt's scaling to XP | yes |
| 0057 | user32: match the size and position of classic caption glyphs to XP | yes |
| 0058 | user32: match icon alpha premultiplication to XP | yes |
| 0059 | conhost: use two cells for double-byte characters | yes |
| 0060 | win32u: show the screen below a top-level window that has not been drawn yet | yes |
| 0061 | win32u: draw at the system DPI, and match icon sizes to XP | yes |
| 0062 | win32u: draw and measure XP's ClearType (`FontSmoothingType` 2) as XP does. The prefix's setting wins over fontconfig and X's settings; sizes with embedded bitmaps (MS UI Gothic up to 22 pixels) are drawn as with Standard; the rest is drawn in monochrome at 6× width and 6 samples are counted per subpixel (7 levels, gamma 2.2); glyph widths are the same monochrome widths as Standard (line lengths do not change with ClearType) | yes |
| 0063 | win32u: in the XP session, work out the inked part (ABC widths) of text at sizes with embedded bitmaps (MS UI Gothic 9 pt and others) from the bitmap's dots (advance widths stay the outline's). ABC widths become the same as XP's, and so do edit controls' margins | yes (XP session) |
| 0064 | win32u: in the XP session, fake bold (for fonts with no bold) is 1 pixel wider to the right, as in XP (the Control Panel's "Pick a category", the user name in the Start menu, the Welcome screen) (AshesTom) | yes (XP session) |

### Controls (0065–0068)

| No. | Contents | Opt-in |
|---|---|---|
| 0065 | user32: measure the ideal size of classic push buttons | yes |
| 0066 | user32, comctl32: draw and size the Start button as XP does. Neither it nor the notification area's chevron button fills its background with the classic face brush (a grey square showed around the round chevron) | yes (the chevron: XP session) |
| 0067 | user32, comctl32: create the XP shell's controls with Wine's themed implementation | yes |
| 0068 | comctl32: match the text position of themed check boxes and group boxes to XP | yes |

### uxtheme (0069–0087)

| No. | Contents | Opt-in |
|---|---|---|
| 0069 | uxtheme, user32: draw themed window frames | yes |
| 0070 | uxtheme: decide the window's shape from the caption's shape | yes |
| 0071 | uxtheme, user32: redraw only the needed parts of Luna frames | yes |
| 0072 | user32, uxtheme: show and track the caption's help button | yes |
| 0073 | uxtheme: match theme image scaling, placement and selection to XP (scaling copies the part to 32 bpp once and picks pixels) | yes |
| 0074 | uxtheme: match theme image alpha premultiplication to XP | yes |
| 0075 | uxtheme: send a per-window theme change number in SetWindowTheme | yes |
| 0076 | uxtheme: use Japanese XP's Luna font settings (the ones in the prefix) | yes |
| 0077 | uxtheme: use values from the string table of localized msstyles | yes |
| 0078 | uxtheme: draw the Start menu user name's continuous shadow | yes |
| 0079 | uxtheme: draw the themed background of the taskbar's children; in the notification area its own background over the taskbar's, which shows through at its left end (under the chevron button) | yes |
| 0080 | uxtheme: draw the notification area's clock with the theme | yes |
| 0081 | uxtheme: draw the taskbar ReBar's grippers and trailing margin | yes |
| 0082 | uxtheme: with a theme applied, start the menu bar height at 19 (96 DPI), as in XP (Luna is 1 taller than Classic; with English XP's Tahoma it stayed at 18) | yes |
| 0083 | uxtheme, user32: draw Luna frames and theme parts with fewer transfers. Keep 8 and 24 bpp theme images at 32 bpp, draw colour-keyed parts scaled by 0073 into the window DC with a single AlphaBlend, do not draw the frame twice when only the size changed, do not draw a classic caption under Luna's (frames were drawn about twice as often while resizing; the pixels do not change) | yes |
| 0084 | uxtheme: DrawThemeParentBackground draws the parent's background at the child window's position (the rectangle is only used for clipping, as in XP. When a folder window's toolbar redrew only the buttons whose state changed, the background of the disabled "Move" and "Copy" turned black) | no |
| 0085 | uxtheme: ClassicGetSystemMetrics, ClassicSystemParametersInfoA/W, ClassicAdjustWindowRectEx, GetThemeSysSize96, GetThemeSysFont96 (used by ordinal by XP's Display Properties' themeui.dll) (AshesTom) | no |
| 0086 | uxtheme: OpenThemeDataFromFile (classes of a theme file that is not active), SetSystemVisualStyle, DrawNCPreview (Display Properties' preview; text in the language of the calling themeui.dll, the title in the style's title text colour), GetThemeParseErrorInfo. Styles can be chosen, previewed and applied in XP's Display Properties. In the XP session Wine's own Light style is not listed (AshesTom) | no |
| 0087 | uxtheme, user32: in the external shell, other processes reload the theme too when it changes (watching ThemeManager's registry changes, redrawing frames with WM_THEMECHANGED). A style applied in XP's Display Properties takes effect at once on the taskbar and folder windows as well. If the new style was loaded after WM_THEMECHANGED, it is sent again to that process's windows (the Start button's width after switching back from Classic) (AshesTom) | yes |

### The Wayland session (0088–0089)

| No. | Contents | Opt-in |
|---|---|---|
| 0088 | winex11: on a rootless X server (Xwayland), build screen reads from each window's contents (the grey log off screen) | yes |
| 0089 | win32u: do not activate a window another thread brought to the foreground if the receiving thread has since brought one of its own windows to the foreground (the send time is attached). In the Wayland session, a jump list or XP's Start menu shown right after a taskbar click turned inactive at once because of the taskbar's late activation | yes |

The labwc and wlroots patches are in `wayland/patches/` ([wayland.md](wayland.md)). Their format is the same as the Wine patches'.

### The ReactOS shell (0090–0093; upstream's; 0090 and 0093 dormant here)

| No. | Contents | Opt-in |
|---|---|---|
| 0090 | explorer: hand folder windows opened with `explorer.exe` to the external shell's explorer (only with `WIN4WINE_FOLDER_SHELL`) | yes |
| 0091 | uxtheme: read negative values of theme margins (MARGINS) correctly (the Start button of ReactOS's themes; XP's Luna has no negative margins) | no |
| 0092 | user32: SwitchToThisWindow also brings the window to the foreground (ReactOS's taskbar; in the Wayland session a restored window was not activated) | yes |
| 0093 | user32: pass the resource numbers of dialog items as "#number" names, as ReactOS does (so ReactOS's Static can load the banner and icons; only with `WIN4WINE_SHELL=reactos`) | yes |

### APIs the Windows 7 shell calls, and Wine fixes (0094–0112; upstream's, still applied)

Functions Wine lacked, and behaviour that differed from Windows, used by Windows 7's explorer, shell32 and others. Most take effect
without the opt-in.

| No. | Contents | Opt-in |
|---|---|---|
| 0094 | dlls: add the functions Windows 7's explorer, shell32 and others import (ntdll's WinSqm*, ShipAssert, RtlSetEnvironmentVar; user32's CalculatePopupWindowPosition and others; kernel32's CheckElevation and others; kernelbase's LoadStringByReference; winsta's WinStationIsSessionRemoteable; the NDR64 names in 64-bit oleaut32; rpcrt4's NdrStubCall3; gdi32's CreateBitmapFromDxSurface). Only additions; functions Wine already had do not change. PaintMonitor draws the wallpaper fitted to that monitor (with two monitors the second was black). CreateBitmapFromDxSurface returns failure (there are no shared DirectX surfaces; as a stub, pressing the Start button after clicking a folder window made explorerframe call it and the shell crashed) | no |
| 0095 | user32, samlib: make Windows 7's Start menu open. Add RegisterMessagePumpHook and UnregisterMessagePumpHook to user32 (the hook is given user32's function table, and the functions the hook returns are used. Counted per thread as in ReactOS; only the registering thread's GetMessage, PeekMessage, WaitMessage, GetQueueStatus and MsgWaitForMultipleObjectsEx go through the hook. DirectUser does deferred layout and painting inside it. Without it, creating the search box part failed and the whole Start menu was torn down; without going through the hook, folder windows' icons did not appear until input arrived). samlib's SamConnect returns failure (there is no SAM), SamCloseHandle and SamFreeMemory return success | no |
| 0096 | apphelp: AllowPermLayer (whether compatibility layers may be used; FALSE for network paths and remote drives, as in ReactOS). Called when starting a program from Windows 7's Start menu; as a stub the shell crashed | no |
| 0097 | ieframe, dsrole, samcli, ntshrui: for Windows 7's folder windows. ieframe ordinals 158 and 159 (SHRestricted2A/W, forwarded to by Windows 7's shdocvw). New builtin dsrole.dll and samcli.dll (forwarding to netapi32) and ntshrui.dll (answers that no folders are shared). Windows 7's shell32 and linkinfo delay-load them | no |
| 0098 | advapi32: add SaferiIsExecutableFileType (called by Windows 7's ShellExecute; without it, opening a file ended the process) | no |
| 0099 | xmllite: read UTF-16 input larger than 16 KB. Only for UTF-16, the converted raw buffer was not freed, and the third read copied the same 8 KB again (the folder window's UIFILE that Windows 7's DirectUI passes as UTF-16 got corrupted midway, and the Win+E window closed before it appeared). Wine's xmllite reader tests (2145) pass | no |
| 0100 | xmllite: accept XmlReaderProperty_RandomAccess. Windows 7's shell32 could not read libraries (*.library-ms); their icons were generic and the folders empty | no |
| 0101 | combase: start the DLL surrogate (dllhost) with the bitness of the side the class is registered on. 64-bit programs always started SysWOW64's 32-bit dllhost, even for classes registered on the 64-bit side (Windows 7's thumbnail cache server could not be loaded) | no |
| 0102 | ntdll: look up resources in MUI files. Resources missing from a (language-neutral) module with RT_MUI are looked up in `<folder>\<UI language>\<file name>.mui` (en-US if missing). The same for modules loaded as data files (a 64-bit DLL a 32-bit process loads from sysnative also uses the MUI file next to it). This lets the Windows 7 shell's DLLs be used as on the media, without writing resources into them | no |
| 0103 | server: share HKCU\Software between 32-bit and 64-bit (as in Windows; only under Classes is it split). When 64-bit software (Windows 7's explorer) created HKCU\Software\Wow6432Node, 32-bit programs could no longer see the user's settings (Wine's X11 settings, the theme) | no |
| 0104 | comdlg32: in the open and save dialogs (XP's form), redraw the file list after setting Wine's dialog font on it. The list was first drawn with the row height of another font (the icon title font), and in prefixes where the heights differ (Windows 7's Meiryo) clicking a row selected the file below it | no |
| 0105 | comdlg32: in Vista-style dialogs (IFileDialog), open the folder the default action (double click, Enter) came for, and build the results from absolute ID lists (SHCreateShellItemArrayFromIDLists). Inside Windows 7's shell32 ("Browse" in the Windows 7 shell's Run dialog) folders could not be entered, and the chosen file was returned as one on the desktop. Environment variables in typed names are expanded (`%APPDATA%\...`), and the ID lists of existing items are built by parsing the path (Windows 7's shell32 makes simple ID lists into files, so the folder chooser (the taskbar's "New toolbar") refused a typed folder) | no |
| 0106 | comdlg32: in the file dialogs (old form), UIActivate the created view to show it (Windows' shell32 views are created hidden; with Windows 7's shell32 the list was empty) | no |
| 0107 | shell32: leave out what only other shells' registrations name. Desktop namespace extensions are only listed if their server DLL exists, and shell32's (or serverless) ones only if this shell32 can create the class (Windows 7's libraries and Control Panel were listed with English names that could not be opened). Desktop, Computer, Documents and the Recycle Bin get shell32's own translated name if LocalizedString cannot be read | no |
| 0108 | comctl32: draw another comctl32's image list through IImageList (an HIMAGELIST is the list's IUnknown; icons from Windows 7's shell32's system image list did not appear in Wine's controls) | no |
| 0109 | kernelbase: in GetNumberFormat, zero without a leading zero (LeadingZero 0) and no decimals becomes "0", and negative numbers that look like zero lose their sign (as in Windows). An empty folder's details pane said " items" | no |
| 0110 | windowscodecs: Fant scaling (averaging over the covered area). The Windows 7 shell uses it to shrink icons to other than their own size (the details pane and others); it was nearest neighbour | no |
| 0111 | win32u: WM_SETTINGCHANGE and WM_DEVMODECHANGE sent to HWND_BROADCAST with SendNotifyMessage are sent with their string copied to the receiving thread (as in Windows 7; to a single window, and broadcasts with SendMessageCallback or PostMessage, still give ERROR_MESSAGE_SYNC_ONLY). Windows 7's Taskbar Properties announces "TraySettings" this way after writing the settings, and the taskbar reloads them. It was refused, so small icons and button combining did not change until the shell restarted | no |
| 0112 | user32: DefDlgProc's DM_REPOSITION. Fit a top-level dialog into its monitor's work area (right edge to the work area's right, bottom edge to 4 px above the work area's bottom, then left and top edges inside the work area; measured on real Windows 7; ReactOS does the same). The buttons of Windows 7's Taskbar Properties were hidden under the taskbar | no |

### Windows Vista and 7 themes (version 4 styles) (0113–0119; upstream's, still applied)

Reads msstyles format 4 and draws the parts of version 4 styles. Behaviour does not change for XP's and ReactOS's themes (format 3).

| No. | Contents | Opt-in |
|---|---|---|
| 0113 | uxtheme: read Windows Vista and 7 themes (msstyles format 4: VMAP, CMAP, VARIANT binary properties, MUI string tables, PNG images). Values are turned into the same text as format 3's ini and put into the current classes, parts and states (Windows 7 Basic's aero.msstyles). Classes take missing values from their BCMAP base class (Tooltip from TooltipStyle; without it DirectUI stopped creating the folder window). Image (PNG) colours are already premultiplied, so they are not multiplied again. How format 3 (XP, ReactOS) is read does not change | no |
| 0114 | uxtheme: remember SetWindowThemeAttribute's (WTA_NONCLIENT) WTNCA_NODRAWCAPTION and WTNCA_NODRAWICON, and the external shell's themed frame (0069) leaves out the title text and icon (Windows 7's folder windows show nothing in the title bar). It was a stub | no |
| 0115 | uxtheme: add DrawThemeParentBackgroundEx (called by Windows 7's comctl32) | no |
| 0116 | win32u, uxtheme: add the padded border (PaddedBorderWidth) to SM_CXFRAME, and give version 4 styles' caption width and buttons the close button's aspect ratio (Windows 7's 8 px border and 31x17 buttons) | Windows 7 styles (with PaddedBorderWidth) |
| 0117 | win32u, user32, uxtheme: Windows 7's themed menus. With the Menu class of version 4 styles, draw the menu bar background, pressed and hot items (active windows only; disabled windows' text is grey), and the popup's border, background, gutter, selected item, check marks, submenu arrows and separators (uxtheme's Win4WineMenuTheme called through a user32 callback). Item heights and the positions of text and shortcuts follow rules measured on a real Windows 7 SP1. With keyboard cues off, access key underlines only show while the menu is used from the keyboard (DrawText's DT_HIDEPREFIX is implemented too) | Windows 7 styles (with popup menu parts) |
| 0118 | uxtheme: DrawThemeTextEx's DTT_CALLBACK (draw with the caller's function) and DTT_COMPOSITED (draw opaquely on a surface with alpha). "Search programs and files" in Windows 7's Start menu search box did not show | no |
| 0119 | uxtheme: Windows 7's minimum part size (TS_MIN = the sum of the content margins). The folder window's address bar and search box were too tall at 47 px | only with v4 styles (Windows 7) |

### The Recycle Bin (0120)

Upstream's patch for the Windows 7 shell, widened here to the XP session.

| No. | Contents | Opt-in |
|---|---|---|
| 0120 | server: keep DACLs set on files and folders in the extended attribute `user.wine.dacl` and return them when the security is read (owner and group still come from the Unix owner). XP's shell32 compares the DACL of `RECYCLER\<SID>` with the one it set, and says "The Recycle Bin on C:\ is corrupted" if they differ; deleted files then did not show in the bin. Windows 7's does the same with `$Recycle.Bin` | yes (XP and Windows 7 sessions) |

### The Windows 7 shell session (0121–0128; upstream's, dormant here)

| No. | Contents | Opt-in |
|---|---|---|
| 0121 | ntdll: keep a directory's read-only attribute in the extended attribute `user.DOSATTRIB` without changing the Unix mode (the same form as Samba). Windows 7 makes Links, Favorites and Desktop read-only (the known folder's Attributes, set with KF_FLAG_INIT in the first session), and the shell reads desktop.ini only in read-only or system folders. Wine dropped this attribute, even the system attribute the prefix set was lost, and Favorites' "Recent Places" showed as "RecentPlaces" | yes (`WIN4WINE_SHELL=win7`) |
| 0122 | uxtheme: Windows 7's taskbar draws its notification area and clock itself. With `WIN4WINE_SHELL=win7`, 0079 (drawing only the notification area's background in DrawThemeParentBackground) and 0080 (replacing the clock) are skipped (Windows 7's notification area background is translucent; it turned black and the time showed twice). BeginBufferedPaint's BP_PAINTPARAMS (BPPF_ERASE, prcExclude, AlphaBlend with pBlendFunction), BufferedPaintClear and BufferedPaintSetAlpha as documented | partly (`WIN4WINE_SHELL=win7`) |
| 0123 | win32u, winex11, comctl32: draw Windows 7's Start button (a layered window owned by the taskbar) and the user picture in the Start menu. Translucent pixels are blended with what the owner window shows (a copy of its surface); outside the owner and where it has not drawn yet, pixels with alpha below 128 are dropped and the premultiplication of the rest is undone (the original image is kept and re-blended when the owner draws something new) (without a compositing manager the edges were black and jagged). When the window grows on hover, the new image is drawn first, then the size is changed, then it is flushed. The window is not managed by the WM, and after a resize X is synced before the image (the grown part flashed black). 0066's Start button class does not draw on layered windows. In the Wayland session (`WIN4WINE_COMPOSITED`) nothing is blended; it is left to the compositor | yes (`WIN4WINE_SHELL=win7`) |
| 0124 | shell32: send notification area icons in Windows 7's taskbar format (1484 bytes: 0034's XP form, a 32-bit balloon icon, the calling process's module path; measured with Windows 7's shell32 and a decoy taskbar window). XP's form was refused | yes (`WIN4WINE_SHELL=win7`) |
| 0125 | dwmapi, combase, ole32: for the Windows key and folder windows. On x64, DwmpActivateLivePreview (ordinal 113) returns S_OK (there is no composition, so no preview; as a stub the shell crashed on the Windows key). DwmpRegisterThumbnail (124) returns E_NOTIMPL (clicking a taskbar button crashed). With `WIN4WINE_SHELL=win7`, the free-threaded marshaler passes as-is, within the same process, interfaces it cannot get through QueryInterface, and returns them as-is on unmarshaling ("File System Error" before). On x64 ole32, HWND's and others' _User*64 (NDR64 names) as ordinary functions. In the Windows 7 shell's process (w7-explorer.exe) DwmIsCompositionEnabled is FALSE (as in Windows 7 Basic; the window list shown when pointing at a taskbar button takes the BasicMenuMode look; with TRUE it was black with Aero glass images) | partly (`WIN4WINE_SHELL=win7`) |
| 0126 | ntdll: in the Windows 7 shell session, give Windows 7's DLLs Windows 7's comctl32. The shell (`w7-explorer.exe`) gets `C:\w7\comctl32.dll` next to it; in other programs, a non-Wine DLL asking in its own manifest's context gets `C:\w7\comctl32.dll` (32-bit: `C:\w7\wow64\comctl32.dll`). The programs themselves keep Wine's Common Controls 6 | yes (`WIN4WINE_SHELL=win7`) |
| 0127 | user32: DrawText's DT_PREFIXONLY (draw only the underline). Windows 7's comctl32 list view calls it for the underline after drawing an item's name, so "Control Panel" and others in the Start menu were drawn twice and looked bold | yes |
| 0128 | combase, explorer, shell32: open folders opened from Wine's programs (`explorer C:\path`, `start C:\path` and so on) in Windows 7's explorer (REG_EXPAND_SZ expansion of LocalServer32, waiting for the /factory server, passing the file to DelegateExecute verbs). A library opened instead | partly (REG_EXPAND_SZ expansion always, as in Windows) |

### The Windows 7 look (0129–0137; upstream's, dormant here)

Matched pixel by pixel against a real Windows 7 SP1 (the reference VM).

| No. | Contents | Opt-in |
|---|---|---|
| 0129 | winex11: in the Windows 7 shell, round drop shadow colours instead of truncating them (pixel-identical to real hardware's shadows) | `WIN4WINE_SHELL=win7` |
| 0130 | comctl32, user32: details of Windows 7's comctl32 6. Edit borders (EP_EDITBORDER_*, hover and focus), drop-down lists (CBS_DROPDOWNLIST) as a button shape (CP_READONLY) with just the arrow, the status bar height from the default font (24 px), tabs (text position, the last tab's width, not taking focus on click), progress bars (filled over a translucent bar), track bars (19 px thumb and its width, channel, ticks), the text positions of group boxes, checks, radios, list boxes and status bars, the phase of DrawFocusRect's checkerboard (all measured on a real Windows 7 SP1) | `WIN4WINE_SHELL=win7` (tab focus: the whole external shell) |
| 0131 | comctl32: Windows 7's details-view list view and header. Row height (font + 4), icon and text positions, the header's minimum height (the theme item's height), the area right of the last item (HP_HEADERITEMRIGHT), the Explorer theme's selected and hot rows (LVP_LISTITEM) and column lines (LVP_COLUMNDETAIL) (measured on a real Windows 7 SP1) | `WIN4WINE_SHELL=win7` |
| 0132 | user32: load Windows 7's system icons (IDI_APPLICATION to IDI_SHIELD) from the Windows 7 user32.dll the prefix places as data (`C:\w7\res\user32-w7.dll`). Icons are shrunk by the same decimation as Windows 7 | `WIN4WINE_SHELL=win7` |
| 0133 | user32, comctl32: Windows 7's message box (white with a button band at the bottom; the layout of icon, text and right-aligned buttons), the default button's pulse and the progress bar's glow (both with a 2-second period, measured on a real Windows 7 SP1) | `WIN4WINE_SHELL=win7` |
| 0134 | win32u: Windows 7's ClearType. The same 7-level sampling as XP, blended without gamma (black text on white only takes 0, 43, 85, 128, 170, 212, 255; checked on real hardware) | `WIN4WINE_SHELL=win7` |
| 0135 | win32u: size and glyphs of Windows 7's linked fonts (Japanese inside Tahoma and so on). Decided from the base font's em; em 11 and 13 become 12 and 15 (measured on real hardware). Sizes with embedded bitmaps are drawn with those bitmaps | `WIN4WINE_SHELL=win7` |
| 0136 | riched20: text in Windows 7's search boxes (Start menu, folder windows; windowless RichEdit). Use the default format the host announces later (Meiryo 9 pt) (whole external shell), measure and draw paragraphs without complex scripts (Arabic and so on) with GDI, and create the font with DEFAULT_CHARSET (Latin in that font, Japanese through font linking; the same glyphs and positions as on real hardware). Japanese showed as boxes or overlapping | partly (`WIN4WINE_SHELL=win7`) |
| 0137 | comdlg32: give the Windows 7 shell session's item dialogs (IFileDialog) Windows 7's form. The top row (back/forward, the breadcrumb address bar, refresh, search) is drawn by a new part (navbar7.c) with theme parts and explorerframe.dll's images and strings. 640x478, the message font, no details or preview pane, a size grip, "Folder:" and "Select Folder" when choosing a folder. Search filters by name within the folder (Windows 7 searches the index) | yes (`WIN4WINE_SHELL=win7`) |

### Details of the XP shell (0138–0170)

Changes for the XP shell (and upstream's ReactOS shell) that use mechanisms added for the Windows 7 shell (0117's themed menus, 0119's TS_MIN, 0126's
comctl32 switching, 0130's and 0131's control checks, 0132's system icon loading, 0133's message box layout).
0144–0150 come from AshesTom's fork (XP's Display Properties, Internet Explorer 6 and more); they touch places the Windows 7 patches changed
(apphelp's function table, uxtheme's function table and title bar, comctl32 switching), so they are placed here.

| No. | Contents | Opt-in |
|---|---|---|
| 0138 | win32u: XP's menu bar. Items start at the bar's top edge (19 px), the last row is COLOR_MENU (white in Luna). The menu bar of a disabled window (a message box's owner and the like) is grey | yes (XP session) |
| 0139 | comctl32: XP's Common Controls 6 (compared with real hardware using xpcmp.exe). Progress bar blocks, the status bar's height (24 px) and text position, the header's height (keeping the value at creation) and the remaining area (part 0), list view and list box text positions, group boxes, check and radio labels, drop-down buttons, the track bar's channel, tabs | yes (XP session) |
| 0140 | uxtheme: TS_MIN from the content margins for XP themes too, and caption text without an icon 2 px further in | yes (XP session) |
| 0141 | user32: load XP's system icons (IDI_APPLICATION and others) from XP's user32.dll (placed by the prefix as data in `C:\xp\res\user32-xp.dll`); shrinking picks pixels | yes (XP session) |
| 0142 | user32: XP's message box layout (decided in pixels and rounded to dialog units; matches 30 cases on real hardware), and DrawFocusRect's checkerboard (XOR with the DC's text and background colours) | yes (XP session) |
| 0143 | ntdll: the ReactOS shell (ros-explorer.exe) uses `C:\ros\comctl32.dll` next to it (ReactOS's Common Controls 6; the same mechanism as 0126). Other programs keep Wine's assembly (it used to be replaced, and 32-bit apps could not create buttons and the like) | yes (`WIN4WINE_SHELL=reactos`) |
| 0144 | apphelp, advapi32: ApphelpCheckExe (allows every program), SaferGetLevelInformation (every level fully trusted), SaferRecordEventLogEntry. XP's shell32 calls them before starting programs or Control Panel items (AshesTom) | no |
| 0145 | uxtheme: export the named functions under the same ordinals as XP (GetCurrentThemeName at 42, DrawThemeBackground at 6 and so on; the "Themes" page of XP's Display Properties looks them up by ordinal) (AshesTom) | no |
| 0146 | uxtheme: IsThemeActiveByPolicy (ordinal 73: whether the group policy SetVisualStyle exists). "Windows and buttons" can be chosen in XP's Display Properties (AshesTom) | no |
| 0147 | uxtheme, win32u: title bar icons use the small icon first (as in XP, the program's own 16x16 image, not a shrunk large icon). Small icons made from a large icon come from the resource's 16x16 (LR_COPYFROMRESOURCE; all of Wine) (AshesTom) | partly |
| 0148 | user32: processes that load XP's comctl32 from next to the executable, like Display Properties (rundll32's desk.cpl and themeui.dll), also use the external shell's themed buttons and so on (AshesTom) | yes |
| 0149 | ntdll: in the XP session, when XP's comctl32.dll (native; not in the system directories) is next to the executable, comctl32's SxS redirection is not used and only that XP Common Controls 6 is (Display Properties' buttons were classic; the ReactOS shell has 0143) (AshesTom) | yes (XP session) |
| 0150 | user32: in the XP session, processes with XP's comctl32 next to them do not get versioned common control class names (Internet Explorer 6's menu bar, toolbar, address bar and status bar were not created) (AshesTom) | yes (XP session) |
| 0151 | user32: when PrivateExtractIconsW gets two sizes at once (32 and 16, 48 and 16), build each size's icon from the image chosen for that size (16x16 was a shrunk 32x32 or 48x48 with no edge alpha. XP's Quick Launch and system image list icons; 0 pixels off from real hardware; all of Wine) | no |
| 0152 | win32u: a top-level window just shown stays off the screen until it and its children have finished drawing (up to 300 ms) (new surfaces are white, and a flush before drawing finished made XP's Start menu look like a white rectangle for about 100 ms. It belongs with 0060 and has not been renumbered yet) | yes |
| 0153 | win32u: a process that sets the wallpaper (SPI_SETDESKWALLPAPER, SPI_SETDESKPATTERN) tells the thread of the shell window (XP's Progman). That thread forgets the wallpaper it had loaded and redraws the shell window with its children (win32u remembers the wallpaper path per process, and choosing the Windows XP theme in Display Properties did not show Bliss until the next logon. When only WallpaperStyle changes with the same image, 0049 does not reload it, so it takes effect from the next logon) | yes |
| 0154 | user32: in the XP session, load the icon of `C:\windows\explorer.exe` (Wine's explorer) from `C:\xp\xp-explorer.exe` (the XP theme writes My Computer's icon into HKCU as `%SystemRoot%\explorer.exe,0`, which gave a blank icon. PrivateExtractIcons, PrivateExtractIconEx) | yes (XP session) |
| 0155 | ntdll: after starting a Unix program (NtCreateUserProcess' fork_and_exec), close the image's file handle and the current directory's fd (each start leaked a handle and two fds in the caller and an open file in the wineserver; xptray, which starts the volume check every 2 s, held 6224 fds after two days. It belongs with 0001–0011 and has not been renumbered yet) | no |
| 0156 | win32u, server: GetProp answers from a cache in the process while the window's properties are unchanged (the wineserver increments a serial in the window's shared memory on every SetProp and RemoveProp; before, each GetProp was a server request, and comctl32's subclassing reads one for every message a subclassed control gets: 27 per repaint of XP's notification area. The server protocol changes. It belongs with 0001–0011 and has not been renumbered yet) | no |
| 0157 | opengl32: send immediate mode calls across in batches (glBegin, glEnd, glVertex, glTexCoord, glColor, glNormal and glMultiTexCoord2fARB are collected per thread and sent in one unix call before any other GL call; before, each was a unix call, and in a WoW64 process on a CPU without FSGSBASE each unix call is also an arch_prctl system call: Half-Life Alpha on the EeePC made 181,000 a second, about 59% of its render thread. `WINE_OPENGL_BATCH=0` turns it off. It belongs with 0001–0011 and has not been renumbered yet) | no |
| 0158 | winex11: in the XP session, `SPI_GETSCREENSAVEACTIVE` and `SPI_SETSCREENSAVEACTIVE` are the user's setting in the registry, not the X server's blanking timer (Display Properties showed and switched Xorg's blanker, which the session turns off: it starts the screen savers itself) | yes (XP session) |
| 0159 | user32, win32u: `LockWorkStation` asks the session to lock the screen (it writes the file `WIN4WINE_LOCK_REQUEST` names; before, a stub that failed), and Win+L reaches no program (the WM asks for the lock and passes the key on, so that Wine does not take the Windows key for pressed alone and open the Start menu after unlocking) | `WIN4WINE_LOCK_REQUEST`; Win+L: XP session |
| 0160 | ntdll: `NtQueryInformationByName` (Windows 10) with `FileStatInformation` and `FileStatLxInformation`, which also gives a file's Unix mode, user, group and device numbers (`NtQueryInformationFile` takes it too). By name it opens nothing: it is a path conversion and `lstat()`, without a server request, and a symbolic link is reported itself. XP Explorer's Linux columns use it; before, no Win32 or NT call returned the mode or owner. The 32-bit `NtWow64*` system calls move up by one. It belongs with 0001–0011 and has not been renumbered yet | no |
| 0161 | ole32: the OLE clipboard makes its hidden window again when the thread that made it has ended (before, every later `OleSetClipboard` in the process failed with `CLIPBRD_E_CANT_OPEN`. XP Explorer calls `OleFlushClipboard` when a folder window closes, so after the first closed window no file could be copied and Paste stayed grey). It belongs with 0001–0011 and has not been renumbered yet | no |
| 0162 | mountmgr: ask only for UDisks2's own property changes, of block devices (before, every `PropertiesChanged` on the system bus made `mountmgr.sys` fetch UDisks2's whole object tree: on the EeePC one ssh login cost about 0.5 s of CPU time in udisksd, dbus-daemon and winedevice.exe. Only where UDisks2 is installed. It belongs with 0001–0011 and has not been renumbered yet) | no |
| 0163 | mountmgr: `DBT_DEVICEARRIVAL` when a volume gets a mount point, `DBT_DEVICEREMOVECOMPLETE` when it loses it or its letter goes (before, every change UDisks2 reported was an arrival, nine for one mounted image, and unmounting one made XP Explorer say "D:\ refers to a location that is unavailable"; a volume unmounted and then removed stayed in My Computer with a question mark. A drive letter that comes (its first mount) or goes is announced without `DBTF_MEDIA`, or XP Explorer keeps a removed drive and does not add it again. It belongs with 0001–0011 and has not been renumbered yet) | no |
| 0164 | mountmgr: `IOCTL_DISK_EJECT_MEDIA` and `IOCTL_STORAGE_EJECT_MEDIA` unmount a CD or DVD drive that is a loop device (a mounted disc image) and delete the loop device, with `udisksctl`; a volume that cannot be unmounted gives `STATUS_DEVICE_BUSY`. XP's Eject sends the first to the volume; before, it was unsupported and Eject did nothing. Real drives and other devices are answered as before. It belongs with 0001–0011 and has not been renumbered yet | no |
| 0165 | gdi32: Uniscribe's font cache list stays whole when two threads make the cache of one font at once (before, Wine took the other thread's cache off the list and freed its own, which stayed on it; a later lookup crashed holding the list's lock, and every thread drawing text then waited for ever. An Eject with two folder windows open froze My Computer. It belongs with 0001–0011 and has not been renumbered yet) | no |
| 0166 | server: the cursor clip is released when the foreground thread ends (before, a program that was ended or exited while it held the mouse left its clip rectangle behind, and winex11's invisible clipping window stayed mapped over the desktop: no desktop icon took a click until another Wine window became the foreground. It belongs with 0001–0011 and has not been renumbered yet) | no |
| 0167 | shcore: `SHRegSetPathA` and `SHRegSetPathW` store the whole path (before, the length in characters was passed as the size in bytes, so half of it was stored: XP's `shimgvw.dll` registered the image types with the icon `shimgv`, and `.jpg`, `.png`, `.bmp` and `.gif` files had the unknown file's icon) | no |
| 0168 | mountmgr: a USB stick or memory card is a removable disk (`DRIVE_REMOVABLE`) with a letter for each partition that holds a file system (before, the whole disk got a letter too, and a mounted stick was a "Local Disk"). Its storage descriptor names the bus, vendor, product and serial number; FAT and exFAT are reported as FAT32 with their ID as the volume serial number. Only drives UDisks2 reports as removable. It belongs with 0001–0011 and has not been renumbered yet | no |
| 0169 | mountmgr: ejecting a USB stick or memory card unmounts its partitions and powers the drive off, with `udisksctl`; a busy one gives `STATUS_DEVICE_BUSY` and stays mounted. `IOCTL_STORAGE_MEDIA_REMOVAL`, which XP's Eject sends first, succeeds for it (before it was unsupported, and XP said "An error was encountered trying to eject"). It belongs with 0001–0011 and has not been renumbered yet | no |
| 0170 | mountmgr: a removed drive's letter is free again (before, the link to its device stayed in `dosdevices` and kept the letter; on the EeePC one stick left three letters taken). At its start it also removes the drives left over from a stick pulled out while Wine was not running. It belongs with 0001–0011 and has not been renumbered yet | no |

## Order

The groups go: fixes that need no opt-in → APIs XP's parts call → external shell foundation → working with the bridge → windows → text
and pixels → controls → uxtheme → the Wayland session → the ReactOS shell → the Windows 7 shell (APIs and Wine fixes → version 4 themes →
session → look) → XP and ReactOS details that use the mechanisms made for Windows 7. Within a group, later patches use code from earlier
ones (e.g. 0070–0072 build on 0069's frame drawing). New patches also go where they fit in a group, not piled up at the end.

## Format

- File names are `NNNN-<DLL>-[external-][win7-]<what>.patch`. `external` marks patches that only take effect with the opt-in, `win7` ones
  for the Windows 7 shell (`external-win7`: only with `WIN4WINE_SHELL=win7`). Patches spanning several DLLs use `dlls`.
- They start with `From:` and `Subject: [PATCH] <DLL>: <what>`. The body is in English, with those of the following fields that apply:
  - `Purpose:` the aim
  - `XP behavior:` / `Windows behavior:` behaviour measured on a real XP or Windows 7 (on the reference VMs, through public APIs and the screen)
  - `Wine before:` Wine's behaviour before the change, and its cause
  - `Change:` what the change does
  - `Evidence:` the tests (`tests/`) or measurements it rests on
  - `Scope:` where it applies (opt-in or not, the windows or classes affected)
  - `Old patches:` numbers from before the first reorganization (2026-10-10)
- After the body come `---`, the diffstat and the diff in `git diff` form.

## Adding a patch

- Put it where it fits in a group and renumber. A change that fixes an existing patch goes into that patch.
- Limit behaviour meant for the XP shell with `win4wine_external_shell()`. Fixes of documented Win32 behaviour that are also right for
  ordinary Wine apps are not put behind the opt-in (treated like 0001–0015).
- Check that they all apply to an unmodified Wine 11.0 with `python3 wine/patch-state.py <source> wine/patches --apply`.
- In the WoW64 build PE modules exist for both i386 and x86_64. Structures passed between processes must have fixed handle widths and
  sizes (0034). Floating point results on the Unix side must round the same in 32-bit and 64-bit (0033).
- After adding new files, or after reordering, build from configure in a fresh source and build directory to check. A reused build directory
  hides missing Makefile entries and stale objects.
