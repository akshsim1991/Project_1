// EngMore.cpp - see EngMore.h.
#include "EngMore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <map>

#include "Annot.h"
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

Entity Thin(Vec2 a, Vec2 b) { return W(Entity::Line(a, b), kThin); }
Entity Thick(Vec2 a, Vec2 b) { return W(Entity::Line(a, b), kThick); }
Entity Hidden(Vec2 a, Vec2 b) {
    Entity e = W(Entity::Line(a, b), 25);
    e.linetype = (int16_t)std::max(0, FindLineType(L"HIDDEN2"));
    return e;
}
Entity Centre(Vec2 a, Vec2 b) {
    Entity e = W(Entity::Line(a, b), kThin);
    e.linetype = (int16_t)std::max(0, FindLineType(L"CENTER2"));
    return e;
}
Entity Poly(const std::vector<Vec2>& pts, bool closed, int lw) {
    std::vector<Vertex> v;
    for (Vec2 p : pts) v.push_back({p, 0});
    return W(Entity::Polyline(v, closed), lw);
}

Box BoxOf(const std::vector<Entity>& ents) {
    Box b;
    for (const Entity& e : ents)
        if (e.type != EntityType::Text) b.Add(Bounds(e));
    return b;
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

std::wstring RfText(double rf) { return rf < 1 ? L"1:" + Num(1 / rf, 2) : Num(rf, 2) + L":1"; }
}  // namespace

// ---------------------------------------------------------------------------
// Scales
// ---------------------------------------------------------------------------

double UnitMm(const std::wstring& unit) {
    const std::wstring u = Lower(unit);
    if (u == L"km") return 1e6;
    if (u == L"hm") return 1e5;
    if (u == L"dam") return 1e4;
    if (u == L"m") return 1000;
    if (u == L"dm") return 100;
    if (u == L"cm") return 10;
    if (u == L"mm") return 1;
    return 0;
}

bool ReadRf(const std::wstring& s, double& rf) {
    std::wstring t;
    for (wchar_t c : s)
        if (!std::iswspace(c)) t.push_back(c);
    const size_t sep = t.find_first_of(L":/");
    try {
        size_t used = 0;
        if (sep == std::wstring::npos) {
            rf = std::stod(t, &used);
            return used == t.size() && rf > 0;
        }
        const double a = std::stod(t.substr(0, sep), &used);
        if (used != sep) return false;
        const std::wstring rest = t.substr(sep + 1);
        const double b = std::stod(rest, &used);
        if (used != rest.size() || !(a > 0) || !(b > 0)) return false;
        rf = a / b;
        return true;
    } catch (...) {
        return false;
    }
}

