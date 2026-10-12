// Commands.cpp - the drawing, editing, view and settings commands, and the
// list of command names and aliases. Each command is a small state machine
// driven by the Engine (see Engine.h): it asks for input with E->Ask(...)
// and reacts in OnPoint / OnNumber / OnKeyword / OnEnter.
#include <algorithm>
#include <functional>

#include "CommandUtil.h"
#include "Engine.h"
#include "Modify.h"

namespace {
// ---------------------------------------------------------------------------
// LINE
// ---------------------------------------------------------------------------
class LineCmd : public Command {
    bool m_have = false;
    Vec2 m_first, m_last;
    std::vector<uint64_t> m_segments;
    std::vector<Vec2> m_starts;

    void AskNext() {
        std::vector<Keyword> kw;
        if (m_segments.size() >= 2) kw.push_back({L"C", L"Close"});
        kw.push_back({L"U", L"Undo"});
        E->Ask(From(P(L"Specify next point", kAcceptPoint | kAcceptEmpty, kw), m_last));
    }
    void Segment(Vec2 to) {
        m_segments.push_back(E->Doc().Add(Entity::Line(m_last, to)));
        m_starts.push_back(m_last);
        m_last = to;
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Line");
        E->Ask(P(L"Specify first point", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        if (!m_have) {
            m_have = true;
            m_first = m_last = p;
        } else if (Near(p, m_last, 1e-12)) {
            E->Msg(L"That is the point the line starts from.");
        } else {
            Segment(p);
        }
        AskNext();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"C") {
            Segment(m_first);
            E->Finish();
        } else if (k == L"U") {
            if (m_segments.empty()) {
                E->Msg(L"All segments already undone.");
            } else {
                E->Doc().Erase(m_segments.back());
                m_last = m_starts.back();
                m_segments.pop_back();
                m_starts.pop_back();
                E->SetLastPoint(m_last);
            }
            AskNext();
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_have) out.entities.push_back(Entity::Line(m_last, c));
    }
};

// ---------------------------------------------------------------------------
// The line, circle or arc picked near p for a tangent (a polyline's segment
// as a line or an arc). False if there is none.
// ---------------------------------------------------------------------------
bool TangentObject(Engine* E, Vec2 p, Entity& out) {
    const uint64_t id = E->PickEntity(p, true);
    const Entity* e = id ? E->Doc().Find(id) : nullptr;
    if (!e) return false;
    if (e->type == EntityType::Line || e->type == EntityType::Circle || e->type == EntityType::Arc) {
        out = *e;
        return true;
    }
    if (e->type != EntityType::Polyline) return false;
    size_t seg;
    if (SegmentAsLine(*e, p, out, seg)) return true;
    std::vector<Prim> prims;
    Primitives(*e, prims);
    for (const Prim& pr : prims) {
        if (pr.kind != Prim::Arc || Distance(pr, p) > E->Host().PickRadius()) continue;
        out = Entity::Arc(pr.c, pr.r, pr.start, pr.start + pr.sweep);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// TANLINE: the line touching two circles or arcs
// ---------------------------------------------------------------------------
class TanLineCmd : public Command {
    Entity m_first;
    Vec2 m_p1;
    bool m_have = false;

    static bool OnCurve(const Entity& e, Vec2 p) {
        if (e.type == EntityType::Circle) return true;
        double sweep = SweepCCW(e.startAngle, e.endAngle);
        if (sweep <= 0) sweep = kTau;
        return AngleInSweep(NormalizeAngle(AngleOf(p - e.p0)), e.startAngle, sweep, 1e-9);
    }
    // Of the (up to four) tangent lines, the one touching nearest to the picks.
    bool Best(const Entity& second, Vec2 p2, Entity& line) const {
        bool found = false;
        double best = 0;
        for (const auto& t : TangentLines(m_first.p0, m_first.radius, second.p0, second.radius)) {
            if (!OnCurve(m_first, t.first) || !OnCurve(second, t.second)) continue;
            const double score = Dist(t.first, m_p1) + Dist(t.second, p2);
            if (!found || score < best) {
                found = true;
                best = score;
                line = Entity::Line(t.first, t.second);
            }
        }
        return found;
    }
    static bool Round(const Entity& e) { return e.type == EntityType::Circle || e.type == EntityType::Arc; }

public:
    void Start() override {
        E->Doc().BeginChange(L"Tangent line");
        E->Ask(Pick(L"Select first circle or arc, near where the line should touch it"));
    }
    void OnPoint(Vec2 p) override {
        Entity e;
        if (!TangentObject(E, p, e) || !Round(e)) {
            E->Msg(L"Pick a circle or an arc.");
            return;
        }
        if (!m_have) {
            m_first = e;
            m_p1 = p;
            m_have = true;
            E->Ask(Pick(L"Select second circle or arc, near where the line should touch it"));
            return;
        }
        Entity line;
        if (!Best(e, p, line)) {
            E->Msg(L"No straight line touches both there (one may be inside the other).");
            return;
        }
        E->Doc().Add(line);
        E->Msg(L"Tangent line, length " + E->Len(Dist(line.p0, line.p1)) + L".");
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        Entity e, line;
        if (m_have && TangentObject(E, c, e) && Round(e) && Best(e, c, line)) out.entities.push_back(line);
    }
};

// ---------------------------------------------------------------------------
// PLINE
// ---------------------------------------------------------------------------
class PlineCmd : public Command {
    std::vector<Vertex> m_v;
    bool m_arc = false;

    Vec2 Tangent() const {
        const size_t n = m_v.size();
        if (n < 2) return {1, 0};
        const Vertex& prev = m_v[n - 2];
        const Vec2 chord = m_v[n - 1].p - prev.p;
        const double theta = 4 * std::atan(prev.bulge);
        return Normalized(RotateAround(chord, {0, 0}, theta / 2));
    }
    void AskNext() {
        if (m_arc)
            E->Ask(From(P(L"Specify end point of arc", kAcceptPoint | kAcceptEmpty,
                          {{L"L", L"Line"}, {L"C", L"Close"}, {L"U", L"Undo"}}),
                        m_v.back().p));
        else
            E->Ask(From(P(L"Specify next point", kAcceptPoint | kAcceptEmpty,
                          {{L"A", L"Arc"}, {L"C", L"Close"}, {L"U", L"Undo"}}),
                        m_v.back().p));
    }
    void Commit(bool closed) {
        if (m_v.size() >= 2) E->Doc().Add(Entity::Polyline(m_v, closed));
        m_v.clear();
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Polyline");
        E->Ask(P(L"Specify start point", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        if (!m_v.empty() && Near(p, m_v.back().p, 1e-12)) {
            E->Msg(L"That is the current point.");
        } else {
            if (!m_v.empty() && m_arc) m_v.back().bulge = TangentBulge(m_v.back().p, Tangent(), p);
            m_v.push_back({p, 0});
        }
        AskNext();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"A" || k == L"L") {
            m_arc = k == L"A";
        } else if (k == L"U") {
            if (m_v.size() > 1) {
                m_v.pop_back();
                m_v.back().bulge = 0;
                E->SetLastPoint(m_v.back().p);
            } else {
                E->Msg(L"Nothing more to undo.");
            }
        } else if (k == L"C") {
            if (m_v.size() < (m_arc ? 2u : 3u)) {
                E->Msg(L"Draw at least two segments before closing.");
            } else {
                if (m_arc) m_v.back().bulge = TangentBulge(m_v.back().p, Tangent(), m_v.front().p);
                Commit(true);
                E->Finish();
                return;
            }
        }
        AskNext();
    }
    void OnEnter() override {
        Commit(false);
        E->Finish();
    }
    void OnCancel() override { Commit(false); }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_v.empty()) return;
        std::vector<Vertex> v = m_v;
        if (m_arc) v.back().bulge = TangentBulge(v.back().p, Tangent(), c);
        v.push_back({c, 0});
        out.entities.push_back(Entity::Polyline(std::move(v), false));
    }
};

// ---------------------------------------------------------------------------
// CIRCLE: centre and radius / diameter, 3 points, 2 points
// ---------------------------------------------------------------------------
class CircleCmd : public Command {
    enum Mode { Centre, Radius, Diameter, ThreeP, TwoP, TanFirst, TanSecond, TanRadius } m_mode = Centre;
    std::vector<Vec2> m_pts;
    Vec2 m_centre;
    Entity m_tan[2];  // tangent, tangent, radius: the two objects

    // The circle of radius r touching both objects, nearest to where they were picked.
    bool Tangent(double r, Vec2 pick2, const Entity& second, Entity& out) const {
        CornerResult res;
        std::wstring why;
        if (!FilletObjects(m_tan[0], m_pts[0], second, pick2, r, false, res, why) || !res.hasJoin) return false;
        out = Entity::Circle(res.join.p0, r);
        return true;
    }

    void AskRadius() {
        const double r = E->lastRadius;
        E->Ask(From(P(L"Specify radius of circle", kAcceptPoint | kAcceptNumber | (r > 0 ? (unsigned)kAcceptEmpty : 0u),
                      {{L"D", L"Diameter"}}, r > 0 ? E->Len(r) : L""),
                    m_centre));
    }
    void Make(Vec2 c, double r) {
        E->lastRadius = r;
        E->Doc().Add(Entity::Circle(c, r));
        E->Finish();
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Circle");
        E->Ask(P(L"Specify center point for circle", kAcceptPoint,
                 {{L"3P", L"3P"}, {L"2P", L"2P"}, {L"T", L"Ttr (tangent tangent radius)"}}));
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"T") {
            m_mode = TanFirst;
            E->Ask(Pick(L"Select a line, circle or arc for the first tangent, near where the circle should touch it"));
        } else if (k == L"3P") {
            m_mode = ThreeP;
            E->Ask(P(L"Specify first point on circle", kAcceptPoint));
        } else if (k == L"2P") {
            m_mode = TwoP;
            E->Ask(P(L"Specify first end point of circle's diameter", kAcceptPoint));
        } else if (k == L"D") {
            m_mode = Diameter;
            const double r = E->lastRadius;
            E->Ask(From(P(L"Specify diameter of circle", kAcceptPoint | kAcceptNumber | (r > 0 ? (unsigned)kAcceptEmpty : 0u),
                          {}, r > 0 ? E->Len(2 * r) : L""),
                        m_centre));
        }
    }
    void OnPoint(Vec2 p) override {
        switch (m_mode) {
            case TanFirst:
            case TanSecond: {
                Entity e;
                if (!TangentObject(E, p, e)) {
                    E->Msg(L"Pick a line, circle or arc.");
                    return;
                }
                m_pts.push_back(p);
                m_tan[m_mode == TanFirst ? 0 : 1] = e;
                if (m_mode == TanFirst) {
                    m_mode = TanSecond;
                    E->Ask(Pick(L"Select a line, circle or arc for the second tangent"));
                } else {
                    m_mode = TanRadius;
                    const double r = E->lastRadius;
                    E->Ask(P(L"Specify radius of circle", kAcceptNumber | (r > 0 ? (unsigned)kAcceptEmpty : 0u), {},
                             r > 0 ? E->Len(r) : L""));
                }
                return;
            }
            case TanRadius:
                return;
            case Centre:
                m_centre = p;
                m_mode = Radius;
                AskRadius();
                break;
            case Radius:
                if (Positive(E, Dist(m_centre, p))) Make(m_centre, Dist(m_centre, p));
                break;
            case Diameter:
                if (Positive(E, Dist(m_centre, p))) Make(m_centre, Dist(m_centre, p) / 2);
                break;
            case ThreeP: {
                m_pts.push_back(p);
                if (m_pts.size() == 1) {
                    E->Ask(From(P(L"Specify second point on circle", kAcceptPoint), p));
                } else if (m_pts.size() == 2) {
                    E->Ask(From(P(L"Specify third point on circle", kAcceptPoint), p));
                } else {
                    Vec2 c;
                    double r;
                    if (CircleFrom3Points(m_pts[0], m_pts[1], m_pts[2], c, r)) {
                        Make(c, r);
                    } else {
                        E->Msg(L"The three points are in a line: no circle goes through them.");
                        m_pts.pop_back();
                    }
                }
                break;
            }
            case TwoP:
                m_pts.push_back(p);
                if (m_pts.size() == 1) {
                    E->Ask(From(P(L"Specify second end point of circle's diameter", kAcceptPoint), p));
                } else if (Positive(E, Dist(m_pts[0], m_pts[1]))) {
                    Make(Lerp(m_pts[0], m_pts[1], 0.5), Dist(m_pts[0], m_pts[1]) / 2);
                } else {
                    m_pts.pop_back();
                }
                break;
        }
    }
    void OnNumber(double v) override {
        if (!Positive(E, v)) return;
        if (m_mode == TanRadius) {
            Entity c;
            if (!Tangent(v, m_pts[1], m_tan[1], c)) {
                E->Msg(L"No circle of that radius touches both. Try another radius.");
                return;
            }
            Make(c.p0, v);
            return;
        }
        if (m_mode == Radius) Make(m_centre, v);
        else if (m_mode == Diameter) Make(m_centre, v / 2);
    }
    void OnEnter() override {
        if (m_mode == TanRadius && E->lastRadius > 0) return OnNumber(E->lastRadius);
        if ((m_mode == Radius || m_mode == Diameter) && E->lastRadius > 0) Make(m_centre, E->lastRadius);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_mode == TanSecond && E->lastRadius > 0) {
            Entity second, circle;
            if (TangentObject(E, c, second) && Tangent(E->lastRadius, c, second, circle)) out.entities.push_back(circle);
        }
        if (m_mode == Radius && Dist(m_centre, c) > 0) out.entities.push_back(Entity::Circle(m_centre, Dist(m_centre, c)));
        if (m_mode == Diameter && Dist(m_centre, c) > 0)
            out.entities.push_back(Entity::Circle(m_centre, Dist(m_centre, c) / 2));
        if (m_mode == TwoP && m_pts.size() == 1 && Dist(m_pts[0], c) > 0)
            out.entities.push_back(Entity::Circle(Lerp(m_pts[0], c, 0.5), Dist(m_pts[0], c) / 2));
        if (m_mode == ThreeP && m_pts.size() == 2) {
            Vec2 cc;
            double r;
            if (CircleFrom3Points(m_pts[0], m_pts[1], c, cc, r)) out.entities.push_back(Entity::Circle(cc, r));
        }
        if (m_mode == Radius || m_mode == Diameter) out.guides.push_back({m_centre, c});
    }
};

// ---------------------------------------------------------------------------
// ARC: three points, or start-centre-end, or centre-start-end / angle
// ---------------------------------------------------------------------------
class ArcCmd : public Command {
    enum Step { Start_, Second, End3P, Centre, CStart, CEnd, CAngle } m_step = Start_;
    Vec2 m_p1, m_p2, m_c;
    bool m_startGiven = false;  // start point first, then "Center"

