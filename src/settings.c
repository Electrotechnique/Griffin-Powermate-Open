/* Settings window: applications on the left, one action per gesture on the right,
 * general options below. Works on a copy of the config and saves on OK / Apply. */
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "settings.h"
#include "config.h"
#include "resource.h"

#define INHERIT (-1)

static HWND   g_dlg;
static HINSTANCE g_inst;
static Config W;                 /* working copy */
static int    g_sel;             /* selected profile */
static BOOL   g_loading;
static void (*g_applied)(void);

/* ---------------------------------------------------------------- helpers */

static Profile *cur(void) { return &W.profiles[g_sel]; }
static BOOL inherited(int g) { return g_sel > 0 && !(cur()->set & (1u << g)); }

static void exe_from_path(const WCHAR *path, WCHAR *out, size_t n)
{
    const WCHAR *b = wcsrchr(path, L'\\');
    wcsncpy(out, b ? b + 1 : path, n - 1);
    out[n - 1] = 0;
    _wcslwr(out);
}

static void key_text(int g)
{
    WCHAR t[80];
    const Action *a = &cur()->g[g];
    if (a->key.vk) combo_format(&a->key, TRUE, t, 80);
    else wcscpy(t, L"Click, then press keys");
    SetDlgItemTextW(g_dlg, IDC_G_KEY + g, t);
}

static void update_param(int g)
{
    const Action *a = &cur()->g[g];
    BOOL own = !inherited(g);
    BOOL key = own && a->type == ACT_KEY, run = own && a->type == ACT_RUN;
    ShowWindow(GetDlgItem(g_dlg, IDC_G_KEY + g), key ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_dlg, IDC_G_RUN + g), run ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_dlg, IDC_G_BROWSE + g), run ? SW_SHOW : SW_HIDE);
    if (key) key_text(g);
    if (run) {
        g_loading = TRUE;
        SetDlgItemTextW(g_dlg, IDC_G_RUN + g, a->cmd);
        g_loading = FALSE;
    }
}

static void fill_combo(int g)
{
    HWND cb = GetDlgItem(g_dlg, IDC_G_COMBO + g);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    if (g_sel > 0) {
        WCHAR t[120];
        const Action *ga = &W.profiles[0].g[g];
        WCHAR detail[64] = L"";
        if (ga->type == ACT_KEY) combo_format(&ga->key, TRUE, detail, 64);
        _snwprintf(t, 120, L"Same as Global (%ls)", detail[0] ? detail : action_label(ga->type));
        t[119] = 0;
        int i = (int)SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t);
        SendMessageW(cb, CB_SETITEMDATA, i, INHERIT);
    }
    for (int t = 0; t < ACT_COUNT; t++) {
        WCHAR label[64];
        _snwprintf(label, 64, (t == ACT_KEY || t == ACT_RUN) ? L"%ls..." : L"%ls", action_label(t));
        int i = (int)SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)label);
        SendMessageW(cb, CB_SETITEMDATA, i, t);
        if (!inherited(g) && cur()->g[g].type == t) sel = i;
    }
    SendMessageW(cb, CB_SETCURSEL, sel, 0);
}

static void fill_rows(void)
{
    g_loading = TRUE;
    WCHAR t[120];
    if (g_sel == 0) {
        SetDlgItemTextW(g_dlg, IDC_ACTIONS_GRP, L"Actions: Global");
        SetDlgItemTextW(g_dlg, IDC_HINT, L"Used everywhere, except in applications listed on the left.");
    } else {
        _snwprintf(t, 120, L"Actions: %ls", cur()->exe);
        SetDlgItemTextW(g_dlg, IDC_ACTIONS_GRP, t);
        SetDlgItemTextW(g_dlg, IDC_HINT, L"Used while this application is in front.");
    }
    for (int g = 0; g < G_COUNT; g++) {
        fill_combo(g);
        update_param(g);
    }
    EnableWindow(GetDlgItem(g_dlg, IDC_REMOVE), g_sel > 0);
    g_loading = FALSE;
}

static void fill_apps(void)
{
    HWND lb = GetDlgItem(g_dlg, IDC_APPS);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"Global (all applications)");
    for (int i = 1; i < W.nprofiles; i++)
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)W.profiles[i].exe);
    SendMessageW(lb, LB_SETCURSEL, g_sel, 0);
}

