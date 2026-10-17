// Dialogs.h - Settings, Add entry, Restore deleted entries, and Details.
#pragma once
#include "Entries.h"
#include "Settings.h"

bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& resetColumns);

struct AddRequest {
    std::wstring name, program, args;
    bool allUsers = false, shortcut = false;
};
bool ShowAddDialog(HINSTANCE inst, HWND owner, bool elevated, AddRequest& out);

// Returns the backup folders the user ticked (empty when cancelled).
std::vector<std::wstring> ShowRestoreDialog(HINSTANCE inst, HWND owner);

// One entry: a full report with buttons. Several: a combined report.
void ShowDetailsDialog(HINSTANCE inst, HWND owner, const std::vector<StartupEntry>& entries);

// Actions shared by the details window and the main window's menus.
void OpenFileLocation(HWND owner, const StartupEntry& e);
void ShowWhereSet(HWND owner, const StartupEntry& e);  // Registry Editor, Explorer or Task Scheduler
void CheckOnVirusTotal(HWND owner, const StartupEntry& e);
void SearchEntryOnline(HWND owner, const StartupEntry& e);