    static bool ThreePoint(Vec2 a, Vec2 b, Vec2 c, Entity& out) {
        Vec2 centre;
        double r;
        if (!CircleFrom3Points(a, b, c, centre, r)) return false;
        const bool ccw = Cross(b - a, c - a) > 0;
        const double aa = AngleOf(a - centre), ac = AngleOf(c - centre);
        out = ccw ? Entity::Arc(centre, r, aa, ac) : Entity::Arc(centre, r, ac, aa);
        return true;
    }
    Entity CentreArc(Vec2 end) const {
        return Entity::Arc(m_c, Dist(m_c, m_p1), AngleOf(m_p1 - m_c), AngleOf(end - m_c));
    }
    void AskEnd() {
        E->Ask(From(P(L"Specify end point of arc", kAcceptPoint, {{L"A", L"Angle"}}), m_c));
        m_step = CEnd;
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Arc");
        E->Ask(P(L"Specify start point of arc", kAcceptPoint, {{L"C", L"Center"}}));
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"C") {
            E->Ask(P(L"Specify center point of arc", kAcceptPoint));
            m_step = Centre;
        } else if (k == L"A" && m_step == CEnd) {
            E->Ask(P(L"Specify included angle (negative: clockwise)", kAcceptNumber));
            m_step = CAngle;
        }
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Start_:
                m_p1 = p;
                m_startGiven = true;
                E->Ask(From(P(L"Specify second point of arc", kAcceptPoint, {{L"C", L"Center"}}), p));
                m_step = Second;
                break;
            case Second:
                m_p2 = p;
                E->Ask(From(P(L"Specify end point of arc", kAcceptPoint), p));
                m_step = End3P;
                break;
            case End3P: {
                Entity arc;
                if (ThreePoint(m_p1, m_p2, p, arc)) {
                    E->Doc().Add(arc);
                    E->Finish();
                } else {
                    E->Msg(L"The three points are in a line: no arc goes through them.");
                }
                break;
            }
            case Centre:
                m_c = p;
                if (m_startGiven) {  // start, centre, end
                    if (!Positive(E, Dist(m_c, m_p1))) return;
                    AskEnd();
                } else {
                    E->Ask(From(P(L"Specify start point of arc", kAcceptPoint), p));
                    m_step = CStart;
                }
                break;
            case CStart:
                if (!Positive(E, Dist(m_c, p))) return;
                m_p1 = p;
                AskEnd();
                break;
            case CEnd:
                if (Near(p, m_c, 1e-12)) return;
                E->Doc().Add(CentreArc(p));
                E->Finish();
                break;
            case CAngle:
                break;
        }
    }
    void OnNumber(double deg) override {
        if (m_step != CAngle) return;
        const double a = ToRadians(deg);
        if (std::fabs(a) < 1e-12 || std::fabs(a) >= kTau) {
            E->Msg(L"The angle must be between -360 and 360 and not zero.");
            return;
        }
        const double r = Dist(m_c, m_p1), a0 = AngleOf(m_p1 - m_c);
        E->Doc().Add(a > 0 ? Entity::Arc(m_c, r, a0, a0 + a) : Entity::Arc(m_c, r, a0 + a, a0));
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        Entity arc;
        if (m_step == Second && !Near(c, m_p1)) out.entities.push_back(Entity::Line(m_p1, c));
        if (m_step == End3P && ThreePoint(m_p1, m_p2, c, arc)) out.entities.push_back(arc);
        if (m_step == CStart) out.guides.push_back({m_c, c});
        if (m_step == CEnd && !Near(c, m_c)) {
            out.entities.push_back(CentreArc(c));
            out.guides.push_back({m_c, c});
        }
    }

};

