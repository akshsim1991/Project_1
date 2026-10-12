// FreeSketch.cpp - freehand strokes into lines, arcs, circles and polylines.
#include "FreeSketch.h"

#include <algorithm>
#include <cmath>

namespace {
// One tidy piece of a stroke: a straight line or an arc from a to b.
struct Piece {
    bool arc = false;
    Vec2 a, b;
    Vec2 c;            // arc centre
    double r = 0;
    double sweep = 0;  // signed: + counter-clockwise
};

double DistToLine(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double len = Length(d);
    if (len < 1e-12) return Dist(p, a);
    return std::fabs(Cross(d, p - a)) / len;
}

// Least-squares circle through the points (Kasa's method, about their mean).
bool FitCircle(const std::vector<Vec2>& p, size_t i, size_t j, Vec2& c, double& r) {
    const size_t n = j - i + 1;
    if (n < 3) return false;
    Vec2 m{0, 0};
    for (size_t k = i; k <= j; ++k) m = m + p[k];
    m = m * (1.0 / (double)n);
    double suu = 0, suv = 0, svv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (size_t k = i; k <= j; ++k) {
        const double u = p[k].x - m.x, v = p[k].y - m.y;
        suu += u * u, suv += u * v, svv += v * v;
        suuu += u * u * u, svvv += v * v * v, suvv += u * v * v, svuu += v * u * u;
    }
    const double det = suu * svv - suv * suv;
    if (std::fabs(det) < 1e-12 * (suu * svv + 1e-30)) return false;  // in a straight line
    const double bu = (suuu + suvv) / 2, bv = (svvv + svuu) / 2;
    const double uc = (bu * svv - bv * suv) / det, vc = (suu * bv - suv * bu) / det;
    c = {m.x + uc, m.y + vc};
    r = std::sqrt(uc * uc + vc * vc + (suu + svv) / (double)n);
    return std::isfinite(r) && r > 0;
}

// The angle the points turn through about c (signed), and whether they go
// round steadily one way.
double TurnAbout(const std::vector<Vec2>& p, size_t i, size_t j, Vec2 c, bool& steady) {
    double total = 0, absTotal = 0;
    double prev = AngleOf(p[i] - c);
    for (size_t k = i + 1; k <= j; ++k) {
        const double a = AngleOf(p[k] - c);
        double d = a - prev;
        while (d > kPi) d -= kTau;
        while (d < -kPi) d += kTau;
        total += d, absTotal += std::fabs(d);
        prev = a;
    }
    steady = absTotal < 1e-12 || std::fabs(total) >= 0.85 * absTotal;
    return total;
}

void Split(const std::vector<Vec2>& p, size_t i, size_t j, double tol, int depth, std::vector<Piece>& out) {
    // Straight?
    size_t far = i;
    double worst = 0;
    for (size_t k = i + 1; k < j; ++k) {
        const double d = DistToLine(p[k], p[i], p[j]);
        if (d > worst) worst = d, far = k;
    }
    const double len = Dist(p[i], p[j]);
    if (j - i < 2 || worst <= std::max(tol, 0.03 * len) || depth > 12) {
        out.push_back({false, p[i], p[j], {}, 0, 0});
        return;
    }
    // A smooth bend: an arc?
    Vec2 c;
    double r;
    if (j - i >= 4 && FitCircle(p, i, j, c, r)) {
        double dev = 0;
        for (size_t k = i; k <= j; ++k) dev = std::max(dev, std::fabs(Dist(p[k], c) - r));
        bool steady;
        const double sweep = TurnAbout(p, i, j, c, steady);
        if (steady && dev <= std::min(std::max(tol, 0.06 * r), 3 * tol) && std::fabs(sweep) < kTau * 0.95 && r < 100 * len) {
            out.push_back({true, p[i], p[j], c, r, sweep});
            return;
        }
    }
    // Otherwise a corner where it strays furthest from straight.
    if (far == i) far = (i + j) / 2;
    Split(p, i, far, tol, depth + 1, out);
    Split(p, far, j, tol, depth + 1, out);
}

double Turn(const Piece& a, const Piece& b) {
    const Vec2 u = a.b - a.a, v = b.b - b.a;
    return std::fabs(std::atan2(Cross(u, v), Dot(u, v)));
}

// Pieces that carry straight on (lines) or round the same circle (arcs) become one.
void Merge(std::vector<Piece>& ps, double straighten, double tol) {
    for (size_t k = 0; k + 1 < ps.size();) {
        Piece &a = ps[k], &b = ps[k + 1];
        bool join = false;
        if (!a.arc && !b.arc) join = Turn(a, b) < straighten * 1.5;
        else if (a.arc && b.arc) join = Dist(a.c, b.c) < 2 * tol && std::fabs(a.r - b.r) < 2 * tol && a.sweep * b.sweep > 0;
        if (join) {
            a.b = b.b;
            a.sweep += b.sweep;
            ps.erase(ps.begin() + (long)k + 1);
        } else {
            ++k;
        }
    }
}

// Nearly level or upright lines made exactly so (moving their far ends).
double Snapped(double angle, double tol, bool& changed) {
    const double q = kPi / 2;
    const double n = std::round(angle / q) * q;
    changed = std::fabs(angle - n) < tol && std::fabs(angle - n) > 0;
    return changed ? n : angle;
}
}  // namespace

