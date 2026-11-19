/* Griffin PowerMate (USB) HID access via the stock Windows HID driver.
 *
 * Input report (after the report-ID byte Windows prepends):
 *   [1] button (bit 0)   [2] signed rotation delta   [3..] LED/pulse state
 * Output report: [0] report ID 0, [1] LED brightness 0..255.
 * No driver install (Zadig/WinUSB) is needed.
 */
#include "device.h"
#include "log.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdlib.h>

static HANDLE g_dev = INVALID_HANDLE_VALUE;
static HANDLE g_thread, g_stop, g_wrEvent;
static HWND   g_hwnd;
static USHORT g_inLen, g_outLen, g_featLen;
static BYTE   g_lastLed;
static BOOL   g_ledValid;

static DWORD WINAPI reader(LPVOID unused)
{
    (void)unused;
    BYTE buf[64];
    BYTE lastBtn = 0;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD len = g_inLen > sizeof buf ? sizeof buf : g_inLen;

    for (;;) {
        DWORD got = 0;
        ResetEvent(ov.hEvent);
        if (!ReadFile(g_dev, buf, len, NULL, &ov) && GetLastError() != ERROR_IO_PENDING)
            break;
        HANDLE hs[2] = { ov.hEvent, g_stop };
        DWORD w = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0) {               /* stop requested */
            CancelIoEx(g_dev, &ov);
            GetOverlappedResult(g_dev, &ov, &got, TRUE);
            CloseHandle(ov.hEvent);
            return 0;
        }
        if (!GetOverlappedResult(g_dev, &ov, &got, FALSE))
            break;                               /* unplugged */
        if (got < 3)
            continue;
        BYTE btn = buf[1] & 1;
        signed char d = (signed char)buf[2];
        if (btn != lastBtn || d != 0) {
            PostMessageW(g_hwnd, WM_PM_INPUT, btn, (LPARAM)d);
            lastBtn = btn;
        }
    }
    CloseHandle(ov.hEvent);
    PostMessageW(g_hwnd, WM_PM_LOST, 0, 0);
    return 0;
}

static BOOL try_open(const WCHAR *path)
{
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    HIDD_ATTRIBUTES a = { sizeof a };
    BOOL match = HidD_GetAttributes(h, &a) && a.VendorID == PM_VID &&
                 (a.ProductID == PM_PID_POWERMATE || a.ProductID == PM_PID_SOUNDKNOB);
    CloseHandle(h);
    if (!match)
        return FALSE;

    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE)
        h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        LOG(L"found PowerMate but could not open it (error %lu)", GetLastError());
        return FALSE;
    }

    PHIDP_PREPARSED_DATA pp;
    HIDP_CAPS caps = {0};
    if (HidD_GetPreparsedData(h, &pp)) {
        HidP_GetCaps(pp, &caps);
        HidD_FreePreparsedData(pp);
    }
    if (caps.InputReportByteLength < 3) {
        CloseHandle(h);
        return FALSE;
    }
    g_inLen = caps.InputReportByteLength;
    g_outLen = caps.OutputReportByteLength;
    g_featLen = caps.FeatureReportByteLength;
    g_dev = h;
    g_ledValid = FALSE;
    LOG(L"opened PowerMate %04X:%04X in=%u out=%u feat=%u", a.VendorID, a.ProductID,
        g_inLen, g_outLen, g_featLen);
    return TRUE;
}

BOOL pm_open(HWND notify)
{
    if (g_dev != INVALID_HANDLE_VALUE)
        return TRUE;
    g_hwnd = notify;

    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return FALSE;

    SP_DEVICE_INTERFACE_DATA ifd = { sizeof ifd };
    BOOL ok = FALSE;
    for (DWORD i = 0; !ok && SetupDiEnumDeviceInterfaces(set, NULL, &guid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *det = malloc(need);
        if (!det)
            continue;
        det->cbSize = sizeof *det;
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, NULL, NULL))
            ok = try_open(det->DevicePath);
        free(det);
    }
    SetupDiDestroyDeviceInfoList(set);
    if (!ok)
        return FALSE;

    if (!g_stop)    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_wrEvent) g_wrEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ResetEvent(g_stop);
    g_thread = CreateThread(NULL, 0, reader, NULL, 0, NULL);
    if (!g_thread) {
        CloseHandle(g_dev);
        g_dev = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    return TRUE;
}

void pm_close(void)
{
    if (g_thread) {
        SetEvent(g_stop);
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_dev != INVALID_HANDLE_VALUE) {
        CloseHandle(g_dev);
        g_dev = INVALID_HANDLE_VALUE;
    }
    g_ledValid = FALSE;
}

BOOL pm_is_open(void) { return g_dev != INVALID_HANDLE_VALUE; }
BOOL pm_has_led(void) { return pm_is_open() && (g_outLen >= 2 || g_featLen >= 2); }
void pm_led_invalidate(void) { g_ledValid = FALSE; }

void pm_set_led(BYTE level)
{
    if (!pm_has_led() || (g_ledValid && level == g_lastLed))
        return;
    BYTE buf[64] = {0};
    buf[1] = level;
    BOOL ok = FALSE;

    if (g_outLen >= 2 && g_outLen <= sizeof buf) {
        OVERLAPPED ov = {0};
        DWORD n;
        ov.hEvent = g_wrEvent;
        ResetEvent(g_wrEvent);
        if (WriteFile(g_dev, buf, g_outLen, NULL, &ov)) {
            ok = TRUE;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(g_wrEvent, 100) == WAIT_OBJECT_0)
                ok = GetOverlappedResult(g_dev, &ov, &n, FALSE);
            else {
                CancelIoEx(g_dev, &ov);
                GetOverlappedResult(g_dev, &ov, &n, TRUE);
            }
        }
    } else if (g_featLen >= 2 && g_featLen <= sizeof buf) {
        ok = HidD_SetFeature(g_dev, buf, g_featLen);
    }
    g_lastLed = level;
    g_ledValid = ok;
}
