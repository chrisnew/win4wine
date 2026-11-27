/* Win32 identities and genuine nonclient frames for native X11 clients.
 * Uses public Win32 APIs only; X11 clients remain root children, paired below
 * their frame by the WM. No Microsoft image/code is copied into this helper. */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLIENTS 2048
struct client { unsigned long xid; HWND hwnd; int seen; int hidden; int active; int moving; int x, y, width, height;
    RECT workarea;
    HICON icon, small_icon; int icon_checked, icon_present; FILETIME icon_time; DWORD icon_size;
};
static struct client clients[MAX_CLIENTS];
/* WM_TIMER of a frame whose caption is painted again after an activation change. */
#define CAPTION_REPAINT_TIMER 0x574e
static HWND previous[MAX_CLIENTS];
static unsigned previous_count;
static WCHAR previous_titles[MAX_CLIENTS][512];
static WCHAR current_titles[MAX_CLIENTS][512];
static HWND current[MAX_CLIENTS];
static unsigned current_count;
static HWND last_foreground;
static WCHAR directory[MAX_PATH];
static UINT shell_message;
static HICON default_icon;
static int syncing;
static unsigned sequence;
static BOOL dirty = TRUE;
/* Only geometry.tsv changed, or a frame moved: frame_geometry alone. A whole
 * pass (windows.tsv, icons, every top-level window for the shell, the tray)
 * cost about 9 ms on an Atom N550, and a Linux window that moved itself
 * brought three or four of them in each step: 53% of a core in the proxy
 * and 44% in wineserver at 60 Hz. */
static BOOL geometry_dirty;
/* A file other than geometry.tsv changed: switch.tsv, shellkeys.tsv and
 * snapping.tsv are read again (each read is wineserver requests). */
static BOOL files_changed = TRUE;
static char frames_text[MAX_CLIENTS * 96];
static BOOL frames_written;

/* Commands for the WM end in .cmd, those for the tray host in .tray. */
static void write_file_command(const char *data, const WCHAR *suffix)
{
    WCHAR temp[MAX_PATH + 64], target[MAX_PATH + 64];
    fprintf(stderr, "bridge command: %s", data);
    DWORD written;
    HANDLE file;
    swprintf(temp, MAX_PATH + 64, L"%ls\\%lu-%u.tmp", directory, GetCurrentProcessId(), sequence);
    swprintf(target, MAX_PATH + 64, L"%ls\\%lu-%u.%ls", directory, GetCurrentProcessId(), sequence++, suffix);
    file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, data, (DWORD)strlen(data), &written, NULL);
    CloseHandle(file);
    if (!MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING)) DeleteFileW(temp);
}

static void write_command(const char *data)
{
    write_file_command(data, L"cmd");
}

static void command(const char *op, unsigned long xid)
{
    char data[80];
    snprintf(data, sizeof(data), "%s %lx\n", op, xid);
    write_command(data);
}

/* The X11 WM moves a native client with its frame itself (wm.py's
 * follow_frame); labwc does not. */
static BOOL wm_follows_frames;

static struct client *find_client(unsigned long xid)
{
    unsigned i;
    for (i = 0; i < MAX_CLIENTS; ++i) if (clients[i].xid == xid) return &clients[i];
    return NULL;
}

static void geometry_command(HWND hwnd, unsigned long xid)
{
    RECT r;
    POINT pt = {0, 0};
    char data[160];
    GetClientRect(hwnd, &r);
    ClientToScreen(hwnd, &pt);
    snprintf(data, sizeof(data), "geometry %lx %ld %ld %ld %ld\n", xid,
             pt.x, pt.y, r.right, r.bottom);
    write_command(data);
}

/* Whether frame C's client area is where geometry.tsv last put its client. */
static BOOL frame_at_client(const struct client *c)
{
    RECT r;
    POINT pt = {0, 0};
    if (!c || !c->width || !c->height) return FALSE;
    GetClientRect(c->hwnd, &r);
    ClientToScreen(c->hwnd, &pt);
    return pt.x == c->x && pt.y == c->y && r.right == c->width && r.bottom == c->height;
}

/* Whether Wine's move loop drags this window now (Wine 0038: "Win4WineSizeMove"
 * without SIZE_MOVE_ENDED; see window snapping below). */
static BOOL snap_moving(HWND hwnd)
{
    UINT_PTR state = (UINT_PTR)GetPropW(hwnd, L"Win4WineSizeMove");
    return state && !(state & 0x1000);
}

/* Keep diagnostic readback usable with unpatched Wine10 too. This read-only
 * scalar query performs public icon/readback APIs in their owning process. */
static LRESULT icon_readback(HWND hwnd, WPARAM field)
{
    HICON icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    ICONINFO info;
    BITMAP bitmap;
    HDC dc;
    HBITMAP image, previous;
    RECT rect;
    DWORD hash = 2166136261u, first = 0, center = 0, last = 0;
    int x, y;
    if (field == 0) return (LRESULT)icon;
    if (field > 5) return 0;
    if (!icon || !GetIconInfo(icon, &info)) return 0;
    if (!GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap))
    {
        if (info.hbmColor) DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
        return 0;
    }
    rect.left = rect.top = 0;
    rect.right = bitmap.bmWidth;
    rect.bottom = bitmap.bmHeight / (info.hbmColor ? 1 : 2);
    if (rect.right <= 0 || rect.right > 256 || rect.bottom <= 0 || rect.bottom > 256)
    {
        if (info.hbmColor) DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
        return 0;
    }
    dc = CreateCompatibleDC(NULL);
    {
        HDC screen = GetDC(NULL);
        image = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
        ReleaseDC(NULL, screen);
    }
    previous = SelectObject(dc, image);
    FillRect(dc, &rect, GetStockObject(WHITE_BRUSH));
    DrawIconEx(dc, 0, 0, icon, rect.right, rect.bottom, 0, NULL, DI_NORMAL);
    for (y = 0; y < rect.bottom; ++y) for (x = 0; x < rect.right; ++x)
    {
        COLORREF color = GetPixel(dc, x, y);
        DWORD rgb = GetRValue(color) << 16 | GetGValue(color) << 8 | GetBValue(color);
        hash = (hash ^ rgb) * 16777619u;
        if (!x && !y) first = rgb;
        if (x == rect.right / 2 && y == rect.bottom / 2) center = rgb;
        last = rgb;
    }
    SelectObject(dc, previous); DeleteObject(image); DeleteDC(dc);
    if (info.hbmColor) DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    if (field == 1) return MAKELONG(rect.right, rect.bottom);
    if (field == 2) return hash;
    if (field == 3) return first;
    if (field == 4) return center;
    if (field == 5) return last;
    return 0;
}

