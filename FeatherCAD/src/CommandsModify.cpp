// CommandsModify.cpp - the editing commands of Phase B1: TRIM, EXTEND,
// OFFSET, FILLET, CHAMFER, MIRROR, SCALE, STRETCH, BREAK, JOIN, EXPLODE,
// LENGTHEN and the associative arrays (ARRAY, ARRAYEDIT). The geometry is
// in Modify.cpp; these classes ask for the input and record the changes.
#include <algorithm>

#include "CommandUtil.h"
#include "Modify.h"

namespace {
const wchar_t* kNotCurve = L"Pick a line, arc, circle or polyline.";

bool IsOpenCurve(const Entity& e) { return IsCurve(e) && !IsClosedCurve(e); }

// The outlines of the other objects, as edges to trim to or extend to. With
// `only` set, just those objects; with `near` set, just the ones that touch it.
std::vector<Prim> Edges(Engine* E, uint64_t except, const std::vector<uint64_t>& only, const Box* near) {
    std::vector<Prim> prims;
    auto add = [&](const Entity& e) {
        if (e.id == except || IsAnnotation(e) || !E->Doc().Visible(e)) return;
        if (near && !Bounds(e).Intersects(*near)) return;
        Primitives(e, prims);
    };
    if (!only.empty()) {
        for (uint64_t id : only)
            if (const Entity* e = E->Doc().Find(id)) add(*e);
    } else {
        for (const auto& kv : E->Doc().Entities()) add(kv.second);
    }
    return prims;
}

double Reach(Engine* E) {
    const Box ext = E->Doc().Extents();
    return 2 * std::hypot(ext.Width(), ext.Height()) + 1;
}

Box BoundsOf(Engine* E, const std::vector<uint64_t>& ids) {
    Box b;
    for (uint64_t id : ids)
        if (const Entity* e = E->Doc().Find(id)) b.Add(Bounds(*e));
    return b;
}

// ---------------------------------------------------------------------------
// TRIM and EXTEND (Shift+click does the other one)
// ---------------------------------------------------------------------------
class TrimExtendCmd : public Command {
    bool m_extend;
    std::vector<uint64_t> m_edges;  // empty: every object is an edge
    StepLog m_log;

    void AskPick() {
        if (m_extend)
            E->Ask(Pick(L"Click each line to lengthen it up to the next object (Shift+click cuts instead)", kAcceptEmpty,
                        {{L"B", L"Boundary edges"}, {L"U", L"Undo"}}));
        else
            E->Ask(Pick(L"Click each piece to cut away, up to where it crosses (Shift+click lengthens instead)", kAcceptEmpty,
                        {{L"T", L"cuTting edges"}, {L"U", L"Undo"}}));
    }
    // Trims or extends object `id` at `pick`; into `preview` only if given.
    bool Apply(uint64_t id, Vec2 pick, bool extend, Preview* preview) {
        const Entity* e = E->Doc().Find(id);
        if (!e) return false;
        if (extend) {
            Entity out;
            if (!ExtendAt(*e, pick, Edges(E, id, m_edges, nullptr), Reach(E), out)) return false;
            if (preview) {
                preview->entities.push_back(out);
                return true;
            }
            m_log.Begin().before.push_back(*e);
            E->Doc().Replace(out);
            return true;
        }
        const Box near = Bounds(*e).Inflated(1e-9 * (1 + Bounds(*e).Width() + Bounds(*e).Height()));
        std::vector<Entity> pieces;
        Entity removed;
        if (!TrimAt(*e, pick, Edges(E, id, m_edges, &near), pieces, &removed)) return false;
        if (preview) {
            preview->removed.push_back(removed);
            return true;
        }
        StepLog::Step& step = m_log.Begin();
        step.before.push_back(*e);
        if (pieces.empty()) {
            E->Doc().Erase(id);
            return true;
        }
        pieces[0].id = id;
        E->Doc().Replace(pieces[0]);
        for (size_t i = 1; i < pieces.size(); ++i) {
            pieces[i].id = 0;
            step.added.push_back(E->Doc().Add(pieces[i]));
        }
        return true;
    }

public:
    explicit TrimExtendCmd(bool extend) : m_extend(extend) {}
    void Start() override {
        E->ClearSelection();
        E->Doc().BeginChange(m_extend ? L"Extend" : L"Trim");
        E->Msg(m_extend ? L"Every object is a boundary; B chooses particular ones."
                        : L"Every object is a cutting edge; T chooses particular ones.");
        AskPick();
    }
    void OnPoint(Vec2 p) override {
        const uint64_t id = E->PickEntity(p);
        const bool extend = m_extend != E->LastPointShift();
        if (!id) E->Msg(L"No object there.");
        else if (!Apply(id, p, extend, nullptr))
            E->Msg(extend ? L"Nothing to extend to in that direction." : L"That object cannot be trimmed there.");
        AskPick();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"U") {
            if (!m_log.Undo(E->Doc())) E->Msg(L"Nothing to undo.");
            AskPick();
        } else {
            E->ClearSelection();
            E->Msg(m_extend ? L"Select the boundary edges, then press Enter." : L"Select the cutting edges, then press Enter.");
            E->AskSelection();
        }
    }
    void OnSelected() override {
        m_edges = SelectionIds(E);
        E->ClearSelection();
        E->Msg(std::to_wstring(m_edges.size()) + (m_extend ? L" boundary edge(s)." : L" cutting edge(s)."));
        AskPick();
    }
    bool OnNothingSelected() override {
        m_edges.clear();
        E->Msg(m_extend ? L"No edges chosen: every object is a boundary." : L"No edges chosen: every object is a cutting edge.");
        AskPick();
        return true;
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (const uint64_t id = E->PickEntity(c)) Apply(id, c, m_extend, &out);
    }
};

// ---------------------------------------------------------------------------
// OFFSET
// ---------------------------------------------------------------------------
class OffsetCmd : public Command {
    enum Step { Distance, SecondPoint, Select, Side } m_step = Distance;
    double m_dist = 0;
    bool m_through = false;
    Vec2 m_p1;
    uint64_t m_target = 0;
    StepLog m_log;

