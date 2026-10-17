// Geometry.cpp - see Geometry.h.
#include "Geometry.h"

#include <algorithm>

double NormalizeAngle(double a) {
    a = std::fmod(a, kTau);
    if (a < 0) a += kTau;
    if (a >= kTau) a -= kTau;
    return a;
}

double SweepCCW(double from, double to) { return NormalizeAngle(to - from); }

bool AngleInSweep(double a, double start, double sweep, double tol) {
    if (sweep >= kTau - 1e-12) return true;
    const double s = SweepCCW(start, a);
    return s <= sweep + tol || s >= kTau - tol;
}

Prim Prim::Seg(Vec2 a, Vec2 b) {
    Prim p;
    p.kind = Segment;
    p.a = a;
    p.b = b;
    return p;
}

Prim Prim::ArcCCW(Vec2 c, double r, double start, double sweep) {
    Prim p;
    p.kind = Arc;
    p.c = c;
    p.r = r;
    p.start = NormalizeAngle(start);
    p.sweep = std::min(sweep, kTau);
    p.a = Polar(c, r, p.start);
    p.b = Polar(c, r, p.start + p.sweep);
    if (p.FullCircle()) p.b = p.a;
    return p;
}

Vec2 Prim::Mid() const {
    if (kind == Segment) return Lerp(a, b, 0.5);
    return Polar(c, r, start + sweep / 2);
}

Box Prim::Bounds() const {
    Box box;
    if (kind == Segment) {
        box.Add(a);
        box.Add(b);
        return box;
    }
    box.Add(a);
    box.Add(b);
    for (int q = 0; q < 4; ++q) {
        const double ang = q * kPi / 2;
        if (AngleInSweep(ang, start, sweep)) box.Add(Polar(c, r, ang));
    }
    return box;
}

Prim ArcFromBulge(Vec2 p0, Vec2 p1, double bulge) {
    const double chord = Dist(p0, p1);
    if (std::fabs(bulge) < 1e-12 || chord < 1e-15) return Prim::Seg(p0, p1);
    const double theta = 4 * std::atan(bulge);  // signed included angle
    const Vec2 u = (p1 - p0) / chord;
    const Vec2 centre = Lerp(p0, p1, 0.5) + Perp(u) * ((chord / 2) / std::tan(theta / 2));
    const double r = Dist(centre, p0);
    if (theta > 0) return Prim::ArcCCW(centre, r, AngleOf(p0 - centre), theta);
    return Prim::ArcCCW(centre, r, AngleOf(p1 - centre), -theta);
}

double TangentBulge(Vec2 p0, Vec2 tangent, Vec2 p1) {
    const Vec2 chord = p1 - p0;
    if (Length(chord) < 1e-15 || Length(tangent) < 1e-15) return 0;
    double phi = std::atan2(Cross(tangent, chord), Dot(tangent, chord));
    // Going straight back would need an infinite circle: stop just short.
    const double limit = kPi - 1e-6;
    phi = std::clamp(phi, -limit, limit);
    return std::tan(phi / 2);
}

bool CircleFrom3Points(Vec2 p1, Vec2 p2, Vec2 p3, Vec2& centre, double& radius) {
    const double d = 2 * (p1.x * (p2.y - p3.y) + p2.x * (p3.y - p1.y) + p3.x * (p1.y - p2.y));
    const double scale = std::max({Dist(p1, p2), Dist(p2, p3), Dist(p1, p3)});
    if (scale <= 0 || std::fabs(d) < 1e-12 * scale * scale) return false;
    const double s1 = Dot(p1, p1), s2 = Dot(p2, p2), s3 = Dot(p3, p3);
    centre.x = (s1 * (p2.y - p3.y) + s2 * (p3.y - p1.y) + s3 * (p1.y - p2.y)) / d;
    centre.y = (s1 * (p3.x - p2.x) + s2 * (p1.x - p3.x) + s3 * (p2.x - p1.x)) / d;
    radius = Dist(centre, p1);
    return true;
}

Vec2 ClosestPoint(const Prim& p, Vec2 q) {
    if (p.kind == Prim::Segment) {
        const Vec2 d = p.b - p.a;
        const double len2 = Dot(d, d);
        if (len2 <= 0) return p.a;
        const double t = std::clamp(Dot(q - p.a, d) / len2, 0.0, 1.0);
        return p.a + d * t;
    }
    const Vec2 v = q - p.c;
    if (Length(v) < 1e-300) return p.a;
    const double ang = AngleOf(v);
    if (AngleInSweep(ang, p.start, p.sweep)) return Polar(p.c, p.r, ang);
    return Dist(q, p.a) <= Dist(q, p.b) ? p.a : p.b;
}

