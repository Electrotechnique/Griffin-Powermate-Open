/* PowerMate for Windows - tray app for the Griffin PowerMate (USB). */
#include <windows.h>
#include <shellapi.h>
#include <dbt.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdlib.h>
#include "device.h"
#include "actions.h"
#include "config.h"
#include "log.h"
#include "settings.h"
#include "resource.h"

#define APP_NAME   L"PowerMate"
#define WM_TRAY    (WM_APP + 10)
#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

enum { T_RETRY = 1, T_LONG, T_DBL, T_LED, T_CFG };
enum { ID_PAUSE = 100, ID_SETTINGS, ID_EDIT, ID_RELOAD, ID_STARTUP, ID_EXIT,
       ID_LED_VOLUME, ID_LED_ON, ID_LED_OFF, ID_LED_PULSE };

static Config  C;
static HWND    g_hwnd;
static HINSTANCE g_inst;
static NOTIFYICONDATAW g_nid;
static UINT    g_taskbarCreated;
static HICON   g_iconOn, g_iconOff;
static BOOL    g_paused;
static FILETIME g_cfgTime;
static HDEVNOTIFY g_devNotify;
static int     g_ledOverride = -1;   /* tray LED choice, -1 = from config */

/* gesture state */
static BOOL    g_down, g_rotated, g_longHeld, g_longFired, g_clickPending;
static const Profile *g_downProfile, *g_clickProfile;
static int     g_accum;

/* ---------------------------------------------------------------- logging */

static BOOL g_console;
void log_init(BOOL console)
{
    g_console = console;
    if (console && AllocConsole()) {
        FILE *f;
        _wfreopen_s(&f, L"CONOUT$", L"w", stdout);
    }
}
void log_printf(const WCHAR *fmt, ...)
{
    if (!g_console) return;
    SYSTEMTIME t; GetLocalTime(&t);
    wprintf(L"%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vwprintf(fmt, ap); va_end(ap);
    wprintf(L"\n");
    fflush(stdout);
}

/* ---------------------------------------------------------------- helpers */

static void foreground_exe(WCHAR *out, size_t n)
{
    static HWND lastWnd;
    static WCHAR lastExe[64];
    out[0] = 0;
    HWND w = GetForegroundWindow();
    if (!w) return;
    if (w == lastWnd) { wcsncpy(out, lastExe, n); return; }
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return;
    WCHAR path[MAX_PATH];
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(p, 0, path, &len)) {
        WCHAR *b = wcsrchr(path, L'\\');
        wcsncpy(lastExe, b ? b + 1 : path, 63);
        lastExe[63] = 0;
        _wcslwr(lastExe);
        lastWnd = w;
        wcsncpy(out, lastExe, n);
    }
    CloseHandle(p);
}

static const Profile *current_profile(void)
{
    WCHAR exe[64];
    foreground_exe(exe, 64);
    return config_profile(&C, exe);
}

BOOL startup_enabled(void)
{
    WCHAR v[MAX_PATH * 2];
    DWORD sz = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME, RRF_RT_REG_SZ, NULL, v, &sz) == ERROR_SUCCESS;
}

void startup_set(BOOL on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        WCHAR exe[MAX_PATH], v[MAX_PATH + 4];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        _snwprintf(v, MAX_PATH + 4, L"\"%ls\"", exe);
        v[MAX_PATH + 3] = 0;
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(k, APP_NAME);
    }
    RegCloseKey(k);
}

static BOOL cfg_mtime(FILETIME *ft)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(config_path(), GetFileExInfoStandard, &a)) return FALSE;
    *ft = a.ftLastWriteTime;
    return TRUE;
}

/* ---------------------------------------------------------------- tray */

