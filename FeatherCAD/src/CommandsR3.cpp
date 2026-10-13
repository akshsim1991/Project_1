// CommandsR3.cpp - construction helpers (BISECT, PARALLELTHRU, PERPTHRU,
// TANFROM), ALIGN, and the engineering drawing constructions of EngMore.h:
// SCALES, POINTPROJ, TRACES, TRANSITION, INTERSECT and PERSPECTIVE; text and
// dimensions on isometric drawings (ISOTEXT, ISODIM), QDIM, DIMTOL (limits
// and fits), GROUP and UNGROUP, HIDEOBJECTS, ISOLATE and UNHIDE, FIND and
// TEXTSTYLE.
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <set>

#include "Annot.h"
#include "CommandUtil.h"
#include "EngGraphics.h"
#include "EngMore.h"
#include "Sheet.h"
#include "Lang.h"
#include "Personal.h"

namespace {
double TextSize(Engine* E) { return E->Doc().Settings().dimStyle.textHeight; }

// A straight piece of an object near p: a line, or the polyline segment there.
bool PickSegment(Engine* E, Vec2 p, Vec2& a, Vec2& b) {
    const uint64_t id = E->PickEntity(p, true);
    const Entity* e = id ? E->Doc().Find(id) : nullptr;
    if (!e) return false;
    if (e->type == EntityType::Line) {
        a = e->p0, b = e->p1;
        return true;
    }
    if (e->type == EntityType::Polyline && e->verts.size() >= 2) {
        double best = 1e300;
        for (size_t i = 0; i < SegmentCount(*e); ++i) {
            const Vec2 s = e->verts[i].p, t = e->verts[(i + 1) % e->verts.size()].p;
            if (e->verts[i].bulge != 0) continue;
            const double dd = Distance(Prim::Seg(s, t), p);
            if (dd < best) best = dd, a = s, b = t;
        }
        return best < 1e300;
    }
    return false;
}

// Where the (endless) lines a0-a1 and b0-b1 cross; false if parallel.
bool CrossPoint(Vec2 a0, Vec2 a1, Vec2 b0, Vec2 b1, Vec2& x) {
    const Vec2 r = a1 - a0, s = b1 - b0;
    const double d = Cross(r, s);
    if (std::fabs(d) < 1e-12 * (1 + Length(r) * Length(s))) return false;
    x = a0 + r * (Cross(b0 - a0, s) / d);
    return true;
}

// The foot of the perpendicular from p on the endless line a-b.
Vec2 FootOn(Vec2 a, Vec2 b, Vec2 p) {
    const Vec2 d = b - a;
    const double l2 = Dot(d, d);
    return l2 < 1e-24 ? a : a + d * (Dot(p - a, d) / l2);
}

// ---------------------------------------------------------------------------
// BISECT: the perpendicular bisector of a line, or the bisector of the
// angle between two lines
// ---------------------------------------------------------------------------
class BisectCmd : public Command {
    bool m_have = false;
    Vec2 m_a0, m_a1, m_pickA;

    static Entity PerpBisector(Vec2 a, Vec2 b) {
        const Vec2 m = (a + b) * 0.5, n = Perp(Normalized(b - a)) * (Dist(a, b) * 0.6);
        return Entity::Line(m - n, m + n);
    }
    bool AngleBisector(Vec2 b0, Vec2 b1, Vec2 pickB, Entity& out, std::wstring& msg) const {
        Vec2 x;
        if (!CrossPoint(m_a0, m_a1, b0, b1, x)) {
            // Parallel: the line midway between them.
            const Vec2 shift = (FootOn(b0, b1, m_a0) - m_a0) * 0.5;
            if (Length(shift) < 1e-9) return msg = L"The two lines are on top of each other.", false;
            const Vec2 u = Normalized(m_a1 - m_a0);
            double lo = 0, hi = Dist(m_a0, m_a1);
            for (Vec2 q : {b0, b1}) {
                const double t = Dot(q - m_a0, u);
                lo = std::min(lo, t), hi = std::max(hi, t);
            }
            out = Entity::Line(m_a0 + u * lo + shift, m_a0 + u * hi + shift);
            msg = L"The lines are parallel: the line midway between them is drawn.";
            return true;
        }
        // Each line's arm: from the corner towards where it was clicked.
        auto arm = [&](Vec2 p0, Vec2 p1, Vec2 pick) {
            Vec2 d = pick - x;
            if (Length(d) < 1e-9) d = Dist(p0, x) > Dist(p1, x) ? p0 - x : p1 - x;
            const Vec2 u = Normalized(p1 - p0);
            return Dot(d, u) >= 0 ? u : u * -1.0;
        };
        const Vec2 ua = arm(m_a0, m_a1, m_pickA), ub = arm(b0, b1, pickB);
        Vec2 dir = ua + ub;
        if (Length(dir) < 1e-9) dir = Perp(ua);
        dir = Normalized(dir);
        double len = 0;
        for (Vec2 q : {m_a0, m_a1, b0, b1}) len = std::max(len, Dist(q, x));
        out = Entity::Line(x, x + dir * len);
        msg = L"The angle " + std::wstring(L"is ") + E->Ang(std::acos(std::max(-1.0, std::min(1.0, Dot(ua, ub))))) +
              L"\x00B0; its bisector is drawn from the corner.";
        return true;
    }

public:
    void Start() override { E->Ask(Pick(L"Click the line to bisect (or the first arm of an angle)")); }
    void OnPoint(Vec2 p) override {
        Vec2 a, b;
        if (!PickSegment(E, p, a, b)) return E->Msg(L"Click a line (or a straight piece of a polyline).");
        if (!m_have) {
            m_have = true;
            m_a0 = a, m_a1 = b, m_pickA = p;
            return E->Ask(Pick(L"Click the other arm of the angle, or press Enter for the perpendicular bisector of this line",
                               kAcceptEmpty));
        }
        Entity e;
        std::wstring msg;
        if (!AngleBisector(a, b, p, e, msg)) return E->Msg(msg);
        AddKeepingStyle(E, L"Angle bisector", {e});
        E->Msg(msg);
        E->Finish();
    }
    void OnEnter() override {
        if (!m_have) return E->Finish();
        AddKeepingStyle(E, L"Perpendicular bisector", {PerpBisector(m_a0, m_a1)});
        E->Msg(L"Perpendicular bisector drawn through the midpoint (" + E->Pt((m_a0 + m_a1) * 0.5) + L").");
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// PARALLELTHRU and PERPTHRU: through a point, parallel or square to a line
// ---------------------------------------------------------------------------
class ThroughCmd : public Command {
    bool m_perp;
    bool m_have = false;
    Vec2 m_a, m_b;

    bool Make(Vec2 p, Entity& out) const {
        if (!m_perp) {
            const Vec2 shift = p - FootOn(m_a, m_b, p);
            out = Entity::Line(m_a + shift, m_b + shift);
            return true;
        }
        const Vec2 f = FootOn(m_a, m_b, p);
        if (Dist(f, p) < 1e-9) {  // on the line: a perpendicular there, as long as the line
            const Vec2 n = Perp(Normalized(m_b - m_a)) * (Dist(m_a, m_b) * 0.5);
            out = Entity::Line(p - n, p + n);
            return true;
        }
        out = Entity::Line(p, f);
        return true;
    }

public:
    explicit ThroughCmd(bool perp) : m_perp(perp) {}
    void Start() override {
        E->Ask(Pick(m_perp ? L"Click the line to draw a perpendicular to" : L"Click the line to draw a parallel to"));
    }
    void OnPoint(Vec2 p) override {
        if (!m_have) {
            if (!PickSegment(E, p, m_a, m_b)) return E->Msg(L"Click a line (or a straight piece of a polyline).");
            m_have = true;
            return E->Ask(P(m_perp ? L"Click the point it goes through (Enter: done)" : L"Click the point the parallel goes through (Enter: done)",
                            kAcceptPoint | kAcceptEmpty));
        }
        Entity e;
        if (!Make(p, e)) return;
        AddKeepingStyle(E, m_perp ? L"Perpendicular" : L"Parallel", {e});
        if (m_perp) {
            const Vec2 f = FootOn(m_a, m_b, p);
            const double t = Dot(f - m_a, m_b - m_a) / std::max(1e-24, Dot(m_b - m_a, m_b - m_a));
            E->Msg(L"Foot at " + E->Pt(f) + L", " + E->Len(Dist(p, f)) + L" from the point" +
                   (t < -1e-9 || t > 1 + 1e-9 ? L" (on the line extended)." : L"."));
        }
    }
    void OnEnter() override { E->Finish(); }
    void GetPreview(Vec2 c, Preview& out) override {
        if (!m_have) return;
        Entity e;
        if (Make(c, e)) out.entities.push_back(e);
        if (m_perp) {
            const Vec2 f = FootOn(m_a, m_b, c);
            const double t = Dot(f - m_a, m_b - m_a) / std::max(1e-24, Dot(m_b - m_a, m_b - m_a));
            if (t < 0) out.guides.push_back({m_a, f});
            if (t > 1) out.guides.push_back({m_b, f});
        }
    }
};

// ---------------------------------------------------------------------------
// TANFROM: the two tangents from a point to a circle or arc
// ---------------------------------------------------------------------------
class TanFromCmd : public Command {
    uint64_t m_id = 0;
    Vec2 m_c;
    double m_r = 0;

    std::vector<Entity> Make(Vec2 p) const {
        std::vector<Vec2> pts;
        TangentPoints(Prim::Circle(m_c, m_r), p, pts);
        std::vector<Entity> out;
        for (Vec2 t : pts) out.push_back(Entity::Line(p, t));
        return out;
    }

public:
    void Start() override { E->Ask(Pick(L"Click the circle or arc")); }
    void OnPoint(Vec2 p) override {
        if (!m_id) {
            const uint64_t id = E->PickEntity(p, true);
            const Entity* e = id ? E->Doc().Find(id) : nullptr;
            if (!e || (e->type != EntityType::Circle && e->type != EntityType::Arc)) return E->Msg(L"Click a circle or an arc.");
            m_id = id, m_c = e->p0, m_r = e->radius;
            return E->Ask(P(L"Click the point outside it the tangents go from (Enter: done)", kAcceptPoint | kAcceptEmpty));
        }
        const std::vector<Entity> out = Make(p);
        if (out.empty()) return E->Msg(L"The point must be outside the circle.");
        AddKeepingStyle(E, L"Tangents from a point", out);
        E->Msg(L"Two tangents drawn, each " + E->Len(Dist(out[0].p0, out[0].p1)) + L" long, touching at " + E->Pt(out[0].p1) +
               L" and " + E->Pt(out[1].p1) + L".");
    }
    void OnEnter() override { E->Finish(); }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_id) out.entities = Make(c);
    }
};

// ---------------------------------------------------------------------------
// ALIGN: move and turn objects so that two of their points land on two
// others (and scale them to fit, if wanted)
// ---------------------------------------------------------------------------
class AlignCmd : public Command {
    enum Step { S1, D1, S2, D2, Fit } m_step = S1;
    std::vector<uint64_t> m_ids;
    Vec2 m_s1, m_d1, m_s2, m_d2;

