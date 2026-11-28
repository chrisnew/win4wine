/* Times reading the security of every entry of a folder, which Wine 0120 makes
 * read an extended attribute as well.
 *
 *   security-time-test.exe [DIR [ROUNDS]]     DIR defaults to C:\windows\system32
 *
 * Prints the entries and the best round's time in total and per entry. Compare
 * a wineserver started with WIN4WINE_EXTERNAL_SHELL=1 (0120 acts) with one
 * started without it. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int wmain(int argc, WCHAR **argv)
{
    const WCHAR *dir = argc > 1 ? argv[1] : L"C:\\windows\\system32";
    int rounds = argc > 2 ? _wtoi(argv[2]) : 5, round;
    static WCHAR names[8192][MAX_PATH];
    static BYTE sd[8192];
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    LARGE_INTEGER frequency, start, end;
    double best = 0;
    int count = 0, failed = 0, i;
    HANDLE find;

    swprintf(pattern, MAX_PATH, L"%ls\\*", dir);
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) { wprintf(L"%ls: error %lu\n", dir, GetLastError()); return 1; }
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
        swprintf(names[count++], MAX_PATH, L"%ls\\%ls", dir, data.cFileName);
    } while (count < 8192 && FindNextFileW(find, &data));
    FindClose(find);

    QueryPerformanceFrequency(&frequency);
    for (round = 0; round < rounds; round++) {
        DWORD needed;
        double ms;

        failed = 0;
        QueryPerformanceCounter(&start);
        for (i = 0; i < count; i++)
            if (!GetFileSecurityW(names[i], OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, sd, sizeof(sd), &needed))
                failed++;
        QueryPerformanceCounter(&end);
        ms = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
        if (!round || ms < best) best = ms;
    }
    wprintf(L"%ls: %d entries, %d failed, best of %d: %.1f ms, %.1f us each\n", dir, count, failed, rounds, best,
            count ? best * 1000 / count : 0);
    return failed != 0;
}
