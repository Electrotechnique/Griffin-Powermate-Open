/* Parser/config tests. Run: make test (needs wine on Linux). */
#include <stdio.h>
#include <wchar.h>
#include "../src/config.h"

void log_init(BOOL c) { (void)c; }
void log_printf(const WCHAR *f, ...) { (void)f; }

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)

int wmain(void)
{
    Action a;
    CHECK(action_parse(L"volume", &a) && a.type == ACT_VOLUME);
    CHECK(action_parse(L"  Scroll", &a) && a.type == ACT_SCROLL);
    CHECK(action_parse(L"", &a) && a.type == ACT_NONE);
    CHECK(action_parse(L"keys:ctrl+tab|ctrl+shift+tab", &a) && a.type == ACT_KEYS &&
          a.cw.vk == VK_TAB && a.cw.mods == MOD_C && a.ccw.mods == (MOD_C | MOD_S));
    CHECK(action_parse(L"key:win+shift+s", &a) && a.cw.vk == 'S' && a.cw.mods == (MOD_W | MOD_S));
    CHECK(action_parse(L"key:f12", &a) && a.cw.vk == VK_F12);
    CHECK(action_parse(L"keys:left|right", &a) && a.cw.vk == VK_LEFT && a.ccw.vk == VK_RIGHT);
    CHECK(action_parse(L"run:\"C:\\x y\\a.exe\" -b", &a) && a.type == ACT_RUN);
    CHECK(!action_parse(L"keys:ctrl+tab", &a));
    CHECK(!action_parse(L"key:ctrl+", &a));
    CHECK(!action_parse(L"key:a+b", &a));
    CHECK(!action_parse(L"bogus", &a));

    /* config: default file is written and loads cleanly */
    DeleteFileW(config_path());
    Config c;
    config_load(&c);
    CHECK(c.errors == 0);
    CHECK(c.led_mode == LED_VOLUME && c.led_max == 255);
    CHECK(c.opts.volume_step == 1.0f && c.opts.scroll_step == 40);
    CHECK(c.profiles[0].rotate.type == ACT_VOLUME && c.profiles[0].click.type == ACT_MUTE);
    CHECK(c.profiles[0].double_click.type == ACT_NONE);
    const Profile *p = config_profile(&c, L"chrome.exe");
    CHECK(p != &c.profiles[0] && p->press_rotate.type == ACT_KEYS && p->rotate.type == ACT_VOLUME);
    CHECK(config_profile(&c, L"notepad.exe") == &c.profiles[0]);

    WritePrivateProfileStringW(L"general", L"volume_step", L"0.5 ; half", config_path());
    WritePrivateProfileStringW(L"app:Code.exe", L"rotate", L"scroll", config_path());
    WritePrivateProfileStringW(L"app:Code.exe", L"click", L"nonsense", config_path());
    config_load(&c);
    CHECK(c.opts.volume_step == 0.5f);
    CHECK(c.errors == 1);
    p = config_profile(&c, L"code.exe");
    CHECK(p->rotate.type == ACT_SCROLL && p->click.type == ACT_MUTE);
    DeleteFileW(config_path());

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
