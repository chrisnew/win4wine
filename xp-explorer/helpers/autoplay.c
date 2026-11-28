/* XP's AutoPlay and AutoRun for a drive that has arrived.
 *
 *   autoplay.exe E:             xptray.exe starts it for each USB stick, memory
 *                               card and mounted disc image that arrives
 *   autoplay.exe PATH [--disc]  the same for a folder, as if it were a stick
 *                               (or with --disc a CD), for trying it out
 *
 * On XP the Shell Hardware Detection service did this. Wine publishes no volume
 * device interfaces for that service to hear of drives, so this program does it,
 * with XP's own dialog templates, texts and icons, read from the user's XP files
 * (C:\xp\native\shell32.dll).
 *
 * A CD or disc image with an autorun.inf runs its AutoRun, its "open" or
 * "shellexecute" line, as a CD did on XP. AutoRun is never taken from a USB
 * stick or memory card: those are other people's, and Windows 7 stopped running
 * it from them for the same reason. Shift held while the drive arrives skips
 * both. Otherwise the drive's files are looked at, two folders deep, and XP's
 * dialog offers what fits; a disc with nothing it knows opens in a window. What
 * "Always do the selected action" keeps is in XP's own place for it,
 * UserChosenExecuteHandlers. Public Win32 APIs only. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winioctl.h>
#include <dbt.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* How long XP's Explorer gets, after a drive arrived, to take it in before a
 * window of it is asked for: one asked for too early shows "E:\" without the
 * label and an empty address bar, and keeps them. On the bench 2.6 s were too
 * short and 3.2 s enough. */
#define SETTLE_MS 3500
/* How long looking at the files may take: the dialog must not wait for a large stick. */
#define SCAN_MS 700

#define PICTURES 1
#define MUSIC    2
#define VIDEO    4
#define OTHER    8

#define CHOSEN L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers\\UserChosenExecuteHandlers"
#define WMP_HANDLER L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers\\Handlers\\MSPlayMediaOnArrival"

/* XP's shell32.dll resources */
#define DIALOG_MIXED   1120   /* "This disk or device contains more than one type of content." */
#define DIALOG_CONTENT 1121   /* with "Always do the selected action." */
#define ITEM_DRIVE_ICON 1000
#define ITEM_LIST       1003
#define ITEM_ALWAYS     1004
#define ITEM_TYPE_ICON  1005
#define ITEM_TYPE_NAME  1006

enum kind { SLIDESHOW, ASSOCIATED, MEDIA_PLAYER, LINUX_PLAYER, OPEN_FOLDER, NOTHING };

typedef struct
{
    enum kind kind;
    const WCHAR *handler;   /* the name XP keeps a choice under */
    WCHAR action[128], provider[128];
    HICON icon;
} ACTION;

static WCHAR root[MAX_PATH];          /* "E:\" or a folder with a backslash */
static int drive = -1;                /* 0 is A:; -1 for a folder */
static BOOL disc, stick;
static ULONGLONG started;
static HMODULE xp_shell;
static ACTION actions[5];
static int action_count, found, chosen = -1;
static BOOL always;
static WCHAR first_picture[MAX_PATH], first_music[MAX_PATH], first_video[MAX_PATH];

static void xp_string(UINT id, const WCHAR *fallback, WCHAR *out, int size)
{
    if (!xp_shell || !LoadStringW(xp_shell, id, out, size)) lstrcpynW(out, fallback, size);
}

static HICON xp_icon(int id, int size)
{
    HICON icon = xp_shell ? LoadImageW(xp_shell, MAKEINTRESOURCEW(id), IMAGE_ICON, size, size, 0) : NULL;
    return icon ? icon : LoadIconW(NULL, IDI_APPLICATION);
}

static BOOL exists(const WCHAR *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* ---- the drive ----------------------------------------------------------- */

/* Whether the drive is a USB stick or memory card, whatever its files are. */
static BOOL removable_medium(void)
{
    WCHAR volume[64];
    STORAGE_PROPERTY_QUERY query = {StorageDeviceProperty, PropertyStandardQuery, {0}};
    union { STORAGE_DEVICE_DESCRIPTOR d; char bytes[1024]; } answer = {0};
    DWORD done;
    HANDLE device;
    BOOL removable = FALSE;

    if (!GetVolumeNameForVolumeMountPointW(root, volume, ARRAYSIZE(volume))) return FALSE;
    volume[wcslen(volume) - 1] = 0;
    device = CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) return FALSE;
    if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &answer, sizeof(answer),
                        &done, NULL))
        removable = answer.d.RemovableMedia;
    CloseHandle(device);
    return removable;
}

