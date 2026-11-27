/* Linux columns in XP's folder windows: permissions, owner, group and Unix
 * path, chosen in Details view like any other column.
 *
 * A column handler (IColumnProvider) that XP's own shell32 loads from
 * HKCR\Folder\shellex\ColumnHandlers (prefix/extras.py registers it). It only
 * reads. XP asks for the columns from its background task (SHCOLSTATE_SLOW),
 * so a slow disk delays the columns, not the folder.
 *
 * Mode, user and group come from NtQueryInformationByName's
 * FileStatLxInformation (Windows 10; Wine patch 0160), which opens nothing.
 * Without it only the path is filled in. No item is ever opened: on Wine,
 * opening a file makes the wineserver open it, which for a device can have
 * side effects and on a hung mount stops every Windows program. That is why
 * the path is not asked of wine_get_unix_file_name() item by item (it opens
 * the file): only a drive's root is, and the rest of the path is appended, in
 * the spelling the folder window gives it.
 *
 * HKCU\Software\win4wine\LinuxColumns, "Debug" = 1 reports the calls through
 * OutputDebugStringW (WINEDEBUG=warn+seh; read when XP loads the handler).
 *
 * The functions above the _WIN32 part are plain C, for tests/linuxcols.py. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* st_mode as "ls -l" writes it, e.g. "drwxr-xr-x". */
static void format_mode(unsigned int mode, char out[11])
{
    unsigned int i;
    switch (mode & 0170000)
    {
    case 0140000: out[0] = 's'; break;
    case 0120000: out[0] = 'l'; break;
    case 0100000: out[0] = '-'; break;
    case 0060000: out[0] = 'b'; break;
    case 0040000: out[0] = 'd'; break;
    case 0020000: out[0] = 'c'; break;
    case 0010000: out[0] = 'p'; break;
    default:      out[0] = '?'; break;
    }
    for (i = 0; i < 9; i++) out[i + 1] = (mode & (0400 >> i)) ? "rwxrwxrwx"[i] : '-';
    /* set user ID, set group ID and sticky, capital without the x under them */
    if (mode & 04000) out[3] = (mode & 0100) ? 's' : 'S';
    if (mode & 02000) out[6] = (mode & 0010) ? 's' : 'S';
    if (mode & 01000) out[9] = (mode & 0001) ? 't' : 'T';
    out[10] = 0;
}

/* The name of a user or group in the text of /etc/passwd or /etc/group (both
 * "name:password:id:..."). Returns 0 when the ID has no line there. */
static int find_name(const char *text, unsigned int id, char *name, size_t size)
{
    const char *line = text, *next, *colon, *number;
    char *end;
    unsigned long value;

    for (; line && *line; line = next)
    {
        next = strchr(line, '\n');
        if (next) next++;
        if (*line == '#' || *line == '+' || *line == '-') continue;
        if (!(colon = strchr(line, ':')) || (next && colon >= next)) continue;
        if (!(number = strchr(colon + 1, ':')) || (next && number >= next)) continue;
        number++;
        if (*number < '0' || *number > '9') continue;
        value = strtoul(number, &end, 10);
        if (*end != ':' || value != id) continue;
        if (colon == line || (size_t)(colon - line) >= size) return 0;
        memcpy(name, line, colon - line);
        name[colon - line] = 0;
        return 1;
    }
    return 0;
}

static int same_ignoring_case(const char *a, const char *b, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
        if (!x) break;
    }
    return 1;
}

/* Unix paths the columns leave alone: /proc, and the GVFS mounts, where one
 * stat() can wait for a server that is gone. Case is ignored, as Windows
 * paths ignore it. */
static int skip_path(const char *path)
{
    const char *p;
    if (same_ignoring_case(path, "/proc/", 6)) return 1;
    if (!same_ignoring_case(path, "/run/user/", 10)) return 0;
    p = path + 10;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') p++;
    return same_ignoring_case(p, "/gvfs", 5) && (!p[5] || p[5] == '/');
}

/* Whether the Details section may wrap a path after its character "c", the one
 * after "previous" (0 at the start): the narrow pane wraps its lines at spaces
 * only, and cuts off what does not fit. After the separators of a path and a
 * file name, but not the dot of a hidden file; and after 14 characters without
 * such a place, though not within a surrogate pair. "run" counts them, from 0. */
