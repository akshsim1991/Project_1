// CommandsRelate.cpp - relationships (HORIZONTAL, PARALLEL, TANGENT...),
// the switches for keeping, adding and showing them, locked dimensions
// (DIMLOCK), variables (VAR) and GOALSEEK.
#include <algorithm>
#include <cmath>

#include "CommandUtil.h"
#include "Expr.h"
#include "Relate.h"

namespace {
// The element of the object at p: a point (an end, centre, midpoint...)
// when `point` is wanted, or when `orPoint` and the click is on one;
// otherwise the line, circle or arc (a polyline's segment).
bool PickElement(Engine* E, Vec2 p, bool point, bool orPoint, DimRef& out) {
    const uint64_t id = E->PickEntity(p, true);
    const Entity* e = id ? E->Doc().Find(id) : nullptr;
    if (!e || IsAnnotation(*e) || e->type == EntityType::Insert || e->type == EntityType::Array) return false;
    if (point || orPoint || e->type == EntityType::Point) {
        std::vector<Grip> grips;
        GripsOf(*e, grips);
        double best = 1e300;
        int bestGrip = -1;
        for (const Grip& g : grips) {
            if (ElementOf(*e, g.index).kind != ElemKind::Point) continue;
            const double d = Dist(g.p, p);
            if (d < best) best = d, bestGrip = g.index;
        }
        const double near = E->Host().PickRadius() * 3;
        if (bestGrip >= 0 && (point || e->type == EntityType::Point || best <= near)) {
            out = {id, bestGrip};
            return true;
        }
        if (point) return false;
    }
    if (e->type == EntityType::Line || e->type == EntityType::Circle || e->type == EntityType::Arc) {
        out = {id, -1};
        return true;
    }
    if (e->type == EntityType::Polyline) {  // the segment clicked
        std::vector<Prim> prims;
        Primitives(*e, prims);
        double best = 1e300;
        size_t k = 0;
        for (size_t i = 0; i < prims.size(); ++i)
            if (Distance(prims[i], p) < best) best = Distance(prims[i], p), k = i;
        out = {id, -1 - (int)k};
        return true;
    }
    return false;
}

void ReportSolve(Engine* E) {
    const SolveReport& r = E->Doc().LastSolve();
    if (!r.ok && !r.message.empty()) E->Msg(r.message);
}

class RelateCmd : public Command {
    RelKind m_kind;
    std::array<DimRef, 3> m_refs{};
    int m_have = 0;

