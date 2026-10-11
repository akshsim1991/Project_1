// Spline.h - smooth curves through fit points (cubic, curvature-continuous,
// spaced by chord length) and points along ellipses. Both are drawn and
// measured as many short straight pieces.
#pragma once
#include <vector>

#include "Geometry.h"

// Points on the cubic spline through `fit` (closed: back to the start),
// `perSpan` pieces between each pair of fit points. Two fit points give a
// straight line.
void SplinePoints(const std::vector<Vec2>& fit, bool closed, int perSpan, std::vector<Vec2>& out);

// The point of an ellipse at parameter t: centre + a cos t + b sin t, where
// a is the major axis vector and b = Perp(a) * ratio.
inline Vec2 EllipsePoint(Vec2 centre, Vec2 axis, double ratio, double t) {
    return centre + axis * std::cos(t) + Perp(axis) * (ratio * std::sin(t));
}
// The parameter of the ellipse point in the direction of p from the centre.
double EllipseParam(Vec2 centre, Vec2 axis, double ratio, Vec2 p);
