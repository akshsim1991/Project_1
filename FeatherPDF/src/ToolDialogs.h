// ToolDialogs.h - the option dialogs of Recognise text (OCR), Watermark,
// Page numbers and Export. Each returns true when the user confirmed; the
// options keep their values for the next time the dialog opens.
#pragma once
#include "RenderTypes.h"

// "All pages / Current page / Pages: 1-3, 5", shared by every dialog.
struct PageChoice {
    enum Mode { All, Current, Range } mode = All;
    std::wstring range;
    int pageCount = 0;   // in
    int current = 0;     // in: the page shown
    std::vector<int> pages;  // out: ascending, no duplicates
};

struct OcrOptions {
    PageChoice pages;
    bool skipText = true;
};

struct WatermarkOptions {
    PageChoice pages;
    std::wstring text = L"CONFIDENTIAL";
    int color = 0;       // index into kWatermarkColors
    int opacity = 2;     // index into kWatermarkOpacity
    int size = 0;        // index into kWatermarkSizes (0: fit the page)
    bool diagonal = true;
    bool behind = false;
};

struct PageNumberOptions {
    PageChoice pages;
    int format = 2;      // index into kNumberFormats
    int position = kNumBottomCenter;
    int start = 1;
    int size = 2;        // index into kNumberSizes
};

struct ExportOptions {
    PageChoice pages;
    ExportFormat format = ExportFormat::Png;
    int dpi = 1;         // index into kExportDpi
};

extern const COLORREF kWatermarkColors[5];
extern const int kWatermarkOpacity[6];
extern const float kWatermarkSizes[6];
extern const wchar_t* const kNumberFormats[5];  // patterns: "{n}", "Page {n} of {total}"...
extern const float kNumberSizes[6];
extern const int kExportDpi[3];

bool ShowOcrDialog(HINSTANCE inst, HWND owner, OcrOptions& o);
bool ShowWatermarkDialog(HINSTANCE inst, HWND owner, WatermarkOptions& o);
bool ShowPageNumberDialog(HINSTANCE inst, HWND owner, PageNumberOptions& o);
bool ShowExportDialog(HINSTANCE inst, HWND owner, ExportOptions& o);
