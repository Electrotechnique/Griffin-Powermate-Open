/* powermate.ini handling.
 * Looked for next to the exe first (portable), else %APPDATA%\PowerMate\powermate.ini.
 * Written as UTF-16 so program paths with any characters survive. */
#include "config.h"
#include "log.h"
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

static WCHAR g_path[MAX_PATH];

static const struct { const WCHAR *id, *label; } GESTURES[G_COUNT] = {
    { L"rotate_left",            L"Rotate left" },
    { L"rotate_right",           L"Rotate right" },
    { L"press_rotate_left",      L"Press + rotate left" },
    { L"press_rotate_right",     L"Press + rotate right" },
    { L"long_press_rotate_left", L"Long press + rotate left" },
    { L"long_press_rotate_right",L"Long press + rotate right" },
    { L"click",                  L"Click" },
    { L"double_click",           L"Double click" },
    { L"long_press",             L"Long press" },
};

const WCHAR *gesture_id(int g)    { return GESTURES[g].id; }
const WCHAR *gesture_label(int g) { return GESTURES[g].label; }

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

/* ---------------------------------------------------------------- defaults */

static void set_action(Profile *p, int g, const WCHAR *s)
{
    action_parse(s, &p->g[g]);
    p->set |= 1u << g;
}

static Profile *add_app(Config *c, const WCHAR *exe)
{
    if (c->nprofiles >= MAX_PROFILES) return NULL;
    Profile *p = &c->profiles[c->nprofiles++];
    ZeroMemory(p, sizeof *p);
    wcsncpy(p->exe, exe, 63);
    _wcslwr(p->exe);
    return p;
}

void config_defaults(Config *c)
{
    ZeroMemory(c, sizeof *c);
    c->led_mode = LED_VOLUME;
    c->led_max = 255;
    c->mute_pulse = 1;
    c->pulse_ms = 2000;
    c->long_press_ms = 500;
    c->double_click_ms = 300;
    c->sensitivity = 1;
    c->acceleration = 2;
    c->opts.volume_step = 1.0f;
    c->opts.scroll_step = 40;
    c->opts.unmute_on_turn = 1;

    Profile *d = &c->profiles[0];
    c->nprofiles = 1;
    for (int g = 0; g < G_COUNT; g++) set_action(d, g, L"none");
    set_action(d, G_ROTATE_LEFT, L"volume_down");
    set_action(d, G_ROTATE_RIGHT, L"volume_up");
    set_action(d, G_PRESS_ROTATE_LEFT, L"prev_track");
    set_action(d, G_PRESS_ROTATE_RIGHT, L"next_track");
    set_action(d, G_CLICK, L"mute");
    set_action(d, G_LONG_PRESS, L"play_pause");

    static const WCHAR *browsers[] = { L"chrome.exe", L"msedge.exe", L"firefox.exe" };
    for (int i = 0; i < 3; i++) {
        Profile *p = add_app(c, browsers[i]);
        set_action(p, G_PRESS_ROTATE_LEFT, L"key:ctrl+shift+tab");
        set_action(p, G_PRESS_ROTATE_RIGHT, L"key:ctrl+tab");
    }
    config_resolve(c);
}

void config_resolve(Config *c)
{
    for (int i = 1; i < c->nprofiles; i++)
        for (int g = 0; g < G_COUNT; g++)
            if (!(c->profiles[i].set & (1u << g)))
                c->profiles[i].g[g] = c->profiles[0].g[g];
}

/* ---------------------------------------------------------------- load */

/* Returns FALSE if the key is absent. Strips " ; comment" tails. */
static BOOL get_str(const WCHAR *sec, const WCHAR *key, WCHAR *out, DWORD n)
{
    GetPrivateProfileStringW(sec, key, L"\x01", out, n, config_path());
    if (out[0] == 1) return FALSE;
    for (WCHAR *p = out; *p; p++)
        if (*p == L';' && (p == out || p[-1] == L' ' || p[-1] == L'\t')) { *p = 0; break; }
    size_t len = wcslen(out);
    while (len && (out[len - 1] == L' ' || out[len - 1] == L'\t')) out[--len] = 0;
    return TRUE;
}