/* "HOLIDAY (E:)", "Removable Disk (E:)", as XP names the drive. */
static void drive_name(WCHAR *out, size_t size)
{
    WCHAR label[64] = L"";
    if (drive < 0)
    {
        lstrcpynW(out, root, (int)size);
        return;
    }
    if (!GetVolumeInformationW(root, label, ARRAYSIZE(label), NULL, NULL, NULL, NULL, 0) || !label[0])
    {
        if (disc) lstrcpynW(label, L"CD Drive", ARRAYSIZE(label));
        else xp_string(9221, L"Removable Disk", label, ARRAYSIZE(label));
    }
    swprintf(out, size, L"%ls (%c:)", label, L'A' + drive);
}

/* ---- AutoRun ------------------------------------------------------------- */

/* Runs LINE of autorun.inf: a program or document, from the disc's root, and
 * what is passed to it. */
static void run_line(WCHAR *line)
{
    WCHAR *file = line, *rest, path[MAX_PATH * 2];
    SHELLEXECUTEINFOW info = {.cbSize = sizeof(info), .nShow = SW_SHOWNORMAL, .lpDirectory = root};

    if (*file == L'"')
    {
        file++;
        rest = wcschr(file, L'"');
    }
    else rest = wcspbrk(file, L" \t");
    if (rest) *rest++ = 0;
    while (rest && (*rest == L' ' || *rest == L'\t')) rest++;
    while (*file == L'\\') file++;
    swprintf(path, ARRAYSIZE(path), L"%ls%ls", root, file);
    if (!exists(path)) wcscat(path, L".exe");
    /* Not on the disc: a program of Windows, found as Run finds it. */
    info.lpFile = exists(path) ? path : file;
    info.lpParameters = rest && *rest ? rest : NULL;
    ShellExecuteExW(&info);   /* says itself why it could not */
}

/* Runs the disc's AutoRun; FALSE if it has none. */
static BOOL autorun(void)
{
    WCHAR inf[MAX_PATH + 16], line[MAX_PATH * 2];
    swprintf(inf, ARRAYSIZE(inf), L"%lsautorun.inf", root);
    if (!exists(inf)) return FALSE;
    GetPrivateProfileStringW(L"autorun", L"open", L"", line, ARRAYSIZE(line), inf);
    if (!line[0]) GetPrivateProfileStringW(L"autorun", L"shellexecute", L"", line, ARRAYSIZE(line), inf);
    if (!line[0]) return FALSE;
    run_line(line);
    return TRUE;
}

/* ---- what is on the drive ------------------------------------------------ */

static int file_kind(const WCHAR *name)
{
    static const struct { const WCHAR *extensions; int kind; } kinds[] = {
        { L".jpg.jpeg.jpe.png.gif.bmp.tif.tiff.", PICTURES },
        { L".mp3.wma.wav.ogg.oga.flac.m4a.aac.mid.midi.", MUSIC },
        { L".avi.mpg.mpeg.wmv.mp4.m4v.mkv.mov.webm.", VIDEO },
    };
    const WCHAR *dot = wcsrchr(name, L'.');
    WCHAR wanted[16];
    if (!dot || wcslen(dot) > 6) return OTHER;
    swprintf(wanted, ARRAYSIZE(wanted), L"%ls.", dot);
    CharLowerW(wanted);
    for (int i = 0; i < 3; i++)
        if (wcsstr(kinds[i].extensions, wanted)) return kinds[i].kind;
    return OTHER;
}