static int wraps_after(unsigned int c, unsigned int previous, unsigned int *run)
{
    if (c == ' ')
    {
        *run = 0;
        return 0;
    }
    ++*run;
    if (c == '/' || c == '_' || c == '-' || (c == '.' && previous != '/') ||
        (*run >= 14 && (c < 0xd800 || c > 0xdbff)))
    {
        *run = 0;
        return 1;
    }
    return 0;
}

#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <ole2.h>
#include <shlobj.h>
#include <shlguid.h>
#include <wchar.h>

static const GUID CLSID_LinuxColumns =
    {0x48e91554, 0xb8e3, 0x45ce, {0xa5, 0xc9, 0xe7, 0xe6, 0x5e, 0x65, 0x4f, 0xf0}};
static const GUID FMTID_LinuxColumns =
    {0x9efe7827, 0xd3d3, 0x4e38, {0xbe, 0x40, 0xbb, 0xff, 0x8b, 0x12, 0xf6, 0x57}};

enum column { COLUMN_PERMISSIONS, COLUMN_OWNER, COLUMN_GROUP, COLUMN_PATH, COLUMN_COUNT };
#define FIRST_PID 2  /* property IDs 0 and 1 are reserved */

/* After the four columns come the same four once more, hidden, for the Details
 * section of the task pane (prefix/extras.py lists them): XP shows a property
 * it does not know without a name, so their values bring one along. */
#define DETAILS_COUNT (2 * COLUMN_COUNT)

static const struct
{
    const WCHAR *title, *description, *label;
    UINT chars;
} columns[COLUMN_COUNT] =
{
    {L"Linux Permissions", L"File type and permissions, as ls -l shows them.", L"Permissions: ", 20},
    {L"Linux Owner", L"The user the file belongs to.", L"Owner: ", 15},
    {L"Linux Group", L"The group the file belongs to.", L"Group: ", 15},
    {L"Linux Path", L"The file's path in Linux.", L"Linux Path: ", 40},
};

/* FILE_STAT_LX_INFORMATION (the WDK's ntifs.h). */
struct stat_lx
{
    LARGE_INTEGER id, creation_time, access_time, write_time, change_time, allocation_size, end_of_file;
    ULONG attributes, reparse_tag, links, effective_access;
    ULONG flags, uid, gid, mode, device_major, device_minor;
};
#define STAT_LX_CLASS 70  /* FileStatLxInformation */
#define LX_HAS_UID 1
#define LX_HAS_GID 2
#define LX_HAS_MODE 4

typedef NTSTATUS (NTAPI *query_by_name)(OBJECT_ATTRIBUTES *, IO_STATUS_BLOCK *, void *, ULONG, int);
typedef char *(CDECL *unix_file_name)(const WCHAR *);

static LONG references;
static BOOL debug;
static query_by_name query_information;
static unix_file_name get_unix_file_name;

/* XP's background task asks for items from its own thread while the view calls
 * Initialize. The lock is held around the caches only, never over the file
 * system. */
static CRITICAL_SECTION lock;
static unsigned int generation = 1;  /* one more with every Initialize */

static struct
{
    BOOL valid;
    WCHAR path[MAX_PATH];
    struct stat_lx info;
} last;

static struct names
{
    const WCHAR *file;
    char *text;
    FILETIME written;
    unsigned int checked;
} users = {L"\\\\?\\unix\\etc\\passwd", NULL, {0, 0}, 0},
  groups = {L"\\\\?\\unix\\etc\\group", NULL, {0, 0}, 0};

static struct
{
    unsigned int checked;
    char directory[1024];
    BOOL known;
} drives[26];

static void report(const WCHAR *format, ...)
{
    WCHAR line[600];
    va_list args;
    if (!debug) return;
    va_start(args, format);
    wvsprintfW(line, format, args);
    va_end(args);
    OutputDebugStringW(line);
}