bool ScaleDrawing(bool diagonal, Vec2 at, double rf, double length, const std::wstring& unitIn, double h,
                  std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    const std::wstring unit = Lower(unitIn);
    static const wchar_t* const units[] = {L"km", L"hm", L"dam", L"m", L"dm", L"cm", L"mm", L"0.1 mm", L"0.01 mm"};
    int ui = -1;
    for (int i = 0; i < 7; ++i)
        if (unit == units[i]) ui = i;
    if (ui < 0) return why = L"The unit must be km, m, dm, cm or mm.", false;
    if (!(rf > 0)) return why = L"The RF must be more than zero.", false;
    const int n = (int)std::ceil(length - 1e-9);
    if (n < 2 || n > 30) return why = L"The scale should show 2 to 30 main units.", false;
    const double div = rf * UnitMm(unit);  // one main unit on paper (mm)
    const double L = n * div;
    if (L < 30 || L > 1000)
        return why = L"With that RF the scale would be " + Num(L, 1) + L" mm long: choose another RF, length or unit.", false;
    const std::wstring sub = units[ui + 1], subsub = units[ui + 2];
    const double s = div / 10;
    const double H = diagonal ? 25 : 8;
    // Outline and main divisions.
    out.push_back(Poly({at, at + Vec2{L, 0}, at + Vec2{L, H}, at + Vec2{0, H}}, true, kThick));
    for (int k = 1; k < n; ++k) out.push_back(W(Entity::Line(at + Vec2{k * div, 0}, at + Vec2{k * div, H}), diagonal ? kThin : 35));
    if (!diagonal) {
        // The first main unit in tenths; a heavier line along the middle as drawn in exams.
        for (int j = 1; j < 10; ++j) out.push_back(Thin(at + Vec2{j * s, H / 2}, at + Vec2{j * s, H}));
        out.push_back(Thick(at + Vec2{0, H / 2}, at + Vec2{L, H / 2}));
    } else {
        for (int r = 1; r < 10; ++r) out.push_back(Thin(at + Vec2{0, r * H / 10}, at + Vec2{L, r * H / 10}));
        for (int j = 1; j < 10; ++j) {
            out.push_back(Thin(at + Vec2{j * s, H - 2}, at + Vec2{j * s, H}));
            out.push_back(Thin(at + Vec2{j * s, 0}, at + Vec2{j * s, 2}));
        }
        // Diagonals: from the bottom of each tenth to the top of the next, counting left from zero.
        for (int k = 0; k < 10; ++k) out.push_back(Thin(at + Vec2{div - k * s, 0}, at + Vec2{div - (k + 1) * s, H}));
        for (int r = 0; r <= 10; r += 2) out.push_back(Note(at + Vec2{-h * 0.5, r * H / 10}, std::to_wstring(r), h * 0.6, 5));
        out.push_back(Note(at + Vec2{-h * 0.5, H + h * 0.9}, subsub, h * 0.6, 5));
    }
    // Numbers: zero at the end of the first unit; tenths to its left, main units to its right.
    const double ty = at.y - h * 0.6;
    out.push_back(Note({at.x + div, ty}, L"0", h * 0.7, 1));
    for (int k = 1; k < n; ++k) out.push_back(Note({at.x + (k + 1) * div, ty}, std::to_wstring(k), h * 0.7, 1));
    out.push_back(Note({at.x + div / 2, ty}, L"5", h * 0.7, 1));
    out.push_back(Note({at.x, ty}, L"10", h * 0.7, 1));
    out.push_back(Note({at.x - h * 1.2, ty}, sub, h * 0.6, 2));
    out.push_back(Note({at.x + L + h * 1.2, ty}, unit, h * 0.6, 0));
    const double cmReal = 10 / rf / UnitMm(unit);  // real main units in 1 cm of paper
    std::wstring note = std::wstring(diagonal ? L"DIAGONAL SCALE" : L"PLAIN SCALE") + L"   RF = " + RfText(rf) + L"   (1 cm = " +
                        Num(cmReal, 4) + L" " + unit + L")\nreads " + std::to_wstring(n) + L" " + unit + L" to 1 " +
                        (diagonal ? subsub : sub);
    out.push_back(Note({at.x, at.y - h * 3}, note, h * 0.8));
    return true;
}

// ---------------------------------------------------------------------------
// Projections of a point; traces of a line
// ---------------------------------------------------------------------------

const wchar_t* QuadrantName(double up, double front) {
    const double e = 1e-9;
    if (std::fabs(up) < e && std::fabs(front) < e) return L"on XY (both planes)";
    if (std::fabs(up) < e) return front > 0 ? L"on HP, in front of VP" : L"on HP, behind VP";
    if (std::fabs(front) < e) return up > 0 ? L"on VP, above HP" : L"on VP, below HP";
    if (up > 0) return front > 0 ? L"first quadrant" : L"second quadrant";
    return front > 0 ? L"fourth quadrant" : L"third quadrant";
}

std::vector<Entity> PointProjection(Vec2 xy, double up, double front, const std::wstring& name, const DimStyle& style) {
    std::vector<Entity> out;
    const double h = style.textHeight;
    const Vec2 a1 = xy + Vec2{0, up}, a = xy + Vec2{0, -front};
    const double lo = std::min({a1.y, a.y, xy.y}) - h, hi = std::max({a1.y, a.y, xy.y}) + h;
    out.push_back(Thin({xy.x, lo}, {xy.x, hi}));
    out.push_back(Entity::Point(a1));
    out.push_back(Entity::Point(a));
    out.push_back(MakePointLabel(a1, name + L"'", h, {1, 0.4}));
    out.push_back(MakePointLabel(a, name, h, {1, -0.4}));
    auto dim = [&](Vec2 from, Vec2 to, double side) {
        if (Dist(from, to) < 1e-9) return;
        DimData d;
        d.kind = DimData::Linear;
        d.angle = kPi / 2;
        d.pts[0] = from;
        d.pts[1] = to;
        d.loc = {xy.x + side * h * 3, (from.y + to.y) / 2};
        d.style = style;
        out.push_back(MakeDimension(d));
    };
    dim(xy, a1, -1);
    dim(xy, a, -2.2);
    std::wstring upper = name;
    for (wchar_t& c : upper) c = (wchar_t)std::towupper(c);
    const std::wstring where = std::fabs(up) < 1e-9 ? L"on HP" : Num(std::fabs(up)) + (up > 0 ? L" above HP" : L" below HP");
    const std::wstring where2 = std::fabs(front) < 1e-9 ? L"on VP" : Num(std::fabs(front)) + (front > 0 ? L" in front of VP" : L" behind VP");
    out.push_back(Note(xy + Vec2{h * 3, -h * 0.5 + std::min(0.0, std::min(up, -front)) - h * 2}, upper + L": " + where + L", " + where2 + L" (" + QuadrantName(up, front) + L")", h * 0.8));
    return out;
}

