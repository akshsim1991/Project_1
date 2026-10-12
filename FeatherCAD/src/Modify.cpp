// Modify.cpp - see Modify.h.
#include "Modify.h"

#include <algorithm>

#include "Annot.h"
#include "Dimension.h"

namespace {
double Eps(double scale) { return 1e-9 * std::max(1.0, scale); }

// An infinite line or a full circle that a curve piece lies on.
struct Carrier {
    bool circle = false;
    Vec2 p, d;  // line: a point and unit direction
    Vec2 c;     // circle
    double r = 0;
};

Carrier CarrierOf(const CurvePiece& piece) {
    Carrier k;
    if (piece.arc) {
        k.circle = true;
        k.c = piece.c;
        k.r = piece.r;
    } else {
        k.p = piece.a;
        k.d = Normalized(piece.b - piece.a);
    }
    return k;
}

// The carrier moved by `off` (lines: to the left; circles: bigger radius).
bool OffsetCarrier(const Carrier& k, double off, Carrier& out) {
    out = k;
    if (k.circle) {
        out.r = k.r + off;
        return out.r > 1e-12;
    }
    out.p = k.p + Perp(k.d) * off;
    return true;
}

std::vector<Vec2> CarrierIntersections(const Carrier& a, const Carrier& b) {
    std::vector<Vec2> out;
    if (!a.circle && !b.circle) {
        const double den = Cross(a.d, b.d);
        if (std::fabs(den) < 1e-12) return out;
        const double t = Cross(b.p - a.p, b.d) / den;
        out.push_back(a.p + a.d * t);
        return out;
    }
    if (a.circle && b.circle) {
        const double d = Dist(a.c, b.c);
        if (d < 1e-12 || d > a.r + b.r + 1e-9 || d < std::fabs(a.r - b.r) - 1e-9) return out;
        const double x = (a.r * a.r - b.r * b.r + d * d) / (2 * d);
        const double h = std::sqrt(std::max(0.0, a.r * a.r - x * x));
        const Vec2 u = (b.c - a.c) / d;
        const Vec2 m = a.c + u * x;
        out.push_back(m + Perp(u) * h);
        if (h > 1e-12) out.push_back(m - Perp(u) * h);
        return out;
    }
    const Carrier& ln = a.circle ? b : a;
    const Carrier& ci = a.circle ? a : b;
    const Vec2 f = ln.p - ci.c;
    const double B = Dot(f, ln.d), C = Dot(f, f) - ci.r * ci.r;
    double disc = B * B - C;
    if (disc < -1e-9 * std::max(1.0, ci.r * ci.r)) return out;
    disc = std::max(0.0, disc);
    const double s = std::sqrt(disc);
    out.push_back(ln.p + ln.d * (-B - s));
    if (s > 1e-12) out.push_back(ln.p + ln.d * (-B + s));
    return out;
}

Vec2 Foot(const Carrier& k, Vec2 p) {
    if (k.circle) return k.c + Normalized(p - k.c) * k.r;
    return k.p + k.d * Dot(p - k.p, k.d);
}

bool PieceNear(const Entity& e, Vec2 pick, CurvePiece& piece, size_t* index = nullptr) {
    const std::vector<CurvePiece> pieces = CurvePieces(e);
    double best = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < pieces.size(); ++i) {
        const double d = Dist(pieces[i].At(pieces[i].Param(pick)), pick);
        if (d < best) {
            best = d;
            piece = pieces[i];
            if (index) *index = i;
        }
    }
    return !pieces.empty();
}

// A line cut back (or extended) to t, keeping the end on the side of `pick`.
bool TrimLineTo(const Entity& line, Vec2 t, Vec2 pick, Entity& out) {
    const Vec2 dir = line.p1 - line.p0;
    const double tp = Dot(pick - t, dir);
    // Ends on the pick's side of t; the farther one if both are (extending).
    const double s0 = Dot(line.p0 - t, dir), s1 = Dot(line.p1 - t, dir);
    Vec2 keep;
    if (tp >= 0) {
        if (s0 <= 0 && s1 <= 0) return false;
        keep = s1 >= s0 ? line.p1 : line.p0;
    } else {
        if (s0 >= 0 && s1 >= 0) return false;
        keep = s1 <= s0 ? line.p1 : line.p0;
    }
    if (Dist(keep, t) < 1e-12) return false;
    out = line;
    out.p0 = keep;
    out.p1 = t;
    if (keep.x == line.p1.x && keep.y == line.p1.y) std::swap(out.p0, out.p1);  // keep the direction
    return true;
}

// An arc cut back (or extended) to the point t, keeping the side with the pick.
bool TrimArcTo(const Entity& arc, Vec2 t, Vec2 pick, Entity& out) {
    const double at = AngleOf(t - arc.p0), ap = AngleOf(pick - arc.p0);
    const Entity keepStart = Entity::Arc(arc.p0, arc.radius, arc.startAngle, at);  // start .. t
    const Entity keepEnd = Entity::Arc(arc.p0, arc.radius, at, arc.endAngle);      // t .. end
    auto sweep = [](const Entity& a) {
        const double s = SweepCCW(a.startAngle, a.endAngle);
        return s <= 0 ? kTau : s;
    };
    const bool inStart = AngleInSweep(ap, keepStart.startAngle, sweep(keepStart), 1e-12);
    const bool inEnd = AngleInSweep(ap, keepEnd.startAngle, sweep(keepEnd), 1e-12);
    if (!inStart && !inEnd) return false;
    if (inStart && (!inEnd || sweep(keepStart) <= sweep(keepEnd))) out = keepStart;
    else out = keepEnd;
    if (sweep(out) < 1e-12 || sweep(out) > kTau - 1e-12) return false;
    out.id = arc.id;
    return true;
}

