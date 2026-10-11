// Entity.cpp - see Entity.h.
#include "Entity.h"

#include <algorithm>

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

const wchar_t* TypeName(EntityType type) {
    switch (type) {
        case EntityType::Line: return L"Line";
        case EntityType::Circle: return L"Circle";
        case EntityType::Arc: return L"Arc";
        case EntityType::Polyline: return L"Polyline";
    }
    return L"Object";
}

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

void Translate(Entity& e, Vec2 d) {
    e.p0 += d;
    e.p1 += d;
    for (Vertex& v : e.verts) v.p += d;
}

void Rotate(Entity& e, Vec2 centre, double angle) {
    e.p0 = RotateAround(e.p0, centre, angle);
    e.p1 = RotateAround(e.p1, centre, angle);
    e.startAngle = NormalizeAngle(e.startAngle + angle);
    e.endAngle = NormalizeAngle(e.endAngle + angle);
    for (Vertex& v : e.verts) v.p = RotateAround(v.p, centre, angle);
}

void EndPoints(const Entity& e, std::vector<Vec2>& out) {
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
        case EntityType::Circle:
            break;
    }
}

void MidPoints(const Entity& e, std::vector<Vec2>& out) {
    if (e.type == EntityType::Circle) return;
    std::vector<Prim> prims;
    Primitives(e, prims);
    for (const Prim& p : prims) out.push_back(p.Mid());
}

void Centres(const Entity& e, std::vector<Vec2>& out) {
    if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
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