Traces LineTraces(const LineViews& v, double xy) {
    Traces t;
    // HT: the front view meets XY at h'; h is below it on the top view.
    if (std::fabs(v.b1.y - v.a1.y) > 1e-9 && std::fabs(v.b.x - v.a.x) > 1e-9) {
        const double u = (xy - v.a1.y) / (v.b1.y - v.a1.y);
        t.h1 = Lerp(v.a1, v.b1, u);
        const double s = (t.h1.x - v.a.x) / (v.b.x - v.a.x);
        t.h = Lerp(v.a, v.b, s);
        t.hasHT = true;
    }
    // VT: the top view meets XY at v; v' is above it on the front view.
    if (std::fabs(v.b.y - v.a.y) > 1e-9 && std::fabs(v.b1.x - v.a1.x) > 1e-9) {
        const double u = (xy - v.a.y) / (v.b.y - v.a.y);
        t.v = Lerp(v.a, v.b, u);
        const double s = (t.v.x - v.a1.x) / (v.b1.x - v.a1.x);
        t.v1 = Lerp(v.a1, v.b1, s);
        t.hasVT = true;
    }
    return t;
}

std::vector<Entity> TraceConstruction(const LineViews& v, double xy, const Traces& t, double h) {
    std::vector<Entity> out;
    // A view extended from its nearer end to the point.
    auto extend = [&](Vec2 a, Vec2 b, Vec2 p) {
        const Vec2 from = Dist(a, p) < Dist(b, p) ? a : b;
        if (Dist(from, p) > 1e-6) out.push_back(Thin(from, p));
    };
    // The point on XY labelled on the side away from its trace; the trace
    // labelled beyond it, away from XY, with HT or VT beside it.
    auto mark = [&](Vec2 onXY, const wchar_t* n1, Vec2 tr, const wchar_t* n2, const wchar_t* name) {
        const double side = tr.y < xy - 1e-9 ? -1 : 1;
        out.push_back(Thin(onXY, tr));
        out.push_back(Entity::Point(tr));
        out.push_back(MakePointLabel(onXY, n1, h, {-0.5, -side}));
        out.push_back(MakePointLabel(tr, n2, h, {-0.6, side}));
        out.push_back(Note(tr + Vec2{h * 0.8, side * h * 0.3}, name, h, side < 0 ? 0 : 6));
    };
    if (t.hasHT) {
        extend(v.a1, v.b1, t.h1);
        extend(v.a, v.b, t.h);
        mark(t.h1, L"h'", t.h, L"h", L"HT");
    }
    if (t.hasVT) {
        extend(v.a, v.b, t.v);
        extend(v.a1, v.b1, t.v1);
        mark(t.v, L"v", t.v1, L"v'", L"VT");
    }
    return out;
}

// ---------------------------------------------------------------------------
// Transition piece by triangulation
// ---------------------------------------------------------------------------

