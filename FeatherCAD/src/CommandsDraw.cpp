// CommandsDraw.cpp - Phase B2 commands: ELLIPSE, POLYGON, SPLINE, POINT,
// SELECTSIMILAR, and grip editing (the command that runs when a grip of a
// selected object is clicked: stretch, move, rotate, scale or mirror).
#include <algorithm>

#include "CommandUtil.h"
#include "Grips.h"
#include "Select.h"
#include "Spline.h"

namespace {
// ---------------------------------------------------------------------------
// ELLIPSE: two axis ends and the other half-axis, or centre first; Arc
// option for an elliptical arc.
// ---------------------------------------------------------------------------
class EllipseCmd : public Command {
    enum Step { Axis1, Axis2, Centre, AxisFromCentre, Other, StartAngle, EndAngle } m_step = Axis1;
    bool m_arc = false;
    Vec2 m_a1, m_c, m_axis;
    double m_ratio = 0.5, m_start = 0;

    void AskOther() {
        E->Ask(From(P(L"Specify distance to other axis", kAcceptPoint | kAcceptNumber), m_c));
        m_step = Other;
    }
    void SetRatio(double halfOther) {
        if (!Positive(E, halfOther)) return;
        m_ratio = halfOther / Length(m_axis);
        if (!m_arc) {
            E->Doc().Add(Entity::Ellipse(m_c, m_axis, m_ratio));
            E->Finish();
            return;
        }
        E->Ask(From(P(L"Specify start angle", kAcceptPoint | kAcceptNumber), m_c));
        m_step = StartAngle;
    }
    double ParamOf(Vec2 p) const { return EllipseParam(m_c, m_axis, m_ratio, p); }
    void End(double t) {
        if (std::fabs(SweepCCW(m_start, t)) < 1e-9) {
            E->Msg(L"The end must differ from the start.");
            return;
        }
        E->Doc().Add(Entity::EllipseArc(m_c, m_axis, m_ratio, m_start, t));
        E->Finish();
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Ellipse");
        E->Ask(P(L"Specify axis endpoint of ellipse", kAcceptPoint, {{L"A", L"Arc"}, {L"C", L"Center"}}));
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"A" && m_step == Axis1) {
            m_arc = true;
            E->Ask(P(L"Specify axis endpoint of elliptical arc", kAcceptPoint, {{L"C", L"Center"}}));
        } else if (k == L"C") {
            E->Ask(P(L"Specify center of ellipse", kAcceptPoint));
            m_step = Centre;
        }
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Axis1:
                m_a1 = p;
                E->Ask(From(P(L"Specify other endpoint of axis", kAcceptPoint), p));
                m_step = Axis2;
                break;
            case Axis2:
                if (Near(p, m_a1, 1e-12)) return;
                m_c = Lerp(m_a1, p, 0.5);
                m_axis = p - m_c;
                AskOther();
                break;
            case Centre:
                m_c = p;
                E->Ask(From(P(L"Specify endpoint of axis", kAcceptPoint), p));
                m_step = AxisFromCentre;
                break;
            case AxisFromCentre:
                if (Near(p, m_c, 1e-12)) return;
                m_axis = p - m_c;
                AskOther();
                break;
            case Other:
                SetRatio(Dist(m_c, p));
                break;
            case StartAngle:
                m_start = ParamOf(p);
                E->Ask(From(P(L"Specify end angle", kAcceptPoint | kAcceptNumber), m_c));
                m_step = EndAngle;
                break;
            case EndAngle:
                End(ParamOf(p));
                break;
        }
    }
    void OnNumber(double v) override {
        if (m_step == Other) {
            SetRatio(v);
        } else if (m_step == StartAngle) {
            m_start = NormalizeAngle(ToRadians(v));
            E->Ask(From(P(L"Specify end angle", kAcceptPoint | kAcceptNumber), m_c));
            m_step = EndAngle;
        } else if (m_step == EndAngle) {
            End(NormalizeAngle(ToRadians(v)));
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        switch (m_step) {
            case Axis2:
                out.guides.push_back({m_a1, c});
                break;
            case AxisFromCentre:
                out.guides.push_back({m_c, c});
                break;
            case Other: {
                const double r = Dist(m_c, c) / Length(m_axis);
                if (r > 1e-9) out.entities.push_back(Entity::Ellipse(m_c, m_axis, r));
                out.guides.push_back({m_c, c});
                break;
            }
            case StartAngle:
                out.guides.push_back({m_c, c});
                out.guides.push_back({m_c - m_axis, m_c + m_axis});
                break;
            case EndAngle: {
                const double t = ParamOf(c);
                if (std::fabs(SweepCCW(m_start, t)) > 1e-9)
                    out.entities.push_back(Entity::EllipseArc(m_c, m_axis, m_ratio, m_start, t));
                out.guides.push_back({m_c, c});
                break;
            }
            default:
                break;
        }
    }
};

