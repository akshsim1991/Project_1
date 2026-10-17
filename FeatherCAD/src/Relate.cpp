// Relate.cpp - see Relate.h.
#include "Relate.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <map>

#include "Expr.h"
#include "Grips.h"

const wchar_t* RelKindName(RelKind k) {
    switch (k) {
        case RelKind::Coincident: return L"Coincident";
        case RelKind::Horizontal: return L"Horizontal";
        case RelKind::Vertical: return L"Vertical";
        case RelKind::Parallel: return L"Parallel";
        case RelKind::Perpendicular: return L"Perpendicular";
        case RelKind::Collinear: return L"Collinear";
        case RelKind::Tangent: return L"Tangent";
        case RelKind::Concentric: return L"Concentric";
        case RelKind::Equal: return L"Equal";
        case RelKind::Fix: return L"Fixed";
        case RelKind::Symmetric: return L"Symmetric";
        case RelKind::Midpoint: return L"Midpoint";
        case RelKind::Regular: return L"Regular polygon";
    }
    return L"";
}

int RelElementCount(RelKind k) {
    switch (k) {
        case RelKind::Horizontal:
        case RelKind::Vertical:
        case RelKind::Fix:
        case RelKind::Regular: return 1;
        case RelKind::Symmetric: return 3;
        default: return 2;
    }
}

// ---------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------

Element ElementOf(const Entity& e, int grip) {
    Element el;
    if (grip >= 0) {
        std::vector<Grip> grips;
        GripsOf(e, grips);
        for (const Grip& g : grips)
            if (g.index == grip) {
                el.kind = ElemKind::Point;
                el.a = g.p;
                return el;
            }
        return el;
    }
    const size_t k = (size_t)(-1 - grip);
    switch (e.type) {
        case EntityType::Line:
            if (k == 0) el = {ElemKind::Line, e.p0, e.p1, 0};
            break;
        case EntityType::Circle:
        case EntityType::Arc:
            if (k == 0) el = {ElemKind::Circle, e.p0, {}, e.radius};
            break;
        case EntityType::Polyline: {
            if (k >= SegmentCount(e)) break;
            const Vertex& v = e.verts[k];
            const Vec2 w = e.verts[(k + 1) % e.verts.size()].p;
            if (v.bulge == 0) {
                el = {ElemKind::Line, v.p, w, 0};
            } else {
                const Prim p = ArcFromBulge(v.p, w, v.bulge);
                el = {ElemKind::Circle, p.c, {}, p.r};
            }
            break;
        }
        default:
            break;
    }
    return el;
}

Element ElementOf(const Document& doc, const DimRef& ref) {
    const Entity* e = ref.id ? doc.Find(ref.id) : nullptr;
    return e ? ElementOf(*e, ref.grip) : Element{};
}

namespace {
Vec2 Dir(const Element& l) { return l.b - l.a; }

// A closed polyline of straight sides (what Regular polygon is for).
bool IsPlainPolygon(const Entity& e) {
    if (e.type != EntityType::Polyline || !e.closed || e.verts.size() < 3) return false;
    for (const Vertex& v : e.verts)
        if (v.bulge != 0) return false;
    return true;
}

Vec2 VertexMean(const Entity& e) {
    Vec2 m{0, 0};
    for (const Vertex& v : e.verts) m = m + v.p;
    return m * (1.0 / (double)e.verts.size());
}

// Where a mark goes for an element: beside a line's middle, on top of a circle, at a point.
Vec2 MarkAnchor(const Element& el) {
    switch (el.kind) {
        case ElemKind::Line: return Lerp(el.a, el.b, 0.5);
        case ElemKind::Circle: return el.a + Vec2{0, el.r};
        default: return el.a;
    }
}
}  // namespace

bool PlaceRelationMarks(const Document& doc, Entity& rel) {
    if (rel.type != EntityType::Relation || !rel.annot) return false;
    const RelationData& r = rel.annot->rel;
    std::vector<Vec2> pts;
    if (r.kind == RelKind::Regular) {  // in the middle of the polygon
        const Entity* pe = doc.Find(r.refs[0].id);
        if (!pe || !IsPlainPolygon(*pe)) return false;
        pts.push_back(VertexMean(*pe));
    }
    Element el[3];
    for (int i = 0; i < RelElementCount(r.kind) && r.kind != RelKind::Regular; ++i) {
        el[i] = ElementOf(doc, r.refs[(size_t)i]);
        if (el[i].kind == ElemKind::None) return false;
        pts.push_back(MarkAnchor(el[i]));
    }
    if (r.kind == RelKind::Tangent) {  // one mark, where they touch
        Vec2 t = pts[0];
        if (el[0].kind == ElemKind::Line || el[1].kind == ElemKind::Line) {
            const Element& l = el[0].kind == ElemKind::Line ? el[0] : el[1];
            const Element& c = el[0].kind == ElemKind::Line ? el[1] : el[0];
            const Vec2 u = Normalized(l.b - l.a);
            t = l.a + u * Dot(c.a - l.a, u);
        } else if (Dist(el[0].a, el[1].a) > 1e-12) {
            const Element& big = el[0].r >= el[1].r ? el[0] : el[1];
            const Element& other = el[0].r >= el[1].r ? el[1] : el[0];
            t = big.a + Normalized(other.a - big.a) * big.r;
        }
        pts = {t};
    }
    // Marks of other relationships at the same spot: this one goes beside them.
    std::vector<int> slots(pts.size(), 0);
    if (r.kind != RelKind::Coincident)
        for (const auto& kv : doc.Entities()) {
            const Entity& o = kv.second;
            if (o.type != EntityType::Relation || !o.annot || kv.first == rel.id || (rel.id && kv.first > rel.id) ||
                o.annot->rel.kind == RelKind::Coincident)
                continue;
            for (size_t i = 0; i < pts.size(); ++i)
                for (size_t j = 0; j < o.annot->pts.size(); ++j)
                    if (Near(o.annot->pts[j], pts[i], 1e-9 * (1 + std::fabs(pts[i].x) + std::fabs(pts[i].y))))
                        slots[i] = std::max(slots[i], (j < o.annot->markSlots.size() ? o.annot->markSlots[j] : 0) + 1);
        }
    if (pts == rel.annot->pts && slots == rel.annot->markSlots) return true;
    Annot& a = EditAnnot(rel);
    a.pts = pts;
    a.markSlots = slots;
    rel.p0 = pts[0];
    return true;
}

