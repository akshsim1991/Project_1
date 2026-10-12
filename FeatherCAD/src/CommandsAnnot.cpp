// CommandsAnnot.cpp - annotation commands: TEXT, TEXTEDIT, LEADER, centre
// marks and lines, HATCH and FILL, blocks (BLOCK, INSERT, ATTDEF, ATTEDIT,
// PURGE), ordinate and angular coordinate dimensions, DIMSPACE, DIMDRIVEN,
// REVCLOUD and AREA.
#include <algorithm>
#include <cmath>
#include <cwctype>

#include "CommandUtil.h"
#include "Curve.h"
#include "Modify.h"
#include "Sheet.h"

namespace {
const wchar_t* const kJustifyKeys[] = {L"TL", L"TC", L"TR", L"ML", L"MC", L"MR", L"BL", L"BC", L"BR"};

std::vector<Keyword> JustifyKeywords() {
    const wchar_t* names[] = {L"Top left", L"Top centre", L"Top right", L"Middle left", L"Middle centre",
                              L"Middle right", L"Bottom left", L"Bottom centre", L"Bottom right"};
    std::vector<Keyword> k;
    for (int i = 0; i < 9; ++i) k.push_back({kJustifyKeys[i], names[i]});
    return k;
}

int JustifyFromKey(const std::wstring& key) {
    for (int i = 0; i < 9; ++i)
        if (key == kJustifyKeys[i]) return i;
    return -1;
}

std::wstring Joined(const std::vector<std::wstring>& lines) {
    std::wstring s;
    for (size_t i = 0; i < lines.size(); ++i) s += (i ? L"\n" : L"") + lines[i];
    return s;
}

// Lines typed one by one in the command line; an empty line ends.
struct TypedLines {
    std::vector<std::wstring> lines;
    bool active = false;
};

// The arc (circle, arc or polyline arc) picked near p.
bool PickedArc(Engine* E, Vec2 p, Vec2& c, double& r) {
    const uint64_t id = E->PickEntity(p, true);
    const Entity* e = id ? E->Doc().Find(id) : nullptr;
    if (!e) return false;
    if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
        c = e->p0;
        r = e->radius;
        return true;
    }
    if (e->type == EntityType::Ellipse) {
        c = e->p0;
        r = Length(e->p1);
        return true;
    }
    if (e->type != EntityType::Polyline) return false;
    std::vector<Prim> prims;
    Primitives(*e, prims);
    double best = 1e300;
    for (const Prim& pr : prims)
        if (pr.kind == Prim::Arc && Distance(pr, p) < best) best = Distance(pr, p), c = pr.c, r = pr.r;
    return best <= E->Host().PickRadius();
}

// The straight piece (line or polyline segment) picked near p.
bool PickedSegment(Engine* E, Vec2 p, Vec2& a, Vec2& b) {
    const uint64_t id = E->PickEntity(p, true);
    const Entity* e = id ? E->Doc().Find(id) : nullptr;
    if (!e) return false;
    if (e->type == EntityType::Line) {
        a = e->p0;
        b = e->p1;
        return true;
    }
    if (e->type != EntityType::Polyline) return false;
    std::vector<Prim> prims;
    Primitives(*e, prims);
    double best = 1e300;
    for (const Prim& pr : prims)
        if (pr.kind == Prim::Segment && Distance(pr, p) < best) best = Distance(pr, p), a = pr.a, b = pr.b;
    return best <= E->Host().PickRadius();
}

Entity CentreLine(Vec2 a, Vec2 b) {
    Entity e = Entity::Line(a, b);
    e.layer = -1;
    return e;
}

// The objects a click inside an area can be bounded by.
std::vector<const Entity*> AreaObjects(Engine* E, std::vector<uint64_t>* ids = nullptr) {
    std::vector<const Entity*> objs;
    for (const auto& kv : E->Doc().Entities()) {
        const Entity& e = kv.second;
        if (IsAnnotation(e) || e.type == EntityType::Point || !E->Doc().Visible(e)) continue;
        objs.push_back(&e);
        if (ids) ids->push_back(kv.first);
    }
    return objs;
}

double LoopLength(const std::vector<Vec2>& l) {
    double s = 0;
    for (size_t i = 0; i < l.size(); ++i) s += Dist(l[i], l[(i + 1) % l.size()]);
    return s;
}

std::wstring AreaUnits(Engine* E) {
    const std::wstring u = UnitsSymbol(E->Doc().Settings().units);
    return u.empty() ? L"" : L" " + u;
}

// ---------------------------------------------------------------------------
// TEXT: one or more lines of text, with symbols and fields
// ---------------------------------------------------------------------------
class TextCmd : public Command {
    enum Step { Where, Height, Rotation, Justify, Font, Lines } m_step = Where;
    TextData m_t;
    Vec2 m_at;
    TypedLines m_typed;

    void AskWhere() {
        m_step = Where;
        E->Ask(P(L"Specify insertion point", kAcceptPoint,
                 {{L"H", L"Height"}, {L"R", L"Rotation"}, {L"J", L"Justify"}, {L"F", L"Font"}}));
    }
    void Make() {
        E->Doc().BeginChange(L"Text");
        E->Doc().Add(MakeText(m_at, m_t));
        E->Doc().EndChange();
        TextData keep = m_t;
        keep.content.clear();
        keep.tag.clear();
        keep.prompt.clear();
        E->lastText = keep;
        E->Finish();
    }

public:
    void Start() override {
        m_t = E->lastText;
        m_t.content.clear();
        AskWhere();
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == Justify) {
            m_t.justify = std::max(0, JustifyFromKey(k));
            return AskWhere();
        }
        if (k == L"H") {
            m_step = Height;
            E->Ask(P(L"Specify text height", kAcceptNumber, {}, E->Len(m_t.height)));
        } else if (k == L"R") {
            m_step = Rotation;
            E->Ask(P(L"Specify rotation angle of text (degrees)", kAcceptNumber, {}, E->Ang(m_t.rotation)));
        } else if (k == L"J") {
            m_step = Justify;
            E->Ask(P(L"Choose where the insertion point is", kAcceptKeyword, JustifyKeywords()));
        } else if (k == L"F") {
            m_step = Font;
            E->Ask(P(L"Enter font name", kAcceptEmpty, {}, m_t.font));
        }
    }
    void OnNumber(double v) override {
        if (m_step == Height) {
            if (!Positive(E, v)) return;
            m_t.height = v;
        } else if (m_step == Rotation) {
            m_t.rotation = ToRadians(v);
        } else {
            return;
        }
        AskWhere();
    }
    bool OnText(const std::wstring& s) override {
        if (m_step == Font) {
            m_t.font = s;
            AskWhere();
            return true;
        }
        if (m_step == Lines) {
            m_typed.lines.push_back(s);
            E->Ask(P(L"Enter next line of text (an empty line ends)", kAcceptEmpty));
            return true;
        }
        return false;
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Where) return;
        m_at = p;
        const int r = E->Host().EditText(m_t, L"Text");
        if (r == 1) {
            if (m_t.content.empty()) return E->Finish();
            return Make();
        }
        if (r == 0) return E->Finish();
        m_step = Lines;
        E->Ask(P(L"Enter text (an empty line ends; %%d \x00B0, %%c \x00D8, %%p \x00B1, {date} {filename})", kAcceptEmpty));
    }
    void OnEnter() override {
        if (m_step == Font || m_step == Height || m_step == Rotation) return AskWhere();
        if (m_step == Lines && !m_typed.lines.empty()) {
            m_t.content = Joined(m_typed.lines);
            return Make();
        }
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Where) return;
        TextData t = m_t;
        t.content = L"Text";
        out.entities.push_back(MakeText(c, t));
    }
};

