// Print.h - plotted pages (Plot.h) drawn with GDI: on a printer, and in
// the print preview.
#pragma once
#include <vector>

#include "Common.h"
#include "Plot.h"

// Draws `page` on `dc`, `pxPerMm` device pixels to the millimetre, the
// page's top left corner at device point (x, y).
void DrawPlotPage(HDC dc, const PlotPage& page, double pxPerMm, double x, double y);

// Asks for a printer (Windows print dialog, set to the first page's size
// and orientation) and prints the pages. `fit`: each page made as large as
// the printer's paper allows; otherwise printed at its real size. False if
// cancelled or failed (`error` says why when it failed).
bool PrintPages(HWND owner, const std::vector<PlotPage>& pages, bool fit, const std::wstring& title, std::wstring& error);