// ---------------------------------------------------------------------------
// RECTANG
// ---------------------------------------------------------------------------
class RectCmd : public Command {
    enum Step { First, Other, Length_, Width, Side } m_step = First;
    Vec2 m_c1;
    double m_len = 0, m_wid = 0;

    void AskOther() {
        E->Ask(From(P(L"Specify other corner point", kAcceptPoint, {{L"D", L"Dimensions"}}), m_c1));
        m_step = Other;
    }
    Vec2 SizedCorner(Vec2 toward) const {
        const double sx = toward.x >= m_c1.x ? 1 : -1, sy = toward.y >= m_c1.y ? 1 : -1;
        return m_c1 + Vec2{sx * m_len, sy * m_wid};
    }
    void Make(Vec2 c2) {
        if (std::fabs(c2.x - m_c1.x) < 1e-12 || std::fabs(c2.y - m_c1.y) < 1e-12) {
            E->Msg(L"The rectangle would have no area: pick a corner off the first one's lines.");
            return;
        }
        E->lastRectLength = std::fabs(c2.x - m_c1.x);
        E->lastRectWidth = std::fabs(c2.y - m_c1.y);
        E->Doc().Add(Entity::Rectangle(m_c1, c2));
        E->Finish();
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Rectangle");
        E->Ask(P(L"Specify first corner point", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        if (m_step == First) {
            m_c1 = p;
            AskOther();
        } else if (m_step == Other) {
            Make(p);
        } else if (m_step == Side) {
            Make(SizedCorner(p));
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"D") {
            const double l = E->lastRectLength;
            E->Ask(P(L"Specify length for rectangles", kAcceptNumber | (l > 0 ? (unsigned)kAcceptEmpty : 0u), {},
                     l > 0 ? E->Len(l) : L""));
            m_step = Length_;
        }
    }
    void OnNumber(double v) override {
        if (!Positive(E, v)) return;
        if (m_step == Length_) {
            m_len = v;
            const double w = E->lastRectWidth;
            E->Ask(P(L"Specify width for rectangles", kAcceptNumber | (w > 0 ? (unsigned)kAcceptEmpty : 0u), {},
                     w > 0 ? E->Len(w) : L""));
            m_step = Width;
        } else if (m_step == Width) {
            m_wid = v;
            E->Ask(From(P(L"Specify other corner point (picks the side)", kAcceptPoint), m_c1));
            m_step = Side;
        }
    }
    void OnEnter() override {
        if (m_step == Length_ && E->lastRectLength > 0) OnNumber(E->lastRectLength);
        else if (m_step == Width && E->lastRectWidth > 0) OnNumber(E->lastRectWidth);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Other && !Near(c, m_c1)) out.entities.push_back(Entity::Rectangle(m_c1, c));
        if (m_step == Side) out.entities.push_back(Entity::Rectangle(m_c1, SizedCorner(c)));
    }
};

// ---------------------------------------------------------------------------
// MOVE and COPY
// ---------------------------------------------------------------------------
class MoveCopyCmd : public Command {
    bool m_copy;
    enum Step { Select, Base, Second, Displacement } m_step = Select;
    std::vector<uint64_t> m_ids;
    Vec2 m_base;
    std::vector<std::vector<uint64_t>> m_batches;  // copies placed, for Undo

