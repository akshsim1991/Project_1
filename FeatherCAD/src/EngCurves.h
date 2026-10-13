// EngCurves.h - the engineering curves of engineering drawing, drawn with
// their construction as in exams: conics by the eccentricity method,
// cycloid, epicycloid, hypocycloid, involutes of a circle and of a polygon,
// the Archimedean spiral, the helix; and the tangent and normal at a point
// of any curve.
//
// Each result: the curve itself (thick), the construction (thin), the
// points named, and a note. Plain C++ (no Windows).
#pragma once
#include <string>
#include <vector>

#include "Entity.h"

// A conic by its eccentricity e (< 1 ellipse, 1 parabola, > 1 hyperbola):
// the directrix upright through `at`, the axis level through it, the focus
// `d` to the right of the directrix.
bool ConicCurve(Vec2 at, double d, double e, double textHeight, std::vector<Entity>& out, std::wstring& why);

// The curve traced by a point on a circle of diameter `dia` rolling (one
// turn) along a line from `at` (cycloid), or round the outside / inside of
// a base circle of diameter `baseDia` centred at `at` (epicycloid /
// hypocycloid), the point starting where the circles touch.
enum class Roll { Cycloid, Epicycloid, Hypocycloid };
bool RollingCurve(Roll kind, Vec2 at, double dia, double baseDia, double textHeight, std::vector<Entity>& out,
                  std::wstring& why);

// The involute of a circle (diameter `size`, one turn, `at` its centre) or
// of a regular polygon (`sides` > 2, side `size`, `at` its centre).
bool InvoluteCurve(Vec2 at, int sides, double size, double textHeight, std::vector<Entity>& out, std::wstring& why);

// The Archimedean spiral about the pole `at`: from radius r0 to r1 in
// `turns` turns (anticlockwise).
bool SpiralCurve(Vec2 at, double r0, double r1, double turns, double textHeight, std::vector<Entity>& out, std::wstring& why);

// A helix on a cylinder of diameter `dia`: its front view from `at` (the
// bottom of the axis) up `turns` x `pitch`, the part at the back hidden
// (dashed), and its top view below.
bool HelixCurve(Vec2 at, double dia, double pitch, double turns, double textHeight, std::vector<Entity>& out, std::wstring& why);

// The tangent and normal at the point of `e` nearest `near` (lines 2 x
// `length` long, named T-T and N-N). False if `e` is not a curve.
bool TangentNormal(const Entity& e, Vec2 near, double length, double textHeight, std::vector<Entity>& out, Vec2* at = nullptr);