/* The whole of a small text file, or NULL. */
static char *read_text(const WCHAR *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, read = 0;
    char *text = NULL;
    if (file == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(file, NULL);
    if (size < 4 * 1024 * 1024 && (text = malloc(size + 1)))
    {
        if (ReadFile(file, text, size, &read, NULL)) text[read] = 0;
        else
        {
            free(text);
            text = NULL;
        }
    }
    CloseHandle(file);
    return text;
}

/* The name of a user or group, or the number if it has none. The file is read
 * again when it changed, looked at no more than once per Initialize. Users
 * that only NSS knows show as numbers. */
static void owner_name(struct names *names, ULONG id, char *out, size_t size)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {0};
    unsigned int current;
    BOOL check, found = FALSE;

    EnterCriticalSection(&lock);
    current = generation;
    check = names->checked != current;
    LeaveCriticalSection(&lock);

    if (check)
    {
        char *text = NULL;
        BOOL changed = FALSE;
        if (GetFileAttributesExW(names->file, GetFileExInfoStandard, &data))
        {
            EnterCriticalSection(&lock);
            changed = !names->text || CompareFileTime(&data.ftLastWriteTime, &names->written);
            LeaveCriticalSection(&lock);
            if (changed) text = read_text(names->file);
        }
        EnterCriticalSection(&lock);
        if (text)
        {
            free(names->text);
            names->text = text;
            names->written = data.ftLastWriteTime;
        }
        names->checked = current;
        LeaveCriticalSection(&lock);
    }

    EnterCriticalSection(&lock);
    if (names->text) found = find_name(names->text, id, out, size);
    LeaveCriticalSection(&lock);

    if (!found) snprintf(out, size, "%lu", id);
}

/* The Unix directory of a drive, without a trailing slash (so empty for "/").
 * Wine's server resolves the drive's link in dosdevices. */
static BOOL query_drive(WCHAR letter, char *directory, size_t size)
{
    WCHAR root_name[] = {letter, ':', '\\', 0};
    char *root;
    size_t length;
    BOOL ok;

    if (!get_unix_file_name || !(root = get_unix_file_name(root_name))) return FALSE;
    length = strlen(root);
    while (length && root[length - 1] == '/') length--;
    if ((ok = length < size))
    {
        memcpy(directory, root, length);
        directory[length] = 0;
    }
    HeapFree(GetProcessHeap(), 0, root);
    return ok;
}

/* The Unix path of "X:\...", without opening the item. Free it with free(). */
static char *unix_path(const WCHAR *path)
{
    unsigned int index = (path[0] | 0x20) - 'a', current;
    char directory[sizeof(drives[0].directory)];
    const WCHAR *rest = path + 2;
    BOOL check, known;
    char *result, *p;
    size_t length;
    int bytes;

    EnterCriticalSection(&lock);
    current = generation;
    check = drives[index].checked != current;
    known = drives[index].known;
    memcpy(directory, drives[index].directory, sizeof(directory));
    LeaveCriticalSection(&lock);

    if (check)
    {
        known = query_drive(path[0], directory, sizeof(directory));
        EnterCriticalSection(&lock);
        drives[index].checked = current;
        drives[index].known = known;
        if (known) memcpy(drives[index].directory, directory, sizeof(directory));
        LeaveCriticalSection(&lock);
    }
    if (!known) return NULL;
    /* A drive's root: the directory itself. */
    if (!rest[1] && directory[0]) rest++;

    bytes = WideCharToMultiByte(CP_UTF8, 0, rest, -1, NULL, 0, NULL, NULL);
    length = strlen(directory);
    if (!bytes || !(result = malloc(length + bytes))) return NULL;
    memcpy(result, directory, length);
    WideCharToMultiByte(CP_UTF8, 0, rest, -1, result + length, bytes, NULL, NULL);
    for (p = result + length; *p; p++) if (*p == '\\') *p = '/';
    return result;
}

/* The item's mode and owner; one question for its three columns. */
static BOOL item_info(const WCHAR *path, BOOL update, struct stat_lx *info)
{
    WCHAR nt_path[MAX_PATH + 4];
    OBJECT_ATTRIBUTES attributes;
    UNICODE_STRING name;
    IO_STATUS_BLOCK io;
    NTSTATUS status;
    BOOL cached;

    if (!query_information) return FALSE;
    EnterCriticalSection(&lock);
    cached = last.valid && !wcscmp(last.path, path);
    if (cached && update) cached = last.valid = FALSE;
    if (cached) *info = last.info;
    LeaveCriticalSection(&lock);
    if (cached) return TRUE;

    /* Not RtlDosPathNameToNtPathName_U, which takes a file named "nul" or
     * "aux" for the device and drops trailing dots. */
    wcscpy(nt_path, L"\\??\\");
    wcscat(nt_path, path);
    name.Buffer = nt_path;
    name.Length = wcslen(nt_path) * sizeof(WCHAR);
    name.MaximumLength = name.Length + sizeof(WCHAR);
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = query_information(&attributes, &io, info, sizeof(*info), STAT_LX_CLASS);
    if (status)
    {
        report(L"linuxcols: %s: status %08x", path, status);
        return FALSE;
    }
    EnterCriticalSection(&lock);
    last.valid = TRUE;
    wcscpy(last.path, path);
    last.info = *info;
    LeaveCriticalSection(&lock);
    return TRUE;
}