// ---------------------------------------------------------------------------
// POLYGON: sides, then centre and radius (inscribed or circumscribed), or
// one edge.
// ---------------------------------------------------------------------------
class PolygonCmd : public Command {
    enum Step { Sides, CentreOrEdge, Kind, Radius, Edge1, Edge2 } m_step = Sides;
    int m_sides = 4;
    Vec2 m_c, m_e1;

    // The polygon around the centre whose first vertex (inscribed) or
    // first edge midpoint (circumscribed) is at p.
    Entity Around(Vec2 p) const {
        const double half = kPi / m_sides;
        Vec2 first = p;
        if (!E->lastInscribed) first = m_c + RotateAround(p - m_c, {0, 0}, -half) / std::cos(half);
        return Entity::RegularPolygon(m_c, first, m_sides);
    }
    Entity FromEdge(Vec2 e2) const {
        const double len = Dist(m_e1, e2);
        const double apothem = len / 2 / std::tan(kPi / m_sides);
        const Vec2 centre = Lerp(m_e1, e2, 0.5) + Perp(Normalized(e2 - m_e1)) * apothem;
        Entity pl = Entity::RegularPolygon(centre, m_e1, m_sides);
        return pl;
    }
    void Add(const Entity& e) {
        E->Doc().Add(e);
        E->Finish();
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Polygon");
        E->Ask(P(L"Enter number of sides", kAcceptNumber | kAcceptEmpty, {}, std::to_wstring(E->lastSides)));
    }
    void OnNumber(double v) override {
        if (m_step == Sides) {
            const int n = (int)std::lround(v);
            if (n < 3 || n > 1024 || std::fabs(v - n) > 1e-9) {
                E->Msg(L"Enter a whole number from 3 to 1024.");
                return;
            }
            m_sides = E->lastSides = n;
            E->Ask(P(L"Specify center of polygon", kAcceptPoint, {{L"E", L"Edge"}}));
            m_step = CentreOrEdge;
        } else if (m_step == Radius) {
            if (!Positive(E, v)) return;
            // A typed radius: the bottom edge lies flat.
            const double half = kPi / m_sides;
            const double circum = E->lastInscribed ? v : v / std::cos(half);
            Add(Entity::RegularPolygon(m_c, Polar(m_c, circum, -kPi / 2 - half), m_sides));
        }
    }
    void OnEnter() override {
        if (m_step == Sides) OnNumber(E->lastSides);
        else if (m_step == Kind) OnKeyword(E->lastInscribed ? L"I" : L"C");
        else E->Finish();
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == CentreOrEdge && k == L"E") {
            E->Ask(P(L"Specify first endpoint of edge", kAcceptPoint));
            m_step = Edge1;
        } else if (m_step == Kind) {
            E->lastInscribed = k == L"I";
            E->Ask(From(P(L"Specify radius of circle", kAcceptPoint | kAcceptNumber), m_c));
            m_step = Radius;
        }
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case CentreOrEdge:
                m_c = p;
                E->Ask(P(L"Enter an option", kAcceptEmpty, {{L"I", L"Inscribed in circle"}, {L"C", L"Circumscribed about circle"}},
                         E->lastInscribed ? L"I" : L"C"));
                m_step = Kind;
                break;
            case Radius:
                if (!Near(p, m_c, 1e-12)) Add(Around(p));
                break;
            case Edge1:
                m_e1 = p;
                E->Ask(From(P(L"Specify second endpoint of edge", kAcceptPoint), p));
                m_step = Edge2;
                break;
            case Edge2:
                if (!Near(p, m_e1, 1e-12)) Add(FromEdge(p));
                break;
            default:
                break;
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Radius && !Near(c, m_c, 1e-12)) {
            out.entities.push_back(Around(c));
            out.guides.push_back({m_c, c});
        }
        if (m_step == Edge2 && !Near(c, m_e1, 1e-12)) out.entities.push_back(FromEdge(c));
    }
};