// ---------------------------------------------------------------------------
// TEXTEDIT: change what a text or a leader's note says
// ---------------------------------------------------------------------------
class TextEditCmd : public Command {
    uint64_t m_id = 0;
    TypedLines m_typed;

    void Apply(const TextData& t) {
        Entity e = *E->Doc().Find(m_id);
        EditAnnot(e).text = t;
        E->Doc().BeginChange(L"Edit text");
        E->Doc().Replace(e);
        E->Doc().EndChange();
    }

public:
    void Start() override {
        if (E->Selection().size() == 1) {
            const uint64_t id = *E->Selection().begin();
            if (const Entity* e = E->Doc().Find(id); e && TextOf(*e)) return Edit(id);
        }
        E->Ask(Pick(L"Select a text or leader to edit", kAcceptEmpty));
    }
    void Edit(uint64_t id) {
        m_id = id;
        TextData t = *TextOf(*E->Doc().Find(id));
        const int r = E->Host().EditText(t, L"Edit text");
        if (r == 1) {
            Apply(t);
            return E->Finish();
        }
        if (r == 0) return E->Finish();
        E->Msg(L"Now: " + t.content);
        E->Ask(P(L"Enter new text (an empty line ends)", kAcceptEmpty));
    }
    void OnPoint(Vec2 p) override {
        if (m_id) return;
        const uint64_t id = E->PickEntity(p);
        const Entity* e = id ? E->Doc().Find(id) : nullptr;
        if (!e || !TextOf(*e)) {
            E->Msg(e && e->type == EntityType::Dimension ? L"That is a dimension: use DIMEDIT (or double-click it)."
                   : e && e->type == EntityType::Insert ? L"That is a block: use ATTEDIT to change its attributes."
                                                         : L"Pick a text or a leader.");
            return;
        }
        Edit(id);
    }
    bool OnText(const std::wstring& s) override {
        if (!m_id) return false;
        m_typed.lines.push_back(s);
        E->Ask(P(L"Enter next line of text (an empty line ends)", kAcceptEmpty));
        return true;
    }
    void OnEnter() override {
        if (m_id && !m_typed.lines.empty()) {
            TextData t = *TextOf(*E->Doc().Find(m_id));
            t.content = Joined(m_typed.lines);
            Apply(t);
        }
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// LEADER: an arrow and lines to a note (in a box, or a balloon)
// ---------------------------------------------------------------------------
class LeaderCmd : public Command {
    std::vector<Vec2> m_pts;
    int m_frame;
    bool m_text = false;
    TypedLines m_typed;

    void AskNext() {
        if (m_pts.empty()) {
            E->Ask(P(L"Specify arrow point", kAcceptPoint, {{L"F", L"Frame"}}));
            return;
        }
        Prompt p = From(P(L"Specify next point", kAcceptPoint | kAcceptEmpty, {{L"U", L"Undo"}}), m_pts.back());
        E->Ask(p);
    }
    void Make(const std::wstring& content) {
        const DimStyle& st = E->Doc().Settings().dimStyle;
        TextData t = E->lastText;
        t.content = content;
        t.height = st.textHeight;
        t.frame = m_frame;
        t.rotation = 0;
        t.tag.clear();
        t.prompt.clear();
        E->Doc().BeginChange(L"Leader");
        E->Doc().Add(MakeLeader(m_pts, t, st.arrowSize));
        E->Doc().EndChange();
        E->Finish();
    }

public:
    explicit LeaderCmd(int frame) : m_frame(frame) {}
    void Start() override { AskNext(); }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"F") {
            E->Ask(P(L"Frame around the note", kAcceptKeyword, {{L"N", L"None"}, {L"B", L"Box"}, {L"C", L"Circle"}}));
        } else if (k == L"N" || k == L"B" || k == L"C") {
            m_frame = k == L"B" ? 1 : k == L"C" ? 2 : 0;
            AskNext();
        } else if (k == L"U" && !m_pts.empty()) {
            m_pts.pop_back();
            AskNext();
        }
    }
    void OnPoint(Vec2 p) override {
        if (m_text) return;
        if (!m_pts.empty() && Near(p, m_pts.back(), 1e-12)) return;
        m_pts.push_back(p);
        AskNext();
    }
    bool OnText(const std::wstring& s) override {
        if (!m_text) return false;
        m_typed.lines.push_back(s);
        E->Ask(P(L"Enter next line of the note (an empty line ends)", kAcceptEmpty));
        return true;
    }
    void OnEnter() override {
        if (m_text) return Make(Joined(m_typed.lines));
        if (m_pts.size() < 2) return E->Finish();
        TextData t = E->lastText;
        t.content.clear();
        t.height = E->Doc().Settings().dimStyle.textHeight;
        t.frame = m_frame;
        const int r = E->Host().EditText(t, L"Leader note");
        if (r == 1) {
            m_frame = t.frame;
            return Make(t.content);
        }
        if (r == 0) return Make(L"");
        m_text = true;
        E->Ask(P(L"Enter the note (an empty line ends; none for just the arrow)", kAcceptEmpty));
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_pts.empty() || m_text) return;
        std::vector<Vec2> pts = m_pts;
        pts.push_back(c);
        out.entities.push_back(MakeLeader(pts, TextData{}, E->Doc().Settings().dimStyle.arrowSize));
    }
};

// ---------------------------------------------------------------------------
// CENTERMARK and CENTERLINE: centre lines (CENTER line type)
// ---------------------------------------------------------------------------
class CentreMarkCmd : public Command {
public:
    void Start() override { E->Ask(Pick(L"Select a circle or arc", kAcceptEmpty)); }
    void OnPoint(Vec2 p) override {
        Vec2 c;
        double r = 0;
        if (!PickedArc(E, p, c, r)) {
            E->Msg(L"Pick a circle or an arc.");
            return;
        }
        const double ext = std::max(r * 0.15, 3.0);
        Entity h = CentreLine(c - Vec2{r + ext, 0}, c + Vec2{r + ext, 0});
        Entity v = CentreLine(c - Vec2{0, r + ext}, c + Vec2{0, r + ext});
        E->Doc().BeginChange(L"Centre mark");
        for (Entity* x : {&h, &v}) {
            const uint64_t id = E->Doc().Add(*x);
            Entity s = *E->Doc().Find(id);
            s.linetype = (int16_t)FindLineType(L"CENTER");
            E->Doc().Replace(s);
        }
        E->Doc().EndChange();
        E->Ask(Pick(L"Select another circle or arc", kAcceptEmpty));
    }
};