static void scan(const WCHAR *folder, int depth)
{
    WCHAR pattern[MAX_PATH * 2], path[MAX_PATH * 2];
    WIN32_FIND_DATAW data;
    HANDLE find;
    swprintf(pattern, ARRAYSIZE(pattern), L"%ls*", folder);
    if ((find = FindFirstFileW(pattern, &data)) == INVALID_HANDLE_VALUE) return;
    do
    {
        /* Not what the drive's own system keeps: "System Volume Information", ".Trash-1000". */
        if ((data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) || data.cFileName[0] == L'.')
            continue;
        if (wcslen(folder) + wcslen(data.cFileName) + 2 >= MAX_PATH) continue;
        swprintf(path, ARRAYSIZE(path), L"%ls%ls", folder, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (depth)
            {
                wcscat(path, L"\\");
                scan(path, depth - 1);
            }
        }
        else
        {
            int kind = file_kind(data.cFileName);
            found |= kind;
            if (kind == PICTURES && !first_picture[0]) wcscpy(first_picture, path);
            if (kind == MUSIC && !first_music[0]) wcscpy(first_music, path);
            if (kind == VIDEO && !first_video[0]) wcscpy(first_video, path);
        }
        if (GetTickCount64() - started > SCAN_MS) break;
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

/* ---- the actions --------------------------------------------------------- */

static ACTION *add_action(enum kind kind, const WCHAR *handler, HICON icon)
{
    ACTION *action = &actions[action_count++];
    action->kind = kind;
    action->handler = handler;
    action->icon = icon;
    return action;
}

/* "using PROVIDER" */
static void using_text(const WCHAR *provider, WCHAR *out, int size)
{
    WCHAR format[64], *at;
    xp_string(17160, L"using %s", format, ARRAYSIZE(format));
    if (!(at = wcsstr(format, L"%s"))) lstrcpynW(out, provider, size);
    else swprintf(out, size, L"%.*ls%ls%ls", (int)(at - format), format, provider, at + 2);
}

static void shell_action(enum kind kind, const WCHAR *handler, UINT text, const WCHAR *fallback, UINT provider,
                         const WCHAR *provider_fallback, int icon)
{
    ACTION *action = add_action(kind, handler, xp_icon(icon, 32));
    WCHAR name[128];
    xp_string(text, fallback, action->action, ARRAYSIZE(action->action));
    if (!provider) return;
    xp_string(provider, provider_fallback, name, ARRAYSIZE(name));
    using_text(name, action->provider, ARRAYSIZE(action->provider));
}

/* A text of Windows Media Player's registered handler, "@file,-id". */
static void handler_text(HKEY key, const WCHAR *value, const WCHAR *fallback, WCHAR *out, int size)
{
    if (RegLoadMUIStringW(key, value, out, size * sizeof(WCHAR), NULL, 0, NULL) || !out[0])
        lstrcpynW(out, fallback, size);
}

/* The program that plays FILE: one the type is associated with, else Windows
 * Media Player, else whatever Linux opens it with. */
static void add_player(const WCHAR *file)
{
    WCHAR program[MAX_PATH], name[128], player[MAX_PATH];
    DWORD size = ARRAYSIZE(program), name_size = ARRAYSIZE(name);
    const WCHAR *extension = wcsrchr(file, L'.');
    HKEY key;
    HICON icon = NULL;
    ACTION *action;

    if (extension &&
        SUCCEEDED(AssocQueryStringW(0, ASSOCSTR_EXECUTABLE, extension, L"open", program, &size)) && exists(program) &&
        !StrStrIW(program, L"rundll32") && !StrStrIW(program, L"winebrowser") && !StrStrIW(program, L"notepad"))
    {
        if (FAILED(AssocQueryStringW(0, ASSOCSTR_FRIENDLYAPPNAME, extension, L"open", name, &name_size)))
            lstrcpynW(name, PathFindFileNameW(program), ARRAYSIZE(name));
        ExtractIconExW(program, 0, &icon, NULL, 1);
        action = add_action(ASSOCIATED, L"MSPlayMediaOnArrival", icon ? icon : xp_icon(5, 32));
        lstrcpynW(action->action, L"Play", ARRAYSIZE(action->action));
        using_text(name, action->provider, ARRAYSIZE(action->provider));
        return;
    }
    ExpandEnvironmentStringsW(L"%ProgramFiles%\\Windows Media Player\\wmplayer.exe", player, ARRAYSIZE(player));
    if (exists(player) && !RegOpenKeyExW(HKEY_LOCAL_MACHINE, WMP_HANDLER, 0, KEY_READ, &key))
    {
        ExtractIconExW(player, 0, &icon, NULL, 1);
        action = add_action(MEDIA_PLAYER, L"MSPlayMediaOnArrival", icon ? icon : xp_icon(5, 32));
        handler_text(key, L"Action", L"Play", action->action, ARRAYSIZE(action->action));
        handler_text(key, L"Provider", L"Windows Media Player", name, ARRAYSIZE(name));
        using_text(name, action->provider, ARRAYSIZE(action->provider));
        RegCloseKey(key);
        return;
    }
    if (exists(L"Z:\\usr\\bin\\xdg-open"))
    {
        action = add_action(LINUX_PLAYER, L"LinuxPlayer", xp_icon(3, 32));
        lstrcpynW(action->action, L"Play", ARRAYSIZE(action->action));
        using_text(L"the Linux player", action->provider, ARRAYSIZE(action->provider));
    }
}

static void build_actions(void)
{
    if (found == PICTURES)
        shell_action(SLIDESHOW, L"MSShowPicturesOnArrival", 17156, L"View a slideshow of the images",
                     17157, L"Windows Picture and Fax Viewer", 249);
    if (found == MUSIC) add_player(first_music);
    if (found == VIDEO) add_player(first_video);
    shell_action(OPEN_FOLDER, L"MSOpenFolder", 17154, L"Open folder to view files", 17155, L"Windows Explorer", 5);
    shell_action(NOTHING, L"MSTakeNoAction", 17168, L"Take no action", 0, NULL, 338);
}

/* Starts PROGRAM; returns its process ID, or 0. */
static DWORD start_program(const WCHAR *program, WCHAR *line)
{
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(program, line, NULL, NULL, FALSE, 0, NULL, L"C:\\", &startup, &process)) return 0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return process.dwProcessId;
}

typedef struct { DWORD process; HWND window; } VIEWER;

static BOOL CALLBACK find_viewer(HWND window, LPARAM parameter)
{
    VIEWER *viewer = (VIEWER *)parameter;
    DWORD process;
    GetWindowThreadProcessId(window, &process);
    /* Its main window, once it shows the picture's name. */
    if (process != viewer->process || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) ||
        !GetWindowTextLengthW(window))
        return TRUE;
    viewer->window = window;
    return FALSE;
}

