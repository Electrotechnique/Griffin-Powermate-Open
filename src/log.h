#pragma once
#include <windows.h>

/* Logging goes to a console only when started with --console. */
void log_init(BOOL console);
void log_printf(const WCHAR *fmt, ...);
#define LOG(...) log_printf(__VA_ARGS__)