/* "label" and the UTF-8 text after it. In the Details section (a label) a path
 * also gets zero width spaces where it may be wrapped (wraps_after). The
 * columns' values stay as they are. */
static HRESULT return_string(VARIANT *value, const WCHAR *label, const char *text)
{
    int prefix = wcslen(label), length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    int i, out = 0;
    unsigned int run = 0;
    BOOL path;
    WCHAR *wide, *line;
    BSTR string;

    if (!length || !(wide = malloc(length * sizeof(WCHAR)))) return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length);
    length--;  /* without the terminating 0 */
    if (!(line = malloc((prefix + 2 * length + 1) * sizeof(WCHAR))))
    {
        free(wide);
        return E_OUTOFMEMORY;
    }
    path = prefix && text[0] == '/';
    memcpy(line, label, prefix * sizeof(WCHAR));
    out = prefix;
    for (i = 0; i < length; i++)
    {
        line[out++] = wide[i];
        /* not after the leading "/", nor at the end */
        if (path && wraps_after(wide[i], i ? wide[i - 1] : 0, &run) && i && i < length - 1)
            line[out++] = 0x200b;
    }
    string = SysAllocStringLen(line, out);
    free(line);
    free(wide);
    if (!string) return E_OUTOFMEMORY;
    V_VT(value) = VT_BSTR;
    V_BSTR(value) = string;
    return S_OK;
}

struct provider
{
    IColumnProvider object;
    LONG references;
};

static ULONG WINAPI provider_AddRef(IColumnProvider *iface);

static HRESULT WINAPI provider_QueryInterface(IColumnProvider *iface, REFIID riid, void **out)
{
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IColumnProvider))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    provider_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI provider_AddRef(IColumnProvider *iface)
{
    return InterlockedIncrement(&((struct provider *)iface)->references);
}

static ULONG WINAPI provider_Release(IColumnProvider *iface)
{
    ULONG left = InterlockedDecrement(&((struct provider *)iface)->references);
    if (!left)
    {
        free(iface);
        InterlockedDecrement(&references);
    }
    return left;
}

static HRESULT WINAPI provider_Initialize(IColumnProvider *iface, LPCSHCOLUMNINIT init)
{
    (void)iface;
    report(L"linuxcols: thread %u Initialize flags %x folder \"%.300s\"",
           GetCurrentThreadId(), init->dwFlags, init->wszFolder);
    /* Look at the users, groups, drives and the last item afresh. */
    EnterCriticalSection(&lock);
    generation++;
    last.valid = FALSE;
    LeaveCriticalSection(&lock);
    return S_OK;
}

static HRESULT WINAPI provider_GetColumnInfo(IColumnProvider *iface, DWORD index, SHCOLUMNINFO *info)
{
    (void)iface;
    if (index >= DETAILS_COUNT) return S_FALSE;
    memset(info, 0, sizeof(*info));
    info->scid.fmtid = FMTID_LinuxColumns;
    info->scid.pid = FIRST_PID + index;
    info->vt = VT_BSTR;
    info->fmt = LVCFMT_LEFT;
    info->csFlags = SHCOLSTATE_TYPE_STR | SHCOLSTATE_SLOW;
    if (index >= COLUMN_COUNT) info->csFlags |= SHCOLSTATE_HIDDEN;
    index %= COLUMN_COUNT;
    info->cChars = columns[index].chars;
    lstrcpynW(info->wszTitle, columns[index].title, MAX_COLUMN_NAME_LEN);
    lstrcpynW(info->wszDescription, columns[index].description, MAX_COLUMN_DESC_LEN);
    return S_OK;
}

