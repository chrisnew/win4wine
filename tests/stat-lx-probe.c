/* NtQueryInformationByName (Wine patch 0160; Windows 10 1709): prints
 * FileStatInformation and FileStatLxInformation for each path given, and
 * what NtQueryInformationFile says through a handle for the ones that may be
 * opened. Compare with
 *
 *   stat -c '%f %u %g %t %T %s %h %i %n' PATH...
 *
 * Usage: stat-lx-probe.exe [-h PATH]... PATH...
 *   PATH is a Windows path ("Z:\usr\bin\passwd", "C:\windows", \\?\unix\...).
 *   -h PATH also asks through a handle (NtOpenFile with FILE_READ_ATTRIBUTES);
 *   not for FIFOs or devices.
 * Before the paths it checks a too short buffer, an unsupported class and a
 * name relative to a directory handle. The exit code is the number of those
 * checks that failed.
 *
 * tests/build.sh builds it (32-bit, which goes through WoW64); for the 64-bit
 * one, MINGW_CC=x86_64-w64-mingw32-gcc and another WIN4WINE_PROBES. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

struct stat_lx
{
    LARGE_INTEGER id, creation_time, access_time, write_time, change_time, allocation_size, end_of_file;
    ULONG attributes, reparse_tag, links, effective_access;
    ULONG flags, uid, gid, mode, device_major, device_minor;
};
#define STAT_SIZE offsetof(struct stat_lx, flags)
#define STAT_CLASS 68
#define STAT_LX_CLASS 70

typedef NTSTATUS (NTAPI *by_name)(OBJECT_ATTRIBUTES *, IO_STATUS_BLOCK *, void *, ULONG, int);
typedef NTSTATUS (NTAPI *by_handle)(HANDLE, IO_STATUS_BLOCK *, void *, ULONG, int);
typedef NTSTATUS (NTAPI *open_file)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, IO_STATUS_BLOCK *, ULONG, ULONG);

static by_name query_name;
static by_handle query_handle;
static open_file nt_open;
static int failures;

/* "\??\Z:\..." from a DOS path, or "\??\..." as given after "\\?\". */
static void nt_name(const WCHAR *path, WCHAR *buffer, UNICODE_STRING *name)
{
    if (!wcsncmp(path, L"\\\\?\\", 4)) path += 4;
    swprintf(buffer, 1024, L"\\??\\%ls", path);
    name->Buffer = buffer;
    name->Length = wcslen(buffer) * sizeof(WCHAR);
    name->MaximumLength = name->Length + sizeof(WCHAR);
}

static NTSTATUS ask(HANDLE root, UNICODE_STRING *name, struct stat_lx *info, ULONG size, int class,
                    ULONG_PTR *returned)
{
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK io = {0};
    NTSTATUS status;

    InitializeObjectAttributes(&attributes, name, OBJ_CASE_INSENSITIVE, root, NULL);
    memset(info, 0xcc, sizeof(*info));
    status = query_name(&attributes, &io, info, size, class);
    if (returned) *returned = io.Information;
    return status;
}

static void show(const char *what, const struct stat_lx *info, int lx)
{
    printf("  %s attributes %08lx tag %08lx links %lu size %lld id %llu write %lld", what,
           info->attributes, info->reparse_tag, info->links, (long long)info->end_of_file.QuadPart,
           (unsigned long long)info->id.QuadPart, (long long)info->write_time.QuadPart);
    if (lx)
        printf("\n  %*s flags %lx mode %lx (%lo) uid %lu gid %lu device %lx,%lx", (int)strlen(what), "",
               info->flags, info->mode, info->mode, info->uid, info->gid,
               info->device_major, info->device_minor);
    printf("\n");
}

static void check(const char *what, NTSTATUS status, NTSTATUS expected)
{
    printf("%-34s status %08lx %s\n", what, status, status == expected ? "ok" : "FAILED");
    if (status != expected) failures++;
}

