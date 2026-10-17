// Common.h - shared Windows includes and command ids.
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

#define APP_NAME L"CurveForge"
#define APP_VERSION L"1.0.0"
#define APP_COPYRIGHT L"\x00A9 2026 Akshaya Simha"
#define APP_WINDOW_CLASS L"CurveForgeMain"
#define APP_REG_KEY L"Software\\CurveForge"
#define APP_FILE_EXT L"cforge"

// Command ids (toolbar, menus, accelerators).
enum : int {
    // file
    ID_NEW = 100,
    ID_OPEN,
    ID_SAVE,
    ID_SAVE_AS,
    ID_SAVE_TABLE_CSV,
    // model and fitting
    ID_MODEL_MENU,
    ID_ORDER_MENU,
    ID_FIT,
    ID_BEST_FIT,
    ID_PARAMS,
    ID_PREDICT,
    ID_EQUATION_EDIT,
    ID_X_MENU,
    ID_Y_MENU,
    ID_SIGMA_MENU,
    ID_ROBUST,
    // view
    ID_CONF_BAND,
    ID_PRED_BAND,
    ID_RESIDUALS,
    ID_LOG_X,
    ID_LOG_Y,
    ID_GRID_LINES,
    ID_RESET_VIEW,
    // export
    ID_EXPORT_MENU,
    ID_EXPORT_CSV,
    ID_EXPORT_PNG,
    ID_EXPORT_SVG,
    ID_COPY_CHART,
    ID_COPY_RESULTS,
    ID_COPY_EQUATION,
    ID_COPY_EXCEL,
    ID_REPORT,
    // data
    ID_UNDO,
    ID_REDO,
    ID_COPY,
    ID_CUT,
    ID_PASTE,
    ID_PASTE_NEW,
    ID_SELECT_ALL,
    ID_INSERT_ROW,
    ID_DELETE_ROWS,
    ID_EXCLUDE_ROWS,
    ID_INCLUDE_ALL,
    ID_ADD_COLUMN,
    ID_FORMULA_COLUMN,
    ID_RENAME_COLUMN,
    ID_DELETE_COLUMN,
    ID_SORT_ASC,
    ID_SORT_DESC,
    ID_SET_X,
    ID_SET_Y,
    ID_SET_SIGMA,
    ID_CLEAR_SIGMA,
    // tools and app
    ID_TOOLS_MENU,
    ID_BATCH,
    ID_MORE_MENU,
    ID_SETTINGS,
    ID_SHORTCUTS,
    ID_ABOUT,
    ID_EXIT,
    ID_FOCUS_TABLE,
    ID_FOCUS_EQUATION,
    ID_STATUS_TEXT,
    ID_STATUS_HOVER,
    ID_MODEL_FIRST = 300,     // + ModelKind
    ID_ORDER_FIRST = 340,     // + order
    ID_EXAMPLE_FIRST = 360,   // + example index
    ID_COL_X_FIRST = 400,     // + column
    ID_COL_Y_FIRST = 600,     // + column
    ID_COL_SIGMA_NONE = 799,
    ID_COL_SIGMA_FIRST = 800, // + column
    ID_RECENT_FIRST = 1000,   // + index
};
