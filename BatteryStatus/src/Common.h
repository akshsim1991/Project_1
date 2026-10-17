// Common.h - shared Windows includes, messages and command ids.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#define APP_NAME L"Battery Status"
#define APP_VERSION L"2.0.0"
#define APP_COPYRIGHT L"\x00A9 2026 Akshaya Simha"
#define APP_WINDOW_CLASS L"BatteryStatusMain"
#define APP_REG_KEY L"Software\\BatteryStatus"
#define APP_RUN_VALUE L"BatteryStatus"
#define APP_OLD_RUN_VALUE L"BatteryMonitorApp"  // used by version 1.2

enum : UINT {
    WM_APP_TRAY = WM_APP + 1,  // tray icon events
    WM_APP_SHOW,               // another copy was started: show the window
};

enum : int {
    ID_RANGE_1H = 100,
    ID_RANGE_6H,
    ID_RANGE_24H,
    ID_RANGE_7D,
    ID_REPORT,
    ID_EXPORT,
    ID_MORE_MENU,
    ID_SETTINGS,
    ID_ABOUT,
    ID_EXIT,
    ID_OPEN,
    ID_PAUSE_ALERTS,
    ID_RESUME_ALERTS,
    ID_AUTOSTART,
    ID_POWER_OPTIONS,
    ID_COPY_SUMMARY,
    ID_HIDE,
};
