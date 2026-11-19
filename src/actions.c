/* Actions: input injection, media keys and Core Audio volume. */
#define COBJMACROS
#include "actions.h"
#include "log.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- parsing */

static const struct { const WCHAR *name; WORD vk; } KEYS[] = {
    { L"tab", VK_TAB }, { L"enter", VK_RETURN }, { L"return", VK_RETURN },
    { L"esc", VK_ESCAPE }, { L"escape", VK_ESCAPE }, { L"space", VK_SPACE },
    { L"backspace", VK_BACK }, { L"delete", VK_DELETE }, { L"del", VK_DELETE },
    { L"insert", VK_INSERT }, { L"ins", VK_INSERT },
    { L"left", VK_LEFT }, { L"right", VK_RIGHT }, { L"up", VK_UP }, { L"down", VK_DOWN },
    { L"home", VK_HOME }, { L"end", VK_END },
    { L"pgup", VK_PRIOR }, { L"pageup", VK_PRIOR }, { L"pgdn", VK_NEXT }, { L"pagedown", VK_NEXT },
    { L"plus", VK_OEM_PLUS }, { L"minus", VK_OEM_MINUS }, { L"comma", VK_OEM_COMMA },
    { L"period", VK_OEM_PERIOD }, { L"[", VK_OEM_4 }, { L"]", VK_OEM_6 },
    { L"volup", VK_VOLUME_UP }, { L"voldown", VK_VOLUME_DOWN }, { L"mute", VK_VOLUME_MUTE },
    { L"playpause", VK_MEDIA_PLAY_PAUSE }, { L"next", VK_MEDIA_NEXT_TRACK },
    { L"prev", VK_MEDIA_PREV_TRACK }, { L"stop", VK_MEDIA_STOP },
    { L"printscreen", VK_SNAPSHOT }, { L"apps", VK_APPS },
    { L"num+", VK_ADD }, { L"num-", VK_SUBTRACT },
};

static BOOL parse_key(const WCHAR *s, size_t n, WORD *vk)
{
    if (n == 1 && iswalnum(s[0])) { *vk = (WORD)towupper(s[0]); return TRUE; }
    if ((s[0] == L'f' || s[0] == L'F') && n >= 2 && n <= 3 && iswdigit(s[1])) {
        int f = _wtoi(s + 1);
        if (f >= 1 && f <= 24) { *vk = (WORD)(VK_F1 + f - 1); return TRUE; }
    }
    if (n >= 3 && n <= 4 && !_wcsnicmp(s, L"vk", 2)) {      /* vkXX: any virtual key */
        WCHAR hex[3] = { s[2], n == 4 ? s[3] : 0, 0 };
        WCHAR *end;
        unsigned long v = wcstoul(hex, &end, 16);
        if (*end == 0 && v > 0 && v < 255) { *vk = (WORD)v; return TRUE; }
    }
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (wcslen(KEYS[i].name) == n && _wcsnicmp(KEYS[i].name, s, n) == 0) {
            *vk = KEYS[i].vk;
            return TRUE;
        }
    return FALSE;
}

/* "ctrl+shift+tab" */
BOOL combo_parse(const WCHAR *s, KeyCombo *k)
{
    k->mods = k->vk = 0;
    while (*s) {
        while (*s == L' ') s++;
        const WCHAR *e = s;
        while (*e && *e != L'+') e++;
        size_t n = (size_t)(e - s);
        while (n && s[n - 1] == L' ') n--;
        if (n == 0) return FALSE;
        if      (n == 4 && !_wcsnicmp(s, L"ctrl", 4))  k->mods |= MOD_C;
        else if (n == 5 && !_wcsnicmp(s, L"shift", 5)) k->mods |= MOD_S;
        else if (n == 3 && !_wcsnicmp(s, L"alt", 3))   k->mods |= MOD_A;
        else if (n == 3 && !_wcsnicmp(s, L"win", 3))   k->mods |= MOD_W;
        else if (k->vk || !parse_key(s, n, &k->vk))     return FALSE;
        s = *e ? e + 1 : e;
    }
    return k->vk != 0;
}