namespace {
struct P3 {
    double x = 0, y = 0, z = 0;
};
double D3(P3 a, P3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

// The point at r1 from p1 and r2 from p2, on the right of p1 -> p2 (or its left).
Vec2 Apex(Vec2 p1, double r1, Vec2 p2, double r2, bool right) {
    const double d = std::max(Dist(p1, p2), 1e-12);
    const double a = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
    const double hh = std::sqrt(std::max(0.0, r1 * r1 - a * a));
    const Vec2 u = (p2 - p1) / d, n = Perp(u);
    return p1 + u * a + n * (right ? -hh : hh);
}
}  // namespace

bool DevelopTransition(double a, double b, double d, double h, Vec2 at, double th, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!(a > 0) || !(b > 0) || !(d > 0) || !(h > 0)) return why = L"The sizes must be more than zero.", false;
    // Bottom: from the middle of the front side round (anticlockwise seen from above) and back.
    std::vector<P3> bottom = {{0, -b / 2, 0}, {a / 2, -b / 2, 0}, {a / 2, b / 2, 0}, {-a / 2, b / 2, 0}, {-a / 2, -b / 2, 0}, {0, -b / 2, 0}};
    const std::wstring bottomNames[] = {L"M", L"A", L"B", L"C", L"D", L"M"};
    // Top: the circle in 12, from the front.
    std::vector<P3> top;
    for (int k = 0; k <= 12; ++k) {
        const double ang = -kPi / 2 + kTau * k / 12;
        top.push_back({d / 2 * std::cos(ang), d / 2 * std::sin(ang), h});
    }
    // Triangles along the surface: each step goes on along the top or the bottom, the shorter way.
    size_t i = 0, j = 0;
    std::vector<Vec2> t2(top.size()), b2(bottom.size());
    b2[0] = at;
    t2[0] = at + Vec2{0, D3(top[0], bottom[0])};
    std::vector<std::pair<Vec2, Vec2>> rulings = {{b2[0], t2[0]}};
    while (i + 1 < top.size() || j + 1 < bottom.size()) {
        const bool canTop = i + 1 < top.size(), canBottom = j + 1 < bottom.size();
        const bool advanceTop = canTop && (!canBottom || D3(top[i + 1], bottom[j]) <= D3(top[i], bottom[j + 1]));
        if (advanceTop) {
            t2[i + 1] = Apex(b2[j], D3(top[i + 1], bottom[j]), t2[i], D3(top[i + 1], top[i]), true);
            ++i;
        } else {
            b2[j + 1] = Apex(b2[j], D3(bottom[j + 1], bottom[j]), t2[i], D3(bottom[j + 1], top[i]), true);
            ++j;
        }
        rulings.push_back({b2[j], t2[i]});
    }
    // Outline: bottom, top, and the two seam lines; the rulings thin.
    std::vector<Vec2> bp(b2.begin(), b2.end()), tp(t2.begin(), t2.end());
    out.push_back(Poly(bp, false, kThick));
    out.push_back(W(Entity::Spline(tp, false), kThick));
    out.push_back(Thick(rulings.front().first, rulings.front().second));
    out.push_back(Thick(rulings.back().first, rulings.back().second));
    for (size_t k = 1; k + 1 < rulings.size(); ++k) out.push_back(Thin(rulings[k].first, rulings[k].second));
    for (size_t k = 0; k < bp.size(); ++k) out.push_back(MakePointLabel(bp[k], bottomNames[k], th, {0, -1}));
    for (size_t k = 0; k < tp.size(); ++k) out.push_back(MakePointLabel(tp[k], std::to_wstring(k % 12), th * 0.8, {0, 1}));
    const Box box = BoxOf(out);
    out.push_back(Note({box.minX, box.minY - th * 3},
                       L"DEVELOPMENT OF A TRANSITION PIECE BY TRIANGULATION\nrectangle " + Num(a) + L" x " + Num(b) +
                           L" to a circle of diameter " + Num(d) + L", " + Num(h) +
                           L" high; seam at M, the middle of the front side; the circle in 12",
                       th * 0.8));
    return true;
}

// ---------------------------------------------------------------------------
// Intersection of solids
// ---------------------------------------------------------------------------