bool MakeRelation(const Document& doc, RelKind kind, const std::array<DimRef, 3>& refs, double markSize, Entity& out,
                  std::wstring& why) {
    const int n = RelElementCount(kind);
    Element el[3];
    for (int i = 0; i < n; ++i) {
        el[i] = ElementOf(doc, refs[(size_t)i]);
        if (el[i].kind == ElemKind::None) {
            why = L"That is not a point, line, circle or arc that can be related.";
            return false;
        }
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (refs[(size_t)i].id == refs[(size_t)j].id && refs[(size_t)i].grip == refs[(size_t)j].grip) {
                why = L"Pick two different elements.";
                return false;
            }
    auto is = [&](int i, ElemKind k) { return el[i].kind == k; };
    double param = 1;
    bool ok = true;
    switch (kind) {
        case RelKind::Horizontal:
        case RelKind::Vertical:
            ok = is(0, ElemKind::Line);
            why = L"Horizontal and vertical are for lines.";
            break;
        case RelKind::Fix:
            ok = true;
            break;
        case RelKind::Parallel:
        case RelKind::Perpendicular:
        case RelKind::Collinear:
            ok = is(0, ElemKind::Line) && is(1, ElemKind::Line);
            why = L"Pick two lines.";
            break;
        case RelKind::Concentric:
            ok = is(0, ElemKind::Circle) && is(1, ElemKind::Circle);
            why = L"Pick two circles or arcs.";
            break;
        case RelKind::Equal:
            ok = (is(0, ElemKind::Line) && is(1, ElemKind::Line)) || (is(0, ElemKind::Circle) && is(1, ElemKind::Circle));
            why = L"Pick two lines, or two circles or arcs.";
            break;
        case RelKind::Tangent: {
            ok = (is(0, ElemKind::Circle) || is(1, ElemKind::Circle)) && !is(0, ElemKind::Point) && !is(1, ElemKind::Point);
            why = L"Tangent is between a circle or arc and a line, or two circles or arcs.";
            if (!ok) break;
            if (is(0, ElemKind::Circle) && is(1, ElemKind::Circle)) {
                // Outside each other (+1) or one inside the other (-1), as they are now.
                const double d = Dist(el[0].a, el[1].a);
                param = std::fabs(d - (el[0].r + el[1].r)) <= std::fabs(d - std::fabs(el[0].r - el[1].r)) ? 1 : -1;
            } else {
                const Element& l = is(0, ElemKind::Line) ? el[0] : el[1];
                const Element& c = is(0, ElemKind::Line) ? el[1] : el[0];
                param = Cross(Normalized(Dir(l)), c.a - l.a) >= 0 ? 1 : -1;  // which side the circle is on
            }
            break;
        }
        case RelKind::Coincident:
            ok = is(0, ElemKind::Point) || is(1, ElemKind::Point);
            why = L"Coincident needs a point (an end or centre) and a point or a line or circle.";
            break;
        case RelKind::Midpoint:
            ok = (is(0, ElemKind::Point) && is(1, ElemKind::Line)) || (is(1, ElemKind::Point) && is(0, ElemKind::Line));
            why = L"Midpoint needs a point and a line.";
            break;
        case RelKind::Symmetric:
            ok = is(0, ElemKind::Point) && is(1, ElemKind::Point) && is(2, ElemKind::Line);
            why = L"Symmetric needs two points and a line to be symmetric about.";
            break;
        case RelKind::Regular: {
            const Entity* pe = doc.Find(refs[0].id);
            ok = pe && IsPlainPolygon(*pe);
            why = L"Regular polygon is for a closed polyline with straight sides.";
            break;
        }
    }
    if (!ok) return false;
    why.clear();
    std::array<DimRef, 3> r = refs;
    // A point first, then what it lies on.
    if ((kind == RelKind::Coincident || kind == RelKind::Midpoint) && !is(0, ElemKind::Point)) std::swap(r[0], r[1]);
    if (kind == RelKind::Regular) r[0].grip = -1;  // the whole polyline
    out = Entity{};
    out.type = EntityType::Relation;
    auto a = std::make_shared<Annot>();
    a->rel.kind = kind;
    a->rel.refs = r;
    a->rel.param = param;
    a->arrow = markSize > 0 ? markSize : 2.5;
    out.annot = std::move(a);
    out.color = 30;  // orange, as relationship marks usually are
    if (!PlaceRelationMarks(doc, out)) {
        why = L"That element cannot be related.";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The solver
// ---------------------------------------------------------------------------

namespace {
// One condition to keep: a relationship or a locked dimension.
struct Con {
    RelKind kind = RelKind::Coincident;
    std::array<DimRef, 4> refs{};
    double param = 1;
    const DimData* dim = nullptr;  // a locked dimension
    uint64_t id = 0;
};

// The coordinates of an object that may move.
void GetVars(const Entity& e, std::vector<double>& v) {
    switch (e.type) {
        case EntityType::Line: v.insert(v.end(), {e.p0.x, e.p0.y, e.p1.x, e.p1.y}); break;
        case EntityType::Circle: v.insert(v.end(), {e.p0.x, e.p0.y, e.radius}); break;
        case EntityType::Arc: v.insert(v.end(), {e.p0.x, e.p0.y, e.radius, e.startAngle, e.endAngle}); break;
        case EntityType::Point: v.insert(v.end(), {e.p0.x, e.p0.y}); break;
        case EntityType::Polyline:
            for (const Vertex& x : e.verts) v.insert(v.end(), {x.p.x, x.p.y});
            break;
        default: break;
    }
}

void SetVars(Entity& e, const double* v) {
    switch (e.type) {
        case EntityType::Line: e.p0 = {v[0], v[1]}, e.p1 = {v[2], v[3]}; break;
        case EntityType::Circle: e.p0 = {v[0], v[1]}, e.radius = std::max(1e-9, v[2]); break;
        case EntityType::Arc:
            e.p0 = {v[0], v[1]}, e.radius = std::max(1e-9, v[2]);
            e.startAngle = v[3], e.endAngle = v[4];
            break;
        case EntityType::Point: e.p0 = {v[0], v[1]}; break;
        case EntityType::Polyline:
            for (size_t i = 0; i < e.verts.size(); ++i) e.verts[i].p = {v[2 * i], v[2 * i + 1]};
            break;
        default: break;
    }
}

// Solves A y = b (A symmetric, n x n) by Gaussian elimination with pivoting.
bool SolveLinear(std::vector<double> A, std::vector<double> b, size_t n, std::vector<double>& y) {
    for (size_t c = 0; c < n; ++c) {
        size_t p = c;
        for (size_t r = c + 1; r < n; ++r)
            if (std::fabs(A[r * n + c]) > std::fabs(A[p * n + c])) p = r;
        if (std::fabs(A[p * n + c]) < 1e-300) return false;
        if (p != c) {
            for (size_t k = 0; k < n; ++k) std::swap(A[c * n + k], A[p * n + k]);
            std::swap(b[c], b[p]);
        }
        for (size_t r = c + 1; r < n; ++r) {
            const double f = A[r * n + c] / A[c * n + c];
            if (f == 0) continue;
            for (size_t k = c; k < n; ++k) A[r * n + k] -= f * A[c * n + k];
            b[r] -= f * b[c];
        }
    }
    y.assign(n, 0);
    for (size_t i = n; i-- > 0;) {
        double s = b[i];
        for (size_t k = i + 1; k < n; ++k) s -= A[i * n + k] * y[k];
        y[i] = s / A[i * n + i];
    }
    return true;
}

class Solver {
public:
    Solver(Document& doc, const std::vector<Con>& cons, const std::vector<uint64_t>& ids, const std::set<uint64_t>& fixed,
           const std::set<uint64_t>& keep, const std::set<std::pair<uint64_t, int>>& pinned)
        : m_doc(doc), m_cons(cons) {
        Box box;
        for (uint64_t id : ids) {
            const Entity* e = doc.Find(id);
            if (!e) continue;
            m_ents[id] = *e;
            box.Add(Bounds(*e));
            if (fixed.count(id)) continue;
            Slot s{id, m_x.size(), 0};
            GetVars(*e, m_x);
            s.count = m_x.size() - s.start;
            if (!s.count) continue;
            const double w = keep.count(id) ? 1e4 : 1;
            // A pinned point (where a driven dimension starts) holds firmer still.
            int pin = -1;
            for (const auto& pp : pinned)
                if (pp.first == id) {
                    if (e->type == EntityType::Line && (pp.second == 0 || pp.second == 1)) pin = pp.second;
                    else if (e->type == EntityType::Polyline && pp.second >= 0 && pp.second < (int)e->verts.size()) pin = pp.second;
                    else if (pp.second == 0 && (e->type == EntityType::Circle || e->type == EntityType::Arc || e->type == EntityType::Point)) pin = 0;
                }
            for (size_t i = 0; i < s.count; ++i) {
                double wi = w;
                if (pin >= 0 && (int)(i / 2) == pin && (e->type != EntityType::Circle && e->type != EntityType::Arc ? true : i < 2)) wi *= 1e6;
                if (e->type == EntityType::Arc && i >= 3) wi *= e->radius * e->radius;  // angles: as far as the ends move
                m_w.push_back(wi);
            }
            m_slots.push_back(s);
        }
        m_L = std::max({box.Width(), box.Height(), 1.0});
    }

    // How far each condition is from holding, as things are (0: it holds).
    std::vector<double> Residuals() {
        std::vector<double> c;
        Eval(m_x, c);
        return c;
    }
    double Size() const { return m_L; }

    // True when everything holds.
    bool Run(std::wstring& why) {
        std::vector<double> c;
        Eval(m_x, c);
        const double tol = 1e-9 * m_L;
        auto worst = [](const std::vector<double>& v) {
            double m = 0;
            for (double x : v) m = std::max(m, std::fabs(x));
            return m;
        };
        if (c.empty()) return true;
        const size_t n = m_x.size(), m = c.size();
        for (int iter = 0; iter < 80 && worst(c) > tol; ++iter) {
            if (n == 0) break;
            // Jacobian by small steps.
            std::vector<double> J(m * n), c2;
            for (size_t j = 0; j < n; ++j) {
                const double h = 1e-7 * (m_L + std::fabs(m_x[j]));
                std::vector<double> x = m_x;
                x[j] += h;
                Eval(x, c2);
                for (size_t i = 0; i < m; ++i) J[i * n + j] = (c2[i] - c[i]) / h;
            }
            // The smallest (weighted) step that makes c zero to first order:
            // dx = -W^-1 J^T (J W^-1 J^T)^-1 c.
            std::vector<double> A(m * m, 0);
            double trace = 0;
            for (size_t a = 0; a < m; ++a)
                for (size_t b = a; b < m; ++b) {
                    double s = 0;
                    for (size_t j = 0; j < n; ++j) s += J[a * n + j] * J[b * n + j] / m_w[j];
                    A[a * m + b] = A[b * m + a] = s;
                    if (a == b) trace += s;
                }
            const double eps = 1e-10 * (trace / (double)m + 1e-30);
            for (size_t a = 0; a < m; ++a) A[a * m + a] += eps;
            std::vector<double> y;
            if (!SolveLinear(A, c, m, y)) break;
            std::vector<double> dx(n, 0);
            for (size_t j = 0; j < n; ++j) {
                double s = 0;
                for (size_t a = 0; a < m; ++a) s += J[a * n + j] * y[a];
                dx[j] = -s / m_w[j];
            }
            // Smaller steps if a full one makes things worse.
            double step = 1;
            const double before = worst(c);
            for (int k = 0; k < 12; ++k, step /= 2) {
                std::vector<double> x = m_x;
                for (size_t j = 0; j < n; ++j) x[j] += step * dx[j];
                Eval(x, c2);
                if (worst(c2) < before || k == 11) {
                    m_x = x;
                    c = c2;
                    break;
                }
            }
        }
        const bool ok = worst(c) <= 1e-7 * m_L;
        if (!ok) why = L"The relationships and locked dimensions cannot all hold at once (too many, or they contradict each other).";
        return ok;
    }

    // The objects as solved; changed ones replaced in the document.
    void Write() {
        Apply(m_x);
        for (const Slot& s : m_slots) {
            const Entity& now = m_ents[s.id];
            const Entity* was = m_doc.Find(s.id);
            if (!was) continue;
            std::vector<double> a, b;
            GetVars(*was, a);
            GetVars(now, b);
            bool moved = false;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i) moved |= std::fabs(a[i] - b[i]) > 1e-12 * m_L;
            if (moved) m_doc.Replace(now);
        }
    }

private:
    struct Slot {
        uint64_t id;
        size_t start, count;
    };

    void Apply(const std::vector<double>& x) {
        for (const Slot& s : m_slots) SetVars(m_ents[s.id], x.data() + s.start);
    }

    const Entity* Ent(uint64_t id) {
        auto it = m_ents.find(id);
        return it != m_ents.end() ? &it->second : m_doc.Find(id);
    }

    Element El(const DimRef& r) {
        auto it = m_ents.find(r.id);
        if (it != m_ents.end()) return ElementOf(it->second, r.grip);
        return ElementOf(m_doc, r);  // not moving
    }

    void Eval(const std::vector<double>& x, std::vector<double>& out) {
        Apply(x);
        out.clear();
        const double L = m_L;
        for (const Con& c : m_cons) {
            if (c.dim) {
                DimData d = *c.dim;
                for (size_t i = 0; i < 4; ++i) {
                    if (!d.refs[i].id) continue;
                    const Element el = El(d.refs[i]);
                    if (el.kind == ElemKind::Point) d.pts[i] = el.a;
                    else if (el.kind == ElemKind::Circle && i == 0) {
                        const Vec2 dir = Normalized(d.pts[1] - d.pts[0]);
                        d.pts[0] = el.a;
                        d.pts[1] = el.a + dir * el.r;
                    }
                }
                const bool angle = d.kind == DimData::Angular || d.kind == DimData::AngularCoord;
                out.push_back((DimMeasure(d) - d.value) * (angle ? L : 1));
                continue;
            }
            const Element a = El(c.refs[0]), b = El(c.refs[1]), e3 = El(c.refs[2]);
            auto unit = [](const Element& l) {
                const Vec2 d = l.b - l.a;
                const double len = Length(d);
                return len > 1e-12 ? d / len : Vec2{1, 0};
            };
            auto pointOn = [&](Vec2 p, const Element& on) {
                if (on.kind == ElemKind::Line) out.push_back(Cross(unit(on), p - on.a));
                else if (on.kind == ElemKind::Circle) out.push_back(Dist(p, on.a) - on.r);
                else out.push_back(p.x - on.a.x), out.push_back(p.y - on.a.y);
            };
            switch (c.kind) {
                case RelKind::Horizontal: out.push_back(a.b.y - a.a.y); break;
                case RelKind::Vertical: out.push_back(a.b.x - a.a.x); break;
                case RelKind::Parallel: out.push_back(L * Cross(unit(a), unit(b))); break;
                case RelKind::Perpendicular: out.push_back(L * Dot(unit(a), unit(b))); break;
                case RelKind::Collinear:
                    out.push_back(L * Cross(unit(a), unit(b)));
                    out.push_back(Cross(unit(a), b.a - a.a));
                    break;
                case RelKind::Concentric:
                    out.push_back(b.a.x - a.a.x);
                    out.push_back(b.a.y - a.a.y);
                    break;
                case RelKind::Equal:
                    if (a.kind == ElemKind::Line) out.push_back(Length(Dir(a)) - Length(Dir(b)));
                    else out.push_back(a.r - b.r);
                    break;
                case RelKind::Tangent:
                    if (a.kind == ElemKind::Circle && b.kind == ElemKind::Circle) {
                        const double d = Dist(a.a, b.a);
                        out.push_back(c.param > 0 ? d - (a.r + b.r) : d - std::fabs(a.r - b.r));
                    } else {
                        const Element& l = a.kind == ElemKind::Line ? a : b;
                        const Element& ci = a.kind == ElemKind::Line ? b : a;
                        out.push_back(Cross(unit(l), ci.a - l.a) * c.param - ci.r);
                    }
                    break;
                case RelKind::Coincident: pointOn(a.a, b); break;
                case RelKind::Midpoint: {
                    const Vec2 m = Lerp(b.a, b.b, 0.5);
                    out.push_back(a.a.x - m.x);
                    out.push_back(a.a.y - m.y);
                    break;
                }
                case RelKind::Symmetric: {
                    const Vec2 u = unit(e3), m = Lerp(a.a, b.a, 0.5);
                    out.push_back(Cross(u, m - e3.a));
                    out.push_back(Dot(b.a - a.a, u));
                    break;
                }
                case RelKind::Regular: {
                    // Each corner, turned about the middle by one step, lands on the next.
                    const Entity* pe = Ent(c.refs[0].id);
                    if (!pe || !IsPlainPolygon(*pe)) break;
                    const auto& v = pe->verts;
                    const size_t n = v.size();
                    const Vec2 m = VertexMean(*pe);
                    double area2 = 0;
                    for (size_t i = 0; i < n; ++i) area2 += Cross(v[i].p, v[(i + 1) % n].p);
                    const double t = (area2 >= 0 ? kTau : -kTau) / (double)n, cs = std::cos(t), sn = std::sin(t);
                    for (size_t i = 0; i < n; ++i) {
                        const Vec2 d = v[i].p - m, e = v[(i + 1) % n].p - m;
                        out.push_back(cs * d.x - sn * d.y - e.x);
                        out.push_back(sn * d.x + cs * d.y - e.y);
                    }
                    break;
                }
                case RelKind::Fix: break;
            }
        }
    }

    Document& m_doc;
    const std::vector<Con>& m_cons;
    std::map<uint64_t, Entity> m_ents;
    std::vector<Slot> m_slots;
    std::vector<double> m_x, m_w;
    double m_L = 1;
};

// The condition a relationship or a locked dimension stands for.
Con ConOf(const Entity& e, uint64_t id) {
    Con c;
    c.id = id;
    if (e.type == EntityType::Relation && e.annot) {
        const RelationData& r = e.annot->rel;
        c.kind = r.kind;
        for (size_t i = 0; i < 3; ++i) c.refs[i] = r.refs[i];
        c.param = r.param;
    } else if (e.dim) {
        c.dim = e.dim.get();
        c.refs = e.dim->refs;
    }
    return c;
}

std::vector<uint64_t> RefIds(const Con& c) {
    std::vector<uint64_t> ids;
    for (const DimRef& r : c.refs)
        if (r.id) ids.push_back(r.id);
    return ids;
}
}  // namespace

SolveReport SolveRelations(Document& doc, const std::set<uint64_t>& changed, const std::set<uint64_t>& keep,
                           const std::set<std::pair<uint64_t, int>>& pinned) {
    SolveReport rep;
    // Everything that must hold, and what touches what.
    std::vector<Con> all;
    std::set<uint64_t> fixed;
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        if (e.type == EntityType::Relation && e.annot) {
            const RelationData& r = e.annot->rel;
            if (r.kind == RelKind::Fix) {
                if (r.refs[0].id) fixed.insert(r.refs[0].id);
                continue;
            }
            all.push_back(ConOf(e, kv.first));
        } else if (e.type == EntityType::Dimension && e.dim && e.dim->locked && HasRefs(e)) {
            all.push_back(ConOf(e, kv.first));
        }
    }
    if (all.empty()) return rep;
    std::map<uint64_t, std::vector<size_t>> byEntity;
    for (size_t i = 0; i < all.size(); ++i)
        for (uint64_t id : RefIds(all[i])) byEntity[id].push_back(i);
    // The connected part around what changed.
    std::set<uint64_t> ents;
    std::set<size_t> cons;
    std::vector<uint64_t> stack;
    for (uint64_t id : changed) {
        stack.push_back(id);
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].id == id && cons.insert(i).second)
                for (uint64_t r : RefIds(all[i])) stack.push_back(r);
    }
    while (!stack.empty()) {
        const uint64_t id = stack.back();
        stack.pop_back();
        if (!ents.insert(id).second) continue;
        if (fixed.count(id)) continue;  // a fixed object does not pass changes on
        auto it = byEntity.find(id);
        if (it == byEntity.end()) continue;
        for (size_t i : it->second)
            if (cons.insert(i).second)
                for (uint64_t r : RefIds(all[i])) stack.push_back(r);
    }
    if (cons.empty()) return rep;
    std::vector<Con> used;
    for (size_t i : cons) used.push_back(all[i]);
    rep.constraints = used.size();
    std::vector<uint64_t> ids(ents.begin(), ents.end());
    std::wstring why;
    // First with what was just edited held exactly where it is; if that
    // cannot work, let it move too (as little as possible).
    if (!keep.empty()) {
        std::set<uint64_t> held = fixed;
        held.insert(keep.begin(), keep.end());
        if (std::any_of(ids.begin(), ids.end(), [&](uint64_t id) { return !held.count(id); })) {
            Solver first(doc, used, ids, held, keep, pinned);
            if (first.Run(why)) {
                first.Write();
                rep.ok = true;
                return rep;
            }
            why.clear();
        }
    }
    Solver s(doc, used, ids, fixed, keep, pinned);
    rep.ok = s.Run(why);
    rep.message = why;
    s.Write();
    return rep;
}