    void AskSelect() {
        E->Ask(Pick(L"Select object to offset", kAcceptEmpty, {{L"E", L"Exit"}, {L"U", L"Undo"}}, L"Exit"));
        m_step = Select;
    }
    bool Make(Vec2 p, Entity& out) {
        const Entity* e = E->Doc().Find(m_target);
        if (!e) return false;
        const double d = m_through ? DistanceTo(*e, p) : m_dist;
        return d > 0 && OffsetEntity(*e, OffsetSide(*e, p) * d, out);
    }

public:
    void Start() override {
        E->Doc().BeginChange(L"Offset");
        const double last = E->lastOffset;
        E->Ask(P(L"Specify offset distance", kAcceptNumber | kAcceptPoint | kAcceptEmpty, {{L"T", L"Through"}},
                 last < 0 ? L"Through" : E->Len(last)));
    }
    void OnNumber(double v) override {
        if (m_step != Distance || !Positive(E, v)) return;
        m_dist = E->lastOffset = v;
        AskSelect();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"T") {
            m_through = true;
            E->lastOffset = -1;
            AskSelect();
        } else if (k == L"B" && m_step == Side) {
            // Symmetric: a copy on each side at the same distance.
            const Entity* e = E->Doc().Find(m_target);
            if (!e || m_through) {
                E->Msg(L"Both sides needs a distance (not Through).");
                return;
            }
            StepLog::Step& step = m_log.Begin();
            int made = 0;
            for (double side : {1.0, -1.0}) {
                Entity out;
                if (OffsetEntity(*E->Doc().Find(m_target), side * m_dist, out)) {
                    step.added.push_back(E->Doc().Add(out));
                    ++made;
                }
            }
            if (made < 2) E->Msg(L"Only one side could be offset that far.");
            AskSelect();
        } else if (k == L"E") {
            E->Finish();
        } else if (k == L"U") {
            if (!m_log.Undo(E->Doc())) E->Msg(L"Nothing to undo.");
            AskSelect();
        }
    }
    void OnEnter() override {
        if (m_step == Distance) {
            if (E->lastOffset < 0) OnKeyword(L"T");
            else OnNumber(E->lastOffset);
            return;
        }
        E->Finish();
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Distance:
                m_p1 = p;
                E->Ask(From(P(L"Specify second point", kAcceptPoint), p));
                m_step = SecondPoint;
                break;
            case SecondPoint:
                OnNumberFromPoints(Dist(m_p1, p));
                break;
            case Select: {
                const uint64_t id = E->PickEntity(p);
                const Entity* e = id ? E->Doc().Find(id) : nullptr;
                if (!e || !IsCurve(*e)) {
                    E->Msg(e ? kNotCurve : L"No object there.");
                    return;
                }
                m_target = id;
                E->Ask(P(m_through ? L"Specify through point" : L"Specify point on side to offset", kAcceptPoint,
                         {{L"B", L"Both sides"}, {L"E", L"Exit"}, {L"U", L"Undo"}}));
                m_step = Side;
                break;
            }
            case Side: {
                Entity out;
                if (!Make(p, out)) {
                    E->Msg(L"The object cannot be offset that far on that side.");
                    return;
                }
                m_log.Begin().added.push_back(E->Doc().Add(out));
                AskSelect();
                break;
            }
        }
    }
    void OnNumberFromPoints(double d) {
        m_step = Distance;
        OnNumber(d);
    }
    void GetPreview(Vec2 c, Preview& out) override {
        Entity e;
        if (m_step == Side && Make(c, e)) out.entities.push_back(e);
        if (m_step == SecondPoint) out.guides.push_back({m_p1, c});
    }
};

// ---------------------------------------------------------------------------
// FILLET and CHAMFER
// ---------------------------------------------------------------------------
bool SameShape(const Entity& a, const Entity& b) {
    if (a.type != b.type) return false;
    const double tol = 1e-9 * (1 + Length(a.p0) + Length(a.p1));
    if (a.type == EntityType::Polyline) {
        if (a.verts.size() != b.verts.size()) return false;
        for (size_t i = 0; i < a.verts.size(); ++i)
            if (!Near(a.verts[i].p, b.verts[i].p, tol) || a.verts[i].bulge != b.verts[i].bulge) return false;
        return true;
    }
    return Near(a.p0, b.p0, tol) && Near(a.p1, b.p1, tol) && std::fabs(a.radius - b.radius) <= tol &&
           std::fabs(a.startAngle - b.startAngle) <= 1e-12 && std::fabs(a.endAngle - b.endAngle) <= 1e-12;
}

class CornerCmd : public Command {
    bool m_chamfer;
    bool m_keptWhole = false;  // a polyline could not be cut back (its corner is inside it)
    enum Step { First, Second, Radius, Dist1, Dist2, AngleLength, AngleAngle, Poly, TrimMode } m_step = First;
    uint64_t m_first = 0;
    Vec2 m_p1;
    bool m_multiple = false;
    StepLog m_log;

