/* What the running shell's desktop icons are: the text of each, the item ID the
 * desktop's list view holds for it, and what the desktop folder makes of that ID
 * (parsing name, attributes, context menu). Usage: desktop-items [FILE].
 * Run it as startmenu.exe to ask XP's own shell32. It only reads: the list view
 * messages need a little memory in the shell's process, freed again. */
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *out;

static void emit(const WCHAR *format, ...)
{
    WCHAR line[2048];
    char utf8[4096];
    va_list args;
    va_start(args, format);
    _vsnwprintf(line, ARRAYSIZE(line) - 1, format, args);
    va_end(args);
    line[ARRAYSIZE(line) - 1] = 0;
    if (WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), NULL, NULL) > 0) fputs(utf8, out);
    fflush(out);
}

static HWND view_of(HWND top)
{
    HWND view = FindWindowExW(top, NULL, L"SHELLDLL_DefView", NULL);
    return view ? FindWindowExW(view, NULL, L"SysListView32", NULL) : NULL;
}

static BOOL CALLBACK find_view(HWND top, LPARAM found)
{
    HWND list = view_of(top);
    if (list) *(HWND *)found = list;
    return !list;
}

/* An item ID list from another process, one SHITEMID at a time. */
static BOOL read_pidl(HANDLE process, const BYTE *from, BYTE *to, size_t size)
{
    size_t used = 0;
    for (;;)
    {
        USHORT cb;
        if (!ReadProcessMemory(process, from + used, &cb, sizeof(cb), NULL)) return FALSE;
        if (used + cb + sizeof(cb) > size) return FALSE;
        memcpy(to + used, &cb, sizeof(cb));
        if (!cb) return TRUE;
        if (cb < sizeof(cb) || !ReadProcessMemory(process, from + used + 2, to + used + 2, cb - 2, NULL)) return FALSE;
        used += cb;
    }
}

static void describe(IShellFolder *desktop, LPCITEMIDLIST pidl)
{
    const BYTE *byte = (const BYTE *)pidl;
    USHORT cb = pidl->mkid.cb;
    WCHAR hex[256] = L"", name[MAX_PATH] = L"";
    SFGAOF attributes = SFGAO_CANCOPY | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE | SFGAO_HASPROPSHEET |
                        SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_FILESYSANCESTOR;
    IContextMenu *menu = NULL;
    STRRET string;
    HRESULT hr;

    for (unsigned i = 0; i < cb && i < 64; i++) swprintf(hex + i * 3, 4, L"%02x ", byte[i]);
    emit(L"  id cb=%u next=%u: %ls\n", cb, *(const USHORT *)(byte + cb), hex);
    hr = IShellFolder_GetDisplayNameOf(desktop, pidl, SHGDN_FORPARSING, &string);
    if (SUCCEEDED(hr)) StrRetToBufW(&string, pidl, name, ARRAYSIZE(name));
    emit(L"  parsing name hr=%08lx \"%ls\"\n", hr, name);
    hr = IShellFolder_GetAttributesOf(desktop, 1, &pidl, &attributes);
    emit(L"  attributes hr=%08lx %08lx\n", hr, attributes);
    hr = IShellFolder_GetUIObjectOf(desktop, NULL, 1, &pidl, &IID_IContextMenu, NULL, (void **)&menu);
    emit(L"  context menu object hr=%08lx\n", hr);
    if (FAILED(hr) || !menu) return;
    HMENU popup = CreatePopupMenu();
    hr = IContextMenu_QueryContextMenu(menu, popup, 0, 1, 0x7fff, CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME);
    UINT first = GetMenuDefaultItem(popup, FALSE, 0);
    emit(L"  context menu hr=%08lx items=%d default=%d:", hr, GetMenuItemCount(popup), (int)first);
    for (int i = 0; i < GetMenuItemCount(popup); i++)
    {
        WCHAR text[128] = L"";
        if (GetMenuStringW(popup, i, text, ARRAYSIZE(text), MF_BYPOSITION)) emit(L" [%ls]", text);
    }
    emit(L"\n");
    DestroyMenu(popup);
    IContextMenu_Release(menu);
}

int wmain(int argc, WCHAR **argv)
{
    IShellFolder *desktop = NULL;
    HWND list = NULL;
    DWORD pid = 0;

    out = argc > 1 ? _wfopen(argv[1], L"w") : stdout;
    if (!out) return 2;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(SHGetDesktopFolder(&desktop))) return 2;
    EnumWindows(find_view, (LPARAM)&list);
    if (!list) { emit(L"no desktop list view\n"); return 1; }
    GetWindowThreadProcessId(list, &pid);
    HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    BYTE *remote = process ? VirtualAllocEx(process, NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : NULL;
    if (!remote) { emit(L"cannot reach process %lu: %lu\n", pid, GetLastError()); return 1; }
    int count = (int)SendMessageW(list, LVM_GETITEMCOUNT, 0, 0);
    emit(L"desktop list view %p of process %lu, %d items\n", list, pid, count);
    for (int i = 0; i < count; i++)
    {
        LVITEMW item = {.mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE, .iItem = i, .stateMask = ~0u,
                        .pszText = (WCHAR *)(remote + 1024), .cchTextMax = 256};
        WCHAR text[256] = L"";
        BYTE pidl[2048] = {0};
        RECT rect = {.left = LVIR_ICON};
        WriteProcessMemory(process, remote, &item, sizeof(item), NULL);
        if (!SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)remote)) continue;
        ReadProcessMemory(process, remote, &item, sizeof(item), NULL);
        if (item.pszText && item.pszText != LPSTR_TEXTCALLBACKW)
            ReadProcessMemory(process, item.pszText, text, sizeof(text) - sizeof(WCHAR), NULL);
        WriteProcessMemory(process, remote + 2048, &rect, sizeof(rect), NULL);
        SendMessageW(list, LVM_GETITEMRECT, i, (LPARAM)(remote + 2048));
        ReadProcessMemory(process, remote + 2048, &rect, sizeof(rect), NULL);
        emit(L"%d \"%ls\" at %ld,%ld state=%x lparam=%p\n", i, text, rect.left, rect.top, item.state,
             (void *)item.lParam);
        if (read_pidl(process, (const BYTE *)item.lParam, pidl, sizeof(pidl)))
            describe(desktop, (LPCITEMIDLIST)pidl);
        else
            emit(L"  no item ID at lparam\n");
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    IShellFolder_Release(desktop);
    return 0;
}
