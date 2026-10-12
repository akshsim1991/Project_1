// Grips.cpp - see Grips.h.
#include "Grips.h"

#include "Annot.h"
#include "Dimension.h"
#include "Spline.h"

namespace {
// The arc from a through m to b (false if the three are in a line).
bool ArcThrough(Vec2 a, Vec2 m, Vec2 b, Entity& out) {
    Vec2 c;
    double r;
    if (!CircleFrom3Points(a, m, b, c, r)) return false;
    const uint64_t id = out.id;
    out = Cross(m - a, b - a) > 0 ? Entity::Arc(c, r, AngleOf(a - c), AngleOf(b - c))
                                  : Entity::Arc(c, r, AngleOf(b - c), AngleOf(a - c));
    out.id = id;
    return true;
}

// The bulge of the polyline arc from a through m to b.
double BulgeThrough(Vec2 a, Vec2 m, Vec2 b) {
    Vec2 c;
    double r;
    if (!CircleFrom3Points(a, m, b, c, r)) return 0;
    const double aa = AngleOf(a - c), ab = AngleOf(b - c);
    const double theta = Cross(m - a, b - a) > 0 ? SweepCCW(aa, ab) : -SweepCCW(ab, aa);
    return std::tan(theta / 4);
}

double ArcSweep(const Entity& e) {
    const double s = SweepCCW(e.startAngle, e.endAngle);
    return s <= 0 ? kTau : s;
}
}  // namespace

void GripsOf(const Entity& e, std::vector<Grip>& out) {
    auto add = [&](int index, Vec2 p) { out.push_back({e.id, index, p}); };
    switch (e.type) {
        case EntityType::Line:
            add(0, e.p0);
            add(1, e.p1);
            add(2, Lerp(e.p0, e.p1, 0.5));
            break;
        case EntityType::Circle:
            add(0, e.p0);
            for (int q = 0; q < 4; ++q) add(1 + q, Polar(e.p0, e.radius, q * kPi / 2));
            break;
        case EntityType::Arc:
            add(0, e.p0);
            add(1, Polar(e.p0, e.radius, e.startAngle));
            add(2, Polar(e.p0, e.radius, e.endAngle));
            add(3, Polar(e.p0, e.radius, e.startAngle + ArcSweep(e) / 2));
            break;
        case EntityType::Polyline: {
            const int n = (int)e.verts.size();
            for (int i = 0; i < n; ++i) add(i, e.verts[(size_t)i].p);
            std::vector<Prim> prims;
            Primitives(e, prims);
            for (size_t i = 0; i < prims.size() && n >= 2; ++i) add(n + (int)i, prims[i].Mid());
            break;
        }
        case EntityType::Ellipse: {
            add(0, e.p0);
            const Vec2 b = Perp(e.p1) * e.radius;
            add(1, e.p0 + e.p1);
            add(2, e.p0 - e.p1);
            add(3, e.p0 + b);
            add(4, e.p0 - b);
            break;
        }
        case EntityType::Spline:
            for (size_t i = 0; i < e.verts.size(); ++i) add((int)i, e.verts[i].p);
            break;
        case EntityType::Point:
            add(0, e.p0);
            break;
        case EntityType::Array:
            add(0, Bounds(e).Center());
            break;
        case EntityType::Dimension: {
            std::vector<std::pair<int, Vec2>> g;
            DimGrips(e, g);
            for (const auto& x : g) add(x.first, x.second);
            break;
        }
        case EntityType::Text:
        case EntityType::Leader:
        case EntityType::Hatch:
        case EntityType::Insert:
        case EntityType::Relation:
            break;
        case EntityType::Viewport: {
            std::vector<std::pair<int, Vec2>> g;
            AnnotGrips(e, g);
            for (const auto& x : g) add(x.first, x.second);
            break;
        }
    }
}

bool DragGrip(Entity& e, int index, Vec2 from, Vec2 to) {
    const Vec2 d = to - from;
    switch (e.type) {
        case EntityType::Line:
            if (index == 0) e.p0 = to;
            else if (index == 1) e.p1 = to;
            else Translate(e, d);
            return !Near(e.p0, e.p1, 1e-12);
        case EntityType::Circle:
            if (index == 0) {
                Translate(e, d);
                return true;
            }
            if (Dist(e.p0, to) <= 1e-12) return false;
            e.radius = Dist(e.p0, to);
            return true;
        case EntityType::Arc: {
            if (index == 0) {
                Translate(e, d);
                return true;
            }
            const Vec2 s = Polar(e.p0, e.radius, e.startAngle), t = Polar(e.p0, e.radius, e.endAngle);
            const Vec2 m = Polar(e.p0, e.radius, e.startAngle + ArcSweep(e) / 2);
            // Ends keep the arc through its middle; the middle bends it.
            if (index == 1) return ArcThrough(to, m, t, e);
            if (index == 2) return ArcThrough(s, m, to, e);
            return ArcThrough(s, to, t, e);
        }
        case EntityType::Polyline: {
            const int n = (int)e.verts.size();
            if (index < n) {
                e.verts[(size_t)index].p = to;
                return true;
            }
            const size_t seg = (size_t)(index - n);
            Vertex& v = e.verts[seg];
            Vertex& w = e.verts[(seg + 1) % e.verts.size()];
            if (v.bulge == 0) {  // a straight segment moves with its midpoint
                v.p += d;
                w.p += d;
            } else {             // an arc segment bends through it
                v.bulge = BulgeThrough(v.p, to, w.p);
            }
            return true;
        }
        case EntityType::Ellipse: {
            if (index == 0) {
                Translate(e, d);
                return true;
            }
            const double minor = Length(e.p1) * e.radius;
            if (index == 1 || index == 2) {
                const Vec2 axis = index == 1 ? to - e.p0 : e.p0 - to;
                if (Length(axis) <= 1e-12) return false;
                e.p1 = axis;
                e.radius = minor / Length(axis);
                return true;
            }
            const double len = Dist(e.p0, to);
            if (len <= 1e-12) return false;
            e.radius = len / Length(e.p1);
            return true;
        }
        case EntityType::Spline:
            if (index >= 0 && (size_t)index < e.verts.size()) e.verts[(size_t)index].p = to;
            return true;
        case EntityType::Point:
        case EntityType::Array:
            Translate(e, d);
            return true;
        case EntityType::Dimension:
            return DragDimGrip(e, index, from, to);
        case EntityType::Text:
        case EntityType::Leader:
        case EntityType::Hatch:
        case EntityType::Insert:
        case EntityType::Viewport:
            return DragAnnotGrip(e, index, from, to);
        case EntityType::Relation:
            return false;
    }
    return false;
}