    // What each pick is for.
    void AskNext() {
        std::wstring q;
        switch (m_kind) {
            case RelKind::Horizontal:
            case RelKind::Vertical: q = L"Select a line"; break;
            case RelKind::Fix: q = L"Select the object to fix in place"; break;
            case RelKind::Parallel:
            case RelKind::Perpendicular:
            case RelKind::Collinear: q = m_have == 0 ? L"Select first line" : L"Select second line (it is the one that moves)"; break;
            case RelKind::Concentric: q = m_have == 0 ? L"Select first circle or arc" : L"Select second circle or arc"; break;
            case RelKind::Equal: q = m_have == 0 ? L"Select first line, circle or arc" : L"Select second one (it changes size)"; break;
            case RelKind::Tangent: q = m_have == 0 ? L"Select first line, circle or arc" : L"Select second one"; break;
            case RelKind::Coincident:
                q = m_have == 0 ? L"Select a point (an end, centre or midpoint)" : L"Select the point or object it goes on";
                break;
            case RelKind::Midpoint: q = m_have == 0 ? L"Select a point (an end or centre)" : L"Select the line whose midpoint it goes on"; break;
            case RelKind::Symmetric:
                q = m_have == 0 ? L"Select first point" : m_have == 1 ? L"Select second point" : L"Select the line they are symmetric about";
                break;
        }
        Prompt p = Pick(L"", kAcceptEmpty);
        p.text = q;
        E->Ask(p);
    }

public:
    explicit RelateCmd(RelKind k) : m_kind(k) {}
    void Start() override { AskNext(); }
    void OnPoint(Vec2 p) override {
        const bool point = (m_kind == RelKind::Coincident && m_have == 0) || (m_kind == RelKind::Midpoint && m_have == 0) ||
                           (m_kind == RelKind::Symmetric && m_have < 2);
        const bool orPoint = m_kind == RelKind::Coincident && m_have == 1;
        DimRef r;
        if (!PickElement(E, p, point, orPoint, r)) {
            E->Msg(point ? L"Pick near an end, centre or midpoint of an object." : L"Pick a line, circle, arc or polyline.");
            return;
        }
        if (m_kind == RelKind::Fix) r.grip = -1;  // the whole object
        m_refs[(size_t)m_have++] = r;
        if (m_have < RelElementCount(m_kind)) return AskNext();
        Entity rel;
        std::wstring why;
        if (!MakeRelation(E->Doc(), m_kind, m_refs, E->Doc().Settings().dimStyle.textHeight * 0.8, rel, why)) {
            E->Msg(why);
            m_have = 0;
            m_refs = {};
            return AskNext();
        }
        rel.layer = -1;
        rel.color = 30;
        E->Doc().BeginChange(std::wstring(RelKindName(m_kind)) + L" relationship");
        E->Doc().Add(rel);
        E->Doc().EndChange();
        ReportSolve(E);
        if (!E->Doc().Settings().maintainRelations)
            E->Msg(L"Relationships are not being kept just now (MAINTAINREL turns it on).");
        // Ready for the next one of the same kind.
        m_have = 0;
        m_refs = {};
        AskNext();
    }
};

// MAINTAINREL, AUTOREL, SHOWREL: switches.
class RelSwitchCmd : public Command {
    int m_which;

public:
    explicit RelSwitchCmd(int which) : m_which(which) {}
    void Start() override {
        DrawingSettings s = E->Doc().Settings();
        bool& on = m_which == 0 ? s.maintainRelations : m_which == 1 ? s.autoRelations : s.showRelations;
        on = !on;
        E->Doc().SetSettings(s);
        const wchar_t* what = m_which == 0 ? L"Relationships are kept when objects change"
                              : m_which == 1 ? L"Relationships are added while drawing lines (horizontal, vertical, ends that meet)"
                                             : L"Relationship marks are shown";
        E->Msg(std::wstring(what) + (on ? L": on." : L": off."));
        if (m_which == 0 && on) {  // catch up with what changed meanwhile
            std::set<uint64_t> all, none;
            for (const auto& kv : E->Doc().Entities()) all.insert(kv.first);
            E->Doc().BeginChange(L"Keep relationships");
            SolveRelations(E->Doc(), all, none);
            E->Doc().EndChange();
            ReportSolve(E);
        }
        E->Finish();
    }
};

// DIMLOCK: dimensions that hold their value (or no longer do).
class DimLockCmd : public Command {
public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        std::vector<Entity> dims;
        bool anyOpen = false;
        for (uint64_t id : SelectionIds(E))
            if (const Entity* e = E->Doc().Find(id); e && e->type == EntityType::Dimension && e->dim && HasRefs(*e)) {
                dims.push_back(*e);
                anyOpen |= !e->dim->locked;
            }
        if (dims.empty()) {
            E->Msg(L"No dimensions attached to objects were selected.");
            return E->Finish();
        }
        E->Doc().BeginChange(anyOpen ? L"Lock dimensions" : L"Unlock dimensions");
        for (Entity& e : dims) {
            DimData d = *e.dim;
            d.locked = anyOpen;
            d.value = DimMeasure(d);
            if (!anyOpen) d.formula.clear();
            e.dim = std::make_shared<const DimData>(d);
            E->Doc().Replace(e);
        }
        E->Doc().EndChange();
        E->Msg(std::to_wstring(dims.size()) + (anyOpen ? L" dimension(s) locked: they hold their value as the drawing changes."
                                                       : L" dimension(s) unlocked: they follow the drawing."));
        ReportSolve(E);
        E->Finish();
    }
};

