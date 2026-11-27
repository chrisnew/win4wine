/*
 * Windows for the Alt+Tab switcher's rules (bridge-wm/proxy.c), which stay
 * until the process ends:
 *   "Plain"          an ordinary window                       listed
 *   "Modal owner"    disabled by its dialog "Modal dialog"    listed, activates the dialog
 *   "Hidden owner"   invisible, owns "Orphan dialog" and      the upper dialog is listed
 *                    "Second orphan"
 *   "Tool"           a tool window                            not listed
 * Prints the foreground window's title whenever it changes.
 */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static HWND make(DWORD ex, DWORD style, const WCHAR *title, HWND owner, int x, int y)
{
    return CreateWindowExW(ex, L"Win4WineAltTabTest", title, style, x, y, 260, 160, owner, NULL,
                           GetModuleHandleW(NULL), NULL);
}

int wmain(void)
{
    WNDCLASSW cls = {0};
    HWND owner, hidden;
    WCHAR title[64], previous[64] = L"";
    const DWORD dialog = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    MSG msg;

    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszClassName = L"Win4WineAltTabTest";
    RegisterClassW(&cls);

    make(0, WS_OVERLAPPEDWINDOW | WS_VISIBLE, L"Plain", NULL, 20, 20);
    owner = make(0, WS_OVERLAPPEDWINDOW | WS_VISIBLE, L"Modal owner", NULL, 320, 20);
    make(0, dialog, L"Modal dialog", owner, 380, 80);
    EnableWindow(owner, FALSE);
    hidden = make(0, WS_OVERLAPPEDWINDOW, L"Hidden owner", NULL, 20, 240);
    make(0, dialog, L"Second orphan", hidden, 40, 260);
    make(0, dialog, L"Orphan dialog", hidden, 100, 320);
    make(WS_EX_TOOLWINDOW, dialog, L"Tool", NULL, 640, 240);

    SetTimer(NULL, 0, 200, NULL);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        title[0] = 0;
        GetWindowTextW(GetForegroundWindow(), title, 64);
        if (wcscmp(title, previous))
        {
            wcscpy(previous, title);
            wprintf(L"foreground: %ls\n", title);
            fflush(stdout);
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
