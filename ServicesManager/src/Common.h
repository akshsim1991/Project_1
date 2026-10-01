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

#define APP_NAME L"Windows Services Manager"
#define APP_VERSION L"2.1.0"
#define APP_COPYRIGHT L"\x00A9 2026 Akshaya Simha"
#define APP_WINDOW_CLASS L"WindowsServicesManagerMain"
#define APP_REG_KEY L"Software\\WindowsServicesManager"

// Messages posted from the worker thread to the main window. lParam carries
// a heap object that the receiver takes ownership of.
enum : UINT {
    WM_APP_SNAPSHOT = WM_APP + 1,  // Snapshot*: the full service list
    WM_APP_OP_PROGRESS,            // OpProgress*: an action started on one service
    WM_APP_OP_DONE,                // OpBatchResult*: a batch of actions finished
    WM_APP_DETAILS_READY,          // to a details page: background facts loaded
    WM_APP_EVENTS_READY,           // to the History page: event-log entries loaded
    WM_APP_ADVICE_READY,           // online advice downloaded (or failed)
};

// Command ids (toolbar, menus, accelerators).
enum : int {
    ID_REFRESH = 100,
    ID_START,
    ID_STOP,
    ID_RESTART,
    ID_PAUSE,
    ID_RESUME,
    ID_KILL,
    ID_STARTTYPE_MENU,
    ID_SET_AUTOMATIC,
    ID_SET_DELAYED,
    ID_SET_MANUAL,
    ID_SET_DISABLED,
    ID_SEARCH_EDIT,
    ID_FILTER_MENU,
    ID_MORE_MENU,
    ID_FOCUS_SEARCH,
    ID_CLEAR_SEARCH,
    ID_SELECT_ALL,
    ID_COPY_NAMES,
    ID_COPY_DETAILS,
    ID_OPEN_LOCATION,
    ID_SEARCH_ONLINE,
    ID_SETTINGS,
    ID_ABOUT,
    ID_ELEVATE,
    ID_EXIT,
    ID_CANCEL_OPS,
    ID_STATUS_TEXT,
    ID_STATUS_OP,
    ID_DETAILS,
    ID_PROFILES_MENU,
    ID_UNDO,
    ID_SAVE_SNAPSHOT,
    ID_RESTORE_SNAPSHOT,
    ID_OPEN_SNAPSHOTS,
    ID_SAVE_PROFILE,
    ID_OPEN_PROFILES,
    ID_FILTER_FIRST = 200,  // + Filter value
    ID_COLUMN_FIRST = 300,  // + column index (show/hide)
    ID_PROFILE_FIRST = 400, // + index into the Profiles menu's list
};