    void Apply(Vec2 d) {
        if (m_copy) {
            std::vector<Entity> copies;
            for (uint64_t id : m_ids) {
                const Entity* e = E->Doc().Find(id);
                if (!e) continue;
                copies.push_back(*e);
                Translate(copies.back(), d);
            }
            // Together, so copied dimensions stay attached to the copied objects.
            m_batches.push_back(E->Doc().AddGroup(std::move(copies)));
        } else {
            for (uint64_t id : m_ids) {
                const Entity* e = E->Doc().Find(id);
                if (!e) continue;
                Entity m = *e;
                Translate(m, d);
                E->Doc().Replace(m);
            }
        }
    }
    void AskSecond() {
        if (m_copy)
            E->Ask(From(P(L"Specify second point", kAcceptPoint | kAcceptEmpty,
                          {{L"U", L"Undo"}, {L"E", L"Exit"}}, m_batches.empty() ? L"use first point as displacement" : L"Exit"),
                        m_base));
        else
            E->Ask(From(P(L"Specify second point", kAcceptPoint | kAcceptEmpty, {}, L"use first point as displacement"),
                        m_base));
        m_step = Second;
    }
    void Done() {
        E->Msg(std::to_wstring(m_ids.size()) + (m_copy ? L" object(s) copied." : L" object(s) moved."));
        E->ClearSelection();
        E->Finish();
    }

public:
    explicit MoveCopyCmd(bool copy) : m_copy(copy) {}
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        m_ids = SelectionIds(E);
        E->Doc().BeginChange(m_copy ? L"Copy" : L"Move");
        E->Ask(P(L"Specify base point", kAcceptPoint | kAcceptEmpty, {{L"D", L"Displacement"}}, L"Displacement"));
        m_step = Base;
    }
    void OnPoint(Vec2 p) override {
        if (m_step == Base) {
            m_base = p;
            AskSecond();
        } else if (m_step == Second) {
            Apply(p - m_base);
            if (m_copy) AskSecond();
            else Done();
        } else if (m_step == Displacement) {
            Apply(p);  // typed "dx,dy"
            Done();
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"D") {
            E->Ask(P(L"Specify displacement (dx,dy)", kAcceptPoint | kAcceptEmpty, {}, L"0,0"));
            m_step = Displacement;
        } else if (k == L"U") {
            if (m_batches.empty()) {
                E->Msg(L"Nothing to undo.");
            } else {
                for (uint64_t id : m_batches.back()) E->Doc().Erase(id);
                m_batches.pop_back();
            }
            AskSecond();
        } else if (k == L"E") {
            Done();
        }
    }
    void OnEnter() override {
        if (m_step == Base) {
            OnKeyword(L"D");
        } else if (m_step == Second && (!m_copy || m_batches.empty())) {
            Apply(m_base);  // the base point is the displacement
            Done();
        } else if (m_step == Second || m_step == Displacement) {
            Done();
        } else {
            E->Finish();
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Second) return;
        const Vec2 d = c - m_base;
        PreviewTransformed(E, m_ids, out, [&](Entity& e) { Translate(e, d); });
        out.guides.push_back({m_base, c});
    }
};