static void tray_update(void)
{
    BOOL on = pm_is_open() && !g_paused;
    g_nid.hIcon = on ? g_iconOn : g_iconOff;
    float v; BOOL m;
    WCHAR vol[32] = L"";
    if (audio_get(&v, &m))
        _snwprintf(vol, 32, m ? L" - muted" : L" - volume %d%%", (int)(v * 100 + 0.5f));
    _snwprintf(g_nid.szTip, sizeof g_nid.szTip / sizeof(WCHAR), L"PowerMate: %ls%ls",
               !pm_is_open() ? L"not connected" : g_paused ? L"paused" : L"active", vol);
    g_nid.szTip[sizeof g_nid.szTip / sizeof(WCHAR) - 1] = 0;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void tray_add(void)
{
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_iconOff;
    wcscpy(g_nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    tray_update();
}

static void balloon(const WCHAR *title, const WCHAR *text)
{
    g_nid.uFlags = NIF_INFO;
    wcsncpy(g_nid.szInfoTitle, title, 63);
    wcsncpy(g_nid.szInfo, text, 255);
    g_nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static int led_mode(void) { return g_ledOverride >= 0 ? g_ledOverride : C.led_mode; }

static void tray_menu(void)
{
    HMENU m = CreatePopupMenu(), led = CreatePopupMenu();
    int lm = led_mode();
    AppendMenuW(led, MF_STRING | (lm == LED_VOLUME ? MF_CHECKED : 0), ID_LED_VOLUME, L"Follow volume");
    AppendMenuW(led, MF_STRING | (lm == LED_ON ? MF_CHECKED : 0), ID_LED_ON, L"On");
    AppendMenuW(led, MF_STRING | (lm == LED_PULSE ? MF_CHECKED : 0), ID_LED_PULSE, L"Pulse");
    AppendMenuW(led, MF_STRING | (lm == LED_OFF ? MF_CHECKED : 0), ID_LED_OFF, L"Off");

    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, pm_is_open() ? L"PowerMate connected" : L"PowerMate not connected");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"&Settings...");
    AppendMenuW(m, MF_STRING | (g_paused ? MF_CHECKED : 0), ID_PAUSE, L"&Pause");
    AppendMenuW(m, MF_POPUP | (pm_has_led() ? 0 : MF_GRAYED), (UINT_PTR)led, L"&LED");
    AppendMenuW(m, MF_STRING, ID_EDIT, L"Open settings &file");
    AppendMenuW(m, MF_STRING, ID_RELOAD, L"&Reload settings file");
    AppendMenuW(m, MF_STRING | (startup_enabled() ? MF_CHECKED : 0), ID_STARTUP, L"Start with &Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");

    SetMenuDefaultItem(m, ID_SETTINGS, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

/* ---------------------------------------------------------------- LED */

static void led_tick(void)
{
    if (!pm_has_led()) return;
    double level;
    double t = (double)(GetTickCount() % (DWORD)C.pulse_ms) / C.pulse_ms;
    double breathe = (1 - cos(2 * 3.14159265358979 * t)) / 2;
    breathe *= breathe;                     /* perceptual curve */
    float v; BOOL m;

    if (g_paused) level = 0;
    else switch (led_mode()) {
    case LED_ON:    level = 1; break;
    case LED_OFF:   level = 0; break;
    case LED_PULSE: level = breathe; break;
    default:
        if (!audio_get(&v, &m)) level = 1;
        else if (m) level = C.mute_pulse ? breathe : 0;
        else level = v;
        break;
    }
    pm_set_led((BYTE)(level * C.led_max + 0.5));
}

/* ---------------------------------------------------------------- gestures */

static void reset_gesture(void)
{
    g_down = g_rotated = g_longHeld = g_longFired = g_clickPending = FALSE;
    g_accum = 0;
    KillTimer(g_hwnd, T_LONG);
    KillTimer(g_hwnd, T_DBL);
}

static BOOL has(const Profile *p, int g) { return p->g[g].type != ACT_NONE; }

/* Keystrokes, clicks and scrolling would land in the settings window itself. */
static BOOL is_input_action(int t)
{
    return t == ACT_KEY || t == ACT_RUN || (t >= ACT_SCROLL_UP && t <= ACT_MIDDLE_CLICK);
}

static void fire(const Profile *p, int g, int n)
{
    const Action *a = &p->g[g];
    WCHAR msg[200], detail[64] = L"";
    if (a->type == ACT_KEY) combo_format(&a->key, TRUE, detail, 64);
    else if (a->type == ACT_RUN) wcsncpy(detail, a->cmd, 63);
    detail[63] = 0;
    BOOL suppress = settings_foreground() && is_input_action(a->type);
    _snwprintf(msg, 200, L"%ls \x2192 %ls%ls%ls%ls%ls", gesture_label(g), action_label(a->type),
               detail[0] ? L" " : L"", detail,
               p->exe[0] ? L"  in " : L"", p->exe);
    msg[199] = 0;
    if (suppress) wcsncat(msg, L"  (not sent while this window is active)", 199 - wcslen(msg));
    settings_gesture(msg);
    LOG(L"%ls x%d", msg, n);
    if (!suppress) action_do(a, n, &C.opts);
}

static void on_down(void)
{
    const Profile *p = current_profile();
    g_down = TRUE;
    g_rotated = g_longHeld = g_longFired = FALSE;
    g_accum = 0;
    g_downProfile = p;
    if (has(p, G_LONG_PRESS) || has(p, G_LONG_ROTATE_LEFT) || has(p, G_LONG_ROTATE_RIGHT))
        SetTimer(g_hwnd, T_LONG, C.long_press_ms, NULL);
}

static void on_long_timer(void)
{
    KillTimer(g_hwnd, T_LONG);
    if (!g_down || g_rotated) return;
    const Profile *p = g_downProfile;
    g_longHeld = TRUE;
    /* With no long-press-rotate actions, fire immediately; otherwise wait for release
     * so that holding and then turning doesn't also trigger the long press. */
    if (!has(p, G_LONG_ROTATE_LEFT) && !has(p, G_LONG_ROTATE_RIGHT) && has(p, G_LONG_PRESS)) {
        g_longFired = TRUE;
        fire(p, G_LONG_PRESS, 1);
    }
}

static void on_turn(int d)
{
    const Profile *p = g_down ? g_downProfile : current_profile();
    int left = G_ROTATE_LEFT;
    if (g_down) {
        if (!g_rotated) {
            g_rotated = TRUE;
            if (!g_longHeld) KillTimer(g_hwnd, T_LONG);
        }
        left = g_longHeld ? G_LONG_ROTATE_LEFT : G_PRESS_ROTATE_LEFT;
    }
    if ((g_accum > 0 && d < 0) || (g_accum < 0 && d > 0)) g_accum = 0;  /* direction change */
    g_accum += d;
    int steps = g_accum / C.sensitivity;
    g_accum -= steps * C.sensitivity;
    if (!steps) return;
    if (C.acceleration && abs(d) >= 3) steps *= C.acceleration;
    fire(p, steps > 0 ? left + 1 : left, abs(steps));
}

static void on_up(void)
{
    g_down = FALSE;
    KillTimer(g_hwnd, T_LONG);
    if (g_rotated || g_longFired) return;
    const Profile *p = g_downProfile;
    if (g_longHeld) {
        if (has(p, G_LONG_PRESS)) fire(p, G_LONG_PRESS, 1);
        return;
    }
    if (!has(p, G_DOUBLE_CLICK)) {
        fire(p, G_CLICK, 1);
    } else if (g_clickPending && g_clickProfile == p) {
        g_clickPending = FALSE;
        KillTimer(g_hwnd, T_DBL);
        fire(p, G_DOUBLE_CLICK, 1);
    } else {
        g_clickPending = TRUE;
        g_clickProfile = p;
        SetTimer(g_hwnd, T_DBL, C.double_click_ms, NULL);
    }
}

/* ---------------------------------------------------------------- device */

static void try_connect(void)
{
    if (pm_is_open()) return;
    if (pm_open(g_hwnd)) {
        reset_gesture();
        pm_led_invalidate();
        KillTimer(g_hwnd, T_RETRY);
        tray_update();
    }
}

static void on_lost(void)
{
    LOG(L"PowerMate disconnected");
    pm_close();
    reset_gesture();
    SetTimer(g_hwnd, T_RETRY, 3000, NULL);
    tray_update();
}

static void reload(BOOL notify)
{
    config_load(&C);
    cfg_mtime(&g_cfgTime);
    g_ledOverride = -1;
    reset_gesture();
    if (notify || C.errors) {
        WCHAR msg[128];
        if (C.errors) _snwprintf(msg, 128, L"%d setting(s) could not be understood and were ignored.", C.errors);
        else          _snwprintf(msg, 128, L"Settings reloaded.");
        balloon(APP_NAME, msg);
    }
}

static void settings_applied(void) { reload(FALSE); }

/* ---------------------------------------------------------------- window */

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PM_INPUT: {
        if (g_paused) return 0;
        BOOL btn = (BOOL)wp;
        int d = (int)lp;
        if (btn && !g_down) on_down();
        if (d) on_turn(d);
        if (!btn && g_down) on_up();
        return 0;
    }
    case WM_PM_LOST:
        on_lost();
        return 0;

    case WM_TIMER:
        switch (wp) {
        case T_RETRY: try_connect(); break;
        case T_LONG: on_long_timer(); break;
        case T_DBL:
            KillTimer(h, T_DBL);
            if (g_clickPending) {
                g_clickPending = FALSE;
                fire(g_clickProfile, G_CLICK, 1);
            }
            break;
        case T_LED: led_tick(); break;
        case T_CFG: {
            FILETIME ft;
            static int n;
            if (cfg_mtime(&ft) && CompareFileTime(&ft, &g_cfgTime) != 0) reload(TRUE);
            if (++n % 5 == 0) audio_refresh();   /* follow default output device changes */
            tray_update();
            break;
        }
        }
        return 0;

    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
            tray_menu();
            break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
            settings_open(g_inst, settings_applied);
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SETTINGS: settings_open(g_inst, settings_applied); break;
        case ID_PAUSE:   g_paused = !g_paused; reset_gesture(); tray_update(); break;
        case ID_EDIT:    ShellExecuteW(NULL, L"open", L"notepad.exe", config_path(), NULL, SW_SHOWNORMAL); break;
        case ID_RELOAD:  reload(TRUE); break;
        case ID_STARTUP: startup_set(!startup_enabled()); break;
        case ID_LED_VOLUME: g_ledOverride = LED_VOLUME; break;
        case ID_LED_ON:     g_ledOverride = LED_ON; break;
        case ID_LED_OFF:    g_ledOverride = LED_OFF; break;
        case ID_LED_PULSE:  g_ledOverride = LED_PULSE; break;
        case ID_EXIT:    DestroyWindow(h); break;
        }
        return 0;

    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL) try_connect();
        return TRUE;

    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            pm_led_invalidate();
            if (!pm_is_open()) SetTimer(h, T_RETRY, 1000, NULL);
        }
        return TRUE;

    case WM_DESTROY:
        pm_set_led(0);
        pm_close();
        if (g_devNotify) UnregisterDeviceNotification(g_devNotify);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_taskbarCreated && msg) {   /* Explorer restarted */
        tray_add();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    (void)prev; (void)show;
    g_inst = inst;
    log_init(cmd && wcsstr(cmd, L"--console") != NULL);

    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\PowerMateForWindows");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"PowerMate is already running (see the notification area).", APP_NAME, MB_ICONINFORMATION);
        return 0;
    }

    audio_init();
    config_load(&C);
    cfg_mtime(&g_cfgTime);

    int sm = GetSystemMetrics(SM_CXSMICON);
    g_iconOn  = LoadImageW(inst, MAKEINTRESOURCEW(IDI_ON),  IMAGE_ICON, sm, sm, 0);
    g_iconOff = LoadImageW(inst, MAKEINTRESOURCEW(IDI_OFF), IMAGE_ICON, sm, sm, 0);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = L"PowerMateWnd";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_ON));
    RegisterClassW(&wc);
    /* A hidden top-level window (not message-only) so it receives broadcasts. */
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, APP_NAME, WS_POPUP,
                             0, 0, 0, 0, NULL, NULL, inst, NULL);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    DEV_BROADCAST_DEVICEINTERFACE_W f = { sizeof f, DBT_DEVTYP_DEVICEINTERFACE };
    f.dbcc_classguid = (GUID){ 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
    g_devNotify = RegisterDeviceNotificationW(g_hwnd, &f, DEVICE_NOTIFY_WINDOW_HANDLE);

    tray_add();
    if (!pm_open(g_hwnd)) {
        SetTimer(g_hwnd, T_RETRY, 3000, NULL);
        balloon(APP_NAME, L"Waiting for a PowerMate to be plugged in.");
    }
    if (C.errors) reload(FALSE);
    SetTimer(g_hwnd, T_LED, 40, NULL);
    SetTimer(g_hwnd, T_CFG, 2000, NULL);
    tray_update();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (settings_dialog_msg(&m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    audio_shutdown();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
