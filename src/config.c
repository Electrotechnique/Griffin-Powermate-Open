/* powermate.ini handling.
 * Looked for next to the exe first (portable), else %APPDATA%\PowerMate\powermate.ini. */
#include "config.h"
#include "log.h"
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static WCHAR g_path[MAX_PATH];

static const char DEFAULT_INI[] =
"; Griffin PowerMate settings. Saved changes are picked up automatically.\n"
"\n"
"[general]\n"
"; LED: volume (brightness follows volume) | on | off | pulse\n"
"led=volume\n"
"led_brightness=255\n"
"; With led=volume, breathe the LED while muted\n"
"mute_pulse=1\n"
"pulse_ms=2000\n"
"long_press_ms=500\n"
"double_click_ms=300\n"
"; Knob detents needed for one step (raise to slow the knob down)\n"
"sensitivity=1\n"
"; Fast spins move this many times further (0 = off)\n"
"acceleration=2\n"
"; Volume change per step, in percent (decimals allowed)\n"
"volume_step=1\n"
"; Wheel units per step; 120 = one mouse-wheel notch\n"
"scroll_step=40\n"
"scroll_invert=0\n"
"unmute_on_turn=1\n"
"\n"
"; Gestures: rotate, press_rotate (turn while held), click, double_click, long_press\n"
";\n"
"; Rotation actions: volume, volume_keys (shows Windows OSD), scroll, hscroll, zoom,\n"
";   track, keys:<clockwise>|<anticlockwise>   e.g. keys:ctrl+tab|ctrl+shift+tab\n"
"; Button actions: mute, play_pause, next_track, prev_track, stop,\n"
";   key:<combo>   e.g. key:ctrl+shift+m\n"
";   run:<command> e.g. run:calc.exe\n"
"; Any gesture: none. Leave double_click empty for instant clicks.\n"
";\n"
"; Key names: a-z 0-9 f1-f24 tab enter esc space backspace delete insert left right\n"
";   up down home end pgup pgdn plus minus comma period [ ] volup voldown mute\n"
";   playpause next prev stop printscreen apps; modifiers ctrl shift alt win\n"
"\n"
"[default]\n"
"rotate=volume\n"
"press_rotate=track\n"
"click=mute\n"
"double_click=\n"
"long_press=play_pause\n"
"\n"
"; Per-application profiles: [app:<exe name>] for the foreground window.\n"
"; Unset gestures fall back to [default].\n"
"\n"
"[app:chrome.exe]\n"
"press_rotate=keys:ctrl+tab|ctrl+shift+tab\n"
"\n"
"[app:msedge.exe]\n"
"press_rotate=keys:ctrl+tab|ctrl+shift+tab\n"
"\n"
"[app:firefox.exe]\n"
"press_rotate=keys:ctrl+tab|ctrl+shift+tab\n";

const WCHAR *config_path(void)
{
    if (g_path[0]) return g_path;

    WCHAR exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n && n < MAX_PATH) {
        WCHAR *slash = wcsrchr(exe, L'\\');
        if (slash) {
            wcscpy(slash + 1, L"powermate.ini");
            if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES) {
                wcscpy(g_path, exe);
                return g_path;
            }
        }
    }
    WCHAR *appdata = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdata))) {
        _snwprintf(g_path, MAX_PATH, L"%ls\\PowerMate", appdata);
        CreateDirectoryW(g_path, NULL);
        wcsncat(g_path, L"\\powermate.ini", MAX_PATH - wcslen(g_path) - 1);
        CoTaskMemFree(appdata);
    }
    return g_path;
}

static void write_default(void)
{
    FILE *f = _wfopen(config_path(), L"wb");
    if (!f) return;
    fwrite(DEFAULT_INI, 1, sizeof DEFAULT_INI - 1, f);
    fclose(f);
}

static void get_str(const WCHAR *sec, const WCHAR *key, WCHAR *out, DWORD n)
{
    GetPrivateProfileStringW(sec, key, L"\x01", out, n, config_path());
    WCHAR *semi = wcschr(out, L';');            /* trailing comments */
    if (semi) *semi = 0;
    size_t len = wcslen(out);
    while (len && (out[len - 1] == L' ' || out[len - 1] == L'\t')) out[--len] = 0;
}