static LRESULT CALLBACK proxy_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    unsigned long xid = (unsigned long)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_APP + 0x574 && lp == 0x57344931) return icon_readback(hwnd, wp);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    if (msg == WM_GETMINMAXINFO && xid)
    {
        unsigned i;
        for (i = 0; i < MAX_CLIENTS; ++i)
            if (clients[i].xid == xid && clients[i].workarea.right > clients[i].workarea.left)
            {
                MINMAXINFO *limits = (MINMAXINFO *)lp;
                MONITORINFO monitor = {.cbSize = sizeof(monitor)};
                RECT area = clients[i].workarea, borders = {0, 0, 0, 0};
                LRESULT result = DefWindowProcW(hwnd, msg, wp, lp);
                /* The borders come from the style, not GetWindowInfo: a
                 * minimized frame restored to maximized asks while it still
                 * has its minimized rectangles, whose empty client area gave
                 * borders of 0 and 31 (the caption ended up off screen). */
                if (!AdjustWindowRectEx(&borders, GetWindowLongW(hwnd, GWL_STYLE) & ~(WS_MINIMIZE | WS_MAXIMIZE),
                                        FALSE, GetWindowLongW(hwnd, GWL_EXSTYLE)) ||
                    !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor)) return result;
                limits->ptMaxPosition.x = area.left - monitor.rcMonitor.left + borders.left;
                limits->ptMaxPosition.y = area.top - monitor.rcMonitor.top - borders.bottom;
                limits->ptMaxSize.x = area.right - area.left - 2 * borders.left;
                limits->ptMaxSize.y = area.bottom - area.top + 2 * borders.bottom;
                limits->ptMaxTrackSize.x = max(limits->ptMaxTrackSize.x, limits->ptMaxSize.x);
                limits->ptMaxTrackSize.y = max(limits->ptMaxTrackSize.y, limits->ptMaxSize.y);
                return result;
            }
    }
    /* While its client has the X focus the frame stays active: a Win32
     * activation of another process that arrives after the WM's report (a
     * window the user had activated just before) deactivates this frame. */
    if (msg == WM_NCACTIVATE && !wp && xid)
    {
        unsigned i;
        for (i = 0; i < MAX_CLIENTS; ++i)
            if (clients[i].xid == xid && clients[i].active) return DefWindowProcW(hwnd, msg, TRUE, lp);
    }
    if (msg == WM_TIMER && wp == CAPTION_REPAINT_TIMER)
    {
        KillTimer(hwnd, CAPTION_REPAINT_TIMER);
        RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        return 0;
    }
    /* Border metrics written to frames.tsv follow theme and settings changes. */
    if (msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE || msg == WM_DISPLAYCHANGE) dirty = TRUE;
    if (!syncing && xid)
    {
        if (msg == WM_WINDOWPOSCHANGED) geometry_dirty = TRUE;
        if (msg == WM_CLOSE) { command("close", xid); return 0; }
        if (msg == WM_SYSCOMMAND)
        {
            if ((wp & 0xfff0) == SC_MINIMIZE) { command("minimize", xid); return 0; }
            if ((wp & 0xfff0) == SC_MAXIMIZE)
            { command(GetPropW(hwnd, L"Win4WineMaximized") ? "restore" : "maximize", xid); return 0; }
            if ((wp & 0xfff0) == SC_RESTORE && snap_moving(hwnd))
            {
                /* Window snapping: Wine's move loop restores a maximized
                 * window dragged by its caption, and moves it on at once. */
                command("restore", xid);
                RemovePropW(hwnd, L"Win4WineMaximized");
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            if ((wp & 0xfff0) == SC_RESTORE)
            { command(IsIconic(hwnd) ? "activate" : "restore", xid); return 0; }
        }
        /* While Wine's move loop drags the frame, the WM keeps the client
         * with it; the final rectangle is still sent, for clients that the
         * WM could not follow (one maximized when the drag began). */
        struct client *c = find_client(xid);
        if (msg == WM_ENTERSIZEMOVE && wm_follows_frames && c) c->moving = 1;
        if (msg == WM_EXITSIZEMOVE && c && c->moving)
        {
            c->moving = 0;
            if (!IsIconic(hwnd)) geometry_command(hwnd, xid);
        }
        /* Not for the rectangle the WM last reported: the WM moved the frame
         * with its client (follow_native), and already has it. */
        if (msg == WM_WINDOWPOSCHANGED && !IsIconic(hwnd) && !(c && c->moving) && !frame_at_client(c))
            geometry_command(hwnd, xid);
        if (msg == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE && !IsIconic(hwnd)) command("activate", xid);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* The windows that take shell hook messages (Wine 0026 marks them). Finding
 * them asks wineserver about every window, a request each, and a window that
 * opens sends several messages: about 40 ms each on an Atom N550. They are
 * looked for again when one has lost its mark (the shell restarted), and at
 * most every two seconds otherwise, for a shell that registers later. */
#define MAX_SHELL_HOOKS 8
static HWND shell_hooks[MAX_SHELL_HOOKS];
static unsigned shell_hook_count;
static DWORD shell_hooks_found;

static BOOL CALLBACK find_hook(HWND hwnd, LPARAM unused)
{
    (void)unused;
    if (shell_hook_count < MAX_SHELL_HOOKS && GetPropW(hwnd, L"Win4WineShellHook"))
        shell_hooks[shell_hook_count++] = hwnd;
    return TRUE;
}

static BOOL CALLBACK find_hook_tree(HWND hwnd, LPARAM unused)
{
    find_hook(hwnd, unused);
    /* XP registers its child task-switcher window, not the taskbar parent. */
    EnumChildWindows(hwnd, find_hook, unused);
    return TRUE;
}

static void notify(UINT event, HWND window)
{
    unsigned i;
    BOOL stale = !shell_hook_count || GetTickCount() - shell_hooks_found > 2000;
    for (i = 0; i < shell_hook_count && !stale; ++i)
        stale = !GetPropW(shell_hooks[i], L"Win4WineShellHook");
    if (stale)
    {
        shell_hook_count = 0;
        EnumWindows(find_hook_tree, 0);
        shell_hooks_found = GetTickCount();
    }
    for (i = 0; i < shell_hook_count; ++i) PostMessageW(shell_hooks[i], shell_message, event, (LPARAM)window);
}

/* The WM writes shellkeys.tsv when the Windows key or Ctrl+Esc is pressed
 * under a native client: "NUMBER windows VK..." with the keys pressed while
 * the Windows key was down, or "NUMBER ctrl-escape". Enter them into Wine as
 * the keyboard would; Wine's foreground then handles XP's hotkeys and the
 * Start menu keys as for its own windows. */
static void key_input(INPUT *input, WORD vk, BOOL up)
{
    memset(input, 0, sizeof(*input));
    input->type = INPUT_KEYBOARD;
    input->ki.wVk = vk;
    input->ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input->ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_LWIN) input->ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
}

static void shell_key_events(BOOL start)
{
    static unsigned seen;
    WCHAR path[MAX_PATH + 32];
    char kind[16] = "";
    unsigned number = 0, vk, count = 0;
    INPUT inputs[2 + 2 * 16 + 2];
    FILE *file;
    swprintf(path, MAX_PATH + 32, L"%ls\\shellkeys.tsv", directory);
    if (!(file = _wfopen(path, L"rb"))) return;
    if (fscanf(file, "%u %15s", &number, kind) != 2) number = seen;
    if (start || number == seen)
    {
        seen = number;
        fclose(file);
        return;
    }
    seen = number;
    if (!strcmp(kind, "ctrl-escape"))
    {
        key_input(&inputs[count++], VK_CONTROL, FALSE);
        key_input(&inputs[count++], VK_ESCAPE, FALSE);
        key_input(&inputs[count++], VK_ESCAPE, TRUE);
        key_input(&inputs[count++], VK_CONTROL, TRUE);
    }
    else if (!strcmp(kind, "windows"))
    {
        key_input(&inputs[count++], VK_LWIN, FALSE);
        while (count < ARRAYSIZE(inputs) - 3 && fscanf(file, "%x", &vk) == 1 && vk && vk < 0xff)
        {
            key_input(&inputs[count++], (WORD)vk, FALSE);
            key_input(&inputs[count++], (WORD)vk, TRUE);
        }
        key_input(&inputs[count++], VK_LWIN, TRUE);
    }
    fclose(file);
    if (count) SendInput(count, inputs, sizeof(INPUT));
}

/* Top-level windows by their last activation, newest first: Alt+Tab's
 * order. On Windows activating a window raises it, so the Z order is this
 * order; here a Linux window's frame is not raised in Win32 when its window
 * is activated, so the proxy keeps the order itself. */
static HWND recent[MAX_CLIENTS * 4];
static unsigned recent_count;

static void note_recent(HWND hwnd)
{
    unsigned i;
    if (!hwnd) return;
    for (i = 0; i < recent_count && recent[i] != hwnd; ++i) {}
    if (i == recent_count && recent_count < ARRAYSIZE(recent)) recent_count++;
    if (i == ARRAYSIZE(recent)) i = recent_count - 1;
    memmove(recent + 1, recent, i * sizeof(HWND));
    recent[0] = hwnd;
}

static unsigned recent_rank(HWND hwnd)
{
    unsigned i;
    for (i = 0; i < recent_count; ++i) if (recent[i] == hwnd) return i;
    return ARRAYSIZE(recent);
}

static BOOL CALLBACK collect(HWND hwnd, LPARAM unused)
{
    WCHAR name[80];
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    (void)unused;
    if (!IsWindowVisible(hwnd) || ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW))) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return TRUE;
    GetClassNameW(hwnd, name, 80);
    if (!wcsicmp(name, L"Progman") || !wcsicmp(name, L"Shell_TrayWnd") || !wcsicmp(name, L"WorkerW") || !wcsicmp(name, L"DV2ControlHost")) return TRUE;
    if (current_count < MAX_CLIENTS)
    {
        current[current_count] = hwnd;
        GetWindowTextW(hwnd, current_titles[current_count], 512);
        current_count++;
    }
    return TRUE;
}

static int contains(HWND *list, unsigned count, HWND hwnd)
{
    unsigned i;
    for (i = 0; i < count; ++i) if (list[i] == hwnd) return 1;
    return 0;
}

static void shell_events(void)
{
    unsigned i;
    HWND foreground;
    current_count = 0;
    EnumWindows(collect, 0);
    for (i = 0; i < current_count; ++i)
    {
        unsigned j;
        for (j = 0; j < previous_count && previous[j] != current[i]; ++j) {}
        if (j == previous_count) notify(HSHELL_WINDOWCREATED, current[i]);
        else if (wcscmp(previous_titles[j], current_titles[i])) notify(HSHELL_REDRAW, current[i]);
    }
    for (i = 0; i < previous_count; ++i)
        if (!contains(current, current_count, previous[i])) notify(HSHELL_WINDOWDESTROYED, previous[i]);
    foreground = GetForegroundWindow();
    /* A native client with X focus is the active task, whether or not its
     * proxy could take the Win32 foreground. */
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i].xid && clients[i].active && contains(current, current_count, clients[i].hwnd))
            foreground = clients[i].hwnd;
    if (foreground && !contains(current, current_count, foreground))
    {
        /* An activation can create an XP task too. Only activate identities
         * included in this snapshot; owned dialogs highlight their app owner. */
        HWND owner = foreground;
        unsigned depth;
        for (depth = 0; owner && depth < MAX_CLIENTS && !contains(current, current_count, owner); ++depth)
            owner = GetWindow(owner, GW_OWNER);
        foreground = contains(current, current_count, owner) ? owner : NULL;
    }
    if (foreground != last_foreground)
    {
        note_recent(foreground);
        notify(HSHELL_WINDOWACTIVATED, foreground);
        last_foreground = foreground;
    }
    memcpy(previous, current, current_count * sizeof(HWND));
    memcpy(previous_titles, current_titles, current_count * sizeof(current_titles[0]));
    previous_count = current_count;
}

/* The WM exports bounded, little-endian ARGB pixels from public X11 icon
 * properties. These are client-owned images, never bundled theme assets. */