/* XP's viewer starts its slideshow on F11: press it once the window is there. */
static void start_slideshow(DWORD process)
{
    VIEWER viewer = {process, NULL};
    for (int tries = 0; process && tries < 100 && !viewer.window; tries++)
    {
        Sleep(200);
        EnumWindows(find_viewer, (LPARAM)&viewer);
    }
    if (!viewer.window) return;
    Sleep(1000);
    PostMessageW(viewer.window, WM_KEYDOWN, VK_F11, 1);
    PostMessageW(viewer.window, WM_KEYUP, VK_F11, 0xc0000001);
}

static void open_folder(void)
{
    static const WCHAR explorer[] = L"C:\\xp\\xp-explorer.exe";
    WCHAR line[MAX_PATH + 40];
    ULONGLONG waited = GetTickCount64() - started;
    if (drive >= 0 && waited < SETTLE_MS) Sleep((DWORD)(SETTLE_MS - waited));
    /* XP's Explorer takes a quoted path as it is, with a backslash before the quote. */
    swprintf(line, ARRAYSIZE(line), L"\"%ls\" \"%ls\"", explorer, root);
    start_program(explorer, line);
}

/* A Linux program for FILE, through linux-launch.exe beside this program. */
static void linux_open(const WCHAR *file)
{
    typedef char *(CDECL *unix_file_name)(const WCHAR *);
    unix_file_name get = (unix_file_name)(void *)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name");
    char *name = get ? get(file) : NULL;
    WCHAR wide[MAX_PATH * 2], line[MAX_PATH * 3];
    /* 65010 is Wine's Unix code page. A quote cannot be passed this way. */
    if (name && MultiByteToWideChar(65010, 0, name, -1, wide, ARRAYSIZE(wide)) && !wcschr(wide, L'"'))
    {
        swprintf(line, ARRAYSIZE(line), L"\"C:\\xp\\bin\\linux-launch.exe\" xdg-open \"%ls\"", wide);
        start_program(L"C:\\xp\\bin\\linux-launch.exe", line);
    }
    if (name) HeapFree(GetProcessHeap(), 0, name);
}

