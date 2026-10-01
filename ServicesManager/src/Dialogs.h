// Dialogs.h - the Settings dialog.
#pragma once
#include "Settings.h"

// Shows Settings for `s`. Returns true on OK (then `s` holds the new
// values); `resetColumns` is set if the user asked for the default columns.
bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& resetColumns);
