// EngMore.h - more constructions of engineering drawing: plain and diagonal
// scales, projections of a point (the four quadrants), the traces of a
// line, the development of a transition piece by triangulation, the
// intersection of two solids, and the perspective of a block by the visual
// ray method.
//
// Plain C++ (no Windows).
#pragma once
#include <string>
#include <vector>

#include "Dimension.h"
#include "EngGraphics.h"

// ---- scales -------------------------------------------------------------------
// A plain scale (main units, the first one in tenths) or a diagonal scale
// (tenths of the tenths too), for a representative fraction `rf` (paper /
// real: 1:50 is 0.02), long enough for `length` main units of `unit`
// (km, m, dm, cm or mm). Its bottom left at `at`.
bool ScaleDrawing(bool diagonal, Vec2 at, double rf, double length, const std::wstring& unit, double textHeight,
                  std::vector<Entity>& out, std::wstring& why);
// Millimetres in one of the units (0 if unknown).
double UnitMm(const std::wstring& unit);
// "1:50", "1/50", "2:1" or "0.02": the representative fraction. False if not one.
bool ReadRf(const std::wstring& s, double& rf);

// ---- projections of a point and the traces of a line -------------------------------
// Point `name` `aboveHP` above HP (negative: below) and `inFrontVP` in front of
// VP (negative: behind): its projector through `xyAt` (on the XY line), a'
// and a, their distances dimensioned, and which quadrant it is in.
std::vector<Entity> PointProjection(Vec2 xyAt, double aboveHP, double inFrontVP, const std::wstring& name, const DimStyle& style);
const wchar_t* QuadrantName(double aboveHP, double inFrontVP);  // "first quadrant", "on HP"...
// Where the line meets HP (the horizontal trace, h' on XY and h below it)
// and VP (the vertical trace, v on XY and v' above it), the views extended
// to them. `why` if it has no trace (the line parallel to that plane).
struct Traces {
    bool hasHT = false, hasVT = false;
    Vec2 h1, h, v, v1;  // h': on XY in the front view; h: the HT; v: on XY; v': the VT
};
Traces LineTraces(const LineViews& v, double xyY);
std::vector<Entity> TraceConstruction(const LineViews& v, double xyY, const Traces& t, double textHeight);

// ---- development of a transition piece -------------------------------------------------
// A rectangle `a` x `b` at the bottom joined to a circle of diameter `d` at
// the top, `h` above it, centred: its development by triangulation, the seam
// at the middle of the front side, its bottom left at `at`.
bool DevelopTransition(double a, double b, double d, double h, Vec2 at, double textHeight, std::vector<Entity>& out,
                       std::wstring& why);

// ---- intersection of two solids --------------------------------------------------------
struct IntersectSpec {
    enum Shape { Cylinder, Prism, PrismDiamond } upright = Cylinder, across = Cylinder;
    double uprightSize = 60;  // diameter, or the side of the square
    double uprightHeight = 100;
    double acrossSize = 40;   // the horizontal solid: diameter or side
    double acrossLength = 120;
    double axisHeight = 50;   // of the horizontal axis above the base
    double offset = 0;        // of the horizontal axis in front of the upright one (top view)
};
// The front view (above, its base on the line y = at.y) and the top view
// (below, `gap` down) of an upright solid penetrated by a horizontal one,
// their axes square to each other, with the curves (or lines) of
// intersection: visible parts thick, hidden ones dashed.
bool IntersectSolids(const IntersectSpec& s, Vec2 at, double gap, double textHeight, std::vector<Entity>& out,
                     std::wstring& why);

// ---- perspective -----------------------------------------------------------------------
struct PerspectiveSpec {
    double length = 60, width = 40, height = 30;  // the block
    double angle = 30;        // of its length to the picture plane (degrees); a vertical edge touches it
    double distance = 120;    // of the station point in front of the picture plane
    double eye = 50;          // height of the eye above the ground
    double sideways = 20;     // of the station point to the right of the touching edge
};
// Plan (the block, the picture plane, the station point and the visual
// rays), the ground and horizon lines, the vanishing points and the
// perspective view below. `at`: where the edge touching the picture plane
// meets it in the plan.
bool PerspectiveDrawing(const PerspectiveSpec& s, Vec2 at, double textHeight, std::vector<Entity>& out, std::wstring& why);
// The perspective of a point (x along the picture plane, y behind it, z up)
// on the picture plane, from the station point (sx, -distance, eye).
Vec2 PerspectivePoint(double x, double y, double z, double sx, double distance, double eye);
