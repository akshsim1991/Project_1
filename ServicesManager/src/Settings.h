// Settings.h - persisted preferences (HKCU\Software\WindowsServicesManager).
#pragma once
#include "Common.h"

// Columns of the service list, in their default order.
enum Column : int {
    kColDisplayName,
    kColName,
    kColStatus,
    kColStartType,
    kColSafety,
    kColPid,
    kColAccount,
    kColDescription,
    kColPath,
    kColCompany,
    kColBootDelay,
    kColWarnings,
    kColumnCount
};

// Quick filters (ID_FILTER_FIRST + value).
enum Filter : int {
    kFilterAll,
    kFilterRunning,
    kFilterStopped,
    kFilterAutoStopped,  // Automatic but not running: often a failed service
    kFilterDisabled,
    kFilterThirdParty,
    kFilterCritical,
    kFilterAttention,    // has warnings
    kFilterBootDelay,    // slowed down Windows start-up
    kFilterCount
};

struct Settings {
    WINDOWPLACEMENT placement{};
    bool hasPlacement = false;

    int themeMode = 0;          // 0 system, 1 light, 2 dark
    int refreshSeconds = 5;     // 0 = off
    int timeoutSeconds = 30;    // per service and action
    bool confirmActions = true; // ask before stop/restart/pause/kill/start-type changes
    bool protectCritical = true;// block risky actions on critical services
    bool alwaysElevate = false; // restart as administrator on launch
    bool onlineAdvice = true;   // download "can it be disabled?" advice

    int filter = kFilterAll;
    int sortColumn = kColDisplayName;
    bool sortDescending = false;
    int columnWidth[kColumnCount] = {};  // in 96-DPI pixels (0 = default)
    int columnOrder[kColumnCount] = {};
    unsigned visibleColumns = 0;         // bit per column

    Settings();
    void Load();
    void Save() const;
};

extern const int kRefreshChoices[];
extern const int kRefreshChoiceCount;
