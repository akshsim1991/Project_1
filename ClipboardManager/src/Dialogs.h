// Dialogs.h - the Settings window.
#pragma once
#include "Settings.h"

// `autostart` is the current start-with-Windows state in and the choice out.
// `folder` is where the history is stored (shown, with a button to open it).
bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& autostart, const std::wstring& folder);
