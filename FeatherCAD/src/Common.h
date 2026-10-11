// Common.h - shared Windows includes, application constants and command ids.
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

#define APP_NAME L"Feather CAD"
#define APP_VERSION L"1.0.1"
#define APP_AUTHOR L"Akshaya Simha"
#define APP_COPYRIGHT L"\x00A9 2026 Akshaya Simha"
#define APP_WINDOW_CLASS L"FeatherCadMain"
#define APP_REG_KEY L"Software\\FeatherCAD"
#define APP_FILE_EXT L".fcad"

// Messages from child windows to the main window.
enum : UINT {
    WM_APP_COMMAND_TEXT = WM_APP + 1,  // command line: Enter (lParam = std::wstring*)
    WM_APP_CANVAS_STATE,               // canvas: cursor or selection changed (refresh bars)
    WM_APP_CANVAS_MENU,                // canvas: right-click (lParam = MAKELPARAM(screen x, y))
    WM_APP_COMMAND_KEY,                // command line: a key it does not use (wParam = VK)
};

// Command ids: menu items, toolbar buttons and accelerators. Items that
// run an engine command carry its name in kCommandIds (MainWindow.cpp).
enum : int {
    ID_FIRST = 100,
    // File
    ID_NEW = ID_FIRST,
    ID_OPEN,
    ID_SAVE,
    ID_SAVE_AS,
    ID_EXPORT_DXF,
    ID_HISTORY,
    ID_RECOVER,
    ID_EXIT,
    // Edit
    ID_UNDO,
    ID_REDO,
    ID_SELECT_ALL,
    ID_ERASE,
    ID_COMMAND_PALETTE,
    // Draw
    ID_LINE,
    ID_PLINE,
    ID_CIRCLE,
    ID_CIRCLE_2P,
    ID_CIRCLE_3P,
    ID_ARC,
    ID_ARC_CENTRE,
    ID_RECTANGLE,
    // Modify
    ID_MOVE,
    ID_COPY,
    ID_ROTATE,
    // View
    ID_ZOOM_EXTENTS,
    ID_ZOOM_WINDOW,
    ID_ZOOM_PREVIOUS,
    ID_ZOOM_IN,
    ID_ZOOM_OUT,
    ID_PAN,
    ID_THEME_SYSTEM,
    ID_THEME_LIGHT,
    ID_THEME_DARK,
    // Tools
    ID_DIST,
    ID_ID_POINT,
    ID_TOGGLE_GRID,
    ID_TOGGLE_SNAP,
    ID_TOGGLE_ORTHO,
    ID_TOGGLE_OSNAP,
    ID_OSNAP_SETTINGS,
    ID_DRAFTING_SETTINGS,
    ID_UNITS,
    ID_AUTOSAVE_SETTINGS,
    // Help
    ID_COMMAND_LIST,
    ID_ABOUT,
    // Menu strip buttons (open a drop-down menu)
    ID_MENU_FILE,
    ID_MENU_EDIT,
    ID_MENU_VIEW,
    ID_MENU_DRAW,
    ID_MENU_MODIFY,
    ID_MENU_TOOLS,
    ID_MENU_HELP,
    // Context menu
    ID_CTX_ENTER,
    ID_CTX_CANCEL,
    ID_CTX_REPEAT,
    ID_ESCAPE,
    ID_TOGGLE_HISTORY,  // F2: more command history lines
    ID_TIPS,
    ID_RECENT_FIRST = 300,   // + index into the recent files (up to 10)
    ID_CTX_KEYWORD = 330,    // + index into the prompt's options
    ID_CTX_RECENT_CMD = 360, // + index into the recent commands
    ID_OSNAP_FIRST = 380,    // + bit index of a snap mode (one-shot snap)
    ID_PALETTE_CMD = 400,    // + index into AllCommands()
};
