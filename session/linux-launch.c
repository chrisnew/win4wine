/* Start a Linux program from an XP Start menu shortcut.
 * Usage: linux-launch.exe PROGRAM [ARGUMENTS...], quoted as a Windows command line.
 * A GUI-subsystem program, so that starting it opens no console. Wine runs a
 * Unix executable named in CreateProcess natively; /usr/bin/env receives the
 * Windows form of its own path as argv[0] and looks PROGRAM up in PATH. The
 * shortcut's working directory becomes the program's. Public Win32 APIs only.
 *
 * linux-launch.exe --unreachable NAME starts nothing: it says, as XP does, that
 * the computer NAME is not accessible (session/network_places.py's shortcuts of
 * computers with nothing to open).
 *
 * linux-launch.exe --terminal PATH opens a Linux terminal in the folder PATH,
 * XP's "Open in Linux Terminal" (the context menu handler in
 * xp-explorer/helpers/linuxcols.c). PATH
 * is the rest of the command line, with or without quotes around it: by the
 * rules of CommandLineToArgvW the closing quote of "C:\" would be escaped.
 * The folder becomes the terminal's working directory and nothing else, so its
 * name reaches no shell. A folder the terminal could not start in gets a
 * message box instead: Wine would start it in another directory.
 *
 * linux-launch.exe --mount-iso PATH mounts the disc image PATH, "Mount" on .iso
 * files (prefix/extras.py), and opens the CD drive Wine makes of it, unless the
 * disc was mounted just now and AutoPlay takes it from there (xptray.exe starts
 * autoplay.exe for it). PATH is the
 * rest of the command line, as for --terminal. iso_mount.py beside this program
 * (session/iso_mount.py) asks UDisks2 and writes what came of it into a file;
 * Wine's mountmgr.sys, which follows UDisks2, gives the disc a drive letter. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* The command line after the first argument, by the rules CommandLineToArgvW uses for it. */
static WCHAR *arguments(WCHAR *line)
{
    BOOL quoted = FALSE;
    for (; *line && (quoted || (*line != ' ' && *line != '\t')); line++)
        if (*line == '"') quoted = !quoted;
    while (*line == ' ' || *line == '\t') line++;
    return line;
}

