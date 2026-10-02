// Theme.h - light/dark colours: follow Windows, or forced by the user.
#pragma once
#include "Common.h"

struct Theme {
    bool dark = false;
    COLORREF barBg, barText, barTextDisabled, barHover, barPressed, barBorder, accent;
    COLORREF editBg, editText, editBorder;
    COLORREF canvasBg, canvasText, pageBorder;
    COLORREF tabStripBg, tabHover;
    // service list
    COLORREF listBg, listText, listDim;
    COLORREF running, stopped, pending, paused, danger, thirdParty;
    HBRUSH editBrush = nullptr;
};

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

// Rebuilds the palette. In System mode the Windows "AppsUseLightTheme"
// setting decides; Light/Dark force a theme regardless of Windows.
void ReloadTheme(ThemeMode mode);
const Theme& CurrentTheme();

// Dark title bar / scrollbars / menus for a window (no-op in light mode or
// on Windows builds that do not support it).
void ApplyWindowTheme(HWND topLevel);
void ApplyScrollbarTheme(HWND hwnd);