    void Transform(Entity& e, bool two, bool fit) const {
        Translate(e, m_d1 - m_s1);
        if (!two) return;
        const double turn = AngleOf(m_d2 - m_d1) - AngleOf(m_s2 - m_s1);
        Rotate(e, m_d1, turn);
        if (fit && Dist(m_s1, m_s2) > 1e-12) Scale(e, m_d1, Dist(m_d1, m_d2) / Dist(m_s1, m_s2));
    }
    void Apply(bool two, bool fit) {
        Document& d = E->Doc();
        d.BeginChange(L"Align");
        for (uint64_t id : m_ids) {
            const Entity* e = d.Find(id);
            if (!e) continue;
            Entity m = *e;
            Transform(m, two, fit);
            d.Replace(m);
        }
        d.EndChange();
        std::wstring msg = std::to_wstring(m_ids.size()) + L" object(s) aligned";
        if (two) msg += L", turned " + E->Ang(AngleOf(m_d2 - m_d1) - AngleOf(m_s2 - m_s1)) + L"\x00B0";
        if (two && fit) msg += L", scaled " + E->Len(Dist(m_d1, m_d2) / std::max(1e-12, Dist(m_s1, m_s2)));
        E->Msg(msg + L".");
        E->ClearSelection();
        E->Finish();
    }

public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        m_ids = SelectionIds(E);
        E->Ask(P(L"Click the first point on the objects (source)", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case S1:
                m_s1 = p, m_step = D1;
                return E->Ask(From(P(L"Click where it goes (destination)", kAcceptPoint), m_s1));
            case D1:
                m_d1 = p, m_step = S2;
                return E->Ask(P(L"Click the second point on the objects (Enter: just move them)", kAcceptPoint | kAcceptEmpty));
            case S2:
                if (Dist(p, m_s1) < 1e-9) return E->Msg(L"The second point must be away from the first.");
                m_s2 = p, m_step = D2;
                return E->Ask(From(P(L"Click where the second point goes", kAcceptPoint), m_d1));
            case D2:
                if (Dist(p, m_d1) < 1e-9) return E->Msg(L"The second point must be away from the first.");
                m_d2 = p, m_step = Fit;
                return E->Ask(P(L"Scale the objects so the points match exactly", kAcceptKeyword | kAcceptEmpty,
                                {{L"N", L"No"}, {L"Y", L"Yes"}}, L"No"));
            default: return;
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == Fit) Apply(true, k == L"Y");
    }
    void OnEnter() override {
        if (m_step == S2) return Apply(false, false);
        if (m_step == Fit) return Apply(true, false);
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == D1) {
            m_d1 = c;
            PreviewTransformed(E, m_ids, out, [&](Entity& e) { Transform(e, false, false); });
            out.guides.push_back({m_s1, c});
        } else if (m_step == D2) {
            m_d2 = c;
            if (Dist(c, m_d1) > 1e-9) PreviewTransformed(E, m_ids, out, [&](Entity& e) { Transform(e, true, false); });
            out.guides.push_back({m_s1, m_d1});
            out.guides.push_back({m_s2, c});
        }
    }
};