bool TrimTo(const Entity& e, Vec2 t, Vec2 pick, Entity& out) {
    if (e.type == EntityType::Line) return TrimLineTo(e, t, pick, out);
    if (e.type == EntityType::Arc) return TrimArcTo(e, t, pick, out);
    out = e;  // circles are kept whole
    return true;
}

Entity ArcBetween(Vec2 c, double r, Vec2 t1, Vec2 t2) {
    const double a1 = AngleOf(t1 - c), a2 = AngleOf(t2 - c);
    return SweepCCW(a1, a2) <= kPi ? Entity::Arc(c, r, a1, a2) : Entity::Arc(c, r, a2, a1);
}

// Polyline vertices with the original shape kept in a list of pieces.
std::vector<Vertex> ReversedVerts(const std::vector<Vertex>& v) {
    std::vector<Vertex> out(v.size());
    const size_t n = v.size();
    for (size_t i = 0; i < n; ++i) {
        out[i].p = v[n - 1 - i].p;
        out[i].bulge = i + 1 < n ? -v[n - 2 - i].bulge : 0;
    }
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------
// Trim and extend
// ---------------------------------------------------------------------------

bool TrimAt(const Entity& e, Vec2 pick, const std::vector<Prim>& edges, std::vector<Entity>& out, Entity* removed) {
    out.clear();
    if (!IsCurve(e)) return false;
    const double L = CurveLength(e);
    const double eps = Eps(L) * 100;
    const std::vector<double> params = CrossingParams(e, edges);
    const double tp = CurveParam(e, pick);
    double prev = -1, next = -1;
    for (double s : params) {
        if (s < tp - eps) prev = s;
        else if (s > tp + eps && next < 0) next = s;
    }
    if (IsClosedCurve(e)) {
        if (params.size() < 2) return false;
        if (prev < 0) prev = params.back();
        if (next < 0) next = params.front();
        if (std::fabs(prev - next) <= eps) return false;
        Entity keep;
        if (!SubCurve(e, next, prev, keep)) return false;
        if (removed && !SubCurve(e, prev, next, *removed)) *removed = e;
        keep.id = e.id;
        out.push_back(keep);
        return true;
    }
    const double a = prev < 0 ? 0 : prev, b = next < 0 ? L : next;
    Entity piece;
    if (a > eps && SubCurve(e, 0, a, piece)) out.push_back(piece);
    if (b < L - eps && SubCurve(e, b, L, piece)) out.push_back(piece);
    if (removed && !SubCurve(e, a, b, *removed)) *removed = e;
    for (Entity& p : out) p.id = e.id;
    return true;
}

bool ExtendAt(const Entity& e, Vec2 pick, const std::vector<Prim>& edges, double reach, Entity& out) {
    if (!IsCurve(e) || IsClosedCurve(e)) return false;
    const double L = CurveLength(e);
    const bool atEnd = CurveParam(e, pick) > L / 2;
    const std::vector<CurvePiece> pieces = CurvePieces(e);
    const CurvePiece& piece = atEnd ? pieces.back() : pieces.front();
    const double eps = Eps(std::max(L, reach)) * 1000;
    double best = std::numeric_limits<double>::infinity();
    std::vector<Vec2> pts;
    if (!piece.arc) {
        const Vec2 dir = atEnd ? piece.Direction(piece.len) : -piece.Direction(0);
        const Vec2 origin = atEnd ? piece.b : piece.a;
        const Prim ray = Prim::Seg(origin, origin + dir * reach);
        for (const Prim& edge : edges) {
            pts.clear();
            Intersect(ray, edge, pts);
            for (Vec2 p : pts) {
                const double t = Dot(p - origin, dir);
                if (t > eps && t < best) best = t;
            }
        }
    } else {
        const Prim circle = Prim::Circle(piece.c, piece.r);
        const double span = std::fabs(piece.sweep);
        const double sign = (piece.sweep > 0) == atEnd ? 1 : -1;  // direction we grow in
        const double endAng = atEnd ? piece.a0 + piece.sweep : piece.a0;
        for (const Prim& edge : edges) {
            pts.clear();
            Intersect(circle, edge, pts);
            for (Vec2 p : pts) {
                const double ang = AngleOf(p - piece.c);
                const double delta = sign > 0 ? SweepCCW(endAng, ang) : SweepCCW(ang, endAng);
                const double len = delta * piece.r;
                if (len > eps && delta < kTau - span - 1e-9 && len < best) best = len;
            }
        }
    }
    if (!std::isfinite(best)) return false;
    out = e;
    return ChangeEnd(out, atEnd, best);
}

// ---------------------------------------------------------------------------
// Offset
// ---------------------------------------------------------------------------

int OffsetSide(const Entity& e, Vec2 p) {
    CurvePiece piece;
    if (!PieceNear(e, p, piece)) return 1;
    if (!piece.arc) return Cross(piece.b - piece.a, p - piece.a) >= 0 ? 1 : -1;
    const bool inside = Dist(p, piece.c) < piece.r;
    return (piece.sweep > 0) == inside ? 1 : -1;
}

namespace {
// Ellipses and splines: a spline through points offset from the curve.
bool OffsetSampled(const Entity& e, double d, Entity& out) {
    std::vector<Vec2> pts;
    if (e.type == EntityType::Ellipse) EllipsePoints(e, pts);
    else SplinePoints(e, pts);
    const bool closed = IsClosedCurve(e);
    if (closed && pts.size() > 2 && Near(pts.front(), pts.back(), 1e-9)) pts.pop_back();
    const size_t n = pts.size();
    if (n < 2) return false;
    // About 72 points round an ellipse; four per span of a spline.
    const size_t want = e.type == EntityType::Ellipse ? (size_t)std::max(8.0, 72 * EllipseSweep(e) / kTau)
                                                      : std::max<size_t>(8, e.verts.size() * 4);
    const size_t step = std::max<size_t>(1, n / want);
    std::vector<size_t> idx;
    for (size_t i = 0; i < n; i += step) idx.push_back(i);
    if (!closed && idx.back() != n - 1) {
        if (n - 1 - idx.back() < step / 2 && idx.size() > 1) idx.back() = n - 1;
        else idx.push_back(n - 1);
    }
    std::vector<Vec2> fit, orig;
    for (size_t i : idx) {
        const Vec2 prev = closed ? pts[(i + n - 1) % n] : pts[i == 0 ? 0 : i - 1];
        const Vec2 next = closed ? pts[(i + 1) % n] : pts[std::min(i + 1, n - 1)];
        if (Near(prev, next, 1e-15)) return false;
        fit.push_back(pts[i] + Perp(Normalized(next - prev)) * d);
        orig.push_back(pts[i]);
    }
    // Too far inside a tight bend, the offset folds over itself.
    for (size_t i = 0; i + 1 < fit.size(); ++i)
        if (Dot(fit[i + 1] - fit[i], orig[i + 1] - orig[i]) <= 0) return false;
    out = Entity::Spline(fit, closed);
    CopyStyle(e, out);
    return true;
}
}  // namespace

bool OffsetEntity(const Entity& e, double d, Entity& out) {
    const double eps = 1e-9;
    out = e;
    out.id = 0;
    switch (e.type) {
        case EntityType::Ellipse:
        case EntityType::Spline:
            return OffsetSampled(e, d, out);
        case EntityType::Line: {
            const Vec2 n = Perp(Normalized(e.p1 - e.p0)) * d;
            Translate(out, n);
            return true;
        }
        case EntityType::Circle:
        case EntityType::Arc:
            // Left of a counter-clockwise curve is the inside.
            out.radius = e.radius - d;
            return out.radius > eps;
        case EntityType::Polyline:
            break;
        default:
            return false;
    }
    struct Off {
        bool arc;
        Vec2 a, b, c;
        double r, sign;
        Vec2 origA, origB;  // the original piece's ends
    };
    std::vector<Off> offs;
    for (const CurvePiece& p : CurvePieces(e)) {
        Off o{p.arc, {}, {}, p.c, p.r, p.sweep >= 0 ? 1.0 : -1.0, p.a, p.b};
        if (!p.arc) {
            const Vec2 n = Perp(Normalized(p.b - p.a)) * d;
            o.a = p.a + n;
            o.b = p.b + n;
        } else {
            o.r = p.r - d * o.sign;
            if (o.r <= eps) continue;  // the arc shrinks to nothing: dropped
            o.a = p.c + Normalized(p.a - p.c) * o.r;
            o.b = p.c + Normalized(p.b - p.c) * o.r;
        }
        offs.push_back(o);
    }
    if (offs.empty()) return false;
    auto carrier = [](const Off& o) {
        Carrier k;
        k.circle = o.arc;
        if (o.arc) {
            k.c = o.c;
            k.r = o.r;
        } else {
            k.p = o.a;
            k.d = Normalized(o.b - o.a);
        }
        return k;
    };
    const double tol = 1e-9 * std::max(1.0, std::fabs(d));
    // Corners: meet where the moved pieces cross (lines are extended or cut
    // back); where they do not cross, a straight piece bridges the gap.
    std::vector<bool> gapAfter(offs.size(), false);
    const size_t joints = e.closed ? offs.size() : offs.size() - 1;
    for (size_t i = 0; i < joints; ++i) {
        Off& o1 = offs[i];
        Off& o2 = offs[(i + 1) % offs.size()];
        if (Near(o1.b, o2.a, tol)) continue;
        const std::vector<Vec2> xs = CarrierIntersections(carrier(o1), carrier(o2));
        const Vec2 mid = Lerp(o1.b, o2.a, 0.5);
        double best = std::numeric_limits<double>::infinity();
        Vec2 x;
        for (Vec2 c : xs)
            if (Dist(c, mid) < best) {
                best = Dist(c, mid);
                x = c;
            }
        if (std::isfinite(best) && best < 4 * std::fabs(d) + Dist(o1.b, o2.a)) {
            o1.b = x;
            o2.a = x;
        } else {
            gapAfter[i] = true;
        }
    }
    std::vector<Vertex> verts;
    for (size_t i = 0; i < offs.size(); ++i) {
        const Off& o = offs[i];
        double bulge = 0;
        if (!o.arc) {
            // A line that turned round has been overtaken by its neighbours.
            if (Dot(o.b - o.a, o.origB - o.origA) <= 0) return false;
        } else {
            const double as = AngleOf(o.a - o.c), ae = AngleOf(o.b - o.c);
            const double sweep = o.sign > 0 ? SweepCCW(as, ae) : -SweepCCW(ae, as);
            bulge = std::tan(sweep / 4);
        }
        verts.push_back({o.a, bulge});
        const bool last = i + 1 == offs.size();
        if ((!last && (gapAfter[i] || !Near(o.b, offs[i + 1].a, tol))) || (last && !e.closed))
            verts.push_back({o.b, 0});
        else if (last && e.closed && !Near(o.b, offs[0].a, tol))
            verts.push_back({o.b, 0});
    }
    if (verts.size() < 2) return false;
    out.verts = std::move(verts);
    out.closed = e.closed;
    return true;
}

// ---------------------------------------------------------------------------
// Fillet and chamfer
// ---------------------------------------------------------------------------

bool FilletObjects(const Entity& a, Vec2 pa, const Entity& b, Vec2 pb, double r, bool trim, CornerResult& res,
                   std::wstring& why) {
    auto supported = [](const Entity& e) {
        return e.type == EntityType::Line || e.type == EntityType::Arc || e.type == EntityType::Circle;
    };
    if (!supported(a) || !supported(b)) {
        why = L"Fillet works on lines, arcs and circles. For polylines use the Polyline option, or explode them.";
        return false;
    }
    CurvePiece pieceA, pieceB;
    PieceNear(a, pa, pieceA);
    PieceNear(b, pb, pieceB);
    const Carrier ka = CarrierOf(pieceA), kb = CarrierOf(pieceB);
    double best = std::numeric_limits<double>::infinity();
    Vec2 centre, ta, tb;
    const double offs[2] = {r, -r};
    for (int i = 0; i < (r > 0 ? 2 : 1); ++i) {
        for (int j = 0; j < (r > 0 ? 2 : 1); ++j) {
            Carrier oa, ob;
            if (!OffsetCarrier(ka, r > 0 ? offs[i] : 0, oa) || !OffsetCarrier(kb, r > 0 ? offs[j] : 0, ob)) continue;
            for (Vec2 c : CarrierIntersections(oa, ob)) {
                const Vec2 t1 = r > 0 ? Foot(ka, c) : c, t2 = r > 0 ? Foot(kb, c) : c;
                const double score = Dist(t1, pa) + Dist(t2, pb);
                if (score < best) {
                    best = score;
                    centre = c;
                    ta = t1;
                    tb = t2;
                }
            }
        }
    }
    if (!std::isfinite(best)) {
        why = r > 0 ? L"No fillet of that radius fits between these objects." : L"These objects do not meet (they are parallel).";
        return false;
    }
    res = CornerResult{};
    res.first = a;
    res.second = b;
    if (trim) {
        if (!TrimTo(a, ta, pa, res.first) || !TrimTo(b, tb, pb, res.second)) {
            why = L"The radius is too large for these objects.";
            return false;
        }
        res.changeFirst = a.type != EntityType::Circle;
        res.changeSecond = b.type != EntityType::Circle;
    }
    if (r > 0) {
        res.hasJoin = true;
        res.join = ArcBetween(centre, r, ta, tb);
    }
    return true;
}

namespace {
// The corner point of two lines and the directions from it towards the picks.
bool LineCorner(const Entity& a, Vec2 pa, const Entity& b, Vec2 pb, Vec2& x, Vec2& ua, Vec2& ub) {
    if (a.type != EntityType::Line || b.type != EntityType::Line) return false;
    Carrier ka, kb;
    ka.p = a.p0;
    ka.d = Normalized(a.p1 - a.p0);
    kb.p = b.p0;
    kb.d = Normalized(b.p1 - b.p0);
    const std::vector<Vec2> xs = CarrierIntersections(ka, kb);
    if (xs.empty()) return false;
    x = xs[0];
    auto toward = [&](const Carrier& k, const Entity& line, Vec2 pick) {
        Vec2 v = Foot(k, pick) - x;
        if (Length(v) < 1e-12) v = (Dist(line.p0, x) > Dist(line.p1, x) ? line.p0 : line.p1) - x;
        return Normalized(v);
    };
    ua = toward(ka, a, pa);
    ub = toward(kb, b, pb);
    return true;
}
}  // namespace

bool ChamferDistanceFromAngle(const Entity& a, Vec2 pa, const Entity& b, Vec2 pb, double d1, double angle,
                              double& d2) {
    Vec2 x, ua, ub;
    if (!LineCorner(a, pa, b, pb, x, ua, ub)) return false;
    const double beta = std::acos(std::clamp(Dot(ua, ub), -1.0, 1.0));
    const double s = std::sin(beta + angle);
    if (s <= 1e-9) return false;
    d2 = d1 * std::sin(angle) / s;
    return d2 > 0;
}

bool ChamferLines(const Entity& a, Vec2 pa, const Entity& b, Vec2 pb, double d1, double d2, bool trim,
                  CornerResult& res, std::wstring& why) {
    Vec2 x, ua, ub;
    if (a.type != EntityType::Line || b.type != EntityType::Line) {
        why = L"Chamfer works on two lines, or on a polyline with the Polyline option.";
        return false;
    }
    if (!LineCorner(a, pa, b, pb, x, ua, ub)) {
        why = L"The lines are parallel.";
        return false;
    }
    const Vec2 ca = x + ua * d1, cb = x + ub * d2;
    res = CornerResult{};
    res.first = a;
    res.second = b;
    if (trim) {
        if (!TrimLineTo(a, ca, pa, res.first) || !TrimLineTo(b, cb, pb, res.second)) {
            why = L"The chamfer distances are too large for these lines.";
            return false;
        }
        res.changeFirst = res.changeSecond = true;
    }
    if (d1 > 0 || d2 > 0) {
        res.hasJoin = true;
        res.join = Entity::Line(ca, cb);
    }
    return true;
}

namespace {
// Shared work of polyline fillet and chamfer: `corner` gives, for a vertex
// with incoming direction din and outgoing dout, how far back (t1) and
// forward (t2) the corner is cut, and the bulge of the joining piece.
template <class Corner>
int CutPolylineCorners(Entity& pl, int onlyVertex, Corner corner) {
    if (pl.type != EntityType::Polyline || pl.verts.size() < 3) return 0;
    const size_t n = pl.verts.size();
    const std::vector<Vertex> v = pl.verts;
    std::vector<double> t1(n, 0), t2(n, 0), bulge(n, 0);
    std::vector<bool> cut(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (!pl.closed && (i == 0 || i == n - 1)) continue;
        if (onlyVertex >= 0 && (size_t)onlyVertex != i) continue;
        const size_t prev = (i + n - 1) % n, next = (i + 1) % n;
        if (v[prev].bulge != 0 || v[i].bulge != 0) continue;  // straight segments only
        const Vec2 din = v[i].p - v[prev].p, dout = v[next].p - v[i].p;
        if (Length(din) < 1e-12 || Length(dout) < 1e-12) continue;
        const Vec2 ui = Normalized(din), uo = Normalized(dout);
        if (std::fabs(Cross(ui, uo)) < 1e-9) continue;  // straight on: no corner
        if (!corner(ui, uo, t1[i], t2[i], bulge[i])) continue;
        cut[i] = true;
    }
    // Each segment must be long enough for the cuts at both its ends.
    for (size_t i = 0; i < n; ++i) {
        const size_t next = (i + 1) % n;
        if (!pl.closed && i == n - 1) break;
        const double len = Dist(v[i].p, v[next].p);
        const double need = (cut[i] ? t2[i] : 0) + (cut[next] ? t1[next] : 0);
        if (need > len + 1e-9) {
            if (cut[next]) cut[next] = false;
            else cut[i] = false;
        }
    }
    std::vector<Vertex> out;
    int count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!cut[i]) {
            out.push_back(v[i]);
            continue;
        }
        const size_t prev = (i + n - 1) % n, next = (i + 1) % n;
        const Vec2 ui = Normalized(v[i].p - v[prev].p), uo = Normalized(v[next].p - v[i].p);
        out.push_back({v[i].p - ui * t1[i], bulge[i]});
        out.push_back({v[i].p + uo * t2[i], 0});
        ++count;
    }
    if (count) pl.verts = std::move(out);
    return count;
}
}  // namespace