static HICON read_icon(const WCHAR *path, DWORD expected_size)
{
    HANDLE file;
    DWORD header[3], count, bytes, i, width, height, stride;
    BITMAPINFO color_info = {0};
    struct { BITMAPINFOHEADER header; RGBQUAD colors[2]; } mask_info = {0};
    HBITMAP color = NULL, mask = NULL;
    DWORD *pixels;
    BYTE *mask_bits;
    ICONINFO info = {0};
    HICON icon = NULL;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (!ReadFile(file, header, sizeof(header), &bytes, NULL) || bytes != sizeof(header) ||
        memcmp(header, "W4I1", 4)) goto done;
    width = header[1]; height = header[2];
    if (!width || !height || width > 256 || height > 256) goto done;
    count = width * height;
    if (expected_size != sizeof(header) + count * sizeof(DWORD)) goto done;
    color_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    color_info.bmiHeader.biWidth = width;
    color_info.bmiHeader.biHeight = -(LONG)height;
    color_info.bmiHeader.biPlanes = 1;
    color_info.bmiHeader.biBitCount = 32;
    color_info.bmiHeader.biCompression = BI_RGB;
    color = CreateDIBSection(NULL, &color_info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (!color || !ReadFile(file, pixels, count * sizeof(DWORD), &bytes, NULL) ||
        bytes != count * sizeof(DWORD)) goto done;
    mask_info.header = color_info.bmiHeader;
    mask_info.header.biBitCount = 1;
    mask_info.colors[1].rgbRed = mask_info.colors[1].rgbGreen = mask_info.colors[1].rgbBlue = 255;
    mask = CreateDIBSection(NULL, (BITMAPINFO *)&mask_info, DIB_RGB_COLORS, (void **)&mask_bits, NULL, 0);
    if (!mask) goto done;
    stride = (width + 31) / 32 * 4;
    memset(mask_bits, 0, stride * height);
    for (i = 0; i < count; ++i)
        if (!(pixels[i] >> 24)) mask_bits[(i / width) * stride + (i % width) / 8] |= 0x80 >> (i % width % 8);
    info.fIcon = TRUE; info.hbmColor = color; info.hbmMask = mask;
    icon = CreateIconIndirect(&info);
 done:
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    CloseHandle(file);
    return icon;
}

static void client_icon(struct client *c)
{
    WCHAR path[MAX_PATH + 40];
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    HICON icon = NULL, small_icon = NULL, previous, previous_small;
    int present;
    swprintf(path, MAX_PATH + 40, L"%ls\\icon-%lx.argb", directory, c->xid);
    present = GetFileAttributesExW(path, GetFileExInfoStandard, &attrs) &&
              !(attrs.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    if (c->icon_checked && present == c->icon_present &&
        (!present || (!CompareFileTime(&attrs.ftLastWriteTime, &c->icon_time) &&
                      attrs.nFileSizeLow == c->icon_size && !attrs.nFileSizeHigh))) return;
    c->icon_checked = 1; c->icon_present = present;
    if (present)
    {
        c->icon_time = attrs.ftLastWriteTime; c->icon_size = attrs.nFileSizeLow;
        if (!attrs.nFileSizeHigh && attrs.nFileSizeLow <= 12 + 256 * 256 * 4)
            icon = read_icon(path, attrs.nFileSizeLow);
        /* The caption's 16x16 image, written before the big one. */
        swprintf(path, MAX_PATH + 40, L"%ls\\icon-%lx-small.argb", directory, c->xid);
        if (icon && GetFileAttributesExW(path, GetFileExInfoStandard, &attrs) &&
            !(attrs.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            !attrs.nFileSizeHigh && attrs.nFileSizeLow <= 12 + 256 * 256 * 4)
            small_icon = read_icon(path, attrs.nFileSizeLow);
    }
    previous = c->icon;
    previous_small = c->small_icon;
    c->icon = icon;
    c->small_icon = small_icon;
    SendMessageW(c->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(small_icon ? small_icon : icon ? icon : default_icon));
    SendMessageW(c->hwnd, WM_SETICON, ICON_BIG, (LPARAM)(icon ? icon : default_icon));
    /* IDI_APPLICATION is shared and never owned. */
    if (previous) DestroyIcon(previous);
    if (previous_small) DestroyIcon(previous_small);
    RedrawWindow(c->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    notify(HSHELL_REDRAW, c->hwnd);
}

static void place_frame(struct client *c)
{
    RECT outer, inner, wanted;
    POINT origin = {0, 0};
    GetWindowRect(c->hwnd, &outer);
    GetClientRect(c->hwnd, &inner);
    ClientToScreen(c->hwnd, &origin);
    wanted.left = c->x - (origin.x - outer.left);
    wanted.top = c->y - (origin.y - outer.top);
    wanted.right = wanted.left + c->width + (outer.right - outer.left - inner.right);
    wanted.bottom = wanted.top + c->height + (outer.bottom - outer.top - inner.bottom);
    if (EqualRect(&wanted, &outer)) return;
    SetWindowPos(c->hwnd, NULL, wanted.left, wanted.top, wanted.right - wanted.left,
                 wanted.bottom - wanted.top, SWP_NOACTIVATE | SWP_NOZORDER);
}

/* A line of geometry.tsv: "XID x y width height maximized" and the work area. */
static BOOL geometry_line(const char *line, unsigned long *xid, long long *x, long long *y, long long *width,
                          long long *height, long long *maximized, RECT *workarea)
{
    long long ax, ay, aw, ah;
    if (sscanf(line, "%lx %lld %lld %lld %lld %lld %lld %lld %lld %lld", xid, x, y, width, height,
               maximized, &ax, &ay, &aw, &ah) != 10 || *width <= 0 || *height <= 0 ||
        *width > 65535 || *height > 65535 || aw <= 0 || ah <= 0 || aw > 65535 || ah > 65535 ||
        *x < -65535 || *x > 65535 || *y < -65535 || *y > 65535 ||
        ax < -65535 || ax > 65535 || ay < -65535 || ay > 65535 ||
        (*maximized != 0 && *maximized != 1)) return FALSE;
    SetRect(workarea, ax, ay, ax + aw, ay + ah);
    return TRUE;
}

/* A new frame goes around its client before it shows: placed once shown, it
 * was painted twice, at its first place and size and again at the client's. */
static void place_new_frame(struct client *c)
{
    WCHAR path[MAX_PATH + 32];
    FILE *file;
    unsigned long xid;
    long long x, y, width, height, maximized;
    char line[240];
    RECT workarea;
    swprintf(path, MAX_PATH + 32, L"%ls\\geometry.tsv", directory);
    if (!(file = _wfopen(path, L"rb"))) return;
    while (fgets(line, sizeof(line), file))
        if (geometry_line(line, &xid, &x, &y, &width, &height, &maximized, &workarea) && xid == c->xid)
        {
            c->workarea = workarea;
            c->x = x; c->y = y; c->width = width; c->height = height;
            if (!maximized) place_frame(c);
            break;
        }
    fclose(file);
}

static void frame_geometry(void)
{
    WCHAR path[MAX_PATH + 32], target[MAX_PATH + 32];
    FILE *file;
    unsigned long xid;
    long long x, y, width, height, maximized;
    char line[240];
    RECT workarea;
    unsigned i;
    swprintf(path, MAX_PATH + 32, L"%ls\\geometry.tsv", directory);
    file = _wfopen(path, L"rb");
    if (!file) return;
    while (fgets(line, sizeof(line), file))
    {
        if (!geometry_line(line, &xid, &x, &y, &width, &height, &maximized, &workarea)) continue;
        for (i = 0; i < MAX_CLIENTS; ++i)
            if (clients[i].xid == xid && !clients[i].hidden)
            {
                struct client *c = &clients[i];
                c->workarea = workarea;
                if (!!GetPropW(c->hwnd, L"Win4WineMaximized") != !!maximized)
                {
                    if (maximized) SetPropW(c->hwnd, L"Win4WineMaximized", (HANDLE)1);
                    else RemovePropW(c->hwnd, L"Win4WineMaximized");
                    RedrawWindow(c->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
                }
                /* (not while a drag restores it: the WM has not had "restore" yet) */
                if (!!IsZoomed(c->hwnd) != !!maximized && !snap_moving(c->hwnd))
                    ShowWindow(c->hwnd, maximized ? SW_MAXIMIZE : SW_RESTORE);
                c->x = x; c->y = y; c->width = width; c->height = height;
                /* Wine sizes a maximized frame itself (Wine 0031) and the WM
                 * fits the client into it: placing the frame around the
                 * client's rectangle of the moment made the frame jump
                 * between that and its maximized size while it maximized. */
                if (!(maximized && IsZoomed(c->hwnd))) place_frame(c);
                break;
            }
    }
    fclose(file);
    {
        static char text[sizeof(frames_text)];
        size_t length = 0;
        text[0] = 0;
        for (i = 0; i < MAX_CLIENTS; ++i)
            if (clients[i].xid && length < sizeof(text))
            {
                RECT outer, inner;
                WINDOWINFO info = {.cbSize = sizeof(info)};
                POINT origin = {0, 0};
                GetWindowRect(clients[i].hwnd, &outer);
                GetClientRect(clients[i].hwnd, &inner);
                ClientToScreen(clients[i].hwnd, &origin);
                GetWindowInfo(clients[i].hwnd, &info);
                length += snprintf(text + length, sizeof(text) - length, "%lx %lx %ld %ld %ld %ld %d %d %u %u\n",
                        (unsigned long)(uintptr_t)clients[i].hwnd,
                        clients[i].xid, origin.x-outer.left, outer.right-origin.x-inner.right,
                        origin.y-outer.top, outer.bottom-origin.y-inner.bottom, !!GetPropW(clients[i].hwnd, L"Win4WineMaximized"), !!IsIconic(clients[i].hwnd), info.cxWindowBorders, info.cyWindowBorders);
            }
        /* Rewriting unchanged pairs would wake the WM for nothing. */
        if (frames_written && !strcmp(text, frames_text)) return;
        swprintf(path, MAX_PATH + 32, L"%ls\\frames.tmp", directory);
        swprintf(target, MAX_PATH + 32, L"%ls\\frames.tsv", directory);
        if (!(file = _wfopen(path, L"wb"))) return;
        fputs(text, file);
        fclose(file);
        if (MoveFileExW(path, target, MOVEFILE_REPLACE_EXISTING))
        {
            strcpy(frames_text, text);
            frames_written = TRUE;
        }
    }
}

/* The owners of owned Linux windows (dialogs), from owners.tsv: XID TAB OWNER. */
static struct { unsigned long xid, owner; } owners[MAX_CLIENTS];
static unsigned owner_count;

static void read_owners(void)
{
    WCHAR path[MAX_PATH + 32];
    char line[64];
    FILE *file;
    owner_count = 0;
    swprintf(path, MAX_PATH + 32, L"%ls\\owners.tsv", directory);
    if (!(file = _wfopen(path, L"rb"))) return;
    while (owner_count < MAX_CLIENTS && fgets(line, sizeof(line), file))
        if (sscanf(line, "%lx\t%lx", &owners[owner_count].xid, &owners[owner_count].owner) == 2) owner_count++;
    fclose(file);
}

static unsigned long owner_of(unsigned long xid)
{
    unsigned i;
    for (i = 0; i < owner_count; ++i) if (owners[i].xid == xid) return owners[i].owner;
    return 0;
}

/* SetForegroundWindow for the proxy, which gets no input of its own: the
 * Linux windows and the WM get it. Wine's server, like Windows, refuses the
 * foreground to a process that is not in the foreground and had no input
 * since the foreground process had some; then the frame of the Linux window
 * the user switched to stayed inactive in Win32, and the next Alt+Tab
 * started from the wrong window. Sharing the foreground thread's input
 * while asking, as Windows programs do, passes that check. */
static void make_foreground(HWND hwnd)
{
    HWND foreground = GetForegroundWindow();
    DWORD self = GetCurrentThreadId(), other = foreground ? GetWindowThreadProcessId(foreground, NULL) : 0;
    BOOL attached = other && other != self && AttachThreadInput(self, other, TRUE);
    note_recent(hwnd);
    SetForegroundWindow(hwnd);
    if (attached) AttachThreadInput(self, other, FALSE);
}

/* The window XP activates when the active one goes: the topmost visible,
 * enabled window that can be activated, other than the frames that close;
 * without one, the desktop (Progman), never the taskbar. */
struct next_active { HWND found; };

static BOOL closing_frame(HWND hwnd)
{
    unsigned i;
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i].xid && !clients[i].seen && clients[i].hwnd == hwnd) return TRUE;
    return FALSE;
}

static BOOL CALLBACK next_active_candidate(HWND hwnd, LPARAM param)
{
    struct next_active *next = (struct next_active *)param;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE), ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    WCHAR cls[32];
    if (!(style & WS_VISIBLE) || (style & (WS_DISABLED | WS_MINIMIZE)) || (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
        return TRUE;
    if (closing_frame(hwnd)) return TRUE;
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) &&
        (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Progman") || !wcscmp(cls, L"Win4WineSwitch")))
        return TRUE;
    next->found = hwnd;
    return FALSE;
}

/* A Linux window that had the keyboard was closed: its frame is still the
 * foreground window. Destroying it would have Wine activate the window
 * below the frame in Win32's order, often the desktop, while the WM gives
 * the X focus to the next window: the desktop's activation came last and
 * no window had the keyboard. Hand the foreground over first, as XP does. */
static void pass_foreground(void)
{
    struct next_active next = {0};
    HWND foreground = GetForegroundWindow();
    if (!foreground || !closing_frame(foreground)) return;
    EnumWindows(next_active_candidate, (LPARAM)&next);
    if (!next.found) next.found = FindWindowW(L"Progman", NULL);
    if (next.found) SetForegroundWindow(next.found);
}

static void snapshot(void)
{
    WCHAR path[MAX_PATH + 32];
    char line[4096];
    FILE *file;
    unsigned i, pass, activations = 0;
    struct client *activated[MAX_CLIENTS];
    swprintf(path, MAX_PATH + 32, L"%ls\\windows.tsv", directory);
    file = _wfopen(path, L"rb");
    if (!file) return; /* A missing snapshot is not an empty client list. */
    read_owners();
    for (i = 0; i < MAX_CLIENTS; ++i) clients[i].seen = 0;
    syncing = 1;
    /* Owned windows after their owners, whose frames own theirs: pass 0 takes
     * the others, the next ones each a level of dialogs, and the last the
     * owned windows whose owner has no frame (they get one of their own). */
    for (pass = 0; pass < 4; ++pass)
    {
        rewind(file);
        while (fgets(line, sizeof(line), file))
        {
            unsigned long xid, owner;
            int hidden, active, offset = 0;
            WCHAR title[1024];
            struct client *c = NULL, *owner_frame;
            if (sscanf(line, "%lx\t%d\t%d\t%n", &xid, &hidden, &active, &offset) != 3 || !offset) continue;
            owner = owner_of(xid);
            if (!pass != !owner) continue;
            owner_frame = owner ? find_client(owner) : NULL;
            if (owner_frame && !owner_frame->seen) owner_frame = NULL;
            if (pass && ((c = find_client(xid)) && c->seen)) continue;
            if (pass && pass < 3 && !owner_frame) continue;
            c = NULL;
            line[strcspn(line, "\r\n")] = 0;
            if (!MultiByteToWideChar(CP_UTF8, 0, line + offset, -1, title, 1024)) continue;
            for (i = 0; i < MAX_CLIENTS; ++i) if (clients[i].xid == xid) { c = &clients[i]; break; }
            if (!c) for (i = 0; i < MAX_CLIENTS; ++i) if (!clients[i].xid) { c = &clients[i]; break; }
            if (!c) continue;
            if (!c->xid)
            {
                c->xid = xid;
                /* As an XP dialog: owned by its owner's frame, so without a
                 * taskbar button, and with only a close button. */
                if (owner_frame)
                    c->hwnd = CreateWindowExW(0, L"Win4WineProxy", title, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
                        -32000, -32000, 1, 1, owner_frame->hwnd, NULL, GetModuleHandleW(NULL), (void *)(uintptr_t)xid);
                else
                    c->hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"Win4WineProxy", title, WS_OVERLAPPEDWINDOW,
                        -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), (void *)(uintptr_t)xid);
                if (!c->hwnd) { c->xid = 0; continue; }
                client_icon(c);
                place_new_frame(c);
                ShowWindow(c->hwnd, SW_SHOWNOACTIVATE);
            }
            c->seen = 1;
            client_icon(c);
            WCHAR old[1024];
            GetWindowTextW(c->hwnd, old, 1024);
            if (wcscmp(old, title)) { SetWindowTextW(c->hwnd, title); notify(HSHELL_REDRAW, c->hwnd); }
            if (hidden == c->hidden && !!IsIconic(c->hwnd) != c->hidden)
            {
                command(IsIconic(c->hwnd) ? "minimize" : "activate", xid);
                continue; /* Wait for X11 to acknowledge a direct ShowWindow operation. */
            }
            c->hidden = hidden;
            if (hidden && !IsIconic(c->hwnd)) ShowWindow(c->hwnd, SW_SHOWMINNOACTIVE);
            else if (!hidden && IsIconic(c->hwnd)) ShowWindow(c->hwnd, SW_SHOWNOACTIVATE);
            if (active != c->active)
            {
                c->active = active; /* before WM_NCACTIVATE, which proxy_proc checks */
                activated[activations++] = c;
            }
        }
    }
    fclose(file);
    pass_foreground();
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i].xid && !clients[i].seen)
        {
            DestroyWindow(clients[i].hwnd);
            if (clients[i].icon) DestroyIcon(clients[i].icon);
            if (clients[i].small_icon) DestroyIcon(clients[i].small_icon);
            memset(&clients[i], 0, sizeof(clients[i]));
        }
    /* The frames first: the WM shows a new client's frame once frames.tsv
     * pairs them, and an activation can wait for the shell. */
    frame_geometry();
    for (i = 0; i < activations; ++i)
    {
        /* The frame looks active while its client has the X focus. Win32
         * activation could not always follow (Wine refuses the foreground
         * to a process without the last input; make_foreground works
         * around it). The frame's caption is set directly, both ways;
         * it would otherwise keep the look of its last real activation.
         * SetForegroundWindow sends the shell its activation messages and
         * waits for them, while XP Explorer adds the new taskbar button. */
        struct client *c = activated[i];
        if (!c->xid) continue; /* gone in this pass */
        if (c->active) make_foreground(c->hwnd);
        SendMessageW(c->hwnd, WM_NCACTIVATE, c->active, 0);
        /* Activations of other processes' windows around this one
         * sometimes leave its caption painted in the previous state,
         * though WM_NCACTIVATE's state is right (seen in the classic
         * theme; a repaint shows it). Paint the caption again once
         * they are over. */
        SetTimer(c->hwnd, CAPTION_REPAINT_TIMER, 250, NULL);
    }
    syncing = 0;
}