// ---------------------------------------------------------------------------
// The constructions: questions asked in turn (Enter keeps the answer shown),
// then a click to place it, previewed at the cursor.
// ---------------------------------------------------------------------------
class AskThenPlace : public Command {
protected:
    struct Q {
        std::wstring text;
        double* num = nullptr;
        double min = 0;  // a number must be more than this (or at least it when !strict)
        bool strict = true;
        int* choice = nullptr;  // or one of the keywords
        std::vector<Keyword> kws;
        std::wstring* str = nullptr;  // or text, checked
        std::function<bool(const std::wstring&)> check;
    };
    std::vector<Q> m_qs;
    size_t m_i = 0;

    static Q Num(std::wstring text, double* v, double min = 0, bool strict = true) {
        Q q;
        q.text = std::move(text), q.num = v, q.min = min, q.strict = strict;
        return q;
    }
    static Q Choice(std::wstring text, int* c, std::vector<Keyword> kws) {
        Q q;
        q.text = std::move(text), q.choice = c, q.kws = std::move(kws);
        return q;
    }
    static Q Str(std::wstring text, std::wstring* s, std::function<bool(const std::wstring&)> check) {
        Q q;
        q.text = std::move(text), q.str = s, q.check = std::move(check);
        return q;
    }
    virtual bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) = 0;
    virtual std::wstring PlaceText() const = 0;
    virtual const wchar_t* Step() const = 0;
    virtual void Questions() {}  // fills m_qs (again, after a choice may change them)
    virtual void Placed() { E->Finish(); }
    void Ask() {
        Questions();
        if (m_i < m_qs.size()) {
            const Q& q = m_qs[m_i];
            if (q.choice) return E->Ask(P(q.text.c_str(), kAcceptKeyword | kAcceptEmpty, q.kws, q.kws[(size_t)*q.choice].name));
            if (q.str) return E->Ask(TextPrompt(q.text.c_str(), 0, *q.str));
            const double v = *q.num;
            return E->Ask(P(q.text.c_str(), kAcceptNumber | kAcceptEmpty, {}, v < 0 ? L"-" + E->Len(-v) : E->Len(v)));
        }
        E->Ask(P(PlaceText().c_str(), kAcceptPoint));
    }
    void Next() {
        ++m_i;
        Ask();
    }

public:
    void Start() override { Ask(); }
    void OnNumber(double v) override {
        if (m_i >= m_qs.size() || !m_qs[m_i].num) return;
        const Q& q = m_qs[m_i];
        if (v < q.min || (q.strict && v <= q.min)) return E->Msg(L"That value is too small.");
        *q.num = v;
        Next();
    }
    bool OnText(const std::wstring& s) override {
        if (m_i >= m_qs.size() || !m_qs[m_i].str) return false;
        if (s.empty()) return false;
        if (!m_qs[m_i].check(s)) return true;
        *m_qs[m_i].str = s;
        Next();
        return true;
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_i >= m_qs.size() || !m_qs[m_i].choice) return;
        const Q& q = m_qs[m_i];
        for (size_t i = 0; i < q.kws.size(); ++i)
            if (q.kws[i].key == k) *q.choice = (int)i;
        Next();
    }
    void OnEnter() override {
        if (m_i < m_qs.size()) return Next();
        E->Finish();
    }
    void OnPoint(Vec2 p) override {
        if (m_i < m_qs.size()) return;
        std::vector<Entity> out;
        std::wstring why;
        if (!Build(p, out, why)) {
            E->Msg(why);
            return E->Finish();
        }
        AddKeepingStyle(E, Step(), out);
        Placed();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_i < m_qs.size()) return;
        std::wstring why;
        Build(c, out.entities, why);
    }
};

// SCALES: a plain or diagonal scale.
int g_scDiag = 0;
std::wstring g_scRf = L"1:50";
double g_scLength = 6;
int g_scUnit = 3;  // m
const std::vector<Keyword> kScaleUnits = {{L"KM", L"km"}, {L"HM", L"hm"}, {L"DAM", L"dam"}, {L"M", L"m"}, {L"DM", L"dm"}, {L"CM", L"cm"}, {L"MM", L"mm"}};

class ScalesCmd : public AskThenPlace {
    void Questions() override {
        if (!m_qs.empty()) return;
        m_qs = {Choice(L"Which scale", &g_scDiag, {{L"P", L"Plain"}, {L"D", L"Diagonal"}}),
                Str(L"Type the RF (representative fraction), such as 1:50, 1/2500 or 2:1", &g_scRf,
                    [this](const std::wstring& s) {
                        double rf;
                        if (ReadRf(s, rf)) return true;
                        E->Msg(L"Type it as 1:50 (paper : real), 1/50 or a number such as 0.02.");
                        return false;
                    }),
                Choice(L"In which unit are the main divisions", &g_scUnit,
                       {{L"KM", L"km"}, {L"HM", L"hm"}, {L"DAM", L"dam"}, {L"M", L"m"}, {L"DM", L"dm"}, {L"CM", L"cm"}, {L"MM", L"mm"}}),
                Num(L"Type the most it must measure, in those units (the scale's length)", &g_scLength)};
    }
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        double rf = 0;
        ReadRf(g_scRf, rf);
        return ScaleDrawing(g_scDiag == 1, at, rf, g_scLength, kScaleUnits[(size_t)g_scUnit].name, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click the bottom left corner of the scale"; }
    const wchar_t* Step() const override { return g_scDiag ? L"Diagonal scale" : L"Plain scale"; }
};

// POINTPROJ: the projections of points, one after another.
std::wstring g_ppName = L"a";
double g_ppUp = 20, g_ppFront = 30;

class PointProjCmd : public AskThenPlace {
    void Questions() override {
        if (!m_qs.empty()) return;
        m_qs = {Str(L"Type the point's name, such as a, p or b", &g_ppName, [](const std::wstring&) { return true; }),
                Num(L"How far is it above HP (below: type it negative, such as -20)", &g_ppUp, -1e9, false),
                Num(L"How far is it in front of VP (behind: type it negative)", &g_ppFront, -1e9, false)};
    }
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring&) override {
        DimStyle st = E->Doc().Settings().dimStyle;
        out = PointProjection(at, g_ppUp, g_ppFront, g_ppName, st);
        return true;
    }
    std::wstring PlaceText() const override { return L"Click the point on the XY line where its projector goes"; }
    const wchar_t* Step() const override { return L"Projections of a point"; }
    void Placed() override {
        E->Msg(L"Point " + g_ppName + L": " + QuadrantName(g_ppUp, g_ppFront) + L".");
        g_ppName = NextName(g_ppName);
        m_i = 0;
        Ask();
    }
};