int FilletPolyline(Entity& pl, double r, int onlyVertex) {
    if (r <= 0) return 0;
    return CutPolylineCorners(pl, onlyVertex, [&](Vec2 ui, Vec2 uo, double& t1, double& t2, double& bulge) {
        const double phi = std::acos(std::clamp(Dot(ui, uo), -1.0, 1.0));  // turning angle
        if (phi >= kPi - 1e-9) return false;
        t1 = t2 = r * std::tan(phi / 2);
        bulge = std::tan(phi / 4) * (Cross(ui, uo) > 0 ? 1 : -1);
        return true;
    });
}

int ChamferPolyline(Entity& pl, double d1, double d2, int onlyVertex) {
    if (d1 <= 0 && d2 <= 0) return 0;
    return CutPolylineCorners(pl, onlyVertex, [&](Vec2, Vec2, double& t1, double& t2, double& bulge) {
        t1 = d1;
        t2 = d2;
        bulge = 0;
        return true;
    });
}

int SharedVertex(const Entity& pl, Vec2 pickA, Vec2 pickB) {
    if (pl.type != EntityType::Polyline) return -1;
    CurvePiece piece;
    size_t ia = 0, ib = 0;
    PieceNear(pl, pickA, piece, &ia);
    PieceNear(pl, pickB, piece, &ib);
    const size_t segs = SegmentCount(pl);
    if (segs < 2) return -1;
    if ((ia + 1) % segs == ib && (ia + 1 < segs || pl.closed)) return (int)((ia + 1) % pl.verts.size());
    if ((ib + 1) % segs == ia && (ib + 1 < segs || pl.closed)) return (int)((ib + 1) % pl.verts.size());
    return -1;
}

