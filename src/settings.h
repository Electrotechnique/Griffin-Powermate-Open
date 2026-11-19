#pragma once
#include <windows.h>

/* Modeless settings window. `applied` runs after settings are saved. */
void settings_open(HINSTANCE inst, void (*applied)(void));
BOOL settings_dialog_msg(MSG *m);      /* call from the message loop */
BOOL settings_foreground(void);        /* settings window is the active window */
void settings_gesture(const WCHAR *text);

/* implemented in main.c */
BOOL startup_enabled(void);
void startup_set(BOOL on);