static void perform(const ACTION *action)
{
    const WCHAR *file = found == VIDEO ? first_video : first_music;
    WCHAR line[MAX_PATH * 3], program[MAX_PATH];
    switch (action->kind)
    {
    case SLIDESHOW:
        /* XP's viewer, on the first picture; F11 starts its slideshow. */
        GetSystemDirectoryW(program, ARRAYSIZE(program));
        swprintf(line, ARRAYSIZE(line), L"rundll32.exe %ls\\shimgvw.dll,ImageView_Fullscreen %ls", program,
                 first_picture);
        wcscat(program, L"\\rundll32.exe");
        start_slideshow(start_program(program, line));
        break;
    case ASSOCIATED:
        ShellExecuteW(NULL, L"open", file, NULL, root, SW_SHOWNORMAL);
        break;
    case MEDIA_PLAYER:
        ExpandEnvironmentStringsW(L"%ProgramFiles%\\Windows Media Player\\wmplayer.exe", program, ARRAYSIZE(program));
        swprintf(line, ARRAYSIZE(line), L"\"%ls\" /Play \"%ls\"", program, file);
        start_program(program, line);
        break;
    case LINUX_PLAYER:
        linux_open(file);
        break;
    case OPEN_FOLDER:
        open_folder();
        break;
    case NOTHING:
        break;
    }
}

/* ---- XP's dialog --------------------------------------------------------- */

static void draw_action(const DRAWITEMSTRUCT *draw)
{
    const ACTION *action;
    BOOL selected = (draw->itemState & ODS_SELECTED) != 0;
    RECT text = draw->rcItem;
    int middle;
    if (draw->itemID >= (UINT)action_count) return;
    action = &actions[draw->itemID];
    FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    DrawIconEx(draw->hDC, draw->rcItem.left + 3, (draw->rcItem.top + draw->rcItem.bottom - 32) / 2, action->icon,
               32, 32, 0, NULL, DI_NORMAL);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    text.left += 42;
    middle = (text.top + text.bottom) / 2;
    if (action->provider[0])
    {
        RECT upper = text, lower = text;
        upper.bottom = lower.top = middle;
        DrawTextW(draw->hDC, action->action, -1, &upper, DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (!selected) SetTextColor(draw->hDC, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(draw->hDC, action->provider, -1, &lower, DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else DrawTextW(draw->hDC, action->action, -1, &text, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
}

static INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        WCHAR title[96], type[64] = L"";
        HWND list = GetDlgItem(dialog, ITEM_LIST), box;
        HFONT font = (HFONT)SendMessageW(dialog, WM_GETFONT, 0, 0);
        RECT place, screen, size;
        HICON icon = xp_icon(disc ? 12 : 8, 32);

        drive_name(title, ARRAYSIZE(title));
        SetWindowTextW(dialog, title);
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)xp_icon(disc ? 12 : 8, 16));
        SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendDlgItemMessageW(dialog, ITEM_DRIVE_ICON, STM_SETICON, (WPARAM)icon, 0);
        if (GetDlgItem(dialog, ITEM_TYPE_NAME))
        {
            const WCHAR *file = found == PICTURES ? first_picture : found == MUSIC ? first_music : first_video;
            SHFILEINFOW info = {0};
            if (found == PICTURES) xp_string(17161, L"Pictures", type, ARRAYSIZE(type));
            else if (found == MUSIC) xp_string(17162, L"Music files", type, ARRAYSIZE(type));
            else xp_string(17163, L"Video files", type, ARRAYSIZE(type));
            SetDlgItemTextW(dialog, ITEM_TYPE_NAME, type);
            if (SHGetFileInfoW(file, 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON))
                SendDlgItemMessageW(dialog, ITEM_TYPE_ICON, STM_SETICON, (WPARAM)info.hIcon, 0);
        }
        /* XP's list view shows the actions as tiles of two lines; here a list
         * box in its place draws them. */
        GetWindowRect(list, &place);
        MapWindowPoints(NULL, dialog, (POINT *)&place, 2);
        box = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                              LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT, place.left, place.top,
                              place.right - place.left, place.bottom - place.top, dialog, NULL,
                              GetModuleHandleW(NULL), NULL);
        SetWindowPos(box, list, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        DestroyWindow(list);
        SetWindowLongPtrW(box, GWLP_ID, ITEM_LIST);
        SendMessageW(box, WM_SETFONT, (WPARAM)font, FALSE);
        SendMessageW(box, LB_SETITEMHEIGHT, 0, 38);
        for (int i = 0; i < action_count; i++) SendMessageW(box, LB_ADDSTRING, 0, (LPARAM)actions[i].action);
        SendMessageW(box, LB_SETCURSEL, 0, 0);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &screen, 0);
        GetWindowRect(dialog, &size);
        SetWindowPos(dialog, HWND_TOP, (screen.left + screen.right - (size.right - size.left)) / 2,
                     (screen.top + screen.bottom - (size.bottom - size.top)) / 2, 0, 0, SWP_NOSIZE);
        SetForegroundWindow(dialog);
        SetFocus(box);
        return FALSE;
    }
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT *)lparam)->itemHeight = 38;
        return TRUE;
    case WM_DRAWITEM:
        if (wparam != ITEM_LIST && ((DRAWITEMSTRUCT *)lparam)->CtlType != ODT_LISTBOX) break;
        draw_action((const DRAWITEMSTRUCT *)lparam);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || (LOWORD(wparam) == ITEM_LIST && HIWORD(wparam) == LBN_DBLCLK))
        {
            chosen = (int)SendDlgItemMessageW(dialog, ITEM_LIST, LB_GETCURSEL, 0, 0);
            always = IsDlgButtonChecked(dialog, ITEM_ALWAYS) == BST_CHECKED;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL)
        {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_DEVICECHANGE:
    {
        /* The drive went, and the question with it. */
        const DEV_BROADCAST_VOLUME *volume = (const DEV_BROADCAST_VOLUME *)lparam;
        if (wparam == DBT_DEVICEREMOVECOMPLETE && drive >= 0 && volume &&
            volume->dbcv_devicetype == DBT_DEVTYP_VOLUME && (volume->dbcv_unitmask & (1u << drive)))
            PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
        return TRUE;
    }
    }
    return FALSE;
}