static void key_name(WORD vk, BOOL pretty, WCHAR *out, size_t n)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        _snwprintf(out, n, pretty ? L"%c" : L"%c", pretty ? vk : towlower(vk));
        return;
    }
    if (vk >= VK_F1 && vk <= VK_F24) { _snwprintf(out, n, pretty ? L"F%d" : L"f%d", vk - VK_F1 + 1); return; }
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (KEYS[i].vk == vk) {
            if (pretty) {
                /* Use the keyboard's own name where Windows has one. */
                UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                LONG lp = (LONG)(sc << 16);
                switch (vk) {
                case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME: case VK_END:
                case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE: lp |= 1 << 24;
                }
                if (sc && vk < VK_VOLUME_MUTE && GetKeyNameTextW(lp, out, (int)n) > 0) return;
            }
            _snwprintf(out, n, L"%ls", KEYS[i].name);
            return;
        }
    if (pretty) {
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        if (sc && GetKeyNameTextW((LONG)(sc << 16), out, (int)n) > 0) return;
    }
    _snwprintf(out, n, L"vk%02x", vk);
}

void combo_format(const KeyCombo *k, BOOL pretty, WCHAR *out, size_t n)
{
    static const WCHAR *P[4] = { L"Ctrl", L"Shift", L"Alt", L"Win" };
    static const WCHAR *R[4] = { L"ctrl", L"shift", L"alt", L"win" };
    WCHAR key[48];
    out[0] = 0;
    if (!k->vk) return;
    size_t len = 0;
    for (int i = 0; i < 4; i++)
        if (k->mods & (1 << i))
            len += (size_t)_snwprintf(out + len, n - len, L"%ls+", pretty ? P[i] : R[i]);
    key_name(k->vk, pretty, key, 48);
    _snwprintf(out + len, n - len, L"%ls", key);
    out[n - 1] = 0;
}

static const struct { const WCHAR *id, *label; } ACTS[ACT_COUNT] = {
    [ACT_NONE]            = { L"none",            L"Do nothing" },
    [ACT_VOLUME_UP]       = { L"volume_up",       L"Volume up" },
    [ACT_VOLUME_DOWN]     = { L"volume_down",     L"Volume down" },
    [ACT_VOLUME_UP_OSD]   = { L"volume_up_osd",   L"Volume up (Windows OSD)" },
    [ACT_VOLUME_DOWN_OSD] = { L"volume_down_osd", L"Volume down (Windows OSD)" },
    [ACT_MUTE]            = { L"mute",            L"Mute / unmute" },
    [ACT_PLAY_PAUSE]      = { L"play_pause",      L"Play / pause" },
    [ACT_NEXT]            = { L"next_track",      L"Next track" },
    [ACT_PREV]            = { L"prev_track",      L"Previous track" },
    [ACT_STOP]            = { L"stop",            L"Stop" },
    [ACT_SCROLL_UP]       = { L"scroll_up",       L"Scroll up" },
    [ACT_SCROLL_DOWN]     = { L"scroll_down",     L"Scroll down" },
    [ACT_SCROLL_LEFT]     = { L"scroll_left",     L"Scroll left" },
    [ACT_SCROLL_RIGHT]    = { L"scroll_right",    L"Scroll right" },
    [ACT_ZOOM_IN]         = { L"zoom_in",         L"Zoom in" },
    [ACT_ZOOM_OUT]        = { L"zoom_out",        L"Zoom out" },
    [ACT_LEFT_CLICK]      = { L"left_click",      L"Mouse left click" },
    [ACT_RIGHT_CLICK]     = { L"right_click",     L"Mouse right click" },
    [ACT_MIDDLE_CLICK]    = { L"middle_click",    L"Mouse middle click" },
    [ACT_KEY]             = { L"key",             L"Keystroke" },
    [ACT_RUN]             = { L"run",             L"Open program / file" },
};

const WCHAR *action_id(int t)    { return t >= 0 && t < ACT_COUNT ? ACTS[t].id : L"none"; }
const WCHAR *action_label(int t) { return t >= 0 && t < ACT_COUNT ? ACTS[t].label : L""; }

