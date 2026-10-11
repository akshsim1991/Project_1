// Snap.h - object snaps: finds the exact point on existing geometry that
// the cursor is near (an end point, a centre, an intersection...).
#pragma once
#include "Document.h"

enum SnapMode : unsigned {
    kSnapEndpoint = 1u << 0,
    kSnapMidpoint = 1u << 1,
    kSnapCentre = 1u << 2,
    kSnapQuadrant = 1u << 3,
    kSnapIntersection = 1u << 4,
    kSnapPerpendicular = 1u << 5,
    kSnapTangent = 1u << 6,
    kSnapNearest = 1u << 7,
    kSnapAll = (1u << 8) - 1,
    kSnapDefault = kSnapEndpoint | kSnapMidpoint | kSnapCentre | kSnapQuadrant | kSnapIntersection |
                   kSnapPerpendicular,
};
constexpr int kSnapModeCount = 8;
// Typed NONE at a point prompt: no object snap for that point.
constexpr unsigned kSnapNone = 1u << 15;

const wchar_t* SnapName(unsigned mode);     // "Endpoint"
const wchar_t* SnapKeyword(unsigned mode);  // "END": typed at a point prompt

struct SnapQuery {
    Vec2 cursor;
    double aperture = 1;  // search radius in drawing units
    unsigned modes = kSnapDefault;
    bool hasBase = false;  // perpendicular and tangent are measured from here
    Vec2 base;
};

struct SnapResult {
    bool found = false;
    unsigned mode = 0;
    Vec2 point;
    uint64_t id = 0;  // the object snapped to
};

SnapResult FindSnap(const Document& doc, const SnapQuery& query);