class CentreLineCmd : public Command {
    bool m_have = false;
    Vec2 m_a, m_b;

public:
    void Start() override { E->Ask(Pick(L"Select first line", kAcceptEmpty)); }
    void OnPoint(Vec2 p) override {
        Vec2 a, b;
        if (!PickedSegment(E, p, a, b)) {
            E->Msg(L"Pick a line (or a straight part of a polyline).");
            return;
        }
        if (!m_have) {
            m_a = a, m_b = b, m_have = true;
            E->Ask(Pick(L"Select second line", kAcceptEmpty));
            return;
        }
        if (Dot(b - a, m_b - m_a) < 0) std::swap(a, b);  // the same way round
        Vec2 s = Lerp(m_a, a, 0.5), t = Lerp(m_b, b, 0.5);
        if (Near(s, t, 1e-12)) {
            E->Msg(L"These lines give no centre line.");
            return;
        }
        const Vec2 u = Normalized(t - s);
        const double ext = std::max(Dist(s, t) * 0.05, 3.0);
        E->Doc().BeginChange(L"Centre line");
        const uint64_t id = E->Doc().Add(CentreLine(s - u * ext, t + u * ext));
        Entity x = *E->Doc().Find(id);
        x.linetype = (int16_t)FindLineType(L"CENTER");
        E->Doc().Replace(x);
        E->Doc().EndChange();
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// HATCH and FILL: a pattern or a solid fill in a closed area
// ---------------------------------------------------------------------------
class HatchCmd : public Command {
    enum Step { Where, Pattern, Scale, Angle } m_step = Where;
    HatchData m_h;
    StepLog m_log;
    bool m_solid;

    void AskWhere() {
        m_step = Where;
        E->Ask(P(L"Pick a point inside an area", kAcceptPoint | kAcceptEmpty,
                 {{L"O", L"select Objects"}, {L"P", L"Pattern"}, {L"S", L"Scale"}, {L"A", L"Angle"}, {L"U", L"Undo"}}));
    }
    uint64_t Add(HatchData h) {
        E->lastHatch.pattern = h.pattern;
        E->lastHatch.scale = h.scale;
        E->lastHatch.angle = h.angle;
        return E->Doc().Add(MakeHatch(std::move(h)));
    }

public:
    explicit HatchCmd(bool solid) : m_solid(solid) {}
    void Start() override {
        m_h = E->lastHatch;
        m_h.loops.clear();
        m_h.refs.clear();
        m_h.seeded = false;
        if (m_solid) m_h.pattern = L"SOLID";
        E->Msg(L"Pattern " + m_h.pattern + (IsSolid(m_h) ? L"" : L", scale " + FormatDecimal(m_h.scale, 4) + L", angle " +
                                                                   E->Ang(m_h.angle)) + L".");
        AskWhere();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"O") {
            E->AskSelection();
        } else if (k == L"P") {
            m_step = Pattern;
            std::wstring names;
            for (const HatchPattern& p : HatchPatterns()) names += (names.empty() ? L"" : L", ") + std::wstring(p.name);
            E->Msg(L"Patterns: " + names + L".");
            E->Ask(P(L"Enter a pattern name", kAcceptEmpty, {}, m_h.pattern));
        } else if (k == L"S") {
            m_step = Scale;
            E->Ask(P(L"Specify pattern scale", kAcceptNumber, {}, FormatDecimal(m_h.scale, 4)));
        } else if (k == L"A") {
            m_step = Angle;
            E->Ask(P(L"Specify pattern angle (degrees)", kAcceptNumber, {}, E->Ang(m_h.angle)));
        } else if (k == L"U") {
            if (!m_log.Undo(E->Doc())) E->Msg(L"Nothing to undo.");
            AskWhere();
        }
    }
    bool OnText(const std::wstring& s) override {
        if (m_step != Pattern) return false;
        const int i = FindHatchPattern(s);
        if (i < 0) {
            E->Msg(L"There is no pattern \"" + s + L"\".");
            return true;
        }
        m_h.pattern = HatchPatterns()[(size_t)i].name;
        AskWhere();
        return true;
    }
    void OnNumber(double v) override {
        if (m_step == Scale) {
            if (!Positive(E, v)) return;
            m_h.scale = v;
        } else if (m_step == Angle) {
            m_h.angle = ToRadians(v);
        } else {
            return;
        }
        AskWhere();
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Where) return;
        std::vector<uint64_t> ids;
        const std::vector<const Entity*> objs = AreaObjects(E, &ids);
        HatchData h = m_h;
        std::vector<size_t> used;
        if (!FindArea(objs, p, h.loops, &used)) {
            E->Msg(L"There is no closed area around that point (the outline may have a gap).");
            return;
        }
        h.seeded = true;
        h.seed = p;
        for (size_t i : used) h.refs.push_back(ids[i]);
        E->Doc().BeginChange(IsSolid(h) ? L"Fill" : L"Hatch");
        m_log.Begin().added.push_back(Add(std::move(h)));
        E->Doc().EndChange();
        AskWhere();
    }
    void OnSelected() override {
        HatchData h = m_h;
        for (uint64_t id : SelectionIds(E)) {
            const Entity* e = E->Doc().Find(id);
            std::vector<Vec2> loop;
            if (!e || !ClosedOutline(*e, loop)) continue;
            h.loops.push_back(std::move(loop));
            h.refs.push_back(id);
        }
        E->ClearSelection();
        if (h.loops.empty()) {
            E->Msg(L"None of those is closed (circle, closed polyline, ellipse or spline).");
            return AskWhere();
        }
        E->Doc().BeginChange(IsSolid(h) ? L"Fill" : L"Hatch");
        m_log.Begin().added.push_back(Add(std::move(h)));
        E->Doc().EndChange();
        AskWhere();
    }
    void OnEnter() override {
        if (m_step != Where) return AskWhere();
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// Blocks: BLOCK, INSERT, ATTDEF, ATTEDIT, PURGE
// ---------------------------------------------------------------------------
class BlockCmd : public Command {
    enum Step { Name, Base, Objects } m_step = Name;
    std::wstring m_name;
    Vec2 m_base;

public:
    void Start() override { E->Ask(P(L"Enter block name", kAcceptEmpty)); }
    bool OnText(const std::wstring& s) override {
        if (m_step != Name) return false;
        if (s[0] == L'*' || s.find_first_of(L"<>/\\\":;?,|=`") != std::wstring::npos) {
            E->Msg(L"A block name cannot start with * or contain < > / \\ \" : ; ? , | = `.");
            return true;
        }
        m_name = s;
        if (E->Doc().FindBlock(s)) E->Msg(L"Block \"" + s + L"\" is already there: it will be defined anew, and its references change with it.");
        m_step = Base;
        E->Ask(P(L"Specify base point", kAcceptPoint));
        return true;
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Base) return;
        m_base = p;
        m_step = Objects;
        E->AskSelection();
    }
    void OnSelected() override {
        BlockData b;
        b.name = m_name;
        b.base = m_base;
        const std::vector<uint64_t> ids = SelectionIds(E);
        for (uint64_t id : ids) {
            const Entity* e = E->Doc().Find(id);
            if (!e) continue;
            if (e->type == EntityType::Insert && e->annot && e->annot->block &&
                (SameName(e->annot->block->name, m_name) || BlockUses(*e->annot->block, m_name))) {
                E->Msg(L"A block cannot contain itself.");
                return E->Finish();
            }
            Entity x = *e;
            if (x.type == EntityType::Dimension && x.dim) {  // not attached inside a block
                DimData d = *x.dim;
                for (DimRef& r : d.refs) r = {};
                x.dim = std::make_shared<const DimData>(std::move(d));
            }
            if (x.type == EntityType::Hatch && x.annot) EditAnnot(x).hatch.refs.clear();
            x.id = 0;
            b.items.push_back(std::move(x));
        }
        if (b.items.empty()) return E->Finish();
        E->ClearSelection();
        E->Doc().BeginChange(L"Block");
        const auto def = E->Doc().DefineBlock(std::move(b));
        // The objects become one reference to the block, where they were.
        for (uint64_t id : ids) E->Doc().Erase(id);
        std::vector<std::wstring> values;
        for (const TextData* t : AttributeDefs(*def)) values.push_back(t->content);
        const uint64_t ref = E->Doc().Add(MakeInsert(def, m_base, 1, 0, values));
        E->Doc().EndChange();
        E->lastBlock = m_name;
        E->AddToSelection({ref});
        E->Msg(L"Block \"" + m_name + L"\" made of " + std::to_wstring(def->items.size()) + L" object(s). INSERT places it again.");
        E->Finish();
    }
};

std::wstring BlockNames(const Document& doc) {
    std::wstring s;
    for (const auto& b : doc.Blocks()) s += (s.empty() ? L"" : L", ") + b->name;
    return s;
}

class InsertCmd : public Command {
    enum Step { Name, Where, Scale, Rotate, Values } m_step = Name;
    std::shared_ptr<const BlockData> m_b;
    double m_scale = 1, m_rot = 0;
    Vec2 m_at;
    std::vector<const TextData*> m_defs;
    std::vector<std::wstring> m_values;