BOOL action_parse(const WCHAR *s, Action *a)
{
    ZeroMemory(a, sizeof *a);
    while (*s == L' ') s++;
    if (!*s) return TRUE;
    if (!_wcsnicmp(s, L"key:", 4)) {
        a->type = ACT_KEY;
        return combo_parse(s + 4, &a->key);
    }
    if (!_wcsnicmp(s, L"run:", 4)) {
        a->type = ACT_RUN;
        wcsncpy(a->cmd, s + 4, 259);
        return a->cmd[0] != 0;
    }
    for (int i = 0; i < ACT_COUNT; i++)
        if (i != ACT_KEY && i != ACT_RUN && !_wcsicmp(s, ACTS[i].id)) { a->type = i; return TRUE; }
    return FALSE;
}

void action_format(const Action *a, WCHAR *out, size_t n)
{
    WCHAR k[64];
    switch (a->type) {
    case ACT_KEY: combo_format(&a->key, FALSE, k, 64); _snwprintf(out, n, L"key:%ls", k); break;
    case ACT_RUN: _snwprintf(out, n, L"run:%ls", a->cmd); break;
    default:      _snwprintf(out, n, L"%ls", action_id(a->type)); break;
    }
    out[n - 1] = 0;
}

/* ---------------------------------------------------------------- input */

static BOOL is_extended(WORD vk)
{
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE: case VK_LWIN: case VK_RWIN:
    case VK_APPS: case VK_SNAPSHOT: case VK_DIVIDE:
        return TRUE;
    }
    return FALSE;
}

static void key_event(INPUT *in, WORD vk, BOOL up)
{
    ZeroMemory(in, sizeof *in);
    in->type = INPUT_KEYBOARD;
    in->ki.wVk = vk;
    in->ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in->ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (is_extended(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
}

static void send_combo(const KeyCombo *k, int times)
{
    static const WORD MODVK[4] = { VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN };
    INPUT in[10];
    while (times-- > 0) {
        UINT n = 0;
        for (int i = 0; i < 4; i++)
            if (k->mods & (1 << i)) key_event(&in[n++], MODVK[i], FALSE);
        key_event(&in[n++], k->vk, FALSE);
        key_event(&in[n++], k->vk, TRUE);
        for (int i = 3; i >= 0; i--)
            if (k->mods & (1 << i)) key_event(&in[n++], MODVK[i], TRUE);
        SendInput(n, in, sizeof(INPUT));
    }
}

static void tap(WORD vk, int times)
{
    KeyCombo k = { 0, vk };
    send_combo(&k, times);
}

static void wheel(DWORD flag, int amount)
{
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    in.mi.mouseData = (DWORD)amount;
    SendInput(1, &in, sizeof in);
}

/* ---------------------------------------------------------------- audio */

/* Defined here so neither MinGW nor MSVC needs extra GUID libraries. */
static const GUID PM_CLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID PM_IID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID PM_IID_IAudioEndpointVolume =
    { 0x5CDF2C82, 0x841E, 0x4546, { 0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A } };

static IMMDeviceEnumerator *g_enum;
static IAudioEndpointVolume *g_vol;

void audio_init(void)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(&PM_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                     &PM_IID_IMMDeviceEnumerator, (void **)&g_enum);
    audio_refresh();
}

void audio_refresh(void)
{
    if (g_vol) { IAudioEndpointVolume_Release(g_vol); g_vol = NULL; }
    if (!g_enum) return;
    IMMDevice *dev = NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_enum, eRender, eMultimedia, &dev))) {
        IMMDevice_Activate(dev, &PM_IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void **)&g_vol);
        IMMDevice_Release(dev);
    }
}

void audio_shutdown(void)
{
    if (g_vol)  IAudioEndpointVolume_Release(g_vol);
    if (g_enum) IMMDeviceEnumerator_Release(g_enum);
    g_vol = NULL; g_enum = NULL;
    CoUninitialize();
}

BOOL audio_get(float *vol, BOOL *muted)
{
    if (!g_vol) return FALSE;
    if (FAILED(IAudioEndpointVolume_GetMasterVolumeLevelScalar(g_vol, vol)) ||
        FAILED(IAudioEndpointVolume_GetMute(g_vol, muted))) {
        audio_refresh();       /* device went away; try the new default */
        return FALSE;
    }
    return TRUE;
}