static int get_int(const WCHAR *key, int def, int lo, int hi)
{
    WCHAR b[32];
    get_str(L"general", key, b, 32);
    if (b[0] == 1 || !b[0]) return def;
    int v = _wtoi(b);
    return v < lo ? lo : v > hi ? hi : v;
}

/* Only overwrites *a if the key is present. */
static void get_action(Config *c, const WCHAR *sec, const WCHAR *key, Action *a)
{
    WCHAR b[300];
    get_str(sec, key, b, 300);
    if (b[0] == 1) return;
    Action tmp;
    if (action_parse(b, &tmp))
        *a = tmp;
    else {
        c->errors++;
        LOG(L"config: [%ls] %ls=%ls is not valid", sec, key, b);
    }
}

static void load_profile(Config *c, const WCHAR *sec, Profile *p)
{
    get_action(c, sec, L"rotate", &p->rotate);
    get_action(c, sec, L"press_rotate", &p->press_rotate);
    get_action(c, sec, L"click", &p->click);
    get_action(c, sec, L"double_click", &p->double_click);
    get_action(c, sec, L"long_press", &p->long_press);
}

void config_load(Config *c)
{
    if (GetFileAttributesW(config_path()) == INVALID_FILE_ATTRIBUTES)
        write_default();

    ZeroMemory(c, sizeof *c);
    WCHAR b[32];
    get_str(L"general", L"led", b, 32);
    c->led_mode = !_wcsicmp(b, L"on") ? LED_ON : !_wcsicmp(b, L"off") ? LED_OFF :
                  !_wcsicmp(b, L"pulse") ? LED_PULSE : LED_VOLUME;
    c->led_max         = get_int(L"led_brightness", 255, 0, 255);
    c->mute_pulse      = get_int(L"mute_pulse", 1, 0, 1);
    c->pulse_ms        = get_int(L"pulse_ms", 2000, 200, 20000);
    c->long_press_ms   = get_int(L"long_press_ms", 500, 100, 5000);
    c->double_click_ms = get_int(L"double_click_ms", 300, 100, 1000);
    c->sensitivity     = get_int(L"sensitivity", 1, 1, 50);
    c->acceleration    = get_int(L"acceleration", 2, 0, 10);
    c->opts.scroll_step    = get_int(L"scroll_step", 40, 1, 1200);
    c->opts.scroll_invert  = get_int(L"scroll_invert", 0, 0, 1);
    c->opts.unmute_on_turn = get_int(L"unmute_on_turn", 1, 0, 1);
    get_str(L"general", L"volume_step", b, 32);
    c->opts.volume_step = (b[0] == 1 || !b[0]) ? 1.0f : (float)_wtof(b);
    if (c->opts.volume_step <= 0 || c->opts.volume_step > 50) c->opts.volume_step = 1.0f;

    /* defaults if the [default] section is missing keys */
    Profile *d = &c->profiles[0];
    action_parse(L"volume", &d->rotate);
    action_parse(L"track", &d->press_rotate);
    action_parse(L"mute", &d->click);
    action_parse(L"play_pause", &d->long_press);
    load_profile(c, L"default", d);
    c->nprofiles = 1;

    static WCHAR names[8192];
    DWORD n = GetPrivateProfileSectionNamesW(names, 8192, config_path());
    for (WCHAR *s = names; n && *s && c->nprofiles < MAX_PROFILES; s += wcslen(s) + 1) {
        if (_wcsnicmp(s, L"app:", 4) || !s[4]) continue;
        Profile *p = &c->profiles[c->nprofiles++];
        *p = *d;
        wcsncpy(p->exe, s + 4, 63);
        p->exe[63] = 0;
        _wcslwr(p->exe);
        load_profile(c, s, p);
    }
    LOG(L"config: %ls (%d profiles, %d errors)", config_path(), c->nprofiles, c->errors);
}

const Profile *config_profile(const Config *c, const WCHAR *exe)
{
    if (exe && *exe)
        for (int i = 1; i < c->nprofiles; i++)
            if (!_wcsicmp(c->profiles[i].exe, exe))
                return &c->profiles[i];
    return &c->profiles[0];
}