    void AskWhere() {
        m_step = Where;
        E->Ask(P(L"Specify insertion point", kAcceptPoint, {{L"S", L"Scale"}, {L"R", L"Rotate"}}));
    }
    void Choose(const std::shared_ptr<const BlockData>& b) {
        m_b = b;
        m_defs = AttributeDefs(*b);
        AskWhere();
    }
    void AskValue() {
        if (m_values.size() >= m_defs.size()) return Make();
        const TextData& t = *m_defs[m_values.size()];
        const std::wstring q = L"Enter " + (t.prompt.empty() ? t.tag : t.prompt);
        Prompt p = P(L"", kAcceptEmpty, {}, t.content);
        p.text = q;
        E->Ask(p);
    }
    void Make() {
        E->Doc().BeginChange(L"Insert");
        E->Doc().Add(MakeInsert(m_b, m_at, m_scale, m_rot, m_values));
        E->Doc().EndChange();
        E->lastBlock = m_b->name;
        E->Finish();
    }

public:
    void Start() override {
        if (E->Doc().Blocks().empty()) {
            E->Msg(L"There are no blocks in this drawing yet. BLOCK makes one from objects.");
            return E->Finish();
        }
        E->Msg(L"Blocks: " + BlockNames(E->Doc()) + L".");
        const auto last = E->Doc().FindBlock(E->lastBlock);
        E->Ask(P(L"Enter block name", kAcceptEmpty, {}, last ? last->name : E->Doc().Blocks().back()->name));
    }
    bool OnText(const std::wstring& s) override {
        if (m_step == Name) {
            if (auto b = E->Doc().FindBlock(s)) Choose(b);
            else E->Msg(L"There is no block \"" + s + L"\". Blocks: " + BlockNames(E->Doc()) + L".");
            return true;
        }
        if (m_step == Values) {
            m_values.push_back(s);
            AskValue();
            return true;
        }
        return false;
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"S") {
            m_step = Scale;
            E->Ask(P(L"Specify scale factor", kAcceptNumber, {}, FormatDecimal(m_scale, 4)));
        } else if (k == L"R") {
            m_step = Rotate;
            E->Ask(P(L"Specify rotation angle (degrees)", kAcceptNumber, {}, E->Ang(m_rot)));
        }
    }
    void OnNumber(double v) override {
        if (m_step == Scale) {
            if (!Positive(E, v)) return;
            m_scale = v;
        } else if (m_step == Rotate) {
            m_rot = ToRadians(v);
        } else {
            return;
        }
        AskWhere();
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Where) return;
        m_at = p;
        m_step = Values;
        AskValue();
    }
    void OnEnter() override {
        if (m_step == Name) {
            const auto last = E->Doc().FindBlock(E->lastBlock);
            return Choose(last ? last : E->Doc().Blocks().back());
        }
        if (m_step == Values) {
            m_values.push_back(m_defs[m_values.size()]->content);
            return AskValue();
        }
        if (m_step == Scale || m_step == Rotate) return AskWhere();
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Where || !m_b) return;
        std::vector<std::wstring> v;
        for (const TextData* t : m_defs) v.push_back(t->content);
        out.entities.push_back(MakeInsert(m_b, c, m_scale, m_rot, v));
    }
};

class AttDefCmd : public Command {
    enum Step { Tag, Question, Default, Where } m_step = Tag;
    TextData m_t;

public:
    void Start() override {
        m_t = E->lastText;
        m_t.content.clear();
        E->Ask(P(L"Enter attribute tag (no spaces)", kAcceptEmpty));
    }
    bool OnText(const std::wstring& s) override {
        if (m_step == Tag) {
            if (s.find(L' ') != std::wstring::npos) {
                E->Msg(L"A tag has no spaces.");
                return true;
            }
            m_t.tag = s;
            m_step = Question;
            E->Ask(P(L"Enter the question asked when the block is placed", kAcceptEmpty, {}, s));
            return true;
        }
        if (m_step == Question) {
            m_t.prompt = s;
            m_step = Default;
            E->Ask(P(L"Enter default value", kAcceptEmpty));
            return true;
        }
        if (m_step == Default) {
            m_t.content = s;
            m_step = Where;
            E->Ask(P(L"Specify start point", kAcceptPoint));
            return true;
        }
        return false;
    }
    void OnEnter() override {
        if (m_step == Question) return (void)OnText(m_t.tag);
        if (m_step == Default) {
            m_step = Where;
            return E->Ask(P(L"Specify start point", kAcceptPoint));
        }
        E->Finish();
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Where) return;
        E->Doc().BeginChange(L"Attribute definition");
        E->Doc().Add(MakeText(p, m_t));
        E->Doc().EndChange();
        E->Msg(L"Put it in a block with BLOCK: each placed block asks for its value.");
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Where) out.entities.push_back(MakeText(c, m_t));
    }
};

class AttEditCmd : public Command {
    uint64_t m_id = 0;
    std::vector<const TextData*> m_defs;
    std::vector<std::wstring> m_values;
    size_t m_i = 0;

    void AskValue() {
        if (m_i >= m_defs.size()) {
            Entity e = *E->Doc().Find(m_id);
            EditAnnot(e).values = m_values;
            E->Doc().BeginChange(L"Edit attributes");
            E->Doc().Replace(e);
            E->Doc().EndChange();
            return E->Finish();
        }
        const TextData& t = *m_defs[m_i];
        Prompt p = P(L"", kAcceptEmpty, {}, m_values[m_i]);
        p.text = L"Enter " + (t.prompt.empty() ? t.tag : t.prompt);
        E->Ask(p);
    }

public:
    void Start() override { E->Ask(Pick(L"Select a block with attributes", kAcceptEmpty)); }
    void OnPoint(Vec2 p) override {
        if (m_id) return;
        const uint64_t id = E->PickEntity(p);
        const Entity* e = id ? E->Doc().Find(id) : nullptr;
        if (!e || e->type != EntityType::Insert || !e->annot || !e->annot->block) {
            E->Msg(L"Pick a block.");
            return;
        }
        m_defs = AttributeDefs(*e->annot->block);
        if (m_defs.empty()) {
            E->Msg(L"That block has no attributes.");
            return;
        }
        m_id = id;
        m_values = e->annot->values;
        m_values.resize(m_defs.size());
        for (size_t i = 0; i < m_defs.size(); ++i)
            if (i >= e->annot->values.size()) m_values[i] = m_defs[i]->content;
        AskValue();
    }
    bool OnText(const std::wstring& s) override {
        if (!m_id) return false;
        m_values[m_i++] = s;
        AskValue();
        return true;
    }
    void OnEnter() override {
        if (!m_id) return E->Finish();
        ++m_i;
        AskValue();
    }
};

