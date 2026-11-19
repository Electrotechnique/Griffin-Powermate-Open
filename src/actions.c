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
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (wcslen(KEYS[i].name) == n && _wcsnicmp(KEYS[i].name, s, n) == 0) {
            *vk = KEYS[i].vk;
            return TRUE;
        }
    return FALSE;
}

/* "ctrl+shift+tab" */
static BOOL parse_combo(const WCHAR *s, KeyCombo *k)
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

static const struct { const WCHAR *name; int type; } NAMES[] = {
    { L"none", ACT_NONE }, { L"volume", ACT_VOLUME }, { L"volume_keys", ACT_VOLUME_KEYS },
    { L"scroll", ACT_SCROLL }, { L"hscroll", ACT_HSCROLL }, { L"zoom", ACT_ZOOM },
    { L"track", ACT_TRACK }, { L"mute", ACT_MUTE }, { L"play_pause", ACT_PLAY_PAUSE },
    { L"next_track", ACT_NEXT }, { L"prev_track", ACT_PREV }, { L"stop", ACT_STOP },
};

BOOL action_parse(const WCHAR *s, Action *a)
{
    ZeroMemory(a, sizeof *a);
    while (*s == L' ') s++;
    if (!*s) return TRUE;
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (!_wcsicmp(s, NAMES[i].name)) { a->type = NAMES[i].type; return TRUE; }

    if (!_wcsnicmp(s, L"keys:", 5)) {
        WCHAR buf[128];
        wcsncpy(buf, s + 5, 127); buf[127] = 0;
        WCHAR *bar = wcschr(buf, L'|');
        if (!bar) return FALSE;
        *bar = 0;
        a->type = ACT_KEYS;
        return parse_combo(buf, &a->cw) && parse_combo(bar + 1, &a->ccw);
    }
    if (!_wcsnicmp(s, L"key:", 4)) {
        a->type = ACT_KEY;
        return parse_combo(s + 4, &a->cw);
    }
    if (!_wcsnicmp(s, L"run:", 4)) {
        a->type = ACT_RUN;
        wcsncpy(a->cmd, s + 4, 259);
        return a->cmd[0] != 0;
    }
    return FALSE;
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

/* ---------------------------------------------------------------- dispatch */

void action_rotate(const Action *a, int steps, const ActionOpts *o)
{
    if (!steps) return;
    int n = abs(steps);
    BOOL cw = steps > 0;
    switch (a->type) {
    case ACT_VOLUME:      volume_change(steps, o); break;
    case ACT_VOLUME_KEYS: tap(cw ? VK_VOLUME_UP : VK_VOLUME_DOWN, n); break;
    case ACT_SCROLL:      wheel(MOUSEEVENTF_WHEEL, (o->scroll_invert ? steps : -steps) * o->scroll_step); break;
    case ACT_HSCROLL:     wheel(MOUSEEVENTF_HWHEEL, (o->scroll_invert ? -steps : steps) * o->scroll_step); break;
    case ACT_ZOOM: {
        INPUT in;
        key_event(&in, VK_CONTROL, FALSE); SendInput(1, &in, sizeof in);
        wheel(MOUSEEVENTF_WHEEL, steps * WHEEL_DELTA);
        key_event(&in, VK_CONTROL, TRUE);  SendInput(1, &in, sizeof in);
        break;
    }
    case ACT_TRACK:       tap(cw ? VK_MEDIA_NEXT_TRACK : VK_MEDIA_PREV_TRACK, n); break;
    case ACT_KEYS:        send_combo(cw ? &a->cw : &a->ccw, n); break;
    default:              break;
    }
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

void action_press(const Action *a)
{
    switch (a->type) {
    case ACT_MUTE:       mute_toggle(); break;
    case ACT_PLAY_PAUSE: tap(VK_MEDIA_PLAY_PAUSE, 1); break;
    case ACT_NEXT:       tap(VK_MEDIA_NEXT_TRACK, 1); break;
    case ACT_PREV:       tap(VK_MEDIA_PREV_TRACK, 1); break;
    case ACT_STOP:       tap(VK_MEDIA_STOP, 1); break;
    case ACT_KEY:        send_combo(&a->cw, 1); break;
    case ACT_KEYS:       send_combo(&a->cw, 1); break;
    case ACT_RUN:        run_command(a->cmd); break;
    default:             break;
    }
}