// ---------------------------------------------------------------------------
// ROTATE
// ---------------------------------------------------------------------------
class RotateCmd : public Command {
    enum Step { Select, Base, Angle, RefAngle, RefSecond, NewAngle, NewFirst, NewSecond } m_step = Select;
    std::vector<uint64_t> m_ids;
    Vec2 m_base, m_r1, m_n1;
    double m_ref = 0;
    bool m_copy = false;

    void AskAngle() {
        E->Ask(From(P(L"Specify rotation angle", kAcceptPoint | kAcceptNumber | kAcceptEmpty,
                      {{L"C", L"Copy"}, {L"R", L"Reference"}}, E->Ang(E->lastRotation)),
                    m_base));
        m_step = Angle;
    }
    void AskNew() {
        E->Ask(From(P(L"Specify the new angle", kAcceptPoint | kAcceptNumber, {{L"P", L"Points"}}), m_base));
        m_step = NewAngle;
    }
    void Apply(double angle) {
        E->lastRotation = NormalizeAngle(angle);
        std::vector<Entity> copies;
        for (uint64_t id : m_ids) {
            const Entity* e = E->Doc().Find(id);
            if (!e) continue;
            Entity r = *e;
            Rotate(r, m_base, angle);
            if (m_copy) copies.push_back(std::move(r));
            else E->Doc().Replace(r);
        }
        if (m_copy) E->Doc().AddGroup(std::move(copies));
        E->Msg(std::to_wstring(m_ids.size()) + L" object(s) rotated by " + E->Ang(NormalizeAngle(angle)) + L"\x00B0.");
        E->ClearSelection();
        E->Finish();
    }
    double PreviewAngle(Vec2 c) const {
        if (m_step == Angle) return AngleOf(c - m_base);
        if (m_step == NewAngle) return AngleOf(c - m_base) - m_ref;
        if (m_step == NewSecond) return AngleOf(c - m_n1) - m_ref;
        return 0;
    }

public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        m_ids = SelectionIds(E);
        E->Doc().BeginChange(L"Rotate");
        E->Ask(P(L"Specify base point", kAcceptPoint));
        m_step = Base;
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Base:
                m_base = p;
                AskAngle();
                break;
            case Angle:
                if (!Near(p, m_base, 1e-12)) Apply(AngleOf(p - m_base));
                break;
            case RefAngle:
                m_r1 = p;
                E->Ask(From(P(L"Specify second point", kAcceptPoint), p));
                m_step = RefSecond;
                break;
            case RefSecond:
                if (Near(p, m_r1, 1e-12)) return;
                m_ref = AngleOf(p - m_r1);
                AskNew();
                break;
            case NewAngle:
                if (!Near(p, m_base, 1e-12)) Apply(AngleOf(p - m_base) - m_ref);
                break;
            case NewFirst:
                m_n1 = p;
                E->Ask(From(P(L"Specify second point", kAcceptPoint), p));
                m_step = NewSecond;
                break;
            case NewSecond:
                if (!Near(p, m_n1, 1e-12)) Apply(AngleOf(p - m_n1) - m_ref);
                break;
            default:
                break;
        }
    }
    void OnNumber(double deg) override {
        if (m_step == Angle) {
            Apply(ToRadians(deg));
        } else if (m_step == RefAngle) {
            m_ref = ToRadians(deg);
            AskNew();
        } else if (m_step == NewAngle) {
            Apply(ToRadians(deg) - m_ref);
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"C" && m_step == Angle) {
            m_copy = !m_copy;
            E->Msg(m_copy ? L"Rotating a copy of the selected objects." : L"Rotating the selected objects.");
            AskAngle();
        } else if (k == L"R" && m_step == Angle) {
            E->Ask(P(L"Specify the reference angle (or pick two points)", kAcceptPoint | kAcceptNumber | kAcceptEmpty, {},
                     L"0"));
            m_step = RefAngle;
        } else if (k == L"P" && m_step == NewAngle) {
            E->Ask(P(L"Specify first point", kAcceptPoint));
            m_step = NewFirst;
        }
    }
    void OnEnter() override {
        if (m_step == Angle) Apply(E->lastRotation);
        else if (m_step == RefAngle) OnNumber(0);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Angle && m_step != NewAngle && m_step != NewSecond) return;
        const double a = PreviewAngle(c);
        PreviewTransformed(E, m_ids, out, [&](Entity& e) { Rotate(e, m_base, a); });
        out.guides.push_back({m_step == NewSecond ? m_n1 : m_base, c});
    }
};

// ---------------------------------------------------------------------------
// ERASE
// ---------------------------------------------------------------------------
class EraseCmd : public Command {
public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        const std::vector<uint64_t> ids = SelectionIds(E);
        E->Doc().BeginChange(L"Erase");
        for (uint64_t id : ids) E->Doc().Erase(id);
        E->ClearSelection();
        E->Msg(std::to_wstring(ids.size()) + L" object(s) erased.");
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// ZOOM and PAN
// ---------------------------------------------------------------------------
class ZoomCmd : public Command {
    bool m_corner = false;
    Vec2 m_c1;

