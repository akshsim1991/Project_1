// Settings.h - persisted user preferences (HKCU\Software\FeatherPDF).
//
// The registry is used instead of an INI file next to the executable so
// that the app works when installed to Program Files. Nothing is written
// until the application exits.
#pragma once
#include "Common.h"

struct Settings {
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;

    int zoomMode = 1;      // 0 = custom, 1 = fit width, 2 = fit page
    double zoom = 1.0;     // used when zoomMode == 0
    int viewMode = 1;      // ViewMode: 0 single page, 1 continuous, 2 two pages
    bool coverPage = true; // two-page layout shows the first page alone
    int pageColors = 0;    // PageColors: 0 normal, 1 dark, 2 dimmed
    int sidebarMode = 0;   // SidebarMode: 0 hidden, 1 bookmarks, 2 thumbnails
    int sidebarWidth = 240;  // in 96-DPI pixels
    bool matchCase = false;
    int highlightColor = 0;  // index into kHighlightColors
    COLORREF drawColor = RGB(220, 30, 30);  // shapes, pen and new text
    int lineWidthTenths = 20;               // line width, in tenths of a point
    int textSize = 12;                      // new text, in points
    std::vector<std::wstring> recent;       // recently opened files, newest first

    int themeMode = 0;     // ThemeMode: 0 = system, 1 = light, 2 = dark

    // Tabs open at exit, reopened when the app starts without a file.
    struct OpenFile {
        std::wstring path;
        int page = 0;
    };
    std::vector<OpenFile> session;
    int activeTab = 0;

    void Load();
    void Save() const;
};