static int get_int(const WCHAR *key, int def, int lo, int hi)
{
    WCHAR b[32];
    if (!get_str(L"general", key, b, 32) || !b[0]) return def;
    int v = _wtoi(b);
    return v < lo ? lo : v > hi ? hi : v;
}

/* Version 1.0 used one setting per rotation: rotate=volume etc. */
static void legacy_rotation(Config *c, const WCHAR *sec, const WCHAR *key, Profile *p, int gl, int gr)
{
    WCHAR b[300];
    if (!get_str(sec, key, b, 300) || (p->set & ((1u << gl) | (1u << gr)))) return;
    static const struct { const WCHAR *name, *left, *right; } MAP[] = {
        { L"none", L"none", L"none" },
        { L"volume", L"volume_down", L"volume_up" },
        { L"volume_keys", L"volume_down_osd", L"volume_up_osd" },
        { L"scroll", L"scroll_up", L"scroll_down" },
        { L"hscroll", L"scroll_left", L"scroll_right" },
        { L"zoom", L"zoom_out", L"zoom_in" },
        { L"track", L"prev_track", L"next_track" },
    };
    if (!b[0]) { set_action(p, gl, L"none"); set_action(p, gr, L"none"); return; }
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        if (!_wcsicmp(b, MAP[i].name)) { set_action(p, gl, MAP[i].left); set_action(p, gr, MAP[i].right); return; }
    if (!_wcsnicmp(b, L"keys:", 5)) {
        WCHAR *bar = wcschr(b, L'|');
        KeyCombo k1, k2;
        if (bar) {
            *bar = 0;
            if (combo_parse(b + 5, &k1) && combo_parse(bar + 1, &k2)) {
                p->g[gr].type = ACT_KEY; p->g[gr].key = k1;
                p->g[gl].type = ACT_KEY; p->g[gl].key = k2;
                p->set |= (1u << gl) | (1u << gr);
                return;
            }
        }
    }
    c->errors++;
    LOG(L"config: [%ls] %ls=%ls is not valid", sec, key, b);
}

static void load_profile(Config *c, const WCHAR *sec, Profile *p)
{
    WCHAR b[300];
    for (int g = 0; g < G_COUNT; g++) {
        if (!get_str(sec, GESTURES[g].id, b, 300)) continue;
        Action a;
        if (action_parse(b, &a)) {
            p->g[g] = a;
            p->set |= 1u << g;
        } else {
            c->errors++;
            LOG(L"config: [%ls] %ls=%ls is not valid", sec, GESTURES[g].id, b);
        }
    }
    legacy_rotation(c, sec, L"rotate", p, G_ROTATE_LEFT, G_ROTATE_RIGHT);
    legacy_rotation(c, sec, L"press_rotate", p, G_PRESS_ROTATE_LEFT, G_PRESS_ROTATE_RIGHT);
}

void config_load(Config *c)
{
    if (GetFileAttributesW(config_path()) == INVALID_FILE_ATTRIBUTES) {
        config_defaults(c);
        config_save(c);
        return;
    }
    config_defaults(c);
    c->nprofiles = 1;                     /* keep global defaults, drop default app profiles */

    WCHAR b[32];
    if (get_str(L"general", L"led", b, 32))
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
    if (get_str(L"general", L"volume_step", b, 32) && b[0]) {
        c->opts.volume_step = (float)_wtof(b);
        if (c->opts.volume_step <= 0 || c->opts.volume_step > 50) c->opts.volume_step = 1.0f;
    }

    c->profiles[0].set = 0;               /* so legacy keys can apply */
    load_profile(c, L"default", &c->profiles[0]);
    c->profiles[0].set = (1u << G_COUNT) - 1;

    static WCHAR names[16384];
    DWORD n = GetPrivateProfileSectionNamesW(names, 16384, config_path());
    for (WCHAR *s = names; n && *s; s += wcslen(s) + 1) {
        if (_wcsnicmp(s, L"app:", 4) || !s[4]) continue;
        Profile *p = add_app(c, s + 4);
        if (!p) break;
        load_profile(c, s, p);
    }
    config_resolve(c);
    LOG(L"config: %ls (%d profiles, %d errors)", config_path(), c->nprofiles, c->errors);
}