namespace {
// The upright solid's surface: how far out (x) it is at depth y; false if
// the depth is beyond it.
bool UprightX(const IntersectSpec& s, double y, double& x) {
    const double r = s.uprightSize / 2;
    switch (s.upright) {
        case IntersectSpec::Cylinder:
            if (std::fabs(y) > r) return false;
            x = std::sqrt(r * r - y * y);
            return true;
        case IntersectSpec::Prism:
            if (std::fabs(y) > r) return false;
            x = r;
            return true;
        case IntersectSpec::PrismDiamond: {
            const double c = s.uprightSize / std::sqrt(2.0);
            if (std::fabs(y) > c) return false;
            x = c - std::fabs(y);
            return true;
        }
    }
    return false;
}
double UprightHalf(const IntersectSpec& s) {
    return s.upright == IntersectSpec::PrismDiamond ? s.uprightSize / std::sqrt(2.0) : s.uprightSize / 2;
}
// The horizontal solid's section (depth y, height z), going round it: f in 0..1.
Vec2 AcrossPoint(const IntersectSpec& s, double f) {
    const double r = s.acrossSize / 2;
    const Vec2 c{s.offset, s.axisHeight};
    if (s.across == IntersectSpec::Cylinder) return c + Vec2{r * std::cos(kTau * f), r * std::sin(kTau * f)};
    std::array<Vec2, 4> v;
    if (s.across == IntersectSpec::Prism) v = {Vec2{r, -r}, Vec2{r, r}, Vec2{-r, r}, Vec2{-r, -r}};
    else {
        const double q = s.acrossSize / std::sqrt(2.0);
        v = {Vec2{q, 0}, Vec2{0, q}, Vec2{-q, 0}, Vec2{0, -q}};
    }
    const double t = std::fmod(f, 1.0) * 4;
    const int k = std::min(3, (int)t);
    return c + Lerp(v[(size_t)k], v[(size_t)((k + 1) % 4)], t - k);
}
double AcrossHalfDepth(const IntersectSpec& s) {
    return s.across == IntersectSpec::PrismDiamond ? s.acrossSize / std::sqrt(2.0) : s.acrossSize / 2;
}
}  // namespace