// ---------------------------------------------------------------------------
// Break, join, explode, stretch
// ---------------------------------------------------------------------------

bool BreakCurve(const Entity& e, double s1, double s2, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!IsCurve(e)) {
        why = L"Only lines, arcs, circles and polylines can be broken.";
        return false;
    }
    const double L = CurveLength(e);
    const double eps = Eps(L) * 100;
    if (IsClosedCurve(e)) {
        if (std::fabs(s1 - s2) <= eps) {
            why = L"A closed shape cannot be broken at a single point: pick two points.";
            return false;
        }
        Entity keep;
        if (!SubCurve(e, s2, s1, keep)) {
            why = L"Nothing would be left.";
            return false;
        }
        keep.id = e.id;
        out.push_back(keep);
        return true;
    }
    const double a = std::min(s1, s2), b = std::max(s1, s2);
    Entity piece;
    if (b - a <= eps) {
        if (a <= eps || a >= L - eps) {
            why = L"That is the end of the object: there is nothing to break.";
            return false;
        }
        if (SubCurve(e, 0, a, piece)) out.push_back(piece);
        if (SubCurve(e, a, L, piece)) out.push_back(piece);
        return out.size() == 2;
    }
    if (a > eps && SubCurve(e, 0, a, piece)) out.push_back(piece);
    if (b < L - eps && SubCurve(e, b, L, piece)) out.push_back(piece);
    return true;  // an empty result erases the object
}