    // The first pick also takes a number: the radius (or both chamfer distances).
    void AskFirst() {
        if (m_chamfer) {
            const std::wstring now = E->chamferByAngle ? E->Len(E->lastChamfer1) + L" at " + E->Ang(E->lastChamferAngle) + L"\x00B0"
                                                       : E->Len(E->lastChamfer1) + L" x " + E->Len(E->lastChamfer2);
            const std::wstring text = L"Select first line (chamfer " + now + L"; type a number to change it)";
            E->Ask(Pick(text.c_str(), kAcceptEmpty | kAcceptNumber,
                        {{L"U", L"Undo"}, {L"P", L"Polyline"}, {L"D", L"Distance"}, {L"A", L"Angle"}, {L"T", L"Trim"},
                         {L"M", L"Multiple"}}));
        } else {
            const std::wstring text = L"Select first object (radius " + E->Len(E->lastFilletRadius) +
                                      L"; type a number to change it)";
            E->Ask(Pick(text.c_str(), kAcceptEmpty | kAcceptNumber,
                        {{L"U", L"Undo"}, {L"P", L"Polyline"}, {L"R", L"Radius"}, {L"T", L"Trim"}, {L"M", L"Multiple"}}));
        }
        m_step = First;
    }
    bool Zero() const {
        return m_chamfer ? !E->chamferByAngle && E->lastChamfer1 == 0 && E->lastChamfer2 == 0 : E->lastFilletRadius == 0;
    }
    void Settings() {
        const std::wstring mode = E->cornerTrim ? L"TRIM" : L"NOTRIM";
        if (m_chamfer) {
            if (E->chamferByAngle)
                E->Msg(L"(" + mode + L" mode) Current chamfer Length = " + E->Len(E->lastChamfer1) + L", Angle = " +
                       E->Ang(E->lastChamferAngle) + L"\x00B0");
            else
                E->Msg(L"(" + mode + L" mode) Current chamfer Dist1 = " + E->Len(E->lastChamfer1) + L", Dist2 = " +
                       E->Len(E->lastChamfer2));
        } else {
            E->Msg(L"Current settings: Mode = " + mode + L", Radius = " + E->Len(E->lastFilletRadius));
        }
        if (Zero())
            E->Msg(m_chamfer ? L"The distances are 0, which makes a sharp corner. Type a distance (for example 5) to bevel."
                             : L"The radius is 0, which makes a sharp corner. Type a radius (for example 5) to round it.");
    }
    // The corner for the second pick; zero = Shift held (sharp corner).
    bool Corner(uint64_t second, Vec2 p2, bool zero, CornerResult& res, std::wstring& why) {
        const Entity* pa = E->Doc().Find(m_first);
        const Entity* pb = E->Doc().Find(second);
        if (!pa || !pb) return false;
        // A polyline's segment takes part as a line, and is put back afterwards.
        Entity a = *pa, b = *pb;
        size_t segA = 0, segB = 0;
        const bool polyA = pa->type == EntityType::Polyline, polyB = pb->type == EntityType::Polyline;
        if ((polyA && !SegmentAsLine(*pa, m_p1, a, segA)) || (polyB && !SegmentAsLine(*pb, p2, b, segB))) {
            why = L"Pick a straight segment of the polyline (or explode it).";
            return false;
        }
        bool ok;
        if (!m_chamfer) {
            ok = FilletObjects(a, m_p1, b, p2, zero ? 0 : E->lastFilletRadius, E->cornerTrim, res, why);
        } else {
            double d1 = zero ? 0 : E->lastChamfer1, d2 = zero ? 0 : E->lastChamfer2;
            if (!zero && E->chamferByAngle &&
                !ChamferDistanceFromAngle(a, m_p1, b, p2, E->lastChamfer1, E->lastChamferAngle, d2)) {
                why = L"That angle does not fit between these lines.";
                return false;
            }
            ok = ChamferLines(a, m_p1, b, p2, d1, d2, E->cornerTrim, res, why);
        }
        if (!ok) return false;
        m_keptWhole = false;
        if (polyA && res.changeFirst) {
            Entity pl;
            if (PutSegmentBack(*pa, segA, res.first, pl)) res.first = pl;
            else res.changeFirst = false, m_keptWhole = true;
        }
        if (polyB && res.changeSecond) {
            Entity pl;
            if (PutSegmentBack(*pb, segB, res.second, pl)) res.second = pl;
            else res.changeSecond = false, m_keptWhole = true;
        }
        if (res.changeFirst && SameShape(res.first, *pa)) res.changeFirst = false;
        if (res.changeSecond && SameShape(res.second, *pb)) res.changeSecond = false;
        return true;
    }
    bool CutPolyline(Entity& pl, int vertex, bool zero) {
        if (m_chamfer) {
            double d2 = E->lastChamfer2;
            if (E->chamferByAngle) d2 = E->lastChamfer1 * std::tan(E->lastChamferAngle);
            return !zero && ChamferPolyline(pl, E->lastChamfer1, d2, vertex) > 0;
        }
        return !zero && FilletPolyline(pl, E->lastFilletRadius, vertex) > 0;
    }
    void Done() {
        if (m_multiple) AskFirst();
        else E->Finish();
    }
    void AskNumber(Step step, const wchar_t* text, double def) {
        E->Ask(P(text, kAcceptNumber | kAcceptEmpty, {}, E->Len(def)));
        m_step = step;
    }

public:
    explicit CornerCmd(bool chamfer) : m_chamfer(chamfer) {}
    void Start() override {
        E->ClearSelection();
        E->Doc().BeginChange(m_chamfer ? L"Chamfer" : L"Fillet");
        Settings();
        AskFirst();
    }
    void OnPoint(Vec2 p) override {
        if (m_step == First || m_step == Poly) {
            const uint64_t id = E->PickEntity(p);
            const Entity* e = id ? E->Doc().Find(id) : nullptr;
            if (!e) {
                E->Msg(L"No object there.");
                return;
            }
            if (m_step == Poly) {
                if (e->type != EntityType::Polyline) {
                    E->Msg(L"That is not a polyline.");
                    return;
                }
                Entity pl = *e;
                const size_t before = pl.verts.size();
                if (!CutPolyline(pl, -1, false)) {
                    E->Msg(L"No corner of that polyline could be done (it needs two straight segments long enough).");
                } else {
                    m_log.Begin().before.push_back(*e);
                    E->Doc().Replace(pl);
                    E->Msg(std::to_wstring(pl.verts.size() - before) + (m_chamfer ? L" corner(s) chamfered." : L" corner(s) filleted."));
                }
                Done();
                return;
            }
            m_first = id;
            m_p1 = p;
            E->Ask(Pick(m_chamfer ? L"Select second line or shift-select to apply corner"
                                  : L"Select second object or shift-select to apply corner"));
            m_step = Second;
            return;
        }
        if (m_step != Second) return;
        const uint64_t id = E->PickEntity(p);
        if (!id) {
            E->Msg(L"No object there.");
            return;
        }
        const bool zero = E->LastPointShift();
        const Entity* a = E->Doc().Find(m_first);
        if (id == m_first) {
            const int v = a ? SharedVertex(*a, m_p1, p) : -1;
            Entity pl = *a;
            if (v < 0 || !CutPolyline(pl, v, zero)) {
                E->Msg(L"Pick a different object (or two neighbouring straight segments of a polyline).");
                return;
            }
            m_log.Begin().before.push_back(*a);
            E->Doc().Replace(pl);
            Done();
            return;
        }
        CornerResult res;
        std::wstring why;
        if (!Corner(id, p, zero, res, why)) {
            E->Msg(why);
            AskFirst();
            return;
        }
        if (!res.changeFirst && !res.changeSecond && !res.hasJoin) {
            E->Msg(Zero() || zero ? (m_chamfer ? L"Those already meet in a sharp corner. The chamfer distances are 0: type a "
                                                 L"distance (for example 5), then pick the lines again."
                                               : L"Those already meet in a sharp corner. The radius is 0: type a radius "
                                                 L"(for example 5), then pick the objects again.")
                                  : L"Nothing to change there.");
            AskFirst();
            return;
        }
        if (m_keptWhole)
            E->Msg(L"The polyline was left whole (the corner is inside it, so it could not be cut back). "
                   L"Explode it first to trim it too.");
        StepLog::Step& step = m_log.Begin();
        if (res.changeFirst) {
            step.before.push_back(*E->Doc().Find(m_first));
            res.first.id = m_first;
            E->Doc().Replace(res.first);
        }
        if (res.changeSecond) {
            step.before.push_back(*E->Doc().Find(id));
            res.second.id = id;
            E->Doc().Replace(res.second);
        }
        if (res.hasJoin) {
            // The new arc or line is on the objects' layer when they share one.
            const Entity* a = E->Doc().Find(m_first);
            const Entity* b = E->Doc().Find(id);
            if (a && b && a->layer == b->layer) CopyStyle(*a, res.join);
            step.added.push_back(E->Doc().Add(res.join));
        }
        Done();
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == TrimMode) {
            E->cornerTrim = k == L"T";
            AskFirst();
            return;
        }
        if (k == L"U") {
            if (!m_log.Undo(E->Doc())) E->Msg(L"Nothing to undo.");
            AskFirst();
        } else if (k == L"P") {
            E->Ask(Pick(L"Select 2D polyline"));
            m_step = Poly;
        } else if (k == L"R") {
            AskNumber(Radius, L"Specify fillet radius", E->lastFilletRadius);
        } else if (k == L"D") {
            AskNumber(Dist1, L"Specify first chamfer distance", E->lastChamfer1);
        } else if (k == L"A") {
            AskNumber(AngleLength, L"Specify chamfer length on the first line", E->lastChamfer1);
        } else if (k == L"T") {
            E->Ask(P(L"Enter Trim mode option", kAcceptEmpty, {{L"T", L"Trim"}, {L"N", L"No trim"}},
                     E->cornerTrim ? L"Trim" : L"No trim"));
            m_step = TrimMode;
        } else if (k == L"M") {
            m_multiple = true;
            E->Msg(L"Multiple: keeps asking until you press Enter.");
            AskFirst();
        }
    }
    void OnNumber(double v) override {
        if (v < 0) {
            E->Msg(L"The value cannot be negative.");
            return;
        }
        switch (m_step) {
            case First:  // typed at the first pick: the radius, or both chamfer distances
                if (m_chamfer) {
                    E->lastChamfer1 = E->lastChamfer2 = v;
                    E->chamferByAngle = false;
                } else {
                    E->lastFilletRadius = v;
                }
                break;
            case Radius:
                E->lastFilletRadius = v;
                break;
            case Dist1:
                E->lastChamfer1 = v;
                E->chamferByAngle = false;
                AskNumber(Dist2, L"Specify second chamfer distance", v);
                return;
            case Dist2:
                E->lastChamfer2 = v;
                break;
            case AngleLength:
                E->lastChamfer1 = v;
                E->Ask(P(L"Specify chamfer angle from the first line", kAcceptNumber | kAcceptEmpty, {},
                         E->Ang(E->lastChamferAngle)));
                m_step = AngleAngle;
                return;
            case AngleAngle:
                if (v <= 0 || v >= 180) {
                    E->Msg(L"The angle must be between 0 and 180.");
                    return;
                }
                E->lastChamferAngle = ToRadians(v);
                E->chamferByAngle = true;
                break;
            default:
                return;
        }
        Settings();
        AskFirst();
    }
    void OnEnter() override {
        switch (m_step) {
            case Radius: OnNumber(E->lastFilletRadius); return;
            case Dist1: OnNumber(E->lastChamfer1); return;
            case Dist2: OnNumber(E->lastChamfer1); return;
            case AngleLength: OnNumber(E->lastChamfer1); return;
            case AngleAngle: OnNumber(ToDegrees(E->lastChamferAngle)); return;
            case TrimMode: AskFirst(); return;
            default: E->Finish();
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Second) return;
        const uint64_t id = E->PickEntity(c);
        if (!id || id == m_first) return;
        CornerResult res;
        std::wstring why;
        if (!Corner(id, c, false, res, why)) return;
        if (res.hasJoin) out.entities.push_back(res.join);
        if (res.changeFirst) out.entities.push_back(res.first);
        if (res.changeSecond) out.entities.push_back(res.second);
    }
};

