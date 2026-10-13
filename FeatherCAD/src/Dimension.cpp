// Dimension.cpp - see Dimension.h.
#include "Dimension.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <set>

#include "Document.h"
#include "Grips.h"
#include "Relate.h"

namespace {
constexpr double kCapHeight = 0.716;  // Arial capitals, in ems

// Arrow with its tip at `tip`, pointing in direction `dir`.
std::array<Vec2, 3> Arrow(Vec2 tip, Vec2 dir, double size) {
    const Vec2 base = tip - dir * size;
    const Vec2 side = Perp(dir) * (size / 6);
    return {tip, base + side, base - side};
}

// An angle turned so that text along it reads left to right (or upwards).
double Readable(double a) {
    a = std::remainder(a, kTau);
    if (a > kPi / 2 + 1e-9) a -= kPi;
    if (a <= -kPi / 2 + 1e-9) a += kPi;
    return a;
}

Vec2 Up(double textAngle) { return {-std::sin(textAngle), std::cos(textAngle)}; }

Vec2 LinearDir(const DimData& d) {
    if (d.kind == DimData::Linear) return {std::cos(d.angle), std::sin(d.angle)};
    const Vec2 v = d.pts[1] - d.pts[0];
    return Length(v) > 1e-12 ? Normalized(v) : Vec2{1, 0};
}

double Tol(Vec2 p) { return 1e-6 * (1 + std::fabs(p.x) + std::fabs(p.y)); }

// ---- angular dimensions -------------------------------------------------------

struct Sector {
    Vec2 v;               // where the lines meet
    double start = 0, sweep = 0;
    int firstLine = 0;    // which line bounds the start of the sector (0 or 1)
    bool firstNeg = false, secondNeg = false;  // the rays run against the lines' directions
};

bool MeetPoint(const DimData& d, Vec2& v) {
    const Vec2 a = d.pts[1] - d.pts[0], b = d.pts[3] - d.pts[2];
    if (d.rays && Near(d.pts[0], d.pts[2], Tol(d.pts[0]))) {
        v = d.pts[0];
        return Length(a) > 1e-12 && Length(b) > 1e-12;
    }
    const double den = Cross(a, b);
    if (std::fabs(den) <= 1e-12 * Length(a) * Length(b) || Length(a) <= 1e-12 || Length(b) <= 1e-12) return false;
    const double t = Cross(d.pts[2] - d.pts[0], b) / den;
    v = d.pts[0] + a * t;
    return true;
}

// The ray directions, as (angle, line, against the line).
struct Ray {
    double a;
    int line;
    bool neg;
};

std::vector<Ray> Rays(const DimData& d, Vec2 v) {
    const Vec2 a = d.pts[1] - d.pts[0], b = d.pts[3] - d.pts[2];
    std::vector<Ray> rays;
    if (d.rays) {
        const Vec2 ra = Length(d.pts[1] - v) > 1e-12 ? d.pts[1] - v : a;
        const Vec2 rb = Length(d.pts[3] - v) > 1e-12 ? d.pts[3] - v : b;
        // Same sense as the lines when the rays run along them.
        rays.push_back({NormalizeAngle(AngleOf(ra)), 0, Dot(ra, a) < 0});
        rays.push_back({NormalizeAngle(AngleOf(rb)), 1, Dot(rb, b) < 0});
    } else {
        rays.push_back({NormalizeAngle(AngleOf(a)), 0, false});
        rays.push_back({NormalizeAngle(AngleOf(a) + kPi), 0, true});
        rays.push_back({NormalizeAngle(AngleOf(b)), 1, false});
        rays.push_back({NormalizeAngle(AngleOf(b) + kPi), 1, true});
    }
    std::sort(rays.begin(), rays.end(), [](const Ray& x, const Ray& y) { return x.a < y.a; });
    return rays;
}

bool FindSector(const DimData& d, Sector& s) {
    if (!MeetPoint(d, s.v)) return false;
    const std::vector<Ray> rays = Rays(d, s.v);
    const double la = Near(d.loc, s.v, 1e-12) ? rays[0].a + 1e-6 : NormalizeAngle(AngleOf(d.loc - s.v));
    for (size_t i = 0; i < rays.size(); ++i) {
        const Ray& from = rays[i];
        const Ray& to = rays[(i + 1) % rays.size()];
        if (from.line == to.line) continue;
        double sw = SweepCCW(from.a, to.a);
        if (sw <= 1e-12) continue;
        if (AngleInSweep(la, from.a, sw, 1e-12)) {
            s.start = from.a;
            s.sweep = sw;
            s.firstLine = from.line;
            s.firstNeg = from.neg;
            s.secondNeg = to.neg;
            return true;
        }
    }
    // On a ray exactly: the first sector after it.
    for (size_t i = 0; i < rays.size(); ++i) {
        const Ray& from = rays[i];
        const Ray& to = rays[(i + 1) % rays.size()];
        if (from.line == to.line) continue;
        s.start = from.a;
        s.sweep = SweepCCW(from.a, to.a);
        s.firstLine = from.line;
        s.firstNeg = from.neg;
        s.secondNeg = to.neg;
        return s.sweep > 1e-12;
    }
    return false;
}

// Direction of line `line` (0 or 1), as the sector's rays use it.
Vec2 LineDir(const DimData& d, int line, bool neg, Vec2 v) {
    Vec2 dir = line == 0 ? d.pts[1] - d.pts[0] : d.pts[3] - d.pts[2];
    if (d.rays) {
        const Vec2 end = line == 0 ? d.pts[1] : d.pts[3];
        if (Length(end - v) > 1e-12) dir = end - v;
        return Normalized(dir);
    }
    return Normalized(neg ? -dir : dir);
}

// ---- grips that move with a point -------------------------------------------

bool PointGrip(const Entity& e, int index) {
    switch (e.type) {
        case EntityType::Line: return index == 0 || index == 1;
        case EntityType::Arc: return index == 1 || index == 2;
        case EntityType::Polyline: return index < (int)e.verts.size();
        case EntityType::Spline:
        case EntityType::Point: return true;
        default: return false;
    }
}

// Moves the ends and vertices of objects (other than dimensions and arrays)
// that lie at `from` to `to`, so connected objects stay connected.
void MovePointEverywhere(Document& doc, Vec2 from, Vec2 to) {
    const double tol = Tol(from);
    std::vector<Entity> changed;
    std::vector<Grip> grips;
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        if (IsAnnotation(e) || e.type == EntityType::Array || e.type == EntityType::Insert) continue;
        if (!Bounds(e).Inflated(tol).Contains(from)) continue;
        grips.clear();
        GripsOf(e, grips);
        Entity copy = e;
        bool moved = false;
        for (const Grip& g : grips) {
            if (!PointGrip(e, g.index) || !Near(g.p, from, tol)) continue;
            if (DragGrip(copy, g.index, g.p, to)) moved = true;
        }
        if (moved) changed.push_back(std::move(copy));
    }
    for (const Entity& e : changed) doc.Replace(e);
}