namespace {
struct Chain {
    std::vector<Vertex> v;  // open; the last vertex's bulge is unused
    std::vector<size_t> sources;
};

bool ToChain(const Entity& e, Chain& c) {
    switch (e.type) {
        case EntityType::Line:
            c.v = {{e.p0, 0}, {e.p1, 0}};
            return true;
        case EntityType::Arc: {
            double sweep = SweepCCW(e.startAngle, e.endAngle);
            if (sweep <= 0) sweep = kTau;
            c.v = {{Polar(e.p0, e.radius, e.startAngle), std::tan(sweep / 4)}, {Polar(e.p0, e.radius, e.endAngle), 0}};
            return true;
        }
        case EntityType::Polyline:
            if (e.closed || e.verts.size() < 2) return false;
            c.v = e.verts;
            c.v.back().bulge = 0;
            return true;
        default:
            return false;
    }
}

// Merges straight runs and arcs of the same circle; true if `closed`.
void Simplify(std::vector<Vertex>& v, bool closed, double tol) {
    bool changed = true;
    while (changed && v.size() > (closed ? 1u : 2u)) {
        changed = false;
        const size_t n = v.size();
        const size_t limit = closed ? n : n - 1;  // vertex i joins segment i-1 and i
        for (size_t i = closed ? 0 : 1; i < limit && v.size() > (closed ? 1u : 2u); ++i) {
            const size_t prev = (i + v.size() - 1) % v.size(), next = (i + 1) % v.size();
            const Vertex &a = v[prev], &b = v[i], &c = v[next];
            bool merge = false;
            double bulge = 0;
            if (a.bulge == 0 && b.bulge == 0) {
                const Vec2 d1 = b.p - a.p, d2 = c.p - b.p;
                merge = std::fabs(Cross(d1, d2)) <= tol * (Length(d1) + Length(d2)) && Dot(d1, d2) > 0;
            } else if (a.bulge != 0 && b.bulge != 0 && (a.bulge > 0) == (b.bulge > 0)) {
                const Prim p1 = ArcFromBulge(a.p, b.p, a.bulge), p2 = ArcFromBulge(b.p, c.p, b.bulge);
                if (Near(p1.c, p2.c, tol) && std::fabs(p1.r - p2.r) <= tol) {
                    const double theta = 4 * std::atan(a.bulge) + 4 * std::atan(b.bulge);
                    if (std::fabs(theta) < kTau - 1e-9) {
                        merge = true;
                        bulge = std::tan(theta / 4);
                    }
                }
            }
            if (merge) {
                v[prev].bulge = bulge;
                v.erase(v.begin() + (long)i);
                changed = true;
                break;
            }
        }
    }
}
}  // namespace

