// Dialogs.h - the Settings dialog and the "review changes" preview.
#pragma once
#include "Services.h"
#include "Settings.h"

// Shows Settings for `s`. Returns true on OK (then `s` holds the new
// values); `resetColumns` is set if the user asked for the default columns.
bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& resetColumns);

// One start-type change in a profile or snapshot restore.
struct PlannedChange {
    std::wstring name, displayName;
    StartMode from = StartMode::Manual, to = StartMode::Manual;
    bool critical = false;
    bool blocked = false;  // critical and protection is on: shown, not applied
    bool apply = true;     // ticked in the preview
};

// Shows the list of changes with a tick box each. Returns true when the
// user clicks Apply (unticked changes have apply == false).
bool ShowChangesDialog(HINSTANCE inst, HWND owner, const std::wstring& title, const std::wstring& intro,
                       std::vector<PlannedChange>& changes);