    void AskCorner() {
        E->Ask(P(L"Specify first corner", kAcceptPoint));
    }

public:
    void Start() override {
        E->Ask(P(L"Specify corner of window, enter a scale factor (nX)", kAcceptPoint | kAcceptEmpty,
                 {{L"A", L"All"}, {L"E", L"Extents"}, {L"P", L"Previous"}, {L"W", L"Window"}}, L"Extents"));
    }
    bool OnText(const std::wstring& t) override {
        std::wstring s = t;
        if (!s.empty() && (s.back() == L'x' || s.back() == L'X')) s.pop_back();
        double f;
        std::string n(s.begin(), s.end());
        if (!ParseNumber(n, f) || f <= 0) return false;
        E->Host().ZoomFactor(f);
        E->Finish();
        return true;
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"A" || k == L"E") {
            E->Host().ZoomExtents();
            E->Finish();
        } else if (k == L"P") {
            E->Host().ZoomPrevious();
            E->Finish();
        } else if (k == L"W") {
            AskCorner();
        }
    }
    void OnPoint(Vec2 p) override {
        if (!m_corner) {
            m_corner = true;
            m_c1 = p;
            E->Ask(From(P(L"Specify opposite corner", kAcceptPoint), p));
        } else {
            E->Host().ZoomWindow(Box(m_c1, p));
            E->Finish();
        }
    }
    void OnEnter() override {
        E->Host().ZoomExtents();
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (!m_corner) return;
        const Vec2 a{m_c1.x, c.y}, b{c.x, m_c1.y};
        out.guides.push_back({m_c1, a});
        out.guides.push_back({a, c});
        out.guides.push_back({c, b});
        out.guides.push_back({b, m_c1});
    }
};

class PanCmd : public Command {
public:
    void Start() override {
        E->SetPanMode(true);
        E->Ask(P(L"Drag with the left mouse button to pan. Press Esc or Enter to exit", kAcceptEmpty));
    }
};

// ---------------------------------------------------------------------------
// DIST and ID
// ---------------------------------------------------------------------------
class DistCmd : public Command {
    bool m_have = false;
    Vec2 m_p1;

public:
    void Start() override { E->Ask(P(L"Specify first point", kAcceptPoint)); }
    void OnPoint(Vec2 p) override {
        if (!m_have) {
            m_have = true;
            m_p1 = p;
            E->Ask(From(P(L"Specify second point", kAcceptPoint), p));
            return;
        }
        const Vec2 d = p - m_p1;
        E->Msg(L"Distance = " + E->Len(Length(d)) + L",  Angle = " + E->Ang(NormalizeAngle(AngleOf(d))) +
               L"\x00B0,  Delta X = " + E->Len(d.x) + L",  Delta Y = " + E->Len(d.y));
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_have) out.guides.push_back({m_p1, c});
    }
};