/* Asks which action to take; -1 for none. */
static int ask(void)
{
    int id = found == PICTURES || found == MUSIC || found == VIDEO ? DIALOG_CONTENT : DIALOG_MIXED;
    HRSRC resource = xp_shell ? FindResourceW(xp_shell, MAKEINTRESOURCEW(id), (LPCWSTR)RT_DIALOG) : NULL;
    const DLGTEMPLATE *template = resource ? LockResource(LoadResource(xp_shell, resource)) : NULL;
    if (!template) return -1;
    if (DialogBoxIndirectParamW(GetModuleHandleW(NULL), template, NULL, dialog_proc, 0) != IDOK) return -1;
    return chosen;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *unused, int show)
{
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    const WCHAR *event;
    WCHAR **argv, kept[64] = L"";
    DWORD size = sizeof(kept);
    int argc, action = -1;
    (void)instance; (void)previous; (void)unused; (void)show;

    started = GetTickCount64();
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2 || wcslen(argv[1]) >= MAX_PATH - 2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS);
    if (wcslen(argv[1]) <= 3 && argv[1][1] == L':')
    {
        drive = (argv[1][0] | 0x20) - L'a';
        if (drive < 0 || drive >= 26) return 2;
        swprintf(root, MAX_PATH, L"%c:\\", L'A' + drive);
        disc = GetDriveTypeW(root) == DRIVE_CDROM;
        stick = removable_medium();
    }
    else
    {
        wcscpy(root, argv[1]);
        if (root[wcslen(root) - 1] != L'\\') wcscat(root, L"\\");
        disc = argc > 2 && !wcscmp(argv[2], L"--disc");
        stick = !disc;
    }
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) return 0;
    if (!exists(root)) return 1;
    InitCommonControlsEx(&controls);
    xp_shell = LoadLibraryExW(L"C:\\xp\\native\\shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);

    if (disc && !stick && autorun()) return 0;
    scan(root, 1);
    /* A disc with nothing to play or show: its window, which is what mounting it was for. */
    if (disc && !stick && !(found & (PICTURES | MUSIC | VIDEO)))
    {
        open_folder();
        return 0;
    }
    build_actions();
    event = found == PICTURES ? L"ShowPicturesOnArrival" : found == MUSIC ? L"PlayMusicFilesOnArrival" :
            found == VIDEO ? L"PlayVideoFilesOnArrival" : L"MixedContentOnArrival";
    if (!RegGetValueW(HKEY_CURRENT_USER, CHOSEN, event, RRF_RT_REG_SZ, NULL, kept, &size))
        for (int i = 0; i < action_count; i++)
            if (!wcscmp(actions[i].handler, kept)) action = i;
    if (action < 0)
    {
        if ((action = ask()) < 0) return 0;
        if (always)
            RegSetKeyValueW(HKEY_CURRENT_USER, CHOSEN, event, REG_SZ, actions[action].handler,
                            (DWORD)(wcslen(actions[action].handler) + 1) * sizeof(WCHAR));
    }
    perform(&actions[action]);
    return 0;
}