// TRANSITION: the development of a square-to-round transition piece.
double g_trA = 80, g_trB = 60, g_trD = 50, g_trH = 60;
class TransitionCmd : public AskThenPlace {
    void Questions() override {
        if (!m_qs.empty()) return;
        m_qs = {Num(L"Type the length of the rectangle at the bottom (left to right)", &g_trA),
                Num(L"Type its width (front to back; the same for a square)", &g_trB),
                Num(L"Type the diameter of the circle at the top", &g_trD),
                Num(L"Type the height between them", &g_trH)};
    }
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        return DevelopTransition(g_trA, g_trB, g_trD, g_trH, at, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click where the development starts (point M on the seam)"; }
    const wchar_t* Step() const override { return L"Transition piece"; }
};

// INTERSECT: an upright solid penetrated by a horizontal one.
int g_isUp = 0, g_isAcross = 0;
IntersectSpec g_is;

class IntersectCmd : public AskThenPlace {
    void Questions() override {
        if (!m_qs.empty()) return;
        const std::vector<Keyword> kw = {{L"C", L"Cylinder"}, {L"P", L"Prism"}, {L"D", L"Diamond"}};
        m_qs = {Choice(L"The upright (standing) solid", &g_isUp, kw),
                Num(L"Type its diameter (or the side of the square)", &g_is.uprightSize),
                Num(L"Type its height", &g_is.uprightHeight),
                Choice(L"The horizontal solid", &g_isAcross, kw),
                Num(L"Type its diameter (or the side of the square)", &g_is.acrossSize),
                Num(L"Type its length", &g_is.acrossLength),
                Num(L"How high is its axis above the base", &g_is.axisHeight),
                Num(L"How far is its axis in front of the upright one (0: they meet; behind: negative)", &g_is.offset, -1e9, false)};
    }
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        IntersectSpec s = g_is;
        s.upright = (IntersectSpec::Shape)g_isUp;
        s.across = (IntersectSpec::Shape)g_isAcross;
        s.offset = -g_is.offset;  // the spec measures it towards the back
        return IntersectSolids(s, at, std::max(s.uprightSize, s.acrossSize) + s.acrossSize + 20, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click the centre of the upright solid's base in the front view (the top view goes below)"; }
    const wchar_t* Step() const override { return L"Intersection of solids"; }
};

// PERSPECTIVE: a block by the visual ray method.
PerspectiveSpec g_ps;
class PerspectiveCmd : public AskThenPlace {
    void Questions() override {
        if (!m_qs.empty()) return;
        m_qs = {Num(L"Type the block's length", &g_ps.length),
                Num(L"Type its width", &g_ps.width),
                Num(L"Type its height", &g_ps.height),
                Num(L"Type the angle of its length to the picture plane (degrees; a vertical edge touches it)", &g_ps.angle),
                Num(L"How far is the station point in front of the picture plane", &g_ps.distance),
                Num(L"How far is it to the right of the touching edge (left: negative)", &g_ps.sideways, -1e9, false),
                Num(L"How high is the eye above the ground", &g_ps.eye, 0, false)};
    }
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        return PerspectiveDrawing(g_ps, at, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click where the touching edge meets the picture plane in the plan (the views go below)"; }
    const wchar_t* Step() const override { return L"Perspective"; }
};

// ---------------------------------------------------------------------------
// TRACES: the HT and VT of a line from its two views
// ---------------------------------------------------------------------------
class TracesCmd : public Command {
    enum Step { Front, Top, XY } m_step = Front;
    Vec2 m_f0, m_f1, m_t0, m_t1;

public:
    void Start() override { E->Ask(Pick(L"Click the front view of the line (a'b')")); }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Front:
                if (!PickSegment(E, p, m_f0, m_f1)) return E->Msg(L"Click a line.");
                m_step = Top;
                return E->Ask(Pick(L"Click the top view of the line (ab)"));
            case Top:
                if (!PickSegment(E, p, m_t0, m_t1)) return E->Msg(L"Click a line.");
                m_step = XY;
                return E->Ask(P(L"Click a point on the XY line", kAcceptPoint));
            case XY: {
                double xy = p.y;
                Vec2 a, b;
                if (PickSegment(E, p, a, b) && std::fabs(a.y - b.y) < 1e-6 * (1 + Dist(a, b))) xy = (a.y + b.y) / 2;
                LineViews v;
                if (m_f1.x < m_f0.x) std::swap(m_f0, m_f1);
                v.a1 = m_f0, v.b1 = m_f1;
                if (std::fabs(m_t0.x - m_f0.x) + std::fabs(m_t1.x - m_f1.x) <= std::fabs(m_t1.x - m_f0.x) + std::fabs(m_t0.x - m_f1.x))
                    v.a = m_t0, v.b = m_t1;
                else
                    v.a = m_t1, v.b = m_t0;
                const Traces t = LineTraces(v, xy);
                if (!t.hasHT && !t.hasVT) {
                    E->Msg(L"This line has no traces: it is parallel to both HP and VP (or a view is a point).");
                    return E->Finish();
                }
                AddKeepingStyle(E, L"Traces", TraceConstruction(v, xy, t, TextSize(E)));
                std::wstring msg;
                if (t.hasHT) msg += L"HT: " + E->Len(std::fabs(t.h.y - xy)) + (t.h.y < xy ? L" in front of VP. " : L" behind VP. ");
                else msg += L"No HT (the line is parallel to HP). ";
                if (t.hasVT) msg += L"VT: " + E->Len(std::fabs(t.v1.y - xy)) + (t.v1.y > xy ? L" above HP." : L" below HP.");
                else msg += L"No VT (the line is parallel to VP).";
                E->Msg(msg);
                return E->Finish();
            }
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == XY) out.guides.push_back({{std::min(m_f0.x, m_t0.x) - 20, c.y}, {std::max(m_f1.x, m_t1.x) + 20, c.y}});
    }
};
// ---------------------------------------------------------------------------
// ISOTEXT: text lying on an isometric face (turned and slanted 30 degrees)
// ---------------------------------------------------------------------------
// The text's turn and slant on a face: left -30/-30, top +30/-30, right +30/+30.
void IsoTextAngles(IsoPlane plane, double& rotation, double& oblique) {
    const double t = ToRadians(30);
    rotation = plane == IsoPlane::Left ? -t : t;
    oblique = plane == IsoPlane::Right ? t : -t;
}

const std::vector<Keyword> kPlanes = {{L"L", L"Left"}, {L"T", L"Top"}, {L"R", L"Right"}};

class IsoTextCmd : public Command {
    IsoPlane m_plane = IsoPlane::Top;
    bool m_placed = false;
    Vec2 m_at;
    TextData Make(const std::wstring& content) const {
        TextData t = E->lastText;
        t.content = content;
        t.justify = 6;  // bottom left: the text stands on the point
        t.style.clear();
        IsoTextAngles(m_plane, t.rotation, t.oblique);
        return t;
    }
    void Ask() {
        E->Ask(P((std::wstring(L"Click where the text starts (on the ") + IsoPlaneName(m_plane) + L" face)").c_str(),
                 kAcceptPoint, kPlanes));
    }

public:
    void Start() override {
        m_plane = (IsoPlane)E->Draft().isoPlane;
        Ask();
    }
    void OnKeyword(const std::wstring& k) override {
        m_plane = k == L"L" ? IsoPlane::Left : k == L"R" ? IsoPlane::Right : IsoPlane::Top;
        Ask();
    }
    void OnPoint(Vec2 p) override {
        if (m_placed) return;
        m_placed = true;
        m_at = p;
        E->Ask(TextPrompt(L"Type the text (Enter: done)"));
    }
    bool OnText(const std::wstring& s) override {
        if (!m_placed || s.empty()) return false;
        AddKeepingStyle(E, L"Isometric text", {MakeText(m_at, Make(s))});
        E->Finish();
        return true;
    }
    void OnEnter() override { E->Finish(); }
    void GetPreview(Vec2 c, Preview& out) override {
        if (!m_placed) out.entities.push_back(MakeText(c, Make(L"TEXT")));
    }
};

// ---------------------------------------------------------------------------
// ISODIM: a dimension along an isometric edge, its extension lines along
// another isometric axis and its figures slanted to match
// ---------------------------------------------------------------------------
class IsoDimCmd : public Command {
    int m_n = 0;
    DimData m_d;

