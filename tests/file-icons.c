/* The icon Explorer shows for each file type: for every extension registered in
 * HKCR (or those given), the type name, the ProgID, its DefaultIcon and
 * whether that icon loads. "blank" marks a type that shows the unknown file's
 * icon. Run it as startmenu.exe, so that XP's shell32 answers.
 * Usage: startmenu.exe OUTPUT [.ext ...]   (tab-separated, UTF-8) */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

static int icon_index(const WCHAR *extension, WCHAR *type)
{
    WCHAR name[MAX_PATH];
    SHFILEINFOW info = {0};
    _snwprintf(name, ARRAYSIZE(name) - 1, L"x%ls", extension);
    name[ARRAYSIZE(name) - 1] = 0;
    if (!SHGetFileInfoW(name, FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                        SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_TYPENAME))
        return -1;
    if (type) wcscpy(type, info.szTypeName);
    return info.iIcon;
}

static BOOL value(const WCHAR *key, WCHAR *data, DWORD size)
{
    return !RegGetValueW(HKEY_CLASSES_ROOT, key, NULL, RRF_RT_REG_SZ, NULL, data, &size);
}

/* "yes" when FILE,INDEX of a DefaultIcon value loads, "per file" for %1. */
static const char *loads(const WCHAR *location)
{
    WCHAR path[MAX_PATH], found[MAX_PATH], *comma, *start = path;
    int index = 0;
    HICON icon = NULL;
    if (!*location) return "-";
    if (wcsstr(location, L"%1")) return "per file";
    wcsncpy(path, location, ARRAYSIZE(path) - 1);
    path[ARRAYSIZE(path) - 1] = 0;
    if ((comma = wcsrchr(path, ','))) { *comma = 0; index = _wtoi(comma + 1); }
    if (*start == '"') { start++; if (wcschr(start, '"')) *wcschr(start, '"') = 0; }
    if (!SearchPathW(NULL, start, NULL, ARRAYSIZE(found), found, NULL)) return "no file";
    if (ExtractIconExW(found, index, &icon, NULL, 1) != 1 || !icon) return "no icon";
    DestroyIcon(icon);
    return "yes";
}

static void report(FILE *out, const WCHAR *extension, int unknown)
{
    WCHAR type[80] = L"", progid[260] = L"", key[600], location[MAX_PATH] = L"";
    int index = icon_index(extension, type);
    value(extension, progid, sizeof(progid));
    _snwprintf(key, ARRAYSIZE(key) - 1, L"%ls\\DefaultIcon", *progid ? progid : extension);
    key[ARRAYSIZE(key) - 1] = 0;
    value(key, location, sizeof(location));
    fwprintf(out, L"%ls\t%ls\t%ls\t%ls\t%ls\t%hs\n", extension, index == unknown ? L"blank" : L"icon",
             type, progid, location, loads(location));
}

int wmain(int argc, WCHAR **argv)
{
    WCHAR name[260];
    DWORD size;
    FILE *out;
    int unknown;
    if (argc < 2 || !(out = _wfopen(argv[1], L"w, ccs=UTF-8"))) return 2;
    CoInitialize(NULL);
    unknown = icon_index(L".win4wine-unknown", NULL);
    if (argc > 2)
        for (int i = 2; i < argc; i++) report(out, argv[i], unknown);
    else
        for (DWORD i = 0; size = ARRAYSIZE(name),
             !RegEnumKeyExW(HKEY_CLASSES_ROOT, i, name, &size, NULL, NULL, NULL, NULL); i++)
            if (name[0] == '.') report(out, name, unknown);
    return fclose(out) ? 1 : 0;
}
