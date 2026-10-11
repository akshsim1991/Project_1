// Dialogs.h - the settings and history dialogs (templates in res/app.rc).
#pragma once
#include "Common.h"
#include "Document.h"
#include "Engine.h"
#include "History.h"

// Units and precision. `forNew` (in/out): also use them for new drawings.
bool UnitsDialog(HWND owner, DrawingSettings& s, bool& forNew);
// Grid, snap, ortho and object snaps; `units` is shown next to spacings.
bool DraftingDialog(HWND owner, Drafting& d);
// Auto-save interval and versions kept.
bool OptionsDialog(HWND owner, int& autosaveMinutes, int& keepVersions, const std::wstring& folder);
// Lists the versions; returns the index to restore, or -1.
int HistoryDialog(HWND owner, const std::wstring& drawingName, const std::vector<VersionInfo>& versions);
// Lists drawings with auto-saved copies; returns the index to open, or -1.
int RecoverDialog(HWND owner, const std::vector<HistoryDrawing>& drawings, bool afterCrash);
void AboutDialog(HWND owner);