    DimData At(Vec2 loc) const {
        DimData d = m_d;
        d.loc = loc;
        const Vec2 u = Normalized(d.pts[1] - d.pts[0]);
        // Of the isometric axes not along the edge, the one that leads from its middle towards the cursor.
        const Vec2 off = loc - Lerp(d.pts[0], d.pts[1], 0.5);
        double best = -1;
        Vec2 w{0, 1};
        for (double deg : {30.0, 90.0, 150.0}) {
            const Vec2 c{std::cos(ToRadians(deg)), std::sin(ToRadians(deg))};
            if (std::fabs(Cross(c, u)) < 0.3) continue;  // along the edge
            const double score = Length(off) > 1e-9 ? std::fabs(Dot(Normalized(off), c)) : std::fabs(Cross(c, u));
            if (score > best) best = score, w = c;
        }
        d.slanted = true;
        d.extAngle = AngleOf(w) - AngleOf(u);
        return d;
    }

public:
    void Start() override {
        m_d.kind = DimData::Aligned;
        m_d.style = E->Doc().Settings().dimStyle;
        m_d.style.unidirectional = false;
        E->Ask(P(L"Click the first end of the isometric edge", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        if (m_n == 0) {
            m_d.pts[0] = p;
            m_d.refs[0] = FindRef(E->Doc(), p);
            m_n = 1;
            return E->Ask(From(P(L"Click its other end", kAcceptPoint), p));
        }
        if (m_n == 1) {
            if (Dist(p, m_d.pts[0]) < 1e-9) return E->Msg(L"The two ends must be apart.");
            m_d.pts[1] = p;
            m_d.refs[1] = FindRef(E->Doc(), p);
            m_n = 2;
            return E->Ask(P(L"Click where the dimension line goes", kAcceptPoint));
        }
        const DimData d = At(p);
        AddKeepingStyle(E, L"Isometric dimension", {MakeDimension(d)});
        E->Msg(L"Isometric dimension " + DimDisplayText(d) + L".");
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_n == 2) out.entities.push_back(MakeDimension(At(c)));
        else if (m_n == 1) out.guides.push_back({m_d.pts[0], c});
    }
};

// ---------------------------------------------------------------------------
// QDIM: dimension the chosen objects in one go, a chain (or from one base)
// ---------------------------------------------------------------------------
bool g_qdimBaseline = false;
class QDimCmd : public Command {
    std::vector<Vec2> m_pts;
    int m_dir = -1;  // -1 as the cursor suggests, 0 level, 1 upright

    std::vector<Entity> Make(Vec2 loc) const {
        std::vector<Entity> out;
        if (m_pts.size() < 2) return out;
        Box box;
        for (Vec2 p : m_pts) box.Add(p);
        int dir = m_dir;
        if (dir < 0) dir = (loc.y > box.maxY || loc.y < box.minY) ? 0 : 1;
        // The distinct places along the direction measured, in order.
        std::vector<Vec2> pts = m_pts;
        auto key = [&](Vec2 p) { return dir == 0 ? p.x : p.y; };
        std::sort(pts.begin(), pts.end(), [&](Vec2 a, Vec2 b) { return key(a) < key(b); });
        std::vector<Vec2> uniq;
        for (Vec2 p : pts)
            if (uniq.empty() || key(p) - key(uniq.back()) > 1e-6) uniq.push_back(p);
            else if ((dir == 0 && std::fabs(p.y - loc.y) < std::fabs(uniq.back().y - loc.y)) ||
                     (dir == 1 && std::fabs(p.x - loc.x) < std::fabs(uniq.back().x - loc.x)))
                uniq.back() = p;  // of points level with each other, the one nearer the dimension line
        const DimStyle& st = E->Doc().Settings().dimStyle;
        const double step = st.textHeight * 2.5;
        for (size_t i = 0; i + 1 < uniq.size(); ++i) {
            DimData d;
            d.kind = DimData::Linear;
            d.angle = dir == 0 ? 0 : kPi / 2;
            d.style = st;
            d.pts[0] = g_qdimBaseline ? uniq[0] : uniq[i];
            d.pts[1] = uniq[i + 1];
            d.loc = loc;
            if (g_qdimBaseline) {  // each one further out than the last
                const double away = dir == 0 ? (loc.y >= box.maxY ? 1 : -1) : (loc.x >= box.maxX ? 1 : -1);
                (dir == 0 ? d.loc.y : d.loc.x) += away * step * (double)i;
            }
            d.refs[0] = FindRef(E->Doc(), d.pts[0]);
            d.refs[1] = FindRef(E->Doc(), d.pts[1]);
            out.push_back(MakeDimension(d));
        }
        return out;
    }
    void Ask() {
        static const wchar_t* const dirs[] = {L"Direction: as the cursor", L"Direction: level", L"Direction: upright"};
        E->Ask(P(L"Click where the dimension line goes", kAcceptPoint,
                 {{L"D", dirs[m_dir + 1]}, {L"B", g_qdimBaseline ? L"Baseline (from one end)" : L"Chain (one after another)"}}));
    }

public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        for (uint64_t id : SelectionIds(E)) {
            const Entity* e = E->Doc().Find(id);
            if (!e || e->type == EntityType::Dimension || e->type == EntityType::Text || e->type == EntityType::Hatch ||
                e->type == EntityType::Relation || e->type == EntityType::Leader)
                continue;
            EndPoints(*e, m_pts);
            Centres(*e, m_pts);
        }
        if (m_pts.size() < 2) {
            E->Msg(L"Choose lines, circles or arcs to dimension.");
            return E->Finish();
        }
        Ask();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"D") m_dir = m_dir == 1 ? -1 : m_dir + 1;
        if (k == L"B") g_qdimBaseline = !g_qdimBaseline;
        Ask();
    }
    void OnPoint(Vec2 p) override {
        const std::vector<Entity> dims = Make(p);
        if (dims.empty()) return E->Msg(L"Nothing to dimension in that direction.");
        AddKeepingStyle(E, L"Quick dimension", dims);
        E->Msg(std::to_wstring(dims.size()) + L" dimension(s) added.");
        E->ClearSelection();
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override { out.entities = Make(c); }
};

// ---------------------------------------------------------------------------
// DIMTOL: a tolerance on a dimension: deviations, limits, or an ISO fit
// ---------------------------------------------------------------------------
class DimTolCmd : public Command {
    enum Step { Pick_, Kind, FitCode, FitShow, Upper, Lower, Plus } m_step = Pick_;
    uint64_t m_id = 0;
    DimData m_d;
    bool m_limits = false;

