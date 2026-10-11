// Curve.cpp - see Curve.h.
#include "Curve.h"

#include <algorithm>

namespace {
double Eps(double length) { return 1e-9 * std::max(1.0, length); }
}  // namespace

Vec2 CurvePiece::At(double s) const {
    const double t = len > 0 ? std::clamp(s / len, 0.0, 1.0) : 0.0;
    if (!arc) return Lerp(a, b, t);
    if (t >= 1) return b;
    if (t <= 0) return a;
    return Polar(c, r, a0 + sweep * t);
}

Vec2 CurvePiece::Direction(double s) const {
    if (!arc) return Normalized(b - a);
    const double t = len > 0 ? std::clamp(s / len, 0.0, 1.0) : 0.0;
    const double ang = a0 + sweep * t;
    const Vec2 ccw = {-std::sin(ang), std::cos(ang)};
    return sweep >= 0 ? ccw : -ccw;
}

double CurvePiece::Param(Vec2 p) const {
    if (!arc) {
        const Vec2 d = b - a;
        const double l2 = Dot(d, d);
        if (l2 <= 0) return 0;
        return std::clamp(Dot(p - a, d) / l2, 0.0, 1.0) * len;
    }
    if (Length(p - c) < 1e-300) return 0;
    const double ang = AngleOf(p - c);
    const double span = std::fabs(sweep);
    const double d = sweep >= 0 ? SweepCCW(a0, ang) : SweepCCW(ang, a0);
    if (span >= kTau - 1e-12 || d <= span) return d * r;
    // Off the arc: the nearer end.
    return (d - span) < (kTau - d) ? len : 0;
}

Prim CurvePiece::ToPrim() const {
    if (!arc) return Prim::Seg(a, b);
    if (sweep >= 0) return Prim::ArcCCW(c, r, a0, sweep);
    return Prim::ArcCCW(c, r, a0 + sweep, -sweep);
}

bool IsCurve(const Entity& e) {
    return e.type == EntityType::Line || e.type == EntityType::Arc || e.type == EntityType::Circle ||
           e.type == EntityType::Ellipse || (e.type == EntityType::Polyline && e.verts.size() >= 2) ||
           (e.type == EntityType::Spline && e.verts.size() >= 2);
}

bool IsClosedCurve(const Entity& e) {
    return e.type == EntityType::Circle || (e.type == EntityType::Ellipse && e.closed) ||
           ((e.type == EntityType::Polyline || e.type == EntityType::Spline) && e.closed && e.verts.size() >= 3);
}

std::vector<CurvePiece> CurvePieces(const Entity& e) {
    std::vector<CurvePiece> out;
    auto line = [&](Vec2 a, Vec2 b) {
        CurvePiece p;
        p.a = a;
        p.b = b;
        p.len = Dist(a, b);
        out.push_back(p);
    };
    auto arc = [&](Vec2 c, double r, double a0, double sweep) {
        CurvePiece p;
        p.arc = true;
        p.c = c;
        p.r = r;
        p.a0 = a0;
        p.sweep = sweep;
        p.a = Polar(c, r, a0);
        p.b = std::fabs(sweep) >= kTau - 1e-12 ? p.a : Polar(c, r, a0 + sweep);
        p.len = r * std::fabs(sweep);
        out.push_back(p);
    };
    switch (e.type) {
        case EntityType::Line:
            line(e.p0, e.p1);
            break;
        case EntityType::Arc: {
            double sweep = SweepCCW(e.startAngle, e.endAngle);
            if (sweep <= 0) sweep = kTau;
            arc(e.p0, e.radius, e.startAngle, sweep);
            break;
        }
        case EntityType::Circle:
            arc(e.p0, e.radius, 0, kTau);
            break;
        case EntityType::Polyline: {
            const size_t n = SegmentCount(e);
            for (size_t i = 0; i < n; ++i) {
                const Vertex& v = e.verts[i];
                const Vertex& w = e.verts[(i + 1) % e.verts.size()];
                if (v.bulge == 0 || Near(v.p, w.p, 1e-15)) {
                    line(v.p, w.p);
                    continue;
                }
                const Prim pr = ArcFromBulge(v.p, w.p, v.bulge);
                arc(pr.c, pr.r, AngleOf(v.p - pr.c), 4 * std::atan(v.bulge));
                out.back().a = v.p;  // exactly the vertices
                out.back().b = w.p;
            }
            break;
        }
        case EntityType::Ellipse:
        case EntityType::Spline: {
            std::vector<Vec2> pts;
            if (e.type == EntityType::Ellipse) EllipsePoints(e, pts);
            else SplinePoints(e, pts);
            for (size_t i = 0; i + 1 < pts.size(); ++i) line(pts[i], pts[i + 1]);
            break;
        }
        case EntityType::Array:
        case EntityType::Point:
            break;
    }
    return out;
}