static void set_int(int id, int v) { SetDlgItemInt(g_dlg, id, (UINT)v, FALSE); }
static int get_int(int id, int lo, int hi, int def)
{
    BOOL ok;
    int v = (int)GetDlgItemInt(g_dlg, id, &ok, FALSE);
    if (!ok) return def;
    return v < lo ? lo : v > hi ? hi : v;
}
static void check(int id, BOOL on) { CheckDlgButton(g_dlg, id, on ? BST_CHECKED : BST_UNCHECKED); }
static BOOL checked(int id) { return IsDlgButtonChecked(g_dlg, id) == BST_CHECKED; }

static void load_general(void)
{
    HWND led = GetDlgItem(g_dlg, IDC_LED);
    SendMessageW(led, CB_RESETCONTENT, 0, 0);
    SendMessageW(led, CB_ADDSTRING, 0, (LPARAM)L"Follows volume");
    SendMessageW(led, CB_ADDSTRING, 0, (LPARAM)L"On");
    SendMessageW(led, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendMessageW(led, CB_ADDSTRING, 0, (LPARAM)L"Pulse");
    SendMessageW(led, CB_SETCURSEL, W.led_mode, 0);
    SendDlgItemMessageW(g_dlg, IDC_BRIGHT, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255));
    SendDlgItemMessageW(g_dlg, IDC_BRIGHT, TBM_SETPAGESIZE, 0, 32);
    SendDlgItemMessageW(g_dlg, IDC_BRIGHT, TBM_SETPOS, TRUE, W.led_max);
    check(IDC_MUTEPULSE, W.mute_pulse);
    check(IDC_STARTUP, startup_enabled());
    set_int(IDC_LONGMS, W.long_press_ms);
    set_int(IDC_DBLMS, W.double_click_ms);
    set_int(IDC_SENS, W.sensitivity);
    set_int(IDC_ACCEL, W.acceleration);
    WCHAR b[32];
    _snwprintf(b, 32, L"%g", (double)W.opts.volume_step);
    SetDlgItemTextW(g_dlg, IDC_VOLSTEP, b);
    set_int(IDC_SCROLLSTEP, W.opts.scroll_step);
    check(IDC_INVERT, W.opts.scroll_invert);
    check(IDC_UNMUTE, W.opts.unmute_on_turn);
}

static void read_general(void)
{
    int led = (int)SendDlgItemMessageW(g_dlg, IDC_LED, CB_GETCURSEL, 0, 0);
    W.led_mode = led < 0 ? LED_VOLUME : led;
    W.led_max = (int)SendDlgItemMessageW(g_dlg, IDC_BRIGHT, TBM_GETPOS, 0, 0);
    W.mute_pulse = checked(IDC_MUTEPULSE);
    W.long_press_ms = get_int(IDC_LONGMS, 100, 5000, 500);
    W.double_click_ms = get_int(IDC_DBLMS, 100, 1000, 300);
    W.sensitivity = get_int(IDC_SENS, 1, 50, 1);
    W.acceleration = get_int(IDC_ACCEL, 0, 10, 2);
    WCHAR b[32];
    GetDlgItemTextW(g_dlg, IDC_VOLSTEP, b, 32);
    float v = (float)_wtof(b);
    W.opts.volume_step = (v > 0 && v <= 50) ? v : 1.0f;
    W.opts.scroll_step = get_int(IDC_SCROLLSTEP, 1, 1200, 40);
    W.opts.scroll_invert = checked(IDC_INVERT);
    W.opts.unmute_on_turn = checked(IDC_UNMUTE);
}

static BOOL apply(void)
{
    read_general();
    config_resolve(&W);
    if (!config_save(&W)) {
        MessageBoxW(g_dlg, L"Could not save the settings file.", L"PowerMate", MB_ICONERROR);
        return FALSE;
    }
    startup_set(checked(IDC_STARTUP));
    if (g_applied) g_applied();
    load_general();       /* show clamped values */
    return TRUE;
}

/* ---------------------------------------------------------------- key capture */

static BOOL is_modifier(WPARAM vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
        return TRUE;
    }
    return FALSE;
}

static WORD held_mods(void)
{
    WORD m = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= MOD_C;
    if (GetKeyState(VK_SHIFT) & 0x8000)   m |= MOD_S;
    if (GetKeyState(VK_MENU) & 0x8000)    m |= MOD_A;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= MOD_W;
    return m;
}

