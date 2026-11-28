/* SHRegSetPathA and SHRegSetPathW store the whole path, with its terminator.
 * XP's shimgvw registers the image types' icons and commands with them. Prints
 * PASS and exits 0, or says what was stored. Writes and removes one key of the
 * current user. */
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <wchar.h>

#define KEY L"Software\\win4wine-shreg-test"

static int check(const char *call, const WCHAR *path, LONG result)
{
    WCHAR data[MAX_PATH] = L"";
    DWORD size = sizeof(data), error;
    /* Windows may store it with an environment variable: compare it expanded. */
    error = RegGetValueW(HKEY_CURRENT_USER, KEY, L"path", RRF_RT_REG_SZ, NULL, data, &size);
    if (!result && !error && !wcsicmp(data, path)) return 0;
    printf("FAIL %s: result %ld, read %lu, %lu bytes, '%ls'\n", call, result, error, size, data);
    return 1;
}

int main(void)
{
    static const WCHAR path[] = L"shimgvw.dll,1";
    int failed = 0;
    failed |= check("SHRegSetPathW", path, SHRegSetPathW(HKEY_CURRENT_USER, KEY, L"path", path, 0));
    RegDeleteKeyW(HKEY_CURRENT_USER, KEY);
    failed |= check("SHRegSetPathA", path, SHRegSetPathA(HKEY_CURRENT_USER, "Software\\win4wine-shreg-test",
                                                         "path", "shimgvw.dll,1", 0));
    RegDeleteKeyW(HKEY_CURRENT_USER, KEY);
    if (!failed) puts("PASS");
    return failed;
}
