// CommandsR3.cpp - construction helpers (BISECT, PARALLELTHRU, PERPTHRU,
// TANFROM), ALIGN, and the engineering drawing constructions of EngMore.h:
// SCALES, POINTPROJ, TRACES, TRANSITION, INTERSECT and PERSPECTIVE.
#include <cmath>
#include <functional>

#include "Annot.h"
#include "CommandUtil.h"
#include "EngGraphics.h"
#include "EngMore.h"

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
    return nullptr;
}
