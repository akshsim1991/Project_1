// EngCurves.cpp - see EngCurves.h.
#include "EngCurves.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <functional>

#include "Annot.h"
#include "Curve.h"
#include "EngGraphics.h"
#include "Style.h"

namespace {
constexpr int kThin = 18, kThick = 50;

Entity W(Entity e, int lw) {
    e.lineweight = (int16_t)lw;
    return e;
}

std::wstring Num(double v, int decimals = 2) {
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%.*f", decimals, v);
    std::wstring s = buf;
    if (s.find(L'.') != std::wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    return s == L"-0" ? L"0" : s;
}

Entity Note(Vec2 at, const std::wstring& s, double h, int justify = 0) {
    TextData t;
    t.content = s;
    t.height = h;
    t.justify = justify;
    return MakeText(at, t);
}

// A smooth curve through the points (thick).
Entity Curve(const std::vector<Vec2>& pts, bool closed = false) { return W(Entity::Spline(pts, closed), kThick); }

Entity CentreLine(Vec2 a, Vec2 b) {
    Entity e = W(Entity::Line(a, b), kThin);
    e.linetype = (int16_t)std::max(0, FindLineType(L"CENTER2"));
    return e;
}

Box BoxOf(const std::vector<Entity>& ents) {
    Box b;
    for (const Entity& e : ents)
        if (e.type != EntityType::Text) b.Add(Bounds(e));
    return b;
}

void AddNote(std::vector<Entity>& out, const std::wstring& text, double h) {
    const Box b = BoxOf(out);
    out.push_back(Note({b.minX, b.minY - h * 2.5}, text, h * 0.8));
}
}  // namespace

bool ConicCurve(Vec2 at, double d, double e, double h, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!(d > 0)) return why = L"The distance of the focus from the directrix must be more than zero.", false;
    if (!(e > 0) || e > 10) return why = L"The eccentricity must be more than 0 (and not over 10).", false;
    const Vec2 F = at + Vec2{d, 0};
    // r(theta) about the focus, theta from the axis away from the directrix: r = e d / (1 - e cos theta).
    auto pointAt = [&](double th) {
        const double r = e * d / (1 - e * std::cos(th));
        return F + Vec2{r * std::cos(th), r * std::sin(th)};
    };
    const bool ellipse = e < 1 - 1e-9;
    std::vector<Vec2> pts;
    double reach;  // how far the curve goes along the axis (from the directrix)
    if (ellipse) {
        for (int k = 0; k < 48; ++k) pts.push_back(pointAt(kTau * k / 48));
        reach = d / (1 - e);
        out.push_back(Curve(pts, true));
    } else {
        // As far as 3 d beyond the focus.
        const double rmax = 3 * d + e * d;
        const double c = std::clamp((1 - e * d / rmax) / e, -1.0, 1.0), th0 = std::acos(c);
        for (int k = 0; k <= 48; ++k) pts.push_back(pointAt(th0 + (kTau - 2 * th0) * k / 48));
        reach = 0;
        for (Vec2 p : pts) reach = std::max(reach, p.x - at.x);
        out.push_back(Curve(pts));
    }
    double half = 0;
    for (Vec2 p : pts) half = std::max(half, std::fabs(p.y - at.y));
    half += h * 2;
    // Directrix, axis, focus and vertex.
    out.push_back(W(Entity::Line(at + Vec2{0, -half}, at + Vec2{0, half}), kThick));
    out.push_back(CentreLine(at - Vec2{h * 2, 0}, at + Vec2{reach + h * 3, 0}));
    out.push_back(MakePointLabel(at + Vec2{0, half}, L"DIRECTRIX", h * 0.8, {0, 1}));
    out.push_back(MakePointLabel(at, L"A", h, {-1, -1}));
    out.push_back(MakePointLabel(F, L"F", h, {0.5, -1}));
    out.push_back(Entity::Point(F));
    const Vec2 V = at + Vec2{e * d / (1 + e), 0};
    out.push_back(MakePointLabel(V, L"V", h, {-0.5, -1}));
    if (ellipse) {
        const Vec2 V2 = at + Vec2{e * d / (1 - e), 0};
        out.push_back(MakePointLabel(V2, L"V'", h, {0.5, -1}));
    }
    const wchar_t* name = ellipse ? L"ELLIPSE" : e < 1 + 1e-9 ? L"PARABOLA" : L"HYPERBOLA";
    AddNote(out,
            std::wstring(name) + L" BY THE ECCENTRICITY METHOD\ndistance of the focus from the directrix AF = " + Num(d) +
                L", eccentricity e = VF / VA = " + Num(e, 4) + L", AV = " + Num(e * d / (1 + e)),
            h);
    return true;
}