static void checks(void)
{
    WCHAR buffer[1024];
    UNICODE_STRING name, relative;
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK io;
    struct stat_lx info, other;
    ULONG_PTR returned;
    HANDLE directory;
    NTSTATUS status;

    nt_name(L"C:\\windows", buffer, &name);
    status = ask(NULL, &name, &info, sizeof(info), STAT_LX_CLASS, &returned);
    check("FileStatLxInformation", status, 0);
    check("  Information is its size", returned == sizeof(info) ? 0 : 1, 0);
    status = ask(NULL, &name, &info, STAT_SIZE, STAT_CLASS, &returned);
    check("FileStatInformation", status, 0);
    check("  Information is its size", returned == STAT_SIZE ? 0 : 1, 0);
    check("too short for LX", ask(NULL, &name, &info, sizeof(info) - 1, STAT_LX_CLASS, NULL),
          (NTSTATUS)0xc0000004);
    check("too short for stat", ask(NULL, &name, &info, STAT_SIZE - 1, STAT_CLASS, NULL),
          (NTSTATUS)0xc0000004);
    check("unsupported class", ask(NULL, &name, &info, sizeof(info), 4 /* FileBasicInformation */, NULL),
          (NTSTATUS)0xc0000003);
    nt_name(L"C:\\windows\\no such file", buffer, &name);
    check("missing file", ask(NULL, &name, &info, sizeof(info), STAT_LX_CLASS, NULL),
          (NTSTATUS)0xc0000034);
    nt_name(L"C:\\no such directory\\file", buffer, &name);
    check("missing directory", ask(NULL, &name, &info, sizeof(info), STAT_LX_CLASS, NULL),
          (NTSTATUS)0xc000003a);
    nt_name(L"C:\\WINDOWS\\SYSTEM.INI", buffer, &name);
    check("name in the wrong case", ask(NULL, &name, &info, sizeof(info), STAT_LX_CLASS, NULL), 0);

    /* relative to a directory handle, compared with the full name */
    nt_name(L"C:\\windows", buffer, &name);
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = nt_open(&directory, FILE_LIST_DIRECTORY | SYNCHRONIZE, &attributes, &io,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     0x21 /* FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT */);
    check("open C:\\windows", status, 0);
    if (status) return;
    relative.Buffer = (WCHAR *)L"system.ini";
    relative.Length = 10 * sizeof(WCHAR);
    relative.MaximumLength = relative.Length + sizeof(WCHAR);
    status = ask(directory, &relative, &other, sizeof(other), STAT_LX_CLASS, NULL);
    check("relative name", status, 0);
    nt_name(L"C:\\windows\\system.ini", buffer, &name);
    ask(NULL, &name, &info, sizeof(info), STAT_LX_CLASS, NULL);
    check("  the same file", info.id.QuadPart == other.id.QuadPart && info.mode == other.mode ? 0 : 1, 0);
    CloseHandle(directory);
}

int wmain(int argc, WCHAR **argv)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    WCHAR buffer[1024];
    UNICODE_STRING name;
    struct stat_lx info;
    NTSTATUS status;
    int i, handle;

    query_name = (by_name)(void *)GetProcAddress(ntdll, "NtQueryInformationByName");
    query_handle = (by_handle)(void *)GetProcAddress(ntdll, "NtQueryInformationFile");
    nt_open = (open_file)(void *)GetProcAddress(ntdll, "NtOpenFile");
    printf("%u-bit probe\n", (unsigned int)(sizeof(void *) * 8));
    if (!query_name)
    {
        printf("no NtQueryInformationByName\n");
        return 100;
    }
    checks();
    for (i = 1; i < argc; i++)
    {
        handle = !wcscmp(argv[i], L"-h") && i + 1 < argc;
        if (handle) i++;
        nt_name(argv[i], buffer, &name);
        printf("%ls\n", argv[i]);
        status = ask(NULL, &name, &info, sizeof(info), STAT_LX_CLASS, NULL);
        if (status) printf("  LX     status %08lx\n", status);
        else show("LX    ", &info, 1);
        status = ask(NULL, &name, &info, STAT_SIZE, STAT_CLASS, NULL);
        if (status) printf("  stat   status %08lx\n", status);
        else show("stat  ", &info, 0);
        if (handle)
        {
            OBJECT_ATTRIBUTES attributes;
            IO_STATUS_BLOCK io;
            HANDLE file;

            InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
            status = nt_open(&file, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes, &io,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             0x20 /* FILE_SYNCHRONOUS_IO_NONALERT */);
            if (status) printf("  handle open status %08lx\n", status);
            else
            {
                memset(&info, 0xcc, sizeof(info));
                status = query_handle(file, &io, &info, sizeof(info), STAT_LX_CLASS);
                if (status) printf("  handle LX status %08lx\n", status);
                else show("handle", &info, 1);
                CloseHandle(file);
            }
        }
        fflush(stdout);
    }
    printf("%d failed checks\n", failures);
    return failures;
}
