#pragma once
#include <windows.h>

enum {
    ACT_NONE = 0,
    /* rotation */
    ACT_VOLUME,        /* fine-grained master volume (Core Audio) */
    ACT_VOLUME_KEYS,   /* volume keys: native Windows OSD, 2% steps */
    ACT_SCROLL,        /* vertical mouse wheel */
    ACT_HSCROLL,       /* horizontal mouse wheel */
    ACT_ZOOM,          /* Ctrl + wheel */
    ACT_TRACK,         /* next / previous track */
    ACT_KEYS,          /* keys:<clockwise>|<anticlockwise> */
    /* button */
    ACT_MUTE,
    ACT_PLAY_PAUSE,
    ACT_NEXT,
    ACT_PREV,
    ACT_STOP,
    ACT_KEY,           /* key:<combo> */
    ACT_RUN,           /* run:<command line> */
};

#define MOD_C 1
#define MOD_S 2
#define MOD_A 4
#define MOD_W 8

typedef struct { WORD mods, vk; } KeyCombo;

typedef struct {
    int      type;
    KeyCombo cw, ccw;          /* ACT_KEYS: cw/ccw; ACT_KEY: cw */
    WCHAR    cmd[260];         /* ACT_RUN */
} Action;

typedef struct {
    float volume_step;    /* percent per step */
    int scroll_step;      /* wheel units per step (120 = one notch) */
    int scroll_invert;
    int unmute_on_turn;
} ActionOpts;

BOOL action_parse(const WCHAR *s, Action *out);  /* FALSE on syntax error */
void action_rotate(const Action *a, int steps, const ActionOpts *o);
void action_press(const Action *a);

void  audio_init(void);
void  audio_refresh(void);                 /* re-acquire default output device */
BOOL  audio_get(float *vol, BOOL *muted);
void  audio_shutdown(void);