std::vector<JoinGroup> JoinObjects(const std::vector<Entity>& in) {
    std::vector<Chain> chains;
    Box box;
    for (size_t i = 0; i < in.size(); ++i) {
        Chain c;
        if (!ToChain(in[i], c)) continue;
        c.sources = {i};
        box.Add(Bounds(in[i]));
        chains.push_back(std::move(c));
    }
    const double tol = 1e-7 * std::max(1.0, std::hypot(box.Width(), box.Height()));
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < chains.size() && !merged; ++i) {
            for (size_t j = 0; j < chains.size() && !merged; ++j) {
                if (i == j) continue;
                Chain& a = chains[i];
                Chain b = chains[j];
                const Vec2 aEnd = a.v.back().p, aStart = a.v.front().p;
                if (Near(aEnd, b.v.back().p, tol) || Near(aStart, b.v.front().p, tol)) b.v = ReversedVerts(b.v);
                if (Near(aEnd, b.v.front().p, tol)) {
                    a.v.pop_back();
                    a.v.insert(a.v.end(), b.v.begin(), b.v.end());
                } else if (Near(aStart, b.v.back().p, tol)) {
                    std::vector<Vertex> joined = b.v;
                    joined.pop_back();
                    joined.insert(joined.end(), a.v.begin(), a.v.end());
                    a.v = std::move(joined);
                } else {
                    continue;
                }
                a.sources.insert(a.sources.end(), b.sources.begin(), b.sources.end());
                chains.erase(chains.begin() + (long)j);
                merged = true;
            }
        }
    }
    std::vector<JoinGroup> groups;
    for (Chain& c : chains) {
        if (c.sources.size() < 2) continue;
        bool closed = c.v.size() >= 3 && Near(c.v.front().p, c.v.back().p, tol);
        if (closed) c.v.pop_back();
        Simplify(c.v, closed, tol);
        JoinGroup g;
        g.sources = c.sources;
        bool circle = false;
        if (closed && c.v.size() == 2 && c.v[0].bulge != 0 && c.v[1].bulge != 0) {
            // Two arcs of one circle closing on each other: a circle.
            const Prim p1 = ArcFromBulge(c.v[0].p, c.v[1].p, c.v[0].bulge);
            const Prim p2 = ArcFromBulge(c.v[1].p, c.v[0].p, c.v[1].bulge);
            if (Near(p1.c, p2.c, tol) && std::fabs(p1.r - p2.r) <= tol && (c.v[0].bulge > 0) == (c.v[1].bulge > 0)) {
                g.result = Entity::Circle(p1.c, p1.r);
                circle = true;
            }
        }
        if (circle) {
            // done
        } else if (!closed && c.v.size() == 2 && c.v[0].bulge == 0) {
            g.result = Entity::Line(c.v[0].p, c.v[1].p);
        } else if (!closed && c.v.size() == 2) {
            const Prim p = ArcFromBulge(c.v[0].p, c.v[1].p, c.v[0].bulge);
            g.result = Entity::Arc(p.c, p.r, p.start, p.start + p.sweep);
        } else {
            g.result = Entity::Polyline(c.v, closed);
        }
        CopyStyle(in[c.sources[0]], g.result);
        groups.push_back(std::move(g));
    }
    return groups;
}