// Objects connected to `start` through shared ends and vertices.
std::set<uint64_t> Connected(const Document& doc, uint64_t start) {
    struct Key {
        long long x, y;
        bool operator<(const Key& o) const { return x != o.x ? x < o.x : y < o.y; }
    };
    const Box ext = doc.Extents();
    const double cell = std::max(1e-9, 1e-7 * (1 + std::max(ext.Width(), ext.Height()) +
                                               std::max(std::fabs(ext.minX), std::fabs(ext.minY))));
    std::map<Key, std::vector<uint64_t>> index;
    std::map<uint64_t, std::vector<Vec2>> ends;
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        if (IsAnnotation(e) || e.type == EntityType::Array) continue;
        std::vector<Vec2> pts;
        EndPoints(e, pts);
        for (Vec2 p : pts) index[{(long long)std::llround(p.x / cell), (long long)std::llround(p.y / cell)}].push_back(kv.first);
        ends[kv.first] = std::move(pts);
    }
    std::set<uint64_t> seen{start};
    std::vector<uint64_t> todo{start};
    while (!todo.empty()) {
        const uint64_t id = todo.back();
        todo.pop_back();
        for (Vec2 p : ends[id]) {
            const long long kx = std::llround(p.x / cell), ky = std::llround(p.y / cell);
            for (long long dx = -1; dx <= 1; ++dx)
                for (long long dy = -1; dy <= 1; ++dy) {
                    auto it = index.find({kx + dx, ky + dy});
                    if (it == index.end()) continue;
                    for (uint64_t other : it->second)
                        if (seen.insert(other).second) todo.push_back(other);
                }
        }
    }
    return seen;
}

// Moves everything of the connected objects that lies beyond `mid` (in
// direction `side`) by d: a rectangle stretched by its width dimension
// stays a rectangle.
void MoveFarSide(Document& doc, const std::set<uint64_t>& ids, Vec2 mid, Vec2 side, Vec2 d) {
    const double tol = Tol(mid);
    auto far = [&](Vec2 p) { return Dot(p - mid, side) > tol; };
    for (uint64_t id : ids) {
        const Entity* src = doc.Find(id);
        if (!src) continue;
        Entity e = *src;
        bool moved = false;
        switch (e.type) {
            case EntityType::Line:
                if (far(e.p0)) e.p0 += d, moved = true;
                if (far(e.p1)) e.p1 += d, moved = true;
                break;
            case EntityType::Circle:
            case EntityType::Ellipse:
            case EntityType::Point:
                if (far(e.p0)) Translate(e, d), moved = true;
                break;
            case EntityType::Arc: {
                const Vec2 s = Polar(e.p0, e.radius, e.startAngle), t = Polar(e.p0, e.radius, e.endAngle);
                if (far(e.p0) || (far(s) && far(t))) {
                    Translate(e, d);
                    moved = true;
                } else if (far(s)) {
                    moved = DragGrip(e, 1, s, s + d);
                } else if (far(t)) {
                    moved = DragGrip(e, 2, t, t + d);
                }
                break;
            }
            case EntityType::Polyline:
            case EntityType::Spline:
                for (Vertex& v : e.verts)
                    if (far(v.p)) v.p += d, moved = true;
                break;
            default:
                break;
        }
        if (moved) doc.Replace(e);
    }
}

void SetData(Entity& e, const DimData& d) { e.dim = std::make_shared<const DimData>(d); }
}  // namespace

// ---------------------------------------------------------------------------
// Values and text
// ---------------------------------------------------------------------------

Entity MakeDimension(DimData d) {
    Entity e;
    e.type = EntityType::Dimension;
    e.dim = std::make_shared<const DimData>(std::move(d));
    return e;
}

const wchar_t* DimKindName(DimData::Kind k) {
    switch (k) {
        case DimData::Linear: return L"Linear";
        case DimData::Aligned: return L"Aligned";
        case DimData::Radius: return L"Radius";
        case DimData::Diameter: return L"Diameter";
        case DimData::Angular: return L"Angular";
        case DimData::Ordinate: return L"Ordinate";
        case DimData::AngularCoord: return L"Angular coordinate";
    }
    return L"";
}

Vec2 DimDirection(const DimData& d) { return LinearDir(d); }

double DimMeasure(const DimData& d) {
    switch (d.kind) {
        case DimData::Linear: return std::fabs(Dot(d.pts[1] - d.pts[0], LinearDir(d)));
        case DimData::Aligned: return Dist(d.pts[0], d.pts[1]);
        case DimData::Radius: return Dist(d.pts[0], d.pts[1]);
        case DimData::Diameter: return 2 * Dist(d.pts[0], d.pts[1]);
        case DimData::Angular: {
            Sector s;
            return FindSector(d, s) ? s.sweep : 0;
        }
        case DimData::Ordinate: return std::fabs(Dot(d.pts[0] - d.pts[1], Vec2{std::cos(d.angle), std::sin(d.angle)}));
        case DimData::AngularCoord:
            if (Near(d.pts[1], d.pts[0], 1e-12) || Near(d.pts[2], d.pts[0], 1e-12)) return 0;
            return SweepCCW(AngleOf(d.pts[1] - d.pts[0]), AngleOf(d.pts[2] - d.pts[0]));
    }
    return 0;
}

std::wstring DimNumber(double v, const DimStyle& s) {
    const int prec = std::clamp(s.precision, 0, 8);
    wchar_t buf[64];
    swprintf(buf, 64, L"%.*f", prec, v);
    std::wstring t = buf;
    if (s.trimZeros && t.find(L'.') != std::wstring::npos) {
        while (!t.empty() && t.back() == L'0') t.pop_back();
        if (!t.empty() && t.back() == L'.') t.pop_back();
    }
    if (t == L"-0") t = L"0";
    return t;
}

std::wstring DimValueText(const DimData& d) {
    const double m = DimMeasure(d);
    switch (d.kind) {
        case DimData::Radius: return L"R" + DimNumber(m, d.style);
        case DimData::Diameter: return L"\x00D8" + DimNumber(m, d.style);
        case DimData::Angular:
        case DimData::AngularCoord: return DimNumber(ToDegrees(m), d.style) + L"\x00B0";
        default: return DimNumber(m, d.style);
    }
}

std::wstring DimDisplayText(const DimData& d) {
    std::wstring t = d.text.empty() ? L"<>" : d.text;
    const std::wstring value = DimValueText(d);
    for (size_t pos = t.find(L"<>"); pos != std::wstring::npos; pos = t.find(L"<>", pos + value.size()))
        t.replace(pos, 2, value);
    return d.driven ? L"(" + t + L")" : t;
}

double FontSizeForHeight(double height) { return height / kCapHeight; }