// ---------------------------------------------------------------------------
// SPLINE through fit points
// ---------------------------------------------------------------------------
class SplineCmd : public Command {
    std::vector<Vec2> m_pts;

    void AskNext() {
        std::vector<Keyword> kw;
        if (m_pts.size() >= 3) kw.push_back({L"C", L"Close"});
        kw.push_back({L"U", L"Undo"});
        E->Ask(From(P(L"Enter next point", kAcceptPoint | kAcceptEmpty, kw), m_pts.back()));
    }
    void Commit(bool closed) {
        if (m_pts.size() >= 2) E->Doc().Add(Entity::Spline(m_pts, closed));
        E->Finish();
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Spline");
        E->Ask(P(L"Specify first point", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        if (!m_pts.empty() && Near(p, m_pts.back(), 1e-12)) return;
        m_pts.push_back(p);
        AskNext();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"C" && m_pts.size() >= 3) {
            Commit(true);
        } else if (k == L"U") {
            if (m_pts.size() > 1) {
                m_pts.pop_back();
                E->SetLastPoint(m_pts.back());
            }
            AskNext();
        }
    }
    void OnEnter() override { Commit(false); }
    void OnCancel() override {
        if (m_pts.size() >= 2) E->Doc().Add(Entity::Spline(m_pts, false));
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_pts.empty()) return;
        std::vector<Vec2> pts = m_pts;
        if (!Near(c, pts.back(), 1e-12)) pts.push_back(c);
        if (pts.size() >= 2) out.entities.push_back(Entity::Spline(pts, false));
    }
};

// ---------------------------------------------------------------------------
// POINT: as many as you like until Enter or Esc
// ---------------------------------------------------------------------------
class PointCmd : public Command {
public:
    void Start() override {
        E->Doc().BeginChange(L"Point");
        E->Ask(P(L"Specify a point (Enter when done)", kAcceptPoint | kAcceptEmpty));
    }
    void OnPoint(Vec2 p) override { E->Doc().Add(Entity::Point(p)); }
};

// ---------------------------------------------------------------------------
// SELECTSIMILAR
// ---------------------------------------------------------------------------
class SelectSimilarCmd : public Command {
public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        std::vector<Entity> kinds;
        for (uint64_t id : SelectionIds(E))
            if (const Entity* e = E->Doc().Find(id)) kinds.push_back(*e);
        std::vector<uint64_t> ids;
        for (const auto& kv : E->Doc().Entities())
            for (const Entity& k : kinds)
                if (SameKind(kv.second, k)) {
                    ids.push_back(kv.first);
                    break;
                }
        E->AddToSelection(ids);
        E->Msg(std::to_wstring(E->Selection().size()) + L" object(s) of the same kind selected.");
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// Grip editing: Enter cycles STRETCH, MOVE, ROTATE, SCALE, MIRROR.
// ---------------------------------------------------------------------------
class GripEditCmd : public Command {
    enum Mode { Stretch, Move, RotateMode, ScaleMode, MirrorMode, ModeCount };
    std::vector<uint64_t> m_ids;
    Grip m_hot;
    Vec2 m_base;
    int m_mode = Stretch;
    bool m_copy = false, m_askBase = false;
    StepLog m_log;