// ---------------------------------------------------------------------------
// MIRROR
// ---------------------------------------------------------------------------
class MirrorCmd : public Command {
    enum Step { Select, First, Second, EraseSource } m_step = Select;
    std::vector<uint64_t> m_ids;
    Vec2 m_a, m_b;

    void Apply(bool eraseSource) {
        E->Doc().BeginChange(L"Mirror");
        std::vector<Entity> copies;
        for (uint64_t id : m_ids) {
            const Entity* e = E->Doc().Find(id);
            if (!e) continue;
            Entity m = *e;
            Mirror(m, m_a, m_b);
            if (eraseSource) E->Doc().Replace(m);
            else copies.push_back(std::move(m));
        }
        if (!eraseSource) E->Doc().AddGroup(std::move(copies));
        E->Msg(std::to_wstring(m_ids.size()) + L" object(s) mirrored.");
        E->ClearSelection();
        E->Finish();
    }

public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        m_ids = SelectionIds(E);
        E->Ask(P(L"Specify first point of mirror line", kAcceptPoint));
        m_step = First;
    }
    void OnPoint(Vec2 p) override {
        if (m_step == First) {
            m_a = p;
            E->Ask(From(P(L"Specify second point of mirror line", kAcceptPoint), p));
            m_step = Second;
        } else if (m_step == Second) {
            if (Near(p, m_a, 1e-12)) return;
            m_b = p;
            E->Ask(P(L"Erase source objects?", kAcceptEmpty, {{L"Y", L"Yes"}, {L"N", L"No"}}, L"No"));
            m_step = EraseSource;
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == EraseSource) Apply(k == L"Y");
    }
    void OnEnter() override {
        if (m_step == EraseSource) Apply(false);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Second || Near(c, m_a, 1e-12)) return;
        PreviewTransformed(E, m_ids, out, [&](Entity& e) { Mirror(e, m_a, c); });
        out.guides.push_back({m_a, c});
    }
};

// ---------------------------------------------------------------------------
// SCALE
// ---------------------------------------------------------------------------
class ScaleCmd : public Command {
    enum Step { Select, Base, Factor, RefLength, RefSecond, NewLength, NewFirst, NewSecond } m_step = Select;
    std::vector<uint64_t> m_ids;
    Vec2 m_base, m_r1, m_n1;
    double m_ref = 1;
    bool m_copy = false;

    void AskFactor() {
        E->Ask(From(P(L"Specify scale factor", kAcceptNumber | kAcceptPoint | kAcceptEmpty,
                      {{L"C", L"Copy"}, {L"R", L"Reference"}}, E->Len(E->lastScale)),
                    m_base));
        m_step = Factor;
    }
    void AskNew() {
        E->Ask(From(P(L"Specify new length", kAcceptNumber | kAcceptPoint, {{L"P", L"Points"}}), m_base));
        m_step = NewLength;
    }
    void Apply(double f) {
        if (!(f > 0) || !std::isfinite(f)) {
            E->Msg(L"The scale factor must be positive.");
            return;
        }
        E->lastScale = f;
        E->Doc().BeginChange(L"Scale");
        std::vector<Entity> copies;
        for (uint64_t id : m_ids) {
            const Entity* e = E->Doc().Find(id);
            if (!e) continue;
            Entity s = *e;
            Scale(s, m_base, f);
            if (m_copy) copies.push_back(std::move(s));
            else E->Doc().Replace(s);
        }
        if (m_copy) E->Doc().AddGroup(std::move(copies));
        E->Msg(std::to_wstring(m_ids.size()) + L" object(s) scaled by " + FormatDecimal(f, 4) + L".");
        E->ClearSelection();
        E->Finish();
    }
    double PreviewFactor(Vec2 c) const {
        switch (m_step) {
            case Factor: return Dist(m_base, c);
            case NewLength: return Dist(m_base, c) / m_ref;
            case NewSecond: return Dist(m_n1, c) / m_ref;
            default: return 0;
        }
    }

public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        m_ids = SelectionIds(E);
        E->Ask(P(L"Specify base point", kAcceptPoint));
        m_step = Base;
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Base:
                m_base = p;
                AskFactor();
                break;
            case Factor:
                Apply(Dist(m_base, p));
                break;
            case RefLength:
                m_r1 = p;
                E->Ask(From(P(L"Specify second point", kAcceptPoint), p));
                m_step = RefSecond;
                break;
            case RefSecond:
                m_ref = Dist(m_r1, p);
                if (Positive(E, m_ref)) AskNew();
                break;
            case NewLength:
                Apply(Dist(m_base, p) / m_ref);
                break;
            case NewFirst:
                m_n1 = p;
                E->Ask(From(P(L"Specify second point", kAcceptPoint), p));
                m_step = NewSecond;
                break;
            case NewSecond:
                Apply(Dist(m_n1, p) / m_ref);
                break;
            default:
                break;
        }
    }
    void OnNumber(double v) override {
        if (m_step == Factor) Apply(v);
        else if (m_step == RefLength && Positive(E, v)) {
            m_ref = v;
            AskNew();
        } else if (m_step == NewLength && Positive(E, v)) {
            Apply(v / m_ref);
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == Factor && k == L"C") {
            m_copy = !m_copy;
            E->Msg(m_copy ? L"Scaling a copy of the selected objects." : L"Scaling the selected objects.");
            AskFactor();
        } else if (m_step == Factor && k == L"R") {
            E->Ask(P(L"Specify reference length (or pick two points)", kAcceptNumber | kAcceptPoint | kAcceptEmpty, {},
                     E->Len(1)));
            m_step = RefLength;
        } else if (m_step == NewLength && k == L"P") {
            E->Ask(P(L"Specify first point", kAcceptPoint));
            m_step = NewFirst;
        }
    }
    void OnEnter() override {
        if (m_step == Factor) Apply(E->lastScale);
        else if (m_step == RefLength) OnNumber(1);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        const double f = PreviewFactor(c);
        if (!(f > 1e-9)) return;
        PreviewTransformed(E, m_ids, out, [&](Entity& e) { Scale(e, m_base, f); });
        out.guides.push_back({m_step == NewSecond ? m_n1 : m_base, c});
    }
};

// ---------------------------------------------------------------------------
// STRETCH
// ---------------------------------------------------------------------------
class StretchCmd : public Command {
    enum Step { Corner1, Corner2, Base, Second, Displacement } m_step = Corner1;
    Vec2 m_c1, m_base;
    Box m_window;
    std::vector<uint64_t> m_ids;

