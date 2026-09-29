// Theme.cpp - light/dark palette and window theming.
#include "Theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {
Theme g_theme;

bool SystemUsesDarkApps() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS) {
        return false;
    }
    return value == 0;
}

// Popup menus only follow the dark theme after opting in through an
// undocumented (but widely used and stable since Windows 10 1903) uxtheme
// export. If it is missing, menus simply stay light.
void AllowDarkMenus(bool dark) {
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    static auto setMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    static auto flush = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    OSVERSIONINFOW vi{};  // ordinal 135 changed meaning in build 18362
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto rtl = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (!rtl || rtl(&vi) != 0 || vi.dwBuildNumber < 18362) return;
    if (setMode) setMode(dark ? 2 /*ForceDark*/ : 3 /*ForceLight*/);
    if (flush) flush();
}
}  // namespace

void ReloadTheme(ThemeMode mode) {
    Theme& t = g_theme;
    t.dark = mode == ThemeMode::Dark || (mode == ThemeMode::System && SystemUsesDarkApps());
    if (t.dark) {
        t.barBg = RGB(32, 32, 32);
        t.barText = RGB(235, 235, 235);
        t.barTextDisabled = RGB(110, 110, 110);
        t.barHover = RGB(55, 55, 55);
        t.barPressed = RGB(70, 70, 70);
        t.barBorder = RGB(20, 20, 20);
        t.accent = RGB(96, 205, 255);
        t.editBg = RGB(45, 45, 45);
        t.editText = RGB(240, 240, 240);
        t.editBorder = RGB(80, 80, 80);
        t.canvasBg = RGB(43, 43, 43);
        t.canvasText = RGB(170, 170, 170);
        t.pageBorder = RGB(20, 20, 20);
        t.tabStripBg = RGB(22, 22, 22);
        t.tabHover = RGB(42, 42, 42);
    } else {
        t.barBg = RGB(249, 249, 249);
        t.barText = RGB(28, 28, 28);
        t.barTextDisabled = RGB(165, 165, 165);
        t.barHover = RGB(232, 232, 232);
        t.barPressed = RGB(218, 218, 218);
        t.barBorder = RGB(222, 222, 222);
        t.accent = RGB(0, 95, 184);
        t.editBg = RGB(255, 255, 255);
        t.editText = RGB(20, 20, 20);
        t.editBorder = RGB(200, 200, 200);
        t.canvasBg = RGB(232, 232, 232);
        t.canvasText = RGB(100, 100, 100);
        t.pageBorder = RGB(190, 190, 190);
        t.tabStripBg = RGB(232, 232, 232);
        t.tabHover = RGB(240, 240, 240);
    }
    if (t.editBrush) DeleteObject(t.editBrush);
    t.editBrush = CreateSolidBrush(t.editBg);
    AllowDarkMenus(t.dark);
}

const Theme& CurrentTheme() { return g_theme; }

void ApplyWindowTheme(HWND topLevel) {
    BOOL dark = g_theme.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(topLevel, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void ApplyScrollbarTheme(HWND hwnd) {
    SetWindowTheme(hwnd, g_theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}
