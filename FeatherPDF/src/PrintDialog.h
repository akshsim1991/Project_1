// PrintDialog.h - Feather PDF's own Print window: printer, pages, copies,
// layout (pages per sheet, booklet), size and orientation, with a preview
// of every sheet exactly as it will be printed (PrintLayout).
//
// It replaces the Windows print dialog, which on Windows 11 shows an
// empty preview for programs like this one.
#pragma once
#include "PrintLayout.h"
#include "ToolDialogs.h"

class RenderWorker;

// What the Print window remembers between uses.
struct PrintSettings {
    std::wstring printer;          // empty: the Windows default printer
    std::vector<uint8_t> devMode;  // that printer's settings (a DEVMODEW), as set in Properties
    PageChoice pages;
    PrintOptions options;          // layout, size, orientation, copies, collate (its pages are set each time)
};

// Shows the Print window for a document (page sizes as shown, in points).
// On Print: a device context for the chosen printer (StartDoc not called;
// the caller owns it) and the sheets to print, copies included.
bool ShowPrintDialog(HINSTANCE inst, HWND owner, RenderWorker* worker, uint32_t docId,
                     const std::vector<SizeF>& pageSizes, PrintSettings& settings, HDC& dc,
                     std::vector<PrintSheet>& sheets);

// A page rendered for the preview (WM_APP_PREVIEW_READY): handed to the
// open Print window, or freed if there is none. Takes ownership.
void OnPrintPreviewPage(TileResult* page);