    void Apply(Vec2 d) {
        size_t n = 0;
        for (uint64_t id : m_ids) {
            const Entity* e = E->Doc().Find(id);
            if (!e) continue;
            Entity s = *e;
            if (StretchEntity(s, m_window, d)) {
                E->Doc().Replace(s);
                ++n;
            }
        }
        E->Msg(std::to_wstring(n) + L" object(s) stretched.");
        E->Finish();
    }

public:
    void Start() override {
        E->ClearSelection();
        E->Doc().BeginChange(L"Stretch");
        E->Msg(L"Points inside the crossing window move; objects completely inside it move whole.");
        E->Ask(P(L"Specify first corner of the crossing window", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Corner1:
                m_c1 = p;
                E->Ask(From(P(L"Specify opposite corner", kAcceptPoint), p));
                m_step = Corner2;
                break;
            case Corner2: {
                m_window = Box(m_c1, p);
                for (const auto& kv : E->Doc().Entities())
                    if (TouchesBox(kv.second, m_window)) m_ids.push_back(kv.first);
                if (m_ids.empty()) {
                    E->Msg(L"The window crosses no objects.");
                    E->Finish();
                    return;
                }
                E->Msg(std::to_wstring(m_ids.size()) + L" found");
                E->Ask(P(L"Specify base point", kAcceptPoint | kAcceptEmpty, {{L"D", L"Displacement"}}, L"Displacement"));
                m_step = Base;
                break;
            }
            case Base:
                m_base = p;
                E->Ask(From(P(L"Specify second point", kAcceptPoint | kAcceptEmpty, {}, L"use first point as displacement"),
                            p));
                m_step = Second;
                break;
            case Second:
                Apply(p - m_base);
                break;
            case Displacement:
                Apply(p);
                break;
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"D") {
            E->Ask(P(L"Specify displacement (dx,dy)", kAcceptPoint));
            m_step = Displacement;
        }
    }
    void OnEnter() override {
        if (m_step == Base) OnKeyword(L"D");
        else if (m_step == Second) Apply(m_base);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Corner2) {
            const Vec2 a{m_c1.x, c.y}, b{c.x, m_c1.y};
            out.guides = {{m_c1, a}, {a, c}, {c, b}, {b, m_c1}};
        }
        if (m_step != Second) return;
        const Vec2 d = c - m_base;
        for (size_t i = 0; i < m_ids.size() && i < kMaxPreview; ++i) {
            const Entity* e = E->Doc().Find(m_ids[i]);
            if (!e) continue;
            Entity s = *e;
            if (StretchEntity(s, m_window, d)) out.entities.push_back(s);
        }
        out.guides.push_back({m_base, c});
    }
};

// ---------------------------------------------------------------------------
// BREAK and BREAKATPOINT
// ---------------------------------------------------------------------------
class BreakCmd : public Command {
    bool m_atPoint;
    enum Step { Select, FirstPoint, SecondPoint } m_step = Select;
    uint64_t m_id = 0;
    Vec2 m_p1;

    void AskSecond() {
        if (m_atPoint) E->Ask(P(L"Specify break point", kAcceptPoint));
        else E->Ask(P(L"Specify second break point", kAcceptPoint, {{L"F", L"First point"}}));
        m_step = SecondPoint;
    }
    void Apply(Vec2 p2) {
        const Entity* e = E->Doc().Find(m_id);
        if (!e) return E->Finish();
        if (m_atPoint) m_p1 = p2;
        std::vector<Entity> pieces;
        std::wstring why;
        if (!BreakCurve(*e, CurveParam(*e, m_p1), CurveParam(*e, p2), pieces, why)) {
            E->Msg(why);
            return;
        }
        E->Doc().BeginChange(L"Break");
        if (pieces.empty()) {
            E->Doc().Erase(m_id);
        } else {
            pieces[0].id = m_id;
            E->Doc().Replace(pieces[0]);
            for (size_t i = 1; i < pieces.size(); ++i) {
                pieces[i].id = 0;
                E->Doc().Add(pieces[i]);
            }
        }
        E->Finish();
    }

public:
    explicit BreakCmd(bool atPoint) : m_atPoint(atPoint) {}
    void Start() override {
        E->ClearSelection();
        E->Ask(Pick(L"Select object"));
    }
    void OnPoint(Vec2 p) override {
        if (m_step == Select) {
            const uint64_t id = E->PickEntity(p);
            const Entity* e = id ? E->Doc().Find(id) : nullptr;
            if (!e || !IsCurve(*e)) {
                E->Msg(e ? kNotCurve : L"No object there.");
                return;
            }
            m_id = id;
            m_p1 = p;  // the pick is the first break point
            AskSecond();
        } else if (m_step == FirstPoint) {
            m_p1 = p;
            AskSecond();
        } else {
            Apply(p);
        }
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"F") {
            E->Ask(P(L"Specify first break point", kAcceptPoint));
            m_step = FirstPoint;
        }
    }
};

// ---------------------------------------------------------------------------
// JOIN and EXPLODE
// ---------------------------------------------------------------------------
class JoinCmd : public Command {
public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        const std::vector<uint64_t> ids = SelectionIds(E);
        std::vector<Entity> ents;
        for (uint64_t id : ids)
            if (const Entity* e = E->Doc().Find(id)) ents.push_back(*e);
        const std::vector<JoinGroup> groups = JoinObjects(ents);
        E->ClearSelection();
        if (groups.empty()) {
            E->Msg(L"Nothing to join: the ends of the selected lines, arcs and polylines do not meet.");
            return E->Finish();
        }
        E->Doc().BeginChange(L"Join");
        size_t joined = 0;
        for (const JoinGroup& g : groups) {
            for (size_t i : g.sources) E->Doc().Erase(ents[i].id);
            E->Doc().Add(g.result);
            joined += g.sources.size();
        }
        E->Msg(std::to_wstring(joined) + L" objects joined into " + std::to_wstring(groups.size()) + L".");
        E->Finish();
    }
};

class ExplodeCmd : public Command {
public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        const std::vector<uint64_t> ids = SelectionIds(E);
        E->ClearSelection();
        E->Doc().BeginChange(L"Explode");
        size_t done = 0;
        for (uint64_t id : ids) {
            const Entity* e = E->Doc().Find(id);
            std::vector<Entity> parts;
            if (!e || !Explode(*e, parts)) continue;
            E->Doc().Erase(id);
            for (Entity& part : parts) {
                part.id = 0;
                E->Doc().Add(part);
            }
            ++done;
        }
        E->Msg(done ? std::to_wstring(done) + L" object(s) exploded."
                    : L"Nothing to explode: only polylines, arrays, blocks, leaders, hatches and texts of several lines are made of other objects.");
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// LENGTHEN
// ---------------------------------------------------------------------------
class LengthenCmd : public Command {
    enum Mode { Delta, Percent, Total, Dynamic } m_mode = Delta;
    enum Step { Measure, Value, Change, DynPick, DynEnd } m_step = Measure;
    double m_value = 0;
    StepLog m_log;
    uint64_t m_dynId = 0;
    bool m_dynAtEnd = true;

