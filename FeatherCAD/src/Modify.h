// Modify.h - the geometry behind the editing commands: trim, extend,
// offset, fillet, chamfer, break, join, explode and stretch. Plain C++ (no
// Windows), unit tested on its own. The commands in CommandsModify.cpp
// pick the objects and record the changes for undo.
#pragma once
#include <string>
#include <vector>

#include "Curve.h"

// ---- trim and extend ---------------------------------------------------------
// Removes the part of `e` around `pick` that lies between its crossings
// with `edges` (the other objects). `out` gets what is left (0, 1 or 2
// pieces) and `removed` the part taken away. With no crossing at all the
// whole object goes, as in other programs' quick trim. False if `e` cannot
// be trimmed (a closed shape needs two crossings).
bool TrimAt(const Entity& e, Vec2 pick, const std::vector<Prim>& edges, std::vector<Entity>& out, Entity* removed);
// Lengthens the end of `e` nearer to `pick` up to the first of `edges` it
// would meet (no further than `reach`). False if nothing is in the way.
bool ExtendAt(const Entity& e, Vec2 pick, const std::vector<Prim>& edges, double reach, Entity& out);

// ---- offset ----------------------------------------------------------------
// +1 if p is on the left of the curve's direction, -1 on the right.
int OffsetSide(const Entity& e, Vec2 p);
// A copy of `e` at distance `d` (positive: to the left). False if the
// result would vanish (a circle smaller than nothing, a shape too narrow).
bool OffsetEntity(const Entity& e, double d, Entity& out);

// ---- fillet and chamfer --------------------------------------------------------
struct CornerResult {
    Entity first, second;  // the two objects, trimmed or extended
    bool changeFirst = false, changeSecond = false;
    bool hasJoin = false;  // the fillet arc or chamfer line
    Entity join;
};
// Rounds the corner between two lines, arcs or circles (picked near the
// parts to keep) with radius r; r = 0 makes a sharp corner. With `trim`
// false the objects are left as they are.
bool FilletObjects(const Entity& a, Vec2 pickA, const Entity& b, Vec2 pickB, double r, bool trim, CornerResult& res,
                   std::wstring& why);
// Bevels the corner between two lines: d1 along the first, d2 along the second.
bool ChamferLines(const Entity& a, Vec2 pickA, const Entity& b, Vec2 pickB, double d1, double d2, bool trim,
                  CornerResult& res, std::wstring& why);
// The second chamfer distance for a first distance and an angle (radians)
// from the first line.
bool ChamferDistanceFromAngle(const Entity& a, Vec2 pickA, const Entity& b, Vec2 pickB, double d1, double angle,
                              double& d2);
// Every corner of a polyline between two straight segments; returns how
// many were done. `onlyVertex` >= 0 limits it to that vertex.
int FilletPolyline(Entity& pl, double r, int onlyVertex = -1);
int ChamferPolyline(Entity& pl, double d1, double d2, int onlyVertex = -1);
// The polyline vertex between the two segments nearest to the picks, if
// they are next to each other; -1 otherwise.
int SharedVertex(const Entity& pl, Vec2 pickA, Vec2 pickB);

// ---- break, join, explode, stretch ----------------------------------------------
// Removes the part between distances s1 and s2 along the curve (s1 == s2
// splits it there). On closed shapes the part from s1 counter-clockwise to
// s2 goes.
bool BreakCurve(const Entity& e, double s1, double s2, std::vector<Entity>& out, std::wstring& why);

struct JoinGroup {
    std::vector<size_t> sources;  // indices into the input
    Entity result;
};
// Joins lines, arcs and open polylines whose ends meet into polylines
// (straight runs into one line, arcs of the same circle into one arc).
std::vector<JoinGroup> JoinObjects(const std::vector<Entity>& in);

// A polyline becomes its lines and arcs; an array its copies.
bool Explode(const Entity& e, std::vector<Entity>& out);

// Moves the points of `e` that lie inside `window` by d (all of it when it
// is completely inside). False if nothing moved.
bool StretchEntity(Entity& e, const Box& window, Vec2 d);
