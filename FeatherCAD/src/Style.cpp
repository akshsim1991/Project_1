// Style.cpp - see Style.h.
#include "Style.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace {
uint32_t Rgb(int r, int g, int b) { return (uint32_t)((r << 16) | (g << 8) | b); }

// The AutoCAD colour index palette: 1-9 named colours, 10-249 hues in
// steps of 15 degrees at five brightnesses (even: pure, odd: pastel),
// 250-255 greys.
const std::vector<uint32_t>& Palette() {
    static const std::vector<uint32_t> p = [] {
        std::vector<uint32_t> v(256, 0);
        const uint32_t named[10] = {0,        Rgb(255, 0, 0),   Rgb(255, 255, 0), Rgb(0, 255, 0),     Rgb(0, 255, 255),
                                    Rgb(0, 0, 255), Rgb(255, 0, 255), Rgb(255, 255, 255), Rgb(128, 128, 128), Rgb(192, 192, 192)};
        for (int i = 0; i < 10; ++i) v[(size_t)i] = named[i];
        const int values[5] = {255, 204, 153, 127, 76};
        for (int i = 10; i < 250; ++i) {
            const double hue = (i / 10 - 1) * 15.0;
            const int d = i % 10;
            const double val = values[d / 2];
            const double low = (d % 2) ? val / 2 : 0;  // pastel: the other channels half up
            // HSV with full saturation, then lifted for pastels.
            const double h6 = hue / 60.0;
            const double x = 1 - std::fabs(std::fmod(h6, 2.0) - 1);
            double r = 0, g = 0, b = 0;
            switch ((int)h6 % 6) {
                case 0: r = 1, g = x; break;
                case 1: r = x, g = 1; break;
                case 2: g = 1, b = x; break;
                case 3: g = x, b = 1; break;
                case 4: r = x, b = 1; break;
                default: r = 1, b = x; break;
            }
            auto ch = [&](double c) { return (int)std::lround(low + (val - low) * c); };
            v[(size_t)i] = Rgb(ch(r), ch(g), ch(b));
        }
        const int greys[6] = {51, 80, 105, 130, 190, 255};
        for (int i = 0; i < 6; ++i) v[(size_t)(250 + i)] = Rgb(greys[i], greys[i], greys[i]);
        return v;
    }();
    return p;
}

std::wstring Upper(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)std::towupper(c);
    return s;
}
}  // namespace

uint32_t AciToRgb(int aci) { return Palette()[(size_t)std::clamp(aci, 1, 255)]; }

int NearestAci(uint32_t rgb) {
    const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    int best = 7;
    long bestD = -1;
    for (int i = 1; i < 256; ++i) {
        const uint32_t c = Palette()[(size_t)i];
        const long dr = r - (int)((c >> 16) & 255), dg = g - (int)((c >> 8) & 255), db = b - (int)(c & 255);
        const long d = dr * dr + dg * dg + db * db;
        if (bestD < 0 || d < bestD) bestD = d, best = i;
    }
    return best;
}

uint32_t ColorRgb(int32_t color) {
    if (color & kTrueColor) return (uint32_t)color & 0xFFFFFF;
    return AciToRgb(color);
}

std::wstring ColorName(int32_t color) {
    if (color == kByLayer) return L"ByLayer";
    if (color == kByBlock) return L"ByBlock";
    if (color & kTrueColor) {
        const uint32_t c = (uint32_t)color & 0xFFFFFF;
        return L"RGB " + std::to_wstring(c >> 16) + L"," + std::to_wstring((c >> 8) & 255) + L"," +
               std::to_wstring(c & 255);
    }
    static const wchar_t* names[10] = {L"", L"Red", L"Yellow", L"Green", L"Cyan", L"Blue", L"Magenta",
                                       L"Black/White", L"Grey", L"Light grey"};
    if (color >= 1 && color <= 9) return names[color];
    return L"Colour " + std::to_wstring(color);
}

const std::vector<LineTypeDef>& LineTypes() {
    // As in AutoCAD's acadiso.lin (millimetres).
    static const std::vector<LineTypeDef> list = {
        {L"CONTINUOUS", L"Solid line", {}},
        {L"DASHED", L"Dashed __ __ __", {12.7, -6.35}},
        {L"HIDDEN", L"Hidden _ _ _ _", {6.35, -3.175}},
        {L"CENTER", L"Center ____ _ ____", {31.75, -6.35, 6.35, -6.35}},
        {L"PHANTOM", L"Phantom ____ _ _ ____", {31.75, -6.35, 6.35, -6.35, 6.35, -6.35}},
        {L"DOT", L"Dot . . . .", {0, -6.35}},
        {L"DASHDOT", L"Dash dot __ . __ .", {12.7, -6.35, 0, -6.35}},
        {L"DIVIDE", L"Divide __ . . __", {12.7, -6.35, 0, -6.35, 0, -6.35}},
        {L"BORDER", L"Border __ __ . __", {12.7, -6.35, 12.7, -6.35, 0, -6.35}},
        {L"DASHED2", L"Dashed (half) _ _ _", {6.35, -3.175}},
        {L"HIDDEN2", L"Hidden (half) _ _ _", {3.175, -1.5875}},
        {L"CENTER2", L"Center (half) ___ _ ___", {19.05, -3.175, 3.175, -3.175}},
        {L"CONSTRUCTION", L"Construction - . - .", {4, -2, 0, -2}},
    };
    return list;
}

int FindLineType(const std::wstring& name) {
    const std::wstring u = Upper(name);
    const auto& list = LineTypes();
    for (size_t i = 0; i < list.size(); ++i)
        if (u == list[i].name) return (int)i;
    if (u == L"BYLAYER") return kLtByLayer;
    if (u == L"BYBLOCK") return kLtByBlock;
    return -100;
}

std::wstring LineTypeName(int lt) {
    if (lt == kLtByLayer) return L"ByLayer";
    if (lt == kLtByBlock) return L"ByBlock";
    if (lt >= 0 && (size_t)lt < LineTypes().size()) return LineTypes()[(size_t)lt].name;
    return L"CONTINUOUS";
}

const std::vector<int>& LineWeights() {
    static const std::vector<int> list = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106,
                                          120, 140, 158, 200, 211};
    return list;
}

std::wstring LineWeightName(int lw) {
    if (lw == kLwByLayer) return L"ByLayer";
    if (lw == kLwByBlock) return L"ByBlock";
    if (lw == kLwDefault || lw < 0) return L"Default";
    wchar_t buf[32];
    swprintf(buf, 32, L"%.2f mm", lw / 100.0);
    return buf;
}
