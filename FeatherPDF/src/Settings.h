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
    bool continuous = true;
    bool matchCase = false;

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