// VAR: a named value, or a formula of variables and dimensions.
class VarCmd : public Command {
    std::wstring m_name;

public:
    void Start() override {
        std::wstring list;
        for (const Variable& v : E->Doc().Variables()) list += (list.empty() ? L"" : L", ") + v.name + L" = " + v.formula;
        if (!list.empty()) E->Msg(L"Variables: " + list + L".");
        E->Ask(P(L"Enter variable name (or a dimension's name, such as D1)", kAcceptEmpty));
    }
    bool OnText(const std::wstring& s) override {
        if (m_name.empty()) {
            if (!ValidName(s)) {
                E->Msg(L"A name starts with a letter, then letters, digits or _.");
                return true;
            }
            m_name = s;
            E->Ask(P(L"Enter its value or formula (empty: remove it)", kAcceptEmpty));
            return true;
        }
        Set(s);
        return true;
    }
    void OnEnter() override {
        if (m_name.empty()) return E->Finish();
        Set(L"");
    }
    void Set(const std::wstring& formula) {
        Document& d = E->Doc();
        // A dimension's name: its formula.
        for (const auto& kv : d.Entities()) {
            const Entity& e = kv.second;
            if (e.type != EntityType::Dimension || !e.dim || !SameName(e.dim->name, m_name)) continue;
            if (!HasRefs(e) && !formula.empty()) {
                E->Msg(L"That dimension is not attached to objects, so a formula cannot change them.");
                return E->Finish();
            }
            Entity x = e;
            DimData dd = *x.dim;
            dd.formula = formula;
            if (!formula.empty()) dd.locked = true, dd.value = DimMeasure(dd);
            x.dim = std::make_shared<const DimData>(dd);
            d.BeginChange(L"Dimension formula");
            d.Replace(x);
            d.EndChange();
            ReportSolve(E);
            return E->Finish();
        }
        std::vector<Variable> vars = d.Variables();
        auto it = std::find_if(vars.begin(), vars.end(), [&](const Variable& v) { return SameName(v.name, m_name); });
        if (formula.empty()) {
            if (it != vars.end()) vars.erase(it);
        } else if (it != vars.end()) {
            it->formula = formula;
        } else {
            Variable v;
            v.name = m_name;
            v.formula = formula;
            vars.push_back(v);
        }
        d.SetVariables(vars, L"Variables");
        ReportSolve(E);
        double v;
        if (!formula.empty() && LookupName(d, m_name, v)) E->Msg(m_name + L" = " + E->Len(v));
        E->Finish();
    }
};

// GOALSEEK: the value a driving dimension (or a variable) needs for
// another dimension to reach a target.
class GoalSeekCmd : public Command {
    enum Step { Goal, Target, Driver } m_step = Goal;
    uint64_t m_goal = 0, m_driver = 0;
    std::wstring m_var;
    double m_target = 0;

