// Dimension.h - dimensions: linear (horizontal, vertical or rotated),
// aligned, radius, diameter, angular, ordinate and angular coordinate.
//
// A dimension remembers the points it measures and, when they were picked
// on objects, which object and which of its grips each point came from.
// When those objects change (stretched, trimmed, moved), the dimension
// follows them (associative dimensions). Changing a dimension's value
// changes the geometry instead (driving dimensions): the part of the shape
// on the far side moves, so a rectangle stays a rectangle.
//
// Pure C++ (no Windows): the canvas draws what DimLayout returns.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Entity.h"

class Document;

struct DimStyle {
    double textHeight = 3.5;
    double arrowSize = 3;
    double extOffset = 1;   // gap between the object and an extension line
    double extBeyond = 2;   // extension lines go this far past the dimension line
    double textGap = 1;     // between the dimension line and the text
    int precision = 2;      // decimal places
    bool trimZeros = true;  // 12.50 is shown as 12.5, 40.00 as 40
    bool operator==(const DimStyle& o) const {
        return textHeight == o.textHeight && arrowSize == o.arrowSize && extOffset == o.extOffset &&
               extBeyond == o.extBeyond && textGap == o.textGap && precision == o.precision &&
               trimZeros == o.trimZeros;
    }
    bool operator!=(const DimStyle& o) const { return !(*this == o); }
};

// Where a measured point comes from: grip `grip` of object `id`. A negative
// grip stands for a whole circle or arc (radius and diameter): -1 for a
// circle or arc, -1 - i for arc segment i of a polyline.
struct DimRef {
    uint64_t id = 0;  // 0: not attached
    int grip = 0;
};

struct DimData {
    enum Kind : uint8_t { Linear, Aligned, Radius, Diameter, Angular, Ordinate, AngularCoord } kind = Aligned;
    // Linear, aligned: pts[0] and pts[1] are measured.
    // Radius, diameter: pts[0] is the centre, pts[1] a point on the circle.
    // Angular: the lines pts[0]-pts[1] and pts[2]-pts[3]. With `rays`,
    //   the angle is between the rays from where they meet towards pts[1]
    //   and pts[3] (three points, or an arc's angle).
    // Ordinate: pts[0] is measured from the origin pts[1], along `angle`
    //   (0: its X, pi/2: its Y).
    // Angular coordinate: the angle about pts[0] from pts[1] (0 degrees)
    //   counter-clockwise to pts[2].
    std::array<Vec2, 4> pts{};
    std::array<DimRef, 4> refs{};
    double angle = 0;   // Linear: the direction measured (0 horizontal, pi/2 vertical)
    bool rays = false;
    // Linear, aligned: a point on the dimension line. Radius, diameter:
    // where the text goes. Angular: a point on the dimension arc (it also
    // picks which of the angles between two lines is meant).
    Vec2 loc;
    std::wstring text;  // replaces the value; "<>" in it stands for the value
    DimStyle style;
    bool driven = false;  // reports only (shown in brackets): never changes the geometry
};

Entity MakeDimension(DimData d);
const wchar_t* DimKindName(DimData::Kind k);

// The direction a linear or aligned dimension measures along.
Vec2 DimDirection(const DimData& d);
// The value measured: a length, a radius, a diameter or an angle (radians).
double DimMeasure(const DimData& d);
// "25", "R12.5", "\x00D8" "40" (O with a stroke: in every font), "45\x00B0"
std::wstring DimValueText(const DimData& d);
// What is shown: the value, or the text that replaces it.
std::wstring DimDisplayText(const DimData& d);
// A number in the style's precision, trailing zeros removed if asked.
std::wstring DimNumber(double v, const DimStyle& s);

// What a dimension looks like.
struct DimGraphics {
    std::vector<Prim> lines;                 // dimension, extension and leader lines
    std::vector<std::array<Vec2, 3>> arrows; // filled triangles
    std::wstring text;
    Vec2 textPos;             // centre of the text
    double textAngle = 0;     // radians, always readable (between -90 and 90 degrees)
    double textHeight = 0;
    double textWidth = 0;     // estimated (Arial)
    std::array<Vec2, 4> TextBox() const;
};
void DimLayout(const DimData& d, DimGraphics& g);
// Width of text `s` drawn `height` high in Arial (capital letters `height` tall).
double TextWidth(const std::wstring& s, double height);
// The em size of a font whose capitals are `height` tall.
double FontSizeForHeight(double height);

// Geometry for hit tests and selection (Entity.cpp uses these).
void DimPrimitives(const Entity& e, std::vector<Prim>& out);
Box DimBounds(const Entity& e);
double DimDistance(const Entity& e, Vec2 p);
bool DimTouchesBox(const Entity& e, const Box& box);

// Moves, turns, scales or mirrors the dimension's points with `point`;
// `vec` does the same to directions. The text keeps its size.
void DimMap(Entity& e, const std::function<Vec2(Vec2)>& point, const std::function<Vec2(Vec2)>& vec);
// STRETCH: the points inside the window move by d.
bool DimStretch(Entity& e, const Box& window, Vec2 d);

// Grips: linear and aligned have the two measured points and the text;
// the others have the text.
void DimGrips(const Entity& e, std::vector<std::pair<int, Vec2>>& out);
bool DragDimGrip(Entity& e, int index, Vec2 from, Vec2 to);

// ---- associativity ---------------------------------------------------------

// The grip of an object (not a dimension or an array) that lies at p, if any.
DimRef FindRef(const Document& doc, Vec2 p);
// The circle (centre and radius) a radius reference stands for.
bool RefCircle(const Document& doc, const DimRef& r, Vec2& c, double& radius);
bool RefPoint(const Document& doc, const DimRef& r, Vec2& p);
// Re-reads the points from the objects the dimension is attached to (refs
// to objects that are gone are dropped). True if anything changed.
bool FollowRefs(const Document& doc, Entity& dim);
// Drops the refs whose objects no longer pass through the dimension's
// points (it was moved or edited on its own). True if any was dropped.
bool CheckRefs(const Document& doc, Entity& dim);
bool HasRefs(const Entity& dim);
// A polyline or spline the dimension is attached to gained or lost vertices
// (`before` is how it was): its refs find their points again by place, or
// are dropped when the point is gone. True if any ref changed.
bool RenumberRefs(Entity& dim, const Entity& before, const Entity& after);
// Copies of objects and their dimensions: refs to objects in `idMap` are
// pointed at the copies.
void RemapRefs(Entity& e, const std::map<uint64_t, uint64_t>& idMap);

// ---- driving -----------------------------------------------------------------

// Changes the attached geometry so that the dimension measures `value`
// (for angles, in radians). Call inside BeginChange / EndChange. False,
// with the reason in `why`, if it cannot.
bool DriveDimension(Document& doc, uint64_t dimId, double value, std::wstring& why);