bool RollingCurve(Roll kind, Vec2 at, double dia, double baseDia, double h, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!(dia > 0)) return why = L"The diameter of the rolling circle must be more than zero.", false;
    const double r = dia / 2, R = baseDia / 2;
    if (kind != Roll::Cycloid && !(R > 0)) return why = L"The diameter of the base circle must be more than zero.", false;
    if (kind == Roll::Hypocycloid && r >= R) return why = L"The rolling circle must be smaller than the base circle.", false;
    const int N = 12, fine = 48;
    // The point and the rolling circle's centre after turning the circle by t (0..2 pi).
    std::function<Vec2(double)> point, centre;
    if (kind == Roll::Cycloid) {
        point = [&](double t) { return at + Vec2{r * (t - std::sin(t)), r * (1 - std::cos(t))}; };
        centre = [&](double t) { return at + Vec2{r * t, r}; };
    } else {
        // Starting at the top of the base circle, rolling clockwise round it.
        const double s = kind == Roll::Epicycloid ? 1 : -1, Rc = R + s * r;
        centre = [=](double t) {
            const double phi = kPi / 2 - t * r / R;  // where the circles touch
            return at + Vec2{Rc * std::cos(phi), Rc * std::sin(phi)};
        };
        // t: how far the rolling circle has turned against the base (arc R theta = r t).
        // From its centre, the point is at the contact direction turned back by t (outside)
        // or on by t (inside), so it is at the contact again after a full turn.
        point = [=](double t) {
            const double phi = kPi / 2 - t * r / R;
            const Vec2 c = at + Vec2{Rc * std::cos(phi), Rc * std::sin(phi)};
            const double dir = s > 0 ? phi + kPi - t : phi + t;
            return c + Vec2{r * std::cos(dir), r * std::sin(dir)};
        };
    }
    std::vector<Vec2> pts;
    for (int k = 0; k <= fine; ++k) pts.push_back(point(kTau * k / fine));
    out.push_back(Curve(pts));
    // The rolling circle where it starts, divided into 12; the centres' path; the base.
    const Vec2 c0 = centre(0);
    out.push_back(W(Entity::Circle(c0, r), kThin));
    std::vector<Vec2> cs;
    for (int k = 0; k <= N; ++k) cs.push_back(centre(kTau * k / N));
    if (kind == Roll::Cycloid) {
        out.push_back(W(Entity::Line(at, at + Vec2{kPi * dia, 0}), kThick));
        out.push_back(CentreLine(cs.front(), cs.back()));
    } else {
        const double sweep = kTau * r / R;  // the arc rolled over
        out.push_back(W(Entity::Arc(at, R, kPi / 2 - sweep - 0.15, kPi / 2 + 0.15), kThick));
        out.push_back(W(Entity::Arc(at, Dist(at, c0), kPi / 2 - sweep, kPi / 2), kThin));
        out.push_back(CentreLine(at, at + Vec2{0, R}));
        out.push_back(W(Entity::Line(at, Polar(at, R, kPi / 2 - sweep)), kThin));
    }
    for (int k = 0; k <= N; ++k) {
        const Vec2 p = point(kTau * k / N);
        if (k > 0 && k < N) out.push_back(W(Entity::Arc(cs[(size_t)k], r, AngleOf(p - cs[(size_t)k]) - 0.25, AngleOf(p - cs[(size_t)k]) + 0.25), kThin));
        out.push_back(MakePointLabel(p, k == 0 ? L"P" : L"P" + std::to_wstring(k), h * 0.8, p - cs[(size_t)k]));
        if (k > 0) out.push_back(MakePointLabel(cs[(size_t)k], L"C" + std::to_wstring(k), h * 0.7, {0, 1}));
    }
    const wchar_t* name = kind == Roll::Cycloid ? L"CYCLOID" : kind == Roll::Epicycloid ? L"EPICYCLOID" : L"HYPOCYCLOID";
    std::wstring note = std::wstring(name) + L": rolling circle diameter " + Num(dia);
    if (kind == Roll::Cycloid) note += L", one turn = pi \x00D7 d = " + Num(kPi * dia);
    else note += L", base circle diameter " + Num(baseDia) + L", angle rolled = 360 \x00D7 r / R = " + Num(360 * r / R, 1) + L"\x00B0";
    AddNote(out, note, h);
    return true;
}