class PurgeCmd : public Command {
public:
    void Start() override {
        std::vector<std::wstring> names;
        for (const auto& b : E->Doc().Blocks()) names.push_back(b->name);
        E->Doc().BeginChange(L"Purge");
        size_t n = 0;
        // Blocks used only by unused blocks go too: repeat until nothing changes.
        for (bool again = true; again;) {
            again = false;
            for (const std::wstring& name : names) {
                std::wstring why;
                if (E->Doc().FindBlock(name) && E->Doc().DeleteBlock(name, why)) ++n, again = true;
            }
        }
        E->Doc().EndChange();
        E->Msg(n ? std::to_wstring(n) + L" unused block(s) removed." : L"There are no unused blocks.");
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// DIMORDINATE: X or Y of a point from the origin
// DIMANGCOORD: angles of points about a centre, from a 0 degree direction
// ---------------------------------------------------------------------------
class OrdinateCmd : public Command {
    bool m_have = false;
    int m_axis = -1;  // -1: by the leader's direction, 0: X, 1: Y
    DimData m_d;

    DimData Make(Vec2 end) const {
        DimData d = m_d;
        int axis = m_axis;
        if (axis < 0) {
            const Vec2 v = end - d.pts[0];
            axis = std::fabs(v.y) >= std::fabs(v.x) ? 0 : 1;  // a leader up or down shows X
        }
        d.angle = axis == 0 ? 0 : kPi / 2;
        d.loc = end;
        return d;
    }

public:
    void Start() override {
        m_d.kind = DimData::Ordinate;
        E->Ask(P(L"Specify feature location", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        if (!m_have) {
            m_d.pts[0] = p;
            m_d.pts[1] = E->Doc().Settings().origin;
            m_d.refs[0] = FindRef(E->Doc(), p);
            m_d.style = E->Doc().Settings().dimStyle;
            m_have = true;
            E->Ask(From(P(L"Specify leader endpoint", kAcceptPoint, {{L"X", L"Xdatum"}, {L"Y", L"Ydatum"}}), p));
            return;
        }
        E->Doc().BeginChange(L"Ordinate dimension");
        E->Doc().Add(MakeDimension(Make(p)));
        E->Doc().EndChange();
        m_have = false;
        m_axis = -1;
        E->Ask(P(L"Specify next feature location", kAcceptPoint | kAcceptEmpty));
    }
    void OnKeyword(const std::wstring& k) override {
        m_axis = k == L"X" ? 0 : 1;
        E->Ask(From(P(L"Specify leader endpoint", kAcceptPoint, {{L"X", L"Xdatum"}, {L"Y", L"Ydatum"}}), m_d.pts[0]));
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_have) out.entities.push_back(MakeDimension(Make(c)));
    }
};

class AngCoordCmd : public Command {
    enum Step { Centre, Zero, Point, Arc } m_step = Centre;
    DimData m_d;

public:
    void Start() override {
        m_d.kind = DimData::AngularCoord;
        E->Ask(P(L"Specify centre (snap to a circle's centre)", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case Centre:
                m_d.pts[0] = p;
                m_d.refs[0] = FindRef(E->Doc(), p);
                m_d.style = E->Doc().Settings().dimStyle;
                m_step = Zero;
                E->Ask(From(P(L"Specify a point in the 0\x00B0 direction", kAcceptPoint), p));
                break;
            case Zero:
                if (Near(p, m_d.pts[0], 1e-12)) return;
                m_d.pts[1] = p;
                m_d.refs[1] = FindRef(E->Doc(), p);
                m_step = Point;
                E->Ask(P(L"Specify point to dimension", kAcceptPoint | kAcceptEmpty));
                break;
            case Point:
                if (Near(p, m_d.pts[0], 1e-12)) return;
                m_d.pts[2] = p;
                m_d.refs[2] = FindRef(E->Doc(), p);
                m_step = Arc;
                E->Ask(P(L"Specify dimension arc location", kAcceptPoint));
                break;
            case Arc: {
                DimData d = m_d;
                d.loc = p;
                E->Doc().BeginChange(L"Angular coordinate dimension");
                E->Doc().Add(MakeDimension(d));
                E->Doc().EndChange();
                m_step = Point;
                E->Ask(P(L"Specify next point to dimension", kAcceptPoint | kAcceptEmpty));
                break;
            }
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Arc) return;
        DimData d = m_d;
        d.loc = c;
        out.entities.push_back(MakeDimension(d));
    }
};

// ---------------------------------------------------------------------------
// DIMSPACE: parallel dimensions evenly apart
// DIMDRIVEN: dimensions that only report (or drive again)
// ---------------------------------------------------------------------------
class DimSpaceCmd : public Command {
    uint64_t m_base = 0;
    std::vector<uint64_t> m_ids;

    static bool Linear(const Entity* e) {
        return e && e->type == EntityType::Dimension && e->dim &&
               (e->dim->kind == DimData::Linear || e->dim->kind == DimData::Aligned);
    }

public:
    void Start() override { E->Ask(Pick(L"Select base dimension")); }
    void OnPoint(Vec2 p) override {
        if (m_base) return;
        const uint64_t id = E->PickEntity(p);
        if (!Linear(E->Doc().Find(id))) {
            E->Msg(L"Pick a linear or aligned dimension.");
            return;
        }
        m_base = id;
        E->ClearSelection();
        E->Msg(L"Select the dimensions to space.");
        E->AskSelection();
    }
    void OnSelected() override {
        m_ids = SelectionIds(E);
        E->ClearSelection();
        E->Ask(P(L"Enter value or [Auto]", kAcceptNumber | kAcceptEmpty, {{L"A", L"Auto"}}, L"Auto"));
    }
    void OnKeyword(const std::wstring&) override { Space(-1); }
    void OnEnter() override {
        if (!m_ids.empty()) return Space(-1);
        E->Finish();
    }
    void OnNumber(double v) override {
        if (v < 0) return E->Msg(L"The spacing cannot be negative.");
        Space(v);
    }
    void Space(double gap) {
        const Entity* be = E->Doc().Find(m_base);
        if (!Linear(be)) return E->Finish();
        const DimData& b = *be->dim;
        const Vec2 u = DimDirection(b), n = Perp(u);
        const double base = Dot(b.loc - b.pts[0], n);
        if (gap < 0) gap = 2 * b.style.textHeight + b.style.textGap;  // Auto: room for the text
        // Parallel ones, nearest first, on the base's side.
        std::vector<std::pair<double, Entity>> list;
        for (uint64_t id : m_ids) {
            const Entity* e = E->Doc().Find(id);
            if (id == m_base || !Linear(e) || std::fabs(Cross(DimDirection(*e->dim), u)) > 1e-6) continue;
            list.push_back({Dot(e->dim->loc - b.pts[0], n), *e});
        }
        const double side = base >= 0 ? 1 : -1;
        std::sort(list.begin(), list.end(), [&](const auto& x, const auto& y) { return x.first * side < y.first * side; });
        E->Doc().BeginChange(L"Dimension spacing");
        double at = base;  // spacing 0: all in line with the base
        for (auto& it : list) {
            at += side * gap;
            DimData d = *it.second.dim;
            d.loc += n * (at - it.first);
            it.second.dim = std::make_shared<const DimData>(d);
            E->Doc().Replace(it.second);
        }
        E->Doc().EndChange();
        E->Msg(std::to_wstring(list.size()) + L" dimension(s) spaced " + E->Len(gap) + L" apart.");
        E->Finish();
    }
};

class DimDrivenCmd : public Command {
public:
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        std::vector<Entity> dims;
        bool anyDriving = false;
        for (uint64_t id : SelectionIds(E))
            if (const Entity* e = E->Doc().Find(id); e && e->type == EntityType::Dimension && e->dim) {
                dims.push_back(*e);
                anyDriving |= !e->dim->driven;
            }
        if (dims.empty()) {
            E->Msg(L"No dimensions selected.");
            return E->Finish();
        }
        E->Doc().BeginChange(anyDriving ? L"Driven dimensions" : L"Driving dimensions");
        for (Entity& e : dims) {
            DimData d = *e.dim;
            d.driven = anyDriving;
            e.dim = std::make_shared<const DimData>(d);
            E->Doc().Replace(e);
        }
        E->Doc().EndChange();
        E->Msg(std::to_wstring(dims.size()) +
               (anyDriving ? L" dimension(s) now only report (driven, shown in brackets)." : L" dimension(s) drive the geometry again."));
        E->Finish();
    }
};

// ---------------------------------------------------------------------------
// REVCLOUD: a cloud of arcs around a change
// ---------------------------------------------------------------------------
// A closed cloud polyline along `loop` (corners kept when `corners`), with
// arcs about `arc` long, bulging outwards.
Entity Cloud(std::vector<Vec2> loop, double arc, bool corners) {
    if (LoopArea(loop) < 0) std::reverse(loop.begin(), loop.end());
    const double total = LoopLength(loop);
    arc = std::clamp(arc, total / 500, std::max(total / 8, 1e-9));
    const double bulge = std::tan(ToRadians(140) / 4);
    std::vector<Vertex> v;
    if (corners) {
        for (size_t i = 0; i < loop.size(); ++i) {
            const Vec2 a = loop[i], b = loop[(i + 1) % loop.size()];
            const int n = std::max(1, (int)std::lround(Dist(a, b) / arc));
            for (int k = 0; k < n; ++k) v.push_back({Lerp(a, b, (double)k / n), bulge});
        }
    } else {  // a curve: evenly along its length
        const int n = std::max(3, (int)std::lround(total / arc));
        const double step = total / n;
        size_t seg = 0;
        double segStart = 0;
        for (int k = 0; k < n; ++k) {
            const double want = k * step;
            while (seg < loop.size() && segStart + Dist(loop[seg], loop[(seg + 1) % loop.size()]) < want) {
                segStart += Dist(loop[seg], loop[(seg + 1) % loop.size()]);
                ++seg;
            }
            if (seg >= loop.size()) break;
            const Vec2 a = loop[seg], b = loop[(seg + 1) % loop.size()];
            const double len = Dist(a, b);
            v.push_back({len > 0 ? Lerp(a, b, (want - segStart) / len) : a, bulge});
        }
    }
    Entity e = Entity::Polyline(v, true);
    e.layer = -1;
    return e;
}

class RevCloudCmd : public Command {
    enum Step { First, Corner, Poly, ArcLen, Object } m_step = First;
    Vec2 m_first;
    std::vector<Vec2> m_pts;

    void AskFirst() {
        m_step = First;
        E->Ask(P(L"Specify first corner", kAcceptPoint, {{L"A", L"Arc length"}, {L"P", L"Polygonal"}, {L"O", L"Object"}}));
    }
    void Add(Entity e) {
        E->Doc().BeginChange(L"Revision cloud");
        E->Doc().Add(std::move(e));
        E->Doc().EndChange();
        E->Finish();
    }

public:
    void Start() override {
        E->Msg(L"Arc length " + E->Len(E->lastCloudArc) + L".");
        AskFirst();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"A") {
            m_step = ArcLen;
            E->Ask(P(L"Specify arc length", kAcceptNumber, {}, E->Len(E->lastCloudArc)));
        } else if (k == L"P") {
            m_step = Poly;
            m_pts.clear();
            E->Ask(P(L"Specify start point", kAcceptPoint));
        } else if (k == L"O") {
            m_step = Object;
            E->Ask(Pick(L"Select a closed object to turn into a cloud"));
        }
    }
    void OnNumber(double v) override {
        if (m_step != ArcLen || !Positive(E, v)) return;
        E->lastCloudArc = v;
        AskFirst();
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case First:
                m_first = p;
                m_step = Corner;
                E->Ask(From(P(L"Specify opposite corner", kAcceptPoint), p));
                break;
            case Corner:
                if (std::fabs(p.x - m_first.x) < 1e-12 || std::fabs(p.y - m_first.y) < 1e-12) return;
                Add(Cloud({m_first, {p.x, m_first.y}, p, {m_first.x, p.y}}, E->lastCloudArc, true));
                break;
            case Poly:
                m_pts.push_back(p);
                E->Ask(From(P(m_pts.size() < 3 ? L"Specify next point" : L"Specify next point (Enter closes the cloud)",
                              kAcceptPoint | (m_pts.size() >= 3 ? (unsigned)kAcceptEmpty : 0u)),
                            p));
                break;
            case Object: {
                const uint64_t id = E->PickEntity(p);
                const Entity* e = id ? E->Doc().Find(id) : nullptr;
                std::vector<Vec2> loop;
                if (!e || !ClosedOutline(*e, loop)) {
                    E->Msg(L"Pick a circle, closed polyline, ellipse or closed spline.");
                    return;
                }
                const bool corners = e->type == EntityType::Polyline &&
                                     std::all_of(e->verts.begin(), e->verts.end(), [](const Vertex& v) { return v.bulge == 0; });
                if (corners) {
                    loop.clear();
                    for (const Vertex& v : e->verts) loop.push_back(v.p);
                }
                Entity c = Cloud(loop, E->lastCloudArc, corners);
                c.id = id;
                CopyStyle(*e, c);
                E->Doc().BeginChange(L"Revision cloud");
                E->Doc().Replace(c);
                E->Doc().EndChange();
                E->Finish();
                break;
            }
            default:
                break;
        }
    }
    void OnEnter() override {
        if (m_step == Poly && m_pts.size() >= 3) return Add(Cloud(m_pts, E->lastCloudArc, true));
        if (m_step == ArcLen) return AskFirst();
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Corner && std::fabs(c.x - m_first.x) > 1e-9 && std::fabs(c.y - m_first.y) > 1e-9)
            out.entities.push_back(Cloud({m_first, {c.x, m_first.y}, c, {m_first.x, c.y}}, E->lastCloudArc, true));
        if (m_step == Poly && !m_pts.empty()) {
            std::vector<Vec2> pts = m_pts;
            pts.push_back(c);
            if (pts.size() >= 3) out.entities.push_back(Cloud(pts, E->lastCloudArc, true));
            else out.entities.push_back(Entity::Line(pts[0], c));
        }
    }
};

