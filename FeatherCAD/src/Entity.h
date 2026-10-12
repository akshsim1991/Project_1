// Entity.h - the drawing objects: lines, circles, arcs, polylines
// (rectangles are closed polylines, as in other CAD programs) and
// associative arrays and dimensions. Every entity can be broken into Prims (segments and
// arcs) for hit testing, snapping and intersections.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Geometry.h"

enum class EntityType : uint8_t {
    Line, Circle, Arc, Polyline, Array, Ellipse, Spline, Point, Dimension,
    Text, Leader, Hatch, Insert,  // annotations and block references (Annot.h)
    Viewport,                     // on a sheet: a window onto the drawing at a scale
    Relation,                     // a relationship kept between objects (Relate.h)
    Image                         // a picture (photo, scan) from p0 (bottom left) to p1 (top right)
};

struct Vertex {
    Vec2 p;
    double bulge = 0;  // arc to the next vertex: tan(included angle / 4), < 0 clockwise
};

struct ArrayData;
struct DimData;  // Dimension.h
struct Annot;    // Annot.h

struct Entity {
    uint64_t id = 0;  // set by the Document; also the drawing order
    EntityType type = EntityType::Line;
    Vec2 p0, p1;                // line: start and end; circle, arc, ellipse, point: p0 = centre / position
                                // ellipse: p1 = major axis vector (from the centre)
    double radius = 0;          // circle and arc; ellipse: minor / major axis ratio
    double startAngle = 0;      // arc, counter-clockwise from start to end (radians);
    double endAngle = 0;        // ellipse: start and end parameters; spline: the part kept
                                // after a trim or break, as fractions of its length (endAngle 0:
                                // all of it; endAngle < startAngle: across the start of a closed one)
    std::vector<Vertex> verts;  // polyline vertices; spline fit points (bulge unused)
    bool closed = false;        // polyline and spline: back to the start; ellipse: full ellipse
    // How it looks (Style.h): layer id (-1: the current layer when added),
    // colour, line type, line weight, line type scale. Construction geometry
    // is for reference: shown faintly, not printed.
    int layer = -1;
    int32_t color = -1;
    int16_t linetype = -1;
    int16_t lineweight = -1;
    float ltscale = 1;
    bool construction = false;
    // Array: shared and never changed in place (a change makes a new copy),
    // so copies of an array entity (undo history) cost nothing.
    std::shared_ptr<const ArrayData> array;
    std::shared_ptr<const DimData> dim;  // Dimension: shared the same way
    // Text (p0: where it is), leader, hatch, block reference (p0: where it
    // is placed, radius: its scale, startAngle: its rotation, closed:
    // mirrored): shared the same way.
    std::shared_ptr<const Annot> annot;

    static Entity Line(Vec2 a, Vec2 b);
    static Entity Circle(Vec2 centre, double r);
    static Entity Arc(Vec2 centre, double r, double startAngle, double endAngle);
    static Entity Polyline(std::vector<Vertex> verts, bool closed);
    static Entity Rectangle(Vec2 corner1, Vec2 corner2);
    static Entity Array(ArrayData data);
    static Entity Ellipse(Vec2 centre, Vec2 majorAxis, double ratio);  // full ellipse
    static Entity EllipseArc(Vec2 centre, Vec2 majorAxis, double ratio, double startParam, double endParam);
    static Entity Spline(const std::vector<Vec2>& fitPoints, bool closed);
    static Entity Point(Vec2 p);
    static Entity RegularPolygon(Vec2 centre, Vec2 firstVertex, int sides);
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
// Gives `to` the layer, colour, line type and weight of `from`.
void CopyStyle(const Entity& from, Entity& to);

// Points along an ellipse (with their parameters) and a spline, fine enough
// to stand for the curve in hit tests, snaps and intersections.
void EllipsePoints(const Entity& e, std::vector<Vec2>& pts, std::vector<double>* params = nullptr);
void SplinePoints(const Entity& e, std::vector<Vec2>& pts);
double EllipseSweep(const Entity& e);  // parameter range, 2pi for a full ellipse
bool SplineTrimmed(const Entity& e);   // only a part of the spline is kept

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