    void AskMeasure() {
        E->Ask(Pick(L"Select an object to measure", kAcceptEmpty,
                    {{L"DE", L"DElta"}, {L"P", L"Percent"}, {L"T", L"Total"}, {L"DY", L"DYnamic"}}, L"DElta"));
        m_step = Measure;
    }
    void AskDynPick() {
        E->Ask(Pick(L"Select an object near the end to drag", kAcceptEmpty, {{L"U", L"Undo"}}));
        m_step = DynPick;
    }
    // Dynamic: the end follows the point c along the line or round the arc.
    bool Dragged(Vec2 c, Entity& out) const {
        const Entity* e = E->Doc().Find(m_dynId);
        if (!e) return false;
        const std::vector<CurvePiece> pieces = CurvePieces(*e);
        if (pieces.empty()) return false;
        const CurvePiece& pc = m_dynAtEnd ? pieces.back() : pieces.front();
        const Vec2 end = m_dynAtEnd ? pc.b : pc.a;
        double delta;
        if (!pc.arc) {
            const Vec2 out_ = m_dynAtEnd ? pc.Direction(pc.len) : -pc.Direction(0);
            delta = Dot(c - end, out_);
        } else {
            // Round the circle, in the direction the end moves outwards.
            const double a = std::remainder(AngleOf(c - pc.c) - AngleOf(end - pc.c), kTau);
            const double outward = (pc.sweep > 0) == m_dynAtEnd ? 1 : -1;
            delta = pc.r * a * outward;
        }
        out = *e;
        return std::fabs(delta) > 1e-12 && ChangeEnd(out, m_dynAtEnd, delta);
    }
    void AskChange() {
        E->Ask(Pick(L"Select an object to change", kAcceptEmpty, {{L"U", L"Undo"}}));
        m_step = Change;
    }
    bool Changed(uint64_t id, Vec2 pick, Entity& out, std::wstring& why) {
        const Entity* e = E->Doc().Find(id);
        if (!e || !IsOpenCurve(*e)) {
            why = L"Pick a line, an arc or an open polyline.";
            return false;
        }
        const double L = CurveLength(*e);
        const double delta = m_mode == Delta ? m_value : m_mode == Percent ? L * (m_value / 100 - 1) : m_value - L;
        out = *e;
        if (!ChangeEnd(out, CurveParam(*e, pick) > L / 2, delta)) {
            why = L"That would shorten it to nothing.";
            return false;
        }
        return true;
    }

public:
    void Start() override {
        E->ClearSelection();
        E->Doc().BeginChange(L"Lengthen");
        AskMeasure();
    }
    void OnPoint(Vec2 p) override {
        if (m_step == DynEnd) {
            Entity out;
            if (!Dragged(p, out)) {
                E->Msg(L"That would shorten it to nothing.");
                return;
            }
            m_log.Begin().before.push_back(*E->Doc().Find(m_dynId));
            E->Doc().Replace(out);
            AskDynPick();
            return;
        }
        const uint64_t id = E->PickEntity(p);
        if (!id) {
            E->Msg(L"No object there.");
            return;
        }
        if (m_step == DynPick) {
            const Entity* e = E->Doc().Find(id);
            if (!e || !IsOpenCurve(*e)) {
                E->Msg(L"Pick a line, an arc or an open polyline.");
                return;
            }
            m_dynId = id;
            m_dynAtEnd = CurveParam(*e, p) > CurveLength(*e) / 2;
            const Vec2 end = CurvePoint(*e, m_dynAtEnd ? CurveLength(*e) : 0);
            E->Ask(From(P(L"Specify new end point", kAcceptPoint), end));
            m_step = DynEnd;
            return;
        }
        if (m_step == Measure) {
            const Entity* e = E->Doc().Find(id);
            if (!IsCurve(*e)) {
                E->Msg(kNotCurve);
                return;
            }
            std::wstring msg = L"Current length: " + E->Len(CurveLength(*e));
            if (e->type == EntityType::Arc) {
                double sweep = SweepCCW(e->startAngle, e->endAngle);
                if (sweep <= 0) sweep = kTau;
                msg += L", included angle: " + E->Ang(sweep) + L"\x00B0";
            }
            E->Msg(msg);
            return;
        }
        if (m_step != Change) return;
        Entity out;
        std::wstring why;
        if (!Changed(id, p, out, why)) {
            E->Msg(why);
            return;
        }
        m_log.Begin().before.push_back(*E->Doc().Find(id));
        E->Doc().Replace(out);
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"U") {
            if (!m_log.Undo(E->Doc())) E->Msg(L"Nothing to undo.");
            return;
        }
        if (k == L"DY") {
            m_mode = Dynamic;
            AskDynPick();
            return;
        }
        m_mode = k == L"P" ? Percent : k == L"T" ? Total : Delta;
        if (m_mode == Delta)
            E->Ask(P(L"Enter delta length (negative shortens)", kAcceptNumber | kAcceptEmpty, {}, E->Len(E->lastLengthen)));
        else if (m_mode == Percent)
            E->Ask(P(L"Enter percentage length", kAcceptNumber, {}, L"100"));
        else
            E->Ask(P(L"Specify total length", kAcceptNumber));
        m_step = Value;
    }
    void OnNumber(double v) override {
        if (m_step != Value) return;
        if (m_mode != Delta && !Positive(E, v)) return;
        m_value = v;
        if (m_mode == Delta) E->lastLengthen = v;
        AskChange();
    }
    void OnEnter() override {
        if (m_step == Measure) OnKeyword(L"DE");
        else if (m_step == Value && m_mode == Delta) OnNumber(E->lastLengthen);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == DynEnd) {
            Entity e;
            if (Dragged(c, e)) out.entities.push_back(e);
            return;
        }
        if (m_step != Change) return;
        Entity e;
        std::wstring why;
        if (const uint64_t id = E->PickEntity(c))
            if (Changed(id, c, e, why)) out.entities.push_back(e);
    }
};

// ---------------------------------------------------------------------------
// ARRAY, ARRAYRECT, ARRAYPOLAR, ARRAYPATH
// ---------------------------------------------------------------------------
class ArrayCmd : public Command {
    int m_kind;  // ArrayData::Kind, or -1 to ask
    enum Step { Type, Columns, Rows, ColSpacing, RowSpacing, Centre, PolarCount, Fill, RotateItems, PathPick,
                PathCount, PathSpacing, Align } m_step = Type;
    std::vector<uint64_t> m_ids;
    Box m_box;
    ArrayData m_data;