// ---------------------------------------------------------------------------
// AREA: area and perimeter of points, an object or the area around a point
// ---------------------------------------------------------------------------
class AreaCmd : public Command {
    enum Step { First, Points, Object, Inside } m_step = First;
    int m_mode = 0;  // 0: one at a time, 1: adding, -1: taking away
    double m_total = 0;
    std::vector<Vec2> m_pts;

    void AskFirst() {
        m_step = First;
        E->Ask(P(m_mode == 0 ? L"Specify first corner point" : m_mode > 0 ? L"(Adding) Specify first corner point"
                                                                           : L"(Taking away) Specify first corner point",
                 kAcceptPoint | kAcceptEmpty,
                 {{L"O", L"Object"}, {L"I", L"Inside"}, {L"A", L"Add area"}, {L"S", L"Subtract area"}}));
    }
    void Report(double area, double perimeter) {
        const std::wstring u = AreaUnits(E);
        std::wstring line = L"Area = " + E->Len(area) + (u.empty() ? L"" : u + L"\x00B2") + L", Perimeter = " + E->Len(perimeter) + u;
        if (m_mode != 0) {
            m_total += m_mode * area;
            line += L"   Total area = " + E->Len(m_total) + (u.empty() ? L"" : u + L"\x00B2");
        }
        E->Msg(line);
        if (m_mode == 0) return E->Finish();
        // Adding or taking away: the same way again until Enter.
        if (m_step == Object) E->Ask(Pick(m_mode > 0 ? L"(Adding) Select a closed object" : L"(Taking away) Select a closed object", kAcceptEmpty));
        else if (m_step == Inside) E->Ask(P(m_mode > 0 ? L"(Adding) Pick a point inside an area" : L"(Taking away) Pick a point inside an area", kAcceptPoint | kAcceptEmpty));
        else AskFirst();
    }

public:
    void Start() override { AskFirst(); }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"O") {
            m_step = Object;
            E->Ask(Pick(L"Select a closed object"));
        } else if (k == L"I") {
            m_step = Inside;
            E->Ask(P(L"Pick a point inside an area", kAcceptPoint));
        } else if (k == L"A" || k == L"S") {
            m_mode = k == L"A" ? 1 : -1;
            AskFirst();
        }
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case First:
            case Points:
                m_step = Points;
                m_pts.push_back(p);
                E->Ask(From(P(L"Specify next point (Enter: done)", kAcceptPoint | kAcceptEmpty), p));
                break;
            case Object: {
                const uint64_t id = E->PickEntity(p, true);
                const Entity* e = id ? E->Doc().Find(id) : nullptr;
                if (e && e->type == EntityType::Hatch && e->annot) {
                    double per = 0;
                    for (const auto& l : e->annot->hatch.loops) per += LoopLength(l);
                    return Report(HatchArea(e->annot->hatch), per);
                }
                std::vector<Vec2> loop;
                if (!e || !ClosedOutline(*e, loop)) {
                    E->Msg(L"Pick a closed object (circle, closed polyline, ellipse, spline or hatch).");
                    return;
                }
                double area = std::fabs(LoopArea(loop));
                if (e->type == EntityType::Circle) area = kPi * e->radius * e->radius;
                return Report(area, LengthOf(*e));
            }
            case Inside: {
                std::vector<std::vector<Vec2>> loops;
                double area = 0, per = 0;
                if (!FindArea(AreaObjects(E), p, loops, nullptr, &area, &per)) {
                    E->Msg(L"There is no closed area around that point.");
                    return;
                }
                return Report(area, per);
            }
        }
    }
    void OnEnter() override {
        if (m_step == Points && m_pts.size() >= 3) {
            const std::vector<Vec2> pts = m_pts;
            m_pts.clear();
            return Report(std::fabs(LoopArea(pts)), LoopLength(pts));
        }
        if (m_step == Points) {
            m_pts.clear();
            E->Msg(L"An area needs at least three points.");
            return AskFirst();
        }
        if (m_mode != 0 && (m_step == Object || m_step == Inside)) return AskFirst();
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Points || m_pts.empty()) return;
        std::vector<Vertex> v;
        for (Vec2 p : m_pts) v.push_back({p, 0});
        v.push_back({c, 0});
        out.entities.push_back(Entity::Polyline(v, true));
    }
};
}  // namespace

