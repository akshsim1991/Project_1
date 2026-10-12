// Plot.h - printing: a sheet (or a part of the drawing) as what goes on
// paper - lines with their widths, filled shapes and text, in millimetres
// from the bottom left corner of the page, with line types already cut
// into dashes. The printer, the print preview and the PDF writer all draw
// from this.
//
// Plain C++ (no Windows).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Sheet.h"

struct PlotItem {
    enum Kind : uint8_t { Path, Fill, Text, Image } kind = Path;
    uint32_t rgb = 0;
    double width = 0;                      // Path: millimetres (0: as thin as the printer draws)
    std::vector<Vec2> pts;                 // Path
    bool closed = false;
    std::vector<std::vector<Vec2>> loops;  // Fill: inside where an odd number enclose
    TextItem text;                         // Text: position and height in millimetres
    std::wstring image;                    // Image: the picture's file (pts: bottom left, top right)
    int clip = -1;                         // index into PlotPage::clips (a viewport), -1: none
};

struct PlotPage {
    std::wstring name;
    double width = 0, height = 0;  // millimetres
    std::vector<Box> clips;
    std::vector<PlotItem> items;
};

struct PlotStyle {
    int colourMode = 0;  // 0 colour, 1 black, 2 grey
    bool lineWeights = true;
    std::array<int, 10> pens{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};  // colours 1-9 -> line weight
};
PlotStyle StyleOf(const SheetSetup& s);

// A sheet: what its viewports show of the drawing, then the sheet's own
// objects. `fields` fills in {title}, {scale}... (SheetFields).
// `style`: instead of the sheet's own.
void PlotSheet(const Sheet& sheet, const Document& model, const FieldValues& fields, PlotPage& out,
               const PlotStyle* style = nullptr);
// A part of the drawing on a page of paperW x paperH millimetres: `scale`
// millimetres per drawing unit (0: as large as fits inside the margins),
// centred on the page.
void PlotDrawing(const Document& model, const Box& area, double paperW, double paperH, double margin, double scale,
                 const PlotStyle& style, const FieldValues& fields, PlotPage& out);

// Reads a picture file as 8-bit RGB, top row first (the program supplies
// this: Windows reads PNG, JPEG, BMP...). Unset: pictures print as frames.
using ImageReader = std::function<bool(const std::wstring& path, int& width, int& height, std::vector<uint8_t>& rgb)>;
void SetImageReader(ImageReader reader);
const ImageReader& GetImageReader();

// The pages as a PDF document (lines, fills and text stay sharp at any zoom).
std::string WritePdf(const std::vector<PlotPage>& pages, const std::wstring& title);
