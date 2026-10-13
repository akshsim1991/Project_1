// EngGraphics.h - the constructions of engineering drawing (engineering
// graphics, CAED): isometric drawing, dividing a curve into equal parts,
// developments of lateral surfaces, the true length of a line from its two
// views, the true shape of a section, the cutting-plane line, and the BIS
// (SP 46) projection symbol.
//
// Plain C++ (no Windows).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "Annot.h"
#include "Document.h"
#include "Entity.h"

// ---------------------------------------------------------------------------
// Isometric drawing
// ---------------------------------------------------------------------------

// Isometric length / true length (isometric projection): sqrt(2/3).
constexpr double kIsoScale = 0.816496580927726;

enum class IsoPlane : int { Left, Top, Right };
const wchar_t* IsoPlaneName(IsoPlane p);
IsoPlane NextIsoPlane(IsoPlane p);
// The two directions lines take in a plane: Left 150 and 90 degrees, Top 30
// and 150, Right 30 and 90.
void IsoAxes(IsoPlane p, Vec2& u, Vec2& v);
// The nearest point of the isometric grid (points `spacing` apart along the
// 30 and 150 degree lines, so `spacing` apart upright too).
Vec2 IsoGridPoint(Vec2 p, Vec2 origin, double spacing);
// p moved onto the nearer of the plane's two directions through `base`.
Vec2 IsoOrtho(Vec2 base, Vec2 p, IsoPlane plane);
// A direction along an isometric axis: 30, 90, 150, 210, 270 or 330 degrees.
bool IsIsoAxis(double angle);
// A circle of radius r (as it would be drawn: already scaled if wanted)
// lying in an isometric plane: an ellipse, its half axes 1.2247 r and 0.7071 r.
Entity IsoCircle(Vec2 centre, double r, IsoPlane plane);
// The isometric scale as drawn in exams: a true scale on a 45 degree line,
// projected down onto a 30 degree line (the isometric lengths), marked
// every `step` from `at`.
std::vector<Entity> IsoScaleDrawing(Vec2 at, double length, double step, double textHeight);

// ---------------------------------------------------------------------------
// Dividing curves (DIVIDE, MEASURE)
// ---------------------------------------------------------------------------

// Curves that can be divided: line, arc, circle, polyline, ellipse, spline.
bool Dividable(const Entity& e);
// The object as a curve measured along its length (Curve.h): lines, arcs,
// circles and polylines as they are, ellipses and splines as fine polylines.
bool AsMeasuredCurve(const Entity& e, Entity& curve);
// Points dividing it into `parts` equal lengths. An open curve: the points
// between its ends, from the end nearer `from`. A closed one: `parts`
// points, the first where it is nearest `from`.
bool DividePoints(const Entity& e, int parts, Vec2 from, std::vector<Vec2>& out);
// Points every `step` along it, from the end (or point) nearest `from`.
bool MeasurePoints(const Entity& e, double step, Vec2 from, std::vector<Vec2>& out);
// A point's label (as LABEL makes them), `height` tall, beside `point` in
// direction `dir`.
Entity MakePointLabel(Vec2 point, const std::wstring& text, double height, Vec2 dir);
// The labels that follow a and b: "a" -> "b", "9" -> "10", "A" -> "B".
std::wstring NextName(const std::wstring& s);

// ---------------------------------------------------------------------------
// Developments of lateral surfaces (DEVELOP)
// ---------------------------------------------------------------------------

struct SolidSpec {
    enum Kind { Prism, Cylinder, Pyramid, Cone } kind = Prism;
    int sides = 4;          // prism and pyramid
    double base = 40;       // side of the base (prism, pyramid) or its diameter (cylinder, cone)
    double height = 60;     // of the axis
    // Prism and pyramid, resting on its base: a side of the base parallel to
    // VP nearest the viewer (false), or a corner nearest the viewer (true).
    bool cornerFront = false;
    int divisions = 12;     // cylinder and cone: generators drawn
    // Cut by a plane square to VP: through the axis `cutHeight` above the
    // base, `cutAngle` degrees to HP (rising to the right in the front view).
    // The part on the base is kept.
    bool cut = false;
    double cutHeight = 30, cutAngle = 30;
};
// The name a solid is known by: "square prism", "cone"...
std::wstring SolidName(const SolidSpec& s);
// The development, its bottom left at `at`: the outline (thick), fold lines
// or generators (thin), the corners or generators named (A, B... or 1,
// 2...), and a note saying what it is. False (with `why`) for sizes that
// cannot be.
bool DevelopSolid(const SolidSpec& s, Vec2 at, double textHeight, std::vector<Entity>& out, std::wstring& why);