static void volume_change(int steps, const ActionOpts *o)
{
    float v; BOOL m;
    if (!audio_get(&v, &m) && !audio_get(&v, &m)) {
        tap(steps > 0 ? VK_VOLUME_UP : VK_VOLUME_DOWN, abs(steps));
        return;
    }
    v += steps * o->volume_step / 100.0f;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    IAudioEndpointVolume_SetMasterVolumeLevelScalar(g_vol, v, NULL);
    if (m && o->unmute_on_turn && steps > 0)
        IAudioEndpointVolume_SetMute(g_vol, FALSE, NULL);
}

static void mute_toggle(void)
{
    float v; BOOL m;
    if (audio_get(&v, &m))
        IAudioEndpointVolume_SetMute(g_vol, !m, NULL);
    else
        tap(VK_VOLUME_MUTE, 1);
}

static void run_command(const WCHAR *cmd)
{
    WCHAR file[260];
    const WCHAR *args;
    while (*cmd == L' ') cmd++;
    if (*cmd == L'"') {
        const WCHAR *q = wcschr(cmd + 1, L'"');
        size_t n = q ? (size_t)(q - cmd - 1) : wcslen(cmd + 1);
        if (n > 259) n = 259;
        wcsncpy(file, cmd + 1, n); file[n] = 0;
        args = q ? q + 1 : L"";
    } else {
        const WCHAR *sp = wcschr(cmd, L' ');
        size_t n = sp ? (size_t)(sp - cmd) : wcslen(cmd);
        if (n > 259) n = 259;
        wcsncpy(file, cmd, n); file[n] = 0;
        args = sp ? sp : L"";
    }
    while (*args == L' ') args++;
    HINSTANCE r = ShellExecuteW(NULL, NULL, file, *args ? args : NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        LOG(L"run failed (%d): %ls", (int)(INT_PTR)r, cmd);
}

static void click(DWORD down, DWORD up)
{
    INPUT in[2] = {0};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = down;
    in[1].mi.dwFlags = up;
    SendInput(2, in, sizeof(INPUT));
}

static void zoom(int notches)
{
    INPUT in;
    key_event(&in, VK_CONTROL, FALSE); SendInput(1, &in, sizeof in);
    wheel(MOUSEEVENTF_WHEEL, notches * WHEEL_DELTA);
    key_event(&in, VK_CONTROL, TRUE);  SendInput(1, &in, sizeof in);
}

void action_do(const Action *a, int n, const ActionOpts *o)
{
    if (n <= 0) return;
    int sc = o->scroll_step * n * (o->scroll_invert ? -1 : 1);
    switch (a->type) {
    case ACT_VOLUME_UP:       volume_change(n, o); break;
    case ACT_VOLUME_DOWN:     volume_change(-n, o); break;
    case ACT_VOLUME_UP_OSD:   tap(VK_VOLUME_UP, n); break;
    case ACT_VOLUME_DOWN_OSD: tap(VK_VOLUME_DOWN, n); break;
    case ACT_MUTE:            while (n--) mute_toggle(); break;
    case ACT_PLAY_PAUSE:      tap(VK_MEDIA_PLAY_PAUSE, n); break;
    case ACT_NEXT:            tap(VK_MEDIA_NEXT_TRACK, n); break;
    case ACT_PREV:            tap(VK_MEDIA_PREV_TRACK, n); break;
    case ACT_STOP:            tap(VK_MEDIA_STOP, n); break;
    case ACT_SCROLL_UP:       wheel(MOUSEEVENTF_WHEEL, sc); break;
    case ACT_SCROLL_DOWN:     wheel(MOUSEEVENTF_WHEEL, -sc); break;
    case ACT_SCROLL_LEFT:     wheel(MOUSEEVENTF_HWHEEL, -sc); break;
    case ACT_SCROLL_RIGHT:    wheel(MOUSEEVENTF_HWHEEL, sc); break;
    case ACT_ZOOM_IN:         zoom(n); break;
    case ACT_ZOOM_OUT:        zoom(-n); break;
    case ACT_LEFT_CLICK:      while (n--) click(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP); break;
    case ACT_RIGHT_CLICK:     while (n--) click(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP); break;
    case ACT_MIDDLE_CLICK:    while (n--) click(MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP); break;
    case ACT_KEY:             send_combo(&a->key, n); break;
    case ACT_RUN:             run_command(a->cmd); break;
    default:                  break;
    }
}
