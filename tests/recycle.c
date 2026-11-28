/* Recycle Bin probe: deletes a file to the Recycle Bin through SHFileOperation
 * and checks what XP's shell32 wrote and what the Recycle Bin folder lists.
 *
 * Run it as startmenu.exe so that XP's native shell32 is loaded (AppDefaults):
 *   cp recycle-test.exe startmenu.exe; wine startmenu.exe [DIR]
 * DIR (default C:\recycle-probe) is where the file is made; its drive's
 * RECYCLER\<SID>\INFO2 is read afterwards. Exit code 0 when the header is
 * sound and the folder lists at least one item. */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <sddl.h>
#include <stdio.h>

static int list_bin(void)
{
    IShellFolder *desktop, *bin;
    IEnumIDList *items;
    LPITEMIDLIST pidl, item;
    WCHAR path[64] = L"::{645FF040-5081-101B-9F08-00AA002F954E}";
    int count = 0;
    HRESULT hr;

    if (FAILED(SHGetDesktopFolder(&desktop))) return -1;
    hr = IShellFolder_ParseDisplayName(desktop, NULL, NULL, path, NULL, &pidl, NULL);
    if (FAILED(hr)) { wprintf(L"parse bin: %08lx\n", hr); return -1; }
    hr = IShellFolder_BindToObject(desktop, pidl, NULL, &IID_IShellFolder, (void **)&bin);
    if (FAILED(hr)) { wprintf(L"bind bin: %08lx\n", hr); return -1; }
    hr = IShellFolder_EnumObjects(bin, NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &items);
    if (hr != S_OK) { wprintf(L"enum bin: %08lx\n", hr); return hr == S_FALSE ? 0 : -1; }
    while (IEnumIDList_Next(items, 1, &item, NULL) == S_OK) {
        STRRET name;
        WCHAR text[MAX_PATH];
        if (SUCCEEDED(IShellFolder_GetDisplayNameOf(bin, item, SHGDN_NORMAL, &name)) &&
            SUCCEEDED(StrRetToBufW(&name, item, text, MAX_PATH)))
            wprintf(L"  item: %ls\n", text);
        CoTaskMemFree(item);
        count++;
    }
    return count;
}

static int dump_info2(const WCHAR *dir)
{
    HANDLE token;
    TOKEN_USER *user;
    BYTE buffer[256];
    DWORD size, header[5], read;
    WCHAR *sid, path[MAX_PATH];
    HANDLE file;
    int sound;

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size);
    user = (TOKEN_USER *)buffer;
    ConvertSidToStringSidW(user->User.Sid, &sid);
    swprintf(path, MAX_PATH, L"%.3lsRECYCLER\\%ls\\INFO2", dir, sid);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) { wprintf(L"%ls: error %lu\n", path, GetLastError()); return 0; }
    size = GetFileSize(file, NULL);
    ReadFile(file, header, sizeof(header), &read, NULL);
    CloseHandle(file);
    sound = read == sizeof(header) && header[3] == 800 && size >= 20 && (size - 20) % 800 == 0;
    wprintf(L"%ls: %lu bytes, version %lu, record size %lu, total size %lu: %ls\n", path, size, header[0],
            header[3], header[4], sound ? L"sound" : L"CORRUPT");
    return sound;
}

int wmain(int argc, WCHAR **argv)
{
    const WCHAR *dir = argc > 1 ? argv[1] : L"C:\\recycle-probe";
    WCHAR file[MAX_PATH], from[MAX_PATH + 2] = {0};
    SHFILEOPSTRUCTW op = {0};
    HANDLE handle;
    int ret, sound, listed;

    CoInitialize(NULL);
    CreateDirectoryW(dir, NULL);
    swprintf(file, MAX_PATH, L"%ls\\probe-%lu.txt", dir, GetTickCount());
    handle = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(handle, "recycle probe\r\n", 15, &(DWORD){0}, NULL);
    CloseHandle(handle);

    lstrcpyW(from, file);
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    ret = SHFileOperationW(&op);
    wprintf(L"delete %ls: %d, aborted %d, still there %d\n", file, ret, op.fAnyOperationsAborted,
            GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES);

    sound = dump_info2(dir);
    listed = list_bin();
    wprintf(L"Recycle Bin lists %d item(s)\n", listed);
    return sound && listed > 0 ? 0 : 1;
}
