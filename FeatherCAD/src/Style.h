// Style.h - how objects look: layers, colours, line types and line
// weights. An object's colour, line type and line weight are its own or
// "by layer" (the layer's); objects inside a block can also be "by block".
// Plain C++ (no Windows).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---- colours ---------------------------------------------------------------
// An object's colour: kByLayer, kByBlock, an AutoCAD colour index (1..255)
// or a true colour (kTrueColor | 0xRRGGBB). Index 7 is black on a light
// background and white on a dark one.
constexpr int32_t kByLayer = -1;
constexpr int32_t kByBlock = 0;
constexpr int32_t kTrueColor = 0x1000000;

uint32_t AciToRgb(int aci);            // 0xRRGGBB
int NearestAci(uint32_t rgb);          // 1..255
uint32_t ColorRgb(int32_t color);      // for an index or a true colour
std::wstring ColorName(int32_t color); // "ByLayer", "Red", "Colour 30", "RGB 12,200,40"

// ---- line types ------------------------------------------------------------
struct LineTypeDef {
    const wchar_t* name;         // "DASHED"
    const wchar_t* description;  // "Dashed __ __ __"
    std::vector<double> pattern; // millimetres: dash > 0, gap < 0, dot 0
};
// Index: 0 is CONTINUOUS. Objects use kByLayer (-1) or kByBlock (-2).
const std::vector<LineTypeDef>& LineTypes();
// Index, kLtByLayer, kLtByBlock, or -100 if unknown (case-insensitive).
int FindLineType(const std::wstring& name);
constexpr int kLtByLayer = -1, kLtByBlock = -2;
std::wstring LineTypeName(int lt);

// ---- line weights ------------------------------------------------------------
// Hundredths of a millimetre, or one of these.
constexpr int kLwByLayer = -1, kLwByBlock = -2, kLwDefault = -3;
const std::vector<int>& LineWeights();  // 0, 5, 9, 13 ... 211
std::wstring LineWeightName(int lw);    // "0.25 mm", "ByLayer", "Default"
constexpr int kDefaultLineWeight = 25;
// Pixels on screen for a line weight (hundredths of a mm), at 96 dpi: each
// step clearly thicker than the one before (0.25 mm: 1, 0.5 mm: 3, 1 mm: 7).
double ScreenLineWidth(int lw);

// ---- layers ------------------------------------------------------------------
struct Layer {
    int id = 0;           // stable: objects refer to it
    std::wstring name;
    int32_t color = 7;    // index or true colour (never ByLayer)
    int linetype = 0;
    int lineweight = kLwDefault;
    bool on = true;
    bool frozen = false;
    bool locked = false;
    bool plot = true;
};

// A saved set of layer settings (on, frozen, locked, colour, line type and
// weight), restored by layer name.
struct LayerState {
    std::wstring name;
    std::vector<Layer> layers;
};

// What new objects get.
struct CurrentStyle {
    int layer = 0;
    int32_t color = kByLayer;
    int linetype = kLtByLayer;
    int lineweight = kLwByLayer;
    double ltscale = 1;
};