// ---------------------------------------------------------------------------
// True length of a line (TRUELENGTH)
// ---------------------------------------------------------------------------

// A line AB by its front view a'b' and its top view ab (below it, the ends
// on the same projectors).
struct LineViews {
    Vec2 a1, b1;  // front view
    Vec2 a, b;    // top view
};
struct TrueLengthResult {
    double tl = 0;           // true length
    double theta = 0;        // to HP (radians)
    double phi = 0;          // to VP
    double alpha = 0, beta = 0;  // apparent angles: front view and top view to XY
    double fvLength = 0, tvLength = 0;
};
TrueLengthResult TrueLengthOf(const LineViews& v);
// The two views of a line from its true length and its angles to HP and VP,
// with end A at a1 (front view) and a (top view); b to the right (or left).
bool ViewsFromTrueLength(Vec2 a1, Vec2 a, double tl, double theta, double phi, bool toRight, LineViews& out,
                         std::wstring& why);
// The rotating-line construction: each view turned level about its end a,
// projected to the other view's locus of b, giving the true length at
// theta (front view) and phi (top view); loci and projectors thin, the
// points named, the results written beside.
std::vector<Entity> TrueLengthConstruction(const LineViews& v, double textHeight, bool withViews);

// ---------------------------------------------------------------------------
// Sections: the cutting plane and the true shape (CUTPLANE, TRUESHAPE)
// ---------------------------------------------------------------------------

// A cutting-plane line through `pts` (two or more): a thin chain line, thick
// at its ends and bends, arrows at the ends pointing to `side` (the way the
// section is seen), and the letter at each arrow.
std::vector<Entity> CuttingPlane(const std::vector<Vec2>& pts, Vec2 side, const std::wstring& letter, double textHeight);

// The true shape of a section by an auxiliary view. The cutting plane is
// seen edge on as the line cutA-cutB in one view; `section` is the
// apparent section in the other view (projectors between the views are
// upright). Each point goes up or down its projector to the cutting plane,
// then square across it to the new reference line X1Y1 (parallel to the
// plane through `x1y1At`) and beyond it by its distance from the XY line
// (`xyY`: the height of XY; if `hasXY` is false, from the section's point
// nearest the cutting plane's view).
struct TrueShapeResult {
    std::vector<Vec2> shape;                         // the true shape, point for point
    std::vector<std::pair<Vec2, Vec2>> projectors;   // from the cutting plane to each point
    Vec2 x1, y1;                                     // the reference line
};
bool TrueShape(Vec2 cutA, Vec2 cutB, const std::vector<Vec2>& section, Vec2 x1y1At, bool hasXY, double xyY,
               TrueShapeResult& out, std::wstring& why);
// Points standing for a closed object (polygon corners, or points along a
// circle, ellipse or spline). Empty if it is not closed.
std::vector<Vec2> OutlinePoints(const Entity& e, bool& curved);

// ---------------------------------------------------------------------------
// BIS (SP 46) drawing setup
// ---------------------------------------------------------------------------

// The first-angle projection symbol (a truncated cone: its front view and,
// to the right, its view from the left), `size` across its larger circle,
// its bottom left at `at`.
std::vector<Entity> FirstAngleSymbol(Vec2 at, double size);
// Millimetres; dimensions 3.5 high in the aligned or unidirectional system;
// layers for each kind of line (visible 0.5 mm, hidden, centre, dimension,
// section, construction, cutting plane), the visible one current; the BIS
// title block for sheets (the "titleblock" property).
void ApplyBisSetup(Document& doc, bool unidirectional);
