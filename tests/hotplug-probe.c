/* Removable drives as a Windows program sees them: USB sticks, memory cards and
 * mounted disc images (Wine patches 0163, 0168-0170; the XP shell's hotplug UI).
 *
 *   hotplug-probe.exe list [OUT]            every drive from D: on: type, label, file
 *                                           system, size and storage descriptor.
 *                                           Prints PASS when every DRIVE_REMOVABLE
 *                                           drive says its media is removable and
 *                                           that it is on a USB or SD bus
 *   hotplug-probe.exe watch SECONDS [OUT]   the WM_DEVICECHANGE messages a top-level
 *                                           window gets, as they come
 *   hotplug-probe.exe eject LETTER [OUT]    IOCTL_STORAGE_EJECT_MEDIA to the volume,
 *                                           as the Safely Remove Hardware icon sends
 *
 * OUT is a file for the output, which a program started without a console loses.
 * It uses public APIs only, so it also runs on a real XP. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <dbt.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static FILE *out;
static ULONGLONG started;

static void say(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);
    fflush(out);
}

/* The volume's device, \\?\Volume{GUID}, which XP's Eject opens too. */
static HANDLE open_volume(WCHAR letter, DWORD access)
{
    WCHAR root[] = L"A:\\", name[64];
    root[0] = letter;
    if (!GetVolumeNameForVolumeMountPointW(root, name, ARRAYSIZE(name))) return INVALID_HANDLE_VALUE;
    name[wcslen(name) - 1] = 0;
    return CreateFileW(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

/* Prints a drive; returns FALSE for a removable drive described as something else. */
static BOOL describe(WCHAR letter)
{
    static const char *types[] = {"unknown", "no root", "removable", "fixed", "remote", "cdrom", "ramdisk"};
    WCHAR root[] = L"A:\\", label[64] = L"", fs[32] = L"";
    DWORD serial = 0, done, type;
    ULARGE_INTEGER free_bytes, total;
    BOOL ok = TRUE;
    HANDLE volume;

    root[0] = letter;
    type = GetDriveTypeW(root);
    say("%c: %s", (char)letter, type < ARRAYSIZE(types) ? types[type] : "?");
    if (GetVolumeInformationW(root, label, ARRAYSIZE(label), &serial, NULL, NULL, fs, ARRAYSIZE(fs)))
        say(", label '%ls', volume serial %08lx, %ls", label, serial, fs);
    else say(", no volume (error %lu)", GetLastError());
    if (GetDiskFreeSpaceExW(root, NULL, &total, &free_bytes))
        say(", %llu of %llu MB free", free_bytes.QuadPart >> 20, total.QuadPart >> 20);
    say("\n");

    volume = open_volume(letter, 0);
    if (volume != INVALID_HANDLE_VALUE)
    {
        STORAGE_PROPERTY_QUERY query = {StorageDeviceProperty, PropertyStandardQuery, {0}};
        union { STORAGE_DEVICE_DESCRIPTOR d; char bytes[1024]; } answer;
        memset(&answer, 0, sizeof(answer));
        if (DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &answer, sizeof(answer),
                            &done, NULL))
        {
            STORAGE_DEVICE_DESCRIPTOR *d = &answer.d;
#define TEXT_AT(offset) ((offset) && (offset) < sizeof(answer) ? answer.bytes + (offset) : "")
            say("    removable media %d, bus %d, vendor '%s', product '%s', serial '%s'\n", d->RemovableMedia,
                d->BusType, TEXT_AT(d->VendorIdOffset), TEXT_AT(d->ProductIdOffset),
                TEXT_AT(d->SerialNumberOffset));
            if (type == DRIVE_REMOVABLE) ok = d->RemovableMedia && (d->BusType == 7 || d->BusType == 12);
        }
        else say("    no storage descriptor (error %lu)\n", GetLastError());
        CloseHandle(volume);
    }
    else
    {
        say("    the volume cannot be opened (error %lu)\n", GetLastError());
        ok = type != DRIVE_REMOVABLE;
    }
    return ok;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DEVICECHANGE)
    {
        const DEV_BROADCAST_HDR *header = (const DEV_BROADCAST_HDR *)lparam;
        say("%6.2f ", (GetTickCount64() - started) / 1000.0);
        if (wparam == DBT_DEVICEARRIVAL) say("arrival");
        else if (wparam == DBT_DEVICEREMOVECOMPLETE) say("removed");
        else say("event %#x", (unsigned)wparam);
        if ((wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE) && header &&
            header->dbch_devicetype == DBT_DEVTYP_VOLUME)
        {
            const DEV_BROADCAST_VOLUME *volume = (const DEV_BROADCAST_VOLUME *)header;
            say(" volume mask %#lx flags %#x:", volume->dbcv_unitmask, volume->dbcv_flags);
            for (int i = 0; i < 26; i++)
                if (volume->dbcv_unitmask & (1u << i)) say(" %c", 'A' + i);
            say("\n");
        }
        else say("\n");
        return TRUE;
    }
    if (message == WM_TIMER)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int main(void)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const WCHAR *verb = argc > 1 ? argv[1] : L"list";
    int out_at = wcscmp(verb, L"list") ? 3 : 2;

    SetErrorMode(SEM_FAILCRITICALERRORS);
    out = argc > out_at ? _wfopen(argv[out_at], L"w") : stdout;
    if (!out) return 2;
    started = GetTickCount64();

    if (!wcscmp(verb, L"watch") && argc > 2)
    {
        WNDCLASSW cls = {.lpfnWndProc = window_proc, .lpszClassName = L"HotplugProbe",
                         .hInstance = GetModuleHandleW(NULL)};
        MSG message;
        HWND window;
        RegisterClassW(&cls);
        window = CreateWindowW(cls.lpszClassName, L"Hotplug probe", WS_POPUP, 0, 0, 0, 0, NULL, NULL,
                               cls.hInstance, NULL);
        SetTimer(window, 1, _wtoi(argv[2]) * 1000, NULL);
        say("watching\n");
        while (GetMessageW(&message, NULL, 0, 0)) DispatchMessageW(&message);
        say("done\n");
        return 0;
    }
    if (!wcscmp(verb, L"eject") && argc > 2)
    {
        HANDLE volume = open_volume(argv[2][0], GENERIC_READ);
        DWORD done, error = 0;
        if (volume == INVALID_HANDLE_VALUE)
        {
            say("open: error %lu\n", GetLastError());
            return 1;
        }
        if (!DeviceIoControl(volume, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &done, NULL))
            error = GetLastError();
        say("eject: error %lu after %.2f s\n", error, (GetTickCount64() - started) / 1000.0);
        return error != 0;
    }
    {
        DWORD drives = GetLogicalDrives();
        BOOL ok = TRUE;
        for (int i = 3; i < 26; i++)
            if (drives & (1u << i) && i != 'Z' - 'A') ok &= describe(L'A' + i);
        say("%s\n", ok ? "PASS" : "FAIL");
        return !ok;
    }
}
