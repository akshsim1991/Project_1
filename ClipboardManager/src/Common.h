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

#define APP_NAME L"Clipboard Manager"
#define APP_VERSION L"1.0.0"
#define APP_COPYRIGHT L"\x00A9 2026 Akshaya Simha"
#define APP_WINDOW_CLASS L"ClipboardManagerMain"
#define APP_REG_KEY L"Software\\ClipboardManager"
#define APP_RUN_VALUE L"ClipboardManager"
#define APP_FOLDER_NAME L"Clipboard contents"

enum : UINT {
    WM_APP_TRAY = WM_APP + 1,  // tray icon events
    WM_APP_SHOW,               // another copy was started: show the window
};

enum : int {
    ID_SEARCH = 100,
    ID_FILTER_MENU,
    ID_COPY,
    ID_PASTE,
    ID_PIN,
    ID_DELETE,
    ID_OPEN_FOLDER,
    ID_MORE_MENU,
    ID_SAVE_AS,
    ID_SHOW_FILE,
    ID_CLEAR_UNPINNED,
    ID_PAUSE,
    ID_SETTINGS,
    ID_ABOUT,
    ID_EXIT,
    ID_OPEN,
    ID_HIDE,
    ID_FOCUS_SEARCH,
    ID_SELECT_ALL,
    ID_AUTOSTART,
    ID_STATUS_TEXT,
    ID_STATUS_PAUSE,
    ID_COPY_PLAIN_PATHS,
    ID_OPEN_ITEM,
    ID_FILTER_FIRST = 200,  // + Filter
    ID_RECENT_FIRST = 300,  // + index: tray menu, copy a recent item
};

enum Filter : int { kFilterAll, kFilterText, kFilterImages, kFilterFiles, kFilterPinned, kFilterToday, kFilterCount };
