#pragma once
#include "actions.h"

enum { LED_VOLUME, LED_ON, LED_OFF, LED_PULSE };

enum {
    G_ROTATE_LEFT, G_ROTATE_RIGHT,
    G_PRESS_ROTATE_LEFT, G_PRESS_ROTATE_RIGHT,
    G_LONG_ROTATE_LEFT, G_LONG_ROTATE_RIGHT,
    G_CLICK, G_DOUBLE_CLICK, G_LONG_PRESS,
    G_COUNT
};

typedef struct {
    WCHAR  exe[64];            /* lower-case exe name; empty = global profile */
    Action g[G_COUNT];
    UINT   set;                /* bit per gesture defined in this profile (app profiles inherit the rest) */
} Profile;

#define MAX_PROFILES 64

typedef struct {
    int        led_mode;
    int        led_max;        /* 0..255 */
    int        mute_pulse;     /* LED_VOLUME: breathe while muted */
    int        pulse_ms;       /* breathing period */
    int        long_press_ms;
    int        double_click_ms;
    int        sensitivity;    /* detents per step */
    int        acceleration;   /* 0 = off, else multiplier for fast spins */
    ActionOpts opts;
    Profile    profiles[MAX_PROFILES];   /* [0] = global */
    int        nprofiles;
    int        errors;         /* unparseable entries on last load */
} Config;

const WCHAR *gesture_id(int g);      /* INI key */
const WCHAR *gesture_label(int g);   /* UI text */

const WCHAR *config_path(void);
void config_defaults(Config *c);
void config_load(Config *c);         /* writes a default file if none exists */
BOOL config_save(const Config *c);
void config_resolve(Config *c);      /* fill app-profile gaps from the global profile */
const Profile *config_profile(const Config *c, const WCHAR *exe);