bool InvoluteCurve(Vec2 at, int sides, double size, double h, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!(size > 0)) return why = L"The size must be more than zero.", false;
    if (sides == 0) {  // a circle: the string unwound for one turn
        const double r = size / 2;
        out.push_back(W(Entity::Circle(at, r), kThick));
        // P starts at the top; the string unwinds clockwise.
        auto point = [&](double t) {
            const double a = kPi / 2 - t;
            const Vec2 c = Polar(at, r, a);
            return c + Vec2{-std::sin(a), std::cos(a)} * (r * t);  // behind the contact, along the string
        };
        std::vector<Vec2> pts;
        for (int k = 0; k <= 48; ++k) pts.push_back(point(kTau * k / 48));
        out.push_back(Curve(pts));
        for (int k = 1; k <= 12; ++k) {
            const double t = kTau * k / 12;
            const Vec2 c = Polar(at, r, kPi / 2 - t), p = point(t);
            out.push_back(W(Entity::Line(c, p), kThin));  // the tangent: the string, k/12 of pi d long
            out.push_back(W(Entity::Line(at, c), kThin));
            out.push_back(MakePointLabel(p, L"P" + std::to_wstring(k), h * 0.8, p - c));
            out.push_back(MakePointLabel(c, std::to_wstring(k), h * 0.7, c - at));
        }
        out.push_back(MakePointLabel(point(0), L"P", h * 0.8, {0, 1}));
        AddNote(out, L"INVOLUTE OF A CIRCLE of diameter " + Num(size) + L": the string, pi \x00D7 d = " + Num(kPi * size) +
                         L" long, unwound for one turn", h);
        return true;
    }
    if (sides < 3 || sides > 12) return why = L"A polygon of 3 to 12 sides.", false;
    // A polygon, a side `size`: the string unwinds round each corner in turn.
    const double R = size / (2 * std::sin(kPi / sides));
    std::vector<Vec2> v;
    for (int k = 0; k < sides; ++k) v.push_back(Polar(at, R, -kPi / 2 + kPi / sides + kTau * k / sides));  // a side at the bottom
    std::vector<Vertex> poly;
    for (Vec2 p : v) poly.push_back({p, 0});
    out.push_back(W(Entity::Polyline(poly, true), kThick));
    // P at corner 0; unwind anticlockwise round corners 1, 2, ...
    Vec2 p = v[0];
    const double ext = kTau / sides;
    for (int k = 1; k <= sides; ++k) {
        const Vec2 c = v[(size_t)(k % sides)];
        const double rad = size * k;
        const double a0 = AngleOf(p - c);
        out.push_back(W(Entity::Arc(c, rad, a0, a0 + ext), kThick));
        const Vec2 q = Polar(c, rad, a0 + ext);
        // The side extended: the string's straight part.
        out.push_back(W(Entity::Line(c, q), kThin));
        out.push_back(MakePointLabel(q, L"P" + std::to_wstring(k), h * 0.8, q - c));
        out.push_back(MakePointLabel(c, std::wstring(1, (wchar_t)(L'A' + k % sides)), h * 0.8, c - at));
        p = q;
    }
    out.push_back(MakePointLabel(v[0], L"P (A)", h * 0.8, v[0] - at));
    AddNote(out, L"INVOLUTE OF A " + std::to_wstring(sides) + L"-SIDED POLYGON of side " + Num(size) +
                     L": arcs about each corner in turn, radii " + Num(size) + L", " + Num(2 * size) + L"...", h);
    return true;
}

