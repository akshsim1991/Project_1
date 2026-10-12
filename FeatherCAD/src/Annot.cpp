// Annot.cpp - see Annot.h.
#include "Annot.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <numeric>
#include <unordered_map>

#include "Dimension.h"
#include "Spline.h"
#include "Style.h"

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

std::wstring ExpandText(const std::wstring& s, const FieldValues* fields) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c == L'%' && i + 2 < s.size() && s[i + 1] == L'%') {
            const wchar_t k = (wchar_t)std::towlower(s[i + 2]);
            const wchar_t sym = k == L'd' ? L'\x00B0' : k == L'c' ? L'\x00D8' : k == L'p' ? L'\x00B1' : k == L'%' ? L'%' : 0;
            if (sym) {
                out += sym;
                i += 2;
                continue;
            }
        }
        if (c == L'{' && fields) {
            const size_t close = s.find(L'}', i + 1);
            if (close != std::wstring::npos && close - i < 40) {
                std::wstring name = s.substr(i + 1, close - i - 1);
                for (wchar_t& ch : name) ch = (wchar_t)std::towlower(ch);
                auto it = fields->find(name);
                if (it != fields->end()) {
                    out += it->second;
                    i = close;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

std::vector<std::wstring> SplitLines(const std::wstring& s) {
    std::vector<std::wstring> lines(1);
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n') lines.emplace_back();
        else lines.back() += c;
    }
    return lines;
}

namespace {
std::array<Vec2, 3> Arrow(Vec2 tip, Vec2 dir, double size) {
    const Vec2 base = tip - dir * size;
    const Vec2 side = Perp(dir) * (size / 6);
    return {tip, base + side, base - side};
}

// The box around laid-out text: [0] bottom left, [1] bottom right, [2] top right, [3] top left.
std::array<Vec2, 4> TextBoxOf(const TextItem& t) {
    const Vec2 u{std::cos(t.angle), std::sin(t.angle)}, n = Perp(u);
    const double left = t.align == 0 ? 0 : t.align == 1 ? -t.width / 2 : -t.width;
    const Vec2 a = t.pos + u * left, b = a + u * t.width;
    const Vec2 up = n * (t.height * 0.75), down = n * (t.height * 0.85);  // descenders below
    return {a - down, b - down, b + up, a + up};
}

// The frame around text lines (all at one angle): a box or a circle.
void Frame(const TextData& t, const std::vector<TextItem>& items, double angle, AnnotGraphics& g) {
    if (!t.frame || items.empty()) return;
    const Vec2 u{std::cos(angle), std::sin(angle)}, n = Perp(u);
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    const Vec2 o = items[0].pos;
    for (const TextItem& it : items) {
        for (const Vec2& c : TextBoxOf(it)) {
            const double x = Dot(c - o, u), y = Dot(c - o, n);
            x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
        }
    }
    const double gap = t.height * 0.4;
    if (t.frame == 2) {
        const Vec2 c = o + u * ((x0 + x1) / 2) + n * ((y0 + y1) / 2);
        const double r = std::hypot(x1 - x0, y1 - y0) / 2 * 0.85 + gap;
        g.lines.push_back(Prim::Circle(c, std::max(r, t.height)));
        return;
    }
    x0 -= gap, x1 += gap, y0 -= gap * 0.6, y1 += gap * 0.6;
    const Vec2 p[4] = {o + u * x0 + n * y0, o + u * x1 + n * y0, o + u * x1 + n * y1, o + u * x0 + n * y1};
    for (int i = 0; i < 4; ++i) g.lines.push_back(Prim::Seg(p[i], p[(i + 1) % 4]));
}
}  // namespace

void LayoutText(const TextData& t, Vec2 at, const FieldValues* fields, AnnotGraphics& g) {
    // An attribute definition shows its tag (in a block reference, the value shows).
    const std::vector<std::wstring> lines = SplitLines(ExpandText(t.tag.empty() ? t.content : t.tag, fields));
    const double h = t.height > 0 ? t.height : 2.5;
    const double step = h * 1.6667 * (t.lineSpacing > 0 ? t.lineSpacing : 1);
    const int row = std::clamp(t.justify, 0, 8) / 3, col = std::clamp(t.justify, 0, 8) % 3;
    const size_t n = lines.size();
    // The middle of the first line's capitals, below the insertion point.
    double first;
    if (row == 0) first = -h / 2;
    else if (row == 1) first = (n - 1) * step / 2;
    else first = (n - 1) * step + h / 2;
    const Vec2 u{std::cos(t.rotation), std::sin(t.rotation)}, nn = Perp(u);
    const size_t start = g.texts.size();
    for (size_t i = 0; i < n; ++i) {
        TextItem it;
        it.text = lines[i];
        it.pos = at + nn * (first - i * step);
        it.align = col;
        it.angle = t.rotation;
        it.height = h;
        it.widthFactor = t.widthFactor > 0 ? t.widthFactor : 1;
        it.width = TextWidth(it.text, h) * it.widthFactor;
        it.font = t.font;
        g.boxes.push_back(TextBoxOf(it));
        g.texts.push_back(std::move(it));
    }
    Frame(t, std::vector<TextItem>(g.texts.begin() + (long)start, g.texts.end()), t.rotation, g);
}

// ---------------------------------------------------------------------------
// Hatch patterns (acadiso.pat, millimetres)
// ---------------------------------------------------------------------------

const std::vector<HatchPattern>& HatchPatterns() {
    static const std::vector<HatchPattern> p = {
        {L"SOLID", L"Solid fill", {}},
        {L"ANSI31", L"ANSI iron, brick, stone masonry", {{45, 0, 0, 0, 3.175, {}}}},
        {L"ANSI32", L"ANSI steel", {{45, 0, 0, 0, 9.525, {}}, {45, 4.49013, 0, 0, 9.525, {}}}},
        {L"ANSI33", L"ANSI bronze, brass, copper", {{45, 0, 0, 0, 6.35, {}}, {45, 4.49013, 0, 0, 6.35, {3.175, -1.5875}}}},
        {L"ANSI34", L"ANSI plastic, rubber",
         {{45, 0, 0, 0, 19.05, {}}, {45, 4.49013, 0, 0, 19.05, {}}, {45, 8.98026, 0, 0, 19.05, {}}, {45, 13.4704, 0, 0, 19.05, {}}}},
        {L"ANSI35", L"ANSI fire brick, refractory", {{45, 0, 0, 0, 6.35, {}}, {45, 4.49013, 0, 0, 6.35, {7.9375, -1.5875, 0, -1.5875}}}},
        {L"ANSI36", L"ANSI marble, slate, glass", {{45, 0, 0, 5.55625, 3.175, {7.9375, -1.5875, 0, -1.5875}}}},
        {L"ANSI37", L"ANSI lead, zinc, magnesium, insulation", {{45, 0, 0, 0, 3.175, {}}, {135, 0, 0, 0, 3.175, {}}}},
        {L"ANSI38", L"ANSI aluminium", {{45, 0, 0, 0, 3.175, {}}, {135, 0, 0, 6.35, 3.175, {7.9375, -4.7625}}}},
        {L"NET", L"Square grid", {{0, 0, 0, 0, 3.175, {}}, {90, 0, 0, 0, 3.175, {}}}},
        {L"LINE", L"Parallel horizontal lines", {{0, 0, 0, 0, 3.175, {}}}},
        {L"DOTS", L"Dots", {{0, 0, 0, 0.79375, 1.5875, {0, -1.5875}}}},
        {L"BRICK", L"Brick or masonry", {{0, 0, 0, 0, 6.35, {}}, {90, 0, 0, 6.35, 6.35, {6.35, -6.35}}, {90, 3.175, 0, 6.35, 6.35, {-6.35, 6.35}}}},
        {L"CROSS", L"Crosses", {{0, 0, 0, 6.35, 6.35, {3.175, -9.525}}, {90, 1.5875, -1.5875, 6.35, 6.35, {3.175, -9.525}}}},
        {L"EARTH", L"Earth or ground",
         {{0, 0, 0, 6.35, 6.35, {6.35, -6.35}}, {0, 0, 2.38125, 6.35, 6.35, {6.35, -6.35}}, {0, 0, 4.7625, 6.35, 6.35, {6.35, -6.35}},
          {90, 0.79375, 5.55625, 6.35, 6.35, {6.35, -6.35}}, {90, 3.175, 5.55625, 6.35, 6.35, {6.35, -6.35}},
          {90, 5.55625, 5.55625, 6.35, 6.35, {6.35, -6.35}}}},
        {L"HONEY", L"Honeycomb",
         {{0, 0, 0, 4.7625, 2.74963, {3.175, -6.35}}, {120, 0, 0, 4.7625, 2.74963, {3.175, -6.35}}, {60, 3.175, 0, 4.7625, 2.74963, {3.175, -6.35}}}},
    };
    return p;
}

int FindHatchPattern(const std::wstring& name) {
    const auto& p = HatchPatterns();
    for (size_t i = 0; i < p.size(); ++i)
        if (SameName(p[i].name, name)) return (int)i;
    return -1;
}

bool IsSolid(const HatchData& h) {
    const int i = FindHatchPattern(h.pattern);
    return i < 0 ? false : HatchPatterns()[(size_t)i].lines.empty();
}

double LoopArea(const std::vector<Vec2>& loop) {
    double a = 0;
    for (size_t i = 0, n = loop.size(); i < n; ++i) a += Cross(loop[i], loop[(i + 1) % n]);
    return a / 2;
}

namespace {
bool InLoop(const std::vector<Vec2>& loop, Vec2 p) {
    bool in = false;
    for (size_t i = 0, n = loop.size(), j = n - 1; i < n; j = i++) {
        const Vec2 a = loop[i], b = loop[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (b.x - a.x) * (p.y - a.y) / (b.y - a.y)) in = !in;
    }
    return in;
}
}  // namespace

bool InsideLoops(const std::vector<std::vector<Vec2>>& loops, Vec2 p) {
    bool in = false;
    for (const auto& l : loops)
        if (l.size() >= 3 && InLoop(l, p)) in = !in;
    return in;
}

double HatchArea(const HatchData& h) {
    double total = 0;
    for (size_t i = 0; i < h.loops.size(); ++i) {
        const auto& l = h.loops[i];
        if (l.size() < 3) continue;
        int depth = 0;
        for (size_t j = 0; j < h.loops.size(); ++j)
            if (j != i && h.loops[j].size() >= 3 && InLoop(h.loops[j], l[0])) ++depth;
        total += (depth % 2 ? -1 : 1) * std::fabs(LoopArea(l));
    }
    return std::fabs(total);
}

bool HatchSegments(const HatchData& h, std::vector<std::pair<Vec2, Vec2>>& out, size_t limit) {
    const int pi = FindHatchPattern(h.pattern);
    if (pi < 0) return true;
    const HatchPattern& pat = HatchPatterns()[(size_t)pi];
    const double sc = h.scale > 0 ? h.scale : 1;
    std::vector<double> cuts;
    for (const HatchPatternLine& pl : pat.lines) {
        const double a = ToRadians(pl.angle) + h.angle;
        const Vec2 u{std::cos(a), std::sin(a)}, n = Perp(u);
        // The pattern's own frame turns with the hatch angle.
        const Vec2 o = h.origin + RotateAround(Vec2{pl.x, pl.y} * sc, {0, 0}, h.angle);
        const double dx = pl.dx * sc, dy = pl.dy * sc;
        if (std::fabs(dy) < 1e-9) continue;
        double lo = 1e300, hi = -1e300;
        for (const auto& l : h.loops)
            for (Vec2 p : l) {
                const double d = Dot(p - o, n);
                lo = std::min(lo, d), hi = std::max(hi, d);
            }
        if (lo > hi) return true;
        long long k0 = (long long)std::ceil(std::min(lo / dy, hi / dy)), k1 = (long long)std::floor(std::max(lo / dy, hi / dy));
        if (k1 - k0 > (long long)limit) return false;
        double period = 0;
        for (double d : pl.dashes) period += std::fabs(d);
        for (long long k = k0; k <= k1; ++k) {
            const Vec2 ok = o + u * (dx * k) + n * (dy * k);
            cuts.clear();
            for (const auto& l : h.loops) {
                for (size_t i = 0, m = l.size(); i < m && m >= 2; ++i) {
                    const Vec2 p = l[i], q = l[(i + 1) % m];
                    const double dp = Dot(p - ok, n), dq = Dot(q - ok, n);
                    if ((dp > 0) == (dq > 0)) continue;
                    const double t = dp / (dp - dq);
                    cuts.push_back(Dot(Lerp(p, q, t) - ok, u));
                }
            }
            std::sort(cuts.begin(), cuts.end());
            for (size_t i = 0; i + 1 < cuts.size(); i += 2) {
                const double s0 = cuts[i], s1 = cuts[i + 1];
                if (s1 - s0 <= 1e-12) continue;
                if (pl.dashes.empty() || period <= 1e-12) {
                    out.push_back({ok + u * s0, ok + u * s1});
                    if (out.size() > limit) return false;
                    continue;
                }
                if ((s1 - s0) / period * pl.dashes.size() > (double)limit) return false;
                // Dashes repeat from the line's origin.
                double s = std::floor(s0 / period) * period;
                while (s < s1) {
                    for (double d : pl.dashes) {
                        const double len = std::fabs(d);
                        if (d >= 0) {
                            const double a0 = std::max(s, s0), a1 = std::min(s + len, s1);
                            if (d == 0 ? (s >= s0 && s <= s1) : a1 > a0) {
                                out.push_back({ok + u * a0, ok + u * (d == 0 ? a0 : a1)});
                                if (out.size() > limit) return false;
                            }
                        }
                        s += len;
                    }
                }
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Areas: the closed region around a point
// ---------------------------------------------------------------------------

bool SameName(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::towupper(a[i]) != std::towupper(b[i])) return false;
    return true;
}

void Tessellate(const Entity& e, std::vector<Vec2>& pts) {
    if (e.type == EntityType::Polyline) {
        // Vertex by vertex, so clockwise arcs are followed the right way.
        if (e.verts.empty()) return;
        pts.push_back(e.verts[0].p);
        const size_t n = SegmentCount(e);
        for (size_t i = 0; i < n; ++i) {
            const Vertex& v = e.verts[i];
            const Vertex& w = e.verts[(i + 1) % e.verts.size()];
            if (v.bulge != 0) {
                const Prim p = ArcFromBulge(v.p, w.p, v.bulge);
                const int k = std::max(4, (int)std::ceil(p.sweep / kTau * 96));
                for (int j = 1; j < k; ++j) {
                    const double t = (double)j / k;
                    pts.push_back(Polar(p.c, p.r, p.start + p.sweep * (v.bulge > 0 ? t : 1 - t)));
                }
            }
            pts.push_back(w.p);
        }
        return;
    }
    std::vector<Prim> prims;
    Primitives(e, prims);
    for (const Prim& p : prims) {
        if (p.kind == Prim::Segment) {
            if (pts.empty() || !Near(pts.back(), p.a, 1e-12)) pts.push_back(p.a);
            pts.push_back(p.b);
            continue;
        }
        const int n = std::max(8, (int)std::ceil(p.sweep / kTau * 96));
        for (int i = 0; i <= n; ++i) {
            const Vec2 q = i == n ? p.b : (i == 0 ? p.a : Polar(p.c, p.r, p.start + p.sweep * i / n));
            if (i == 0 && !pts.empty() && Near(pts.back(), q, 1e-12)) continue;
            pts.push_back(q);
        }
    }
}

bool ClosedOutline(const Entity& e, std::vector<Vec2>& loop) {
    loop.clear();
    const bool closed = e.type == EntityType::Circle || (e.type == EntityType::Polyline && e.closed) ||
                        (e.type == EntityType::Ellipse && e.closed) ||
                        (e.type == EntityType::Spline && e.closed && !SplineTrimmed(e));
    if (!closed) return false;
    Tessellate(e, loop);
    if (loop.size() > 1 && Near(loop.front(), loop.back(), 1e-9)) loop.pop_back();
    return loop.size() >= 3;
}

namespace {
struct Graph {
    std::vector<Vec2> v;
    std::vector<std::vector<int>> adj;         // neighbours, sorted counter-clockwise
    std::vector<std::vector<size_t>> adjObj;   // the object each edge came from
    std::vector<std::vector<int>> adjArc;      // the circle it lies on (-1: straight)
    double tol = 1e-9;
    std::unordered_map<long long, std::vector<int>> grid;
    double cell = 1;

    long long Key(long long i, long long j) const { return i * 73856093LL ^ j * 19349663LL; }
    int Vertex(Vec2 p) {
        const long long i = (long long)std::floor(p.x / cell), j = (long long)std::floor(p.y / cell);
        for (long long a = i - 1; a <= i + 1; ++a)
            for (long long b = j - 1; b <= j + 1; ++b) {
                auto it = grid.find(Key(a, b));
                if (it == grid.end()) continue;
                for (int k : it->second)
                    if (Dist(v[(size_t)k], p) <= tol) return k;
            }
        v.push_back(p);
        adj.emplace_back();
        adjObj.emplace_back();
        adjArc.emplace_back();
        grid[Key(i, j)].push_back((int)v.size() - 1);
        return (int)v.size() - 1;
    }
    void Edge(int a, int b, size_t obj, int arc) {
        if (a == b) return;
        for (int x : adj[(size_t)a])
            if (x == b) return;
        adj[(size_t)a].push_back(b);
        adjObj[(size_t)a].push_back(obj);
        adjArc[(size_t)a].push_back(arc);
        adj[(size_t)b].push_back(a);
        adjObj[(size_t)b].push_back(obj);
        adjArc[(size_t)b].push_back(arc);
    }
};
}  // namespace

bool FindArea(const std::vector<const Entity*>& objects, Vec2 seed, std::vector<std::vector<Vec2>>& loops,
              std::vector<size_t>* used, double* exactArea, double* exactPerimeter) {
    loops.clear();
    if (used) used->clear();
    // 1. Every object as straight pieces (those of arcs know their circle).
    struct Seg {
        Vec2 a, b;
        size_t obj;
        int arc;
        std::vector<double> ts;
    };
    std::vector<Seg> segs;
    std::vector<std::pair<Vec2, double>> arcs;
    auto pieces = [&](const Entity& x, size_t obj) {
        std::vector<Prim> prims;
        Primitives(x, prims);
        for (const Prim& p : prims) {
            if (p.kind == Prim::Segment) {
                if (!Near(p.a, p.b, 1e-15)) segs.push_back({p.a, p.b, obj, -1, {}});
                continue;
            }
            arcs.push_back({p.c, p.r});
            const int n = std::max(8, (int)std::ceil(p.sweep / kTau * 96));
            Vec2 prev = p.a;
            for (int k = 1; k <= n; ++k) {
                const Vec2 q = k == n ? p.b : Polar(p.c, p.r, p.start + p.sweep * k / n);
                segs.push_back({prev, q, obj, (int)arcs.size() - 1, {}});
                prev = q;
            }
        }
    };
    Box all;
    for (size_t i = 0; i < objects.size(); ++i) {
        const Entity& e = *objects[i];
        if (e.type == EntityType::Point || IsAnnotation(e)) continue;
        if (e.type == EntityType::Insert || e.type == EntityType::Array) {
            std::vector<Entity> parts;
            if (e.type == EntityType::Insert) ExpandBlock(e, parts);
            else ExpandArray(e, parts);
            for (const Entity& x : parts)
                if (!IsAnnotation(x) && x.type != EntityType::Point) pieces(x, i);
            continue;
        }
        pieces(e, i);
    }
    for (const Seg& s : segs) all.Add(s.a), all.Add(s.b);
    if (segs.empty() || !all.Contains(seed)) return false;
    const double size = std::max(all.Width(), all.Height());
    const double tol = std::max(1e-9, size * 1e-7);
    // 2. Cut the pieces where they cross (sweep along x).
    std::vector<size_t> order(segs.size());
    std::iota(order.begin(), order.end(), 0);
    auto minX = [&](size_t i) { return std::min(segs[i].a.x, segs[i].b.x); };
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return minX(a) < minX(b); });
    for (size_t oi = 0; oi < order.size(); ++oi) {
        Seg& s = segs[order[oi]];
        const double sMax = std::max(s.a.x, s.b.x) + tol;
        const double sy0 = std::min(s.a.y, s.b.y) - tol, sy1 = std::max(s.a.y, s.b.y) + tol;
        for (size_t oj = oi + 1; oj < order.size() && minX(order[oj]) <= sMax; ++oj) {
            Seg& t = segs[order[oj]];
            if (std::max(t.a.y, t.b.y) < sy0 || std::min(t.a.y, t.b.y) > sy1) continue;
            const Vec2 r = s.b - s.a, q = t.b - t.a;
            const double den = Cross(r, q);
            const double lr = Length(r), lq = Length(q);
            if (lr < 1e-15 || lq < 1e-15) continue;
            if (std::fabs(den) > 1e-12 * lr * lq) {
                const double u = Cross(t.a - s.a, q) / den, w = Cross(t.a - s.a, r) / den;
                const double eu = tol / lr, ew = tol / lq;
                if (u < -eu || u > 1 + eu || w < -ew || w > 1 + ew) continue;
                s.ts.push_back(std::clamp(u, 0.0, 1.0));
                t.ts.push_back(std::clamp(w, 0.0, 1.0));
            } else if (std::fabs(Cross(t.a - s.a, r)) / lr <= tol) {
                // Lying on one another: each one's ends cut the other.
                auto cut = [&](Seg& x, Vec2 p) {
                    const Vec2 d = x.b - x.a;
                    const double f = Dot(p - x.a, d) / Dot(d, d);
                    if (f > 0 && f < 1) x.ts.push_back(f);
                };
                cut(s, t.a), cut(s, t.b), cut(t, s.a), cut(t, s.b);
            }
        }
    }
    // 3. The planar graph.
    Graph g;
    g.tol = tol;
    g.cell = tol * 4;
    for (Seg& s : segs) {
        s.ts.push_back(0);
        s.ts.push_back(1);
        std::sort(s.ts.begin(), s.ts.end());
        int prev = g.Vertex(s.a);
        for (size_t k = 1; k < s.ts.size(); ++k) {
            const int cur = g.Vertex(Lerp(s.a, s.b, s.ts[k]));
            g.Edge(prev, cur, s.obj, s.arc);
            prev = cur;
        }
    }
    // Loose ends lead nowhere: remove them.
    std::vector<int> stack;
    for (size_t i = 0; i < g.v.size(); ++i)
        if (g.adj[i].size() == 1) stack.push_back((int)i);
    while (!stack.empty()) {
        const int a = stack.back();
        stack.pop_back();
        if (g.adj[(size_t)a].size() != 1) continue;
        const int b = g.adj[(size_t)a][0];
        g.adj[(size_t)a].clear();
        g.adjObj[(size_t)a].clear();
        g.adjArc[(size_t)a].clear();
        auto& nb = g.adj[(size_t)b];
        for (size_t k = 0; k < nb.size(); ++k)
            if (nb[k] == a) {
                nb.erase(nb.begin() + (long)k);
                g.adjObj[(size_t)b].erase(g.adjObj[(size_t)b].begin() + (long)k);
                g.adjArc[(size_t)b].erase(g.adjArc[(size_t)b].begin() + (long)k);
                break;
            }
        if (nb.size() == 1) stack.push_back(b);
    }
    // Neighbours in counter-clockwise order.
    for (size_t i = 0; i < g.v.size(); ++i) {
        auto& nb = g.adj[i];
        std::vector<size_t> idx(nb.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return AngleOf(g.v[(size_t)nb[a]] - g.v[i]) < AngleOf(g.v[(size_t)nb[b]] - g.v[i]);
        });
        std::vector<int> n2, a2;
        std::vector<size_t> o2;
        for (size_t k : idx) n2.push_back(nb[k]), o2.push_back(g.adjObj[i][k]), a2.push_back(g.adjArc[i][k]);
        nb = n2;
        g.adjObj[i] = o2;
        g.adjArc[i] = a2;
    }
    // Connected parts.
    std::vector<int> comp(g.v.size(), -1);
    int comps = 0;
    for (size_t i = 0; i < g.v.size(); ++i) {
        if (comp[i] >= 0 || g.adj[i].empty()) continue;
        std::vector<int> st{(int)i};
        comp[i] = comps;
        while (!st.empty()) {
            const int a = st.back();
            st.pop_back();
            for (int b : g.adj[(size_t)a])
                if (comp[(size_t)b] < 0) comp[(size_t)b] = comps, st.push_back(b);
        }
        ++comps;
    }
    // 4. Faces: from each half-edge, turn as far right as possible at each
    // vertex; bounded faces come out counter-clockwise.
    auto slot = [&](int a, int b) {
        const auto& nb = g.adj[(size_t)a];
        for (size_t k = 0; k < nb.size(); ++k)
            if (nb[k] == b) return k;
        return (size_t)0;
    };
    struct Face {
        std::vector<int> verts;
        std::vector<size_t> objs;
        std::vector<int> arcs;
        double area = 0;
        int comp = -1;
    };
    std::vector<Face> faces;
    std::map<std::pair<int, int>, int> faceOf;
    for (size_t a = 0; a < g.v.size(); ++a) {
        for (int b0 : g.adj[a]) {
            if (faceOf.count({(int)a, b0})) continue;
            Face f;
            f.comp = comp[a];
            int u = (int)a, w = b0;
            const int id = (int)faces.size();
            for (size_t guard = 0; guard < 4 * g.v.size() + 8; ++guard) {
                if (faceOf.count({u, w})) break;
                faceOf[{u, w}] = id;
                f.verts.push_back(u);
                f.objs.push_back(g.adjObj[(size_t)u][slot(u, w)]);
                f.arcs.push_back(g.adjArc[(size_t)u][slot(u, w)]);
                const auto& nb = g.adj[(size_t)w];
                const size_t k = slot(w, u);
                const int next = nb[(k + nb.size() - 1) % nb.size()];
                u = w;
                w = next;
            }
            std::vector<Vec2> pts;
            for (int x : f.verts) pts.push_back(g.v[(size_t)x]);
            f.area = LoopArea(pts);
            faces.push_back(std::move(f));
        }
    }
    // 5. The face around the seed: the nearest edge to its right, on the
    // seed's side. Inside an island's outline, look past that island.
    const double ty = seed.y + tol * 0.37;  // not exactly through a vertex
    std::vector<bool> skip(comps, false);
    int found = -1;
    for (int tries = 0; tries <= comps && found < 0; ++tries) {
        double bestX = 1e300;
        int ba = -1, bb = -1;
        for (size_t a = 0; a < g.v.size(); ++a) {
            if (comp[a] < 0 || skip[(size_t)comp[a]]) continue;
            for (int b : g.adj[a]) {
                if ((int)a > b) continue;
                const Vec2 p = g.v[a], q = g.v[(size_t)b];
                if ((p.y > ty) == (q.y > ty)) continue;
                const double x = p.x + (q.x - p.x) * (ty - p.y) / (q.y - p.y);
                if (x > seed.x && x < bestX) bestX = x, ba = (int)a, bb = b;
            }
        }
        if (ba < 0) return false;  // nothing to the right: not closed in
        const Vec2 p = g.v[(size_t)ba], q = g.v[(size_t)bb];
        const bool left = Cross(q - p, Vec2{seed.x, ty} - p) > 0;
        const int f = left ? faceOf[{ba, bb}] : faceOf[{bb, ba}];
        if (faces[(size_t)f].area > 0) found = f;
        else skip[(size_t)comp[(size_t)ba]] = true;
    }
    if (found < 0) return false;
    const Face& outer = faces[(size_t)found];
    std::vector<Vec2> outline;
    for (int x : outer.verts) outline.push_back(g.v[(size_t)x]);
    loops.push_back(outline);
    std::vector<size_t> objs = outer.objs;
    std::vector<const Face*> chosen{&outer};
    // 6. Islands: the outer outline of every other part inside it.
    std::vector<int> outerFace(comps, -1);
    for (size_t f = 0; f < faces.size(); ++f) {
        const int c = faces[f].comp;
        if (c < 0) continue;
        if (outerFace[(size_t)c] < 0 || faces[f].area < faces[(size_t)outerFace[(size_t)c]].area) outerFace[(size_t)c] = (int)f;
    }
    for (int c = 0; c < comps; ++c) {
        if (c == outer.comp || outerFace[(size_t)c] < 0) continue;
        const Face& f = faces[(size_t)outerFace[(size_t)c]];
        if (f.verts.empty() || !InLoop(outline, g.v[(size_t)f.verts[0]])) continue;
        std::vector<Vec2> l;
        for (int x : f.verts) l.push_back(g.v[(size_t)x]);
        loops.push_back(std::move(l));
        objs.insert(objs.end(), f.objs.begin(), f.objs.end());
        chosen.push_back(&f);
    }
    if (exactArea || exactPerimeter) {
        // Arcs as arcs: each piece adds (or takes away) the sliver between it and its circle.
        double total = 0, per = 0;
        std::vector<double> areas;
        for (const Face* f : chosen) {
            double a = 0;
            const size_t n = f->verts.size();
            for (size_t k = 0; k < n; ++k) {
                const Vec2 p = g.v[(size_t)f->verts[k]], q = g.v[(size_t)f->verts[(k + 1) % n]];
                a += Cross(p, q) / 2;
                const int arc = f->arcs[k];
                if (arc < 0) {
                    per += Dist(p, q);
                    continue;
                }
                const Vec2 c = arcs[(size_t)arc].first;
                const double r = arcs[(size_t)arc].second;
                const double th = 2 * std::asin(std::min(1.0, Dist(p, q) / (2 * r)));
                per += r * th;
                const double sliver = r * r / 2 * (th - std::sin(th));
                a += Cross(q - p, c - p) > 0 ? sliver : -sliver;  // the arc bulges to the right: more on the left
            }
            areas.push_back(a);
        }
        for (size_t i = 0; i < chosen.size(); ++i) {
            int depth = 0;
            const Vec2 probe = g.v[(size_t)chosen[i]->verts[0]];
            for (size_t j = 0; j < chosen.size(); ++j)
                if (j != i && InLoop(loops[j], probe)) ++depth;
            total += (depth % 2 ? -1 : 1) * std::fabs(areas[i]);
        }
        if (exactArea) *exactArea = std::fabs(total);
        if (exactPerimeter) *exactPerimeter = per;
    }
    if (used) {
        std::sort(objs.begin(), objs.end());
        objs.erase(std::unique(objs.begin(), objs.end()), objs.end());
        *used = objs;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

std::vector<const TextData*> AttributeDefs(const BlockData& b) {
    std::vector<const TextData*> out;
    for (const Entity& e : b.items)
        if (e.type == EntityType::Text && e.annot && !e.annot->text.tag.empty()) out.push_back(&e.annot->text);
    return out;
}

bool BlockUses(const BlockData& b, const std::wstring& name, int depth) {
    if (depth > 16) return true;
    for (const Entity& e : b.items) {
        if (e.type != EntityType::Insert || !e.annot || !e.annot->block) continue;
        if (SameName(e.annot->block->name, name)) return true;
        if (BlockUses(*e.annot->block, name, depth + 1)) return true;
    }
    return false;
}

void ExpandBlock(const Entity& ref, std::vector<Entity>& out, int depth, bool deep) {
    if (ref.type != EntityType::Insert || !ref.annot || !ref.annot->block || depth > 16) return;
    const BlockData& b = *ref.annot->block;
    const double s = ref.radius > 0 ? ref.radius : 1;
    size_t attr = 0;
    for (const Entity& item : b.items) {
        Entity x = item;
        x.id = ref.id;
        if (x.type == EntityType::Text && x.annot && !x.annot->text.tag.empty()) {
            Annot& a = EditAnnot(x);
            if (attr < ref.annot->values.size()) a.text.content = ref.annot->values[attr];
            a.text.tag.clear();
            a.text.prompt.clear();
            ++attr;
        }
        // ByBlock and layer 0 take the reference's.
        if (x.layer <= 0) x.layer = ref.layer;
        if (x.color == kByBlock) x.color = ref.color;
        if (x.linetype == -2) x.linetype = ref.linetype;
        if (x.lineweight == -2) x.lineweight = ref.lineweight;
        Translate(x, -b.base);
        if (ref.closed) Mirror(x, {0, 0}, {0, 1});
        if (s != 1) Scale(x, {0, 0}, s);
        if (ref.startAngle != 0) Rotate(x, {0, 0}, ref.startAngle);
        Translate(x, ref.p0);
        if (x.type == EntityType::Insert && deep) {
            ExpandBlock(x, out, depth + 1);
        } else if (x.type == EntityType::Array && deep) {
            std::vector<Entity> copies;
            ExpandArray(x, copies);
            for (Entity& c : copies) {
                c.id = ref.id;
                if (c.type == EntityType::Insert) ExpandBlock(c, out, depth + 1);
                else out.push_back(std::move(c));
            }
        } else {
            out.push_back(std::move(x));
        }
    }
}

// ---------------------------------------------------------------------------
// The objects
// ---------------------------------------------------------------------------

bool IsAnnotType(EntityType t) {
    return t == EntityType::Text || t == EntityType::Leader || t == EntityType::Hatch || t == EntityType::Insert ||
           t == EntityType::Viewport;
}

Entity MakeViewport(Vec2 a, Vec2 b, Vec2 centre, double scale, bool locked) {
    Entity e;
    e.type = EntityType::Viewport;
    e.p0 = {std::min(a.x, b.x), std::min(a.y, b.y)};
    e.p1 = {std::max(a.x, b.x), std::max(a.y, b.y)};
    auto an = std::make_shared<Annot>();
    an->viewCentre = centre;
    an->viewScale = scale > 0 ? scale : 1;
    an->viewLocked = locked;
    e.annot = std::move(an);
    return e;
}

Vec2 ViewportToModel(const Entity& vp, Vec2 p) {
    const Vec2 c = Lerp(vp.p0, vp.p1, 0.5);
    return vp.annot->viewCentre + (p - c) / vp.annot->viewScale;
}

Vec2 ViewportToSheet(const Entity& vp, Vec2 m) {
    const Vec2 c = Lerp(vp.p0, vp.p1, 0.5);
    return c + (m - vp.annot->viewCentre) * vp.annot->viewScale;
}

Box ViewportModelBox(const Entity& vp) { return Box(ViewportToModel(vp, vp.p0), ViewportToModel(vp, vp.p1)); }

bool IsAnnotation(const Entity& e) {
    return e.type == EntityType::Text || e.type == EntityType::Leader || e.type == EntityType::Hatch ||
           e.type == EntityType::Dimension;
}

Annot& EditAnnot(Entity& e) {
    auto copy = e.annot ? std::make_shared<Annot>(*e.annot) : std::make_shared<Annot>();
    Annot& ref = *copy;
    e.annot = std::move(copy);
    return ref;
}

const TextData* TextOf(const Entity& e) {
    if ((e.type == EntityType::Text || e.type == EntityType::Leader) && e.annot) return &e.annot->text;
    return nullptr;
}

Entity MakeText(Vec2 at, TextData t) {
    Entity e;
    e.type = EntityType::Text;
    e.p0 = at;
    auto a = std::make_shared<Annot>();
    a->text = std::move(t);
    e.annot = std::move(a);
    return e;
}

Entity MakeLeader(std::vector<Vec2> pts, TextData t, double arrow) {
    Entity e;
    e.type = EntityType::Leader;
    if (!pts.empty()) e.p0 = pts[0];
    auto a = std::make_shared<Annot>();
    a->pts = std::move(pts);
    a->text = std::move(t);
    a->arrow = arrow;
    e.annot = std::move(a);
    return e;
}

Entity MakeHatch(HatchData h) {
    Entity e;
    e.type = EntityType::Hatch;
    auto a = std::make_shared<Annot>();
    a->hatch = std::move(h);
    e.annot = std::move(a);
    return e;
}

Entity MakeInsert(std::shared_ptr<const BlockData> b, Vec2 at, double scale, double rotation,
                  std::vector<std::wstring> values, bool mirrored) {
    Entity e;
    e.type = EntityType::Insert;
    e.p0 = at;
    e.radius = scale > 0 ? scale : 1;
    e.startAngle = rotation;
    e.closed = mirrored;
    auto a = std::make_shared<Annot>();
    a->block = std::move(b);
    a->values = std::move(values);
    e.annot = std::move(a);
    return e;
}

bool LeaderNote(const Entity& e, const FieldValues* fields, TextData& note, Vec2& at, Vec2* landing) {
    if (e.type != EntityType::Leader || !e.annot || e.annot->pts.empty() || e.annot->text.content.empty()) return false;
    const auto& p = e.annot->pts;
    const TextData& t = e.annot->text;
    const Vec2 last = p.back();
    const double dir = p.size() >= 2 && p[p.size() - 1].x < p[p.size() - 2].x - 1e-12 ? -1 : 1;
    const double h = t.height > 0 ? t.height : 2.5;
    // A short level landing, then the note (in a box or a balloon).
    const Vec2 land = last + Vec2{dir * h, 0};
    if (landing) *landing = land;
    note = t;
    note.rotation = 0;
    if (t.frame == 2) {
        AnnotGraphics probe;
        note.justify = 4;
        LayoutText(note, {0, 0}, fields, probe);
        double r = h;
        for (const Prim& pr : probe.lines)
            if (pr.kind == Prim::Arc) r = pr.r;
        at = land + Vec2{dir * r, 0};
        return true;
    }
    note.justify = dir > 0 ? 3 : 5;
    at = land + Vec2{dir * (t.frame == 1 ? h * 0.4 : h * 0.5), 0};
    return true;
}

namespace {
// A leader's lines, arrow and note.
void LeaderLayout(const Entity& e, const FieldValues* fields, AnnotGraphics& g) {
    const Annot& a = *e.annot;
    const auto& p = a.pts;
    if (p.empty()) return;
    for (size_t i = 0; i + 1 < p.size(); ++i) g.lines.push_back(Prim::Seg(p[i], p[i + 1]));
    if (p.size() >= 2 && a.arrow > 0 && Dist(p[0], p[1]) > a.arrow * 0.5)
    {
        const auto ar = Arrow(p[0], Normalized(p[0] - p[1]), a.arrow);
        g.fills.push_back({ar.begin(), ar.end()});
    }
    TextData note;
    Vec2 at, land;
    if (!LeaderNote(e, fields, note, at, &land)) return;
    g.lines.push_back(Prim::Seg(p.back(), land));
    LayoutText(note, at, fields, g);
}

std::vector<std::pair<Vec2, Vec2>>& HatchLines(const Annot& a) {
    if (!a.cache.segs) {
        a.cache.segs = std::make_shared<std::vector<std::pair<Vec2, Vec2>>>();
        if (!IsSolid(a.hatch)) a.cache.tooDense = !HatchSegments(a.hatch, *a.cache.segs);
        if (a.cache.tooDense) a.cache.segs->clear();
    }
    return *a.cache.segs;
}
}  // namespace

void AnnotLayout(const Entity& e, const FieldValues* fields, AnnotGraphics& g) {
    if (!e.annot) return;
    switch (e.type) {
        case EntityType::Text:
            LayoutText(e.annot->text, e.p0, fields, g);
            break;
        case EntityType::Leader:
            LeaderLayout(e, fields, g);
            break;
        case EntityType::Hatch: {
            const Annot& a = *e.annot;
            if (IsSolid(a.hatch)) {
                g.solid = true;
                g.fills = a.hatch.loops;
                break;
            }
            for (const auto& s : HatchLines(a)) g.lines.push_back(Prim::Seg(s.first, s.second));
            if (a.cache.tooDense)  // too fine to draw: just its outline
                for (const auto& l : a.hatch.loops)
                    for (size_t i = 0; i < l.size(); ++i) g.lines.push_back(Prim::Seg(l[i], l[(i + 1) % l.size()]));
            break;
        }
        default:
            break;
    }
}

void AnnotPrimitives(const Entity& e, std::vector<Prim>& out) {
    if (!e.annot) return;
    if (e.type == EntityType::Viewport) {  // its border
        const Vec2 c[4] = {e.p0, {e.p1.x, e.p0.y}, e.p1, {e.p0.x, e.p1.y}};
        for (int i = 0; i < 4; ++i) out.push_back(Prim::Seg(c[i], c[(i + 1) % 4]));
        return;
    }
    if (e.type == EntityType::Insert) {
        std::vector<Entity> parts;
        ExpandBlock(e, parts);
        for (const Entity& x : parts) Primitives(x, out);
        return;
    }
    if (e.type == EntityType::Hatch) {
        for (const auto& l : e.annot->hatch.loops)
            for (size_t i = 0; i < l.size(); ++i) out.push_back(Prim::Seg(l[i], l[(i + 1) % l.size()]));
        return;
    }
    AnnotGraphics g;
    AnnotLayout(e, nullptr, g);
    out.insert(out.end(), g.lines.begin(), g.lines.end());
    for (const auto& b : g.boxes)
        for (int i = 0; i < 4; ++i) out.push_back(Prim::Seg(b[(size_t)i], b[(size_t)(i + 1) % 4]));
    for (const auto& f : g.fills)
        for (size_t i = 0; i < f.size(); ++i) out.push_back(Prim::Seg(f[i], f[(i + 1) % f.size()]));
}

double AnnotDistance(const Entity& e, Vec2 p) {
    if (!e.annot) return 1e300;
    if (e.type == EntityType::Insert) {
        std::vector<Entity> parts;
        ExpandBlock(e, parts);
        double best = 1e300;
        for (const Entity& x : parts) best = std::min(best, DistanceTo(x, p));
        return best;
    }
    if (e.type == EntityType::Hatch && InsideLoops(e.annot->hatch.loops, p)) return 0;
    AnnotGraphics g;
    if (e.type != EntityType::Hatch && e.type != EntityType::Viewport) {
        AnnotLayout(e, nullptr, g);
        for (const auto& b : g.boxes)
            if (InLoop({b.begin(), b.end()}, p)) return 0;
    }
    std::vector<Prim> prims;
    AnnotPrimitives(e, prims);
    double best = 1e300;
    for (const Prim& pr : prims) best = std::min(best, Distance(pr, p));
    return best;
}

bool AnnotTouchesBox(const Entity& e, const Box& box) {
    if (!e.annot) return false;
    if (e.type == EntityType::Insert) {
        std::vector<Entity> parts;
        ExpandBlock(e, parts);
        for (const Entity& x : parts)
            if (TouchesBox(x, box)) return true;
        return false;
    }
    std::vector<Prim> prims;
    AnnotPrimitives(e, prims);
    for (const Prim& p : prims)
        if (PrimTouchesBox(p, box)) return true;
    // The window inside a text or a hatch.
    const Vec2 c = box.Center();
    if (e.type == EntityType::Viewport) return false;
    if (e.type == EntityType::Hatch) return InsideLoops(e.annot->hatch.loops, c);
    AnnotGraphics g;
    AnnotLayout(e, nullptr, g);
    for (const auto& b : g.boxes)
        if (InLoop({b.begin(), b.end()}, c)) return true;
    return false;
}

namespace {
// The angle `a` after mapping, kept readable after a mirror.
double MapAngle(double a, const std::function<Vec2(Vec2)>& vec, bool mirror) {
    double r = AngleOf(vec(Vec2{std::cos(a), std::sin(a)}));
    if (mirror && std::cos(r) < -1e-9) r += kPi;
    return NormalizeAngle(r) > kPi + 1e-12 ? NormalizeAngle(r) - kTau : NormalizeAngle(r);
}
}  // namespace

void AnnotMap(Entity& e, const std::function<Vec2(Vec2)>& point, const std::function<Vec2(Vec2)>& vec, bool mirror,
              double factor) {
    if (!e.annot) return;
    Annot& a = EditAnnot(e);
    if (e.type == EntityType::Viewport) {
        // It stays square to the sheet: its centre moves, its size (and the
        // drawing in it) scales.
        const Vec2 c = point(Lerp(e.p0, e.p1, 0.5));
        const Vec2 half = (e.p1 - e.p0) * (factor / 2);
        e.p0 = c - half;
        e.p1 = c + half;
        a.viewScale *= factor;
        return;
    }
    e.p0 = point(e.p0);
    switch (e.type) {
        case EntityType::Text:
        case EntityType::Leader:
            a.text.rotation = e.type == EntityType::Text ? MapAngle(a.text.rotation, vec, mirror) : 0;
            a.text.height *= factor;
            for (Vec2& p : a.pts) p = point(p);
            a.arrow *= factor;
            if (!a.pts.empty()) e.p0 = a.pts[0];
            break;
        case EntityType::Hatch: {
            HatchData& h = a.hatch;
            for (auto& l : h.loops) {
                for (Vec2& p : l) p = point(p);
                if (mirror) std::reverse(l.begin(), l.end());
            }
            h.seed = point(h.seed);
            h.origin = point(h.origin);
            h.angle = AngleOf(vec(Vec2{std::cos(h.angle), std::sin(h.angle)}));
            if (mirror) h.angle += kPi / 2;  // the pattern's lines mirrored (45 degrees becomes 135)
            h.angle = std::remainder(h.angle, kTau);
            h.scale *= factor;
            break;
        }
        case EntityType::Insert: {
            const double r = AngleOf(vec(Vec2{std::cos(e.startAngle), std::sin(e.startAngle)}));
            if (mirror) {
                e.closed = !e.closed;
                e.startAngle = std::remainder(r + kPi, kTau);
            } else {
                e.startAngle = std::remainder(r, kTau);
            }
            e.radius *= factor;
            break;
        }
        default:
            break;
    }
}

void AnnotGrips(const Entity& e, std::vector<std::pair<int, Vec2>>& out) {
    if (!e.annot) return;
    switch (e.type) {
        case EntityType::Text:
        case EntityType::Insert:
            out.push_back({0, e.p0});
            break;
        case EntityType::Leader:
            for (size_t i = 0; i < e.annot->pts.size(); ++i) out.push_back({(int)i, e.annot->pts[i]});
            break;
        case EntityType::Viewport:
            out.push_back({0, e.p0});
            out.push_back({1, {e.p1.x, e.p0.y}});
            out.push_back({2, e.p1});
            out.push_back({3, {e.p0.x, e.p1.y}});
            break;
        case EntityType::Hatch: {
            Box b;
            for (const auto& l : e.annot->hatch.loops)
                for (Vec2 p : l) b.Add(p);
            if (!b.Empty()) out.push_back({0, b.Center()});
            break;
        }
        default:
            break;
    }
}

bool DragAnnotGrip(Entity& e, int index, Vec2 from, Vec2 to) {
    if (!e.annot) return false;
    if (e.type == EntityType::Leader) {
        Annot& a = EditAnnot(e);
        if (index < 0 || (size_t)index >= a.pts.size()) return false;
        a.pts[(size_t)index] = to;
        e.p0 = a.pts[0];
        return true;
    }
    if (e.type == EntityType::Viewport && index >= 0 && index < 4) {
        // A corner: the opposite one stays; the drawing stays where it is on the sheet.
        const Vec2 corners[4] = {e.p0, {e.p1.x, e.p0.y}, e.p1, {e.p0.x, e.p1.y}};
        const Vec2 fixed = corners[(index + 2) % 4];
        if (std::fabs(to.x - fixed.x) < 1e-9 || std::fabs(to.y - fixed.y) < 1e-9) return false;
        const Vec2 anchor = ViewportToModel(e, fixed);
        Annot& a = EditAnnot(e);
        e.p0 = {std::min(fixed.x, to.x), std::min(fixed.y, to.y)};
        e.p1 = {std::max(fixed.x, to.x), std::max(fixed.y, to.y)};
        a.viewCentre = anchor + (Lerp(e.p0, e.p1, 0.5) - fixed) / a.viewScale;
        return true;
    }
    Translate(e, to - from);
    return true;
}