std::unique_ptr<Command> CreateAnnotCommand(const std::wstring& name) {
    if (name == L"TEXT") return std::make_unique<TextCmd>();
    if (name == L"TEXTEDIT") return std::make_unique<TextEditCmd>();
    if (name == L"LEADER") return std::make_unique<LeaderCmd>(0);
    if (name == L"BALLOON") return std::make_unique<LeaderCmd>(2);
    if (name == L"CENTERMARK") return std::make_unique<CentreMarkCmd>();
    if (name == L"CENTERLINE") return std::make_unique<CentreLineCmd>();
    if (name == L"HATCH") return std::make_unique<HatchCmd>(false);
    if (name == L"FILL") return std::make_unique<HatchCmd>(true);
    if (name == L"BLOCK") return std::make_unique<BlockCmd>();
    if (name == L"INSERT") return std::make_unique<InsertCmd>();
    if (name == L"ATTDEF") return std::make_unique<AttDefCmd>();
    if (name == L"ATTEDIT") return std::make_unique<AttEditCmd>();
    if (name == L"PURGE") return std::make_unique<PurgeCmd>();
    if (name == L"DIMORDINATE") return std::make_unique<OrdinateCmd>();
    if (name == L"DIMANGCOORD") return std::make_unique<AngCoordCmd>();
    if (name == L"DIMSPACE") return std::make_unique<DimSpaceCmd>();
    if (name == L"DIMDRIVEN") return std::make_unique<DimDrivenCmd>();
    if (name == L"REVCLOUD") return std::make_unique<RevCloudCmd>();
    if (name == L"AREA") return std::make_unique<AreaCmd>();
    return nullptr;
}

// ---------------------------------------------------------------------------
// Sheets: MVIEW, VPSCALE, VPPAN, VPLOCK, TITLEBLOCK
// ---------------------------------------------------------------------------
namespace {
// "1:2", "2:1", "1/2" or a plain ratio (0.5).
bool ParseScale(const std::wstring& s, double& ratio) {
    const size_t sep = s.find_first_of(L":/");
    auto num = [](const std::wstring& t, double& v) { return ParseNumber(TextToUtf8(t), v); };
    double a, b;
    if (sep == std::wstring::npos) {
        if (!num(s, a) || a <= 0) return false;
        ratio = a;
        return true;
    }
    if (!num(s.substr(0, sep), a) || !num(s.substr(sep + 1), b) || a <= 0 || b <= 0) return false;
    ratio = a / b;
    return true;
}

std::wstring Up(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)std::towupper(c);
    return s;
}

bool OnSheet(Engine* E) {
    if (E->model) return true;
    E->Msg(L"Viewports belong on sheets: click a sheet tab at the bottom of the drawing (or + for a new sheet) first.");
    E->Finish();
    return false;
}

