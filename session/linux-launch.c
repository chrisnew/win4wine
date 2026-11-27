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
 * XP's "Open in Linux Terminal" on folders and drives (prefix/extras.py). PATH
 * is the rest of the command line, with or without quotes around it: by the
 * rules of CommandLineToArgvW the closing quote of "C:\" would be escaped.
 * The folder becomes the terminal's working directory and nothing else, so its
 * name reaches no shell. A folder the terminal could not start in gets a
 * message box instead: Wine would start it in another directory. */
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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

static int terminal(WCHAR *path)
{
    typedef char *(CDECL *unix_file_name)(const WCHAR *);
    /* The session's debug channels and font configuration are not for the
     * user's shell. Its Wine variables stay, so that "wine" there starts
     * programs as the shell does (session/wine.py). */
    static const WCHAR clean[] = L"/usr/bin/env -u WINEDEBUG -u FONTCONFIG_FILE ";
    unix_file_name get_unix_file_name = (unix_file_name)(void *)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name");
    WCHAR folder[MAX_PATH], line[96];
    size_t length = wcslen(path);
    DWORD size, attributes;
    char *name;

    while (length && (path[length - 1] == ' ' || path[length - 1] == '\t')) path[--length] = 0;
    if (length >= 2 && path[0] == '"' && path[length - 1] == '"')
    {
        path[length - 1] = 0;
        path++;
    }
    if (!*path) return 2;
    if (changed_by_wine(path))
        return refuse(path, L"has a name that ends with a dot or a space");
    /* A folder on a drive: Wine can start a Unix program in no other. */
    size = GetFullPathNameW(path, MAX_PATH, folder, NULL);
    if (!size || size >= MAX_PATH || folder[1] != ':')
        return refuse(path, L"is not on the Linux file system");
    attributes = GetFileAttributesW(folder);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        !get_unix_file_name || !(name = get_unix_file_name(folder)))
        return refuse(path, L"is not on the Linux file system");
    HeapFree(GetProcessHeap(), 0, name);
    if (!can_enter(folder))
        return refuse(path, L"does not let you in");

    wcscpy(line, clean);
    wcscat(line, GetFileAttributesW(L"Z:\\usr\\bin\\x-terminal-emulator") != INVALID_FILE_ATTRIBUTES ?
                 L"x-terminal-emulator" : L"xterm");
    return start(line, folder);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, WCHAR *unused, int show)
{
    static const WCHAR unreachable_option[] = L"--unreachable ";
    static const WCHAR terminal_option[] = L"--terminal ";
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
    if (!*rest || !(line = malloc(length * sizeof(WCHAR)))) return 2;
    wcscpy(line, prefix);
    wcscat(line, rest);
    result = start(line, NULL);
    free(line);
    return result;
}