// ---------------------------------------------------------------------------
// Formulas
// ---------------------------------------------------------------------------

namespace {
double DimValueForFormulas(const DimData& d) {
    const double m = DimMeasure(d);
    return d.kind == DimData::Angular || d.kind == DimData::AngularCoord ? ToDegrees(m) : m;
}
}  // namespace

bool LookupName(const Document& doc, const std::wstring& name, double& value) {
    for (const Variable& v : doc.Variables())
        if (SameName(v.name, name)) {
            value = v.value;
            return true;
        }
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        if (e.type == EntityType::Dimension && e.dim && SameName(e.dim->name, name)) {
            value = DimValueForFormulas(*e.dim);
            return true;
        }
    }
    return false;
}

bool EvaluateFormulas(Document& doc, std::set<uint64_t>& changed, std::wstring& error) {
    // Everything with a name, and what it is worked out from.
    struct Item {
        std::wstring formula;
        uint64_t dim = 0;
        size_t var = 0;
        bool hasFormula = false;
        double value = 0;
        int state = 0;  // 0 not yet, 1 working on it, 2 done
    };
    std::map<std::wstring, Item> items;  // by upper-case name
    auto key = [](std::wstring s) {
        for (wchar_t& c : s) c = (wchar_t)std::towupper(c);
        return s;
    };
    bool any = false;
    for (size_t i = 0; i < doc.Variables().size(); ++i) {
        const Variable& v = doc.Variables()[i];
        Item it;
        it.formula = v.formula;
        it.var = i;
        it.hasFormula = true;
        items[key(v.name)] = it;
        any = true;
    }
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        if (e.type != EntityType::Dimension || !e.dim || e.dim->name.empty()) continue;
        Item it;
        it.dim = kv.first;
        it.formula = e.dim->formula;
        it.hasFormula = !e.dim->formula.empty();
        it.value = DimValueForFormulas(*e.dim);
        any |= it.hasFormula;
        items[key(e.dim->name)] = it;
    }
    if (!any) return true;
    error.clear();
    std::function<bool(const std::wstring&, double&)> value = [&](const std::wstring& name, double& out) -> bool {
        auto it = items.find(key(name));
        if (it == items.end()) return false;
        Item& item = it->second;
        if (!item.hasFormula || item.state == 2) {
            out = item.value;
            return true;
        }
        if (item.state == 1) {
            if (error.empty()) error = L"\"" + name + L"\" is worked out from itself (a formula goes round in a circle).";
            out = item.value;
            return true;
        }
        item.state = 1;
        std::wstring why;
        double v = 0;
        if (!EvalExpression(item.formula, value, v, why)) {
            if (error.empty()) error = L"The formula of \"" + name + L"\": " + why;
            v = item.value;
        }
        item.value = v;
        item.state = 2;
        out = v;
        return true;
    };
    std::vector<double> varValues(doc.Variables().size(), 0);
    for (auto& kv : items) {
        double v;
        value(kv.first, v);
        if (kv.second.dim == 0) varValues[kv.second.var] = v;
    }
    doc.SetVariableValues(varValues);
    // Dimensions with formulas hold the result.
    for (const auto& kv : items) {
        const Item& it = kv.second;
        if (!it.dim || !it.hasFormula) continue;
        const Entity* e = doc.Find(it.dim);
        if (!e || !e->dim) continue;
        const bool angle = e->dim->kind == DimData::Angular || e->dim->kind == DimData::AngularCoord;
        const double target = angle ? ToRadians(it.value) : it.value;
        if (e->dim->locked && std::fabs(e->dim->value - target) <= 1e-12 * (1 + std::fabs(target))) continue;
        if (!(target > 0) && e->dim->kind != DimData::Ordinate) {
            if (error.empty()) error = L"Dimension \"" + e->dim->name + L"\" would be zero or negative.";
            continue;
        }
        Entity x = *e;
        DimData d = *x.dim;
        d.locked = true;
        d.value = target;
        x.dim = std::make_shared<const DimData>(std::move(d));
        doc.Replace(x);
        changed.insert(it.dim);
    }
    return error.empty();
}

