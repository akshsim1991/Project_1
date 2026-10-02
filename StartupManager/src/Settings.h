// Settings.h - persisted preferences (HKCU\Software\StartupManager).
#pragma once
#include "Common.h"

enum Column : int {
    kColName,
    kColStatus,
    kColPublisher,
    kColType,
    kColScope,
    kColCommand,
    kColLocation,
    kColWarnings,
    kColProgram,
    kColDescription,
    kColTrigger,
    kColumnCount
};

enum Filter : int {
    kFilterAll,
    kFilterEnabled,
    kFilterDisabled,
    kFilterAttention,   // has warnings
    kFilterThirdParty,  // not from Microsoft
    kFilterRegistry,
    kFilterStartupFolder,
    kFilterTasks,
    kFilterCount
};

struct Settings {
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;

    int themeMode = 0;              // 0 system, 1 light, 2 dark
    int refreshSeconds = 10;        // 0 = off
    bool confirmDisable = false;    // ask before disabling (deleting always asks)
    bool showMicrosoftTasks = false;// Windows' own scheduled tasks (there are many)
    bool showRunOnce = true;
    bool alwaysElevate = false;

    int filter = kFilterAll;
    int sortColumn = kColName;
    bool sortDescending = false;
    int columnWidth[kColumnCount] = {};
    int columnOrder[kColumnCount] = {};
    unsigned visibleColumns = 0;

    Settings();
    void Load();
    void Save() const;
};

extern const int kRefreshChoices[];
extern const int kRefreshChoiceCount;