bool IntersectSolids(const IntersectSpec& s, Vec2 at, double gap, double th, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!(s.uprightSize > 0) || !(s.uprightHeight > 0) || !(s.acrossSize > 0) || !(s.acrossLength > 0))
        return why = L"The sizes must be more than zero.", false;
    const double X1 = UprightHalf(s), yr = AcrossHalfDepth(s), zr = AcrossHalfDepth(s);
    const double L2 = s.acrossLength / 2;
    if (std::fabs(s.offset) + yr >= X1 * 0.999 && s.upright != IntersectSpec::Prism)
        return why = L"The horizontal solid must pass right through the upright one: make it thinner or move it nearer the axis.", false;
    if (s.upright == IntersectSpec::Prism && std::fabs(s.offset) + yr > X1)
        return why = L"The horizontal solid must pass right through the upright one: make it thinner or move it nearer the axis.", false;
    if (s.axisHeight - zr <= 0 || s.axisHeight + zr >= s.uprightHeight)
        return why = L"The horizontal solid must stay between the base and the top of the upright one.", false;
    if (L2 <= X1) return why = L"The horizontal solid must be longer than the upright one is wide.", false;
    const Vec2 fv = at, tv = at - Vec2{0, gap};  // front view base centre; top view centre
    // ---- front view ----
    // The upright: its outline, cut where the horizontal solid joins it (at its depth 0).
    double zlo = 1e300, zhi = -1e300;
    for (int k = 0; k < 720; ++k) {
        const Vec2 p = AcrossPoint(s, k / 720.0);
        if (std::fabs(p.x) < AcrossHalfDepth(s) * 0.02 + 1e-9 || (s.across != IntersectSpec::Cylinder && std::fabs(p.x) < 1e-6)) {
            zlo = std::min(zlo, p.y), zhi = std::max(zhi, p.y);
        }
    }
    double ylo = 1e300, yhi = -1e300;  // the horizontal solid's depth range
    for (int k = 0; k < 720; ++k) {
        const Vec2 p = AcrossPoint(s, k / 720.0);
        ylo = std::min(ylo, p.x), yhi = std::max(yhi, p.x);
    }
    const bool coversSilhouette = ylo <= 0 && yhi >= 0;
    for (double sx : {-X1, X1}) {
        if (coversSilhouette && zhi > zlo) {
            out.push_back(Thick(fv + Vec2{sx, 0}, fv + Vec2{sx, zlo}));
            out.push_back(Thick(fv + Vec2{sx, zhi}, fv + Vec2{sx, s.uprightHeight}));
        } else {
            out.push_back(Thick(fv + Vec2{sx, 0}, fv + Vec2{sx, s.uprightHeight}));
        }
    }
    out.push_back(Thick(fv + Vec2{-X1, 0}, fv + Vec2{X1, 0}));
    out.push_back(Thick(fv + Vec2{-X1, s.uprightHeight}, fv + Vec2{X1, s.uprightHeight}));
    if (s.upright == IntersectSpec::PrismDiamond) {  // the front edge
        out.push_back(Thick(fv, fv + Vec2{0, zlo}));
        out.push_back(Thick(fv + Vec2{0, zhi}, fv + Vec2{0, s.uprightHeight}));
    }
    out.push_back(Centre(fv + Vec2{0, -th}, fv + Vec2{0, s.uprightHeight + th}));
    // The horizontal solid: its generators (edges) outside the upright, its ends.
    std::vector<double> gens;  // section parameters of the lines drawn along it
    if (s.across == IntersectSpec::Cylinder) gens = {0.25, 0.75};
    else if (s.across == IntersectSpec::Prism) gens = {0.0, 0.25, 0.5, 0.75};
    else gens = {0.0, 0.25, 0.5, 0.75};
    for (double g : gens) {
        const Vec2 p = AcrossPoint(s, g);
        double xs = 0;
        UprightX(s, p.x, xs);
        const bool behind = p.x > s.offset + 1e-9 && s.across != IntersectSpec::Cylinder;
        auto line = [&](Vec2 a, Vec2 b) { out.push_back(behind ? Hidden(a, b) : Thick(a, b)); };
        if (s.across == IntersectSpec::Prism && (g == 0.0 || g == 0.5)) continue;  // its side faces: seen edge on
        line(fv + Vec2{-L2, p.y}, fv + Vec2{-xs, p.y});
        line(fv + Vec2{xs, p.y}, fv + Vec2{L2, p.y});
    }
    if (s.across == IntersectSpec::Prism) {
        for (double g : {0.125, 0.375, 0.625, 0.875}) {  // its four edges (corners of the square)
            const Vec2 p = AcrossPoint(s, g);
            double xs = 0;
            UprightX(s, p.x, xs);
            auto line = [&](Vec2 a, Vec2 b) { out.push_back(p.x > s.offset ? Hidden(a, b) : Thick(a, b)); };
            line(fv + Vec2{-L2, p.y}, fv + Vec2{-xs, p.y});
            line(fv + Vec2{xs, p.y}, fv + Vec2{L2, p.y});
        }
    }
    for (double ex : {-L2, L2}) out.push_back(Thick(fv + Vec2{ex, s.axisHeight - zr}, fv + Vec2{ex, s.axisHeight + zr}));
    out.push_back(Centre(fv + Vec2{-L2 - th, s.axisHeight}, fv + Vec2{L2 + th, s.axisHeight}));
    // The curves of intersection: on each side, visible at the front, hidden at the back.
    for (int side : {-1, 1}) {
        std::vector<Vec2> run;
        bool runBack = false;
        // With the axes meeting, the hidden half lies right behind the visible one: not drawn.
        const bool hiddenBehind = std::fabs(s.offset) < 1e-9;
        auto flush = [&]() {
            if (run.size() >= 2 && !(runBack && hiddenBehind)) {
                Entity e = Poly(run, false, kThick);
                if (runBack) e.linetype = (int16_t)std::max(0, FindLineType(L"HIDDEN2")), e.lineweight = 25;
                out.push_back(e);
            }
            run.clear();
        };
        const int n = 720;
        for (int k = 0; k <= n; ++k) {
            const Vec2 p = AcrossPoint(s, (double)k / n);
            double xs;
            if (!UprightX(s, p.x, xs)) {
                flush();
                continue;
            }
            const Vec2 q = fv + Vec2{side * xs, p.y};
            const bool back = p.x > 1e-9;
            if (!run.empty() && back != runBack) {  // the next run starts where this one ends
                const Vec2 last = run.back();
                flush();
                run.push_back(last);
            }
            runBack = back;
            run.push_back(q);
        }
        flush();
    }
    // ---- top view ----
    if (s.upright == IntersectSpec::Cylinder) out.push_back(W(Entity::Circle(tv, X1), kThick));
    else if (s.upright == IntersectSpec::Prism) out.push_back(Poly({tv + Vec2{-X1, -X1}, tv + Vec2{X1, -X1}, tv + Vec2{X1, X1}, tv + Vec2{-X1, X1}}, true, kThick));
    else out.push_back(Poly({tv + Vec2{X1, 0}, tv + Vec2{0, X1}, tv + Vec2{-X1, 0}, tv + Vec2{0, -X1}}, true, kThick));
    // The horizontal solid seen from above: its outline (depth = -y on paper) outside the upright.
    std::vector<double> plan = {s.offset - yr, s.offset + yr};
    if (s.across != IntersectSpec::Cylinder) plan.push_back(s.offset);  // its top edge (diamond) or face lines
    for (double y : plan) {
        if (s.across == IntersectSpec::Prism && std::fabs(y - s.offset) < 1e-9) continue;
        double xs = 0;
        UprightX(s, y, xs);
        out.push_back(Thick(tv + Vec2{-L2, -y}, tv + Vec2{-xs, -y}));
        out.push_back(Thick(tv + Vec2{xs, -y}, tv + Vec2{L2, -y}));
    }
    for (double ex : {-L2, L2}) out.push_back(Thick(tv + Vec2{ex, -s.offset - yr}, tv + Vec2{ex, -s.offset + yr}));
    out.push_back(Centre(tv + Vec2{-L2 - th, -s.offset}, tv + Vec2{L2 + th, -s.offset}));
    out.push_back(Centre(tv + Vec2{0, -X1 - th}, tv + Vec2{0, X1 + th}));
    static const wchar_t* const names[] = {L"cylinder", L"square prism", L"square prism (corner to the front)"};
    const Box box = BoxOf(out);
    out.push_back(Note({box.minX, box.minY - th * 3},
                       std::wstring(L"INTERSECTION OF SOLIDS: an upright ") + names[s.upright] + L" (" + Num(s.uprightSize) + L") and a horizontal " +
                           names[s.across] + L" (" + Num(s.acrossSize) + L"), axes square to each other, the horizontal axis " +
                           Num(s.axisHeight) + L" above the base" + (std::fabs(s.offset) > 1e-9 ? L" and " + Num(std::fabs(s.offset)) + L" " + (s.offset > 0 ? L"behind" : L"in front of") + L" the upright one" : L"") +
                           L"\nthe curves of intersection: visible thick, hidden dashed",
                       th * 0.8));
    return true;
}

