// FreeSketch.h - freehand strokes tidied into CAD objects: a wobbly stroke
// becomes a straight line, an arc, a circle, or a polyline of lines and arcs
// (split at its corners); nearly level or upright lines are made exactly so.
//
// Plain C++ (no Windows).
#pragma once
#include <vector>

#include "Entity.h"

struct StrokeOptions {
    double tol = 1;                // how far (drawing units) a hand-drawn line may wander from straight
    double straighten = 0.07;      // radians (about 4 degrees): closer to level or upright is made exact
};

// What the stroke looks like, as one or more objects (empty if too short).
std::vector<Entity> RecognizeStroke(const std::vector<Vec2>& stroke, const StrokeOptions& opt);
