/* What Display Properties' Screen Saver tab and the session rely on: whether a
 * screen saver is active is the user's setting (Wine patch 0158), the chosen
 * screen saver written to system.ini's [boot] reaches the registry
 * (IniFileMapping, prefix/extras.py), and LockWorkStation succeeds when the
 * session can lock the screen (0159). The settings are put back.
 * Usage: screen-saver [lock]   ("lock" also calls LockWorkStation, which locks)
 * Exit status 0 when all of it holds. */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define DESKTOP_KEY L"Control Panel\\Desktop"

static int failures;

static void check(const char *what, int ok)
{
    printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    failures += !ok;
}

/* The registry's string VALUE of the desktop key, L"" if there is none. */
static void setting(const WCHAR *value, WCHAR *text, DWORD size)
{
    text[0] = 0;
    RegGetValueW(HKEY_CURRENT_USER, DESKTOP_KEY, value, RRF_RT_REG_SZ, NULL, text, &size);
}

int main(int argc, char **argv)
{
    WCHAR before[MAX_PATH], now[MAX_PATH], read[MAX_PATH];
    BOOL active = FALSE, value = FALSE;

    check("SPI_GETSCREENSAVEACTIVE", SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &active, 0));
    for (int on = 0; on <= 1; on++)
    {
        SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, on, NULL, SPIF_UPDATEINIFILE);
        SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &value, 0);
        setting(L"ScreenSaveActive", now, sizeof(now));
        check(on ? "active, in the registry too" : "inactive, in the registry too",
              !value == !on && !wcscmp(now, on ? L"1" : L"0"));
    }
    SystemParametersInfoW(SPI_SETSCREENSAVEACTIVE, active, NULL, SPIF_UPDATEINIFILE);

    setting(L"SCRNSAVE.EXE", before, sizeof(before));
    WritePrivateProfileStringW(L"boot", L"SCRNSAVE.EXE", L"C:\\windows\\system32\\probe.scr", L"system.ini");
    setting(L"SCRNSAVE.EXE", now, sizeof(now));
    check("system.ini's screen saver is the registry's", !wcscmp(now, L"C:\\windows\\system32\\probe.scr"));
    GetPrivateProfileStringW(L"boot", L"SCRNSAVE.EXE", L"", read, MAX_PATH, L"system.ini");
    check("and is read back from there", !wcscmp(read, now));
    WritePrivateProfileStringW(L"boot", L"SCRNSAVE.EXE", before[0] ? before : NULL, L"system.ini");

    if (argc > 1 && !strcmp(argv[1], "lock")) check("LockWorkStation", LockWorkStation());
    return failures != 0;
}