// ---------------------------------------------------------------------------
// Relationships while drawing
// ---------------------------------------------------------------------------

namespace {
// The points of an object where another object can be joined to it: a
// line's or an arc's ends, a polyline's vertices, a point.
void JoinPoints(const Entity& e, std::vector<Grip>& out) {
    out.clear();
    std::vector<Grip> grips;
    GripsOf(e, grips);
    for (const Grip& g : grips) {
        bool end = false;
        switch (e.type) {
            case EntityType::Line: end = g.index == 0 || g.index == 1; break;
            case EntityType::Arc: end = g.index == 1 || g.index == 2; break;
            case EntityType::Polyline: end = g.index >= 0 && g.index < (int)e.verts.size(); break;
            case EntityType::Point: end = g.index == 0; break;
            default: break;
        }
        if (end) out.push_back(g);
    }
}

bool Joinable(const Document& doc, const Entity& e) {
    return !e.construction && doc.Visible(e) &&
           (e.type == EntityType::Line || e.type == EntityType::Arc || e.type == EntityType::Polyline || e.type == EntityType::Point);
}

bool SameRef(const DimRef& a, const DimRef& b) { return a.id == b.id && a.grip == b.grip; }
}  // namespace

int AddImpliedRelations(Document& doc, const std::set<uint64_t>& ids) {
    // The relationships there already are.
    std::vector<RelationData> have;
    for (const auto& kv : doc.Entities())
        if (kv.second.type == EntityType::Relation && kv.second.annot) have.push_back(kv.second.annot->rel);
    auto exists = [&](RelKind k, DimRef a, DimRef b) {
        for (const RelationData& r : have) {
            if (r.kind != k) continue;
            if ((SameRef(r.refs[0], a) && SameRef(r.refs[1], b)) || (SameRef(r.refs[0], b) && SameRef(r.refs[1], a))) return true;
        }
        return false;
    };
    const double markSize = doc.Settings().dimStyle.textHeight * 0.8;
    int added = 0;
    auto add = [&](RelKind k, DimRef a, DimRef b) {
        if (exists(k, a, b)) return;
        Entity rel;
        std::wstring why;
        if (!MakeRelation(doc, k, {a, b, DimRef{}}, markSize, rel, why)) return;
        EditAnnot(rel).rel.implied = true;
        rel.layer = -1;
        doc.Add(rel);
        have.push_back(rel.annot->rel);
        ++added;
    };

    // Through every object joined end to end with these, a connect at each joint.
    std::set<uint64_t> seen;
    std::vector<uint64_t> queue;
    for (uint64_t id : ids)
        if (const Entity* e = doc.Find(id); e && Joinable(doc, *e) && seen.insert(id).second) queue.push_back(id);
    std::vector<Grip> mine, theirs;
    while (!queue.empty()) {
        const uint64_t id = queue.back();
        queue.pop_back();
        const Entity* e = doc.Find(id);
        if (!e) continue;
        JoinPoints(*e, mine);
        const std::vector<Grip> myPoints = mine;
        for (const Grip& g : myPoints) {
            const double tol = 1e-7 * (1 + std::fabs(g.p.x) + std::fabs(g.p.y));
            for (const auto& kv : doc.Entities()) {
                if (kv.first == id || !Joinable(doc, kv.second) || !Bounds(kv.second).Inflated(tol).Contains(g.p)) continue;
                JoinPoints(kv.second, theirs);
                for (const Grip& o : theirs) {
                    if (!Near(o.p, g.p, tol)) continue;
                    add(RelKind::Coincident, {id, g.index}, {kv.first, o.index});
                    if (seen.insert(kv.first).second) queue.push_back(kv.first);
                }
            }
        }
    }
    // Rectangles (closed four-sided polylines with square corners) keep their
    // right angles; other regular polygons (POLYGON) stay regular.
    for (uint64_t id : seen) {
        const Entity* e = doc.Find(id);
        if (!e || !IsPlainPolygon(*e)) continue;
        if (e->verts.size() != 4) {
            const size_t n = e->verts.size();
            const Vec2 m = VertexMean(*e);
            const double side = Dist(e->verts[0].p, e->verts[1].p), r = Dist(e->verts[0].p, m);
            bool regular = side > 1e-9;
            for (size_t i = 0; i < n && regular; ++i)
                regular = std::fabs(Dist(e->verts[i].p, e->verts[(i + 1) % n].p) - side) <= 1e-6 * side &&
                          std::fabs(Dist(e->verts[i].p, m) - r) <= 1e-6 * side;
            if (regular && !exists(RelKind::Regular, {id, -1}, {})) {
                Entity rel;
                std::wstring why;
                if (MakeRelation(doc, RelKind::Regular, {DimRef{id, -1}, DimRef{}, DimRef{}}, markSize, rel, why)) {
                    EditAnnot(rel).rel.implied = true;
                    rel.layer = -1;
                    doc.Add(rel);
                    have.push_back(rel.annot->rel);
                    ++added;
                }
            }
            continue;
        }
        bool rect = true;
        for (size_t i = 0; i < 4 && rect; ++i) {
            const Vec2 u = e->verts[(i + 1) % 4].p - e->verts[i].p, w = e->verts[(i + 2) % 4].p - e->verts[(i + 1) % 4].p;
            rect = e->verts[i].bulge == 0 && std::fabs(Dot(Normalized(u), Normalized(w))) < 1e-6;
        }
        if (!rect) continue;
        for (int i = 0; i < 3; ++i) add(RelKind::Perpendicular, {id, -1 - i}, {id, -2 - i});
    }
    return added;
}