bool Explode(const Entity& e, std::vector<Entity>& out) {
    if (e.type == EntityType::Array) {
        ExpandArray(e, out);
        return true;
    }
    if (e.type == EntityType::Insert) {  // one level: blocks inside stay blocks
        ExpandBlock(e, out, 0, false);
        return !out.empty();
    }
    if (e.type == EntityType::Leader && e.annot) {
        const auto& p = e.annot->pts;
        for (size_t i = 0; i + 1 < p.size(); ++i) out.push_back(Entity::Line(p[i], p[i + 1]));
        AnnotGraphics g;
        AnnotLayout(e, nullptr, g);
        for (const auto& f : g.fills) {  // the arrow
            std::vector<Vertex> v;
            for (Vec2 q : f) v.push_back({q, 0});
            out.push_back(Entity::Polyline(v, true));
        }
        TextData note;
        Vec2 at, land;
        if (LeaderNote(e, nullptr, note, at, &land)) {
            out.push_back(Entity::Line(p.back(), land));
            out.push_back(MakeText(at, note));
        }
        for (Entity& x : out) CopyStyle(e, x);
        return !out.empty();
    }
    if (e.type == EntityType::Hatch && e.annot) {  // its pattern as lines (a solid fill cannot be)
        if (IsSolid(e.annot->hatch)) return false;
        std::vector<std::pair<Vec2, Vec2>> segs;
        if (!HatchSegments(e.annot->hatch, segs) || segs.empty()) return false;
        for (const auto& sg : segs) {
            out.push_back(Near(sg.first, sg.second, 1e-12) ? Entity::Point(sg.first) : Entity::Line(sg.first, sg.second));
            CopyStyle(e, out.back());
        }
        return true;
    }
    if (e.type == EntityType::Text && e.annot && e.annot->text.frame < kFrameControl) {  // one text per line
        AnnotGraphics g;
        LayoutText(e.annot->text, e.p0, nullptr, g);
        if (g.texts.size() < 2) return false;
        for (const TextItem& it : g.texts) {
            TextData t = e.annot->text;
            t.content = SplitLines(t.content)[&it - &g.texts[0]];
            t.justify = 3 + it.align;
            t.frame = 0;
            out.push_back(MakeText(it.pos, t));
            CopyStyle(e, out.back());
        }
        return true;
    }
    if (e.type != EntityType::Polyline) return false;
    for (const CurvePiece& p : CurvePieces(e)) {
        if (!p.arc) out.push_back(Entity::Line(p.a, p.b));
        else if (p.sweep > 0) out.push_back(Entity::Arc(p.c, p.r, p.a0, p.a0 + p.sweep));
        else out.push_back(Entity::Arc(p.c, p.r, p.a0 + p.sweep, p.a0));
        CopyStyle(e, out.back());
    }
    return !out.empty();
}