static HWND previous_tray;
static DWORD previous_pid;
static BOOL attempted;

/* shell_layout runs on a 200 ms timer only while a visible taskbar awaits it. */
static BOOL shell_layout_pending(void)
{
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    DWORD pid = 0;
    if (!tray || !IsWindowVisible(tray)) return FALSE;
    GetWindowThreadProcessId(tray, &pid);
    return !attempted || tray != previous_tray || pid != previous_pid;
}

/* Explorer's initial WM_SIZE can run before its child layout is ready.
 * Synchronize once after the mapped taskbar and its controls settle. The shell
 * decides all child sizes; this message carries only its current client area. */
static void shell_layout(void)
{
    static HWND previous_start, previous_rebar;
    static RECT previous_rect, previous_start_rect, previous_rebar_rect;
    static BOOL stable;
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL), start, rebar;
    DWORD pid = 0;
    RECT rect, start_rect, rebar_rect;
    DWORD_PTR result;
    if (tray) GetWindowThreadProcessId(tray, &pid);
    if (tray != previous_tray || pid != previous_pid)
    {
        previous_tray = tray;
        previous_pid = pid;
        previous_start = previous_rebar = NULL;
        stable = attempted = FALSE;
    }
    if (!tray || attempted) return;
    /* A temporarily hidden taskbar keeps its completed startup state. */
    if (!IsWindowVisible(tray)) { stable = FALSE; return; }
    start = FindWindowExW(tray, NULL, L"Win4WineStartButton", NULL);
    if (!start) start = FindWindowExW(tray, NULL, L"Button", NULL);
    rebar = FindWindowExW(tray, NULL, L"ReBarWindow32", NULL);
    if (!start || !rebar || !IsWindowVisible(start) || !IsWindowVisible(rebar) ||
        !GetClientRect(tray, &rect) || rect.right <= 0 || rect.bottom <= 0 ||
        !GetWindowRect(start, &start_rect) || start_rect.right <= start_rect.left || start_rect.bottom <= start_rect.top ||
        !GetWindowRect(rebar, &rebar_rect) || rebar_rect.right <= rebar_rect.left || rebar_rect.bottom <= rebar_rect.top)
    { stable = FALSE; return; }
    if (!stable || start != previous_start || rebar != previous_rebar || !EqualRect(&rect, &previous_rect) ||
        !EqualRect(&start_rect, &previous_start_rect) || !EqualRect(&rebar_rect, &previous_rebar_rect))
    {
        previous_start = start;
        previous_rebar = rebar;
        previous_rect = rect;
        previous_start_rect = start_rect;
        previous_rebar_rect = rebar_rect;
        stable = TRUE;
        return;
    }
    /* One bounded attempt per shell lifetime, including an unresponsive shell. */
    attempted = TRUE;
    if (!IsWindow(tray) || GetWindowThreadProcessId(tray, &pid) == 0 || pid != previous_pid) return;
    fprintf(stderr, "shell layout sync hwnd=%p pid=%lu client=%ld,%ld\n",
            tray, pid, rect.right, rect.bottom);
    SendMessageTimeoutW(tray, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rect.right, rect.bottom),
                        SMTO_ABORTIFHUNG, 500, &result);
}

/* XP's Alt+Tab window, as measured on the reference XP machine: 331 px wide,
 * seven 43 px cells per row, centred while there is only one row, a 2 px
 * highlight frame around the selected icon and its title in a sunken field
 * below; the window is centred in the work area. The WM owns the keyboard
 * while Alt is held and records the session, the number of Tab steps and
 * whether the switch is open, committed or cancelled in switch.tsv. */
#define SWITCH_COLUMNS 7
#define SWITCH_ROWS 3
#define SWITCH_CELL 43
#define SWITCH_MAX (SWITCH_COLUMNS * 12)
static struct
{
    unsigned session;
    int steps, count, selected, limit;
    char state;
    HWND window, items[SWITCH_MAX];
    HFONT font;
} switcher;

static BOOL switch_shell_window(HWND hwnd)
{
    WCHAR name[64];
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    return !wcscmp(name, L"Progman") || !wcscmp(name, L"Shell_TrayWnd");
}

/* Where HWND's chain of owners ends, short of the desktop and the taskbar. */
static HWND switch_root(HWND hwnd)
{
    HWND owner;
    unsigned depth;
    for (depth = 0; depth < MAX_CLIENTS; ++depth)
    {
        if (!(owner = GetWindow(hwnd, GW_OWNER)) || switch_shell_window(owner)) break;
        hwnd = owner;
    }
    return hwnd;
}

static BOOL switch_owned_by(HWND hwnd, HWND owner)
{
    unsigned depth;
    for (depth = 0; hwnd && depth < MAX_CLIENTS; ++depth, hwnd = GetWindow(hwnd, GW_OWNER))
        if (hwnd == owner) return TRUE;
    return FALSE;
}

struct switch_search
{
    HWND owner, found;
};

static BOOL CALLBACK find_popup(HWND hwnd, LPARAM param)
{
    struct switch_search *search = (struct switch_search *)param;
    if (hwnd == search->owner || !IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd) ||
        !switch_owned_by(hwnd, search->owner)) return TRUE;
    search->found = hwnd;
    return FALSE;
}

/* The window to activate for an entry, as on XP: the popup it owns that was
 * active last, otherwise the entry itself, or, while a modal dialog has it
 * disabled, a popup that takes input. NULL if nothing does. */
static HWND switch_popup(HWND hwnd)
{
    struct switch_search search = {hwnd, NULL};
    HWND popup = GetLastActivePopup(hwnd);
    if (popup && popup != hwnd && IsWindowVisible(popup) && IsWindowEnabled(popup)) return popup;
    if (IsWindowEnabled(hwnd)) return hwnd;
    EnumWindows(find_popup, (LPARAM)&search);
    return search.found;
}

/* One entry for a chain of owners: its first window that shows. A dialog
 * whose owners are hidden (a file's properties, Run) has no taskbar button
 * but is listed; one beside its owner is reached through the owner's entry. */
static BOOL CALLBACK switch_candidate(HWND hwnd, LPARAM param)
{
    DWORD style = GetWindowLongW(hwnd, GWL_STYLE), ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    (void)param;
    if (switcher.count >= switcher.limit) return FALSE;
    if (hwnd == switcher.window || !(style & WS_VISIBLE) || (ex & WS_EX_NOACTIVATE)) return TRUE;
    if (switch_shell_window(hwnd)) return TRUE;
    if (!(ex & WS_EX_APPWINDOW))
    {
        HWND owner, root = switch_root(hwnd);
        unsigned depth;
        int i;
        if (ex & WS_EX_TOOLWINDOW) return TRUE;
        for (owner = hwnd, depth = 0; owner != root && depth < MAX_CLIENTS; ++depth)
            if (IsWindowVisible(owner = GetWindow(owner, GW_OWNER))) return TRUE;
        /* Of two dialogs of a hidden owner, the upper one. */
        for (i = 0; root != hwnd && i < switcher.count; ++i)
            if (switch_root(switcher.items[i]) == root) return TRUE;
    }
    /* A minimized window's popups are hidden with it. */
    if (!IsIconic(hwnd) && !switch_popup(hwnd)) return TRUE;
    switcher.items[switcher.count++] = hwnd;
    return TRUE;
}

static unsigned switch_rank(HWND hwnd)
{
    unsigned rank = ARRAYSIZE(recent), depth;
    for (depth = 0; hwnd && depth < MAX_CLIENTS; ++depth, hwnd = GetWindow(hwnd, GW_OWNER))
    {
        unsigned r = recent_rank(hwnd);
        if (r < rank) rank = r;
    }
    return rank;
}

