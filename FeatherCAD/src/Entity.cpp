// Entity.cpp - see Entity.h.
#include "Entity.h"

#include <algorithm>
#include <functional>

#include "Curve.h"
#include "Spline.h"

namespace {
constexpr size_t kMaxArrayCopies = 100000;

Vec2 Reflect(Vec2 p, Vec2 a, Vec2 u) {  // u: unit direction of the mirror line
    const Vec2 d = p - a;
    return a + u * (2 * Dot(d, u)) - d;
}
Vec2 ReflectVector(Vec2 v, Vec2 u) { return u * (2 * Dot(v, u)) - v; }

// Applies `fn` to a private copy of the array's data.
template <class Fn>
void EditArray(Entity& e, Fn fn) {
    auto copy = std::make_shared<ArrayData>(*e.array);
    fn(*copy);
    e.array = std::move(copy);
}
}  // namespace

Entity Entity::Line(Vec2 a, Vec2 b) {
    Entity e;
    e.type = EntityType::Line;
    e.p0 = a;
    e.p1 = b;
    return e;
}

Entity Entity::Circle(Vec2 centre, double r) {
    Entity e;
    e.type = EntityType::Circle;
    e.p0 = centre;
    e.radius = r;
    return e;
}

Entity Entity::Arc(Vec2 centre, double r, double startAngle, double endAngle) {
    Entity e;
    e.type = EntityType::Arc;
    e.p0 = centre;
    e.radius = r;
    e.startAngle = NormalizeAngle(startAngle);
    e.endAngle = NormalizeAngle(endAngle);
    return e;
}

Entity Entity::Polyline(std::vector<Vertex> verts, bool closed) {
    Entity e;
    e.type = EntityType::Polyline;
    e.verts = std::move(verts);
    e.closed = closed;
    return e;
}

Entity Entity::Rectangle(Vec2 c1, Vec2 c2) {
    return Polyline({{{c1.x, c1.y}}, {{c2.x, c1.y}}, {{c2.x, c2.y}}, {{c1.x, c2.y}}}, true);
}

Entity Entity::Array(ArrayData data) {
    Entity e;
    e.type = EntityType::Array;
    e.array = std::make_shared<const ArrayData>(std::move(data));
    return e;
}

Entity Entity::Ellipse(Vec2 centre, Vec2 majorAxis, double ratio) {
    Entity e;
    e.type = EntityType::Ellipse;
    e.p0 = centre;
    e.p1 = majorAxis;
    e.radius = ratio;
    e.startAngle = 0;
    e.endAngle = 0;
    e.closed = true;
    return e;
}

Entity Entity::EllipseArc(Vec2 centre, Vec2 majorAxis, double ratio, double startParam, double endParam) {
    Entity e = Ellipse(centre, majorAxis, ratio);
    e.startAngle = NormalizeAngle(startParam);
    e.endAngle = NormalizeAngle(endParam);
    e.closed = false;
    return e;
}

Entity Entity::Spline(const std::vector<Vec2>& fit, bool closed) {
    Entity e;
    e.type = EntityType::Spline;
    for (Vec2 p : fit) e.verts.push_back({p, 0});
    e.closed = closed;
    return e;
}

Entity Entity::Point(Vec2 p) {
    Entity e;
    e.type = EntityType::Point;
    e.p0 = p;
    return e;
}

Entity Entity::RegularPolygon(Vec2 centre, Vec2 first, int sides) {
    std::vector<Vertex> v;
    sides = std::max(3, sides);
    for (int i = 0; i < sides; ++i) v.push_back({RotateAround(first, centre, kTau * i / sides), 0});
    return Polyline(std::move(v), true);
}

double EllipseSweep(const Entity& e) {
    if (e.closed) return kTau;
    const double s = SweepCCW(e.startAngle, e.endAngle);
    return s <= 0 ? kTau : s;
}