// ---------------------------------------------------------------------------
// Perspective
// ---------------------------------------------------------------------------

Vec2 PerspectivePoint(double x, double y, double z, double sx, double D, double E) {
    const double k = D / (D + y);
    return {sx + (x - sx) * k, E + (z - E) * k};
}

bool PerspectiveDrawing(const PerspectiveSpec& s, Vec2 at, double th, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    if (!(s.length > 0) || !(s.width > 0) || !(s.height > 0)) return why = L"The block's sizes must be more than zero.", false;
    if (!(s.angle > 0) || !(s.angle < 90)) return why = L"The angle to the picture plane must be between 0 and 90 degrees.", false;
    if (!(s.distance > 0)) return why = L"The station point must be in front of the picture plane.", false;
    const double t = ToRadians(s.angle);
    const Vec2 u1{std::cos(t), std::sin(t)}, u2{-std::sin(t), std::cos(t)};
    // Plan corners (x along PP, y behind it), A touching the picture plane.
    const Vec2 A{0, 0}, B = A + u1 * s.length, D2 = A + u2 * s.width, C = B + u2 * s.width;
    const std::array<Vec2, 4> base = {A, B, C, D2};
    const Vec2 sp{s.sideways, -s.distance};
    // Plan: the block above PP, SP below.
    auto P = [&](Vec2 q) { return at + q; };
    out.push_back(Poly({P(A), P(B), P(C), P(D2)}, true, kThin));
    double xmin = std::min({A.x, B.x, C.x, D2.x, sp.x}), xmax = std::max({A.x, B.x, C.x, D2.x, sp.x});
    // Vanishing points: lines from SP parallel to the sides, to the picture plane.
    const double v1 = sp.x + s.distance * u1.x / u1.y, v2 = sp.x + s.distance * u2.x / u2.y;
    xmin = std::min({xmin, v1, v2}) - 10, xmax = std::max({xmax, v1, v2}) + 10;
    out.push_back(Thin(P({xmin, 0}), P({xmax, 0})));
    out.push_back(Note(P({xmin - th * 0.5, 0}), L"PP", th, 5));
    out.push_back(Entity::Point(P(sp)));
    out.push_back(MakePointLabel(P(sp), L"SP", th, {0, -1}));
    out.push_back(Thin(P(sp), P({v1, 0})));
    out.push_back(Thin(P(sp), P({v2, 0})));
    // Visual rays from SP to the corners; where they cross PP, projectors down.
    // The ground line: below SP, far enough for the view (up to the horizon) to clear it.
    const double gl = -s.distance - std::max(s.height, s.eye) - std::max(25.0, th * 6);
    auto persp = [&](Vec2 q, double z) {
        const Vec2 p = PerspectivePoint(q.x, q.y, z, s.sideways, s.distance, s.eye);
        return at + Vec2{p.x, gl + p.y};
    };
    static const wchar_t* const names[] = {L"a", L"b", L"c", L"d"};
    for (size_t k = 0; k < 4; ++k) {
        const Vec2 q = base[k];
        out.push_back(Thin(P(sp), P(q)));
        const Vec2 pp = PerspectivePoint(q.x, q.y, 0, s.sideways, s.distance, s.eye);
        out.push_back(Thin(P({pp.x, 0}), at + Vec2{pp.x, gl}));  // the projector, down to the ground line
        out.push_back(MakePointLabel(P(q), names[k], th * 0.8, q - (A + B + C + D2) * 0.25));
    }
    // Ground line, horizon line, vanishing points.
    out.push_back(Thin(at + Vec2{xmin, gl}, at + Vec2{xmax, gl}));
    out.push_back(Note(at + Vec2{xmin - th * 0.5, gl}, L"GL", th, 5));
    out.push_back(Thin(at + Vec2{xmin, gl + s.eye}, at + Vec2{xmax, gl + s.eye}));
    out.push_back(Note(at + Vec2{xmin - th * 0.5, gl + s.eye}, L"HL", th, 5));
    out.push_back(Thin(P({v1, 0}), at + Vec2{v1, gl + s.eye}));
    out.push_back(Thin(P({v2, 0}), at + Vec2{v2, gl + s.eye}));
    out.push_back(Entity::Point(at + Vec2{v1, gl + s.eye}));
    out.push_back(Entity::Point(at + Vec2{v2, gl + s.eye}));
    out.push_back(MakePointLabel(at + Vec2{v1, gl + s.eye}, L"VP1", th, {0, -1}));
    out.push_back(MakePointLabel(at + Vec2{v2, gl + s.eye}, L"VP2", th, {0, -1}));
    // The block: the edges of the faces turned to the eye.
    struct Face {
        std::array<int, 4> c;  // corners: 0-3 base, 4-7 top
        double nx, ny, nz;
    };
    auto corner = [&](int i) {
        const Vec2 q = base[(size_t)(i % 4)];
        return std::array<double, 3>{q.x, q.y, i < 4 ? 0.0 : s.height};
    };
    const std::array<Face, 6> faces = {{
        {{0, 1, 2, 3}, 0, 0, -1},
        {{4, 5, 6, 7}, 0, 0, 1},
        {{0, 1, 5, 4}, -u2.x, -u2.y, 0},
        {{1, 2, 6, 5}, u1.x, u1.y, 0},
        {{2, 3, 7, 6}, u2.x, u2.y, 0},
        {{3, 0, 4, 7}, -u1.x, -u1.y, 0},
    }};
    std::map<std::pair<int, int>, bool> edges;
    for (const Face& f : faces) {
        const auto c0 = corner(f.c[0]);
        const double vx = s.sideways - c0[0], vy = -s.distance - c0[1], vz = s.eye - c0[2];
        const bool seen = f.nx * vx + f.ny * vy + f.nz * vz > 1e-9;
        for (int k = 0; k < 4; ++k) {
            int a = f.c[(size_t)k], b = f.c[(size_t)((k + 1) % 4)];
            if (a > b) std::swap(a, b);
            edges[{a, b}] = edges[{a, b}] || seen;
        }
    }
    for (const auto& e : edges) {
        if (!e.second) continue;
        const auto a = corner(e.first.first), b = corner(e.first.second);
        out.push_back(Thick(persp({a[0], a[1]}, a[2]), persp({b[0], b[1]}, b[2])));
    }
    const Box box = BoxOf(out);
    out.push_back(Note({box.minX, box.minY - th * 3},
                       L"PERSPECTIVE OF A BLOCK " + Num(s.length) + L" x " + Num(s.width) + L" x " + Num(s.height) +
                           L" by the visual ray method\nits length at " + Num(s.angle, 1) + L"\x00B0 to PP, an edge touching it; SP " +
                           Num(s.distance) + L" in front of PP and " + Num(s.sideways) + L" to the right; eye " + Num(s.eye) +
                           L" above the ground; the edge touching PP shows its true height",
                       th * 0.8));
    return true;
}
