// Geometry.h - 2D vectors, boxes and the curve primitives (segments and
// arcs) that every entity is made of: closest points, intersections and
// box tests. Pure C++ (no Windows), so it is unit tested on its own.
//
// Coordinates are drawing units with Y pointing up. Angles are radians,
// counter-clockwise from the positive X axis.
#pragma once
#include <cmath>
#include <limits>
#include <vector>

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2 * kPi;
constexpr double kEps = 1e-9;

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
};

inline double Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double Length(Vec2 v) { return std::hypot(v.x, v.y); }
inline double Dist(Vec2 a, Vec2 b) { return Length(b - a); }
inline Vec2 Perp(Vec2 v) { return {-v.y, v.x}; }  // +90 degrees
inline Vec2 Normalized(Vec2 v) {
    double l = Length(v);
    return l > 0 ? v / l : Vec2{1, 0};
}
inline double AngleOf(Vec2 v) { return std::atan2(v.y, v.x); }
inline Vec2 Polar(Vec2 c, double r, double angle) { return {c.x + r * std::cos(angle), c.y + r * std::sin(angle)}; }
inline Vec2 Lerp(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }
inline Vec2 RotateAround(Vec2 p, Vec2 c, double angle) {
    const double cs = std::cos(angle), sn = std::sin(angle);
    const Vec2 d = p - c;
    return {c.x + d.x * cs - d.y * sn, c.y + d.x * sn + d.y * cs};
}
inline bool Near(Vec2 a, Vec2 b, double tol = 1e-9) { return Dist(a, b) <= tol; }

// Angle in [0, 2pi).
double NormalizeAngle(double a);
// Counter-clockwise sweep from `from` to `to`, in [0, 2pi).
double SweepCCW(double from, double to);
// Is `a` on the counter-clockwise arc that starts at `start` and sweeps `sweep`?
bool AngleInSweep(double a, double start, double sweep, double tol = 1e-12);
inline double ToDegrees(double r) { return r * 180.0 / kPi; }
inline double ToRadians(double d) { return d * kPi / 180.0; }

struct Box {
    double minX = std::numeric_limits<double>::infinity(), minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity(), maxY = -std::numeric_limits<double>::infinity();

    Box() = default;
    Box(Vec2 a, Vec2 b) { Add(a); Add(b); }
    bool Empty() const { return minX > maxX || minY > maxY; }
    void Add(Vec2 p) {
        if (p.x < minX) minX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.x > maxX) maxX = p.x;
        if (p.y > maxY) maxY = p.y;
    }
    void Add(const Box& b) {
        if (b.Empty()) return;
        Add(Vec2{b.minX, b.minY});
        Add(Vec2{b.maxX, b.maxY});
    }
    Box Inflated(double d) const {
        Box b = *this;
        b.minX -= d; b.minY -= d; b.maxX += d; b.maxY += d;
        return b;
    }
    bool Contains(Vec2 p) const { return p.x >= minX && p.x <= maxX && p.y >= minY && p.y <= maxY; }
    bool Contains(const Box& b) const { return !b.Empty() && b.minX >= minX && b.maxX <= maxX && b.minY >= minY && b.maxY <= maxY; }
    bool Intersects(const Box& b) const { return !(b.minX > maxX || b.maxX < minX || b.minY > maxY || b.maxY < minY); }
    double Width() const { return Empty() ? 0 : maxX - minX; }
    double Height() const { return Empty() ? 0 : maxY - minY; }
    Vec2 Center() const { return {(minX + maxX) / 2, (minY + maxY) / 2}; }
};

// A straight segment or a counter-clockwise circular arc. A full circle is
// an arc with sweep 2pi (then a == b).
struct Prim {
    enum Kind { Segment, Arc } kind = Segment;
    Vec2 a, b;           // start and end point
    Vec2 c;              // arc centre
    double r = 0;        // arc radius
    double start = 0;    // arc start angle
    double sweep = 0;    // arc sweep, > 0 (counter-clockwise)

    static Prim Seg(Vec2 a, Vec2 b);
    static Prim ArcCCW(Vec2 c, double r, double start, double sweep);
    static Prim Circle(Vec2 c, double r) { return ArcCCW(c, r, 0, kTau); }
    bool FullCircle() const { return kind == Arc && sweep >= kTau - 1e-12; }
    Vec2 Mid() const;
    Box Bounds() const;
    double LengthOf() const { return kind == Segment ? Dist(a, b) : r * sweep; }
};

// The arc of a polyline segment from p0 to p1 with the given bulge (tan of
// a quarter of the included angle; negative = clockwise). Returned as a
// counter-clockwise Prim (so for a clockwise bulge its `a` is p1).
Prim ArcFromBulge(Vec2 p0, Vec2 p1, double bulge);
// The bulge of the arc through p0 and p1 that leaves p0 in direction
// `tangent` (used for tangent arcs in polylines).
double TangentBulge(Vec2 p0, Vec2 tangent, Vec2 p1);
// Circle through three points; false if they are (nearly) collinear.
bool CircleFrom3Points(Vec2 p1, Vec2 p2, Vec2 p3, Vec2& centre, double& radius);

Vec2 ClosestPoint(const Prim& p, Vec2 q);
inline double Distance(const Prim& p, Vec2 q) { return Dist(ClosestPoint(p, q), q); }
// Intersection points of two primitives (appended to `out`).
void Intersect(const Prim& p, const Prim& q, std::vector<Vec2>& out);
// Does the primitive touch the box (cross its edges or lie inside it)?
bool PrimTouchesBox(const Prim& p, const Box& box);
// Foot of the perpendicular from `from` onto the primitive (if it lands on it).
bool PerpendicularFoot(const Prim& p, Vec2 from, Vec2& foot);
// Points where lines from `from` touch a circular primitive (0 or 2).
void TangentPoints(const Prim& p, Vec2 from, std::vector<Vec2>& out);