class IdCmd : public Command {
public:
    void Start() override { E->Ask(P(L"Specify point", kAcceptPoint)); }
    void OnPoint(Vec2 p) override {
        E->Msg(L"X = " + E->Len(p.x) + L"     Y = " + E->Len(p.y));
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// GRID, SNAP, ORTHO
// ---------------------------------------------------------------------------
class SpacingCmd : public Command {
    bool m_grid;

public:
    explicit SpacingCmd(bool grid) : m_grid(grid) {}
    void Start() override {
        Drafting& d = E->Draft();
        E->Ask(P(m_grid ? L"Specify grid spacing" : L"Specify snap spacing", kAcceptNumber | kAcceptEmpty,
                 {{L"ON", L"ON"}, {L"OFF", L"OFF"}}, E->Len(m_grid ? d.gridSpacing : d.snapSpacing)));
    }
    void OnNumber(double v) override {
        if (!Positive(E, v)) return;
        Drafting& d = E->Draft();
        (m_grid ? d.gridSpacing : d.snapSpacing) = v;
        (m_grid ? d.gridOn : d.snapOn) = true;
        E->Host().DraftingChanged();
        E->Finish();
    }
    void OnKeyword(const std::wstring& k) override {
        Drafting& d = E->Draft();
        (m_grid ? d.gridOn : d.snapOn) = k == L"ON";
        E->Host().DraftingChanged();
        E->Finish();
    }
};

class OrthoCmd : public Command {
public:
    void Start() override {
        E->Ask(P(L"Enter mode", kAcceptEmpty, {{L"ON", L"ON"}, {L"OFF", L"OFF"}},
                 E->Draft().orthoOn ? L"ON" : L"OFF"));
    }
    void OnKeyword(const std::wstring& k) override {
        E->Draft().orthoOn = k == L"ON";
        E->Host().DraftingChanged();
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// UNDO, REDO, SELECTALL
// ---------------------------------------------------------------------------
class UndoRedoCmd : public Command {
    bool m_redo;

public:
    explicit UndoRedoCmd(bool redo) : m_redo(redo) {}
    void Start() override {
        Document& doc = E->Doc();
        const std::wstring name = m_redo ? doc.RedoName() : doc.UndoName();
        if (m_redo ? doc.Redo() : doc.Undo()) {
            E->Msg((m_redo ? L"Redo " : L"Undo ") + name);
            E->PruneSelection();
        } else {
            E->Msg(m_redo ? L"Nothing to redo." : L"Nothing to undo.");
        }
        E->Finish();
    }
};

class SelectAllCmd : public Command {
public:
    void Start() override {
        E->SelectAll();
        E->Msg(std::to_wstring(E->Selection().size()) + L" object(s) selected.");
        E->Finish();
    }
};
}  // namespace

const std::vector<CommandInfo>& AllCommands() {
    static const std::vector<CommandInfo> list = {
        {L"LINE", {L"L"}, L"Line", L"Draw straight line segments"},
        {L"PLINE", {L"PL"}, L"Polyline", L"Draw connected lines and arcs as one object"},
        {L"TANLINE", {L"TL"}, L"Tangent line", L"A line touching two circles or arcs (click near where it should touch)"},
        {L"CIRCLE", {L"C"}, L"Circle",
         L"Draw a circle (centre and radius, diameter, 2 or 3 points, or tangent to two objects with a radius)"},
        {L"ARC", {L"A"}, L"Arc", L"Draw an arc (3 points, or centre, start and end)"},
        {L"RECTANG", {L"REC", L"RECTANGLE"}, L"Rectangle", L"Draw a rectangle from two corners or its size"},
        {L"POLYGON", {L"POL"}, L"Polygon", L"Draw a regular polygon (inscribed, circumscribed or by an edge)"},
        {L"ELLIPSE", {L"EL"}, L"Ellipse", L"Draw an ellipse or elliptical arc"},
        {L"SPLINE", {L"SPL"}, L"Spline", L"Draw a smooth curve through points"},
        {L"POINT", {L"PO"}, L"Point", L"Place points"},
        {L"MOVE", {L"M"}, L"Move", L"Move objects"},
        {L"COPY", {L"CO", L"CP"}, L"Copy", L"Copy objects, as many times as you like"},
        {L"ROTATE", {L"RO"}, L"Rotate", L"Rotate objects around a base point"},
        {L"ERASE", {L"E", L"DELETE"}, L"Erase", L"Remove objects from the drawing"},
        {L"TRIM", {L"TR"}, L"Trim", L"Cut objects back to where they cross others (Shift+click extends)"},
        {L"EXTEND", {L"EX"}, L"Extend", L"Lengthen objects to meet others (Shift+click trims)"},
        {L"OFFSET", {L"O"}, L"Offset", L"Parallel copy of a line, arc, circle or polyline at a distance"},
        {L"FILLET", {L"F"}, L"Fillet", L"Round a corner with an arc (radius 0: sharp corner)"},
        {L"CHAMFER", {L"CHA"}, L"Chamfer", L"Bevel a corner between two lines"},
        {L"MIRROR", {L"MI"}, L"Mirror", L"Mirror objects about a line"},
        {L"SCALE", {L"SC"}, L"Scale", L"Make objects bigger or smaller"},
        {L"STRETCH", {L"S"}, L"Stretch", L"Move the points inside a crossing window"},
        {L"BREAK", {L"BR"}, L"Break", L"Remove the part of an object between two points"},
        {L"BREAKATPOINT", {}, L"Break at point", L"Split an object in two at a point"},
        {L"JOIN", {L"J"}, L"Join", L"Join lines, arcs and polylines whose ends meet"},
        {L"EXPLODE", {L"X"}, L"Explode", L"Split a polyline, array, block, leader or hatch into separate objects"},
        {L"LENGTHEN", {L"LEN"}, L"Lengthen", L"Change the length of lines and arcs"},
        {L"ARRAY", {L"AR"}, L"Array", L"Copies in a grid, around a centre or along a path"},
        {L"ARRAYRECT", {}, L"Rectangular array", L"Copies in rows and columns that stay linked"},
        {L"ARRAYPOLAR", {}, L"Polar array", L"Copies around a centre that stay linked"},
        {L"ARRAYPATH", {}, L"Path array", L"Copies along a curve that stay linked"},
        {L"ARRAYEDIT", {}, L"Edit array", L"Change the count and spacing of an array"},
        {L"DIM", {L"SMARTDIM", L"SD"}, L"Smart dimension",
         L"Click a line, circle, arc or two lines: the right dimension follows the cursor. Type a value to resize"},
        {L"DIMLINEAR", {L"DLI"}, L"Linear dimension", L"Horizontal or vertical distance between two points"},
        {L"DIMALIGNED", {L"DAL"}, L"Aligned dimension", L"Distance along the line between two points"},
        {L"DIMRADIUS", {L"DRA"}, L"Radius dimension", L"Radius of an arc or circle"},
        {L"DIMDIAMETER", {L"DDI"}, L"Diameter dimension", L"Diameter of a circle or arc"},
        {L"DIMANGULAR", {L"DAN"}, L"Angular dimension", L"Angle between two lines, of an arc, or by three points"},
        {L"DIMCONTINUE", {L"DCO"}, L"Continue dimension", L"Chain of dimensions from the last one"},
        {L"DIMBASELINE", {L"DBA"}, L"Baseline dimension", L"Stack of dimensions from the same base line"},
        {L"DIMORDINATE", {L"DOR", L"DIMORD"}, L"Ordinate dimension", L"X or Y of a point, measured from the origin (coordinate dimension)"},
        {L"DIMANGCOORD", {L"DAC"}, L"Angular coordinate dimension", L"Angles of points about a centre, from a 0\x00B0 direction"},
        {L"DIMSPACE", {L"DSP"}, L"Dimension spacing", L"Space parallel dimensions evenly from a base one"},
        {L"DIMDRIVEN", {L"DDR"}, L"Driven dimension", L"Make dimensions only report (in brackets), or drive the geometry again"},
        {L"DIMEDIT", {L"DED"}, L"Edit dimension", L"New value (the geometry changes to it) or new text (double-click a dimension)"},
        {L"DIMSTYLE", {L"D", L"DST"}, L"Dimension style", L"Text height, arrows and decimals of dimensions", true},
        {L"TEXT", {L"T", L"DT", L"MTEXT", L"MT"}, L"Text", L"Write text: several lines, any font, symbols (\x00B0 \x00D8 \x00B1 \x2104) and fields (date, file name)"},
        {L"TEXTEDIT", {L"ED", L"DDEDIT"}, L"Edit text", L"Change a text or a leader's note (or double-click it)"},
        {L"LEADER", {L"LE", L"QLEADER", L"MLEADER"}, L"Leader", L"An arrow and lines to a note (Frame: in a box or a circle)"},
        {L"BALLOON", {L"CALLOUT"}, L"Balloon", L"A leader to a note in a circle (item numbers)"},
        {L"CENTERMARK", {L"CM", L"CENTREMARK"}, L"Centre mark", L"Centre lines across circles and arcs"},
        {L"CENTERLINE", {L"CL", L"CENTRELINE"}, L"Centre line", L"A centre line between two lines"},
        {L"HATCH", {L"H", L"BHATCH"}, L"Hatch", L"Fill an area with a pattern: click inside it (follows its outline when it changes)"},
        {L"FILL", {L"SOLIDFILL"}, L"Fill", L"Fill an area with solid colour: click inside it"},
        {L"BLOCK", {L"B"}, L"Make block", L"Make a named block from objects, to place many times"},
        {L"INSERT", {L"I"}, L"Insert block", L"Place a block (scale, rotation, attribute values)"},
        {L"ATTDEF", {L"ATT"}, L"Attribute definition", L"A value asked for each time a block is placed (part number, size...)"},
        {L"ATTEDIT", {L"ATE", L"EATTEDIT"}, L"Edit attributes", L"Change the attribute values of a placed block"},
        {L"PURGE", {L"PU"}, L"Purge", L"Remove blocks that are not used"},
        {L"REVCLOUD", {L"RC"}, L"Revision cloud", L"A cloud of arcs around a change (rectangle, polygon or an object)"},
        {L"AREA", {L"AA"}, L"Area", L"Area and perimeter of points, an object or the area around a point"},
        {L"COPYCLIP", {}, L"Copy to clipboard", L"Copy objects to paste them elsewhere, also into another drawing (Ctrl+C)"},
        {L"CUTCLIP", {}, L"Cut to clipboard", L"Remove objects to paste them elsewhere (Ctrl+X)"},
        {L"PASTECLIP", {}, L"Paste", L"Paste copied objects at a point (Ctrl+V)"},
        {L"LAYER", {L"LA"}, L"Layers", L"Layer manager: new layers, on/off, freeze, lock, colour, line type, layer states", true},
        {L"PROPERTIES", {L"PR", L"PROPS"}, L"Properties", L"Show or hide the Properties panel (Ctrl+1)", true},
        {L"MATCHPROP", {L"MA"}, L"Match properties", L"Copy the layer, colour and line type of one object onto others"},
        {L"CONSTRUCTION", {L"CONS"}, L"Construction geometry", L"Make objects reference geometry (faint, not printed), or normal again"},
        {L"ORIGIN", {L"UCS"}, L"Grid origin", L"Where coordinates and the grid start (Reset: back to 0,0)"},
        {L"LTSCALE", {L"LTS"}, L"Line type scale", L"How large dashes and gaps of line types are"},
        {L"UNDO", {L"U"}, L"Undo", L"Take back the last command"},
        {L"REDO", {}, L"Redo", L"Do again what Undo took back"},
        {L"SELECTALL", {L"ALL"}, L"Select all", L"Select every object"},
        {L"SELECTSIMILAR", {}, L"Select similar", L"Select every object of the same kind as the selected ones"},
        {L"QSELECT", {}, L"Quick select", L"Select objects by type and property (radius, length, area...)", true},
        {L"ZOOM", {L"Z"}, L"Zoom", L"Zoom to the extents, a window, a factor or the previous view"},
        {L"PAN", {L"P"}, L"Pan", L"Move the view by dragging"},
        {L"DIST", {L"DI"}, L"Distance", L"Measure the distance and angle between two points"},
        {L"ID", {}, L"Point coordinates", L"Show the coordinates of a point"},
        {L"GRID", {}, L"Grid", L"Grid spacing, on or off (F7)"},
        {L"SNAP", {L"SN"}, L"Snap", L"Snap spacing, on or off (F9)"},
        {L"ORTHO", {}, L"Ortho", L"Horizontal and vertical lines only (F8)"},
        {L"OSNAP", {L"OS"}, L"Object snaps", L"Choose the object snaps (F3 turns them on or off)", true},
        {L"UNITS", {L"UN"}, L"Units", L"Drawing units and decimal places", true},
        {L"NEW", {}, L"New drawing", L"Start a new drawing", true},
        {L"OPEN", {}, L"Open", L"Open a Feather CAD drawing or a DXF file in a new tab", true},
        {L"IMPORT", {L"DXFIN"}, L"Import DXF", L"Bring a DXF drawing's objects into this drawing", true},
        {L"CLOSE", {}, L"Close drawing", L"Close this drawing's tab (Ctrl+W)", true},
        {L"SAVE", {L"QSAVE"}, L"Save", L"Save the drawing", true},
        {L"SAVEAS", {}, L"Save as", L"Save the drawing under a new name", true},
        {L"DXFOUT", {L"EXPORT"}, L"Export DXF", L"Save a copy for AutoCAD and other CAD programs", true},
        {L"HISTORY", {L"VERSIONS"}, L"Version history", L"Go back to an earlier saved or auto-saved version", true},
        {L"RECOVER", {}, L"Recover drawings", L"Open the auto-saved copy of a drawing that was not saved", true},
        {L"COMMANDS", {L"HELP"}, L"Search commands", L"Find a command by name (Ctrl+K)", true},
        {L"ABOUT", {}, L"About Feather CAD", L"Version and author", true},
        {L"EXIT", {L"QUIT"}, L"Exit", L"Close Feather CAD", true},
    };
    return list;
}

std::unique_ptr<Command> CreateCommand(const std::wstring& name) {
    if (auto cmd = CreateModifyCommand(name)) return cmd;
    if (auto cmd = CreateDrawCommand(name)) return cmd;
    if (auto cmd = CreateDimCommand(name)) return cmd;
    if (auto cmd = CreateAnnotCommand(name)) return cmd;
    if (name == L"LINE") return std::make_unique<LineCmd>();
    if (name == L"PLINE") return std::make_unique<PlineCmd>();
    if (name == L"TANLINE") return std::make_unique<TanLineCmd>();
    if (name == L"CIRCLE") return std::make_unique<CircleCmd>();
    if (name == L"ARC") return std::make_unique<ArcCmd>();
    if (name == L"RECTANG") return std::make_unique<RectCmd>();
    if (name == L"MOVE") return std::make_unique<MoveCopyCmd>(false);
    if (name == L"COPY") return std::make_unique<MoveCopyCmd>(true);
    if (name == L"ROTATE") return std::make_unique<RotateCmd>();
    if (name == L"ERASE") return std::make_unique<EraseCmd>();
    if (name == L"UNDO") return std::make_unique<UndoRedoCmd>(false);
    if (name == L"REDO") return std::make_unique<UndoRedoCmd>(true);
    if (name == L"SELECTALL") return std::make_unique<SelectAllCmd>();
    if (name == L"ZOOM") return std::make_unique<ZoomCmd>();
    if (name == L"PAN") return std::make_unique<PanCmd>();
    if (name == L"DIST") return std::make_unique<DistCmd>();
    if (name == L"ID") return std::make_unique<IdCmd>();
    if (name == L"GRID") return std::make_unique<SpacingCmd>(true);
    if (name == L"SNAP") return std::make_unique<SpacingCmd>(false);
    if (name == L"ORTHO") return std::make_unique<OrthoCmd>();
    return nullptr;
}