    void Apply() {
        Entity e = *E->Doc().Find(m_id);
        e.dim = std::make_shared<const DimData>(m_d);
        E->Doc().BeginChange(L"Tolerance");
        E->Doc().Replace(e);
        E->Doc().EndChange();
        E->Msg(L"Tolerance set: " + DimDisplayText(m_d) + (m_d.tol == DimData::TolFit || m_d.tol == DimData::TolFitDeviation ? m_d.fit : L"") +
               (m_d.tol == DimData::TolNone ? L"" : L" (" + DeviationText(m_d.tolUpper) + L" / " + DeviationText(m_d.tolLower) + L")"));
        E->Finish();
    }

public:
    void Start() override {
        if (E->Selection().size() == 1) {
            const Entity* e = E->Doc().Find(*E->Selection().begin());
            if (e && e->dim) return OnPoint({});
        }
        E->Ask(Pick(L"Click the dimension"));
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Pick_) return;
        uint64_t id = 0;
        if (E->Selection().size() == 1 && !m_id) {
            const Entity* e = E->Doc().Find(*E->Selection().begin());
            if (e && e->dim) id = e->id;
        }
        if (!id) id = E->PickEntity(p, true);
        const Entity* e = id ? E->Doc().Find(id) : nullptr;
        if (!e || !e->dim || e->dim->kind == DimData::Angular || e->dim->kind == DimData::AngularCoord)
            return E->Msg(L"Click a length, radius or diameter dimension.");
        m_id = id;
        m_d = *e->dim;
        m_step = Kind;
        E->Ask(P(L"Which tolerance", kAcceptKeyword | kAcceptEmpty,
                 {{L"F", L"Fit (H7, g6)"}, {L"S", L"Symmetric (\x00B1)"}, {L"D", L"Deviations (+ and -)"}, {L"L", L"Limits"}, {L"N", L"None"}},
                 L"Fit"));
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == Kind) {
            if (k == L"N") {
                m_d.tol = DimData::TolNone;
                m_d.fit.clear();
                return Apply();
            }
            if (k == L"F") {
                m_step = FitCode;
                return E->Ask(TextPrompt(L"Type the fit: a capital for a hole (H7, F8), a small letter for a shaft (g6, k6, p6)"));
            }
            if (k == L"S") {
                m_step = Plus;
                return E->Ask(P(L"Type the tolerance either way (0.02 makes \x00B1" L"0.02)", kAcceptNumber));
            }
            m_limits = k == L"L";
            m_step = Upper;
            return E->Ask(P(L"Type the upper deviation (such as 0.025, or -0.009)", kAcceptNumber));
        }
        if (m_step == FitShow) {
            m_d.tol = k == L"L" ? DimData::TolFit : k == L"V" ? DimData::TolFitDeviation : DimData::TolLimits;
            if (k == L"M") m_d.fit.clear();
            Apply();
        }
    }
    bool OnText(const std::wstring& s) override {
        if (m_step != FitCode || s.empty()) return false;
        double up, lo;
        std::wstring why;
        if (!IsoFit(s, DimMeasure(m_d), up, lo, why)) {
            E->Msg(why);
            return true;
        }
        std::wstring code;
        for (wchar_t c : s)
            if (!std::iswspace(c)) code.push_back(c);
        m_d.fit = code;
        m_d.tolUpper = up, m_d.tolLower = lo;
        m_step = FitShow;
        E->Msg(code + L" at " + DimNumber(DimMeasure(m_d), m_d.style) + L": " + DeviationText(up) + L" / " + DeviationText(lo) + L".");
        E->Ask(P(L"Show", kAcceptKeyword | kAcceptEmpty,
                 {{L"V", L"Fit and Values"}, {L"L", L"Letters only (H7)"}, {L"M", L"liMits (largest and smallest)"}}, L"Fit and Values"));
        return true;
    }
    void OnNumber(double v) override {
        if (m_step == Plus) {
            if (!(v > 0)) return E->Msg(L"Type a number more than zero.");
            m_d.tol = DimData::TolDeviation;
            m_d.tolUpper = v, m_d.tolLower = -v;
            m_d.fit.clear();
            return Apply();
        }
        if (m_step == Upper) {
            m_d.tolUpper = v;
            m_step = Lower;
            return E->Ask(P(L"Type the lower deviation (such as 0, or -0.025)", kAcceptNumber));
        }
        if (m_step == Lower) {
            if (v > m_d.tolUpper) return E->Msg(L"The lower deviation must not be more than the upper one.");
            m_d.tolLower = v;
            m_d.tol = m_limits ? DimData::TolLimits : DimData::TolDeviation;
            m_d.fit.clear();
            Apply();
        }
    }
    void OnEnter() override {
        if (m_step == Kind) return OnKeyword(L"F");
        if (m_step == FitShow) return OnKeyword(L"V");
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// GROUP, UNGROUP: objects picked together
// ---------------------------------------------------------------------------
class GroupCmd : public Command {
    bool m_make;

public:
    explicit GroupCmd(bool make) : m_make(make) {}
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        const std::vector<uint64_t> ids = SelectionIds(E);
        Document& d = E->Doc();
        if (m_make && ids.size() < 2) {
            E->Msg(L"Choose two or more objects to group.");
            return E->Finish();
        }
        const uint32_t g = m_make ? d.NewGroupNumber() : 0;
        size_t n = 0;
        d.BeginChange(m_make ? L"Group" : L"Ungroup");
        for (uint64_t id : ids) {
            const Entity* e = d.Find(id);
            if (!e || e->group == g) continue;
            Entity x = *e;
            x.group = g;
            d.Replace(x);
            ++n;
        }
        d.EndChange();
        E->Msg(m_make ? std::to_wstring(ids.size()) + L" objects grouped: clicking one picks them all. UNGROUP parts them."
                      : std::to_wstring(n) + L" object(s) taken out of their groups.");
        E->ClearSelection();
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// HIDEOBJECTS, ISOLATE, UNHIDE: objects out of sight for a while
// ---------------------------------------------------------------------------
class HideCmd : public Command {
    int m_mode;  // 0 hide, 1 isolate, 2 show all

public:
    explicit HideCmd(int mode) : m_mode(mode) {}
    void Start() override {
        if (m_mode == 2) {
            const size_t n = E->Doc().HiddenCount();
            E->Doc().UnhideAll();
            E->Msg(n ? std::to_wstring(n) + L" hidden object(s) shown again." : L"Nothing was hidden.");
            return E->Finish();
        }
        E->AskSelection();
    }
    void OnSelected() override {
        const std::vector<uint64_t> ids = SelectionIds(E);
        Document& d = E->Doc();
        std::vector<uint64_t> hide;
        if (m_mode == 0) {
            hide = ids;
        } else {
            const std::set<uint64_t> keep(ids.begin(), ids.end());
            for (const auto& kv : d.Entities())
                if (!keep.count(kv.first) && d.Visible(kv.second)) hide.push_back(kv.first);
        }
        d.HideObjects(hide);
        E->ClearSelection();
        E->Msg(std::to_wstring(hide.size()) + L" object(s) hidden for now (not deleted). UNHIDE shows them again.");
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// FIND: find text in the drawing (and replace it)
// ---------------------------------------------------------------------------
std::wstring LowerOf(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
    return s;
}
// `s` with every `from` (any case) made `to`; how many.
size_t ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    const std::wstring low = LowerOf(s), f = LowerOf(from);
    std::wstring out;
    size_t n = 0, pos = 0;
    for (size_t at = low.find(f); at != std::wstring::npos; at = low.find(f, pos)) {
        out += s.substr(pos, at - pos) + to;
        pos = at + f.size();
        ++n;
    }
    if (n) s = out + s.substr(pos);
    return n;
}

class FindCmd : public Command {
    int m_step = 0;
    std::wstring m_find;

    void Run(bool replace, const std::wstring& to) {
        Document& d = E->Doc();
        std::vector<uint64_t> found;
        size_t changes = 0;
        if (replace) d.BeginChange(L"Replace text");
        for (const auto& kv : d.Entities()) {
            const Entity& e = kv.second;
            Entity x = e;
            size_t n = 0;
            if (const TextData* t = TextOf(e)) {
                std::wstring c = t->content;
                n += ReplaceAll(c, m_find, to);
                if (n && replace) EditAnnot(x).text.content = c;
            }
            if (e.type == EntityType::Insert && e.annot) {
                std::vector<std::wstring> values = e.annot->values;
                size_t k = 0;
                for (std::wstring& v : values) k += ReplaceAll(v, m_find, to);
                if (k && replace) EditAnnot(x).values = values;
                n += k;
            }
            if (e.dim && !e.dim->text.empty()) {
                DimData dd = *e.dim;
                const size_t k = ReplaceAll(dd.text, m_find, to);
                if (k && replace) x.dim = std::make_shared<const DimData>(dd);
                n += k;
            }
            if (!n) continue;
            found.push_back(kv.first);
            changes += n;
            if (replace) d.Replace(x);
        }
        if (replace) d.EndChange();
        E->ClearSelection();
        E->AddToSelection(found);
        if (found.empty()) E->Msg(L"\"" + m_find + L"\" was not found.");
        else if (replace) E->Msg(std::to_wstring(changes) + L" replaced in " + std::to_wstring(found.size()) + L" object(s) (selected). Undo takes it back.");
        else E->Msg(std::to_wstring(changes) + L" found in " + std::to_wstring(found.size()) + L" object(s); they are selected.");
        E->Finish();
    }

public:
    void Start() override { E->Ask(TextPrompt(L"Type the text to find")); }
    bool OnText(const std::wstring& s) override {
        if (s.empty()) return false;
        if (m_step == 0) {
            m_find = s;
            m_step = 1;
            E->Ask(TextPrompt(L"Type what to put in its place (Enter: only find it)"));
            return true;
        }
        Run(true, s);
        return true;
    }
    void OnEnter() override {
        if (m_step == 1) return Run(false, m_find);
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// TEXTSTYLE: named ways of writing text (font, width, slant)
// ---------------------------------------------------------------------------
void UseTextStyle(Engine* E, const TextStyle& s) {
    E->lastText.font = s.font;
    E->lastText.widthFactor = s.widthFactor;
    E->lastText.oblique = s.oblique;
    E->lastText.style = s.name;
    if (s.height > 0) E->lastText.height = s.height;
}

class TextStyleCmd : public Command {
    int m_step = 0;  // 0 choose, 1 name, 2 font, 3 width, 4 slant
    TextStyle m_new;

    void AskChoice() {
        const DrawingSettings& st = E->Doc().Settings();
        std::wstring names;
        for (const TextStyle& t : st.AllTextStyles()) names += (names.empty() ? L"" : L", ") + t.name;
        E->Msg(L"Text styles: " + names + L".");
        Prompt p = TextPrompt(L"Type the style to write in, or N for a new one", 0,
                              st.textStyle.empty() ? std::wstring(L"Standard") : st.textStyle);
        p.accept |= kAcceptKeyword;
        p.keywords = {{L"N", L"New"}};
        E->Ask(p);
    }
    void Choose(const TextStyle& t) {
        DrawingSettings st = E->Doc().Settings();
        st.textStyle = t.name;
        E->Doc().SetSettings(st);
        UseTextStyle(E, t);
        E->Msg(L"New text is written in \"" + t.name + L"\" (" + t.font + (t.widthFactor != 1 ? L", width " + E->Len(t.widthFactor) : L"") +
               (t.oblique != 0 ? L", slant " + E->Ang(t.oblique) + L"\x00B0" : L"") + L").");
        E->Finish();
    }
    void Save() {
        DrawingSettings st = E->Doc().Settings();
        bool replaced = false;
        for (TextStyle& t : st.textStyles)
            if (LowerOf(t.name) == LowerOf(m_new.name)) t = m_new, replaced = true;
        if (!replaced) st.textStyles.push_back(m_new);
        E->Doc().SetSettings(st);
        // Texts already in that style follow it.
        Document& d = E->Doc();
        size_t n = 0;
        d.BeginChange(L"Text style");
        for (const auto& kv : d.Entities()) {
            const TextData* t = TextOf(kv.second);
            if (!t || LowerOf(t->style) != LowerOf(m_new.name)) continue;
            Entity x = kv.second;
            TextData& tx = EditAnnot(x).text;
            tx.font = m_new.font, tx.widthFactor = m_new.widthFactor, tx.oblique = m_new.oblique;
            if (m_new.height > 0) tx.height = m_new.height;
            d.Replace(x);
            ++n;
        }
        d.EndChange();
        if (n) E->Msg(std::to_wstring(n) + L" text(s) in that style changed with it.");
        Choose(m_new);
    }

public:
    void Start() override { AskChoice(); }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == 0 && k == L"N") {
            m_step = 1;
            E->Ask(TextPrompt(L"Type the new style's name (an existing one is changed)"));
        }
    }
    bool OnText(const std::wstring& s) override {
        if (s.empty()) return false;
        if (m_step == 0) {
            if (LowerOf(s) == L"n" || LowerOf(s) == L"new") return false;  // the keyword
            const TextStyle* t = E->Doc().Settings().FindTextStyle(s);
            if (!t) {
                E->Msg(L"There is no style \"" + s + L"\". Type N to make one.");
                return true;
            }
            Choose(*t);
            return true;
        }
        if (m_step == 1) {
            const TextStyle* old = E->Doc().Settings().FindTextStyle(s);
            m_new = old ? *old : TextStyle{};
            m_new.name = s;
            m_step = 2;
            E->Ask(TextPrompt(L"Type the font (Enter keeps it)", 0, m_new.font));
            return true;
        }
        if (m_step == 2) {
            m_new.font = s;
            m_step = 3;
            E->Ask(P(L"Type the width of the letters (1 normal, 0.8 narrower)", kAcceptNumber | kAcceptEmpty, {}, E->Len(m_new.widthFactor)));
            return true;
        }
        return false;
    }
    void OnNumber(double v) override {
        if (m_step == 3) {
            if (!(v > 0)) return E->Msg(L"The width must be more than zero.");
            m_new.widthFactor = v;
            m_step = 4;
            return E->Ask(P(L"Type the slant of the letters in degrees (0 upright; 30 or -30 for isometric)", kAcceptNumber | kAcceptEmpty,
                            {}, E->Ang(m_new.oblique)));
        }
        if (m_step == 4) {
            if (std::fabs(v) >= 80) return E->Msg(L"The slant must be less than 80 degrees.");
            m_new.oblique = ToRadians(v);
            Save();
        }
    }
    void OnEnter() override {
        if (m_step == 0) return E->Finish();  // keeps the current one
        if (m_step == 2) {
            m_step = 3;
            return E->Ask(P(L"Type the width of the letters (1 normal, 0.8 narrower)", kAcceptNumber | kAcceptEmpty, {}, E->Len(m_new.widthFactor)));
        }
        if (m_step == 3) return OnNumber(m_new.widthFactor);
        if (m_step == 4) return Save();
        E->Finish();
    }
};
// ---------------------------------------------------------------------------
// MYDETAILS: the student's name, USN, branch and institution, for title blocks
// EXAMSHEET: a sheet for an exam or lab drawing with them filled in
// ---------------------------------------------------------------------------
class DetailsCmd : public Command {
protected:
    size_t m_k = 0;  // which detail is asked
    void AskDetail() {
        static const wchar_t* const questions[] = {L"Your name", L"Your USN (university seat number) or roll number",
                                                   L"Your branch, semester or batch (such as Mechanical, 2nd sem, B2)",
                                                   L"Your college or institution"};
        E->Ask(TextPrompt((std::wstring(questions[m_k]) + L" (Enter keeps it; - clears it)").c_str(), 0,
                          MyDetails()[kMyDetailKeys[m_k]]));
    }
    virtual void DetailsDone() = 0;
    void Next() {
        if (++m_k < std::size(kMyDetailKeys)) return AskDetail();
        E->Host().MyDetailsChanged();
        DetailsDone();
    }

public:
    bool OnText(const std::wstring& s) override {
        if (m_k >= std::size(kMyDetailKeys) || s.empty()) return false;
        MyDetails()[kMyDetailKeys[m_k]] = s == L"-" ? L"" : s;
        Next();
        return true;
    }
    void OnEnter() override {
        if (m_k < std::size(kMyDetailKeys)) return Next();
        E->Finish();
    }
};

class MyDetailsCmd : public DetailsCmd {
    void DetailsDone() override {
        const bool changed = ApplyMyDetails(E->Doc(), true);
        E->Msg(std::wstring(L"Your details are kept for new drawings and exam sheets") +
               (changed ? L", and are now in this drawing's title block." : L"."));
        E->Finish();
    }

public:
    void Start() override { AskDetail(); }
};

std::wstring g_examPaper = L"A3";
class ExamSheetCmd : public DetailsCmd {
    enum Step { Details, Title, Paper } m_step = Details;
    void DetailsDone() override { AskTitle(); }
    void AskTitle() {
        m_step = Title;
        auto it = E->Doc().Properties().find(L"title");
        E->Ask(TextPrompt(L"The drawing's title (the question, such as PROJECTIONS OF SOLIDS)", 0,
                          it == E->Doc().Properties().end() ? L"" : it->second));
    }
    void AskPaper() {
        m_step = Paper;
        E->Ask(P(L"Paper size (landscape)", kAcceptKeyword | kAcceptEmpty, {{L"A4", L"A4"}, {L"A3", L"A3"}, {L"A2", L"A2"}}, g_examPaper));
    }
    void Make() {
        Document& d = E->Doc();
        d.BeginChange(L"Exam sheet");
        ApplyMyDetails(d, true);
        if (!UsesBisTitleBlock(d)) ApplyBisSetup(d, false);
        d.EndChange();
        if (!E->Host().NewExamSheet(g_examPaper)) {
            E->Msg(L"The details are in the drawing's properties; add a sheet with SHEETNEW to see the title block.");
            return E->Finish();
        }
        E->Msg(L"Exam sheet: " + g_examPaper + L" with the BIS title block and your details. "
               L"Draw on the Drawing tab; the sheet shows it to scale. Ctrl+P prints.");
        E->Finish();
    }

public:
    void Start() override {
        if (MyDetails()[L"name"].empty()) {
            E->Msg(L"First, your details for the title block (asked once; MYDETAILS changes them).");
            return AskDetail();
        }
        AskTitle();
    }
    bool OnText(const std::wstring& s) override {
        if (m_step == Details) return DetailsCmd::OnText(s);
        if (m_step == Title && !s.empty()) {
            E->Doc().SetProperty(L"title", s);
            AskPaper();
            return true;
        }
        return false;
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step != Paper) return;
        g_examPaper = k;
        Make();
    }
    void OnEnter() override {
        if (m_step == Details) return DetailsCmd::OnEnter();
        if (m_step == Title) return AskPaper();
        Make();
    }
};

// LANGUAGE: the language of the menus.
class LanguageCmd : public Command {
public:
    void Start() override {
        std::vector<Keyword> kw;
        for (const Language& l : Languages()) kw.push_back({std::wstring(1, (wchar_t)std::towupper(l.code[0])), l.name});
        std::wstring now;
        for (const Language& l : Languages())
            if (CurrentLanguage() == l.code) now = l.name;
        E->Ask(P(L"Menus in", kAcceptKeyword | kAcceptEmpty, kw, now));
    }
    void OnKeyword(const std::wstring& k) override {
        for (const Language& l : Languages())
            if ((wchar_t)std::towupper(l.code[0]) == k[0]) SetLanguage(l.code);
        E->Host().LanguageChanged();
        E->Msg(std::wstring(L"Menus are now in ") +
               (CurrentLanguage() == L"en" ? L"English." : L"the chosen language (a first draft; commands and prompts stay in English)."));
        E->Finish();
    }
    void OnEnter() override { E->Finish(); }
};
}  // namespace

std::unique_ptr<Command> CreateR3Command(const std::wstring& name) {
    if (name == L"BISECT") return std::make_unique<BisectCmd>();
    if (name == L"PARALLELTHRU") return std::make_unique<ThroughCmd>(false);
    if (name == L"PERPTHRU") return std::make_unique<ThroughCmd>(true);
    if (name == L"TANFROM") return std::make_unique<TanFromCmd>();
    if (name == L"ALIGN") return std::make_unique<AlignCmd>();
    if (name == L"SCALES") return std::make_unique<ScalesCmd>();
    if (name == L"POINTPROJ") return std::make_unique<PointProjCmd>();
    if (name == L"TRACES") return std::make_unique<TracesCmd>();
    if (name == L"TRANSITION") return std::make_unique<TransitionCmd>();
    if (name == L"INTERSECT") return std::make_unique<IntersectCmd>();
    if (name == L"PERSPECTIVE") return std::make_unique<PerspectiveCmd>();
    if (name == L"ISOTEXT") return std::make_unique<IsoTextCmd>();
    if (name == L"ISODIM") return std::make_unique<IsoDimCmd>();
    if (name == L"QDIM") return std::make_unique<QDimCmd>();
    if (name == L"DIMTOL") return std::make_unique<DimTolCmd>();
    if (name == L"GROUP") return std::make_unique<GroupCmd>(true);
    if (name == L"UNGROUP") return std::make_unique<GroupCmd>(false);
    if (name == L"HIDEOBJECTS") return std::make_unique<HideCmd>(0);
    if (name == L"ISOLATE") return std::make_unique<HideCmd>(1);
    if (name == L"UNHIDE") return std::make_unique<HideCmd>(2);
    if (name == L"FIND") return std::make_unique<FindCmd>();
    if (name == L"TEXTSTYLE") return std::make_unique<TextStyleCmd>();
    if (name == L"MYDETAILS") return std::make_unique<MyDetailsCmd>();
    if (name == L"EXAMSHEET") return std::make_unique<ExamSheetCmd>();
    if (name == L"LANGUAGE") return std::make_unique<LanguageCmd>();
    return nullptr;
}