    std::vector<Entity> Items() {
        std::vector<Entity> items;
        for (uint64_t id : m_ids)
            if (const Entity* e = E->Doc().Find(id)) {
                Entity c = *e;
                c.id = 0;
                items.push_back(std::move(c));
            }
        return items;
    }
    void Create() {
        m_data.items = Items();
        E->Doc().BeginChange(L"Array");
        for (uint64_t id : m_ids) E->Doc().Erase(id);
        E->Doc().Add(Entity::Array(m_data));
        E->ClearSelection();
        E->Msg(L"Array of " + std::to_wstring(ArrayCopyCount(m_data)) +
               L" copies made. ARRAYEDIT changes it; EXPLODE turns it into separate objects.");
        E->Finish();
    }
    void AskInt(Step step, const wchar_t* text, int def) {
        E->Ask(P(text, kAcceptNumber | kAcceptEmpty, {}, std::to_wstring(def)));
        m_step = step;
    }
    double DefaultColumn() const { return m_box.Width() > 1e-9 ? m_box.Width() * 1.5 : 10; }
    double DefaultRow() const { return m_box.Height() > 1e-9 ? m_box.Height() * 1.5 : 10; }
    void AskSpacing(Step step) {
        const bool cols = step == ColSpacing;
        E->Ask(From(P(cols ? L"Specify the distance between columns" : L"Specify the distance between rows",
                      kAcceptNumber | kAcceptPoint | kAcceptEmpty, {}, E->Len(cols ? DefaultColumn() : DefaultRow())),
                    Vec2{m_box.minX, m_box.minY}));
        m_step = step;
    }
    void Begin() {
        m_data = ArrayData{};
        m_data.kind = (ArrayData::Kind)m_kind;
        if (m_kind == ArrayData::Rect) {
            m_data.columnStep = {DefaultColumn(), 0};
            m_data.rowStep = {0, DefaultRow()};
            AskInt(Columns, L"Enter the number of columns", E->lastColumns);
        } else if (m_kind == ArrayData::Polar) {
            E->Ask(P(L"Specify center point of array", kAcceptPoint));
            m_step = Centre;
        } else {
            E->Ask(Pick(L"Select path curve"));
            m_step = PathPick;
        }
    }

public:
    explicit ArrayCmd(int kind) : m_kind(kind) {}
    void Start() override {
        if (m_kind < 0) {
            E->Ask(P(L"Enter array type", kAcceptEmpty, {{L"R", L"Rectangular"}, {L"PA", L"PAth"}, {L"PO", L"POlar"}},
                     L"Rectangular"));
            m_step = Type;
            return;
        }
        E->AskSelection();
    }
    void OnSelected() override {
        m_ids.clear();
        for (uint64_t id : SelectionIds(E)) {
            const Entity* e = E->Doc().Find(id);
            if (e && e->type != EntityType::Dimension) m_ids.push_back(id);  // dimensions are not arrayed
        }
        if (m_ids.empty()) {
            E->Msg(L"Dimensions cannot be put in an array. Select other objects.");
            return E->Finish();
        }
        m_box = BoundsOf(E, m_ids);
        Begin();
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == Type) {
            m_kind = k == L"PA" ? ArrayData::Path : k == L"PO" ? ArrayData::Polar : ArrayData::Rect;
            E->AskSelection();
        } else if (m_step == RotateItems) {
            m_data.rotateItems = k == L"Y";
            Create();
        } else if (m_step == Align) {
            m_data.align = k == L"Y";
            Create();
        } else if (m_step == PathSpacing && k == L"D") {
            m_data.spacing = 0;
            E->Ask(P(L"Align items to the path?", kAcceptEmpty, {{L"Y", L"Yes"}, {L"N", L"No"}}, L"Yes"));
            m_step = Align;
        }
    }
    void OnNumber(double v) override {
        const int n = (int)std::lround(v);
        auto count = [&](int limit) {
            if (n < 1 || std::fabs(v - n) > 1e-9 || n > limit) {
                E->Msg(L"Enter a whole number from 1 to " + std::to_wstring(limit) + L".");
                return false;
            }
            return true;
        };
        switch (m_step) {
            case Columns:
                if (!count(10000)) return;
                m_data.columns = E->lastColumns = n;
                AskInt(Rows, L"Enter the number of rows", E->lastRows);
                break;
            case Rows:
                if (!count(10000)) return;
                if ((long long)n * m_data.columns > 100000) {
                    E->Msg(L"At most 100000 copies.");
                    return;
                }
                m_data.rows = E->lastRows = n;
                AskSpacing(ColSpacing);
                break;
            case ColSpacing:
                if (v == 0) return (void)E->Msg(L"The distance cannot be zero.");
                m_data.columnStep = {v, 0};
                AskSpacing(RowSpacing);
                break;
            case RowSpacing:
                if (v == 0) return (void)E->Msg(L"The distance cannot be zero.");
                m_data.rowStep = {0, v};
                Create();
                break;
            case PolarCount:
                if (!count(100000)) return;
                m_data.count = E->lastPolarCount = n;
                E->Ask(P(L"Specify the angle to fill (+=ccw, -=cw)", kAcceptNumber | kAcceptEmpty, {}, L"360"));
                m_step = Fill;
                break;
            case Fill:
                if (v == 0 || std::fabs(v) > 360) return (void)E->Msg(L"The angle must be from -360 to 360 and not 0.");
                m_data.fillAngle = ToRadians(v);
                E->Ask(P(L"Rotate arrayed items?", kAcceptEmpty, {{L"Y", L"Yes"}, {L"N", L"No"}}, L"Yes"));
                m_step = RotateItems;
                break;
            case PathCount:
                if (!count(100000)) return;
                m_data.count = E->lastPathCount = n;
                E->Ask(P(L"Specify the distance between items", kAcceptNumber | kAcceptEmpty, {{L"D", L"Divide evenly"}},
                         L"Divide evenly"));
                m_step = PathSpacing;
                break;
            case PathSpacing:
                if (!Positive(E, v)) return;
                m_data.spacing = v;
                E->Ask(P(L"Align items to the path?", kAcceptEmpty, {{L"Y", L"Yes"}, {L"N", L"No"}}, L"Yes"));
                m_step = Align;
                break;
            default:
                break;
        }
    }
    void OnPoint(Vec2 p) override {
        const Vec2 base{m_box.minX, m_box.minY};
        switch (m_step) {
            case ColSpacing:
                OnNumber(std::fabs(p.x - base.x) > 1e-9 ? p.x - base.x : DefaultColumn());
                break;
            case RowSpacing:
                OnNumber(std::fabs(p.y - base.y) > 1e-9 ? p.y - base.y : DefaultRow());
                break;
            case Centre:
                m_data.centre = p;
                AskInt(PolarCount, L"Enter number of items", E->lastPolarCount);
                break;
            case PathPick: {
                const uint64_t id = E->PickEntity(p);
                const Entity* e = id ? E->Doc().Find(id) : nullptr;
                if (!e || !IsCurve(*e) || std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end()) {
                    E->Msg(L"Pick a line, arc, circle or polyline that is not one of the objects being copied.");
                    return;
                }
                Entity path = *e;
                path.id = 0;
                m_data.path = {path};
                AskInt(PathCount, L"Enter number of items along the path", E->lastPathCount);
                break;
            }
            default:
                break;
        }
    }
    void OnEnter() override {
        switch (m_step) {
            case Type: OnKeyword(L"R"); return;
            case Columns: OnNumber(E->lastColumns); return;
            case Rows: OnNumber(E->lastRows); return;
            case ColSpacing: OnNumber(DefaultColumn()); return;
            case RowSpacing: OnNumber(DefaultRow()); return;
            case PolarCount: OnNumber(E->lastPolarCount); return;
            case Fill: OnNumber(360); return;
            case RotateItems: OnKeyword(L"Y"); return;
            case PathCount: OnNumber(E->lastPathCount); return;
            case PathSpacing: OnKeyword(L"D"); return;
            case Align: OnKeyword(L"Y"); return;
            default: E->Finish();
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_ids.empty() || m_step == Type || m_step == Centre || m_step == PathPick) return;
        ArrayData d = m_data;
        d.items = Items();
        const Vec2 base{m_box.minX, m_box.minY};
        if (m_step == ColSpacing && std::fabs(c.x - base.x) > 1e-9) d.columnStep = {c.x - base.x, 0};
        if (m_step == RowSpacing && std::fabs(c.y - base.y) > 1e-9) d.rowStep = {0, c.y - base.y};
        if (ArrayCopyCount(d) * d.items.size() > 20000) return;  // too many to draw while moving
        ExpandArray(Entity::Array(d), out.entities);
    }
};

// ---------------------------------------------------------------------------
// ARRAYEDIT
// ---------------------------------------------------------------------------
class ArrayEditCmd : public Command {
    enum Step { Select, Option, Columns, Rows, ColSpacing, RowSpacing, Items, Fill, Rotate, Spacing, Align } m_step = Select;
    uint64_t m_id = 0;

