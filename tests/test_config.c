/* Parser/config tests. Run: make test */
#include <stdio.h>
#include <wchar.h>
#include "../src/config.h"

void log_init(BOOL c) { (void)c; }
void log_printf(const WCHAR *f, ...) { (void)f; }

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)

static void write_file(const char *text)
{
    FILE *f = _wfopen(config_path(), L"wb");
    fputs(text, f);
    fclose(f);
}

int wmain(void)
{
    Action a;
    WCHAR buf[300];
    CHECK(action_parse(L"volume_up", &a) && a.type == ACT_VOLUME_UP);
    CHECK(action_parse(L"  Scroll_Down", &a) && a.type == ACT_SCROLL_DOWN);
    CHECK(action_parse(L"", &a) && a.type == ACT_NONE);
    CHECK(action_parse(L"key:ctrl+shift+tab", &a) && a.type == ACT_KEY &&
          a.key.vk == VK_TAB && a.key.mods == (MOD_C | MOD_S));
    CHECK(action_parse(L"key:win+shift+s", &a) && a.key.vk == 'S' && a.key.mods == (MOD_W | MOD_S));
    CHECK(action_parse(L"key:f12", &a) && a.key.vk == VK_F12);
    CHECK(action_parse(L"key:vkba", &a) && a.key.vk == 0xBA);
    CHECK(action_parse(L"run:\"C:\\x y\\a.exe\" -b", &a) && a.type == ACT_RUN);
    CHECK(!action_parse(L"key:ctrl+", &a));
    CHECK(!action_parse(L"key:a+b", &a));
    CHECK(!action_parse(L"bogus", &a));

    /* round trips */
    action_parse(L"key:ctrl+alt+pgdn", &a);
    action_format(&a, buf, 300);
    CHECK(!wcscmp(buf, L"key:ctrl+alt+pgdn"));
    action_parse(L"key:vkba", &a);
    action_format(&a, buf, 300);
    CHECK(!wcscmp(buf, L"key:vkba"));
    for (int t = 0; t < ACT_COUNT; t++) {
        if (t == ACT_KEY || t == ACT_RUN) continue;
        Action b = { t };
        action_format(&b, buf, 300);
        CHECK(action_parse(buf, &a) && a.type == t);
    }

    /* first run writes defaults */
    DeleteFileW(config_path());
    Config c, d;
    config_load(&c);
    CHECK(c.errors == 0);
    CHECK(c.led_mode == LED_VOLUME && c.led_max == 255);
    CHECK(c.profiles[0].g[G_ROTATE_RIGHT].type == ACT_VOLUME_UP);
    CHECK(c.profiles[0].g[G_CLICK].type == ACT_MUTE);
    const Profile *p = config_profile(&c, L"chrome.exe");
    CHECK(p != &c.profiles[0] && p->g[G_PRESS_ROTATE_RIGHT].type == ACT_KEY);
    CHECK(p->g[G_ROTATE_RIGHT].type == ACT_VOLUME_UP);           /* inherited */
    CHECK(!(p->set & (1u << G_ROTATE_RIGHT)));
    CHECK(config_profile(&c, L"notepad.exe") == &c.profiles[0]);

    /* save / load round trip keeps inheritance and settings */
    c.opts.volume_step = 0.5f;
    c.profiles[0].g[G_LONG_ROTATE_RIGHT].type = ACT_ZOOM_IN;
    action_parse(L"run:\"C:\\Program Files\\\x00e9t\x00e9\\app.exe\"", &c.profiles[1].g[G_CLICK]);
    c.profiles[1].set |= 1u << G_CLICK;
    CHECK(config_save(&c));
    config_load(&d);
    CHECK(d.errors == 0 && d.nprofiles == c.nprofiles);
    CHECK(d.opts.volume_step == 0.5f);
    CHECK(d.profiles[0].g[G_LONG_ROTATE_RIGHT].type == ACT_ZOOM_IN);
    CHECK(d.profiles[1].g[G_LONG_ROTATE_RIGHT].type == ACT_ZOOM_IN);  /* inherited */
    CHECK(d.profiles[1].g[G_CLICK].type == ACT_RUN && !wcscmp(d.profiles[1].g[G_CLICK].cmd, c.profiles[1].g[G_CLICK].cmd));
    CHECK(d.profiles[1].set == c.profiles[1].set);

    /* version 1.0 files still load */
    write_file("[general]\nvolume_step=2 ; comment\n[default]\nrotate=volume\npress_rotate=track\nclick=mute\n"
               "[app:Code.exe]\nrotate=keys:ctrl+tab|ctrl+shift+tab\nclick=nonsense\n");
    config_load(&c);
    CHECK(c.errors == 1);
    CHECK(c.opts.volume_step == 2.0f);
    CHECK(c.profiles[0].g[G_ROTATE_LEFT].type == ACT_VOLUME_DOWN);
    CHECK(c.profiles[0].g[G_PRESS_ROTATE_RIGHT].type == ACT_NEXT);
    p = config_profile(&c, L"code.exe");
    CHECK(p->g[G_ROTATE_RIGHT].type == ACT_KEY && p->g[G_ROTATE_RIGHT].key.mods == MOD_C);
    CHECK(p->g[G_ROTATE_LEFT].key.mods == (MOD_C | MOD_S));
    CHECK(p->g[G_CLICK].type == ACT_MUTE);
    DeleteFileW(config_path());

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
