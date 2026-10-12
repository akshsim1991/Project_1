// Sheet.h - drawing sheets (paper space): a sheet is a page of a given
// paper size with its own objects (border, title block, notes) and
// viewports showing the drawing at a scale. Each sheet's objects are kept
// in a Document of their own, in millimetres, so every command works on a
// sheet as it does in the drawing.
//
// Plain C++ (no Windows).
#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "Annot.h"
#include "Document.h"

struct PaperSize {
    const wchar_t* name;
    double width, height;  // millimetres, portrait
};
const std::vector<PaperSize>& PaperSizes();  // A4..A0, Letter, Legal, ANSI B..E
const PaperSize* FindPaper(const std::wstring& name);

struct SheetSetup {
    std::wstring name = L"Sheet1";
    std::wstring paper = L"A3";  // a name from PaperSizes(), or "Custom"
    bool landscape = true;
    double width = 420, height = 297;  // the page as laid out (millimetres)
    double margin = 10;                // unprinted edge, and where the border goes
    // Printing: 0 colour, 1 black (monochrome), 2 shades of grey.
    int colourMode = 0;
    bool lineWeights = true;  // print line weights (off: all thin)
    // Pens: line weights for the nine standard colours (index 1-9; -1: the object's own).
    std::array<int, 10> pens{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};
// Sets width and height from the paper name and orientation.
void ApplyPaper(SheetSetup& s);

struct Sheet {
    Sheet();  // an empty sheet in millimetres
    SheetSetup setup;
    Document doc;
};
using SheetList = std::vector<std::unique_ptr<Sheet>>;

// Millimetres in one drawing unit (unitless counts as millimetres).
double MmPerUnit(Units u);
// A viewport's scale as written on drawings: "1:2", "5:1".
std::wstring ScaleText(double ratio);
// Ratio of a viewport (paper size : real size).
double ViewportRatio(const Entity& vp, const Document& model);
// The usual scales, largest first: 10:1, 5:1, 2:1, 1:1, 1:2 ... 1:1000.
const std::vector<double>& StandardScales();
// The largest usual scale at which `extent` (drawing units) fits in `w` x `h` millimetres.
double FitScale(const Box& extent, double w, double h, Units units);

// The drawing's properties shown in title blocks: {key} fields.
struct PropertyKey {
    const wchar_t* key;
    const wchar_t* label;
};
const std::vector<PropertyKey>& DrawingPropertyKeys();  // title, number, drawnby, checkedby, company...
// Field values for a sheet: the drawing's properties, the sheet's name and
// number, the scale of its first viewport, and `base` (date, file name...).
FieldValues SheetFields(const Document& model, const SheetList& sheets, size_t index, const FieldValues& base);

// A new sheet: a border and title block, and one viewport showing the
// whole drawing at a usual scale.
std::unique_ptr<Sheet> NewSheet(const Document& model, const std::wstring& name, const std::wstring& paper, bool landscape);
// Border and title block for the sheet's paper (replacing the ones it has).
void AddBorder(Sheet& sheet);
// The area inside the border above the title block (for viewports).
Box DrawingArea(const SheetSetup& s);

// The drawing with its sheets as one file, and back (sheets are written
// after the drawing, so earlier versions just read the drawing).
std::string DrawingToText(const Document& model, const SheetList& sheets);
bool DrawingFromText(const std::string& text, Document& model, SheetList& sheets, std::wstring& error);
// A DXF drawing: its paper space layouts become sheets.
bool DrawingFromDxf(const std::string& text, Document& model, SheetList& sheets, std::wstring& error, std::wstring& notes);
// A DXF of the drawing, with the first sheet as paper space (DXF R12 has one).
std::string DrawingToDxf(const Document& model, const SheetList& sheets);
