// Settings.h - user preferences, kept in HKCU\Software\FeatherCAD.
#pragma once
#include "Common.h"
#include "Engine.h"

struct Settings {
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;
    int themeMode = 0;                  // ThemeMode: 0 system, 1 light, 2 dark
    std::vector<std::wstring> recent;   // recently opened drawings, newest first
    Drafting drafting;                  // grid, snap, ortho, object snaps
    int newUnits = 1;                   // Units of new drawings (1 = millimetres)
    int newPrecision = 2;
    int autosaveMinutes = 5;            // 0 = off
    int keepVersions = 30;              // versions kept per drawing

    void Load();
    void Save() const;
    void AddRecent(const std::wstring& path);
};

// Set while Feather CAD runs; still set at the next start after a crash.
bool PreviousRunCrashed();
void MarkRunning(bool running);