double CurveLength(const Entity& e) {
    double len = 0;
    for (const CurvePiece& p : CurvePieces(e)) len += p.len;
    return len;
}

Vec2 CurvePoint(const Entity& e, double s) {
    const std::vector<CurvePiece> pieces = CurvePieces(e);
    if (pieces.empty()) return {};
    for (const CurvePiece& p : pieces) {
        if (s <= p.len) return p.At(std::max(0.0, s));
        s -= p.len;
    }
    return pieces.back().b;
}

Vec2 CurveDirection(const Entity& e, double s) {
    const std::vector<CurvePiece> pieces = CurvePieces(e);
    if (pieces.empty()) return {1, 0};
    for (const CurvePiece& p : pieces) {
        if (s <= p.len) return p.Direction(std::max(0.0, s));
        s -= p.len;
    }
    return pieces.back().Direction(pieces.back().len);
}

double CurveParam(const Entity& e, Vec2 p) {
    double best = std::numeric_limits<double>::infinity(), param = 0, offset = 0;
    for (const CurvePiece& piece : CurvePieces(e)) {
        const double s = piece.Param(p);
        const double d = Dist(piece.At(s), p);
        if (d < best) {
            best = d;
            param = offset + s;
        }
        offset += piece.len;
    }
    return param;
}

bool SubCurve(const Entity& e, double s0, double s1, Entity& out) {
    const double L = CurveLength(e);
    const double eps = Eps(L);
    s0 = std::clamp(s0, 0.0, L);
    s1 = std::clamp(s1, 0.0, L);
    const bool closed = IsClosedCurve(e);
    const bool wraps = closed && s0 > s1;
    const double partLen = wraps ? (L - s0) + s1 : s1 - s0;
    if (partLen <= eps) return false;
    out = e;
    out.id = e.id;
    switch (e.type) {
        case EntityType::Line:
            out.p0 = CurvePoint(e, s0);
            out.p1 = CurvePoint(e, s1);
            return true;
        case EntityType::Arc: {
            const double a0 = e.startAngle;
            out.startAngle = NormalizeAngle(a0 + s0 / e.radius);
            out.endAngle = NormalizeAngle(a0 + s1 / e.radius);
            return true;
        }
        case EntityType::Circle:
            out = Entity::Arc(e.p0, e.radius, s0 / e.radius, s1 / e.radius);
            out.id = e.id;
            return true;
        case EntityType::Polyline: {
            std::vector<std::pair<double, double>> ranges;
            if (wraps) ranges = {{s0, L}, {0, s1}};
            else ranges = {{s0, s1}};
            const std::vector<CurvePiece> pieces = CurvePieces(e);
            std::vector<Vertex> verts{{CurvePoint(e, s0), 0}};
            for (const auto& range : ranges) {
                double offset = 0;
                for (const CurvePiece& p : pieces) {
                    const double u0 = std::max(range.first, offset), u1 = std::min(range.second, offset + p.len);
                    offset += p.len;
                    if (u1 - u0 <= eps) continue;
                    const double l0 = u0 - (offset - p.len), l1 = u1 - (offset - p.len);
                    verts.back().bulge = p.arc ? std::tan(p.sweep * (l1 - l0) / p.len / 4) : 0.0;
                    verts.push_back({p.At(l1), 0});
                }
            }
            if (verts.size() < 2) return false;
            out.verts = std::move(verts);
            out.closed = false;
            return true;
        }
        case EntityType::Ellipse: {
            // Distances along the curve back to ellipse parameters.
            std::vector<Vec2> pts;
            std::vector<double> params;
            EllipsePoints(e, pts, &params);
            auto paramAt = [&](double s) {
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    const double len = Dist(pts[i], pts[i + 1]);
                    if (s <= len || i + 2 == pts.size()) {
                        const double f = len > 0 ? std::clamp(s / len, 0.0, 1.0) : 0.0;
                        return params[i] + (params[i + 1] - params[i]) * f;
                    }
                    s -= len;
                }
                return params.back();
            };
            out = Entity::EllipseArc(e.p0, e.p1, e.radius, paramAt(s0), paramAt(s1));
            out.id = e.id;
            return true;
        }
        case EntityType::Array:
        case EntityType::Spline:  // a part of a spline is not kept as a spline
        case EntityType::Point:
            return false;
    }
    return false;
}