static void switch_collect(void)
{
    HWND foreground = GetForegroundWindow();
    int i;
    switcher.count = 0;
    switcher.limit = SWITCH_COLUMNS * SWITCH_ROWS;
    note_recent(foreground);
    EnumWindows(switch_candidate, 0);
    /* By last activation, stably; windows never activated keep their Z order
     * behind the others. An entry's rank is its own or its owner's. */
    for (i = 1; i < switcher.count; ++i)
    {
        int k;
        for (k = i; k > 0 && switch_rank(switcher.items[k - 1]) > switch_rank(switcher.items[k]); --k)
        {
            HWND item = switcher.items[k];
            switcher.items[k] = switcher.items[k - 1];
            switcher.items[k - 1] = item;
        }
    }
    /* Start from the active window, which is not always the topmost one: its
     * own entry, or that of an owner when it is a dialog without one. */
    for (; foreground; foreground = GetWindow(foreground, GW_OWNER))
    {
        for (i = 0; i < switcher.count && switcher.items[i] != foreground; ++i) {}
        if (i == switcher.count) continue;
        memmove(switcher.items + 1, switcher.items, i * sizeof(HWND));
        switcher.items[0] = foreground;
        break;
    }
}

static HICON window_icon(HWND hwnd)
{
    DWORD_PTR icon = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &icon)) icon = 0;
    if (!icon) icon = GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!icon && !SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 100, &icon)) icon = 0;
    if (!icon) icon = GetClassLongPtrW(hwnd, GCLP_HICONSM);
    return (HICON)icon;
}

static HICON switch_icon(HWND hwnd)
{
    HICON icon = NULL;
    unsigned depth;
    /* A dialog without an icon of its own shows its owner's. */
    for (depth = 0; !icon && hwnd && depth < MAX_CLIENTS; ++depth, hwnd = GetWindow(hwnd, GW_OWNER))
        icon = window_icon(hwnd);
    return icon ? icon : default_icon;
}

static int switch_rows(void)
{
    return switcher.count > SWITCH_COLUMNS ? (switcher.count + SWITCH_COLUMNS - 1) / SWITCH_COLUMNS : 1;
}

static void switch_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rect, field;
    int shown = switcher.count < SWITCH_COLUMNS ? switcher.count : SWITCH_COLUMNS;
    int left = 15 + (SWITCH_COLUMNS - shown) * SWITCH_CELL / 2, i, rows = switch_rows();
    WCHAR title[512] = L"";
    HGDIOBJ old_font;

    GetClientRect(hwnd, &rect);
    FillRect(dc, &rect, GetSysColorBrush(COLOR_3DFACE));
    DrawEdge(dc, &rect, EDGE_RAISED, BF_RECT);
    for (i = 0; i < switcher.count; ++i)
    {
        int x = left + i % SWITCH_COLUMNS * SWITCH_CELL, y = 19 + i / SWITCH_COLUMNS * SWITCH_CELL;
        if (i == switcher.selected)
        {
            RECT frame = {x + 1, y, x + 1 + SWITCH_CELL, y + SWITCH_CELL};
            HBRUSH brush = GetSysColorBrush(COLOR_HIGHLIGHT);
            FrameRect(dc, &frame, brush);
            InflateRect(&frame, -1, -1);
            FrameRect(dc, &frame, brush);
        }
        DrawIconEx(dc, x + 6, y + 5, switch_icon(switcher.items[i]), 32, 32, 0, NULL, DI_NORMAL);
    }
    SetRect(&field, 15, 72 + (rows - 1) * SWITCH_CELL, 316, 97 + (rows - 1) * SWITCH_CELL);
    DrawEdge(dc, &field, EDGE_SUNKEN, BF_RECT);
    if (switcher.selected < switcher.count)
        GetWindowTextW(switcher.items[switcher.selected], title, ARRAYSIZE(title));
    InflateRect(&field, -2, -2);
    field.left += 2;
    old_font = SelectObject(dc, switcher.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    DrawTextW(dc, title, -1, &field, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old_font);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK switch_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) { switch_paint(hwnd); return 0; }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void switch_show(void)
{
    RECT area;
    int width = 30 + SWITCH_COLUMNS * SWITCH_CELL, height = 64 + switch_rows() * SWITCH_CELL;
    if (!switcher.font)
    {
        NONCLIENTMETRICSW metrics = {.cbSize = sizeof(metrics)};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        metrics.lfCaptionFont.lfWeight = FW_BOLD;
        switcher.font = CreateFontIndirectW(&metrics.lfCaptionFont);
    }
    if (!switcher.window)
        switcher.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"Win4WineSwitch", L"", WS_POPUP,
                                          0, 0, width, height, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!switcher.window) return;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    SetWindowPos(switcher.window, HWND_TOPMOST, area.left + (area.right - area.left - width) / 2,
                 area.top + (area.bottom - area.top - height) / 2, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(switcher.window, NULL, FALSE);
    UpdateWindow(switcher.window);
}

static void switch_commit(void)
{
    HWND target = switcher.selected < switcher.count ? switcher.items[switcher.selected] : NULL;
    HWND popup;
    unsigned i;
    if (switcher.window) ShowWindow(switcher.window, SW_HIDE);
    if (!target) return;
    if (!IsIconic(target) && (popup = switch_popup(target))) target = popup;
    /* The WM activates the chosen window as for a click; Wine gives a
     * managed window X focus only through the WM. Linux windows are named by
     * their own X window, Wine windows by the one Wine created. */
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i].xid && clients[i].hwnd == target)
        {
            /* The frame stands for the Linux window in Win32 (see below). */
            make_foreground(target);
            command("switch", clients[i].xid);
            return;
        }
    if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
    /* Win32's foreground too: Wine may let the WM's focus pass without
     * activating the window, and the foreground stayed where it was (a Linux
     * window's frame), so the next Alt+Tab started from there and chose
     * the window that was already active. */
    make_foreground(target);
    command("switch", (unsigned long)(uintptr_t)GetPropW(target, L"__wine_x11_whole_window"));
}

static void switch_events(void)
{
    WCHAR path[MAX_PATH + 32];
    unsigned session;
    int steps;
    char state;
    FILE *file;
    swprintf(path, MAX_PATH + 32, L"%ls\\switch.tsv", directory);
    if (!(file = _wfopen(path, L"rb"))) return;
    if (fscanf(file, "%u %d %c", &session, &steps, &state) != 3) state = 0;
    fclose(file);
    if (!state || (session == switcher.session && steps == switcher.steps && state == switcher.state)) return;
    if (session != switcher.session)
    {
        if (switcher.window) ShowWindow(switcher.window, SW_HIDE);
        switch_collect();
    }
    else if (switcher.state != 'o') return; /* This session already ended. */
    switcher.session = session;
    switcher.steps = steps;
    switcher.state = state;
    if (!switcher.count) return;
    switcher.selected = ((steps % switcher.count) + switcher.count) % switcher.count;
    if (state == 'o' && switcher.count > 1) switch_show();
    else if (state == 'c') switch_commit();
    else if (switcher.window) ShowWindow(switcher.window, SW_HIDE);
}

/* Linux tray icons. bridge-wm/tray.py hosts StatusNotifierItems and lists
 * them in tray.tsv with a W4I1 image each; they join the XP notification
 * area through the public Shell_NotifyIcon. Clicks go back as *.tray files,
 * and a context menu that tray.py fetched over D-Bus is shown here. */
#define MAX_TRAY 64
#define TRAY_CALLBACK (WM_APP + 0x10)
#define MAX_MENU 256
static struct tray_icon
{
    unsigned id;
    int seen, added, failed, menu_pending;
    char status;
    HICON icon, retired;
    FILETIME icon_time;
    DWORD icon_size;
    WCHAR tip[128];
    POINT menu_point;
    unsigned menu_shown;
} tray_icons[MAX_TRAY];
static HWND tray_window;
static UINT taskbar_created;

static void tray_notify(struct tray_icon *t, DWORD op)
{
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof(data);
    data.hWnd = tray_window;
    data.uID = t->id;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TRAY_CALLBACK;
    data.hIcon = t->icon ? t->icon : default_icon;
    lstrcpynW(data.szTip, t->tip, ARRAYSIZE(data.szTip));
    if (op == NIM_DELETE) data.uFlags = 0;
    /* Before the shell's taskbar exists every attempt fails; the next
     * change or TaskbarCreated tries again. Report only the first. An add
     * the shell took while still starting may have answered FALSE: then
     * the icon exists and only NIM_MODIFY succeeds. */
    if (!Shell_NotifyIconW(op, &data) && (op != NIM_ADD || !Shell_NotifyIconW(NIM_MODIFY, &data)))
    {
        if (!t->failed) fprintf(stderr, "Shell_NotifyIcon %lu for tray icon %u failed\n", op, t->id);
        t->failed = 1;
        return;
    }
    t->failed = 0;
    if (op == NIM_ADD) t->added = 1;
}

static void tray_remove(struct tray_icon *t)
{
    if (t->added) tray_notify(t, NIM_DELETE);
    if (t->icon) DestroyIcon(t->icon);
    if (t->retired) DestroyIcon(t->retired);
    memset(t, 0, sizeof(*t));
}

/* Returns whether the image changed. */
static int tray_icon_image(struct tray_icon *t)
{
    WCHAR path[MAX_PATH + 40];
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    HICON icon;
    swprintf(path, MAX_PATH + 40, L"%ls\\tray-%u.argb", directory, t->id);
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attrs) || attrs.nFileSizeHigh ||
        attrs.nFileSizeLow > 12 + 64 * 64 * 4)
        return 0;
    if (t->icon && !CompareFileTime(&attrs.ftLastWriteTime, &t->icon_time) && attrs.nFileSizeLow == t->icon_size)
        return 0;
    if (!(icon = read_icon(path, attrs.nFileSizeLow))) return 0;
    /* The shell keeps showing the previous image until NIM_MODIFY has the
     * new handle; destroying it first could hand the same value back. */
    if (t->retired) DestroyIcon(t->retired);
    t->retired = t->icon;
    t->icon = icon;
    t->icon_time = attrs.ftLastWriteTime;
    t->icon_size = attrs.nFileSizeLow;
    return 1;
}

static void tray_menu(struct tray_icon *t)
{
    WCHAR path[MAX_PATH + 40], label[256];
    char line[1024];
    unsigned seq = 0, count = 0, items[MAX_MENU];
    HMENU stack[9] = {0};
    int depth, item, offset, chosen;
    char flags[8];
    FILE *file;
    swprintf(path, MAX_PATH + 40, L"%ls\\menu-%u.tsv", directory, t->id);
    if (!(file = _wfopen(path, L"rb"))) return;
    if (!fgets(line, sizeof(line), file) || sscanf(line, "%u", &seq) != 1 || seq <= t->menu_shown)
    {
        fclose(file);
        return;
    }
    t->menu_pending = 0;
    t->menu_shown = seq;
    stack[0] = CreatePopupMenu();
    while (stack[0] && count < MAX_MENU && fgets(line, sizeof(line), file))
    {
        UINT mf = MF_STRING;
        offset = 0;
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%d\t%d\t%7s\t%n", &depth, &item, flags, &offset) != 3 || depth < 0 || depth > 7) continue;
        if (!offset) offset = strlen(line);
        if (!MultiByteToWideChar(CP_UTF8, 0, line + offset, -1, label, ARRAYSIZE(label))) label[0] = 0;
        while (depth > 0 && !stack[depth]) --depth;
        if (strchr(flags, 's')) { AppendMenuW(stack[depth], MF_SEPARATOR, 0, NULL); continue; }
        if (strchr(flags, 'd')) mf |= MF_GRAYED;
        if (strchr(flags, 'c') || strchr(flags, 'r')) mf |= MF_CHECKED;
        items[count] = item;
        /* An entry followed by deeper ones opens a submenu; the next line decides. */
        AppendMenuW(stack[depth], mf, ++count, label);
        stack[depth + 1] = NULL;
        {
            long position = ftell(file);
            char next[64];
            int next_depth;
            if (fgets(next, sizeof(next), file) && sscanf(next, "%d", &next_depth) == 1 && next_depth == depth + 1)
            {
                HMENU sub = CreatePopupMenu();
                MENUITEMINFOW info = {.cbSize = sizeof(info), .fMask = MIIM_SUBMENU, .hSubMenu = sub};
                SetMenuItemInfoW(stack[depth], count, FALSE, &info);
                stack[depth + 1] = sub;
            }
            fseek(file, position, SEEK_SET);
        }
    }
    fclose(file);
    if (!stack[0]) return;
    /* The usual notification-area idiom: the owner must be foreground for
     * the menu to close when the user clicks elsewhere. */
    SetForegroundWindow(tray_window);
    chosen = TrackPopupMenuEx(stack[0], TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              t->menu_point.x, t->menu_point.y, tray_window, NULL);
    PostMessageW(tray_window, WM_NULL, 0, 0);
    DestroyMenu(stack[0]);
    if (chosen > 0 && (unsigned)chosen <= count)
    {
        char data[80];
        snprintf(data, sizeof(data), "event %u %u %u\n", t->id, seq, items[chosen - 1]);
        write_file_command(data, L"tray");
    }
}