    const ArrayData* Data() {
        const Entity* e = E->Doc().Find(m_id);
        return e && e->type == EntityType::Array ? e->array.get() : nullptr;
    }
    void Update(const std::function<void(ArrayData&)>& change) {
        const Entity* e = E->Doc().Find(m_id);
        if (!e) return;
        ArrayData d = *e->array;
        change(d);
        Entity n = Entity::Array(std::move(d));
        n.id = m_id;
        E->Doc().Replace(n);
        AskOption();
    }
    void AskOption() {
        const ArrayData* d = Data();
        if (!d) return E->Finish();
        if (d->kind == ArrayData::Rect)
            E->Ask(P(L"Enter an option", kAcceptEmpty,
                     {{L"C", L"Columns"}, {L"R", L"Rows"}, {L"S", L"Spacing"}, {L"E", L"Explode"}, {L"X", L"eXit"}}, L"eXit"));
        else if (d->kind == ArrayData::Polar)
            E->Ask(P(L"Enter an option", kAcceptEmpty,
                     {{L"I", L"Items"}, {L"F", L"Fill angle"}, {L"R", L"Rotate items"}, {L"E", L"Explode"}, {L"X", L"eXit"}},
                     L"eXit"));
        else
            E->Ask(P(L"Enter an option", kAcceptEmpty,
                     {{L"I", L"Items"}, {L"S", L"Spacing"}, {L"A", L"Align items"}, {L"E", L"Explode"}, {L"X", L"eXit"}},
                     L"eXit"));
        m_step = Option;
    }
    void AskValue(Step step, const wchar_t* text, const std::wstring& def, std::vector<Keyword> kw = {}) {
        E->Ask(P(text, kAcceptNumber | kAcceptEmpty, std::move(kw), def));
        m_step = step;
    }
    void AskYesNo(Step step, const wchar_t* text, bool current) {
        E->Ask(P(text, kAcceptEmpty, {{L"Y", L"Yes"}, {L"N", L"No"}}, current ? L"Yes" : L"No"));
        m_step = step;
    }

public:
    void Start() override {
        E->ClearSelection();
        E->Doc().BeginChange(L"Edit array");
        E->Ask(Pick(L"Select array"));
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Select) return;
        const uint64_t id = E->PickEntity(p);
        const Entity* e = id ? E->Doc().Find(id) : nullptr;
        if (!e || e->type != EntityType::Array) {
            E->Msg(e ? L"That is not an array (ARRAY makes one)." : L"No object there.");
            return;
        }
        m_id = id;
        AskOption();
    }
    void OnKeyword(const std::wstring& k) override {
        const ArrayData* d = Data();
        if (!d) return E->Finish();
        if (m_step == Rotate || m_step == Align) {
            const bool yes = k == L"Y";
            const bool rotate = m_step == Rotate;
            Update([&](ArrayData& a) { (rotate ? a.rotateItems : a.align) = yes; });
            return;
        }
        if (m_step == Spacing && k == L"D") {
            Update([](ArrayData& a) { a.spacing = 0; });
            return;
        }
        if (m_step != Option) return;
        if (k == L"X") return E->Finish();
        if (k == L"E") {
            std::vector<Entity> parts;
            Explode(*E->Doc().Find(m_id), parts);
            E->Doc().Erase(m_id);
            for (Entity& part : parts) {
                part.id = 0;
                E->Doc().Add(part);
            }
            E->Msg(L"The array is now " + std::to_wstring(parts.size()) + L" separate objects.");
            return E->Finish();
        }
        if (d->kind == ArrayData::Rect) {
            if (k == L"C") AskValue(Columns, L"Enter the number of columns", std::to_wstring(d->columns));
            else if (k == L"R") AskValue(Rows, L"Enter the number of rows", std::to_wstring(d->rows));
            else if (k == L"S") AskValue(ColSpacing, L"Specify the distance between columns", E->Len(Length(d->columnStep)));
        } else if (d->kind == ArrayData::Polar) {
            if (k == L"I") AskValue(Items, L"Enter number of items", std::to_wstring(d->count));
            else if (k == L"F") AskValue(Fill, L"Specify the angle to fill (+=ccw, -=cw)", E->Ang(d->fillAngle));
            else if (k == L"R") AskYesNo(Rotate, L"Rotate arrayed items?", d->rotateItems);
        } else {
            if (k == L"I") AskValue(Items, L"Enter number of items along the path", std::to_wstring(d->count));
            else if (k == L"S")
                AskValue(Spacing, L"Specify the distance between items", d->spacing > 0 ? E->Len(d->spacing) : L"Divide evenly",
                         {{L"D", L"Divide evenly"}});
            else if (k == L"A") AskYesNo(Align, L"Align items to the path?", d->align);
        }
    }
    void OnNumber(double v) override {
        const int n = (int)std::lround(v);
        const bool whole = n >= 1 && std::fabs(v - n) < 1e-9 && n <= 100000;
        // A spacing keeps its direction; only its length changes.
        auto withLength = [](Vec2 step, double len, Vec2 axis) {
            const Vec2 dir = Length(step) > 1e-12 ? Normalized(step) : axis;
            return dir * len;
        };
        switch (m_step) {
            case Columns:
            case Rows:
            case Items:
                if (!whole) return (void)E->Msg(L"Enter a whole number from 1 to 100000.");
                if (m_step == Columns) Update([&](ArrayData& a) { a.columns = n; });
                else if (m_step == Rows) Update([&](ArrayData& a) { a.rows = n; });
                else Update([&](ArrayData& a) { a.count = n; });
                break;
            case ColSpacing:
                if (!Positive(E, v)) return;
                {
                    const ArrayData* d = Data();
                    const Vec2 col = withLength(d->columnStep, v, {1, 0});
                    Update([&](ArrayData& a) { a.columnStep = col; });
                }
                AskValue(RowSpacing, L"Specify the distance between rows", E->Len(Length(Data()->rowStep)));
                break;
            case RowSpacing:
                if (!Positive(E, v)) return;
                Update([&](ArrayData& a) { a.rowStep = withLength(a.rowStep, v, {0, 1}); });
                break;
            case Fill:
                if (v == 0 || std::fabs(v) > 360) return (void)E->Msg(L"The angle must be from -360 to 360 and not 0.");
                Update([&](ArrayData& a) { a.fillAngle = ToRadians(v); });
                break;
            case Spacing:
                if (!Positive(E, v)) return;
                Update([&](ArrayData& a) { a.spacing = v; });
                break;
            default:
                break;
        }
    }
    void OnEnter() override {
        if (m_step == Option || m_step == Select) E->Finish();
        else AskOption();
    }
};
}  // namespace

std::unique_ptr<Command> CreateModifyCommand(const std::wstring& name) {
    if (name == L"TRIM") return std::make_unique<TrimExtendCmd>(false);
    if (name == L"EXTEND") return std::make_unique<TrimExtendCmd>(true);
    if (name == L"OFFSET") return std::make_unique<OffsetCmd>();
    if (name == L"FILLET") return std::make_unique<CornerCmd>(false);
    if (name == L"CHAMFER") return std::make_unique<CornerCmd>(true);
    if (name == L"MIRROR") return std::make_unique<MirrorCmd>();
    if (name == L"SCALE") return std::make_unique<ScaleCmd>();
    if (name == L"STRETCH") return std::make_unique<StretchCmd>();
    if (name == L"BREAK") return std::make_unique<BreakCmd>(false);
    if (name == L"BREAKATPOINT") return std::make_unique<BreakCmd>(true);
    if (name == L"JOIN") return std::make_unique<JoinCmd>();
    if (name == L"EXPLODE") return std::make_unique<ExplodeCmd>();
    if (name == L"LENGTHEN") return std::make_unique<LengthenCmd>();
    if (name == L"ARRAY") return std::make_unique<ArrayCmd>(-1);
    if (name == L"ARRAYRECT") return std::make_unique<ArrayCmd>(ArrayData::Rect);
    if (name == L"ARRAYPOLAR") return std::make_unique<ArrayCmd>(ArrayData::Polar);
    if (name == L"ARRAYPATH") return std::make_unique<ArrayCmd>(ArrayData::Path);
    if (name == L"ARRAYEDIT") return std::make_unique<ArrayEditCmd>();
    return nullptr;
}
