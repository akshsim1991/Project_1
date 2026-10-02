// Dialogs.h - the Settings window, and alert sounds.
#pragma once
#include "Settings.h"

// `autostart` is the current start-with-Windows state in and the choice out.
bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& autostart);

// Plays the alert sound chosen in Settings (or the given mode, for "Test").
void PlayAlertSound(int mode, const std::wstring& file, bool urgent);
