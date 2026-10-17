// Curve.h - lines, arcs, circles and polylines seen as curves measured
// along their length: a distance s from the start (0..length) names any
// point on them. Trim, extend, break, lengthen and path arrays work with
// these distances.
#pragma once
#include <vector>

#include "Entity.h"

// A straight or circular piece of a curve, in the curve's own direction.
struct CurvePiece {
    bool arc = false;
    Vec2 a, b;           // start and end point
    Vec2 c;              // arc centre
    double r = 0;        // arc radius
    double a0 = 0;       // arc start angle
    double sweep = 0;    // arc sweep, signed: < 0 clockwise
    double len = 0;
    Vec2 At(double s) const;          // 0 <= s <= len
    Vec2 Direction(double s) const;   // unit tangent, in the direction of travel
    double Param(Vec2 p) const;       // distance of the nearest point from the start
    Prim ToPrim() const;              // for intersections (arcs counter-clockwise)
};

bool IsCurve(const Entity& e);        // line, arc, circle or polyline
bool IsClosedCurve(const Entity& e);  // circle or closed polyline
std::vector<CurvePiece> CurvePieces(const Entity& e);
double CurveLength(const Entity& e);
Vec2 CurvePoint(const Entity& e, double s);
Vec2 CurveDirection(const Entity& e, double s);
// Distance along the curve of the point nearest to p.
double CurveParam(const Entity& e, Vec2 p);
// The part of the curve from s0 to s1. On a closed curve s0 > s1 goes
// round through the start. False if the part is too short.
bool SubCurve(const Entity& e, double s0, double s1, Entity& out);
// Where the curve meets the primitives, as sorted distances along it.
std::vector<double> CrossingParams(const Entity& e, const std::vector<Prim>& others);
// Moves one end of an open curve (line, arc, open polyline) by `delta`
// along the curve: positive lengthens, negative shortens. False if the end
// piece would vanish.
bool ChangeEnd(Entity& e, bool atEnd, double delta);
