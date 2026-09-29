// Theme.h - light/dark colours following the Windows app theme setting.
#pragma once
#include "Common.h"

struct Theme {
    bool dark = false;
    COLORREF barBg, barText, barTextDisabled, barHover, barPressed, barBorder, accent;
    COLORREF editBg, editText, editBorder;
    COLORREF canvasBg, canvasText, pageBorder;
    HBRUSH editBrush = nullptr;
};

// Reads the "AppsUseLightTheme" setting and rebuilds the palette.
void ReloadTheme();
const Theme& CurrentTheme();

// Dark title bar / scrollbars / menus for a window (no-op in light mode or
// on Windows builds that do not support it).
void ApplyWindowTheme(HWND topLevel);
void ApplyScrollbarTheme(HWND hwnd);
