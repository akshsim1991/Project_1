// Common.h - shared Windows includes, message ids and command ids.
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

#include <cstdint>
#include <string>
#include <vector>

#define APP_NAME L"Startup Manager"
#define APP_VERSION L"1.0.0"
#define APP_COPYRIGHT L"\x00A9 2026 Akshaya Simha"
#define APP_WINDOW_CLASS L"StartupManagerMain"
#define APP_REG_KEY L"Software\\StartupManager"

// Messages posted from the worker thread to the main window. lParam carries
// a heap object that the receiver takes ownership of.
enum : UINT {
    WM_APP_ENTRIES = WM_APP + 1,  // EntryList*: everything that starts with Windows
    WM_APP_OP_DONE,               // OpResult*: a change finished
    WM_APP_DETAILS_READY,         // to the details dialog: signature etc. loaded
};

// Command ids (toolbar, menus, accelerators).
enum : int {
    ID_REFRESH = 100,
    ID_ENABLE,
    ID_DISABLE,
    ID_DELETE,
    ID_ADD,
    ID_DETAILS,
    ID_UNDO,
    ID_RESTORE_DELETED,
    ID_SEARCH_EDIT,
    ID_FILTER_MENU,
    ID_MORE_MENU,
    ID_FOCUS_SEARCH,
    ID_SELECT_ALL,
    ID_COPY_NAMES,
    ID_COPY_DETAILS,
    ID_OPEN_FILE_LOCATION,
    ID_OPEN_ENTRY_LOCATION,
    ID_SEARCH_ONLINE,
    ID_VIRUSTOTAL,
    ID_OPEN_BACKUPS,
    ID_SETTINGS,
    ID_ABOUT,
    ID_ELEVATE,
    ID_EXIT,
    ID_STATUS_TEXT,
    ID_STATUS_OP,
    ID_FILTER_FIRST = 200,  // + Filter value
    ID_COLUMN_FIRST = 300,  // + column index (show/hide)
};
