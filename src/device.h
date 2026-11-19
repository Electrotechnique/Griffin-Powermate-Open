/* Griffin PowerMate (USB) HID access. */
#pragma once
#include <windows.h>

#define PM_VID          0x077D
#define PM_PID_POWERMATE 0x0410
#define PM_PID_SOUNDKNOB 0x04AA

/* Posted to the notify window by the reader thread.
 * WM_PM_INPUT: wParam = button state (0/1), lParam = signed rotation delta. */
#define WM_PM_INPUT (WM_APP + 1)
#define WM_PM_LOST  (WM_APP + 2)

BOOL  pm_open(HWND notify);     /* find, open and start reading; TRUE if open */
void  pm_close(void);
BOOL  pm_is_open(void);
BOOL  pm_has_led(void);
void  pm_set_led(BYTE level);   /* 0..255, skipped if unchanged */
void  pm_led_invalidate(void);  /* force next pm_set_led to be sent */
