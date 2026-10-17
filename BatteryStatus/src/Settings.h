// Settings.h - preferences (HKCU\Software\BatteryStatus).
#pragma once
#include "Common.h"

enum SoundMode : int { kSoundNone, kSoundNotification, kSoundAlarm, kSoundFile };

struct Settings {
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;

    int themeMode = 0;          // 0 system, 1 light, 2 dark
    int refreshSeconds = 5;
    int trayStyle = 0;          // 0 battery picture, 1 percentage number
    bool startInTray = true;    // when started with Windows
    bool closeToTray = true;    // the close button keeps it running
    int keepDays = 30;
    int graphHours = 24;        // 1, 6, 24 or 168

    bool lowAlert = true;
    int lowPercent = 20;
    bool criticalAlert = true;
    int criticalPercent = 10;
    bool fullAlert = true;
    int fullPercent = 99;
    bool plugAlert = false;     // charger connected / disconnected
    int repeatMinutes = 5;      // 0 = once
    bool messageBox = false;    // also a message box for low and critical
    int sound = kSoundNotification;
    std::wstring soundFile;

    bool askedAutostart = false;
    bool toldAboutTray = false;

    void Load();
    void Save() const;
};

// Start with Windows (HKCU Run key).
bool AutostartEnabled();
bool SetAutostart(bool on);
bool OldVersionAutostart();  // version 1.2's entry is present