namespace {
bool HasRelation(const Document& doc, RelKind k, const DimRef& a, const DimRef& b) {
    for (const auto& kv : doc.Entities()) {
        if (kv.second.type != EntityType::Relation || !kv.second.annot) continue;
        const RelationData& r = kv.second.annot->rel;
        if (r.kind != k) continue;
        if ((SameRef(r.refs[0], a) && SameRef(r.refs[1], b)) || (SameRef(r.refs[0], b) && SameRef(r.refs[1], a))) return true;
    }
    return false;
}
}  // namespace

int ReleaseBrokenRelations(Document& doc, const std::set<uint64_t>& changed, const std::map<uint64_t, Entity>& before) {
    std::vector<uint64_t> ids;
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        const DimRef* refs = nullptr;
        size_t n = 0;
        if (e.type == EntityType::Relation && e.annot) refs = e.annot->rel.refs.data(), n = 3;
        else if (e.type == EntityType::Dimension && e.dim && e.dim->locked) refs = e.dim->refs.data(), n = 4;
        bool touched = false;
        for (size_t i = 0; i < n; ++i) touched |= refs[i].id && changed.count(refs[i].id);
        if (touched) ids.push_back(kv.first);
    }
    int released = 0;
    for (uint64_t id : ids) {
        const Entity* x = doc.Find(id);
        if (!x) continue;
        Entity e = *x;
        const Con con = ConOf(e, id);
        std::vector<uint64_t> refIds = RefIds(con);
        bool gone = false;
        for (uint64_t r : refIds) gone |= !doc.Find(r);
        std::vector<double> res;
        double size = 1;
        if (!gone) {
            const std::vector<Con> one{con};
            const std::set<uint64_t> all(refIds.begin(), refIds.end());
            Solver s(doc, one, refIds, all, {}, {});
            res = s.Residuals();
            size = s.Size();
        }
        double worst = 0;
        for (double r : res) worst = std::max(worst, std::fabs(r));
        if (!gone && worst <= 1e-7 * size) continue;  // still holds
        if (e.type == EntityType::Dimension) {
            // A locked dimension takes the size the object now has.
            if (gone || res.size() != 1) continue;
            DimData d = *e.dim;
            const bool angle = d.kind == DimData::Angular || d.kind == DimData::AngularCoord;
            d.value += res[0] / (angle ? size : 1);
            e.dim = std::make_shared<const DimData>(std::move(d));
            doc.Replace(e);
            continue;
        }
        RelationData r = e.annot->rel;
        if (!gone && r.kind == RelKind::Coincident) {
            // One point stayed where it was and the other was cut away from it:
            // whatever end is left at that corner (the other piece of a broken
            // line) takes its place.
            int stayed = -1;
            for (int k = 0; k < 2; ++k) {
                const Element now = ElementOf(doc, r.refs[(size_t)k]);
                if (now.kind != ElemKind::Point) continue;
                auto it = before.find(r.refs[(size_t)k].id);
                const Element was = it == before.end() ? now : ElementOf(it->second, r.refs[(size_t)k].grip);
                if (was.kind == ElemKind::Point && Near(was.a, now.a, 1e-9 * (1 + size))) stayed = stayed < 0 ? k : -2;
            }
            if (stayed >= 0) {
                const DimRef keepRef = r.refs[(size_t)stayed], lost = r.refs[(size_t)(1 - stayed)];
                const Vec2 at = ElementOf(doc, keepRef).a;
                const double tol = 1e-7 * (1 + std::fabs(at.x) + std::fabs(at.y));
                DimRef found;
                std::vector<Grip> pts;
                for (const auto& kv : doc.Entities()) {
                    if (kv.first == keepRef.id || !Joinable(doc, kv.second) || !Bounds(kv.second).Inflated(tol).Contains(at)) continue;
                    JoinPoints(kv.second, pts);
                    for (const Grip& g : pts)
                        if (Near(g.p, at, tol) && !SameRef({kv.first, g.index}, lost) &&
                            !HasRelation(doc, RelKind::Coincident, keepRef, {kv.first, g.index}))
                            found = {kv.first, g.index};
                    if (found.id) break;
                }
                if (found.id) {
                    r.refs[(size_t)(1 - stayed)] = found;
                    EditAnnot(e).rel = r;
                    if (PlaceRelationMarks(doc, e)) {
                        doc.Replace(e);
                        continue;
                    }
                }
            }
        }
        doc.Erase(id);
        ++released;
    }
    return released;
}

