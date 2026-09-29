// Common.h - shared Windows includes, message ids, command ids and small
// value types used across modules.
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

#define APP_NAME L"Feather PDF"
#define APP_VERSION L"1.0.0"
#define APP_REG_KEY L"Software\\FeatherPDF"

// ---------------------------------------------------------------------------
// Messages posted from the render worker thread to the main window.
// lParam always carries a heap object that the receiver takes ownership of.
// ---------------------------------------------------------------------------
enum : UINT {
    WM_APP_DOC_LOADED = WM_APP + 1,  // DocLoadResult*
    WM_APP_TILE_READY,               // TileResult*
    WM_APP_SEARCH_RESULT,            // SearchPageResult*
};

// ---------------------------------------------------------------------------
// Command ids (toolbar buttons, menu items, accelerators).
// ---------------------------------------------------------------------------
enum : int {
    ID_OPEN = 100,
    ID_PREV_PAGE,
    ID_NEXT_PAGE,
    ID_FIRST_PAGE,
    ID_LAST_PAGE,
    ID_PAGE_EDIT,
    ID_PAGE_TOTAL,
    ID_GOTO_PAGE,
    ID_ZOOM_IN,
    ID_ZOOM_OUT,
    ID_ZOOM_LABEL,
    ID_FIT_WIDTH,
    ID_FIT_PAGE,
    ID_ACTUAL_SIZE,
    ID_CONTINUOUS,
    ID_SINGLE_PAGE,
    ID_FULLSCREEN,
    ID_SEARCH,
    ID_MENU,
    ID_SEARCH_EDIT,
    ID_FIND_NEXT,
    ID_FIND_PREV,
    ID_MATCH_CASE,
    ID_SEARCH_STATUS,
    ID_SEARCH_CLOSE,
    ID_ESCAPE,
    ID_REGISTER_DEFAULT,
    ID_ABOUT,
    ID_EXIT,
    ID_ZOOM_PRESET_FIRST = 300,  // + index into kZoomPresets
};

// Page geometry in PDF points (1/72 inch), already adjusted for /Rotate.
struct SizeF {
    float w = 0, h = 0;
};

// Rectangle in page points, origin at the top-left of the displayed page.
struct RectF {
    float left = 0, top = 0, right = 0, bottom = 0;
};