namespace {
// Parameters t along a->b where the infinite line meets the circle.
int LineCircle(Vec2 a, Vec2 b, Vec2 c, double r, double t[2]) {
    const Vec2 d = b - a, f = a - c;
    const double A = Dot(d, d);
    if (A <= 0) return 0;
    const double B = 2 * Dot(f, d), C = Dot(f, f) - r * r;
    double disc = B * B - 4 * A * C;
    // Treat a near-touch as a touch (tangent lines).
    if (disc < 0) {
        if (disc > -1e-9 * B * B - 1e-18) disc = 0;
        else return 0;
    }
    const double s = std::sqrt(disc);
    t[0] = (-B - s) / (2 * A);
    t[1] = (-B + s) / (2 * A);
    return s == 0 ? 1 : 2;
}

bool OnArc(const Prim& arc, Vec2 p) { return AngleInSweep(AngleOf(p - arc.c), arc.start, arc.sweep, 1e-9); }

void SegSeg(const Prim& p, const Prim& q, std::vector<Vec2>& out) {
    const Vec2 r = p.b - p.a, s = q.b - q.a;
    const double denom = Cross(r, s);
    const double scale = Length(r) * Length(s);
    if (scale <= 0 || std::fabs(denom) <= 1e-12 * scale) return;  // parallel
    const Vec2 qp = q.a - p.a;
    const double t = Cross(qp, s) / denom, u = Cross(qp, r) / denom;
    const double tol = 1e-9;
    if (t >= -tol && t <= 1 + tol && u >= -tol && u <= 1 + tol) out.push_back(p.a + r * t);
}

void SegArc(const Prim& seg, const Prim& arc, std::vector<Vec2>& out) {
    double t[2];
    const int n = LineCircle(seg.a, seg.b, arc.c, arc.r, t);
    const double tol = 1e-9;
    for (int i = 0; i < n; ++i) {
        if (t[i] < -tol || t[i] > 1 + tol) continue;
        const Vec2 pt = Lerp(seg.a, seg.b, t[i]);
        if (OnArc(arc, pt)) out.push_back(pt);
    }
}

void ArcArc(const Prim& p, const Prim& q, std::vector<Vec2>& out) {
    const double d = Dist(p.c, q.c);
    if (d < 1e-12) return;  // concentric
    if (d > p.r + q.r + 1e-9 || d < std::fabs(p.r - q.r) - 1e-9) return;
    const double a = (p.r * p.r - q.r * q.r + d * d) / (2 * d);
    const double h = std::sqrt(std::max(0.0, p.r * p.r - a * a));
    const Vec2 u = (q.c - p.c) / d;
    const Vec2 m = p.c + u * a;
    const Vec2 cand[2] = {m + Perp(u) * h, m - Perp(u) * h};
    const int n = h < 1e-12 ? 1 : 2;
    for (int i = 0; i < n; ++i)
        if (OnArc(p, cand[i]) && OnArc(q, cand[i])) out.push_back(cand[i]);
}
}  // namespace

void Intersect(const Prim& p, const Prim& q, std::vector<Vec2>& out) {
    if (!p.Bounds().Inflated(1e-9).Intersects(q.Bounds())) return;
    if (p.kind == Prim::Segment && q.kind == Prim::Segment)
        SegSeg(p, q, out);
    else if (p.kind == Prim::Segment)
        SegArc(p, q, out);
    else if (q.kind == Prim::Segment)
        SegArc(q, p, out);
    else
        ArcArc(p, q, out);
}

bool PrimTouchesBox(const Prim& p, const Box& box) {
    if (box.Empty() || !p.Bounds().Intersects(box)) return false;
    if (box.Contains(p.a) || box.Contains(p.b)) return true;
    if (p.kind == Prim::Segment) {
        // Liang-Barsky clipping.
        double t0 = 0, t1 = 1;
        const Vec2 d = p.b - p.a;
        const double pp[4] = {-d.x, d.x, -d.y, d.y};
        const double qq[4] = {p.a.x - box.minX, box.maxX - p.a.x, p.a.y - box.minY, box.maxY - p.a.y};
        for (int i = 0; i < 4; ++i) {
            if (pp[i] == 0) {
                if (qq[i] < 0) return false;
                continue;
            }
            const double t = qq[i] / pp[i];
            if (pp[i] < 0) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
            if (t0 > t1) return false;
        }
        return true;
    }
    const Vec2 c0{box.minX, box.minY}, c1{box.maxX, box.minY}, c2{box.maxX, box.maxY}, c3{box.minX, box.maxY};
    const Prim edges[4] = {Prim::Seg(c0, c1), Prim::Seg(c1, c2), Prim::Seg(c2, c3), Prim::Seg(c3, c0)};
    std::vector<Vec2> hits;
    for (const Prim& e : edges) {
        SegArc(e, p, hits);
        if (!hits.empty()) return true;
    }
    return false;
}

bool PerpendicularFoot(const Prim& p, Vec2 from, Vec2& foot) {
    if (p.kind == Prim::Segment) {
        const Vec2 d = p.b - p.a;
        const double len2 = Dot(d, d);
        if (len2 <= 0) return false;
        const double t = Dot(from - p.a, d) / len2;
        if (t < -1e-9 || t > 1 + 1e-9) return false;
        foot = p.a + d * t;
        return true;
    }
    const Vec2 v = from - p.c;
    if (Length(v) < 1e-12) return false;
    const double ang = AngleOf(v);
    if (AngleInSweep(ang, p.start, p.sweep)) {
        foot = Polar(p.c, p.r, ang);
        return true;
    }
    if (AngleInSweep(ang + kPi, p.start, p.sweep)) {
        foot = Polar(p.c, p.r, ang + kPi);
        return true;
    }
    return false;
}

void TangentPoints(const Prim& p, Vec2 from, std::vector<Vec2>& out) {
    if (p.kind != Prim::Arc) return;
    const double d = Dist(from, p.c);
    if (d <= p.r + 1e-12) return;
    const double alpha = std::acos(p.r / d), base = AngleOf(from - p.c);
    for (double ang : {base + alpha, base - alpha})
        if (AngleInSweep(ang, p.start, p.sweep)) out.push_back(Polar(p.c, p.r, ang));
}