static int unreachable(const WCHAR *name)
{
    WCHAR title[96], message[512];
    _snwprintf(title, 96, L"\\\\%ls", name);
    title[95] = 0;
    _snwprintf(message, 512, L"%ls is not accessible. You might not have permission to use this network "
               L"resource. Contact the administrator of this server to find out if you have access "
               L"permissions.\n\nThe network path was not found.", title);
    message[511] = 0;
    MessageBoxW(NULL, message, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 0;
}

/* Run LINE, a command of /usr/bin/env, in DIRECTORY (NULL: this one). */
static int start(WCHAR *line, const WCHAR *directory)
{
    static const WCHAR env[] = L"Z:\\usr\\bin\\env";
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(env, line, NULL, NULL, FALSE, 0, NULL, directory, &startup, &process))
    {
        WCHAR message[160];
        DWORD error = GetLastError();
        _snwprintf(message, 160, L"The Linux program could not be started (error %lu).", error);
        message[159] = 0;
        MessageBoxW(NULL, message, L"Linux", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

static int refuse(const WCHAR *path, const WCHAR *reason)
{
    WCHAR message[640];
    _snwprintf(message, 640, L"%.400ls %ls, so it cannot be opened in a Linux terminal.", path, reason);
    message[639] = 0;
    MessageBoxW(NULL, message, L"Open in Linux Terminal", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 1;
}

/* Whether Wine would give a Unix program another working directory than PATH:
 * it drops a dot at the end of a folder's name, and spaces and dots at the end
 * of the path ("." and ".." mean what they say). */
static BOOL changed_by_wine(const WCHAR *path)
{
    const WCHAR *part = path, *end;
    size_t length = wcslen(path);

    if (length && path[length - 1] == ' ') return TRUE;
    for (; *part; part = *end ? end + 1 : end)
    {
        for (end = part; *end && *end != '\\' && *end != '/'; end++) {}
        if (end == part || end[-1] != '.') continue;
        if (end - part == 1 || (end - part == 2 && part[0] == '.')) continue;
        return TRUE;
    }
    return FALSE;
}

/* Whether the Unix program can start in the folder (shorter than MAX_PATH).
 * To look a name up, Wine changes to the folder as it does for the program, so
 * a folder that cannot be entered denies the access. Opening it is not enough:
 * one that may be listed but not entered opens. */
static BOOL can_enter(const WCHAR *folder)
{
    WIN32_FIND_DATAW data;
    WCHAR name[MAX_PATH + 16];
    HANDLE find;

    wcscpy(name, folder);
    if (name[wcslen(name) - 1] != '\\') wcscat(name, L"\\");
    wcscat(name, L"w4w-probe");
    if ((find = FindFirstFileW(name, &data)) == INVALID_HANDLE_VALUE) return GetLastError() != ERROR_ACCESS_DENIED;
    FindClose(find);
    return TRUE;
}

/* PATH, the rest of the command line, without the blanks at its end and the quotes around it. */
static WCHAR *unquoted(WCHAR *path)
{
    size_t length = wcslen(path);

    while (length && (path[length - 1] == ' ' || path[length - 1] == '\t')) path[--length] = 0;
    if (length >= 2 && path[0] == '"' && path[length - 1] == '"')
    {
        path[length - 1] = 0;
        path++;
    }
    return path;
}

typedef char *(CDECL *unix_file_name)(const WCHAR *);

/* The Unix name of the Windows path, to be freed with HeapFree, or NULL. */
static char *unix_name(const WCHAR *path)
{
    unix_file_name get = (unix_file_name)(void *)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name");
    return get ? get(path) : NULL;
}

static int terminal(WCHAR *path)
{
    /* The session's debug channels and font configuration are not for the
     * user's shell. Its Wine variables stay, so that "wine" there starts
     * programs as the shell does (session/wine.py). */
    static const WCHAR clean[] = L"/usr/bin/env -u WINEDEBUG -u FONTCONFIG_FILE ";
    WCHAR folder[MAX_PATH], line[96];
    DWORD size, attributes;
    char *name;

    path = unquoted(path);
    if (!*path) return 2;
    if (changed_by_wine(path))
        return refuse(path, L"has a name that ends with a dot or a space");
    /* A folder on a drive: Wine can start a Unix program in no other. */
    size = GetFullPathNameW(path, MAX_PATH, folder, NULL);
    if (!size || size >= MAX_PATH || folder[1] != ':')
        return refuse(path, L"is not on the Linux file system");
    attributes = GetFileAttributesW(folder);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        !(name = unix_name(folder)))
        return refuse(path, L"is not on the Linux file system");
    HeapFree(GetProcessHeap(), 0, name);
    if (!can_enter(folder))
        return refuse(path, L"does not let you in");

    wcscpy(line, clean);
    wcscat(line, GetFileAttributesW(L"Z:\\usr\\bin\\x-terminal-emulator") != INVALID_FILE_ATTRIBUTES ?
                 L"x-terminal-emulator" : L"xterm");
    return start(line, folder);
}

/* How long XP's Explorer gets, after a new drive has its letter, to take it in
 * before a window of it is asked for. A window asked for too early shows "D:\"
 * without the disc's label and an empty address bar, and keeps them. On the
 * EeePC 0.3 s and 0.8 s were too short and 1.5 s enough. */
#define SETTLE_MS 2000

/* Wine's Unix code page, in which its Unix file names are. */
#define CP_UNIXCP 65010

/* Wine's own request to mountmgr.sys (include/ddk/mountmgr.h): what a drive letter stands for. */
#define IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE CTL_CODE('m', 33, METHOD_BUFFERED, FILE_READ_ACCESS)
struct unix_drive
{
    ULONG size, type, fs_type;
    DWORD serial;
    ULONGLONG unix_dev;
    WCHAR letter;
    USHORT mount_point_offset, device_offset, label_offset;
};

static int not_mounted(const WCHAR *text)
{
    MessageBoxW(NULL, text, L"Mount", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 1;
}

/* Append NAME, a Unix file name, to the command line as one argument, quoted by
 * the rules Wine splits a command line with. */
static BOOL append_argument(WCHAR *line, size_t size, const char *name)
{
    WCHAR wide[4096], *in, *out = line + wcslen(line);
    size_t backslashes = 0, i;

    if (!MultiByteToWideChar(CP_UNIXCP, 0, name, -1, wide, 4096)) return FALSE;
    if (out - line + 2 * wcslen(wide) + 4 > size) return FALSE;
    *out++ = ' ';
    *out++ = '"';
    for (in = wide; *in; *out++ = *in++)
    {
        if (*in == '\\') backslashes++;
        else
        {
            if (*in == '"') for (i = 0; i <= backslashes; i++) *out++ = '\\';
            backslashes = 0;
        }
    }
    for (i = 0; i < backslashes; i++) *out++ = '\\';
    *out++ = '"';
    *out = 0;
    return TRUE;
}

/* Wait for iso_mount.py to put its result into FILE, and read it. */
static BOOL read_result(const WCHAR *file, char *text, DWORD size)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    DWORD waited, read = 0;
    HANDLE handle;

    for (waited = 0; waited < 60000; waited += 100, Sleep(100))
    {
        if (!GetFileAttributesExW(file, GetFileExInfoStandard, &data) || !data.nFileSizeLow) continue;
        handle = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) continue;
        if (!ReadFile(handle, text, size - 1, &read, NULL)) read = 0;
        CloseHandle(handle);
        text[read] = 0;
        return read != 0;
    }
    return FALSE;
}

/* The drive letter mountmgr.sys has given DEVICE, mounted on MOUNT_POINT, within
 * five seconds; 0 if none. */
static WCHAR drive_of(const char *device, const char *mount_point)
{
    union { struct unix_drive drive; char bytes[1024]; } out;
    struct unix_drive in = {0};
    HANDLE manager;
    DWORD waited, read;
    WCHAR letter, found = 0;

    manager = CreateFileW(L"\\\\.\\MountPointManager", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, 0, NULL);
    if (manager == INVALID_HANDLE_VALUE) return 0;
    for (waited = 0; !found && waited < 5000; waited += 100, Sleep(100))
    {
        for (letter = 'D'; !found && letter <= 'Z'; letter++)
        {
            in.letter = letter;
            memset(&out, 0, sizeof(out));
            if (!DeviceIoControl(manager, IOCTL_MOUNTMGR_QUERY_UNIX_DRIVE, &in, sizeof(in), &out, sizeof(out),
                                 &read, NULL)) continue;
            if (out.drive.device_offset && out.drive.mount_point_offset &&
                !strcmp(out.bytes + out.drive.device_offset, device) &&
                !strcmp(out.bytes + out.drive.mount_point_offset, mount_point))
                found = letter;
        }
    }
    CloseHandle(manager);
    return found;
}

/* Why a mounted disc has no drive, for the session's log. */
static void log_no_drive(const char *device, const char *mount_point)
{
    typedef int (CDECL *debug_output)(const char *);
    debug_output output = (debug_output)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "__wine_dbg_output");
    char message[640];

    snprintf(message, sizeof(message), "linux-launch: mountmgr gave %.64s (%.480s) no drive letter within 5 s; "
             "opening the folder\n", device, mount_point);
    if (output) output(message);
}

/* Open an Explorer window of the folder PATH. By starting XP's Explorer: opening
 * the folder as a document goes over DDE, which fails from a program that is not
 * the shell ("DDE failure"), and "explorer.exe" would be Wine's. */
static void open_folder(const WCHAR *path)
{
    WCHAR argument[MAX_PATH + 8];

    /* XP's Explorer takes a quoted path as it is, with a backslash before the quote. */
    _snwprintf(argument, MAX_PATH + 8, L"\"%ls\"", path);
    argument[MAX_PATH + 7] = 0;
    ShellExecuteW(NULL, NULL, L"C:\\xp\\xp-explorer.exe", argument, NULL, SW_SHOWNORMAL);
}

/* Whether a drive that arrives gets XP's AutoPlay: xptray.exe is there to hear
 * of it, and autoplay.exe beside this program. */
static BOOL autoplay_runs(void)
{
    WCHAR path[MAX_PATH];
    DWORD size = GetModuleFileNameW(NULL, path, MAX_PATH);

    if (!size || size >= MAX_PATH - 16 || !wcsrchr(path, '\\')) return FALSE;
    wcscpy(wcsrchr(path, '\\') + 1, L"autoplay.exe");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES && FindWindowW(L"XPTrayHelper", NULL);
}

static void open_disc(const char *device, const char *mount_point, BOOL new)
{
    typedef WCHAR *(CDECL *dos_file_name)(const char *);
    WCHAR root[] = L"D:\\", *folder;
    dos_file_name get;

    if ((root[0] = drive_of(device, mount_point)))
    {
        /* A new disc is AutoPlay's: its AutoRun, its dialog or its window. */
        if (new && autoplay_runs()) return;
        if (new) Sleep(SETTLE_MS);
        open_folder(root);
        return;
    }
    log_no_drive(device, mount_point);
    get = (dos_file_name)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_dos_file_name");
    if (get && (folder = get(mount_point)))
    {
        open_folder(folder);
        HeapFree(GetProcessHeap(), 0, folder);
    }
}

static int mount_iso(WCHAR *path)
{
    static WCHAR line[20000];
    static char result[8192];
    WCHAR full[MAX_PATH], script[MAX_PATH], temp[MAX_PATH], answer[MAX_PATH], message[2048], reason[1024], *name;
    char *image, *helper = NULL, *reply = NULL, *device, *mount_point, *end;
    DWORD size, attributes;
    BOOL new = FALSE;
    int started = 1;

    path = unquoted(path);
    if (!*path) return 2;
    size = GetFullPathNameW(path, MAX_PATH, full, NULL);
    attributes = size && size < MAX_PATH ? GetFileAttributesW(full) : INVALID_FILE_ATTRIBUTES;
    image = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) ?
            unix_name(full) : NULL;
    if (!image)
    {
        _snwprintf(message, 2048, L"%.400ls is not on the Linux file system, so it cannot be mounted.", path);
        message[2047] = 0;
        return not_mounted(message);
    }
    name = wcsrchr(full, '\\');
    name = name ? name + 1 : full;

    /* iso_mount.py lies beside this program; its result goes into a temporary file. */
    size = GetModuleFileNameW(NULL, script, MAX_PATH);
    if (size && size < MAX_PATH - 16 && wcsrchr(script, '\\'))
    {
        wcscpy(wcsrchr(script, '\\') + 1, L"iso_mount.py");
        helper = unix_name(script);
    }
    if (GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"iso", 0, answer))
        reply = unix_name(answer);
    wcscpy(line, L"/usr/bin/env -u WINEDEBUG python3 -I");
    if (helper && reply && append_argument(line, 20000, helper) && append_argument(line, 20000, image) &&
        append_argument(line, 20000, reply))
        /* Not in the image's folder: that may be on a disc that is to be ejected. */
        started = start(line, L"C:\\");
    HeapFree(GetProcessHeap(), 0, image);
    if (started)
    {
        if (reply) DeleteFileW(answer);
        _snwprintf(message, 2048, L"%.400ls could not be mounted.", name);
        message[2047] = 0;
        return not_mounted(message);
    }

    if (!read_result(answer, result, sizeof(result))) strcpy(result, "error\nThere was no answer within a minute.\n");
    DeleteFileW(answer);
    if (!strncmp(result, "ok\n", 3) && (end = strchr(device = result + 3, '\n')))
    {
        *end = 0;
        mount_point = end + 1;
        if ((end = strchr(mount_point, '\n')))
        {
            *end = 0;
            new = !strncmp(end + 1, "new", 3);
        }
        open_disc(device, mount_point, new);
        return 0;
    }
    end = !strncmp(result, "error\n", 6) ? result + 6 : result;
    if (!MultiByteToWideChar(CP_UTF8, 0, end, -1, reason, 1024)) wcscpy(reason, L"The reason does not fit here.");
    _snwprintf(message, 2048, L"%.400ls could not be mounted:\n\n%ls", name, reason);
    message[2047] = 0;
    return not_mounted(message);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *unused, int show)
{
    static const WCHAR unreachable_option[] = L"--unreachable ";
    static const WCHAR terminal_option[] = L"--terminal ";
    static const WCHAR mount_option[] = L"--mount-iso ";
    /* The XP session's debug channels are for its own logs, not for Linux programs. */
    static const WCHAR prefix[] = L"/usr/bin/env -u WINEDEBUG ";
    WCHAR *rest = arguments(GetCommandLineW()), *line;
    size_t length = wcslen(prefix) + wcslen(rest) + 1;
    int result;
    (void)instance; (void)previous; (void)unused; (void)show;
    if (!wcsncmp(rest, unreachable_option, wcslen(unreachable_option)))
        return unreachable(rest + wcslen(unreachable_option));
    if (!wcsncmp(rest, terminal_option, wcslen(terminal_option)))
        return terminal(rest + wcslen(terminal_option));
    if (!wcsncmp(rest, mount_option, wcslen(mount_option)))
        return mount_iso(rest + wcslen(mount_option));
    if (!*rest || !(line = malloc(length * sizeof(WCHAR)))) return 2;
    wcscpy(line, prefix);
    wcscat(line, rest);
    result = start(line, NULL);
    free(line);
    return result;
}