static LRESULT CALLBACK key_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    int g = (int)ref;
    (void)id;
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (is_modifier(wp)) {
            KeyCombo k = { held_mods(), 0 };
            WCHAR t[80] = L"";
            static const WCHAR *P[4] = { L"Ctrl+", L"Shift+", L"Alt+", L"Win+" };
            for (int i = 0; i < 4; i++) if (k.mods & (1 << i)) wcscat(t, P[i]);
            wcscat(t, L"...");
            SetWindowTextW(h, t);
        } else {
            Action *a = &cur()->g[g];
            a->key.mods = held_mods();
            a->key.vk = (WORD)wp;
            key_text(g);
        }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (is_modifier(wp) && !held_mods()) key_text(g);
        return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_CONTEXTMENU:
        return 0;
    case WM_SETFOCUS:
        SetWindowTextW(h, L"Press a key combination");
        break;
    case WM_KILLFOCUS:
        key_text(g);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, key_proc, 0);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- add application */

typedef struct { WCHAR exe[40][64]; int n; } AppList;

static BOOL CALLBACK enum_win(HWND w, LPARAM lp)
{
    AppList *l = (AppList *)lp;
    if (l->n >= 40 || !IsWindowVisible(w) || GetWindow(w, GW_OWNER) ||
        (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || GetWindowTextLengthW(w) == 0)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return TRUE;
    WCHAR path[MAX_PATH], exe[64];
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(p, 0, path, &len)) {
        exe_from_path(path, exe, 64);
        BOOL dup = !_wcsicmp(exe, L"powermate.exe") || !_wcsicmp(exe, L"explorer.exe") ||
                   !_wcsicmp(exe, L"applicationframehost.exe") || !_wcsicmp(exe, L"textinputhost.exe");
        for (int i = 0; i < l->n && !dup; i++) dup = !_wcsicmp(l->exe[i], exe);
        for (int i = 1; i < W.nprofiles && !dup; i++) dup = !_wcsicmp(W.profiles[i].exe, exe);
        if (!dup) wcscpy(l->exe[l->n++], exe);
    }
    CloseHandle(p);
    return TRUE;
}

static void add_profile(const WCHAR *exe)
{
    for (int i = 1; i < W.nprofiles; i++)
        if (!_wcsicmp(W.profiles[i].exe, exe)) { g_sel = i; fill_apps(); fill_rows(); return; }
    if (W.nprofiles >= MAX_PROFILES) {
        MessageBoxW(g_dlg, L"Too many applications.", L"PowerMate", MB_ICONWARNING);
        return;
    }
    Profile *p = &W.profiles[W.nprofiles];
    ZeroMemory(p, sizeof *p);
    wcsncpy(p->exe, exe, 63);
    _wcslwr(p->exe);
    g_sel = W.nprofiles++;
    config_resolve(&W);
    fill_apps();
    fill_rows();
}

static BOOL browse(const WCHAR *filter, WCHAR *out, DWORD n)
{
    OPENFILENAMEW of = { sizeof of };
    out[0] = 0;
    of.hwndOwner = g_dlg;
    of.lpstrFilter = filter;
    of.lpstrFile = out;
    of.nMaxFile = n;
    of.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&of);
}

static void add_menu(void)
{
    static AppList l;
    l.n = 0;
    EnumWindows(enum_win, (LPARAM)&l);
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < l.n; i++) AppendMenuW(m, MF_STRING, 100 + i, l.exe[i]);
    if (l.n) AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 1, L"Browse for a program...");
    RECT r;
    GetWindowRect(GetDlgItem(g_dlg, IDC_ADD), &r);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, g_dlg, NULL);
    DestroyMenu(m);
    if (cmd >= 100 && cmd < 100 + l.n) {
        add_profile(l.exe[cmd - 100]);
    } else if (cmd == 1) {
        WCHAR path[MAX_PATH], exe[64];
        if (browse(L"Programs (*.exe)\0*.exe\0", path, MAX_PATH)) {
            exe_from_path(path, exe, 64);
            add_profile(exe);
        }
    }
}

static void remove_profile(void)
{
    if (g_sel <= 0) return;
    memmove(&W.profiles[g_sel], &W.profiles[g_sel + 1], (size_t)(W.nprofiles - g_sel - 1) * sizeof(Profile));
    W.nprofiles--;
    if (g_sel >= W.nprofiles) g_sel = W.nprofiles - 1;
    fill_apps();
    fill_rows();
}

