// Settings.h - preferences (HKCU\Software\CurveForge).
#pragma once
#include "Common.h"

struct Settings {
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;

    int themeMode = 0;        // 0 system, 1 light, 2 dark
    int digits = 6;           // significant digits in results
    int confidence = 95;      // 90, 95 or 99 percent
    bool autoFit = true;      // refit after every change
    bool confidenceBand = true;
    bool predictionBand = false;
    bool residuals = true;
    bool gridLines = true;
    int tableWidthDip = 380;
    int resultsHeightDip = 240;
    std::wstring lastFolder;
    std::vector<std::wstring> recent;  // newest first

    void Load();
    void Save() const;
    void AddRecent(const std::wstring& path);
};
