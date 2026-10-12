// Dialogs.h - the settings and history dialogs (templates in res/app.rc).
#pragma once
#include <functional>

#include "Common.h"
#include "Document.h"
#include "Engine.h"
#include "History.h"
#include "Plot.h"
#include "Select.h"

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

// Quick select: how the matching objects change the selection.
enum class QSelectMode { Replace, Add, Remove };
bool QSelectDialog(HWND owner, SelectQuery& query, QSelectMode& mode);

// Dimension style. `applyExisting`: also change the dimensions already drawn.
bool DimStyleDialog(HWND owner, DimStyle& style, bool& applyExisting);
// One dimension: its value (when attached, the geometry changes to it),
// its text and its decimal places. `value` is in degrees for angles.
struct DimEditResult {
    bool valueChanged = false;
    double value = 0;
    std::wstring text;
    int precision = 2;
};
bool DimEditDialog(HWND owner, const DimData& d, bool attached, DimEditResult& out);

// The layer manager. Changes go straight into the drawing (each one can be
// undone); `changed` is called after each so the drawing is redrawn.
void LayerDialog(HWND owner, Document& doc, const std::function<void()>& changed);
// The text editor: content (several lines), symbols, fields, font, size, angle, justification, frame.
bool TextEditDialog(HWND owner, TextData& text, const std::wstring& title);

// A sheet's paper, margins and printing. `redoBorder`: the user wants the
// border and title block made again (for a new paper).
bool PageSetupDialog(HWND owner, SheetSetup& setup, bool& redoBorder);
// The drawing's title block fields (DrawingPropertyKeys), in that order.
bool DrawingPropsDialog(HWND owner, std::vector<std::wstring>& values);

// What to print, and how.
struct PlotRequest {
    enum What { ThisSheet, AllSheets, DrawingView, DrawingAll } what = DrawingAll;
    std::wstring paper = L"A3";  // for the drawing (sheets have their own)
    bool landscape = true;
    double ratio = 0;            // for the drawing: paper size : real size (0: fit)
    int colourMode = 0;
    bool lineWeights = true;
    bool fit = false;            // to the printer's paper
};
// The print dialog with a preview of the pages `build` makes. Returns 0
// (closed), 1 (print) or 2 (save as PDF).
int PlotDialog(HWND owner, PlotRequest& request, bool hasSheets, bool onSheet,
               const std::function<std::vector<PlotPage>(const PlotRequest&)>& build);
// A colour chosen from a menu at the cursor (named colours, or any colour
// with the Windows colour picker). `allowBy`: ByLayer and ByBlock too.
// False if nothing was chosen.
bool PickColor(HWND owner, int32_t& color, bool allowBy);
