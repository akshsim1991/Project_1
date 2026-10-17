// PrintLayout.h - where each page goes on the printed sheets: one page per
// sheet (fitted, actual size or only large pages shrunk), several pages
// per sheet, or a booklet in folding order. The print preview and the
// printing itself use the same layout, so the preview shows exactly what
// is printed.
//
// Plain C++ (no PDFium, no GDI).
#pragma once
#include "RenderTypes.h"

struct PrintOptions {
    std::vector<int> pages;  // ascending page indices
    int perSheet = 1;        // 1, 2, 4, 6 or 9
    bool booklet = false;    // two pages per side, in folding order
    int scale = 0;           // one page per sheet: 0 fit to the paper, 1 actual size, 2 shrink large pages only
    int orientation = 0;     // 0 automatic (as the pages), 1 portrait, 2 landscape
    int copies = 1;
    bool collate = true;     // copies: whole sets (1 2 3, 1 2 3) rather than 1 1 2 2 3 3
};

// The paper as the printer uses it, in points: its size and the part the
// printer can print on (from the paper's top left corner).
struct PaperInfo {
    float w = 595.28f, h = 841.89f;               // A4
    RectF printable{14.2f, 14.2f, 581.1f, 827.7f};  // 5 mm margins
};

// Whether the paper should be turned landscape for these pages.
bool PrintLandscape(const PrintOptions& o, const std::vector<SizeF>& pageSizes);

// The sheets to print. `withCopies`: repeated for the copies (the preview
// shows one set).
std::vector<PrintSheet> LayoutSheets(const PrintOptions& o, const std::vector<SizeF>& pageSizes,
                                     const PaperInfo& paper, bool withCopies);