static HRESULT WINAPI provider_GetItemData(IColumnProvider *iface, LPCSHCOLUMNID id,
                                           LPCSHCOLUMNDATA data, VARIANT *value)
{
    const WCHAR *path = data->wszFile, *label = L"";
    struct stat_lx info;
    char text[256];
    char *name;
    unsigned int column;
    HRESULT result;

    (void)iface;
    if (!IsEqualGUID(&id->fmtid, &FMTID_LinuxColumns)) return S_FALSE;
    column = id->pid - FIRST_PID;
    if (column >= DETAILS_COUNT) return S_FALSE;
    report(L"linuxcols: thread %u GetItemData column %u flags %x \"%.300s\"",
           GetCurrentThreadId(), column, data->dwFlags, path);
    if (column >= COLUMN_COUNT)
    {
        column -= COLUMN_COUNT;
        label = columns[column].label;
    }
    /* Items on a drive only: "X:\..." */
    if (wmemchr(path, 0, MAX_PATH) == NULL || (unsigned int)((path[0] | 0x20) - 'a') >= 26 ||
        path[1] != ':' || path[2] != '\\')
        return S_FALSE;

    name = unix_path(path);
    if (name && skip_path(name))
    {
        free(name);
        return S_FALSE;
    }
    if (column == COLUMN_PATH)
    {
        result = name ? return_string(value, label, name) : S_FALSE;
        free(name);
        return result;
    }
    free(name);

    if (!item_info(path, data->dwFlags & SHCDF_UPDATEITEM, &info)) return S_FALSE;
    switch (column)
    {
    case COLUMN_PERMISSIONS:
        if (!(info.flags & LX_HAS_MODE)) return S_FALSE;
        format_mode(info.mode, text);
        break;
    case COLUMN_OWNER:
        if (!(info.flags & LX_HAS_UID)) return S_FALSE;
        owner_name(&users, info.uid, text, sizeof(text));
        break;
    default:
        if (!(info.flags & LX_HAS_GID)) return S_FALSE;
        owner_name(&groups, info.gid, text, sizeof(text));
        break;
    }
    return return_string(value, label, text);
}

static IColumnProviderVtbl provider_vtbl =
{
    provider_QueryInterface,
    provider_AddRef,
    provider_Release,
    provider_Initialize,
    provider_GetColumnInfo,
    provider_GetItemData,
};

static HRESULT WINAPI factory_QueryInterface(IClassFactory *iface, REFIID riid, void **out)
{
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    return S_OK;
}

/* The factory is static. */
static ULONG WINAPI factory_AddRef(IClassFactory *iface)
{
    (void)iface;
    return 2;
}

static ULONG WINAPI factory_Release(IClassFactory *iface)
{
    (void)iface;
    return 1;
}

static HRESULT WINAPI factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out)
{
    struct provider *provider;
    HRESULT result;

    (void)iface;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(provider = malloc(sizeof(*provider)))) return E_OUTOFMEMORY;
    provider->object.lpVtbl = &provider_vtbl;
    provider->references = 1;
    InterlockedIncrement(&references);
    result = provider_QueryInterface(&provider->object, riid, out);
    provider_Release(&provider->object);
    return result;
}

static HRESULT WINAPI factory_LockServer(IClassFactory *iface, BOOL keep)
{
    (void)iface;
    if (keep) InterlockedIncrement(&references);
    else InterlockedDecrement(&references);
    return S_OK;
}

static IClassFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_CreateInstance,
    factory_LockServer,
};

static IClassFactory factory = {&factory_vtbl};

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID *out)
{
    DWORD value = 0, size = sizeof(value);
    if (!IsEqualCLSID(clsid, &CLSID_LinuxColumns))
    {
        *out = NULL;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    debug = !RegGetValueW(HKEY_CURRENT_USER, L"Software\\win4wine\\LinuxColumns", L"Debug",
                          RRF_RT_REG_DWORD, NULL, &value, &size) && value;
    return IClassFactory_QueryInterface(&factory, riid, out);
}

STDAPI DllCanUnloadNow(void)
{
    return references ? S_FALSE : S_OK;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&lock);
        query_information = (query_by_name)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                  "NtQueryInformationByName");
        get_unix_file_name = (unix_file_name)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                    "wine_get_unix_file_name");
    }
    else if (reason == DLL_PROCESS_DETACH && !reserved)
    {
        DeleteCriticalSection(&lock);
        free(users.text);
        free(groups.text);
    }
    return TRUE;
}

#endif /* _WIN32 */
