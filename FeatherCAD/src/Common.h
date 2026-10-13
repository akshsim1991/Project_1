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
#define APP_VERSION L"2.3.0"
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
    WM_APP_COMMAND_TYPING,             // command line: the text being typed changed (lParam = std::wstring*)
    WM_APP_CANVAS_EDIT,                // canvas: an object was double-clicked (lParam = const uint64_t* id)
    WM_APP_PROPERTIES,                 // fill the Properties panel again (after a change made in it)
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
    ID_MIRROR,
    ID_SCALE,
    ID_STRETCH,
    ID_TRIM,
    ID_EXTEND,
    ID_OFFSET,
    ID_FILLET,
    ID_CHAMFER,
    ID_LENGTHEN,
    ID_BREAK,
    ID_BREAK_AT_POINT,
    ID_JOIN,
    ID_EXPLODE,
    ID_ARRAY_RECT,
    ID_ARRAY_POLAR,
    ID_ARRAY_PATH,
    ID_ARRAY_EDIT,
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
    ID_TOGGLE_POLAR,
    ID_TOGGLE_OTRACK,
    ID_TOGGLE_DYN,
    ID_QSELECT,
    ID_SELECT_SIMILAR,
    ID_POLYGON,
    ID_ELLIPSE,
    ID_ELLIPSE_CENTRE,
    ID_ELLIPSE_ARC,
    ID_SPLINE,
    ID_POINT,
    ID_OSNAP_SETTINGS,
    ID_DRAFTING_SETTINGS,
    ID_UNITS,
    ID_AUTOSAVE_SETTINGS,
    // Version 1.3: drawings in tabs, DXF import, dimensions, clipboard
    ID_IMPORT_DXF,
    ID_CLOSE_DRAWING,
    ID_NEXT_DRAWING,
    ID_PREV_DRAWING,
    ID_COPYCLIP,
    ID_CUTCLIP,
    ID_PASTECLIP,
    ID_DIM_SMART,
    ID_DIM_LINEAR,
    ID_DIM_ALIGNED,
    ID_DIM_RADIUS,
    ID_DIM_DIAMETER,
    ID_DIM_ANGULAR,
    ID_DIM_CONTINUE,
    ID_DIM_BASELINE,
    ID_DIM_EDIT,
    ID_DIM_STYLE,
    // Version 1.3.1
    ID_TANLINE,
    ID_CIRCLE_TTR,
    ID_LENGTHEN_DYNAMIC,
    // Version 1.4
    ID_TOGGLE_LWT,
    ID_LAYERS,
    ID_PROPERTIES,
    ID_MATCHPROP,
    ID_CONSTRUCTION,
    ID_ORIGIN,
    ID_LTSCALE,
    ID_LAYER_COMBO,
    // Version 1.5: annotation and blocks
    ID_TEXT,
    ID_TEXTEDIT,
    ID_LEADER,
    ID_BALLOON,
    ID_CENTERMARK,
    ID_CENTERLINE,
    ID_HATCH,
    ID_FILL,
    ID_BLOCK,
    ID_INSERT,
    ID_ATTDEF,
    ID_ATTEDIT,
    ID_PURGE,
    ID_REVCLOUD,
    ID_AREA,
    ID_DIM_ORDINATE,
    ID_DIM_ANGCOORD,
    ID_DIM_SPACE,
    ID_DIM_DRIVEN,
    // Version 1.6: sheets and printing
    ID_SHEET_NEW,
    ID_PAGESETUP,
    ID_TITLEBLOCK,
    ID_DWGPROPS,
    ID_MVIEW,
    ID_VPSCALE,
    ID_VPPAN,
    ID_VPLOCK,
    ID_PLOT,
    ID_EXPORT_PDF,
    ID_SHEET_RENAME,
    ID_SHEET_DELETE,
    // Version 1.7: relationships, variables, engineering symbols
    ID_REL_HORIZONTAL,
    ID_REL_VERTICAL,
    ID_REL_PARALLEL,
    ID_REL_PERPENDICULAR,
    ID_REL_COLLINEAR,
    ID_REL_TANGENT,
    ID_REL_CONCENTRIC,
    ID_REL_EQUAL,
    ID_REL_COINCIDENT,
    ID_REL_MIDPOINT,
    ID_REL_SYMMETRIC,
    ID_REL_FIX,
    ID_REL_MAINTAIN,
    ID_REL_AUTO,
    ID_REL_SHOW,
    ID_DIMLOCK,
    ID_VAR,
    ID_VARIABLES,
    ID_GOALSEEK,
    ID_FCF,
    ID_DATUM,
    ID_SURFACE,
    ID_WELD,
    ID_PARTSLIST,
    // Version 1.8
    ID_SKETCH,
    ID_BPARAM,
    ID_TABLE,
    ID_PARTBALLOON,
    ID_IMAGE,
    ID_PROJECTOR,
    ID_LABEL,
    ID_HIDDEN,
    ID_TOOLBAR_MORE,
    // Help
    ID_COMMAND_LIST,
    ID_ABOUT,
    // Menu strip buttons (open a drop-down menu)
    ID_MENU_FILE,
    ID_MENU_EDIT,
    ID_MENU_VIEW,
    ID_MENU_DRAW,
    ID_MENU_MODIFY,
    ID_MENU_DIMENSION,
    ID_MENU_RELATE,
    ID_MENU_ANNOTATE,
    ID_MENU_FORMAT,
    ID_MENU_TOOLS,
    ID_MENU_GRAPHICS,
    ID_MENU_HELP,
    // Context menu
    ID_CTX_ENTER,
    ID_CTX_CANCEL,
    ID_CTX_REPEAT,
    ID_ESCAPE,
    ID_TOGGLE_HISTORY,  // F2: more command history lines
    ID_TIPS,
    ID_TOGGLE_ISO,       // isometric drawing on/off (status bar)
    ID_ISOPLANE_NEXT,    // F5: the next isometric plane
    ID_RECENT_FIRST = 300,   // + index into the recent files (up to 10)
    ID_CTX_KEYWORD = 330,    // + index into the prompt's options
    ID_CTX_RECENT_CMD = 360, // + index into the recent commands
    ID_OSNAP_FIRST = 380,    // + bit index of a snap mode (one-shot snap)
    ID_SNAPMODE_FIRST = 388, // + bit index of a snap mode: turn it on or off (status bar E, M, I, C)
    ID_PALETTE_CMD = 400,    // + index into AllCommands()
    ID_TAB_FIRST = 600,      // + index of a drawing tab
    ID_TAB_CLOSE_FIRST = 700,  // + index of a drawing tab: its close button
    ID_SHEET_TAB_FIRST = 800,  // + 0: the drawing, + 1 + i: sheet i
    ID_SHEET_MENU_FIRST = 900, // + 1 + i: right click on sheet i's tab
    ID_SWATCH_FIRST = 1000,    // + index of a colour swatch in the guide bar
    ID_SWATCH_MORE = 1030,     // the guide bar's "More..." colours
    ID_WEIGHT_MENU = 1031,     // the guide bar's thickness button
    kMaxTabs = 100,
};