double TextWidth(const std::wstring& s, double height) {
    double em = 0;
    for (wchar_t c : s) {
        double w;
        if (c >= L'0' && c <= L'9') w = 0.556;
        else if (c == L'.' || c == L',' || c == L' ' || c == L'i' || c == L'l' || c == L'I') w = 0.278;
        else if (c == L'-' || c == L'(' || c == L')') w = 0.333;
        else if (c == L'\x00B0') w = 0.4;
        else if (c == L'\x2300' || c == L'\x00D8') w = 0.778;
        else if (c == L'+' || c == L'\x00B1' || c == L'=' || c == L'<' || c == L'>') w = 0.584;
        else if (c == L'M' || c == L'W' || c == L'm' || c == L'w') w = 0.833;
        else if (c >= L'A' && c <= L'Z') w = 0.667;
        else if (c >= L'a' && c <= L'z') w = 0.5;
        else w = 0.6;
        em += w;
    }
    return em * FontSizeForHeight(height);
}

std::array<Vec2, 4> DimGraphics::TextBox() const {
    const Vec2 u{std::cos(textAngle), std::sin(textAngle)}, n = Perp(u);
    // A little taller than the capitals, for descenders and the degree sign.
    const Vec2 hu = u * (textWidth / 2), hn = n * (textHeight * 0.65);
    return {textPos - hu - hn, textPos + hu - hn, textPos + hu + hn, textPos - hu + hn};
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void DimLayout(const DimData& d, DimGraphics& g) {
    g = DimGraphics{};
    const DimStyle& st = d.style;
    const double h = st.textHeight, as = st.arrowSize;
    g.text = DimDisplayText(d);
    const bool tolerated = d.tol != DimData::TolNone && d.kind != DimData::Angular && d.kind != DimData::AngularCoord;
    std::wstring limitHi, limitLo;
    if (tolerated && (d.tol == DimData::TolFit || d.tol == DimData::TolFitDeviation)) g.text += d.fit;
    if (tolerated && d.tol == DimData::TolLimits) {
        // The largest and smallest sizes allowed, one over the other.
        const std::wstring v = DimValueText(d);
        size_t k = 0;
        while (k < v.size() && !std::iswdigit(v[k]) && v[k] != L'-' && v[k] != L'.') ++k;
        DimStyle three = d.style;
        three.precision = std::max(3, d.style.precision);
        three.trimZeros = false;
        const double m = DimMeasure(d);
        limitHi = v.substr(0, k) + DimNumber(m + d.tolUpper, three);
        limitLo = v.substr(0, k) + DimNumber(m + d.tolLower, three);
        g.text = limitHi.size() >= limitLo.size() ? limitHi : limitLo;
    }
    g.textHeight = h;
    g.textWidth = TextWidth(g.text, h);
    auto seg = [&](Vec2 a, Vec2 b) {
        if (!Near(a, b, 1e-12)) g.lines.push_back(Prim::Seg(a, b));
    };
    // Extension line from the object point p to the dimension line at q.
    auto ext = [&](Vec2 p, Vec2 q) {
        const Vec2 v = q - p;
        const double len = Length(v);
        if (len <= st.extOffset + 1e-12) return;
        const Vec2 u = v / len;
        seg(p + u * st.extOffset, q + u * st.extBeyond);
    };

    switch (d.kind) {
        case DimData::Linear:
        case DimData::Aligned: {
            const Vec2 u = LinearDir(d), n = Perp(u);
            Vec2 d0 = d.pts[0] + n * Dot(d.loc - d.pts[0], n);
            Vec2 d1 = d.pts[1] + n * Dot(d.loc - d.pts[1], n);
            // Slanted extension lines: along w, to the line through loc parallel to u.
            const Vec2 w{std::cos(AngleOf(u) + d.extAngle), std::sin(AngleOf(u) + d.extAngle)};
            const bool slanted = d.slanted && std::fabs(Cross(w, u)) > 0.05;
            if (slanted) {
                d0 = d.pts[0] + w * (-Cross(d.pts[0] - d.loc, u) / Cross(w, u));
                d1 = d.pts[1] + w * (-Cross(d.pts[1] - d.loc, u) / Cross(w, u));
            }
            ext(d.pts[0], d0);
            ext(d.pts[1], d1);
            const double len = Dist(d0, d1);
            const Vec2 e = len > 1e-12 ? (d1 - d0) / len : u;
            const Vec2 mid = Lerp(d0, d1, 0.5);
            // Unidirectional: level text in a break of the line, if there is room.
            const double half = std::fabs(e.x) * (g.textWidth / 2 + st.textGap) + std::fabs(e.y) * (h / 2 + st.textGap);
            const bool inBreak = st.unidirectional && !slanted && len >= 2 * half + 2.5 * as;
            if (len >= 2.5 * as) {
                if (inBreak) {
                    seg(d0, mid - e * half);
                    seg(mid + e * half, d1);
                } else {
                    seg(d0, d1);
                }
                g.arrows.push_back(Arrow(d0, -e, as));
                g.arrows.push_back(Arrow(d1, e, as));
            } else {  // no room: arrows outside, pointing in
                seg(d0 - e * (2 * as), d1 + e * (2 * as));
                g.arrows.push_back(Arrow(d0, e, as));
                g.arrows.push_back(Arrow(d1, -e, as));
            }
            if (slanted) {
                // Along the line, its letters leaning with the extension lines.
                g.textAngle = Readable(AngleOf(e));
                const Vec2 tu{std::cos(g.textAngle), std::sin(g.textAngle)};
                const double along = Dot(w, tu), across = Dot(w, Perp(tu));
                g.textOblique = std::clamp(std::atan2(along * (across < 0 ? -1 : 1), std::fabs(across)), -ToRadians(75), ToRadians(75));
                const Vec2 lean = Perp(tu) + tu * std::tan(g.textOblique);  // "up" for the leaning letters
                g.textPos = mid + lean * (st.textGap + h / 2);
            } else if (st.unidirectional) {
                g.textAngle = 0;
                if (inBreak) {
                    g.textPos = mid;
                } else {  // beside the line: above it, or to its left
                    Vec2 side = Perp(e);
                    if (side.y < -1e-9 || (std::fabs(side.y) <= 1e-9 && side.x > 0)) side = side * -1;
                    g.textPos = mid + side * (std::fabs(side.x) * g.textWidth / 2 + std::fabs(side.y) * h / 2 + st.textGap);
                }
            } else {
                g.textAngle = Readable(AngleOf(e));
                g.textPos = mid + Up(g.textAngle) * (st.textGap + h / 2);
            }
            break;
        }
        case DimData::Radius:
        case DimData::Diameter: {
            const Vec2 c = d.pts[0];
            const double r = Dist(d.pts[0], d.pts[1]);
            if (r <= 1e-12) break;
            Vec2 v = d.loc - c;
            if (Length(v) <= 1e-12) v = d.pts[1] - c;
            const Vec2 dir = Normalized(v);
            const Vec2 q = c + dir * r, q2 = c - dir * r;
            const bool dia = d.kind == DimData::Diameter;
            if (Length(v) > r * (1 + 1e-9)) {
                // Text outside: a leader out to the text and a short landing under it.
                const double side = d.loc.x >= c.x ? 1 : -1;
                const Vec2 landEnd = d.loc + Vec2{side * (g.textWidth + 2 * st.textGap), 0};
                if (dia) {
                    seg(q2, d.loc);
                    g.arrows.push_back(Arrow(q, dir, as));
                    g.arrows.push_back(Arrow(q2, -dir, as));
                } else {
                    seg(q, d.loc);
                    g.arrows.push_back(Arrow(q, -dir, as));
                }
                seg(d.loc, landEnd);
                g.textAngle = 0;
                g.textPos = Lerp(d.loc, landEnd, 0.5) + Vec2{0, st.textGap + h / 2};
            } else {
                // Text inside, along the line.
                const Vec2 from = dia ? q2 : c;
                seg(from, q);
                g.arrows.push_back(Arrow(q, dir, as));
                if (dia) g.arrows.push_back(Arrow(q2, -dir, as));
                g.textAngle = st.unidirectional ? 0 : Readable(AngleOf(dir));
                g.textPos = Lerp(from, q, 0.5) + Up(g.textAngle) * (st.textGap + h / 2);
            }
            break;
        }
        case DimData::Angular: {
            Sector s;
            if (!FindSector(d, s)) {
                g.textPos = d.loc;
                break;
            }
            const double R = std::max(Dist(d.loc, s.v), 1e-9);
            const Vec2 e1 = Polar(s.v, R, s.start), e2 = Polar(s.v, R, s.start + s.sweep);
            const Vec2 t1{-std::sin(s.start), std::cos(s.start)};
            const Vec2 t2{-std::sin(s.start + s.sweep), std::cos(s.start + s.sweep)};
            if (R * s.sweep >= 2.5 * as) {
                g.lines.push_back(Prim::ArcCCW(s.v, R, s.start, s.sweep));
                g.arrows.push_back(Arrow(e1, -t1, as));
                g.arrows.push_back(Arrow(e2, t2, as));
            } else {
                const double extra = 2 * as / R;
                g.lines.push_back(Prim::ArcCCW(s.v, R, s.start - extra, s.sweep + 2 * extra));
                g.arrows.push_back(Arrow(e1, t1, as));
                g.arrows.push_back(Arrow(e2, -t2, as));
            }
            // Extension lines where the arc lies beyond the drawn lines.
            for (int k = 0; k < 2; ++k) {
                const int line = k == 0 ? s.firstLine : 1 - s.firstLine;
                const Vec2 w = k == 0 ? Vec2{std::cos(s.start), std::sin(s.start)}
                                      : Vec2{std::cos(s.start + s.sweep), std::sin(s.start + s.sweep)};
                const Vec2 a = line == 0 ? d.pts[0] : d.pts[2], b = line == 0 ? d.pts[1] : d.pts[3];
                const double ta = Dot(a - s.v, w), tb = Dot(b - s.v, w);
                const double tmax = std::max(ta, tb), tmin = std::min(ta, tb);
                if (R > tmax + st.extOffset) {
                    const double from = tmax > 1e-9 ? tmax + st.extOffset : 0;
                    seg(s.v + w * from, s.v + w * (R + st.extBeyond));
                } else if (R < tmin - st.extOffset) {
                    seg(s.v + w * (tmin - st.extOffset), s.v + w * std::max(0.0, R - st.extBeyond));
                }
            }
            const double mid = s.start + s.sweep / 2;
            g.textAngle = st.unidirectional ? 0 : Readable(mid - kPi / 2);
            g.textPos = Polar(s.v, R + st.textGap + (st.unidirectional ? h / 2 + g.textWidth / 2 * std::fabs(std::cos(mid)) : h / 2), mid);
            break;
        }
        case DimData::Ordinate: {
            // A leader square to the axis measured, with a jog over to the text.
            const Vec2 u{std::cos(d.angle), std::sin(d.angle)}, n = Perp(u);
            const Vec2 p = d.pts[0];
            const double t = Dot(d.loc - p, n), sgn = t >= 0 ? 1 : -1;
            const double along = Dot(d.loc - p, u);
            const Vec2 start = p + n * (sgn * st.extOffset);
            if (std::fabs(along) < 1e-9 || std::fabs(t) < 3 * as) {
                seg(start, d.loc);
            } else {
                const Vec2 k1 = p + n * (t * 0.5), k2 = d.loc - n * (t * 0.25);
                seg(start, k1);
                seg(k1, k2);
                seg(k2, d.loc);
            }
            g.textAngle = Readable(AngleOf(n * sgn));
            g.textPos = d.loc + n * (sgn * (st.textGap + g.textWidth / 2));
            break;
        }
        case DimData::AngularCoord: {
            const Vec2 c = d.pts[0];
            const double R = std::max(Dist(d.loc, c), 1e-9);
            const double a0 = AngleOf(d.pts[1] - c), sweep = DimMeasure(d), a1 = a0 + sweep;
            const Vec2 e1 = Polar(c, R, a1);
            const Vec2 t1{-std::sin(a1), std::cos(a1)};
            // The 0 degree line, the arc round to the point and a line out to it.
            seg(c, Polar(c, R + st.extBeyond, a0));
            if (sweep > 1e-12) {
                g.lines.push_back(Prim::ArcCCW(c, R, a0, sweep));
                g.arrows.push_back(Arrow(e1, t1, as));
            }
            const double rp = Dist(d.pts[2], c);
            if (R > rp + st.extOffset) seg(Polar(c, rp + st.extOffset, a1), Polar(c, R + st.extBeyond, a1));
            else if (R < rp - st.extOffset) seg(Polar(c, rp - st.extOffset, a1), Polar(c, std::max(0.0, R - st.extBeyond), a1));
            g.textAngle = Readable(a1 - kPi / 2);
            g.textPos = Polar(c, R + st.textGap + h / 2, a1) + t1 * (g.textWidth / 2 + as);
            break;
        }
    }
    if (!tolerated) return;
    // Tolerances after the value (or the limits in its place), smaller and stacked.
    const Vec2 tu{std::cos(g.textAngle), std::sin(g.textAngle)};
    const Vec2 up = Perp(tu) + tu * std::tan(g.textOblique);
    auto piece = [&](const std::wstring& s, Vec2 at, double height) {
        g.extra.push_back({s, at, g.textAngle, height, g.textOblique});
    };
    if (d.tol == DimData::TolLimits) {
        const double lh = h * 0.8;
        piece(limitHi, g.textPos + up * (lh * 0.65), lh);
        piece(limitLo, g.textPos - up * (lh * 0.65), lh);
        g.text.clear();
        return;
    }
    if (d.tol == DimData::TolFit) return;
    const std::wstring hi = DeviationText(d.tolUpper), lo = DeviationText(d.tolLower);
    if (std::fabs(d.tolUpper + d.tolLower) < 1e-12 && d.tolUpper > 0) {  // equal both ways: +-0.02
        const std::wstring pm = L"\x00B1" + DeviationText(d.tolUpper).substr(1);
        piece(pm, g.textPos + tu * (g.textWidth / 2 + h * 0.2 + TextWidth(pm, h) / 2), h);
        return;
    }
    const double th = h * 0.7;
    const Vec2 start = g.textPos + tu * (g.textWidth / 2 + h * 0.3);
    piece(hi, start + tu * (TextWidth(hi, th) / 2) + up * (th * 0.65), th);
    piece(lo, start + tu * (TextWidth(lo, th) / 2) - up * (th * 0.65), th);
}

std::wstring DeviationText(double v) {
    if (std::fabs(v) < 5e-7) return L"0";
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%+.4f", v);
    std::wstring s = buf;
    while (s.back() == L'0') s.pop_back();
    if (s.back() == L'.') s.pop_back();
    return s;
}

namespace {
// ISO 286-1: the size ranges (over the one before, up to and including this), in millimetres.
const double kFitSizes[13] = {3, 6, 10, 18, 30, 50, 80, 120, 180, 250, 315, 400, 500};
// Standard tolerance grades IT5..IT12, micrometres.
const int kIt[8][13] = {
    {4, 5, 6, 8, 9, 11, 13, 15, 18, 20, 23, 25, 27},
    {6, 8, 9, 11, 13, 16, 19, 22, 25, 29, 32, 36, 40},
    {10, 12, 15, 18, 21, 25, 30, 35, 40, 46, 52, 57, 63},
    {14, 18, 22, 27, 33, 39, 46, 54, 63, 72, 81, 89, 97},
    {25, 30, 36, 43, 52, 62, 74, 87, 100, 115, 130, 140, 155},
    {40, 48, 58, 70, 84, 100, 120, 140, 160, 185, 210, 230, 250},
    {60, 75, 90, 110, 130, 160, 190, 220, 250, 290, 320, 360, 400},
    {100, 120, 150, 180, 210, 250, 300, 350, 400, 460, 520, 570, 630},
};
// Fundamental deviations of shafts, micrometres: the upper one (es) for
// d..h, the lower one (ei) for k, m, n, p (k: grades 4 to 7).
struct ShaftRow {
    const wchar_t* letter;
    bool upper;
    int v[13];
};
const ShaftRow kShafts[] = {
    {L"d", true, {-20, -30, -40, -50, -65, -80, -100, -120, -145, -170, -190, -210, -230}},
    {L"e", true, {-14, -20, -25, -32, -40, -50, -60, -72, -85, -100, -110, -125, -135}},
    {L"f", true, {-6, -10, -13, -16, -20, -25, -30, -36, -43, -50, -56, -62, -68}},
    {L"g", true, {-2, -4, -5, -6, -7, -9, -10, -12, -14, -15, -17, -18, -20}},
    {L"h", true, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {L"k", false, {0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5}},
    {L"m", false, {2, 4, 6, 7, 8, 9, 11, 13, 15, 17, 20, 21, 23}},
    {L"n", false, {4, 8, 10, 12, 15, 17, 20, 23, 27, 31, 34, 37, 40}},
    {L"p", false, {6, 12, 15, 18, 22, 26, 32, 37, 43, 50, 56, 62, 68}},
};
}  // namespace

bool IsoFit(const std::wstring& codeIn, double size, double& upper, double& lower, std::wstring& why) {
    std::wstring code;
    for (wchar_t c : codeIn)
        if (!std::iswspace(c)) code.push_back(c);
    size_t k = 0;
    while (k < code.size() && std::iswalpha(code[k])) ++k;
    if (k == 0 || k == code.size()) return why = L"Type a fit such as H7, g6 or js6: a letter and a grade.", false;
    std::wstring letter = code.substr(0, k);
    const bool hole = std::iswupper(letter[0]) != 0;
    for (wchar_t& c : letter) c = (wchar_t)std::towlower(c);
    int grade = 0;
    for (size_t i = k; i < code.size(); ++i) {
        if (!std::iswdigit(code[i])) return why = L"The grade must be a number, as in H7.", false;
        grade = grade * 10 + (code[i] - L'0');
    }
    if (grade < 5 || grade > 12) return why = L"Grades 5 to 12 (IT5 to IT12) are known.", false;
    if (!(size > 0) || size > 500) return why = L"Fits are known for sizes up to 500 mm.", false;
    int r = 0;
    while (size > kFitSizes[r] + 1e-9) ++r;
    const double it = kIt[grade - 5][r] / 1000.0;
    if (letter == L"js") {
        upper = it / 2, lower = -it / 2;
        return true;
    }
    for (const ShaftRow& s : kShafts) {
        if (letter != s.letter) continue;
        double es, ei;
        if (s.upper) {
            es = s.v[r] / 1000.0, ei = es - it;
        } else {
            ei = (letter == L"k" && (grade > 7)) ? 0 : s.v[r] / 1000.0;
            es = ei + it;
        }
        if (!hole) {
            upper = es, lower = ei;
            return true;
        }
        // Holes D..H mirror the shafts: EI = -es.
        if (!s.upper) return why = L"Holes K, M, N and P are not in the table; use A to H or JS.", false;
        lower = -es, upper = lower + it;
        return true;
    }
    return why = L"The fit letter \"" + code.substr(0, k) +
                 L"\" is not in the table (holes D to H, JS; shafts d to h, js, k, m, n, p).", false;
}

// ---------------------------------------------------------------------------
// Hit tests
// ---------------------------------------------------------------------------

void DimPrimitives(const Entity& e, std::vector<Prim>& out) {
    if (!e.dim) return;
    DimGraphics g;
    DimLayout(*e.dim, g);
    out.insert(out.end(), g.lines.begin(), g.lines.end());
    if (g.lines.empty()) out.push_back(Prim::Seg(g.textPos, g.textPos));
}

Box DimBounds(const Entity& e) {
    Box b;
    if (!e.dim) return b;
    DimGraphics g;
    DimLayout(*e.dim, g);
    for (const Prim& p : g.lines) b.Add(p.Bounds());
    for (const auto& a : g.arrows)
        for (Vec2 p : a) b.Add(p);
    for (Vec2 p : g.TextBox()) b.Add(p);
    return b;
}

namespace {
bool InQuad(const std::array<Vec2, 4>& q, Vec2 p) {
    for (int i = 0; i < 4; ++i)
        if (Cross(q[(i + 1) % 4] - q[i], p - q[i]) < 0) return false;
    return true;
}
}  // namespace

double DimDistance(const Entity& e, Vec2 p) {
    if (!e.dim) return std::numeric_limits<double>::infinity();
    DimGraphics g;
    DimLayout(*e.dim, g);
    double best = std::numeric_limits<double>::infinity();
    for (const Prim& pr : g.lines) best = std::min(best, Distance(pr, p));
    const auto box = g.TextBox();
    if (InQuad(box, p)) return 0;
    for (int i = 0; i < 4; ++i) best = std::min(best, Distance(Prim::Seg(box[i], box[(i + 1) % 4]), p));
    return best;
}

bool DimTouchesBox(const Entity& e, const Box& box) {
    if (!e.dim) return false;
    DimGraphics g;
    DimLayout(*e.dim, g);
    for (const Prim& p : g.lines)
        if (PrimTouchesBox(p, box)) return true;
    const auto q = g.TextBox();
    for (int i = 0; i < 4; ++i)
        if (PrimTouchesBox(Prim::Seg(q[i], q[(i + 1) % 4]), box)) return true;
    return InQuad(q, box.Center());
}

// ---------------------------------------------------------------------------
// Transformations and grips
// ---------------------------------------------------------------------------

void DimMap(Entity& e, const std::function<Vec2(Vec2)>& point, const std::function<Vec2(Vec2)>& vec) {
    if (!e.dim) return;
    DimData d = *e.dim;
    for (Vec2& p : d.pts) p = point(p);
    d.loc = point(d.loc);
    const Vec2 u = vec({std::cos(d.angle), std::sin(d.angle)});
    d.angle = NormalizeAngle(AngleOf(u));
    SetData(e, d);
}

bool DimStretch(Entity& e, const Box& w, Vec2 delta) {
    if (!e.dim) return false;
    DimData d = *e.dim;
    bool moved = false;
    const int n = d.kind == DimData::Angular ? 4 : d.kind == DimData::AngularCoord ? 3 : d.kind == DimData::Ordinate ? 1 : 2;
    for (int i = 0; i < n; ++i)
        if (w.Contains(d.pts[(size_t)i])) d.pts[(size_t)i] += delta, moved = true;
    if (w.Contains(d.loc)) d.loc += delta, moved = true;
    if (moved) SetData(e, d);
    return moved;
}

void DimGrips(const Entity& e, std::vector<std::pair<int, Vec2>>& out) {
    if (!e.dim) return;
    const DimData& d = *e.dim;
    DimGraphics g;
    DimLayout(d, g);
    if (d.kind == DimData::Linear || d.kind == DimData::Aligned) {
        out.push_back({0, d.pts[0]});
        out.push_back({1, d.pts[1]});
        out.push_back({2, g.textPos});
    } else if (d.kind == DimData::Ordinate) {
        out.push_back({0, d.pts[0]});
        out.push_back({2, d.loc});
    } else {
        out.push_back({2, g.textPos});
    }
}

bool DragDimGrip(Entity& e, int index, Vec2 from, Vec2 to) {
    if (!e.dim) return false;
    DimData d = *e.dim;
    if (index == 0 && d.kind == DimData::Ordinate) {
        d.pts[0] = to;
    } else if ((index == 0 || index == 1) && (d.kind == DimData::Linear || d.kind == DimData::Aligned)) {
        d.pts[(size_t)index] = to;
        if (d.kind == DimData::Aligned && Near(d.pts[0], d.pts[1], 1e-12)) return false;
    } else {
        d.loc += to - from;
    }
    SetData(e, d);
    return true;
}

// ---------------------------------------------------------------------------
// Associativity
// ---------------------------------------------------------------------------

DimRef FindRef(const Document& doc, Vec2 p) {
    const double tol = Tol(p);
    std::vector<Grip> grips;
    DimRef best;
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        if (IsAnnotation(e) || e.type == EntityType::Array) continue;
        if (!Bounds(e).Inflated(tol).Contains(p)) continue;
        grips.clear();
        GripsOf(e, grips);
        for (const Grip& g : grips) {
            if (!Near(g.p, p, tol)) continue;
            // End points and vertices are the best references.
            if (!best.id || PointGrip(e, g.index)) best = {kv.first, g.index};
            if (PointGrip(e, g.index)) return best;
        }
    }
    return best;
}

bool RefPoint(const Document& doc, const DimRef& r, Vec2& p) {
    if (!r.id || r.grip < 0) return false;
    const Entity* e = doc.Find(r.id);
    if (!e || e->type == EntityType::Dimension) return false;
    std::vector<Grip> grips;
    GripsOf(*e, grips);
    for (const Grip& g : grips)
        if (g.index == r.grip) {
            p = g.p;
            return true;
        }
    return false;
}

bool RefCircle(const Document& doc, const DimRef& r, Vec2& c, double& radius) {
    if (!r.id || r.grip >= 0) return false;
    const Entity* e = doc.Find(r.id);
    if (!e) return false;
    if ((e->type == EntityType::Circle || e->type == EntityType::Arc) && r.grip == -1) {
        c = e->p0;
        radius = e->radius;
        return true;
    }
    if (e->type == EntityType::Polyline) {
        const size_t seg = (size_t)(-(r.grip + 1));
        if (seg >= SegmentCount(*e)) return false;
        const Vertex& v = e->verts[seg];
        if (v.bulge == 0) return false;
        const Prim a = ArcFromBulge(v.p, e->verts[(seg + 1) % e->verts.size()].p, v.bulge);
        c = a.c;
        radius = a.r;
        return true;
    }
    return false;
}

bool HasRefs(const Entity& e) {
    if (e.type != EntityType::Dimension || !e.dim) return false;
    for (const DimRef& r : e.dim->refs)
        if (r.id) return true;
    return false;
}

bool RenumberRefs(Entity& dim, const Entity& before, const Entity& after) {
    if (!HasRefs(dim) || before.id != after.id) return false;
    DimData d = *dim.dim;
    bool changed = false;
    std::vector<Grip> oldGrips, newGrips;
    GripsOf(before, oldGrips);
    GripsOf(after, newGrips);
    for (DimRef& r : d.refs) {
        if (r.id != after.id) continue;
        if (r.grip >= 0) {
            const Grip* old = nullptr;
            for (const Grip& g : oldGrips)
                if (g.index == r.grip) old = &g;
            int found = -1;
            if (old)
                for (const Grip& g : newGrips)
                    if (Near(g.p, old->p, Tol(old->p))) {
                        found = g.index;
                        break;
                    }
            if (found != r.grip) {
                if (found >= 0) r.grip = found;
                else r = {};
                changed = true;
            }
        } else if (before.type == EntityType::Polyline) {
            // An arc segment: the segment of the same circle now.
            const size_t seg = (size_t)(-(r.grip + 1));
            int found = 0;  // 0: not found (arc refs are negative)
            if (seg < SegmentCount(before) && before.verts[seg].bulge != 0) {
                const Prim a = ArcFromBulge(before.verts[seg].p, before.verts[(seg + 1) % before.verts.size()].p,
                                            before.verts[seg].bulge);
                for (size_t i = 0; i < SegmentCount(after) && found == 0; ++i) {
                    if (after.verts[i].bulge == 0) continue;
                    const Prim b = ArcFromBulge(after.verts[i].p, after.verts[(i + 1) % after.verts.size()].p,
                                                after.verts[i].bulge);
                    if (Near(a.c, b.c, Tol(a.c)) && std::fabs(a.r - b.r) <= Tol(a.c)) found = -1 - (int)i;
                }
            }
            if (found == 0) {
                r = {};
                changed = true;
            } else if (found != r.grip) {
                r.grip = found;
                changed = true;
            }
        }
    }
    if (changed) SetData(dim, d);
    return changed;
}

bool FollowRefs(const Document& doc, Entity& dim) {
    if (!HasRefs(dim)) return false;
    DimData d = *dim.dim;
    bool changed = false;
    std::array<Vec2, 4> np = d.pts;
    auto point = [&](size_t i) {
        if (!d.refs[i].id) return;
        Vec2 p;
        if (RefPoint(doc, d.refs[i], p)) {
            np[i] = p;
        } else {
            d.refs[i] = {};
            changed = true;
        }
    };
    switch (d.kind) {
        case DimData::Linear:
        case DimData::Aligned: {
            point(0);
            point(1);
            if (np[0] == d.pts[0] && np[1] == d.pts[1]) break;
            if (d.kind == DimData::Aligned && Near(np[0], np[1], 1e-12)) break;  // keep it as it was
            const Vec2 oldMid = Lerp(d.pts[0], d.pts[1], 0.5), newMid = Lerp(np[0], np[1], 0.5);
            if (d.kind == DimData::Linear) {
                // The dimension line keeps its distance from the nearer point.
                const Vec2 u = LinearDir(d), n = Perp(u);
                const size_t k = std::fabs(Dot(d.loc - d.pts[0], n)) <= std::fabs(Dot(d.loc - d.pts[1], n)) ? 0 : 1;
                const double off = Dot(d.loc - d.pts[k], n), along = Dot(d.loc - oldMid, u);
                d.loc = u * (Dot(newMid, u) + along) + n * (Dot(np[k], n) + off);
            } else {
                const Vec2 uo = LinearDir(d);
                const Vec2 un = Normalized(np[1] - np[0]);
                const double a = Dot(d.loc - oldMid, uo), b = Dot(d.loc - oldMid, Perp(uo));
                d.loc = newMid + un * a + Perp(un) * b;
            }
            d.pts = np;
            changed = true;
            break;
        }
        case DimData::Radius:
        case DimData::Diameter: {
            if (!d.refs[0].id) break;
            Vec2 c;
            double r;
            if (!RefCircle(doc, d.refs[0], c, r)) {
                d.refs[0] = {};
                changed = true;
                break;
            }
            const Vec2 oc = d.pts[0];
            const double orad = Dist(d.pts[0], d.pts[1]);
            if (c == oc && std::fabs(r - orad) <= 1e-12 * (1 + r)) break;
            const Vec2 dir = Normalized(d.pts[1] - oc);
            const Vec2 ld = d.loc - oc;
            const double dl = Length(ld);
            if (dl > orad) d.loc = c + Normalized(ld) * (r + (dl - orad));
            else d.loc = c + ld * (orad > 0 ? r / orad : 1);
            d.pts[0] = c;
            d.pts[1] = c + dir * r;
            changed = true;
            break;
        }
        case DimData::Angular: {
            for (size_t i = 0; i < 4; ++i) point(i);
            if (np == d.pts) break;
            Sector os;
            const bool hadSector = FindSector(d, os);
            const double R = hadSector ? Dist(d.loc, os.v) : 0;
            const double f = hadSector && os.sweep > 0 ? SweepCCW(os.start, NormalizeAngle(AngleOf(d.loc - os.v))) / os.sweep : 0.5;
            DimData nd = d;
            nd.pts = np;
            Vec2 v;
            if (hadSector && MeetPoint(nd, v)) {
                const Vec2 a = LineDir(nd, os.firstLine, os.firstNeg, v);
                const Vec2 b = LineDir(nd, 1 - os.firstLine, os.secondNeg, v);
                const double start = AngleOf(a), sweep = SweepCCW(start, AngleOf(b));
                nd.loc = Polar(v, R, start + sweep * std::clamp(f, 0.0, 1.0));
            }
            d = nd;
            changed = true;
            break;
        }
        case DimData::Ordinate: {
            point(0);
            if (np[0] == d.pts[0]) break;
            d.loc += np[0] - d.pts[0];  // the text keeps its place by the point
            d.pts = np;
            changed = true;
            break;
        }
        case DimData::AngularCoord: {
            for (size_t i = 0; i < 3; ++i) point(i);
            if (np == d.pts) break;
            const double R = Dist(d.loc, d.pts[0]);
            d.pts = np;
            d.loc = Polar(np[0], R, AngleOf(np[2] - np[0]));
            changed = true;
            break;
        }
    }
    if (changed) SetData(dim, d);
    return changed;
}

bool CheckRefs(const Document& doc, Entity& dim) {
    if (!HasRefs(dim)) return false;
    DimData d = *dim.dim;
    bool dropped = false;
    for (size_t i = 0; i < 4; ++i) {
        DimRef& r = d.refs[i];
        if (!r.id) continue;
        bool ok;
        if (r.grip < 0) {
            Vec2 c;
            double rad;
            ok = i == 0 && RefCircle(doc, r, c, rad) && Near(c, d.pts[0], Tol(c)) &&
                 std::fabs(rad - Dist(d.pts[0], d.pts[1])) <= Tol(c);
        } else {
            Vec2 p;
            ok = RefPoint(doc, r, p) && Near(p, d.pts[i], Tol(p));
        }
        if (!ok) {
            r = {};
            dropped = true;
        }
    }
    if (dropped) SetData(dim, d);
    return dropped;
}

void RemapRefs(Entity& e, const std::map<uint64_t, uint64_t>& idMap) {
    if (!HasRefs(e)) return;
    DimData d = *e.dim;
    bool changed = false;
    for (DimRef& r : d.refs) {
        auto it = idMap.find(r.id);
        if (r.id && it != idMap.end()) {
            r.id = it->second;
            changed = true;
        }
    }
    if (changed) SetData(e, d);
}

// ---------------------------------------------------------------------------
// Driving dimensions
// ---------------------------------------------------------------------------

bool DriveDimension(Document& doc, uint64_t dimId, double value, std::wstring& why) {
    const Entity* de = doc.Find(dimId);
    if (!de || de->type != EntityType::Dimension || !de->dim) {
        why = L"That is not a dimension.";
        return false;
    }
    const DimData d = *de->dim;
    if (d.driven) {
        why = L"This is a driven dimension: it only reports. Make it driving first (in its properties).";
        return false;
    }
    if (!d.formula.empty()) {
        why = L"This dimension follows its formula (" + d.formula + L"): change the formula in Variables.";
        return false;
    }
    // Held by the relationship solver: a locked dimension, or one on objects
    // with relationships. It holds the new value; the solver moves the objects.
    bool related = d.locked;
    if (!related && doc.Settings().maintainRelations) {
        for (const auto& kv : doc.Entities()) {
            if (kv.second.type != EntityType::Relation || !kv.second.annot) continue;
            for (const DimRef& rr : kv.second.annot->rel.refs)
                for (const DimRef& dr : d.refs) related |= rr.id && rr.id == dr.id;
            if (related) break;
        }
    }
    if (related && doc.Settings().maintainRelations && HasRefs(*de)) {
        if (!(value > 0) || !std::isfinite(value)) {
            why = L"The value must be positive.";
            return false;
        }
        Entity x = *de;
        DimData nd = d;
        nd.locked = true;
        nd.value = value;
        // The shape it measures keeps its joined corners while it changes.
        std::set<uint64_t> touched;
        for (const DimRef& r : d.refs)
            if (r.id) touched.insert(r.id);
        AddImpliedRelations(doc, touched);
        x.dim = std::make_shared<const DimData>(std::move(nd));
        doc.Replace(x);
        return true;
    }
    if (d.kind == DimData::Ordinate || d.kind == DimData::AngularCoord) {
        if (!d.refs[d.kind == DimData::Ordinate ? 0 : 2].id) {
            why = L"This dimension is not attached to any object, so there is nothing to change.";
            return false;
        }
        if (!(value >= 0) || !std::isfinite(value)) {
            why = L"The value cannot be negative.";
            return false;
        }
        Vec2 to;
        if (d.kind == DimData::Ordinate) {
            const Vec2 u{std::cos(d.angle), std::sin(d.angle)};
            const double cur = Dot(d.pts[0] - d.pts[1], u);
            to = d.pts[0] + u * ((cur < 0 ? -value : value) - cur);
        } else {
            if (value >= kTau) {
                why = L"The angle must be less than 360\x00B0.";
                return false;
            }
            to = RotateAround(d.pts[2], d.pts[0], value - DimMeasure(d));
        }
        MovePointEverywhere(doc, d.pts[d.kind == DimData::Ordinate ? 0 : 2], to);
        return true;
    }
    if (!(value > 0) || !std::isfinite(value)) {
        why = L"The value must be positive.";
        return false;
    }
    switch (d.kind) {
        case DimData::Linear:
        case DimData::Aligned: {
            const bool a0 = d.refs[0].id != 0, a1 = d.refs[1].id != 0;
            if (!a0 && !a1) break;
            const Vec2 u = LinearDir(d);
            const double signedLen = Dot(d.pts[1] - d.pts[0], u);
            const double sgn = signedLen >= 0 ? 1 : -1;
            if (d.kind == DimData::Linear && std::fabs(signedLen) <= 1e-12) {
                why = L"The points are in line across the direction measured; there is no length to change.";
                return false;
            }
            const double change = value - std::fabs(signedLen);
            const Vec2 mid = Lerp(d.pts[0], d.pts[1], 0.5);
            // Move the second point's side (or the first's, if only it is attached).
            const size_t k = a1 ? 1 : 0;
            const Vec2 side = u * (k == 1 ? sgn : -sgn);
            MoveFarSide(doc, Connected(doc, d.refs[k].id), mid, side, side * change);
            return true;
        }
        case DimData::Radius:
        case DimData::Diameter: {
            if (!d.refs[0].id) break;
            const double r = d.kind == DimData::Diameter ? value / 2 : value;
            const Entity* src = doc.Find(d.refs[0].id);
            if (!src) break;
            Entity e = *src;
            if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
                e.radius = r;
            } else if (e.type == EntityType::Polyline) {
                const size_t seg = (size_t)(-(d.refs[0].grip + 1));
                if (seg >= SegmentCount(e)) break;
                Vertex& v = e.verts[seg];
                const Vec2 w = e.verts[(seg + 1) % e.verts.size()].p;
                const double chord = Dist(v.p, w);
                if (r < chord / 2 * (1 - 1e-12)) {
                    why = L"An arc between these two vertices needs a radius of at least " +
                          DimNumber(chord / 2, d.style) + L".";
                    return false;
                }
                double theta = 2 * std::asin(std::min(1.0, chord / (2 * r)));
                if (std::fabs(std::atan(v.bulge)) * 4 > kPi) theta = kTau - theta;  // was the larger arc
                v.bulge = std::tan(theta / 4) * (v.bulge < 0 ? -1 : 1);
            } else {
                break;
            }
            doc.Replace(e);
            return true;
        }
        case DimData::Angular: {
            Sector s;
            if (!FindSector(d, s)) {
                why = L"The lines of this angle are parallel.";
                return false;
            }
            if (value >= kTau || (!d.rays && value >= kPi)) {
                why = d.rays ? L"The angle must be less than 360\x00B0." : L"The angle between two lines must be less than 180\x00B0.";
                return false;
            }
            const double delta = value - s.sweep;
            // Turn the second line about the vertex (the first if only it is attached).
            const bool bAttached = d.refs[2].id || d.refs[3].id;
            const bool aAttached = d.refs[0].id || d.refs[1].id;
            if (!aAttached && !bAttached) break;
            const int line = bAttached ? 1 : 0;
            // Turning the line that ends the sector counter-clockwise opens it.
            const double turn = (s.firstLine == line ? -delta : delta);
            for (size_t i = (size_t)line * 2; i < (size_t)line * 2 + 2; ++i) {
                const DimRef& r = d.refs[i];
                if (!r.id) continue;
                const Entity* src = doc.Find(r.id);
                if (!src) continue;
                const Vec2 from = d.pts[i];
                if (Near(from, s.v, Tol(from))) continue;  // the vertex stays
                const Vec2 to = RotateAround(from, s.v, turn);
                if (src->type == EntityType::Arc && Near(src->p0, s.v, Tol(s.v)) && (r.grip == 1 || r.grip == 2)) {
                    Entity e = *src;  // an arc's angle: move its end round the circle
                    if (r.grip == 1) e.startAngle = NormalizeAngle(e.startAngle + turn);
                    else e.endAngle = NormalizeAngle(e.endAngle + turn);
                    doc.Replace(e);
                } else {
                    MovePointEverywhere(doc, from, to);
                }
            }
            return true;
        }
        default:
            break;
    }
    why = L"This dimension is not attached to any object, so there is nothing to change. "
          L"Change its text instead, or dimension the objects again.";
    return false;
}