    static const wchar_t* Name(int mode) {
        static const wchar_t* names[] = {L"STRETCH", L"MOVE", L"ROTATE", L"SCALE", L"MIRROR"};
        return names[mode];
    }
    void AskMode() {
        static const wchar_t* asks[] = {L"Specify stretch point", L"Specify move point", L"Specify rotation angle",
                                        L"Specify scale factor", L"Specify second point"};
        std::wstring text = std::wstring(L"** ") + Name(m_mode) + (m_copy ? L" (multiple)" : L"") + L" ** " + asks[m_mode];
        Prompt p;
        p.text = text;
        p.accept = kAcceptPoint | kAcceptEmpty | kAcceptKeyword |
                   ((m_mode == RotateMode || m_mode == ScaleMode) ? (unsigned)kAcceptNumber : 0u);
        p.keywords = {{L"B", L"Base point"}, {L"C", L"Copy"}, {L"U", L"Undo"}, {L"X", L"eXit"}};
        p.hasBase = true;
        p.base = m_base;
        E->Ask(p);
    }
    // The selected objects as they would be with the cursor (or a typed
    // value) at p.
    std::vector<Entity> Edited(Vec2 p, double number, bool useNumber) const {
        std::vector<Entity> out;
        const double tol = 1e-9 * (1 + Length(m_hot.p));
        for (uint64_t id : m_ids) {
            const Entity* e = E->Doc().Find(id);
            if (!e) continue;
            Entity x = *e;
            bool changed = true;
            switch (m_mode) {
                case Stretch: {
                    // Every grip at the hot grip's place moves (shared end points).
                    std::vector<Grip> grips;
                    GripsOf(x, grips);
                    changed = false;
                    const Vec2 to = m_hot.p + (p - m_base);
                    for (const Grip& g : grips)
                        if (Near(g.p, m_hot.p, tol)) {
                            Entity trial = x;
                            if (DragGrip(trial, g.index, g.p, to)) {
                                x = trial;
                                changed = true;
                            }
                            break;  // one grip per object
                        }
                    break;
                }
                case Move:
                    Translate(x, p - m_base);
                    break;
                case RotateMode:
                    Rotate(x, m_base, useNumber ? ToRadians(number) : AngleOf(p - m_base));
                    break;
                case ScaleMode: {
                    const double f = useNumber ? number : Dist(m_base, p);
                    if (!(f > 1e-12)) changed = false;
                    else Scale(x, m_base, f);
                    break;
                }
                case MirrorMode:
                    if (Near(p, m_base, 1e-12)) changed = false;
                    else Mirror(x, m_base, p);
                    break;
            }
            if (changed) out.push_back(std::move(x));
        }
        return out;
    }
    void Apply(Vec2 p, double number, bool useNumber) {
        std::vector<Entity> edited = Edited(p, number, useNumber);
        if (edited.empty()) {
            E->Msg(L"Nothing changes there.");
            return;
        }
        if (m_copy) {
            StepLog::Step& step = m_log.Begin();
            for (Entity& x : edited) {
                x.id = 0;
                step.added.push_back(E->Doc().Add(x));
            }
            AskMode();
            return;
        }
        for (const Entity& x : edited) E->Doc().Replace(x);
        E->Finish();
    }

public:
    GripEditCmd(std::vector<uint64_t> ids, const Grip& hot) : m_ids(std::move(ids)), m_hot(hot), m_base(hot.p) {}
    void Start() override {
        E->Doc().BeginChange(L"Grip edit");
        AskMode();
    }
    void OnPoint(Vec2 p) override {
        if (m_askBase) {
            m_askBase = false;
            m_base = p;
            AskMode();
            return;
        }
        Apply(p, 0, false);
    }
    void OnNumber(double v) override {
        if (m_mode == RotateMode || m_mode == ScaleMode) Apply(m_base, v, true);
    }
    void OnEnter() override {
        m_mode = (m_mode + 1) % ModeCount;
        AskMode();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"X") return E->Finish();
        if (k == L"B") {
            m_askBase = true;
            E->Ask(P(L"Specify base point", kAcceptPoint));
            return;
        }
        if (k == L"C") m_copy = true;
        if (k == L"U" && !m_log.Undo(E->Doc())) E->Msg(L"Nothing to undo.");
        AskMode();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_askBase) return;
        out.entities = Edited(c, 0, false);
        if (out.entities.size() > kMaxPreview) out.entities.resize(kMaxPreview);
        out.guides.push_back({m_base, c});
    }
};
}  // namespace

std::unique_ptr<Command> CreateDrawCommand(const std::wstring& name) {
    if (name == L"ELLIPSE") return std::make_unique<EllipseCmd>();
    if (name == L"POLYGON") return std::make_unique<PolygonCmd>();
    if (name == L"SPLINE") return std::make_unique<SplineCmd>();
    if (name == L"POINT") return std::make_unique<PointCmd>();
    if (name == L"SELECTSIMILAR") return std::make_unique<SelectSimilarCmd>();
    return nullptr;
}

std::unique_ptr<Command> CreateGripEdit(const std::vector<uint64_t>& ids, const Grip& hot) {
    return std::make_unique<GripEditCmd>(ids, hot);
}
