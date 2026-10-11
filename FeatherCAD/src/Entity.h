// Entity.h - the drawing objects: lines, circles, arcs, polylines
// (rectangles are closed polylines, as in other CAD programs) and
// associative arrays. Every entity can be broken into Prims (segments and
// arcs) for hit testing, snapping and intersections.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Geometry.h"

enum class EntityType : uint8_t { Line, Circle, Arc, Polyline, Array };

struct Vertex {
    Vec2 p;
    double bulge = 0;  // arc to the next vertex: tan(included angle / 4), < 0 clockwise
};

struct ArrayData;

struct Entity {
    uint64_t id = 0;  // set by the Document; also the drawing order
    EntityType type = EntityType::Line;
    Vec2 p0, p1;                // line: start and end; circle and arc: p0 = centre
    double radius = 0;          // circle and arc
    double startAngle = 0;      // arc, counter-clockwise from start to end (radians)
    double endAngle = 0;
    std::vector<Vertex> verts;  // polyline
    bool closed = false;        // polyline: the last vertex joins the first
    // Array: shared and never changed in place (a change makes a new copy),
    // so copies of an array entity (undo history) cost nothing.
    std::shared_ptr<const ArrayData> array;

    static Entity Line(Vec2 a, Vec2 b);
    static Entity Circle(Vec2 centre, double r);
    static Entity Arc(Vec2 centre, double r, double startAngle, double endAngle);
    static Entity Polyline(std::vector<Vertex> verts, bool closed);
    static Entity Rectangle(Vec2 corner1, Vec2 corner2);
    static Entity Array(ArrayData data);
};

// An associative array: copies of the source objects laid out in a grid,
// around a centre or along a path. It stays one object: moving, rotating
// or editing it (ARRAYEDIT) updates every copy; EXPLODE makes the copies
// ordinary objects.
struct ArrayData {
    enum Kind : uint8_t { Rect, Polar, Path } kind = Rect;
    std::vector<Entity> items;  // the source objects (the first copy)
    // Rect: columns along columnStep, rows along rowStep.
    int columns = 1, rows = 1;
    Vec2 columnStep, rowStep;
    // Polar: `count` copies around `centre`, spread over `fillAngle`
    // (radians, negative = clockwise), turned with the array or not.
    Vec2 centre;
    int count = 1;
    double fillAngle = kTau;
    bool rotateItems = true;
    // Path: `count` copies along path[0] from its start, `spacing` apart
    // (0 = spread evenly), turned to follow the path or not.
    std::vector<Entity> path;
    double spacing = 0;
    bool align = true;
};

const wchar_t* TypeName(EntityType type);

// The copies an array stands for (arrays inside arrays are expanded too).
void ExpandArray(const Entity& e, std::vector<Entity>& out);
size_t ArrayCopyCount(const ArrayData& a);

// Number of segments in a polyline (closed polylines have one more).
size_t SegmentCount(const Entity& e);
void Primitives(const Entity& e, std::vector<Prim>& out);
Box Bounds(const Entity& e);
double DistanceTo(const Entity& e, Vec2 p);
// Window selection: completely inside. Crossing selection: touches.
bool InsideBox(const Entity& e, const Box& box);
bool TouchesBox(const Entity& e, const Box& box);
// Total length (perimeter for closed shapes).
double LengthOf(const Entity& e);

void Translate(Entity& e, Vec2 d);
void Rotate(Entity& e, Vec2 centre, double angle);
void Scale(Entity& e, Vec2 base, double factor);  // factor > 0
void Mirror(Entity& e, Vec2 a, Vec2 b);           // about the line through a and b

// Characteristic points used by object snaps.
void EndPoints(const Entity& e, std::vector<Vec2>& out);
void MidPoints(const Entity& e, std::vector<Vec2>& out);
void Centres(const Entity& e, std::vector<Vec2>& out);
void Quadrants(const Entity& e, std::vector<Vec2>& out);