void AutoRelate(Document& doc, uint64_t id) {
    const Entity* e = doc.Find(id);
    if (!e || (e->type != EntityType::Line && e->type != EntityType::Arc)) return;
    const Entity me = *e;
    const double size = doc.Settings().dimStyle.textHeight * 0.8;
    auto add = [&](RelKind k, std::array<DimRef, 3> refs) {
        if (HasRelation(doc, k, refs[0], refs[1])) return;
        Entity rel;
        std::wstring why;
        if (MakeRelation(doc, k, refs, size, rel, why)) {
            rel.layer = -1;
            doc.Add(rel);
        }
    };
    Vec2 ends[2];
    int endGrip[2];
    double len;
    if (me.type == EntityType::Line) {
        len = Dist(me.p0, me.p1);
        if (len <= 1e-9) return;
        const double tol = 1e-9 * (1 + len);
        if (std::fabs(me.p1.y - me.p0.y) <= tol) add(RelKind::Horizontal, {DimRef{id, -1}, {}, {}});
        else if (std::fabs(me.p1.x - me.p0.x) <= tol) add(RelKind::Vertical, {DimRef{id, -1}, {}, {}});
        ends[0] = me.p0, ends[1] = me.p1, endGrip[0] = 0, endGrip[1] = 1;
    } else {
        len = me.radius;
        if (len <= 1e-9) return;
        ends[0] = Polar(me.p0, me.radius, me.startAngle), ends[1] = Polar(me.p0, me.radius, me.endAngle);
        endGrip[0] = 1, endGrip[1] = 2;
    }
    const double tol = 1e-9 * (1 + len + std::fabs(ends[0].x) + std::fabs(ends[0].y));
    // Its ends on other objects' ends, centres and midpoints.
    for (int k = 0; k < 2; ++k) {
        std::vector<Grip> grips;
        for (const auto& kv : doc.Entities()) {
            const Entity& o = kv.second;
            if (kv.first == id || IsAnnotation(o) || o.type == EntityType::Relation || o.type == EntityType::Insert ||
                o.type == EntityType::Array || !Bounds(o).Inflated(tol).Contains(ends[k]))
                continue;
            grips.clear();
            GripsOf(o, grips);
            for (const Grip& g : grips)
                if (Near(g.p, ends[k], tol)) {
                    add(RelKind::Coincident, {DimRef{id, endGrip[k]}, DimRef{kv.first, g.index}, {}});
                    // An arc that runs smoothly on from a line or another arc (a fillet): tangent too.
                    if (me.type == EntityType::Arc && (o.type == EntityType::Line || o.type == EntityType::Arc)) {
                        const Element other = ElementOf(o, -1);
                        const double gap = other.kind == ElemKind::Line
                                               ? std::fabs(std::fabs(Cross(Normalized(other.b - other.a), me.p0 - other.a)) - me.radius)
                                               : std::min(std::fabs(Dist(me.p0, other.a) - (me.radius + other.r)),
                                                          std::fabs(Dist(me.p0, other.a) - std::fabs(me.radius - other.r)));
                        if (gap <= 1e-7 * (1 + len)) add(RelKind::Tangent, {DimRef{id, -1}, DimRef{kv.first, -1}, {}});
                    }
                    break;
                }
        }
    }
}