/* ---------------------------------------------------------------- dialog */

static void on_combo(int g)
{
    HWND cb = GetDlgItem(g_dlg, IDC_G_COMBO + g);
    int i = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
    if (i < 0) return;
    int t = (int)SendMessageW(cb, CB_GETITEMDATA, i, 0);
    Profile *p = cur();
    if (t == INHERIT) {
        p->set &= ~(1u << g);
        p->g[g] = W.profiles[0].g[g];
    } else {
        p->set |= 1u << g;
        p->g[g].type = t;
    }
    if (g_sel == 0) {
        config_resolve(&W);    /* apps that inherit follow the new global action */
    }
    update_param(g);
    if (t == ACT_KEY && !p->g[g].key.vk)
        SetFocus(GetDlgItem(g_dlg, IDC_G_KEY + g));
    if (t == ACT_RUN && !p->g[g].cmd[0])
        PostMessageW(g_dlg, WM_COMMAND, MAKEWPARAM(IDC_G_BROWSE + g, BN_CLICKED), 0);
}

static void on_browse(int g)
{
    WCHAR path[MAX_PATH];
    if (!browse(L"Programs and files\0*.exe;*.bat;*.cmd;*.lnk;*.url\0All files\0*.*\0", path, MAX_PATH))
        return;
    Action *a = &cur()->g[g];
    if (wcschr(path, L' ')) _snwprintf(a->cmd, 260, L"\"%ls\"", path);
    else wcsncpy(a->cmd, path, 259);
    a->cmd[259] = 0;
    update_param(g);
}

static INT_PTR CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        g_dlg = h;
        SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_ON)));
        SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_ON), IMAGE_ICON,
                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        for (int g = 0; g < G_COUNT; g++) {
            SetDlgItemTextW(h, IDC_G_LABEL + g, gesture_label(g));
            SetWindowSubclass(GetDlgItem(h, IDC_G_KEY + g), key_proc, 0, (DWORD_PTR)g);
        }
        config_load(&W);
        g_sel = 0;
        fill_apps();
        fill_rows();
        load_general();
        return TRUE;

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= IDC_G_COMBO && id < IDC_G_COMBO + G_COUNT && code == CBN_SELCHANGE && !g_loading)
            on_combo(id - IDC_G_COMBO);
        else if (id >= IDC_G_BROWSE && id < IDC_G_BROWSE + G_COUNT && code == BN_CLICKED)
            on_browse(id - IDC_G_BROWSE);
        else if (id >= IDC_G_RUN && id < IDC_G_RUN + G_COUNT && code == EN_CHANGE && !g_loading)
            GetDlgItemTextW(h, id, cur()->g[id - IDC_G_RUN].cmd, 260);
        else if (id == IDC_APPS && code == LBN_SELCHANGE) {
            int s = (int)SendDlgItemMessageW(h, IDC_APPS, LB_GETCURSEL, 0, 0);
            if (s >= 0 && s < W.nprofiles) { g_sel = s; fill_rows(); }
        }
        else if (id == IDC_ADD)    add_menu();
        else if (id == IDC_REMOVE) remove_profile();
        else if (id == IDC_APPLY)  apply();
        else if (id == IDOK)       { if (apply()) DestroyWindow(h); }
        else if (id == IDCANCEL)   DestroyWindow(h);
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return TRUE;
    case WM_DESTROY:
        g_dlg = NULL;
        return TRUE;
    }
    return FALSE;
}

void settings_open(HINSTANCE inst, void (*applied)(void))
{
    if (g_dlg) {
        ShowWindow(g_dlg, SW_RESTORE);
        SetForegroundWindow(g_dlg);
        return;
    }
    g_inst = inst;
    g_applied = applied;
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    HWND h = CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS), NULL, dlg_proc, 0);
    if (h) {
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
    }
}

BOOL settings_dialog_msg(MSG *m) { return g_dlg && IsDialogMessageW(g_dlg, m); }
BOOL settings_foreground(void)   { return g_dlg && GetForegroundWindow() == g_dlg; }

void settings_gesture(const WCHAR *text)
{
    if (g_dlg) SetDlgItemTextW(g_dlg, IDC_STATUS, text);
}