static void tray_sync(void)
{
    WCHAR path[MAX_PATH + 32];
    char line[1024];
    FILE *file;
    unsigned i;
    if (!tray_window) return;
    for (i = 0; i < MAX_TRAY; ++i) tray_icons[i].seen = 0;
    swprintf(path, MAX_PATH + 32, L"%ls\\tray.tsv", directory);
    if ((file = _wfopen(path, L"rb")))
    {
        while (fgets(line, sizeof(line), file))
        {
            unsigned id;
            char status;
            int offset = 0, changed;
            WCHAR tip[128];
            struct tray_icon *t = NULL;
            line[strcspn(line, "\r\n")] = 0;
            if (sscanf(line, "%u\t%c\t%n", &id, &status, &offset) != 2 || !id) continue;
            if (!offset) offset = strlen(line);
            if (!MultiByteToWideChar(CP_UTF8, 0, line + offset, -1, tip, ARRAYSIZE(tip))) tip[0] = 0;
            for (i = 0; i < MAX_TRAY && !t; ++i) if (tray_icons[i].id == id) t = &tray_icons[i];
            for (i = 0; i < MAX_TRAY && !t; ++i) if (!tray_icons[i].id) { t = &tray_icons[i]; t->id = id; }
            if (!t) continue;
            t->seen = 1;
            changed = tray_icon_image(t) | (wcscmp(t->tip, tip) != 0) | (t->status != status);
            wcscpy(t->tip, tip);
            t->status = status;
            if (!t->added) tray_notify(t, NIM_ADD);
            else if (changed) tray_notify(t, NIM_MODIFY);
            if (t->retired && t->added)
            {
                DestroyIcon(t->retired);
                t->retired = NULL;
            }
        }
        fclose(file);
    }
    for (i = 0; i < MAX_TRAY; ++i)
    {
        if (tray_icons[i].id && !tray_icons[i].seen) tray_remove(&tray_icons[i]);
        else if (tray_icons[i].menu_pending) tray_menu(&tray_icons[i]);
    }
}

