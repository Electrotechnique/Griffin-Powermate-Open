#pragma once
#include <windows.h>

/* Every action is one-directional; rotation runs it once per step. */
enum {
    ACT_NONE = 0,
    ACT_VOLUME_UP, ACT_VOLUME_DOWN,
    ACT_VOLUME_UP_OSD, ACT_VOLUME_DOWN_OSD,
    ACT_MUTE, ACT_PLAY_PAUSE, ACT_NEXT, ACT_PREV, ACT_STOP,
    ACT_SCROLL_UP, ACT_SCROLL_DOWN, ACT_SCROLL_LEFT, ACT_SCROLL_RIGHT,
    ACT_ZOOM_IN, ACT_ZOOM_OUT,
    ACT_LEFT_CLICK, ACT_RIGHT_CLICK, ACT_MIDDLE_CLICK,
    ACT_KEY,           /* key:<combo> */
    ACT_RUN,           /* run:<command line> */
    ACT_COUNT
};

#define MOD_C 1
#define MOD_S 2
#define MOD_A 4
#define MOD_W 8

typedef struct { WORD mods, vk; } KeyCombo;

typedef struct {
    int      type;
    KeyCombo key;              /* ACT_KEY */
    WCHAR    cmd[260];         /* ACT_RUN */
} Action;

typedef struct {
    float volume_step;    /* percent per step */
    int scroll_step;      /* wheel units per step (120 = one notch) */
    int scroll_invert;
    int unmute_on_turn;
} ActionOpts;

const WCHAR *action_id(int type);      /* INI name, e.g. "volume_up" */
const WCHAR *action_label(int type);   /* UI name, e.g. "Volume up" */

BOOL action_parse(const WCHAR *s, Action *out);       /* FALSE on syntax error */
void action_format(const Action *a, WCHAR *out, size_t n);
BOOL combo_parse(const WCHAR *s, KeyCombo *k);
void combo_format(const KeyCombo *k, BOOL pretty, WCHAR *out, size_t n);

void action_do(const Action *a, int times, const ActionOpts *o);

void  audio_init(void);
void  audio_refresh(void);                 /* re-acquire default output device */
BOOL  audio_get(float *vol, BOOL *muted);
void  audio_shutdown(void);