/* ---------------------------------------------------------------- save */

typedef struct { WCHAR *buf; size_t len, cap; } Out;

static void out(Out *o, const WCHAR *fmt, ...)
{
    WCHAR line[600];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf(line, 600, fmt, ap);
    va_end(ap);
    if (n < 0) n = 599;
    line[599] = 0;
    if (o->len + (size_t)n + 1 > o->cap) {
        size_t cap = (o->cap + (size_t)n + 1) * 2;
        WCHAR *nb = realloc(o->buf, cap * sizeof(WCHAR));
        if (!nb) return;
        o->buf = nb; o->cap = cap;
    }
    wcscpy(o->buf + o->len, line);
    o->len += (size_t)n;
}

static void out_profile(Out *o, const Profile *p, BOOL all)
{
    WCHAR a[300];
    for (int g = 0; g < G_COUNT; g++) {
        if (!all && !(p->set & (1u << g))) continue;
        action_format(&p->g[g], a, 300);
        out(o, L"%ls=%ls\r\n", GESTURES[g].id, a);
    }
}

BOOL config_save(const Config *c)
{
    static const WCHAR *LED[] = { L"volume", L"on", L"off", L"pulse" };
    Out o = {0};
    out(&o, L"\xFEFF; Griffin PowerMate settings. Edit with PowerMate's Settings window, or by hand.\r\n"
            L"; Actions: none volume_up volume_down volume_up_osd volume_down_osd mute play_pause\r\n"
            L";   next_track prev_track stop scroll_up scroll_down scroll_left scroll_right zoom_in\r\n"
            L";   zoom_out left_click right_click middle_click key:<ctrl+shift+tab> run:<command>\r\n\r\n");
    out(&o, L"[general]\r\nled=%ls\r\nled_brightness=%d\r\nmute_pulse=%d\r\npulse_ms=%d\r\n",
        LED[c->led_mode], c->led_max, c->mute_pulse, c->pulse_ms);
    out(&o, L"long_press_ms=%d\r\ndouble_click_ms=%d\r\nsensitivity=%d\r\nacceleration=%d\r\n",
        c->long_press_ms, c->double_click_ms, c->sensitivity, c->acceleration);
    out(&o, L"volume_step=%g\r\nscroll_step=%d\r\nscroll_invert=%d\r\nunmute_on_turn=%d\r\n\r\n",
        (double)c->opts.volume_step, c->opts.scroll_step, c->opts.scroll_invert, c->opts.unmute_on_turn);
    out(&o, L"[default]\r\n");
    out_profile(&o, &c->profiles[0], TRUE);
    for (int i = 1; i < c->nprofiles; i++) {
        out(&o, L"\r\n[app:%ls]\r\n", c->profiles[i].exe);
        out_profile(&o, &c->profiles[i], FALSE);
    }
    if (!o.buf) return FALSE;

    /* write to a temp file and swap, so a crash never leaves a half-written config */
    WCHAR tmp[MAX_PATH + 8];
    _snwprintf(tmp, MAX_PATH + 8, L"%ls.tmp", config_path());
    tmp[MAX_PATH + 7] = 0;
    BOOL ok = FALSE;
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w;
        ok = WriteFile(f, o.buf, (DWORD)(o.len * sizeof(WCHAR)), &w, NULL) && w == o.len * sizeof(WCHAR);
        CloseHandle(f);
        ok = ok && MoveFileExW(tmp, config_path(), MOVEFILE_REPLACE_EXISTING);
        if (!ok) DeleteFileW(tmp);
    }
    free(o.buf);
    return ok;
}

const Profile *config_profile(const Config *c, const WCHAR *exe)
{
    if (exe && *exe)
        for (int i = 1; i < c->nprofiles; i++)
            if (!_wcsicmp(c->profiles[i].exe, exe))
                return &c->profiles[i];
    return &c->profiles[0];
}
