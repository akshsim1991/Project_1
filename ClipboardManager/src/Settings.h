// Settings.h - preferences (HKCU\Software\ClipboardManager).
#pragma once
#include "Common.h"

// The global shortcut that opens the history.
enum HotkeyChoice : int { kHotkeyCtrlShiftV, kHotkeyCtrlAltV, kHotkeyWinShiftV, kHotkeyNone, kHotkeyCount };

struct Settings {
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;

    int themeMode = 0;          // 0 system, 1 light, 2 dark
    int hotkey = kHotkeyCtrlShiftV;
    bool pasteOnEnter = true;   // Enter pastes into the app you were in
    bool recordImages = true;
    bool recordFiles = true;
    bool startInTray = true;    // when started with Windows
    bool closeToTray = true;
    int deleteAfterDays = 0;    // 0 = keep everything forever (unpinned items only)
    int filter = kFilterAll;
    int listWidthDip = 430;

    bool askedAutostart = false;
    bool toldAboutTray = false;

    void Load();
    void Save() const;
};

const wchar_t* HotkeyName(int choice);
bool HotkeyKeys(int choice, UINT& modifiers, UINT& vk);

bool AutostartEnabled();
bool SetAutostart(bool on);