bool StretchEntity(Entity& e, const Box& w, Vec2 d) {
    if (InsideBox(e, w)) {
        Translate(e, d);
        return true;
    }
    switch (e.type) {
        case EntityType::Line: {
            bool moved = false;
            if (w.Contains(e.p0)) {
                e.p0 += d;
                moved = true;
            }
            if (w.Contains(e.p1)) {
                e.p1 += d;
                moved = true;
            }
            return moved;
        }
        case EntityType::Circle:
            if (!w.Contains(e.p0)) return false;
            Translate(e, d);
            return true;
        case EntityType::Arc: {
            const Vec2 s = Polar(e.p0, e.radius, e.startAngle), t = Polar(e.p0, e.radius, e.endAngle);
            const bool ms = w.Contains(s), mt = w.Contains(t);
            if (!ms && !mt) return false;
            if (ms && mt) {
                Translate(e, d);
                return true;
            }
            double sweep = SweepCCW(e.startAngle, e.endAngle);
            if (sweep <= 0) sweep = kTau;
            const Vec2 mid = Polar(e.p0, e.radius, e.startAngle + sweep / 2);
            const Vec2 ns = ms ? s + d : s, nt = mt ? t + d : t;
            Vec2 c;
            double r;
            if (!CircleFrom3Points(ns, mid, nt, c, r)) return false;
            const bool ccw = Cross(mid - ns, nt - ns) > 0;
            const uint64_t id = e.id;
            e = ccw ? Entity::Arc(c, r, AngleOf(ns - c), AngleOf(nt - c)) : Entity::Arc(c, r, AngleOf(nt - c), AngleOf(ns - c));
            e.id = id;
            return true;
        }
        case EntityType::Polyline: {
            bool moved = false;
            for (Vertex& v : e.verts)
                if (w.Contains(v.p)) {
                    v.p += d;
                    moved = true;
                }
            return moved;
        }
        case EntityType::Ellipse:
            if (!w.Contains(e.p0)) return false;
            Translate(e, d);
            return true;
        case EntityType::Spline: {
            bool moved = false;
            for (Vertex& v : e.verts)
                if (w.Contains(v.p)) {
                    v.p += d;
                    moved = true;
                }
            return moved;
        }
        case EntityType::Dimension:
            return DimStretch(e, w, d);
        case EntityType::Leader: {
            bool moved = false;
            Annot& a = EditAnnot(e);
            for (Vec2& p : a.pts)
                if (w.Contains(p)) {
                    p += d;
                    moved = true;
                }
            if (!a.pts.empty()) e.p0 = a.pts[0];
            return moved;
        }
        case EntityType::Text:
        case EntityType::Insert:
            if (!w.Contains(e.p0)) return false;
            Translate(e, d);
            return true;
        case EntityType::Array:
        case EntityType::Point:
        case EntityType::Hatch:  // follows its outline (if associative)
        case EntityType::Viewport:
        case EntityType::Relation:
            return false;
        case EntityType::Image:  // moves when wholly inside the window
            if (!w.Contains(e.p0) || !w.Contains(e.p1)) return false;
            Translate(e, d);
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Polyline segments in fillet, chamfer and tangent circles
// ---------------------------------------------------------------------------

// A straight polyline segment picked near p, as a line (seg: its index).
bool SegmentAsLine(const Entity& pl, Vec2 p, Entity& line, size_t& seg) {
    std::vector<Prim> prims;
    Primitives(pl, prims);
    if (prims.empty()) return false;
    size_t best = 0;
    for (size_t i = 1; i < prims.size(); ++i)
        if (Distance(prims[i], p) < Distance(prims[best], p)) best = i;
    if (prims[best].kind != Prim::Segment || pl.verts[best].bulge != 0) return false;
    seg = best;
    line = Entity::Line(pl.verts[best].p, pl.verts[(best + 1) % pl.verts.size()].p);
    return true;
}

// Puts a trimmed or extended segment back into its polyline. Only the free
// ends of an open polyline can move; false if an inner vertex would have to.
bool PutSegmentBack(const Entity& pl, size_t seg, const Entity& line, Entity& out) {
    out = pl;
    const size_t n = pl.verts.size(), i = seg, j = (seg + 1) % n;
    const Vec2 s = pl.verts[i].p, t = pl.verts[j].p;
    Vec2 ns = line.p0, nt = line.p1;
    if (Dist(ns, s) + Dist(nt, t) > Dist(nt, s) + Dist(ns, t)) std::swap(ns, nt);
    const double tol = 1e-9 * (1 + Length(s) + Length(t));
    auto freeEnd = [&](size_t k) { return !pl.closed && (k == 0 || k == n - 1); };
    if (!Near(ns, s, tol) && !freeEnd(i)) return false;
    if (!Near(nt, t, tol) && !freeEnd(j)) return false;
    out.verts[i].p = ns;
    out.verts[j].p = nt;
    return true;
}


std::vector<std::pair<Vec2, Vec2>> TangentLines(Vec2 c1, double r1, Vec2 c2, double r2) {
    std::vector<std::pair<Vec2, Vec2>> out;
    const Vec2 d = c2 - c1;
    const double D2 = Dot(d, d);
    if (D2 <= 1e-24) return out;  // concentric: no common tangent
    // Lines n.x + c = 0 (|n| = 1) at distance r1 from c1 and s*r2 from c2.
    for (int s : {1, -1}) {
        const double rr = s * r2 - r1;
        const double h2 = D2 - rr * rr;
        if (h2 < -1e-12 * D2) continue;
        const double h = std::sqrt(std::max(0.0, h2));
        for (int k : {1, -1}) {
            const Vec2 n = (d * rr + Perp(d) * (k * h)) / D2;
            const Vec2 t1 = c1 - n * r1, t2 = c2 - n * (s * r2);
            if (Near(t1, t2, 1e-12)) continue;
            bool dup = false;
            for (const auto& o : out) dup |= Near(o.first, t1, 1e-12) && Near(o.second, t2, 1e-12);
            if (!dup) out.push_back({t1, t2});
            if (h == 0) break;
        }
    }
    return out;
}