bool SpiralCurve(Vec2 at, double r0, double r1, double turns, double h, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (r0 < 0 || !(r1 > r0)) return why = L"The end radius must be larger than the start radius.", false;
    if (!(turns > 0) || turns > 20) return why = L"Between 0 and 20 turns.", false;
    const int N = 12;
    const int fine = std::max(24, (int)std::ceil(48 * turns));
    auto point = [&](double f) {  // f: 0..1 of the way
        const double a = kTau * turns * f, r = r0 + (r1 - r0) * f;
        return Polar(at, r, a);
    };
    std::vector<Vec2> pts;
    for (int k = 0; k <= fine; ++k) pts.push_back(point((double)k / fine));
    out.push_back(Curve(pts));
    out.push_back(W(Entity::Circle(at, r1), kThin));
    if (r0 > 0) out.push_back(W(Entity::Circle(at, r0), kThin));
    for (int k = 0; k < N; ++k) out.push_back(W(Entity::Line(at, Polar(at, r1, kTau * k / N)), kThin));
    const int marks = (int)std::lround(N * turns);
    for (int k = 0; k <= marks; ++k) {
        const Vec2 p = point((double)k / marks);
        out.push_back(MakePointLabel(p, k == 0 ? L"P" : L"P" + std::to_wstring(k), h * 0.7, p - at + Vec2{1e-9, 0}));
    }
    out.push_back(MakePointLabel(at, L"O", h, {-1, -1}));
    AddNote(out, L"ARCHIMEDEAN SPIRAL: from " + Num(r0) + L" to " + Num(r1) + L" from the pole in " + Num(turns, 2) +
                     L" turn(s); the radius grows " + Num((r1 - r0) / (N * turns)) + L" every 30\x00B0",
            h);
    return true;
}