void EllipsePoints(const Entity& e, std::vector<Vec2>& pts, std::vector<double>* params) {
    const double sweep = EllipseSweep(e);
    const int n = std::max(8, (int)std::ceil(256 * sweep / kTau));
    for (int k = 0; k <= n; ++k) {
        const double t = e.startAngle + sweep * k / n;
        pts.push_back(EllipsePoint(e.p0, e.p1, e.radius, t));
        if (params) params->push_back(t);
    }
}

void SplinePoints(const Entity& e, std::vector<Vec2>& pts) {
    std::vector<Vec2> fit;
    for (const Vertex& v : e.verts) fit.push_back(v.p);
    ::SplinePoints(fit, e.closed, 24, pts);
}

const wchar_t* TypeName(EntityType type) {
    switch (type) {
        case EntityType::Line: return L"Line";
        case EntityType::Circle: return L"Circle";
        case EntityType::Arc: return L"Arc";
        case EntityType::Polyline: return L"Polyline";
        case EntityType::Array: return L"Array";
        case EntityType::Ellipse: return L"Ellipse";
        case EntityType::Spline: return L"Spline";
        case EntityType::Point: return L"Point";
    }
    return L"Object";
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

size_t ArrayCopyCount(const ArrayData& a) {
    switch (a.kind) {
        case ArrayData::Rect: return (size_t)std::max(1, a.columns) * (size_t)std::max(1, a.rows);
        case ArrayData::Polar:
        case ArrayData::Path: return (size_t)std::max(1, a.count);
    }
    return 1;
}

void ExpandArray(const Entity& e, std::vector<Entity>& out) {
    if (e.type != EntityType::Array || !e.array) {
        out.push_back(e);
        return;
    }
    const ArrayData& a = *e.array;
    auto place = [&](const std::function<void(Entity&)>& transform) {
        for (const Entity& item : a.items) {
            if (out.size() >= kMaxArrayCopies) return;
            Entity c = item;
            transform(c);
            if (c.type == EntityType::Array) ExpandArray(c, out);
            else out.push_back(std::move(c));
        }
    };
    switch (a.kind) {
        case ArrayData::Rect:
            for (int r = 0; r < std::max(1, a.rows); ++r)
                for (int c = 0; c < std::max(1, a.columns); ++c) {
                    const Vec2 d = a.columnStep * c + a.rowStep * r;
                    place([&](Entity& x) { Translate(x, d); });
                }
            break;
        case ArrayData::Polar: {
            const int n = std::max(1, a.count);
            const bool full = std::fabs(a.fillAngle) >= kTau - 1e-9;
            const double step = n == 1 ? 0 : full ? a.fillAngle / n : a.fillAngle / (n - 1);
            Box box;
            for (const Entity& item : a.items) box.Add(Bounds(item));
            const Vec2 ref = box.Empty() ? a.centre : box.Center();
            for (int k = 0; k < n; ++k) {
                const double ang = step * k;
                if (a.rotateItems) place([&](Entity& x) { Rotate(x, a.centre, ang); });
                else place([&](Entity& x) { Translate(x, RotateAround(ref, a.centre, ang) - ref); });
            }
            break;
        }
        case ArrayData::Path: {
            if (a.path.empty() || !IsCurve(a.path[0])) {
                place([](Entity&) {});
                break;
            }
            const Entity& path = a.path[0];
            const double L = CurveLength(path);
            const bool closed = IsClosedCurve(path);
            const int n = std::max(1, a.count);
            const double step = a.spacing > 0 ? a.spacing : closed ? L / n : (n > 1 ? L / (n - 1) : 0);
            const Vec2 p0 = CurvePoint(path, 0);
            const double t0 = AngleOf(CurveDirection(path, 0));
            for (int k = 0; k < n; ++k) {
                double s = step * k;
                if (s > L * (1 + 1e-9)) break;
                if (closed && L > 0) s = std::fmod(s, L);
                const Vec2 pk = CurvePoint(path, s);
                const double turn = AngleOf(CurveDirection(path, s)) - t0;
                place([&](Entity& x) {
                    Translate(x, pk - p0);
                    if (a.align) Rotate(x, pk, turn);
                });
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

size_t SegmentCount(const Entity& e) {
    if (e.type != EntityType::Polyline || e.verts.size() < 2) return 0;
    return e.closed ? e.verts.size() : e.verts.size() - 1;
}

void Primitives(const Entity& e, std::vector<Prim>& out) {
    switch (e.type) {
        case EntityType::Line:
            out.push_back(Prim::Seg(e.p0, e.p1));
            break;
        case EntityType::Circle:
            out.push_back(Prim::Circle(e.p0, e.radius));
            break;
        case EntityType::Arc: {
            double sweep = SweepCCW(e.startAngle, e.endAngle);
            if (sweep <= 0) sweep = kTau;
            out.push_back(Prim::ArcCCW(e.p0, e.radius, e.startAngle, sweep));
            break;
        }
        case EntityType::Polyline: {
            const size_t n = SegmentCount(e);
            for (size_t i = 0; i < n; ++i) {
                const Vertex& v = e.verts[i];
                const Vertex& w = e.verts[(i + 1) % e.verts.size()];
                out.push_back(ArcFromBulge(v.p, w.p, v.bulge));
            }
            if (e.verts.size() == 1) out.push_back(Prim::Seg(e.verts[0].p, e.verts[0].p));
            break;
        }
        case EntityType::Array: {
            std::vector<Entity> copies;
            ExpandArray(e, copies);
            for (const Entity& c : copies) Primitives(c, out);
            break;
        }
        case EntityType::Ellipse:
        case EntityType::Spline: {
            std::vector<Vec2> pts;
            if (e.type == EntityType::Ellipse) EllipsePoints(e, pts);
            else SplinePoints(e, pts);
            for (size_t i = 0; i + 1 < pts.size(); ++i) out.push_back(Prim::Seg(pts[i], pts[i + 1]));
            if (pts.size() == 1) out.push_back(Prim::Seg(pts[0], pts[0]));
            break;
        }
        case EntityType::Point:
            out.push_back(Prim::Seg(e.p0, e.p0));
            break;
    }
}

Box Bounds(const Entity& e) {
    Box box;
    if (e.type == EntityType::Circle) {
        box.Add(e.p0 - Vec2{e.radius, e.radius});
        box.Add(e.p0 + Vec2{e.radius, e.radius});
        return box;
    }
    if (e.type == EntityType::Line) return Box(e.p0, e.p1);
    std::vector<Prim> prims;
    Primitives(e, prims);
    for (const Prim& p : prims) box.Add(p.Bounds());
    return box;
}

double DistanceTo(const Entity& e, Vec2 p) {
    std::vector<Prim> prims;
    Primitives(e, prims);
    double best = std::numeric_limits<double>::infinity();
    for (const Prim& pr : prims) best = std::min(best, Distance(pr, p));
    return best;
}

bool InsideBox(const Entity& e, const Box& box) { return box.Contains(Bounds(e)); }

bool TouchesBox(const Entity& e, const Box& box) {
    if (!Bounds(e).Intersects(box)) return false;
    std::vector<Prim> prims;
    Primitives(e, prims);
    for (const Prim& p : prims)
        if (PrimTouchesBox(p, box)) return true;
    return false;
}

double LengthOf(const Entity& e) {
    std::vector<Prim> prims;
    Primitives(e, prims);
    double len = 0;
    for (const Prim& p : prims) len += p.LengthOf();
    return len;
}

// ---------------------------------------------------------------------------
// Transformations
// ---------------------------------------------------------------------------

void Translate(Entity& e, Vec2 d) {
    if (e.type == EntityType::Array) {
        EditArray(e, [&](ArrayData& a) {
            for (Entity& x : a.items) Translate(x, d);
            for (Entity& x : a.path) Translate(x, d);
            a.centre += d;
        });
        return;
    }
    e.p0 += d;
    if (e.type != EntityType::Ellipse) e.p1 += d;  // an ellipse's p1 is a direction
    for (Vertex& v : e.verts) v.p += d;
}

void Rotate(Entity& e, Vec2 centre, double angle) {
    if (e.type == EntityType::Array) {
        EditArray(e, [&](ArrayData& a) {
            for (Entity& x : a.items) Rotate(x, centre, angle);
            for (Entity& x : a.path) Rotate(x, centre, angle);
            a.centre = RotateAround(a.centre, centre, angle);
            a.columnStep = RotateAround(a.columnStep, {0, 0}, angle);
            a.rowStep = RotateAround(a.rowStep, {0, 0}, angle);
        });
        return;
    }
    if (e.type == EntityType::Ellipse) {
        // Parameters are measured from the major axis, which turns with it.
        e.p0 = RotateAround(e.p0, centre, angle);
        e.p1 = RotateAround(e.p1, {0, 0}, angle);
        return;
    }
    e.p0 = RotateAround(e.p0, centre, angle);
    e.p1 = RotateAround(e.p1, centre, angle);
    e.startAngle = NormalizeAngle(e.startAngle + angle);
    e.endAngle = NormalizeAngle(e.endAngle + angle);
    for (Vertex& v : e.verts) v.p = RotateAround(v.p, centre, angle);
}

void Scale(Entity& e, Vec2 base, double f) {
    auto sp = [&](Vec2 p) { return base + (p - base) * f; };
    if (e.type == EntityType::Array) {
        EditArray(e, [&](ArrayData& a) {
            for (Entity& x : a.items) Scale(x, base, f);
            for (Entity& x : a.path) Scale(x, base, f);
            a.centre = sp(a.centre);
            a.columnStep = a.columnStep * f;
            a.rowStep = a.rowStep * f;
            a.spacing *= f;
        });
        return;
    }
    if (e.type == EntityType::Ellipse) {
        e.p0 = sp(e.p0);
        e.p1 = e.p1 * f;  // the ratio stays
        return;
    }
    e.p0 = sp(e.p0);
    e.p1 = sp(e.p1);
    e.radius *= f;
    for (Vertex& v : e.verts) v.p = sp(v.p);
}

void Mirror(Entity& e, Vec2 a, Vec2 b) {
    const Vec2 u = Normalized(b - a);
    const double phi = AngleOf(u);
    if (e.type == EntityType::Array) {
        EditArray(e, [&](ArrayData& d) {
            for (Entity& x : d.items) Mirror(x, a, b);
            for (Entity& x : d.path) Mirror(x, a, b);
            d.centre = Reflect(d.centre, a, u);
            d.columnStep = ReflectVector(d.columnStep, u);
            d.rowStep = ReflectVector(d.rowStep, u);
            d.fillAngle = -d.fillAngle;
        });
        return;
    }
    if (e.type == EntityType::Ellipse) {
        // The mirror image of parameter t is -t on the mirrored axis.
        e.p0 = Reflect(e.p0, a, u);
        e.p1 = ReflectVector(e.p1, u);
        const double s = e.startAngle, t = e.endAngle;
        e.startAngle = NormalizeAngle(-t);
        e.endAngle = NormalizeAngle(-s);
        return;
    }
    e.p0 = Reflect(e.p0, a, u);
    e.p1 = Reflect(e.p1, a, u);
    if (e.type == EntityType::Arc) {
        // A mirror turns counter-clockwise into clockwise: swap the ends.
        const double s = e.startAngle, t = e.endAngle;
        e.startAngle = NormalizeAngle(2 * phi - t);
        e.endAngle = NormalizeAngle(2 * phi - s);
    }
    for (Vertex& v : e.verts) {
        v.p = Reflect(v.p, a, u);
        v.bulge = -v.bulge;
    }
}

namespace {
bool OnEllipse(const Entity& e, double t) { return e.closed || AngleInSweep(t, e.startAngle, EllipseSweep(e), 1e-12); }
}  // namespace

// ---------------------------------------------------------------------------
// Snap points
// ---------------------------------------------------------------------------

namespace {
template <class Fn>
bool ForArrayCopies(const Entity& e, std::vector<Vec2>& out, Fn fn) {
    if (e.type != EntityType::Array) return false;
    std::vector<Entity> copies;
    ExpandArray(e, copies);
    for (const Entity& c : copies) fn(c, out);
    return true;
}
}  // namespace

void EndPoints(const Entity& e, std::vector<Vec2>& out) {
    if (ForArrayCopies(e, out, EndPoints)) return;
    switch (e.type) {
        case EntityType::Line:
            out.push_back(e.p0);
            out.push_back(e.p1);
            break;
        case EntityType::Arc:
            out.push_back(Polar(e.p0, e.radius, e.startAngle));
            out.push_back(Polar(e.p0, e.radius, e.endAngle));
            break;
        case EntityType::Polyline:
            for (const Vertex& v : e.verts) out.push_back(v.p);
            break;
        case EntityType::Ellipse:
            if (!e.closed) {
                out.push_back(EllipsePoint(e.p0, e.p1, e.radius, e.startAngle));
                out.push_back(EllipsePoint(e.p0, e.p1, e.radius, e.endAngle));
            }
            break;
        case EntityType::Spline:
            if (!e.closed && !e.verts.empty()) {
                out.push_back(e.verts.front().p);
                out.push_back(e.verts.back().p);
            }
            break;
        case EntityType::Point:
            out.push_back(e.p0);
            break;
        default:
            break;
    }
}

void MidPoints(const Entity& e, std::vector<Vec2>& out) {
    if (ForArrayCopies(e, out, MidPoints)) return;
    if (e.type == EntityType::Circle || e.type == EntityType::Spline || e.type == EntityType::Point) return;
    if (e.type == EntityType::Ellipse) {
        if (!e.closed) out.push_back(EllipsePoint(e.p0, e.p1, e.radius, e.startAngle + EllipseSweep(e) / 2));
        return;
    }
    std::vector<Prim> prims;
    Primitives(e, prims);
    for (const Prim& p : prims) out.push_back(p.Mid());
}

void Centres(const Entity& e, std::vector<Vec2>& out) {
    if (ForArrayCopies(e, out, Centres)) return;
    if (e.type == EntityType::Circle || e.type == EntityType::Arc || e.type == EntityType::Ellipse) {
        out.push_back(e.p0);
        return;
    }
    if (e.type == EntityType::Polyline) {
        std::vector<Prim> prims;
        Primitives(e, prims);
        for (const Prim& p : prims)
            if (p.kind == Prim::Arc) out.push_back(p.c);
    }
}

void Quadrants(const Entity& e, std::vector<Vec2>& out) {
    if (ForArrayCopies(e, out, Quadrants)) return;
    if (e.type == EntityType::Ellipse) {
        for (int q = 0; q < 4; ++q)
            if (OnEllipse(e, q * kPi / 2)) out.push_back(EllipsePoint(e.p0, e.p1, e.radius, q * kPi / 2));
        return;
    }
    if (e.type == EntityType::Spline) return;
    std::vector<Prim> prims;
    Primitives(e, prims);
    for (const Prim& p : prims) {
        if (p.kind != Prim::Arc) continue;
        for (int q = 0; q < 4; ++q) {
            const double ang = q * kPi / 2;
            if (AngleInSweep(ang, p.start, p.sweep)) out.push_back(Polar(p.c, p.r, ang));
        }
    }
}