    static bool IsAngle(const DimData& d) { return d.kind == DimData::Angular || d.kind == DimData::AngularCoord; }
    double Measure() const {
        const Entity* g = E->Doc().Find(m_goal);
        if (!g || !g->dim) return 0;
        const double m = DimMeasure(*g->dim);
        return IsAngle(*g->dim) ? ToDegrees(m) : m;
    }
    double DriverValue() const {
        double v = 0;
        if (!m_var.empty()) LookupName(E->Doc(), m_var, v);
        else if (const Entity* e = E->Doc().Find(m_driver); e && e->dim)
            v = IsAngle(*e->dim) ? ToDegrees(DimMeasure(*e->dim)) : DimMeasure(*e->dim);
        return v;
    }
    void SetDriver(double v) {
        Document& d = E->Doc();
        if (!m_var.empty()) {
            std::vector<Variable> vars = d.Variables();
            for (Variable& x : vars)
                if (SameName(x.name, m_var)) x.formula = TextFromUtf8(FormatNumber(v));
            d.SetVariables(vars, L"Goal seek");
        } else if (const Entity* e = d.Find(m_driver)) {
            Entity x = *e;
            DimData dd = *x.dim;
            dd.locked = true;
            dd.formula.clear();
            dd.value = IsAngle(dd) ? ToRadians(v) : v;
            x.dim = std::make_shared<const DimData>(dd);
            d.Replace(x);
        }
        d.Resolve();
    }
    void Seek() {
        Document& d = E->Doc();
        d.BeginChange(L"Goal seek");
        double v0 = DriverValue(), f0 = Measure() - m_target;
        const double tol = 1e-9 * (1 + std::fabs(m_target));
        bool ok = std::fabs(f0) <= tol;
        double v1 = v0 + (std::fabs(v0) > 1e-9 ? v0 * 0.02 : 0.1);
        if (!ok) {
            SetDriver(v1);
            double f1 = Measure() - m_target;
            for (int i = 0; i < 60 && !ok; ++i) {
                if (std::fabs(f1) <= tol) {
                    ok = true;
                    break;
                }
                const double slope = (f1 - f0) / (v1 - v0);
                if (!std::isfinite(slope) || std::fabs(slope) < 1e-14) break;
                double v2 = v1 - f1 / slope;
                if (m_var.empty() && v2 <= 0) v2 = v1 / 2;  // lengths stay positive
                v0 = v1, f0 = f1, v1 = v2;
                SetDriver(v1);
                f1 = Measure() - m_target;
            }
            ok = ok || std::fabs(f1) <= tol * 1e3;
        }
        if (!ok || !d.LastSolve().ok) {
            d.CancelChange();
            E->Msg(L"Goal seek found no value that gives " + E->Len(m_target) +
                   L" (is that dimension changed by the one chosen, through relationships or formulas?).");
            return E->Finish();
        }
        d.EndChange();
        const Entity* g = d.Find(m_goal);
        E->Msg(L"Goal seek: " + (g && g->dim ? g->dim->name : std::wstring(L"the dimension")) + L" is " + E->Len(Measure()) +
               L" when " + (m_var.empty() ? (d.Find(m_driver) ? d.Find(m_driver)->dim->name : L"") : m_var) + L" is " +
               E->Len(DriverValue()) + L".");
        E->Finish();
    }

public:
    void Start() override { E->Ask(Pick(L"Select the dimension that should reach a value")); }
    void OnPoint(Vec2 p) override {
        const uint64_t id = E->PickEntity(p, true);
        const Entity* e = id ? E->Doc().Find(id) : nullptr;
        if (!e || e->type != EntityType::Dimension || !e->dim) return E->Msg(L"Pick a dimension.");
        if (m_step == Goal) {
            m_goal = id;
            m_step = Target;
            E->Ask(P(L"Enter the value it should have", kAcceptNumber, {}, E->Len(Measure())));
        } else if (m_step == Driver) {
            if (id == m_goal) return E->Msg(L"Pick another dimension: the one that may change.");
            if (!HasRefs(*e)) return E->Msg(L"That dimension is not attached to objects, so it cannot change them.");
            m_driver = id;
            Seek();
        }
    }
    void OnNumber(double v) override {
        if (m_step != Target) return;
        m_target = v;
        m_step = Driver;
        E->Ask(Pick(L"Select the dimension that may change, or type a variable's name"));
    }
    bool OnText(const std::wstring& s) override {
        if (m_step != Driver || !ValidName(s)) return false;
        for (const Variable& v : E->Doc().Variables())
            if (SameName(v.name, s)) {
                m_var = v.name;
                Seek();
                return true;
            }
        E->Msg(L"There is no variable \"" + s + L"\".");
        return true;
    }
};
}  // namespace

std::unique_ptr<Command> CreateRelateCommand(const std::wstring& name) {
    static const std::pair<const wchar_t*, RelKind> kinds[] = {
        {L"HORIZONTAL", RelKind::Horizontal}, {L"VERTICAL", RelKind::Vertical},     {L"PARALLEL", RelKind::Parallel},
        {L"PERPENDICULAR", RelKind::Perpendicular}, {L"COLLINEAR", RelKind::Collinear}, {L"TANGENT", RelKind::Tangent},
        {L"CONCENTRIC", RelKind::Concentric}, {L"EQUAL", RelKind::Equal},           {L"COINCIDENT", RelKind::Coincident},
        {L"MIDPOINT", RelKind::Midpoint},   {L"SYMMETRIC", RelKind::Symmetric},     {L"FIX", RelKind::Fix},
    };
    for (const auto& k : kinds)
        if (name == k.first) return std::make_unique<RelateCmd>(k.second);
    if (name == L"MAINTAINREL") return std::make_unique<RelSwitchCmd>(0);
    if (name == L"AUTOREL") return std::make_unique<RelSwitchCmd>(1);
    if (name == L"SHOWREL") return std::make_unique<RelSwitchCmd>(2);
    if (name == L"DIMLOCK") return std::make_unique<DimLockCmd>();
    if (name == L"VAR") return std::make_unique<VarCmd>();
    if (name == L"GOALSEEK") return std::make_unique<GoalSeekCmd>();
    return nullptr;
}
