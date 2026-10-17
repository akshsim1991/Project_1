// Grips.h - the handles shown on selected objects (end points, midpoints,
// centres, quadrants, vertices, fit points) and what dragging one does:
// an end point stretches a line, a quadrant changes a circle's radius, a
// midpoint moves a line or bends an arc, and so on.
#pragma once
#include <vector>

#include "Entity.h"

struct Grip {
    uint64_t id = 0;  // the object
    int index = 0;    // which handle (meaning depends on the object type)
    Vec2 p;
};

void GripsOf(const Entity& e, std::vector<Grip>& out);
// Moves handle `index` (now at `from`) to `to`. False if the result would
// be invalid (an arc through three points in a line, a zero radius).
bool DragGrip(Entity& e, int index, Vec2 from, Vec2 to);