// The viewport picked at p (the topmost), or nullptr.
const Entity* PickedViewport(Engine* E, Vec2 p) {
    const uint64_t id = E->PickEntity(p);
    const Entity* e = id ? E->Doc().Find(id) : nullptr;
    if (e && e->type == EntityType::Viewport && e->annot) return e;
    // Inside one also counts.
    for (auto it = E->Doc().Entities().rbegin(); it != E->Doc().Entities().rend(); ++it)
        if (it->second.type == EntityType::Viewport && Box(it->second.p0, it->second.p1).Contains(p)) return &it->second;
    return nullptr;
}

const wchar_t* const kScaleHelp = L"Specify scale (1:2, 2:1 or a number) or [Fit]";

class MViewCmd : public Command {
    enum Step { First, Second, Scale } m_step = First;
    Vec2 m_a, m_b;

    void Make(double ratio) {
        const Box ext = E->model->Extents();
        const double mm = MmPerUnit(E->model->Settings().units);
        if (ratio <= 0) ratio = FitScale(ext, std::fabs(m_b.x - m_a.x), std::fabs(m_b.y - m_a.y), E->model->Settings().units);
        Entity vp = MakeViewport(m_a, m_b, ext.Empty() ? Vec2{} : ext.Center(), ratio * mm);
        vp.layer = -1;
        E->Doc().BeginChange(L"Viewport");
        E->Doc().Add(vp);
        E->Doc().EndChange();
        E->Msg(L"Viewport at " + ScaleText(ratio) + L". VPSCALE changes the scale, VPPAN moves the drawing in it.");
        E->Finish();
    }

public:
    void Start() override {
        if (!OnSheet(E)) return;
        E->Ask(P(L"Specify first corner of viewport", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        if (m_step == First) {
            m_a = p;
            m_step = Second;
            E->Ask(From(P(L"Specify opposite corner", kAcceptPoint), p));
        } else if (m_step == Second) {
            if (std::fabs(p.x - m_a.x) < 1 || std::fabs(p.y - m_a.y) < 1) return E->Msg(L"Make it at least 1 mm each way.");
            m_b = p;
            m_step = Scale;
            E->Ask(P(kScaleHelp, kAcceptEmpty, {{L"F", L"Fit"}}, L"Fit"));
        }
    }
    bool OnText(const std::wstring& s) override {
        if (m_step != Scale) return false;
        double r;
        if (Up(s) == L"F" || Up(s) == L"FIT") return Make(0), true;
        if (!ParseScale(s, r)) return E->Msg(L"Type a scale such as 1:2, 1:50 or 2:1."), true;
        Make(r);
        return true;
    }
    void OnKeyword(const std::wstring&) override { Make(0); }
    void OnEnter() override {
        if (m_step == Scale) return Make(0);
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Second) out.entities.push_back(Entity::Rectangle(m_a, c));
    }
};

class VpScaleCmd : public Command {
    uint64_t m_id = 0;

    void Apply(double ratio) {
        Entity e = *E->Doc().Find(m_id);
        Annot& a = EditAnnot(e);
        if (ratio <= 0) {
            const Box ext = E->model->Extents();
            ratio = FitScale(ext, e.p1.x - e.p0.x, e.p1.y - e.p0.y, E->model->Settings().units);
            if (!ext.Empty()) a.viewCentre = ext.Center();
        }
        a.viewScale = ratio * MmPerUnit(E->model->Settings().units);
        E->Doc().BeginChange(L"Viewport scale");
        E->Doc().Replace(e);
        E->Doc().EndChange();
        E->Msg(L"Viewport scale " + ScaleText(ratio) + L".");
        E->Finish();
    }

public:
    void Start() override {
        if (!OnSheet(E)) return;
        E->Ask(Pick(L"Select a viewport"));
    }
    void OnPoint(Vec2 p) override {
        if (m_id) return;
        const Entity* vp = PickedViewport(E, p);
        if (!vp) return E->Msg(L"Pick a viewport's edge (or click inside it).");
        if (vp->annot->viewLocked) return E->Msg(L"That viewport is locked (VPLOCK unlocks it).");
        m_id = vp->id;
        Prompt pr = P(kScaleHelp, kAcceptEmpty, {{L"F", L"Fit"}}, ScaleText(ViewportRatio(*vp, *E->model)));
        E->Ask(pr);
    }
    bool OnText(const std::wstring& s) override {
        if (!m_id) return false;
        double r;
        if (Up(s) == L"F" || Up(s) == L"FIT") return Apply(0), true;
        if (!ParseScale(s, r)) return E->Msg(L"Type a scale such as 1:2, 1:50 or 2:1."), true;
        Apply(r);
        return true;
    }
    void OnKeyword(const std::wstring&) override { Apply(0); }
};

class VpPanCmd : public Command {
    uint64_t m_id = 0;
    bool m_have = false;
    Vec2 m_from;

public:
    void Start() override {
        if (!OnSheet(E)) return;
        E->Ask(Pick(L"Select a viewport"));
    }
    void OnPoint(Vec2 p) override {
        if (!m_id) {
            const Entity* vp = PickedViewport(E, p);
            if (!vp) return E->Msg(L"Pick a viewport's edge (or click inside it).");
            if (vp->annot->viewLocked) return E->Msg(L"That viewport is locked (VPLOCK unlocks it).");
            m_id = vp->id;
            E->Ask(P(L"Specify base point", kAcceptPoint));
            return;
        }
        if (!m_have) {
            m_from = p;
            m_have = true;
            E->Ask(From(P(L"Specify where it goes", kAcceptPoint), p));
            return;
        }
        Entity e = *E->Doc().Find(m_id);
        Annot& a = EditAnnot(e);
        a.viewCentre -= (p - m_from) / a.viewScale;
        E->Doc().BeginChange(L"Pan viewport");
        E->Doc().Replace(e);
        E->Doc().EndChange();
        E->Finish();
    }
};

class VpLockCmd : public Command {
public:
    void Start() override {
        if (!OnSheet(E)) return;
        E->AskSelection();
    }
    void OnSelected() override {
        std::vector<Entity> vps;
        bool anyOpen = false;
        for (uint64_t id : SelectionIds(E))
            if (const Entity* e = E->Doc().Find(id); e && e->type == EntityType::Viewport && e->annot) {
                vps.push_back(*e);
                anyOpen |= !e->annot->viewLocked;
            }
        if (vps.empty()) {
            E->Msg(L"No viewports selected.");
            return E->Finish();
        }
        E->Doc().BeginChange(anyOpen ? L"Lock viewports" : L"Unlock viewports");
        for (Entity& e : vps) {
            EditAnnot(e).viewLocked = anyOpen;
            E->Doc().Replace(e);
        }
        E->Doc().EndChange();
        E->Msg(std::to_wstring(vps.size()) + (anyOpen ? L" viewport(s) locked: their scale and view stay as they are."
                                                       : L" viewport(s) unlocked."));
        E->Finish();
    }
};
}  // namespace

std::unique_ptr<Command> CreateSheetCommand(const std::wstring& name) {
    if (name == L"MVIEW") return std::make_unique<MViewCmd>();
    if (name == L"VPSCALE") return std::make_unique<VpScaleCmd>();
    if (name == L"VPPAN") return std::make_unique<VpPanCmd>();
    if (name == L"VPLOCK") return std::make_unique<VpLockCmd>();
    return nullptr;
}
