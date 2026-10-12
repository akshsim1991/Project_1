// EngGraphics.cpp - see EngGraphics.h.
#include "EngGraphics.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

#include "Curve.h"
#include "Dimension.h"
#include "Style.h"

namespace {
constexpr int kThin = 18, kMedium = 35, kThick = 50;  // line weights (hundredths of a millimetre)

Entity Weighted(Entity e, int lw) {
    e.lineweight = (int16_t)lw;
    return e;
}

Entity Note(Vec2 at, const std::wstring& s, double h, int justify = 0) {
    TextData t;
    t.content = s;
    t.height = h;
    t.justify = justify;
    return MakeText(at, t);
}

std::wstring Num(double v, int decimals = 1) {
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%.*f", decimals, std::fabs(v) < 0.5 * std::pow(10.0, -decimals) ? 0.0 : v);
    std::wstring s = buf;
    if (s.find(L'.') != std::wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    return s;
}

std::wstring Deg(double rad) { return Num(ToDegrees(rad), 1) + L"\x00B0"; }
}  // namespace

// ---------------------------------------------------------------------------
// Isometric drawing
// ---------------------------------------------------------------------------

const wchar_t* IsoPlaneName(IsoPlane p) {
    switch (p) {
        case IsoPlane::Left: return L"Left";
        case IsoPlane::Top: return L"Top";
        case IsoPlane::Right: return L"Right";
    }
    return L"";
}

IsoPlane NextIsoPlane(IsoPlane p) {
    return p == IsoPlane::Left ? IsoPlane::Top : p == IsoPlane::Top ? IsoPlane::Right : IsoPlane::Left;
}

void IsoAxes(IsoPlane p, Vec2& u, Vec2& v) {
    const Vec2 a30{std::cos(kPi / 6), 0.5}, a150{-std::cos(kPi / 6), 0.5}, up{0, 1};
    switch (p) {
        case IsoPlane::Left: u = a150, v = up; break;
        case IsoPlane::Top: u = a30, v = a150; break;
        case IsoPlane::Right: u = a30, v = up; break;
    }
}

Vec2 IsoGridPoint(Vec2 p, Vec2 origin, double spacing) {
    if (!(spacing > 0)) return p;
    const double c = std::cos(kPi / 6);
    const Vec2 a30{c, 0.5}, a150{-c, 0.5};
    const Vec2 d = p - origin;
    // p - origin = a * a30 + b * a150.
    const double a = (d.x / c + d.y / 0.5) / 2 / spacing, b = (d.y / 0.5 - d.x / c) / 2 / spacing;
    Vec2 best = p;
    double bestD = 1e300;
    for (double i : {std::floor(a), std::ceil(a)})
        for (double j : {std::floor(b), std::ceil(b)}) {
            const Vec2 q = origin + a30 * (i * spacing) + a150 * (j * spacing);
            if (const double dd = Dist(q, p); dd < bestD) bestD = dd, best = q;
        }
    return best;
}

Vec2 IsoOrtho(Vec2 base, Vec2 p, IsoPlane plane) {
    Vec2 u, v;
    IsoAxes(plane, u, v);
    const Vec2 d = p - base;
    const Vec2 pu = base + u * Dot(d, u), pv = base + v * Dot(d, v);
    return Dist(pu, p) <= Dist(pv, p) ? pu : pv;
}

bool IsIsoAxis(double angle) {
    double deg = std::fmod(ToDegrees(angle), 360.0);
    if (deg < 0) deg += 360;
    const double off = std::fmod(deg + 30, 60.0);  // 30, 90, 150... -> 0
    return std::min(off, 60 - off) < 1e-6;
}

Entity IsoCircle(Vec2 centre, double r, IsoPlane plane) {
    const double major = r * std::sqrt(1.5);
    const double dir = plane == IsoPlane::Top ? 0 : plane == IsoPlane::Left ? kPi * 2 / 3 : kPi / 3;
    return Entity::Ellipse(centre, Vec2{std::cos(dir), std::sin(dir)} * major, 1 / std::sqrt(3.0));
}

std::vector<Entity> IsoScaleDrawing(Vec2 at, double length, double step, double h) {
    std::vector<Entity> out;
    if (!(length > 0) || !(step > 0)) return out;
    const Vec2 u45{std::cos(kPi / 4), std::sin(kPi / 4)}, u30{std::cos(kPi / 6), std::sin(kPi / 6)};
    const double isoLen = length * kIsoScale;
    out.push_back(Weighted(Entity::Line(at, at + u45 * length), kMedium));
    out.push_back(Weighted(Entity::Line(at, at + u30 * isoLen), kMedium));
    const int n = (int)std::floor(length / step + 1e-9);
    const double tick = h * 0.6;
    for (int k = 0; k <= n; ++k) {
        const double s = std::min(k * step, length);
        const Vec2 p = at + u45 * s;
        const Vec2 q = at + u30 * (s * kIsoScale);  // straight below p
        if (k > 0) out.push_back(Weighted(Entity::Line(p, q), kThin));
        out.push_back(Weighted(Entity::Line(p - Perp(u45) * tick, p + Perp(u45) * tick), kThin));
        out.push_back(Weighted(Entity::Line(q - Perp(u30) * tick, q + Perp(u30) * tick), kThin));
        const std::wstring label = Num(s, 1);
        Entity t1 = Note(p + Perp(u45) * (tick + h * 0.8), label, h * 0.7, 4);
        EditAnnot(t1).text.rotation = kPi / 4;
        out.push_back(t1);
        Entity t2 = Note(q - Perp(u30) * (tick + h * 0.8), label, h * 0.7, 4);
        EditAnnot(t2).text.rotation = kPi / 6;
        out.push_back(t2);
    }
    Entity a = Note(at + u45 * (length * 0.5) + Perp(u45) * (tick + h * 2.4), L"TRUE LENGTHS", h, 4);
    EditAnnot(a).text.rotation = kPi / 4;
    out.push_back(a);
    Entity b = Note(at + u30 * (isoLen * 0.5) - Perp(u30) * (tick + h * 2.4), L"ISOMETRIC LENGTHS", h, 4);
    EditAnnot(b).text.rotation = kPi / 6;
    out.push_back(b);
    out.push_back(Note(at + Vec2{h * 4.5, h * 2.6}, L"45\x00B0", h * 0.8, 4));
    out.push_back(Note(at + Vec2{h * 5.5, h * 0.9}, L"30\x00B0", h * 0.8, 4));
    out.push_back(Note(at + Vec2{0, -h * 3}, L"ISOMETRIC SCALE (isometric length = 0.816 \x00D7 true length)", h));
    return out;
}

// ---------------------------------------------------------------------------
// Dividing curves
// ---------------------------------------------------------------------------

namespace {
// The object as a curve measured along its length (ellipses and splines as
// fine polylines); `closed` if it goes round.
bool AsCurve(const Entity& e, Entity& c) {
    if (IsCurve(e)) {
        c = e;
        return true;
    }
    std::vector<Vec2> pts;
    bool closed = false;
    if (e.type == EntityType::Ellipse) {
        EllipsePoints(e, pts);
        closed = e.closed;
    } else if (e.type == EntityType::Spline) {
        SplinePoints(e, pts);
        closed = e.closed && !SplineTrimmed(e);
    } else {
        return false;
    }
    if (closed && pts.size() > 2 && Near(pts.front(), pts.back(), 1e-9 * (1 + Length(pts.front())))) pts.pop_back();
    if (pts.size() < 2) return false;
    std::vector<Vertex> v;
    for (Vec2 p : pts) v.push_back({p, 0});
    c = Entity::Polyline(v, closed);
    return true;
}
}  // namespace

bool Dividable(const Entity& e) {
    Entity c;
    return AsCurve(e, c) && CurveLength(c) > 1e-9;
}

bool DividePoints(const Entity& e, int parts, Vec2 from, std::vector<Vec2>& out) {
    out.clear();
    Entity c;
    if (parts < 2 || !AsCurve(e, c)) return false;
    const double L = CurveLength(c);
    if (!(L > 1e-9)) return false;
    if (IsClosedCurve(c)) {
        const double s0 = CurveParam(c, from);
        for (int k = 0; k < parts; ++k) out.push_back(CurvePoint(c, std::fmod(s0 + L * k / parts, L)));
    } else {
        const bool reverse = Dist(from, CurvePoint(c, L)) < Dist(from, CurvePoint(c, 0));
        for (int k = 1; k < parts; ++k) out.push_back(CurvePoint(c, reverse ? L - L * k / parts : L * k / parts));
    }
    return true;
}

bool MeasurePoints(const Entity& e, double step, Vec2 from, std::vector<Vec2>& out) {
    out.clear();
    Entity c;
    if (!(step > 0) || !AsCurve(e, c)) return false;
    const double L = CurveLength(c);
    if (!(L > 1e-9) || L / step > 10000) return false;
    const double eps = 1e-9 * (1 + L);
    if (IsClosedCurve(c)) {
        const double s0 = CurveParam(c, from);
        for (int k = 0; k * step < L - eps; ++k) out.push_back(CurvePoint(c, std::fmod(s0 + k * step, L)));
    } else {
        const bool reverse = Dist(from, CurvePoint(c, L)) < Dist(from, CurvePoint(c, 0));
        for (int k = 1; k * step < L - eps; ++k) out.push_back(CurvePoint(c, reverse ? L - k * step : k * step));
    }
    return true;
}

Entity MakePointLabel(Vec2 point, const std::wstring& text, double h, Vec2 dir) {
    dir = Length(dir) > 1e-12 ? Normalized(dir) : Vec2{1, 1} * std::sqrt(0.5);
    TextData t;
    t.content = text;
    t.height = h;
    t.frame = kFrameLabel;
    t.justify = 4;
    const double w = TextWidth(text, h) * 0.8;
    return MakeText(point + dir * (h * 0.9) + Vec2{dir.x * w / 2, 0}, t);
}

std::wstring NextName(const std::wstring& s) {
    if (s.empty()) return L"1";
    // A number at the end counts up: 9 -> 10, a9 -> a10.
    size_t i = s.size();
    while (i > 0 && std::iswdigit(s[i - 1])) --i;
    if (i < s.size()) return s.substr(0, i) + std::to_wstring(std::stoll(s.substr(i)) + 1);
    std::wstring r = s;
    for (size_t k = r.size(); k-- > 0;) {
        wchar_t& c = r[k];
        if ((c >= L'a' && c < L'z') || (c >= L'A' && c < L'Z')) return ++c, r;
        if (c == L'z' || c == L'Z') return c = (wchar_t)(c - 25), r;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Developments
// ---------------------------------------------------------------------------

namespace {
struct Vec3 {
    double x = 0, y = 0, z = 0;
};
Vec3 Mix(Vec3 a, Vec3 b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

// A face of the solid and where it lies in the development, corner for corner.
struct Face {
    std::vector<Vec3> p;
    std::vector<Vec2> q;
};

// What is kept: on the base side of the cutting plane (>= 0).
double Keep(const SolidSpec& s, Vec3 p) {
    if (!s.cut) return 1;
    return s.cutHeight + std::tan(ToRadians(s.cutAngle)) * p.x - p.z;
}

// The part of a face kept (clipped by the cutting plane), mapped the same way.
Face Clip(const SolidSpec& s, const Face& f) {
    Face out;
    const size_t n = f.p.size();
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        const double ki = Keep(s, f.p[i]), kj = Keep(s, f.p[j]);
        if (ki >= 0) out.p.push_back(f.p[i]), out.q.push_back(f.q[i]);
        if ((ki >= 0) != (kj >= 0)) {
            const double t = ki / (ki - kj);
            out.p.push_back(Mix(f.p[i], f.p[j], t));
            out.q.push_back(Lerp(f.q[i], f.q[j], t));
        }
    }
    return out;
}

// How far along a generator from base to top the kept part reaches (0..1).
double KeptFraction(const SolidSpec& s, Vec3 base, Vec3 top) {
    const double k0 = Keep(s, base), k1 = Keep(s, top);
    if (k1 >= 0) return 1;
    if (k0 <= 0) return 0;
    return k0 / (k0 - k1);
}

bool OnSegmentLine(Vec2 p, Vec2 a, Vec2 b, double tol) {
    const Vec2 d = b - a;
    const double len = Length(d);
    if (len < 1e-12) return Dist(p, a) <= tol;
    return std::fabs(Cross(d / len, p - a)) <= tol;
}
}  // namespace

std::wstring SolidName(const SolidSpec& s) {
    static const wchar_t* const poly[] = {L"triangular", L"square", L"pentagonal", L"hexagonal", L"heptagonal", L"octagonal"};
    std::wstring shape = s.sides >= 3 && s.sides <= 8 ? poly[s.sides - 3] : std::to_wstring(s.sides) + L"-sided";
    switch (s.kind) {
        case SolidSpec::Prism: return shape + L" prism";
        case SolidSpec::Pyramid: return shape + L" pyramid";
        case SolidSpec::Cylinder: return L"cylinder";
        case SolidSpec::Cone: return L"cone";
    }
    return L"";
}

bool DevelopSolid(const SolidSpec& s, Vec2 at, double h, std::vector<Entity>& out, std::wstring& why) {
    out.clear();
    const bool polygonal = s.kind == SolidSpec::Prism || s.kind == SolidSpec::Pyramid;
    if (polygonal && (s.sides < 3 || s.sides > 12)) return why = L"The base needs 3 to 12 sides.", false;
    if (!(s.base > 0) || !(s.height > 0)) return why = L"The sizes must be more than zero.", false;
    if (!polygonal && (s.divisions < 4 || s.divisions > 48)) return why = L"Divide the base into 4 to 48 parts.", false;
    if (s.cut && std::fabs(s.cutAngle) >= 89.9) return why = L"The cutting plane must be less than 90\x00B0 to HP.", false;
    const int n = polygonal ? s.sides : s.divisions;
    const double R = polygonal ? s.base / (2 * std::sin(kPi / n)) : s.base / 2;
    const bool apexed = s.kind == SolidSpec::Pyramid || s.kind == SolidSpec::Cone;

    // Corners (or generators) round the base, starting at the leftmost (the
    // front one of two), going round through the front.
    std::vector<double> phi(n);
    for (int k = 0; k < n; ++k) {
        if (!polygonal) phi[k] = kPi + kTau * k / n;
        else phi[k] = -kPi / 2 + (s.cornerFront ? 0 : kPi / n) + kTau * k / n;
    }
    if (polygonal) {
        int start = 0;
        for (int k = 1; k < n; ++k) {
            const double xk = std::cos(phi[k]), xs = std::cos(phi[start]);
            if (xk < xs - 1e-9 || (xk < xs + 1e-9 && std::sin(phi[k]) < std::sin(phi[start]))) start = k;
        }
        std::rotate(phi.begin(), phi.begin() + start, phi.end());
        for (int k = 1; k < n; ++k)
            while (phi[k] < phi[k - 1]) phi[k] += kTau;
    }
    auto basePt = [&](double a) { return Vec3{R * std::cos(a), R * std::sin(a), 0}; };
    auto topPt = [&](double a) { return apexed ? Vec3{0, 0, s.height} : Vec3{R * std::cos(a), R * std::sin(a), s.height}; };
    auto name = [&](int k) {
        if (!polygonal) return std::to_wstring(k % n + 1);
        return std::wstring(1, (wchar_t)(L'A' + k % n));
    };
    const double slant = std::hypot(R, s.height);  // pyramid: its edges; cone: its generators
    const double tol = 1e-7 * (1 + s.base + s.height);
    std::vector<Entity> thick, thin;
    Box box;

    if (s.kind == SolidSpec::Prism || s.kind == SolidSpec::Pyramid) {
        // Each face laid flat, clipped by the cutting plane.
        std::vector<Vec2> fold(n + 1);
        Vec2 apex2;
        if (s.kind == SolidSpec::Prism) {
            for (int k = 0; k <= n; ++k) fold[k] = at + Vec2{k * s.base, 0};
        } else {
            const double beta = 2 * std::asin(s.base / (2 * slant));
            apex2 = at + Vec2{slant, slant};
            const double a0 = -kPi / 2 - n * beta / 2;
            for (int k = 0; k <= n; ++k) fold[k] = Polar(apex2, slant, a0 + k * beta);
        }
        for (int k = 0; k < n; ++k) {
            const double a = phi[k], b = phi[(k + 1) % n];
            Face f;
            f.p.push_back(basePt(a));
            f.p.push_back(basePt(b));
            f.q.push_back(fold[k]);
            f.q.push_back(fold[k + 1]);
            if (s.kind == SolidSpec::Prism) {
                f.p.push_back(topPt(b));
                f.p.push_back(topPt(a));
                f.q.push_back(fold[k + 1] + Vec2{0, s.height});
                f.q.push_back(fold[k] + Vec2{0, s.height});
            } else {
                f.p.push_back(topPt(a));
                f.q.push_back(apex2);
            }
            const Face c = Clip(s, f);
            const size_t m = c.q.size();
            if (m < 3) continue;
            // The face's two folds: thick where they are the ends of the development.
            const Vec2 l0 = f.q[0], l1 = f.q.back(), r0 = f.q[1], r1 = f.q[2];
            for (size_t i = 0; i < m; ++i) {
                const Vec2 p = c.q[i], q = c.q[(i + 1) % m];
                if (Near(p, q, tol)) continue;
                box.Add(p);
                const bool left = OnSegmentLine(p, l0, l1, tol) && OnSegmentLine(q, l0, l1, tol);
                const bool right = OnSegmentLine(p, r0, r1, tol) && OnSegmentLine(q, r0, r1, tol);
                if (left && k > 0) continue;  // drawn with the face before
                if ((left && k == 0) || (right && k == n - 1) || (!left && !right)) thick.push_back(Entity::Line(p, q));
                else thin.push_back(Entity::Line(p, q));
            }
        }
        for (int k = 0; k <= n; ++k) {
            const Vec2 out2 = s.kind == SolidSpec::Prism ? Vec2{0, -1} : Normalized(fold[k] - apex2);
            out.push_back(MakePointLabel(fold[k], name(k), h, out2));
        }
        if (s.kind == SolidSpec::Pyramid) {
            out.push_back(MakePointLabel(apex2, L"O", h, {0, 1}));
            Entity arc = Entity::Arc(apex2, slant, AngleOf(fold[0] - apex2), AngleOf(fold[n] - apex2));
            arc.lineweight = kThin;
            arc.construction = false;
            if (s.cut) thin.push_back(arc);  // the base arc, for reference
        }
    } else {
        // Cylinder and cone: generators, and the cut traced finely between them.
        const int fine = n * 8;
        std::vector<Vec2> curve;
        std::vector<Vec2> base2(fine + 1), top2(fine + 1);
        Vec2 apex2;
        const double width = kPi * s.base;
        const double theta = kTau * R / slant;  // the cone's sector angle
        if (s.kind == SolidSpec::Cone) apex2 = at + Vec2{slant, slant};
        for (int j = 0; j <= fine; ++j) {
            const double a = kPi + kTau * j / fine;
            const double t = KeptFraction(s, basePt(a), topPt(a));
            if (s.kind == SolidSpec::Cylinder) {
                base2[j] = at + Vec2{width * j / fine, 0};
                top2[j] = base2[j] + Vec2{0, s.height * t};
            } else {
                const double ang = -kPi / 2 - theta / 2 + theta * j / fine;
                base2[j] = Polar(apex2, slant, ang);
                top2[j] = Polar(apex2, slant * (1 - t), ang);
            }
            box.Add(base2[j]);
            box.Add(top2[j]);
        }
        // Outline: base, the two ends, and the top (or the cut).
        if (s.kind == SolidSpec::Cylinder) {
            thick.push_back(Entity::Line(base2[0], base2[fine]));
        } else {
            thick.push_back(Entity::Arc(apex2, slant, AngleOf(base2[0] - apex2), AngleOf(base2[fine] - apex2)));
        }
        thick.push_back(Entity::Line(base2[0], top2[0]));
        thick.push_back(Entity::Line(base2[fine], top2[fine]));
        bool degenerate = true;
        for (int j = 1; j <= fine; ++j) degenerate &= Near(top2[j], top2[0], tol);
        if (!degenerate) {
            std::vector<Vertex> v;
            for (int j = 0; j <= fine; ++j)
                if (v.empty() || !Near(v.back().p, top2[j], tol)) v.push_back({top2[j], 0});
            if (v.size() == 2) thick.push_back(Entity::Line(v[0].p, v[1].p));
            else if (v.size() > 2) thick.push_back(Entity::Polyline(v, false));
        }
        for (int k = 0; k <= n; ++k) {
            const int j = k * 8;
            if (k > 0 && k < n && !Near(base2[j], top2[j], tol)) thin.push_back(Entity::Line(base2[j], top2[j]));
            const Vec2 dir = s.kind == SolidSpec::Cylinder ? Vec2{0, -1} : Normalized(base2[j] - apex2);
            out.push_back(MakePointLabel(base2[j], name(k), h, dir));
        }
        if (s.kind == SolidSpec::Cone) out.push_back(MakePointLabel(apex2, L"O", h, {0, 1}));
    }
    for (Entity& e : thick) out.push_back(Weighted(e, kThick));
    for (Entity& e : thin) out.push_back(Weighted(e, kThin));

    // What it is.
    std::wstring note = L"DEVELOPMENT OF THE LATERAL SURFACE OF A " + SolidName(s) + L"\n";
    if (polygonal) note += L"base side " + Num(s.base, 2) + L", axis " + Num(s.height, 2);
    else note += L"base diameter " + Num(s.base, 2) + L", axis " + Num(s.height, 2);
    if (s.kind == SolidSpec::Pyramid) note += L", slant edge " + Num(slant, 2);
    if (s.kind == SolidSpec::Cone)
        note += L"\nslant height L = " + Num(slant, 2) + L", sector angle = 360 \x00D7 r / L = 360 \x00D7 " + Num(R, 2) +
                L" / " + Num(slant, 2) + L" = " + Num(360 * R / slant, 1) + L"\x00B0";
    if (s.kind == SolidSpec::Cylinder) note += L"\nlength = \x03C0 \x00D7 d = " + Num(kPi * s.base, 2);
    if (s.cut)
        note += L"\ncut by a plane square to VP, " + Num(s.cutAngle, 1) + L"\x00B0 to HP, meeting the axis " +
                Num(s.cutHeight, 2) + L" above the base";
    out.push_back(Note(Vec2{box.Empty() ? at.x : box.minX, (box.Empty() ? at.y : box.minY) - h * 3}, note, h * 0.8));
    return true;
}

// ---------------------------------------------------------------------------
// True length
// ---------------------------------------------------------------------------

TrueLengthResult TrueLengthOf(const LineViews& v) {
    TrueLengthResult r;
    const double dx = std::fabs(((v.b1.x - v.a1.x) + (v.b.x - v.a.x)) / 2);
    const double dh = std::fabs(v.b1.y - v.a1.y), dd = std::fabs(v.b.y - v.a.y);
    r.tl = std::sqrt(dx * dx + dh * dh + dd * dd);
    if (r.tl > 1e-12) {
        r.theta = std::asin(std::min(1.0, dh / r.tl));
        r.phi = std::asin(std::min(1.0, dd / r.tl));
    }
    r.alpha = std::atan2(dh, dx);
    r.beta = std::atan2(dd, dx);
    r.fvLength = std::hypot(dx, dh);
    r.tvLength = std::hypot(dx, dd);
    return r;
}

bool ViewsFromTrueLength(Vec2 a1, Vec2 a, double tl, double theta, double phi, bool toRight, LineViews& out,
                         std::wstring& why) {
    if (!(tl > 0)) return why = L"The true length must be more than zero.", false;
    if (theta < 0 || phi < 0 || theta + phi > kPi / 2 + 1e-9)
        return why = L"The angles to HP and VP must add up to 90\x00B0 or less.", false;
    const double dh = tl * std::sin(theta), dd = tl * std::sin(phi);
    const double dx = std::sqrt(std::max(0.0, tl * tl - dh * dh - dd * dd));
    const double s = toRight ? 1 : -1;
    out.a1 = a1;
    out.a = {a1.x, a.y};
    out.b1 = a1 + Vec2{s * dx, dh};
    out.b = out.a + Vec2{s * dx, -dd};
    return true;
}

std::vector<Entity> TrueLengthConstruction(const LineViews& v, double h, bool withViews) {
    std::vector<Entity> out;
    const TrueLengthResult r = TrueLengthOf(v);
    const double s = (v.b1.x - v.a1.x) + (v.b.x - v.a.x) >= 0 ? 1 : -1;
    auto thin = [&](Vec2 p, Vec2 q) {
        if (!Near(p, q, 1e-9)) out.push_back(Weighted(Entity::Line(p, q), kThin));
    };
    auto arcAbout = [&](Vec2 c, Vec2 from, Vec2 to) {
        const double rr = Dist(c, from);
        if (rr < 1e-9) return;
        double a0 = AngleOf(from - c), a1 = AngleOf(to - c);
        if (SweepCCW(a0, a1) > kPi) std::swap(a0, a1);
        out.push_back(Weighted(Entity::Arc(c, rr, a0, a1), kThin));
    };
    if (withViews) {
        out.push_back(Weighted(Entity::Line(v.a1, v.b1), kThick));
        out.push_back(Weighted(Entity::Line(v.a, v.b), kThick));
        thin(v.a1, v.a);
        thin(v.b1, v.b);
    }
    // Top view turned level about a, projected up to b's level in the front view: theta.
    const Vec2 b1 = v.a + Vec2{s * r.tvLength, 0};
    const Vec2 b1f = {b1.x, v.b1.y};
    arcAbout(v.a, v.b, b1);
    thin(v.a, b1);
    thin(b1, b1f);
    thin(v.b1, b1f);  // locus of b'
    out.push_back(Weighted(Entity::Line(v.a1, b1f), kMedium));
    // Front view turned level about a', projected down to b's level in the top view: phi.
    const Vec2 b2f = v.a1 + Vec2{s * r.fvLength, 0};
    const Vec2 b2 = {b2f.x, v.b.y};
    arcAbout(v.a1, v.b1, b2f);
    thin(v.a1, b2f);
    thin(b2f, b2);
    thin(v.b, b2);  // locus of b
    out.push_back(Weighted(Entity::Line(v.a, b2), kMedium));
    // Names.
    const Vec2 left{-s, 0}, right{s, 0};
    out.push_back(MakePointLabel(v.a1, L"a'", h, left + Vec2{0, 0.4}));
    out.push_back(MakePointLabel(v.b1, L"b'", h, Vec2{-s * 0.4, 1}));
    out.push_back(MakePointLabel(v.a, L"a", h, left + Vec2{0, -0.4}));
    out.push_back(MakePointLabel(v.b, L"b", h, Vec2{-s * 0.4, -1}));
    out.push_back(MakePointLabel(b1, L"b1", h, right + Vec2{0, -0.6}));
    out.push_back(MakePointLabel(b1f, L"b1'", h, right + Vec2{0, 0.6}));
    out.push_back(MakePointLabel(b2f, L"b2'", h, right + Vec2{0, 0.6}));
    out.push_back(MakePointLabel(b2, L"b2", h, right + Vec2{0, -0.6}));
    // Angle marks at a' and a.
    const double rr = std::max(h * 3, std::min(r.tl * 0.25, h * 8));
    auto angleMark = [&](Vec2 c, double ang, bool down, const std::wstring& label) {
        if (ang < 1e-6) return;
        const double from = s > 0 ? (down ? -ang : 0) : (down ? kPi : kPi - ang);
        out.push_back(Weighted(Entity::Arc(c, rr, from, from + ang), kThin));
        const double mid = from + ang / 2;
        out.push_back(Note(Polar(c, rr + h * 1.2, mid), label, h, 4));
    };
    angleMark(v.a1, r.theta, false, L"\x03B8");
    angleMark(v.a, r.phi, true, L"\x03C6");
    // The results.
    const double xr = s > 0 ? std::max({v.b1.x, v.b.x, b1.x, b2f.x}) + h * 4 : std::min({v.b1.x, v.b.x, b1.x, b2f.x}) - h * 4;
    const std::wstring text = L"TL = " + Num(r.tl, 2) + L"\n\x03B8 = " + Deg(r.theta) + L" (to HP)\n\x03C6 = " +
                              Deg(r.phi) + L" (to VP)\nFV length = " + Num(r.fvLength, 2) + L", \x03B1 = " +
                              Deg(r.alpha) + L"\nTV length = " + Num(r.tvLength, 2) + L", \x03B2 = " + Deg(r.beta);
    out.push_back(Note({xr, std::max(v.b1.y, v.a1.y)}, text, h, s > 0 ? 0 : 2));
    return out;
}

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------

std::vector<Entity> CuttingPlane(const std::vector<Vec2>& pts, Vec2 side, const std::wstring& letter, double h) {
    std::vector<Entity> out;
    if (pts.size() < 2) return out;
    std::vector<Vertex> v;
    for (Vec2 p : pts) v.push_back({p, 0});
    Entity chain = Entity::Polyline(v, false);
    chain.linetype = (int16_t)std::max(0, FindLineType(L"CENTER"));
    chain.lineweight = kThin;
    out.push_back(chain);
    // Thick at the ends and on both sides of each bend.
    const double thick = h * 3;
    auto stub = [&](Vec2 from, Vec2 toward) {
        const double len = std::min(thick, Dist(from, toward) / 2);
        if (len > 1e-9) out.push_back(Weighted(Entity::Line(from, from + Normalized(toward - from) * len), kThick));
    };
    stub(pts[0], pts[1]);
    stub(pts.back(), pts[pts.size() - 2]);
    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        stub(pts[i], pts[i - 1]);
        stub(pts[i], pts[i + 1]);
    }
    // Arrows square to the end pieces, towards the side the section is seen from.
    const Vec2 u0 = Normalized(pts[1] - pts[0]);
    Vec2 n0 = Perp(u0);
    if (Dot(side - pts[0], n0) < 0) n0 = n0 * -1;
    const Vec2 ue = Normalized(pts.back() - pts[pts.size() - 2]);
    Vec2 ne = Perp(ue);
    if (Dot(ne, n0) < 0) ne = ne * -1;
    const double arrowLen = h * 3.5, arrow = h;
    for (int k = 0; k < 2; ++k) {
        const Vec2 end = k == 0 ? pts[0] : pts.back();
        const Vec2 along = k == 0 ? u0 * -1 : ue;  // outwards from the line
        const Vec2 n = k == 0 ? n0 : ne;
        const Vec2 tail = end;
        out.push_back(Weighted(MakeLeader({tail + n * arrowLen, tail}, TextData{}, arrow), kThin));
        TextData t;
        t.content = letter;
        t.height = h * 1.4;
        t.justify = 4;
        out.push_back(MakeText(tail + n * (arrowLen * 0.6) + along * (h * 1.6), t));
    }
    return out;
}

std::vector<Vec2> OutlinePoints(const Entity& e, bool& curved) {
    std::vector<Vec2> pts;
    curved = false;
    switch (e.type) {
        case EntityType::Polyline: {
            if (!e.closed || e.verts.size() < 3) break;
            for (size_t i = 0; i < e.verts.size(); ++i) {
                const Vertex& v = e.verts[i];
                pts.push_back(v.p);
                if (v.bulge != 0) {
                    curved = true;
                    const Prim a = ArcFromBulge(v.p, e.verts[(i + 1) % e.verts.size()].p, v.bulge);
                    const int m = 12;
                    const double a0 = AngleOf(v.p - a.c);
                    const double sweep = 4 * std::atan(v.bulge);
                    for (int k = 1; k < m; ++k) pts.push_back(Polar(a.c, a.r, a0 + sweep * k / m));
                }
            }
            break;
        }
        case EntityType::Circle:
            curved = true;
            for (int k = 0; k < 48; ++k) pts.push_back(Polar(e.p0, e.radius, kTau * k / 48));
            break;
        case EntityType::Ellipse:
        case EntityType::Spline: {
            if (!e.closed || SplineTrimmed(e)) break;
            curved = true;
            std::vector<Vec2> fine;
            if (e.type == EntityType::Ellipse) EllipsePoints(e, fine);
            else SplinePoints(e, fine);
            if (fine.size() > 1 && Near(fine.front(), fine.back(), 1e-9)) fine.pop_back();
            const size_t step = std::max<size_t>(1, fine.size() / 48);
            for (size_t i = 0; i < fine.size(); i += step) pts.push_back(fine[i]);
            break;
        }
        default:
            break;
    }
    return pts;
}

bool TrueShape(Vec2 cutA, Vec2 cutB, const std::vector<Vec2>& section, Vec2 x1y1At, bool hasXY, double xyY,
               TrueShapeResult& out, std::wstring& why) {
    out = TrueShapeResult{};
    if (section.size() < 3) return why = L"The section needs three points or more.", false;
    if (Dist(cutA, cutB) < 1e-9) return why = L"The cutting plane line has no length.", false;
    const Vec2 u = Normalized(cutB - cutA);
    if (std::fabs(u.x) < 1e-6) return why = L"The cutting plane line must not run along the projectors (upright).", false;
    Vec2 n = Perp(u);
    double gap = Dot(x1y1At - cutA, n);
    if (gap < 0) n = n * -1, gap = -gap;
    if (gap < 1e-9) return why = L"Click away from the cutting plane line, where the true shape should go.", false;
    // Distances from XY (or from the point of the section nearest the cutting plane's view).
    Vec2 mid{0, 0};
    double minY = 1e300, maxY = -1e300;
    for (Vec2 p : section) mid = mid + p / (double)section.size(), minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
    const Vec2 cutMid = Lerp(cutA, cutB, 0.5);
    double ref, sgn;
    if (hasXY) {
        ref = xyY;
        sgn = mid.y < xyY ? -1 : 1;  // away from XY
    } else {
        sgn = cutMid.y > mid.y ? -1 : 1;
        ref = sgn < 0 ? maxY : minY;
    }
    double t0 = 1e300, t1 = -1e300;
    for (Vec2 p : section) {
        const double t = (p.x - cutA.x) / u.x;
        const Vec2 onCut = cutA + u * t;
        const double d = sgn * (p.y - ref);
        const Vec2 q = onCut + n * (gap + d);
        out.shape.push_back(q);
        out.projectors.push_back({onCut, q});
        t0 = std::min(t0, t), t1 = std::max(t1, t);
    }
    const double margin = std::max(5.0, (t1 - t0) * 0.15);
    out.x1 = cutA + u * (t0 - margin) + n * gap;
    out.y1 = cutA + u * (t1 + margin) + n * gap;
    return true;
}

// ---------------------------------------------------------------------------
// BIS
// ---------------------------------------------------------------------------

void ApplyBisSetup(Document& doc, bool unidirectional) {
    DrawingSettings st = doc.Settings();
    st.units = Units::Millimetres;
    DimStyle& ds = st.dimStyle;
    ds.textHeight = 3.5;
    ds.arrowSize = 3;
    ds.extOffset = 1;
    ds.extBeyond = 2;
    ds.textGap = 1;
    ds.trimZeros = true;
    ds.unidirectional = unidirectional;
    doc.SetSettings(st);
    struct Def {
        const wchar_t* name;
        const wchar_t* linetype;
        int weight;
        int color;
    };
    static const Def defs[] = {
        {L"Visible", L"CONTINUOUS", 50, 7},  {L"Hidden", L"HIDDEN2", 25, 6},     {L"Centre", L"CENTER2", 25, 1},
        {L"Dimension", L"CONTINUOUS", 25, 3}, {L"Section", L"CONTINUOUS", 18, 4}, {L"Construction", L"CONTINUOUS", 18, 8},
        {L"Cutting plane", L"CENTER2", 25, 1},
    };
    doc.BeginChange(L"BIS setup");
    int visible = 0;
    for (const Def& d : defs) {
        Layer l;
        if (const Layer* have = doc.LayerByName(d.name)) l = *have;
        l.name = d.name;
        l.linetype = std::max(0, FindLineType(d.linetype));
        l.lineweight = d.weight;
        l.color = d.color;
        if (doc.LayerByName(d.name)) doc.UpdateLayer(l);
        else l.id = doc.AddLayer(l);
        if (std::wstring(d.name) == L"Visible") visible = doc.LayerByName(d.name)->id;
    }
    CurrentStyle cur = doc.Current();
    cur.layer = visible;
    doc.SetCurrent(cur);
    doc.SetProperty(L"titleblock", L"BIS");
    doc.EndChange();
}

std::vector<Entity> FirstAngleSymbol(Vec2 at, double D) {
    std::vector<Entity> out;
    if (!(D > 0)) return out;
    const double d = D / 2, L = D * 0.9, gap = D * 0.45;
    const double cy = at.y + D / 2 + D * 0.15;
    const double x0 = at.x + D * 0.15;
    // Front view: the small end at the left.
    std::vector<Vertex> v = {{{x0, cy - d / 2}, 0}, {{x0 + L, cy - D / 2}, 0}, {{x0 + L, cy + D / 2}, 0}, {{x0, cy + d / 2}, 0}};
    out.push_back(Weighted(Entity::Polyline(v, true), kMedium));
    // View from the left, drawn to the right (first angle).
    const Vec2 c{x0 + L + gap + D / 2, cy};
    out.push_back(Weighted(Entity::Circle(c, D / 2), kMedium));
    out.push_back(Weighted(Entity::Circle(c, d / 2), kMedium));
    // Centre lines.
    const int centre = std::max(0, FindLineType(L"CENTER"));
    Entity axis = Weighted(Entity::Line({x0 - D * 0.15, cy}, {c.x + D / 2 + D * 0.15, cy}), kThin);
    axis.linetype = (int16_t)centre;
    axis.ltscale = (float)(D / 40);
    out.push_back(axis);
    Entity up = Weighted(Entity::Line({c.x, cy - D / 2 - D * 0.15}, {c.x, cy + D / 2 + D * 0.15}), kThin);
    up.linetype = (int16_t)centre;
    up.ltscale = (float)(D / 40);
    out.push_back(up);
    return out;
}