static LRESULT CALLBACK tray_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == TRAY_CALLBACK)
    {
        const char *op = NULL;
        unsigned i;
        POINT pt;
        GetCursorPos(&pt);
        switch (LOWORD(lp))
        {
        case WM_LBUTTONUP: op = "activate"; break;
        case WM_MBUTTONUP: op = "secondary"; break;
        case WM_RBUTTONUP: op = "context"; break;
        }
        for (i = 0; op && i < MAX_TRAY; ++i)
            if (tray_icons[i].id == wp)
            {
                char data[80];
                if (!strcmp(op, "context"))
                {
                    tray_icons[i].menu_pending = 1;
                    tray_icons[i].menu_point = pt;
                }
                snprintf(data, sizeof(data), "%s %u %ld %ld\n", op, tray_icons[i].id, pt.x, pt.y);
                write_file_command(data, L"tray");
                break;
            }
        return 0;
    }
    if (msg == taskbar_created && msg)
    {
        /* A restarted shell has an empty notification area. */
        unsigned i;
        for (i = 0; i < MAX_TRAY; ++i) tray_icons[i].added = 0;
        dirty = TRUE;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void tray_start(void)
{
    WNDCLASSW cls = {0};
    WCHAR path[MAX_PATH + 32];
    char size[16];
    FILE *file;
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    cls.lpfnWndProc = tray_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"Win4WineTray";
    if (!RegisterClassW(&cls)) return;
    tray_window = CreateWindowExW(WS_EX_TOOLWINDOW, L"Win4WineTray", L"", WS_POPUP, 0, 0, 0, 0,
                                  NULL, NULL, GetModuleHandleW(NULL), NULL);
    /* tray.py renders the images at the notification area's icon size. */
    swprintf(path, MAX_PATH + 32, L"%ls\\tray-size.tmp", directory);
    snprintf(size, sizeof(size), "%d\n", GetSystemMetrics(SM_CXSMICON));
    if ((file = _wfopen(path, L"wb")))
    {
        WCHAR target[MAX_PATH + 32];
        fputs(size, file);
        fclose(file);
        swprintf(target, MAX_PATH + 32, L"%ls\\tray-size", directory);
        if (!MoveFileExW(path, target, MOVEFILE_REPLACE_EXISTING)) DeleteFileW(path);
    }
}

/* Window snapping, opt-in (session.json "window_snap"), as Windows 7 does:
 * Win+Left and Win+Right put the foreground window on half of its monitor's
 * work area, Win+Up maximizes it and Win+Down restores or minimizes it.
 * Dragging a caption to the left or right edge of the screen snaps the
 * window there, to the top edge maximizes it; an outline shows where.
 * Wine's move loop reports drags (Wine 0038: EVENT_SYSTEM_MOVESIZESTART/END
 * and "Win4WineSizeMove") and gives a snapped window its size back when its
 * caption is dragged away ("Win4WineSnapRestore"), and restores a maximized
 * window dragged by its caption. Native clients' frames snap like Wine's
 * windows; the WM moves their clients after them.
 * In the Wayland session labwc drags windows, snaps them at the edges and
 * keeps which are snapped: Win+arrows ask it to snap or unsnap a window
 * ("snap HWND SIDE", "unsnap HWND"), tiled.tsv says which windows are, and
 * snapping.tsv where a dragged one would go, for this outline. */
#define SIZE_MOVE_ENDED     0x1000
#define SIZE_MOVE_MOVED     0x0100
#define SIZE_MOVE_CANCELLED 0x0200
#define MAX_SNAPPED 256
enum snap_side { SNAP_NONE, SNAP_LEFT, SNAP_RIGHT, SNAP_TOP };
static struct snapped { HWND hwnd; enum snap_side side; RECT restore, rect; } snapped[MAX_SNAPPED];
static BOOL snap_enabled, snap_compositor;
static struct
{
    HWND hwnd, preview[4];  /* the outline's sides */
    BOOL snapped;  /* whether the window was snapped when its drag began */
    BOOL zoomed;   /* or maximized */
    RECT restore;  /* then its size and place to restore */
    RECT shown;
    UINT_PTR timer;
} snap_drag;
static struct { HWND hwnd; enum snap_side side; HMONITOR monitor; BOOL known; RECT restore; int tries; UINT_PTR timer; } snap_pending;

static BOOL snap_window(HWND hwnd)
{
    WCHAR name[80];
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (!hwnd || !IsWindowVisible(hwnd) || (style & WS_CHILD) || (style & WS_CAPTION) != WS_CAPTION) return FALSE;
    GetClassNameW(hwnd, name, 80);
    return wcsicmp(name, L"Progman") && wcsicmp(name, L"Shell_TrayWnd") && wcsicmp(name, L"WorkerW") &&
           wcsicmp(name, L"DV2ControlHost") && wcsicmp(name, L"Win4WineSwitch");
}

/* A native client's frame is maximized by the WM, and Win32 learns it later. */
static BOOL snap_zoomed(HWND hwnd)
{
    return IsZoomed(hwnd) || GetPropW(hwnd, L"Win4WineMaximized");
}

static RECT snap_box(HMONITOR monitor, enum snap_side side)
{
    MONITORINFO info = {.cbSize = sizeof(info)};
    RECT box;
    GetMonitorInfoW(monitor, &info);
    box = info.rcWork;
    if (side == SNAP_LEFT) box.right = box.left + (box.right - box.left) / 2;
    else if (side == SNAP_RIGHT) box.left = box.left + (box.right - box.left) / 2;
    return box;
}

/* labwc's word on a window (tiled.tsv): which half it is snapped to. */
static struct snapped *snap_tiled(HWND hwnd)
{
    static struct snapped entry;
    WCHAR path[MAX_PATH + 32];
    unsigned long window;
    int right;
    FILE *file;
    swprintf(path, MAX_PATH + 32, L"%ls\\tiled.tsv", directory);
    if (!(file = _wfopen(path, L"rb"))) return NULL;
    memset(&entry, 0, sizeof(entry));
    while (fscanf(file, "%lx %d", &window, &right) == 2)
        if ((HWND)(ULONG_PTR)window == hwnd)
        {
            entry.hwnd = hwnd;
            entry.side = right ? SNAP_RIGHT : SNAP_LEFT;
        }
    fclose(file);
    return entry.hwnd ? &entry : NULL;
}

/* RESTORE: where unsnapping it puts it, if labwc does not know that yet. */
static void snap_command(const char *op, HWND hwnd, enum snap_side side, const RECT *restore)
{
    char data[160];
    if (side == SNAP_NONE) snprintf(data, sizeof(data), "%s %lx\n", op, (unsigned long)(ULONG_PTR)hwnd);
    else if (!restore) snprintf(data, sizeof(data), "%s %lx %d\n", op, (unsigned long)(ULONG_PTR)hwnd, side == SNAP_RIGHT);
    else snprintf(data, sizeof(data), "%s %lx %d %ld %ld %ld %ld\n", op, (unsigned long)(ULONG_PTR)hwnd,
                  side == SNAP_RIGHT, restore->left, restore->top, restore->right - restore->left,
                  restore->bottom - restore->top);
    write_command(data);
}

/* The entry of a window that is still where it was snapped. */
static struct snapped *snap_find(HWND hwnd)
{
    unsigned i;
    if (snap_compositor) return snap_tiled(hwnd);
    for (i = 0; i < MAX_SNAPPED; ++i)
    {
        RECT rect;
        if (!snapped[i].hwnd) continue;
        if (!IsWindow(snapped[i].hwnd)) { memset(&snapped[i], 0, sizeof(snapped[i])); continue; }
        if (snapped[i].hwnd != hwnd) continue;
        GetWindowRect(hwnd, &rect);
        /* A client with size increments may stop short of the box's far edges. */
        if (snap_zoomed(hwnd) || (rect.left == snapped[i].rect.left && rect.top == snapped[i].rect.top))
            return &snapped[i];
        RemovePropW(hwnd, L"Win4WineSnapRestore");
        memset(&snapped[i], 0, sizeof(snapped[i]));
        return NULL;
    }
    return NULL;
}

static void snap_forget(HWND hwnd)
{
    unsigned i;
    if (snap_compositor) return;
    RemovePropW(hwnd, L"Win4WineSnapRestore");
    for (i = 0; i < MAX_SNAPPED; ++i) if (snapped[i].hwnd == hwnd) memset(&snapped[i], 0, sizeof(snapped[i]));
}

static void snap_place(HWND hwnd, enum snap_side side, HMONITOR monitor, const RECT *restore)
{
    struct snapped *entry;
    RECT box = snap_box(monitor, side);
    unsigned i;
    if (snap_compositor)
    {
        /* labwc snaps it on its output, and keeps where it was. */
        snap_command("snap", hwnd, side, restore);
        return;
    }
    entry = snap_find(hwnd);
    if (!entry)
        for (i = 0; i < MAX_SNAPPED && !entry; ++i) if (!snapped[i].hwnd) entry = &snapped[i];
    if (!entry) return;
    entry->hwnd = hwnd;
    entry->side = side;
    entry->restore = *restore;
    entry->rect = box;
    SetPropW(hwnd, L"Win4WineSnapRestore", (HANDLE)(UINT_PTR)MAKELONG(restore->right - restore->left,
                                                                       restore->bottom - restore->top));
    SetWindowPos(hwnd, NULL, box.left, box.top, box.right - box.left, box.bottom - box.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

/* A maximized window is restored first; a native client's frame follows its
 * client's restore when the WM reports it. */
static void CALLBACK snap_pending_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    (void)hwnd; (void)msg; (void)time;
    if (id != snap_pending.timer) return;
    if (!IsWindow(snap_pending.hwnd) || (snap_zoomed(snap_pending.hwnd) && ++snap_pending.tries < 40)) return;
    KillTimer(NULL, snap_pending.timer);
    snap_pending.timer = 0;
    if (snap_zoomed(snap_pending.hwnd)) return;
    if (!snap_pending.known) GetWindowRect(snap_pending.hwnd, &snap_pending.restore);
    snap_place(snap_pending.hwnd, snap_pending.side, snap_pending.monitor, &snap_pending.restore);
}

static void snap_to(HWND hwnd, enum snap_side side, HMONITOR monitor)
{
    struct snapped *entry = snap_find(hwnd);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    RECT restore;
    if (side == SNAP_TOP)
    {
        if ((style & WS_MAXIMIZEBOX) && !snap_zoomed(hwnd)) PostMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        return;
    }
    if (!(style & WS_THICKFRAME)) return;
    if (entry) restore = entry->restore;
    if (snap_zoomed(hwnd))
    {
        /* Wine gives other processes no normal position: the window's
         * restored rectangle is the place to restore it to later. */
        DWORD_PTR result;
        SendMessageTimeoutW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0, SMTO_ABORTIFHUNG, 1000, &result);
        if (snap_zoomed(hwnd))
        {
            if (snap_pending.timer) KillTimer(NULL, snap_pending.timer);
            snap_pending.hwnd = hwnd;
            snap_pending.side = side;
            snap_pending.monitor = monitor;
            snap_pending.known = entry != NULL;
            snap_pending.restore = restore;
            snap_pending.tries = 0;
            snap_pending.timer = SetTimer(NULL, 0, 50, snap_pending_timer);
            return;
        }
    }
    if (!entry) GetWindowRect(hwnd, &restore);
    snap_place(hwnd, side, monitor, &restore);
}

static void snap_unsnap(struct snapped *entry)
{
    HWND hwnd = entry->hwnd;
    RECT restore = entry->restore;
    if (snap_compositor)
    {
        snap_command("unsnap", hwnd, SNAP_NONE, NULL);
        return;
    }
    snap_forget(hwnd);
    SetWindowPos(hwnd, NULL, restore.left, restore.top, restore.right - restore.left,
                 restore.bottom - restore.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

/* The monitor beside this one, or NULL at the edge of the screen. */
static HMONITOR snap_neighbour(HMONITOR monitor, int direction)
{
    MONITORINFO info = {.cbSize = sizeof(info)};
    POINT pt;
    GetMonitorInfoW(monitor, &info);
    pt.x = direction < 0 ? info.rcMonitor.left - 1 : info.rcMonitor.right;
    pt.y = (info.rcMonitor.top + info.rcMonitor.bottom) / 2;
    return MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
}

static void snap_hotkey(UINT vk)
{
    HWND hwnd = GetForegroundWindow();
    struct snapped *entry;
    HMONITOR monitor, next;
    LONG style;
    unsigned i;
    /* A native client with X focus is the foreground, as for the taskbar. */
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i].xid && clients[i].active && !clients[i].hidden) hwnd = clients[i].hwnd;
    if (hwnd) hwnd = GetAncestor(hwnd, GA_ROOT);
    if (!snap_window(hwnd) || IsIconic(hwnd)) return;
    style = GetWindowLongW(hwnd, GWL_STYLE);
    entry = snap_find(hwnd);
    monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    switch (vk)
    {
    case VK_LEFT:
    case VK_RIGHT:
    {
        enum snap_side side = vk == VK_LEFT ? SNAP_LEFT : SNAP_RIGHT, other = vk == VK_LEFT ? SNAP_RIGHT : SNAP_LEFT;
        if (entry && entry->side == other && !snap_zoomed(hwnd)) snap_unsnap(entry);
        else if (entry && entry->side == side && !snap_zoomed(hwnd))
        {
            /* On to the next monitor's near half; labwc does that itself. */
            if (snap_compositor) snap_command("snap", hwnd, side, NULL);
            else if ((next = snap_neighbour(monitor, vk == VK_LEFT ? -1 : 1))) snap_to(hwnd, other, next);
        }
        else snap_to(hwnd, side, monitor);
        break;
    }
    case VK_UP:
        snap_to(hwnd, SNAP_TOP, monitor);
        break;
    case VK_DOWN:
        if (snap_zoomed(hwnd)) PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        else if (entry) snap_unsnap(entry);
        else if (style & WS_MINIMIZEBOX) PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        break;
    }
}

/* Where a caption dragged to this point would snap. Only the outer edges of
 * the screen count, not the edges between two monitors. */
static enum snap_side snap_edge(HWND hwnd, POINT pt, HMONITOR *monitor)
{
    MONITORINFO info = {.cbSize = sizeof(info)};
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    POINT beyond = pt;
    *monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(*monitor, &info);
    if (pt.y <= info.rcMonitor.top && (style & WS_MAXIMIZEBOX))
    {
        beyond.y = info.rcMonitor.top - 1;
        if (!MonitorFromPoint(beyond, MONITOR_DEFAULTTONULL)) return SNAP_TOP;
    }
    if (!(style & WS_THICKFRAME)) return SNAP_NONE;
    beyond = pt;
    if (pt.x <= info.rcMonitor.left)
    {
        beyond.x = info.rcMonitor.left - 1;
        if (!MonitorFromPoint(beyond, MONITOR_DEFAULTTONULL)) return SNAP_LEFT;
    }
    if (pt.x >= info.rcMonitor.right - 1)
    {
        beyond.x = info.rcMonitor.right;
        if (!MonitorFromPoint(beyond, MONITOR_DEFAULTTONULL)) return SNAP_RIGHT;
    }
    return SNAP_NONE;
}

/* The outline: a frame in the selection colour with XP's halftone, a little
 * inside the box so that it is never under the pointer at the edge. Its four
 * sides are windows of their own: Xwayland shows a window region's outside. */
static LRESULT CALLBACK snap_preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        static const WORD pattern[8] = {0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa};
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        HBITMAP bitmap = CreateBitmap(8, 8, 1, 1, pattern);
        HBRUSH brush = CreatePatternBrush(bitmap);
        RECT rect, window;
        /* One pattern across the sides */
        GetWindowRect(hwnd, &window);
        SetBrushOrgEx(dc, -window.left & 7, -window.top & 7, NULL);
        GetClientRect(hwnd, &rect);
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        DeleteObject(bitmap);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void snap_show_preview(const RECT *box)
{
    int thickness = max(4, GetSystemMetrics(SM_CXSIZEFRAME)), inset = 2 * thickness;
    RECT rect, sides[4];
    unsigned i;
    if (!box)
    {
        for (i = 0; i < ARRAYSIZE(snap_drag.preview); ++i)
            if (snap_drag.preview[i]) ShowWindow(snap_drag.preview[i], SW_HIDE);
        SetRectEmpty(&snap_drag.shown);
        return;
    }
    rect = *box;
    InflateRect(&rect, -inset, -inset);
    if (EqualRect(&rect, &snap_drag.shown)) return;
    snap_drag.shown = rect;
    SetRect(&sides[0], rect.left, rect.top, rect.right, rect.top + thickness);
    SetRect(&sides[1], rect.left, rect.bottom - thickness, rect.right, rect.bottom);
    SetRect(&sides[2], rect.left, rect.top + thickness, rect.left + thickness, rect.bottom - thickness);
    SetRect(&sides[3], rect.right - thickness, rect.top + thickness, rect.right, rect.bottom - thickness);
    for (i = 0; i < ARRAYSIZE(sides); ++i)
    {
        if (!snap_drag.preview[i])
            snap_drag.preview[i] = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                                   L"Win4WineSnapPreview", L"", WS_POPUP, 0, 0, 0, 0,
                                                   NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (!snap_drag.preview[i]) continue;
        SetWindowPos(snap_drag.preview[i], HWND_TOPMOST, sides[i].left, sides[i].top, sides[i].right - sides[i].left,
                     sides[i].bottom - sides[i].top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        /* Now: the proxy's own frames are dragged in a move loop of this thread. */
        RedrawWindow(snap_drag.preview[i], NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

static void snap_drag_end(void)
{
    if (snap_drag.timer) KillTimer(NULL, snap_drag.timer);
    snap_show_preview(NULL);
    snap_drag.timer = 0;
    snap_drag.hwnd = NULL;
}

/* The pointer, while a caption is dragged; timers run in the move loop too. */
static void CALLBACK snap_drag_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    UINT_PTR state;
    HMONITOR monitor;
    enum snap_side side;
    POINT pt;
    (void)hwnd; (void)msg; (void)time;
    if (id != snap_drag.timer) return;
    state = (UINT_PTR)GetPropW(snap_drag.hwnd, L"Win4WineSizeMove");
    if (!IsWindow(snap_drag.hwnd) || (state & SIZE_MOVE_ENDED) || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
    {
        /* EVENT_SYSTEM_MOVESIZEEND snaps; this only hides the outline. */
        snap_show_preview(NULL);
        return;
    }
    GetCursorPos(&pt);
    side = snap_edge(snap_drag.hwnd, pt, &monitor);
    if (side == SNAP_NONE) snap_show_preview(NULL);
    else
    {
        RECT box = snap_box(monitor, side);
        snap_show_preview(&box);
    }
}

static void snap_move_size(DWORD event, HWND hwnd)
{
    UINT_PTR state = (UINT_PTR)GetPropW(hwnd, L"Win4WineSizeMove");
    if (event == EVENT_SYSTEM_MOVESIZESTART)
    {
        /* A drag that already ended was a click. */
        if ((state & SIZE_MOVE_ENDED) || (state & 0xff) != HTCAPTION || !snap_window(hwnd)) return;
        struct snapped *entry = snap_find(hwnd);
        snap_drag_end();
        snap_drag.hwnd = hwnd;
        snap_drag.snapped = entry != NULL;
        snap_drag.zoomed = snap_zoomed(hwnd);
        if (entry) snap_drag.restore = entry->restore;
        else GetWindowRect(hwnd, &snap_drag.restore);
        snap_drag.timer = SetTimer(NULL, 0, 30, snap_drag_timer);
        return;
    }
    if (!(state & SIZE_MOVE_ENDED)) return;
    RemovePropW(hwnd, L"Win4WineSizeMove");
    if ((state & 0xff) != HTCAPTION)
    {
        /* Resizing a snapped window keeps its new size. */
        if (state & SIZE_MOVE_MOVED) snap_forget(hwnd);
        return;
    }
    if (hwnd != snap_drag.hwnd) return;
    if ((state & SIZE_MOVE_MOVED) && !(state & SIZE_MOVE_CANCELLED))
    {
        HMONITOR monitor;
        POINT pt;
        RECT restore = snap_drag.restore;
        enum snap_side side;
        if (snap_drag.zoomed && !snap_drag.snapped)
        {
            /* Wine restored it on the way, at "Win4WineSizeMoveRestored"
             * (left and top plus 0x8000); its size has not changed since. */
            UINT_PTR origin = (UINT_PTR)RemovePropW(hwnd, L"Win4WineSizeMoveRestored");
            GetWindowRect(hwnd, &restore);
            if (origin) OffsetRect(&restore, (int)LOWORD(origin) - 0x8000 - restore.left,
                                   (int)HIWORD(origin) - 0x8000 - restore.top);
        }
        GetCursorPos(&pt);
        side = snap_edge(hwnd, pt, &monitor);
        if (snap_drag.snapped && side != SNAP_LEFT && side != SNAP_RIGHT) snap_forget(hwnd);
        if (side == SNAP_TOP)
        {
            /* Restoring it later gives back its place before the drag. */
            SetWindowPos(hwnd, NULL, restore.left, restore.top, restore.right - restore.left,
                         restore.bottom - restore.top, SWP_NOZORDER | SWP_NOACTIVATE);
            snap_to(hwnd, SNAP_TOP, monitor);
        }
        else if (side != SNAP_NONE)
        {
            snap_forget(hwnd);
            snap_place(hwnd, side, monitor, &restore);
        }
    }
    snap_drag_end();
}

/* labwc writes snapping.tsv as it drags a window: "NUMBER X Y WIDTH HEIGHT",
 * the box it would snap to, or an empty one. */
static void snap_compositor_events(BOOL start)
{
    static unsigned seen;
    WCHAR path[MAX_PATH + 32];
    unsigned number;
    long x, y, width, height;
    FILE *file;
    if (!snap_compositor) return;
    swprintf(path, MAX_PATH + 32, L"%ls\\snapping.tsv", directory);
    if (!(file = _wfopen(path, L"rb"))) return;
    if (fscanf(file, "%u %ld %ld %ld %ld", &number, &x, &y, &width, &height) != 5) number = seen;
    fclose(file);
    if (number == seen) return;
    seen = number;
    if (start || width <= 0 || height <= 0 || width > 65535 || height > 65535) snap_show_preview(NULL);
    else
    {
        RECT box = {x, y, x + width, y + height};
        snap_show_preview(&box);
    }
}

static void snap_start(void)
{
    static const UINT keys[] = {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN};
    WNDCLASSW cls = {0};
    WCHAR value[8];
    DWORD len = GetEnvironmentVariableW(L"WIN4WINE_WINDOW_SNAP", value, ARRAYSIZE(value));
    unsigned i;
    snap_enabled = len && len < ARRAYSIZE(value) && !wcscmp(value, L"1");
    if (!snap_enabled) return;
    snap_compositor = GetEnvironmentVariableW(L"WAYLAND_DISPLAY", NULL, 0) != 0;
    cls.lpfnWndProc = snap_preview_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.lpszClassName = L"Win4WineSnapPreview";
    RegisterClassW(&cls);
    for (i = 0; i < ARRAYSIZE(keys); ++i)
        if (!RegisterHotKey(NULL, (int)keys[i], MOD_WIN, keys[i]))
            fprintf(stderr, "snap: Win+%02x is taken: %lu\n", keys[i], GetLastError());
}

/* Out-of-context WinEvents arrive as messages for this thread. Only top-level
 * windows can be tasks; a destroyed one is matched against the last list. */
static void CALLBACK win_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child,
                               DWORD thread, DWORD time)
{
    (void)hook; (void)thread; (void)time;
    if (event == EVENT_SYSTEM_FOREGROUND) dirty = TRUE;
    else if (event == EVENT_SYSTEM_MOVESIZESTART || event == EVENT_SYSTEM_MOVESIZEEND) snap_move_size(event, hwnd);
    else if (object != OBJID_WINDOW || child != CHILDID_SELF || !hwnd) return;
    else if (event == EVENT_OBJECT_DESTROY) dirty |= contains(previous, previous_count, hwnd);
    else if (GetAncestor(hwnd, GA_PARENT) == GetDesktopWindow()) dirty = TRUE;
}

int wmain(int argc, WCHAR **argv)
{
    WNDCLASSW cls = {0};
    MSG msg;
    HANDLE changes, watch, lock;
    OVERLAPPED watching = {0};
    static DWORD notes[4096];
    WCHAR path[MAX_PATH + 32];
    UINT_PTR layout_timer = 0;
    if (argc != 2 || wcslen(argv[1]) >= MAX_PATH) return 2;
    wcscpy(directory, argv[1]);
    shell_message = RegisterWindowMessageW(L"SHELLHOOK");
    default_icon = LoadIconW(NULL, IDI_APPLICATION);
    cls.hIcon = default_icon;
    cls.lpfnWndProc = proxy_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"Win4WineProxy";
    if (!RegisterClassW(&cls)) return 3;
    cls.hIcon = NULL;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.lpfnWndProc = switch_proc;
    cls.lpszClassName = L"Win4WineSwitch";
    if (!RegisterClassW(&cls)) return 3;
    tray_start();
    wm_follows_frames = !GetEnvironmentVariableW(L"WAYLAND_DISPLAY", NULL, 0);
    /* The WM tests this lock to know whether the proxy is running.
     * wineserver backs it with a POSIX lock that ends with this process. */
    swprintf(path, MAX_PATH + 32, L"%ls\\proxy.lock", directory);
    lock = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (lock == INVALID_HANDLE_VALUE || !LockFile(lock, 0, 0, 1, 0))
        fprintf(stderr, "proxy lock unavailable: %lu\n", GetLastError());
    /* Wake when the WM writes a file or a Win32 window changes; never poll.
     * ReadDirectoryChangesW names the files, so that the proxy's own files
     * and geometry.tsv alone cost no whole pass (see geometry_dirty). */
    watch = CreateFileW(directory, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    changes = CreateEventW(NULL, TRUE, FALSE, NULL);
    watching.hEvent = changes;
    if (watch == INVALID_HANDLE_VALUE || !changes ||
        !ReadDirectoryChangesW(watch, notes, sizeof(notes), FALSE,
                               FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &watching, NULL))
    {
        /* Without names: a whole pass on every change, as before. */
        fprintf(stderr, "ReadDirectoryChangesW unavailable: %lu\n", GetLastError());
        if (watch != INVALID_HANDLE_VALUE) CloseHandle(watch);
        if (changes) CloseHandle(changes);
        watch = INVALID_HANDLE_VALUE;
        changes = FindFirstChangeNotificationW(directory, FALSE,
                                               FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (changes == INVALID_HANDLE_VALUE) return 4;
    }
    SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, win_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, NULL, win_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE, NULL, win_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, win_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    snap_start();
    if (snap_enabled)
        SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, NULL, win_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    shell_key_events(TRUE);
    snap_compositor_events(TRUE);
    for (;;)
    {
        if (files_changed)
        {
            files_changed = FALSE;
            switch_events();
            shell_key_events(FALSE);
            snap_compositor_events(FALSE);
        }
        if (dirty)
        {
            dirty = FALSE;
            geometry_dirty = FALSE;
            snapshot();
            shell_events();
            tray_sync();
        }
        else if (geometry_dirty)
        {
            geometry_dirty = FALSE;
            syncing = 1;
            frame_geometry();
            syncing = 0;
        }
        if (shell_layout_pending() != !!layout_timer)
        {
            if (layout_timer) { KillTimer(NULL, layout_timer); layout_timer = 0; }
            else layout_timer = SetTimer(NULL, 0, 200, NULL);
        }
        /* Paint and other messages wait for the frames to move: a native
         * client that moved first exposes part of its frame, and repainting
         * that first would hold back the move. */
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                unsigned i;
                for (i = 0; i < MAX_CLIENTS; ++i) if (clients[i].xid)
                {
                    DestroyWindow(clients[i].hwnd);
                    if (clients[i].icon) DestroyIcon(clients[i].icon);
                    if (clients[i].small_icon) DestroyIcon(clients[i].small_icon);
                }
                for (i = 0; i < MAX_TRAY; ++i) if (tray_icons[i].id) tray_remove(&tray_icons[i]);
                return 0;
            }
            if (msg.message == WM_HOTKEY && !msg.hwnd)
            {
                snap_hotkey(HIWORD(msg.lParam));
                continue;
            }
            if (msg.message == WM_TIMER && !msg.hwnd && msg.wParam == layout_timer)
            {
                shell_layout();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (dirty || geometry_dirty) continue; /* Changes made by this pass. */
        if (MsgWaitForMultipleObjects(1, &changes, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0)
        {
            DWORD size = 0;
            BOOL done = watch != INVALID_HANDLE_VALUE && GetOverlappedResult(watch, &watching, &size, FALSE);
            if (watch == INVALID_HANDLE_VALUE)
            {
                FindNextChangeNotification(changes);
                dirty = files_changed = TRUE;
                continue;
            }
            if (!done || !size) dirty = files_changed = TRUE; /* an overflow or an error: everything */
            else
            {
                FILE_NOTIFY_INFORMATION *note = (FILE_NOTIFY_INFORMATION *)notes;
                for (;;)
                {
                    WCHAR name[MAX_PATH];
                    DWORD length = min(note->FileNameLength / sizeof(WCHAR), MAX_PATH - 1), n;
                    memcpy(name, note->FileName, length * sizeof(WCHAR));
                    name[length] = 0;
                    n = length;
                    /* The proxy's own files (commands for the WM and the tray
                     * host, which remove them, and frames.tsv) and temporary
                     * files, which are renamed to their real names, change
                     * nothing here. */
                    if ((n > 4 && (!_wcsicmp(name + n - 4, L".cmd") || !_wcsicmp(name + n - 4, L".tmp"))) ||
                        (n > 5 && !_wcsicmp(name + n - 5, L".tray")) || !_wcsicmp(name, L"frames.tsv") ||
                        !_wcsicmp(name, L"proxy.lock")) {}
                    else if (!_wcsicmp(name, L"geometry.tsv")) geometry_dirty = TRUE;
                    else dirty = files_changed = TRUE;
                    if (!note->NextEntryOffset) break;
                    note = (FILE_NOTIFY_INFORMATION *)((char *)note + note->NextEntryOffset);
                }
            }
            ResetEvent(changes);
            if (!ReadDirectoryChangesW(watch, notes, sizeof(notes), FALSE,
                                       FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &watching,
                                       NULL))
            {
                /* Should not happen: go on without names, as before. */
                fprintf(stderr, "ReadDirectoryChangesW: %lu\n", GetLastError());
                CloseHandle(watch);
                CloseHandle(changes);
                watch = INVALID_HANDLE_VALUE;
                changes = FindFirstChangeNotificationW(directory, FALSE,
                                                       FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
                if (changes == INVALID_HANDLE_VALUE) return 4;
                dirty = files_changed = TRUE;
            }
        }
    }
}
