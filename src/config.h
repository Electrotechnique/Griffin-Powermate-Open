#pragma once
#include "actions.h"

enum { LED_VOLUME, LED_ON, LED_OFF, LED_PULSE };

typedef struct {
    WCHAR  exe[64];            /* lower-case exe name; empty = default profile */
    Action rotate, press_rotate, click, double_click, long_press;
} Profile;

#define MAX_PROFILES 32

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
    Profile    profiles[MAX_PROFILES];   /* [0] = default */
    int        nprofiles;
    int        errors;         /* unparseable entries on last load */
} Config;

const WCHAR *config_path(void);
void config_load(Config *c);          /* writes a default file if none exists */
const Profile *config_profile(const Config *c, const WCHAR *exe);