bool HelixCurve(Vec2 at, double dia, double pitch, double turns, double h, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!(dia > 0) || !(pitch > 0)) return why = L"The diameter and pitch must be more than zero.", false;
    if (!(turns > 0) || turns > 20) return why = L"Between 0 and 20 turns.", false;
    const double r = dia / 2, H = pitch * turns;
    // Front view: x = r cos t, height = pitch t / 2 pi; the back half (sin t > 0) hidden.
    const int fine = std::max(24, (int)std::ceil(48 * turns));
    const int hidden = std::max(0, FindLineType(L"HIDDEN2"));
    std::vector<Vec2> run;
    bool back = false;
    auto flush = [&]() {
        if (run.size() >= 2) {
            Entity e = Curve(run);
            if (back) e.linetype = (int16_t)hidden, e.lineweight = 25;
            out.push_back(e);
        }
        run.clear();
    };
    for (int k = 0; k <= fine; ++k) {
        const double t = kTau * turns * k / fine;
        const Vec2 p = at + Vec2{r * std::cos(kPi - t), pitch * t / kTau};
        const bool b = std::sin(kPi - t) > 1e-9;  // y > 0: behind
        if (!run.empty() && b != back) {
            run.push_back(p);
            flush();
        }
        back = b;
        run.push_back(p);
    }
    flush();
    // The cylinder's outline and axis.
    out.push_back(W(Entity::Line(at + Vec2{-r, 0}, at + Vec2{-r, H}), kThin));
    out.push_back(W(Entity::Line(at + Vec2{r, 0}, at + Vec2{r, H}), kThin));
    out.push_back(W(Entity::Line(at + Vec2{-r, 0}, at + Vec2{r, 0}), kThin));
    out.push_back(W(Entity::Line(at + Vec2{-r, H}, at + Vec2{r, H}), kThin));
    out.push_back(CentreLine(at - Vec2{0, h}, at + Vec2{0, H + h}));
    // Top view below, divided into 12.
    const Vec2 c = at - Vec2{0, r + h * 4};
    out.push_back(W(Entity::Circle(c, r), kThick));
    for (int k = 0; k < 12; ++k) {
        const Vec2 q = Polar(c, r, kPi + kTau * k / 12);
        out.push_back(MakePointLabel(q, std::to_wstring(k), h * 0.7, q - c));
    }
    AddNote(out, L"HELIX on a cylinder of diameter " + Num(dia) + L", pitch " + Num(pitch) + L", " + Num(turns, 2) +
                     L" turn(s): the point rises pitch / 12 = " + Num(pitch / 12) + L" for every 30\x00B0 round",
            h);
    return true;
}

bool TangentNormal(const Entity& e, Vec2 near, double length, double h, std::vector<Entity>& out, Vec2* atOut) {
    out.clear();
    Entity c;
    if (!AsMeasuredCurve(e, c) || !(length > 0)) return false;
    const double L = CurveLength(c);
    if (!(L > 1e-9)) return false;
    const double s = CurveParam(c, near);
    Vec2 at = CurvePoint(c, s);
    Vec2 t = CurveDirection(c, s);
    if (e.type == EntityType::Ellipse) {
        // Exactly: the nearest parameter (refined from the fine points), and the derivative there.
        std::vector<Vec2> pts;
        std::vector<double> params;
        EllipsePoints(e, pts, &params);
        size_t best = 0;
        for (size_t i = 1; i < pts.size(); ++i)
            if (Dist(pts[i], near) < Dist(pts[best], near)) best = i;
        const Vec2 u = e.p1, v = Perp(e.p1) * e.radius;
        auto pointAt = [&](double q) { return e.p0 + u * std::cos(q) + v * std::sin(q); };
        double lo = params[best] - kTau / 256, hi = params[best] + kTau / 256;
        for (int k = 0; k < 60; ++k) {
            const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
            if (Dist(pointAt(m1), near) < Dist(pointAt(m2), near)) hi = m2;
            else lo = m1;
        }
        const double q = (lo + hi) / 2;
        at = pointAt(q);
        t = Normalized(u * -std::sin(q) + v * std::cos(q));
    }
    if (c.type == EntityType::Polyline && e.type == EntityType::Spline) {
        // A fine polyline: the chord across the point is closer to the tangent than one piece.
        const double ds = std::min(L / 2000, length / 100);
        const Vec2 a = CurvePoint(c, std::max(0.0, s - ds)), b = CurvePoint(c, std::min(L, s + ds));
        if (Dist(a, b) > 1e-12) t = Normalized(b - a);
    }
    if (Length(t) < 1e-12) return false;
    const Vec2 n = Perp(t);
    out.push_back(W(Entity::Line(at - t * length, at + t * length), 35));
    out.push_back(W(Entity::Line(at - n * length, at + n * length), 35));
    out.push_back(MakePointLabel(at + t * length, L"T", h, t));
    out.push_back(MakePointLabel(at - t * length, L"T", h, t * -1));
    out.push_back(MakePointLabel(at + n * length, L"N", h, n));
    out.push_back(MakePointLabel(at - n * length, L"N", h, n * -1));
    if (atOut) *atOut = at;
    return true;
}