std::vector<Entity> RecognizeStroke(const std::vector<Vec2>& strokeIn, const StrokeOptions& opt) {
    std::vector<Entity> out;
    const double tol = std::max(opt.tol, 1e-9);
    // Points closer together than a fraction of the tolerance add nothing.
    std::vector<Vec2> p;
    for (Vec2 q : strokeIn)
        if (p.empty() || Dist(p.back(), q) > tol * 0.2) p.push_back(q);
    if (p.size() < 2) return out;
    double length = 0;
    Box box;
    for (size_t k = 0; k < p.size(); ++k) {
        if (k) length += Dist(p[k - 1], p[k]);
        box.Add(p[k]);
    }
    if (length < 2 * tol) return out;
    const double size = std::max(box.Width(), box.Height());

    // Closed: ends close together compared with the stroke.
    const bool closed = p.size() >= 8 && Dist(p.front(), p.back()) <= std::max(3 * tol, 0.15 * length) && length > 8 * tol;
    if (closed) {
        Vec2 c;
        double r;
        if (FitCircle(p, 0, p.size() - 1, c, r)) {
            double dev = 0;
            for (Vec2 q : p) dev = std::max(dev, std::fabs(Dist(q, c) - r));
            bool steady;
            const double sweep = std::fabs(TurnAbout(p, 0, p.size() - 1, c, steady));
            if (steady && dev <= std::max(tol, 0.12 * r) && sweep > kTau * 0.8) {
                out.push_back(Entity::Circle(c, r));
                return out;
            }
        }
        p.back() = p.front();
    }

    std::vector<Piece> ps;
    Split(p, 0, p.size() - 1, std::max(tol, 0.01 * size), 0, ps);
    Merge(ps, opt.straighten, tol);
    if (closed && ps.size() > 2 && !ps.front().arc && !ps.back().arc && Turn(ps.back(), ps.front()) < opt.straighten * 1.5) {
        // The stroke started part way along a side: that side is one piece.
        ps.front().a = ps.back().a;
        ps.pop_back();
    }
    if (ps.empty()) return out;

    // Level and upright lines, in order (each piece starts where the last ends).
    for (size_t k = 0; k < ps.size(); ++k) {
        Piece& s = ps[k];
        if (s.arc) continue;
        const Vec2 d = s.b - s.a;
        bool changed;
        const double ang = Snapped(AngleOf(d), opt.straighten, changed);
        if (!changed) continue;
        const bool level = std::fabs(std::sin(ang)) < 0.5;
        const bool closing = closed && k + 1 == ps.size() && ps.size() > 1;
        if (closing) {  // the end is fixed (the first point): move the start
            if (level) s.a.y = s.b.y;
            else s.a.x = s.b.x;
            if (k > 0) ps[k - 1].b = s.a;
        } else {
            if (level) s.b.y = s.a.y;
            else s.b.x = s.a.x;
            if (k + 1 < ps.size()) ps[k + 1].a = s.b;
            else if (closed) ps[0].a = s.b;
        }
    }

    if (ps.size() == 1 && !closed) {
        const Piece& s = ps[0];
        if (!s.arc) {
            out.push_back(Entity::Line(s.a, s.b));
        } else {
            // Ends on the circle; counter-clockwise from start to end.
            double a0 = AngleOf(s.a - s.c), a1 = AngleOf(s.b - s.c);
            if (s.sweep < 0) std::swap(a0, a1);
            out.push_back(Entity::Arc(s.c, s.r, NormalizeAngle(a0), NormalizeAngle(a1)));
        }
        return out;
    }
    // A polyline of lines and arcs (bulges).
    Entity pl;
    pl.type = EntityType::Polyline;
    pl.closed = closed;
    for (size_t k = 0; k < ps.size(); ++k) {
        Vertex v;
        v.p = ps[k].a;
        v.bulge = ps[k].arc ? std::tan(ps[k].sweep / 4) : 0;
        pl.verts.push_back(v);
    }
    if (!closed) pl.verts.push_back({ps.back().b, 0});
    out.push_back(pl);
    return out;
}