std::vector<double> CrossingParams(const Entity& e, const std::vector<Prim>& others) {
    std::vector<double> params;
    std::vector<Vec2> pts;
    double offset = 0;
    for (const CurvePiece& piece : CurvePieces(e)) {
        const Prim prim = piece.ToPrim();
        for (const Prim& o : others) {
            pts.clear();
            Intersect(prim, o, pts);
            for (Vec2 p : pts) params.push_back(offset + piece.Param(p));
        }
        offset += piece.len;
    }
    std::sort(params.begin(), params.end());
    const double eps = Eps(offset) * 100;
    params.erase(std::unique(params.begin(), params.end(), [&](double x, double y) { return std::fabs(x - y) <= eps; }),
                 params.end());
    return params;
}

bool ChangeEnd(Entity& e, bool atEnd, double delta) {
    const double eps = 1e-9;
    switch (e.type) {
        case EntityType::Line: {
            const double len = Dist(e.p0, e.p1);
            if (len <= 0 || len + delta <= eps) return false;
            const Vec2 dir = (e.p1 - e.p0) / len;
            if (atEnd) e.p1 = e.p0 + dir * (len + delta);
            else e.p0 = e.p1 - dir * (len + delta);
            return true;
        }
        case EntityType::Arc: {
            double sweep = SweepCCW(e.startAngle, e.endAngle);
            if (sweep <= 0) sweep = kTau;
            const double d = delta / e.radius;
            if (sweep + d <= eps || sweep + d >= kTau - eps) return false;
            if (atEnd) e.endAngle = NormalizeAngle(e.endAngle + d);
            else e.startAngle = NormalizeAngle(e.startAngle - d);
            return true;
        }
        case EntityType::Polyline: {
            if (e.closed || e.verts.size() < 2) return false;
            const size_t n = e.verts.size();
            Vertex& v = atEnd ? e.verts[n - 2] : e.verts[0];
            Vertex& w = atEnd ? e.verts[n - 1] : e.verts[1];
            if (v.bulge == 0) {
                Vertex& moving = atEnd ? w : v;
                const Vertex& fixed = atEnd ? v : w;
                const double len = Dist(fixed.p, moving.p);
                if (len <= 0 || len + delta <= eps) return false;
                moving.p = fixed.p + (moving.p - fixed.p) / len * (len + delta);
                return true;
            }
            const Prim pr = ArcFromBulge(v.p, w.p, v.bulge);
            const double theta = 4 * std::atan(v.bulge);
            const double sign = theta > 0 ? 1 : -1;
            const double nt = theta + sign * delta / pr.r;
            if (std::fabs(nt) <= eps || std::fabs(nt) >= kTau - eps || nt * theta <= 0) return false;
            if (atEnd) w.p = Polar(pr.c, pr.r, AngleOf(v.p - pr.c) + nt);
            else v.p = Polar(pr.c, pr.r, AngleOf(w.p - pr.c) - nt);
            v.bulge = std::tan(nt / 4);
            return true;
        }
        default:
            return false;
    }
}
