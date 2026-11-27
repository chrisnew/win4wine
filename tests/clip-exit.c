/* The cursor clip of a program that ends while it holds it is released.
 *
 * A child process takes the foreground with a window over the whole screen,
 * clips the cursor and is ended without releasing the clip, as a game that
 * crashes or is killed. Afterwards the clip must be the whole screen again.
 * With Wine 0166 missing, the old rectangle stays, and under X11 so does the
 * invisible clipping window, which swallows every click on the desktop.
 *
 *   clip-exit-test.exe            both cases: a child that is killed and one
 *                                 that exits on its own
 *
 * Run it with nothing else taking the foreground. Prints PASS or FAIL.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int child(const char *mode, const char *event_name) {
    WNDCLASSA wc = {0};
    HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name);
    HWND window;
    RECT clip = {100, 100, 300, 300}, now;
    MSG msg;
    DWORD end;

    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "ClipExitTest";
    RegisterClassA(&wc);
    window = CreateWindowExA(0, wc.lpszClassName, "clip-exit", WS_POPUP | WS_VISIBLE, 0, 0,
                             GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 0, 0, wc.hInstance, 0);
    SetForegroundWindow(window);
    /* Give the window system time to map the window and give it the keyboard. */
    for (end = GetTickCount() + 2000; GetTickCount() < end; Sleep(20))
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    if (GetForegroundWindow() != window || !ClipCursor(&clip)) return 2;
    for (end = GetTickCount() + 500; GetTickCount() < end; Sleep(20))
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    if (!GetClipCursor(&now) || !EqualRect(&now, &clip)) return 3;
    SetEvent(event);
    if (!strcmp(mode, "exit")) ExitProcess(0);
    Sleep(INFINITE);
    return 0;
}

static int run(const char *mode) {
    char path[MAX_PATH], command[MAX_PATH + 128], name[64];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE event, handles[2];
    RECT before, after;
    DWORD wait, code = 0;
    BOOL pass;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    ClipCursor(NULL);
    GetClipCursor(&before);
    sprintf(name, "clip-exit-%lu-%s", GetCurrentProcessId(), mode);
    event = CreateEventA(NULL, TRUE, FALSE, name);
    GetModuleFileNameA(NULL, path, sizeof(path));
    sprintf(command, "\"%s\" %s %s", path, mode, name);
    if (!CreateProcessA(path, command, 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
        printf("FAIL %s: cannot start the child (%lu)\n", mode, GetLastError());
        return 1;
    }
    handles[0] = event;
    handles[1] = pi.hProcess;
    wait = WaitForMultipleObjects(2, handles, FALSE, 20000);
    if (WaitForSingleObject(event, 0) != WAIT_OBJECT_0) {
        if (wait != WAIT_TIMEOUT) GetExitCodeProcess(pi.hProcess, &code);
        TerminateProcess(pi.hProcess, 1);
        printf("FAIL %s: the child could not clip the cursor (exit code %lu)\n", mode, code);
        return 1;
    }
    if (!strcmp(mode, "kill")) TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 10000);
    Sleep(1000);
    GetClipCursor(&after);
    pass = EqualRect(&before, &after);
    printf("%s %s: clip %ld,%ld-%ld,%ld after the child ended, %ld,%ld-%ld,%ld before\n", pass ? "PASS" : "FAIL",
           mode, after.left, after.top, after.right, after.bottom,
           before.left, before.top, before.right, before.bottom);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(event);
    return !pass;
}

int main(int argc, char **argv) {
    int failed;
    if (argc == 3) return child(argv[1], argv[2]);
    failed = run("kill");
    failed += run("exit");
    ClipCursor(NULL);
    return failed;
}
