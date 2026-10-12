// CommandsDim.cpp - dimensions and the clipboard.
//
//   DIM          smart dimension: click a line, a circle, an arc or two
//                lines and the right dimension follows the cursor
//   DIMLINEAR, DIMALIGNED, DIMRADIUS, DIMDIAMETER, DIMANGULAR
//   DIMCONTINUE, DIMBASELINE   chains and stacks from the last dimension
//   DIMEDIT      a new value (the geometry changes to it) or new text
//   COPYCLIP, CUTCLIP, PASTECLIP   copy and paste, also between drawings
//
// Typing a number while placing a dimension makes the object that size
// (driving dimensions), as in Solid Edge.
#include <algorithm>

#include "CommandUtil.h"
#include "Dimension.h"

namespace {
// What a click on an object offers a dimension.
struct Picked {
    enum Kind { None, Segment, Circle, Arc } kind = None;
    uint64_t id = 0;
    Vec2 a, b;          // Segment: its ends
    DimRef ra, rb;
    Vec2 c;             // Circle, Arc: centre and radius
    double r = 0;
    DimRef rc;          // the circle as a whole (radius, diameter)
    double a0 = 0, a1 = 0;  // Arc: start and end angle, counter-clockwise
    DimRef rStart, rEnd, rCentre;
};

Picked PickFor(Engine* E, Vec2 p) {
    Picked k;
    const uint64_t id = E->PickEntity(p, true);  // locked layers can be measured
    const Entity* e = id ? E->Doc().Find(id) : nullptr;
    if (!e) return k;
    k.id = id;
    switch (e->type) {
        case EntityType::Line:
            k.kind = Picked::Segment;
            k.a = e->p0;
            k.b = e->p1;
            k.ra = {id, 0};
            k.rb = {id, 1};
            break;
        case EntityType::Circle:
            k.kind = Picked::Circle;
            k.c = e->p0;
            k.r = e->radius;
            k.rc = {id, -1};
            break;
        case EntityType::Arc:
            k.kind = Picked::Arc;
            k.c = e->p0;
            k.r = e->radius;
            k.rc = {id, -1};
            k.a0 = e->startAngle;
            k.a1 = e->endAngle;
            k.rCentre = {id, 0};
            k.rStart = {id, 1};
            k.rEnd = {id, 2};
            break;
        case EntityType::Polyline: {
            std::vector<Prim> prims;
            Primitives(*e, prims);
            size_t best = 0;
            double bestDist = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < prims.size(); ++i) {
                const double d = Distance(prims[i], p);
                if (d < bestDist) {
                    bestDist = d;
                    best = i;
                }
            }
            if (prims.empty() || e->verts.size() < 2) break;
            const int n = (int)e->verts.size();
            const int i = (int)best, j = (i + 1) % n;
            const Prim& pr = prims[best];
            if (pr.kind == Prim::Segment) {
                k.kind = Picked::Segment;
                k.a = e->verts[(size_t)i].p;
                k.b = e->verts[(size_t)j].p;
                k.ra = {id, i};
                k.rb = {id, j};
            } else {
                k.kind = Picked::Arc;
                k.c = pr.c;
                k.r = pr.r;
                k.rc = {id, -1 - i};
                k.a0 = pr.start;
                k.a1 = pr.start + pr.sweep;
                // The counter-clockwise arc starts at the second vertex when the bulge is negative.
                const bool ccw = e->verts[(size_t)i].bulge > 0;
                k.rStart = {id, ccw ? i : j};
                k.rEnd = {id, ccw ? j : i};
            }
            break;
        }
        default:
            break;
    }
    return k;
}

double MeasureOf(const DimData& d) { return DimMeasure(d); }

// ---------------------------------------------------------------------------
// DIM and the single-kind dimension commands
// ---------------------------------------------------------------------------
enum class DimMode { Smart, Linear, Aligned, Radius, Diameter, Angular };

class DimCmd : public Command {
    const DimMode m_start;  // the command it was started as
    DimMode m_mode;
    enum Step { PickObj, First, Second, Place, SecondLine, Vertex3, End1, End2, TextEntry } m_step = PickObj;
    enum Orient { Auto, Horizontal, Vertical, AlignedO, Fixed } m_orient = Auto;  // Fixed: as m_d is
    DimData m_d;
    bool m_fromLine = false;  // smart: a picked line (a second one makes an angle)
    Picked m_line;

    void Reset() {
        m_d = DimData{};
        m_d.style = E->Doc().Settings().dimStyle;
        m_orient = Auto;
        m_fromLine = false;
        m_mode = m_start;
    }
    void AskStart() {
        Reset();
        switch (m_mode) {
            case DimMode::Smart:
                E->Ask(Pick(L"Select a line, arc or circle to dimension", kAcceptEmpty,
                            {{L"P", L"Points"}, {L"A", L"Angle"}}));
                m_step = PickObj;
                break;
            case DimMode::Linear:
            case DimMode::Aligned:
                E->Ask(P(L"Specify first extension line origin", kAcceptPoint | kAcceptEmpty, {}, L"select object"));
                m_step = First;
                break;
            case DimMode::Radius:
            case DimMode::Diameter:
                E->Ask(Pick(L"Select arc or circle"));
                m_step = PickObj;
                break;
            case DimMode::Angular:
                E->Ask(Pick(L"Select an arc or the first line", kAcceptEmpty, {}, L"specify vertex"));
                m_step = PickObj;
                break;
        }
    }
    bool LinearFamily() const { return m_d.kind == DimData::Linear || m_d.kind == DimData::Aligned; }
    void AskPlace() {
        std::vector<Keyword> kw;
        if (LinearFamily()) {
            kw = {{L"H", L"Horizontal"}, {L"V", L"Vertical"}, {L"A", L"Aligned"}};
        } else if (m_d.kind == DimData::Radius) {
            kw = {{L"D", L"Diameter"}};
        } else if (m_d.kind == DimData::Diameter) {
            kw = {{L"R", L"Radius"}};
        }
        kw.push_back({L"T", L"Text"});
        const wchar_t* what = m_d.kind == DimData::Angular ? L"Specify dimension arc location, or type the angle it should be"
                                                           : L"Specify dimension line location, or type the size it should be";
        Prompt p = Pick(what, kAcceptNumber, kw);
        E->Ask(p);
        m_step = Place;
    }

    Orient AutoOrient(Vec2 c) const {
        if (m_mode == DimMode::Aligned) return AlignedO;
        const Vec2 p0 = m_d.pts[0], p1 = m_d.pts[1];
        const double tol = 1e-9 * (1 + Length(p0) + Length(p1));
        const bool flatX = std::fabs(p1.y - p0.y) <= tol, flatY = std::fabs(p1.x - p0.x) <= tol;
        if (m_mode == DimMode::Smart && m_fromLine) {
            if (flatX) return Horizontal;
            if (flatY) return Vertical;
        }
        if (m_mode == DimMode::Smart && !flatX && !flatY) {
            // Beside a slanted line: along it.
            const Vec2 u = Normalized(p1 - p0);
            const double t = Dot(c - p0, u);
            if (t >= 0 && t <= Dist(p0, p1)) return AlignedO;
        }
        const Box b(p0, p1);
        const double dx = c.x < b.minX ? b.minX - c.x : c.x > b.maxX ? c.x - b.maxX : 0;
        const double dy = c.y < b.minY ? b.minY - c.y : c.y > b.maxY ? c.y - b.maxY : 0;
        Orient o = dy > dx ? Horizontal : dx > dy ? Vertical : (b.Width() >= b.Height() ? Horizontal : Vertical);
        // Never a dimension that measures nothing.
        if (o == Horizontal && flatY) o = Vertical;
        else if (o == Vertical && flatX) o = Horizontal;
        return o;
    }
    DimData Shaped(Vec2 c) const {
        DimData d = m_d;
        d.loc = c;
        if (LinearFamily() && m_orient != Fixed) {
            const Orient o = m_orient == Auto ? AutoOrient(c) : m_orient;
            if (o == AlignedO && !Near(d.pts[0], d.pts[1], 1e-12)) {
                d.kind = DimData::Aligned;
            } else {
                d.kind = DimData::Linear;
                d.angle = o == Vertical ? kPi / 2 : 0;
            }
        }
        return d;
    }

    void Made(uint64_t id, const DimData& d) {
        if (d.kind == DimData::Linear || d.kind == DimData::Aligned) E->lastDim = id;
        E->Msg(std::wstring(DimKindName(d.kind)) + L" dimension: " + DimDisplayText(d));
    }
    void Next() {
        if (m_start == DimMode::Smart) AskStart();
        else E->Finish();
    }
    bool PlaceAt(Vec2 c) {
        const DimData d = Shaped(c);
        if (MeasureOf(d) <= 1e-12) {
            E->Msg(L"That dimension would measure nothing. Place it elsewhere or choose another direction.");
            return false;
        }
        E->Doc().BeginChange(L"Dimension");
        const uint64_t id = E->Doc().Add(MakeDimension(d));
        E->Doc().EndChange();
        Made(id, d);
        Next();
        return true;
    }
    // Driving: the dimension is placed where the cursor is, and the
    // geometry changes to the value typed.
    void PlaceWithValue(double value) {
        DimData d = Shaped(E->Host().TrackedCursor());
        if (MeasureOf(d) <= 1e-12) {
            E->Msg(L"Move the cursor to where the dimension should go, then type the value.");
            return;
        }
        if (d.kind == DimData::Angular) value = ToRadians(value);
        E->Doc().BeginChange(L"Dimension");
        const uint64_t id = E->Doc().Add(MakeDimension(d));
        std::wstring why;
        const bool driven = DriveDimension(E->Doc(), id, value, why);
        E->Doc().EndChange();
        const Entity* now = E->Doc().Find(id);
        if (now) Made(id, *now->dim);
        if (!driven) E->Msg(why);
        Next();
    }

    void FromSegment(const Picked& k) {
        m_d.kind = DimData::Aligned;
        m_d.pts[0] = k.a;
        m_d.pts[1] = k.b;
        m_d.refs[0] = k.ra;
        m_d.refs[1] = k.rb;
        m_fromLine = true;
        m_line = k;
        AskPlace();
    }
    void FromCircle(const Picked& k, bool diameter) {
        m_d.kind = diameter ? DimData::Diameter : DimData::Radius;
        m_d.pts[0] = k.c;
        const double mid = k.kind == Picked::Arc ? k.a0 + SweepCCW(k.a0, k.a1) / 2 : kPi / 4;
        m_d.pts[1] = Polar(k.c, k.r, mid);
        m_d.refs[0] = k.rc;
        AskPlace();
    }
    void FromArcAngle(const Picked& k) {
        m_d.kind = DimData::Angular;
        m_d.rays = true;
        m_d.pts = {k.c, Polar(k.c, k.r, k.a0), k.c, Polar(k.c, k.r, k.a1)};
        m_d.refs = {k.rCentre, k.rStart, k.rCentre, k.rEnd};
        AskPlace();
    }
    bool Parallel(Vec2 a, Vec2 b, Vec2 c, Vec2 d) const {
        const Vec2 u = b - a, v = d - c;
        return std::fabs(Cross(u, v)) <= 1e-9 * Length(u) * Length(v);
    }
    // Distance between two parallel lines: measured across them, from an end
    // of each (so it stays attached to both).
    void Across(const Picked& first, const Picked& second) {
        const Vec2 along = Normalized(first.b - first.a);
        const Vec2 ends1[2] = {first.a, first.b}, ends2[2] = {second.a, second.b};
        const DimRef refs1[2] = {first.ra, first.rb}, refs2[2] = {second.ra, second.rb};
        int bi = 0, bj = 0;
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                const double off = std::fabs(Dot(ends2[j] - ends1[i], along));
                if (off < best - 1e-12) best = off, bi = i, bj = j;
            }
        m_d.kind = DimData::Linear;
        m_d.angle = NormalizeAngle(AngleOf(Perp(along)));
        m_d.pts[0] = ends1[bi];
        m_d.pts[1] = ends2[bj];
        m_d.refs[0] = refs1[bi];
        m_d.refs[1] = refs2[bj];
        m_orient = Fixed;
        m_fromLine = false;
        E->Msg(L"Distance between the parallel lines: " + E->Len(DimMeasure(m_d)) + L".");
        AskPlace();
    }
    void TwoLines(const Picked& first, const Picked& second) {
        m_d = DimData{};
        m_d.style = E->Doc().Settings().dimStyle;
        m_d.kind = DimData::Angular;
        m_d.pts = {first.a, first.b, second.a, second.b};
        m_d.refs = {first.ra, first.rb, second.ra, second.rb};
        m_fromLine = false;
        AskPlace();
    }

public:
    explicit DimCmd(DimMode mode) : m_start(mode), m_mode(mode) {}
    void Start() override { AskStart(); }

    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case PickObj: {
                const Picked k = PickFor(E, p);
                if (k.kind == Picked::None) {
                    E->Msg(m_mode == DimMode::Smart ? L"Nothing there to dimension. Click a line, arc or circle, or type P "
                                                      L"to dimension between two points."
                                                    : L"Nothing there to dimension.");
                    return;
                }
                switch (m_mode) {
                    case DimMode::Smart:
                        if (k.kind == Picked::Segment) FromSegment(k);
                        else FromCircle(k, k.kind == Picked::Circle);
                        return;
                    case DimMode::Linear:
                    case DimMode::Aligned:
                        if (k.kind == Picked::Segment) FromSegment(k);
                        else E->Msg(L"Pick a line or a straight polyline segment.");
                        return;
                    case DimMode::Radius:
                    case DimMode::Diameter:
                        if (k.kind == Picked::Segment) E->Msg(L"Pick an arc or a circle.");
                        else FromCircle(k, m_mode == DimMode::Diameter);
                        return;
                    case DimMode::Angular:
                        if (k.kind == Picked::Segment) {
                            m_line = k;
                            E->Ask(Pick(L"Select second line"));
                            m_step = SecondLine;
                        } else if (k.kind == Picked::Arc) {
                            FromArcAngle(k);
                        } else {
                            E->Msg(L"Pick an arc or a line (press Enter to pick a vertex and two points).");
                        }
                        return;
                }
                return;
            }
            case SecondLine: {
                const Picked k = PickFor(E, p);
                if (k.kind != Picked::Segment) {
                    E->Msg(L"Pick a line or a straight polyline segment.");
                } else if (Parallel(m_line.a, m_line.b, k.a, k.b)) {
                    E->Msg(L"The lines are parallel: they make no angle.");
                } else {
                    TwoLines(m_line, k);
                }
                return;
            }
            case First:
                m_d.pts[0] = p;
                m_d.refs[0] = FindRef(E->Doc(), p);
                E->Ask(From(P(L"Specify second extension line origin", kAcceptPoint), p));
                m_step = Second;
                return;
            case Second:
                if (Near(p, m_d.pts[0], 1e-12)) {
                    E->Msg(L"That is the first point.");
                    return;
                }
                m_d.kind = DimData::Aligned;
                m_d.pts[1] = p;
                m_d.refs[1] = FindRef(E->Doc(), p);
                AskPlace();
                return;
            case Vertex3:
                m_d.kind = DimData::Angular;
                m_d.rays = true;
                m_d.pts[0] = m_d.pts[2] = p;
                m_d.refs[0] = m_d.refs[2] = FindRef(E->Doc(), p);
                E->Ask(From(P(L"Specify first angle endpoint", kAcceptPoint), p));
                m_step = End1;
                return;
            case End1:
                if (Near(p, m_d.pts[0], 1e-12)) return;
                m_d.pts[1] = p;
                m_d.refs[1] = FindRef(E->Doc(), p);
                E->Ask(From(P(L"Specify second angle endpoint", kAcceptPoint), m_d.pts[0]));
                m_step = End2;
                return;
            case End2:
                if (Near(p, m_d.pts[0], 1e-12)) return;
                m_d.pts[3] = p;
                m_d.refs[3] = FindRef(E->Doc(), p);
                AskPlace();
                return;
            case Place: {
                // A second line clicked instead of a place: the distance across
                // to it when parallel, the angle between them otherwise (smart).
                if (m_fromLine) {
                    const Picked k = PickFor(E, p);
                    const bool same = k.kind == Picked::Segment && Near(k.a, m_line.a, 1e-9) && Near(k.b, m_line.b, 1e-9);
                    if (k.kind == Picked::Segment && !same) {
                        const bool parallel = Parallel(m_line.a, m_line.b, k.a, k.b);
                        const double gap = std::fabs(Cross(Normalized(m_line.b - m_line.a), k.a - m_line.a));
                        if (parallel && gap > 1e-9) {
                            Across(m_line, k);
                            return;
                        }
                        if (!parallel && m_start == DimMode::Smart) {
                            TwoLines(m_line, k);
                            return;
                        }
                    }
                }
                PlaceAt(p);
                return;
            }
            default:
                return;
        }
    }
    void OnNumber(double v) override {
        if (m_step == Place) PlaceWithValue(v);
    }
    bool OnText(const std::wstring& t) override {
        if (m_step != TextEntry) return false;
        m_d.text = t;
        AskPlace();
        return true;
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == PickObj && m_mode == DimMode::Smart) {
            if (k == L"P") {
                E->Ask(P(L"Specify first extension line origin", kAcceptPoint));
                m_step = First;
            } else if (k == L"A") {
                m_mode = DimMode::Angular;
                E->Ask(Pick(L"Select an arc or the first line", kAcceptEmpty, {}, L"specify vertex"));
            }
            return;
        }
        if (m_step != Place) return;
        if (k == L"H") m_orient = Horizontal;
        else if (k == L"V") m_orient = Vertical;
        else if (k == L"A") m_orient = AlignedO;
        else if (k == L"R") m_d.kind = DimData::Radius;
        else if (k == L"D") m_d.kind = DimData::Diameter;
        else if (k == L"T") {
            E->Ask(TextPrompt(L"Enter dimension text (<> stands for the measured value)", 0, L"measured value"));
            m_step = TextEntry;
            return;
        }
        AskPlace();
    }
    void OnEnter() override {
        switch (m_step) {
            case PickObj:
                if (m_mode == DimMode::Angular) {
                    E->Ask(P(L"Specify angle vertex", kAcceptPoint));
                    m_step = Vertex3;
                } else {
                    E->Finish();
                }
                return;
            case First:
                E->Ask(Pick(L"Select a line to dimension"));
                m_step = PickObj;
                return;
            case TextEntry:
                m_d.text.clear();
                AskPlace();
                return;
            case Place:
                if (m_start == DimMode::Smart) AskStart();
                else E->Finish();
                return;
            default:
                E->Finish();
                return;
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Place) {
            const DimData d = Shaped(c);
            if (MeasureOf(d) > 1e-12) out.entities.push_back(MakeDimension(d));
        } else if (m_step == Second || m_step == End1) {
            out.guides.push_back({m_d.pts[0], c});
        } else if (m_step == End2) {
            out.guides.push_back({m_d.pts[0], m_d.pts[1]});
            out.guides.push_back({m_d.pts[0], c});
        }
    }
};

// ---------------------------------------------------------------------------
// DIMCONTINUE and DIMBASELINE
// ---------------------------------------------------------------------------
class DimChainCmd : public Command {
    const bool m_baseline;
    uint64_t m_base = 0;
    std::vector<uint64_t> m_made, m_bases;
    enum Step { Select, Next } m_step = Select;

    const DimData* Base() const {
        const Entity* e = E->Doc().Find(m_base);
        if (!e || e->type != EntityType::Dimension || !e->dim) return nullptr;
        const DimData* d = e->dim.get();
        return d->kind == DimData::Linear || d->kind == DimData::Aligned ? d : nullptr;
    }
    void AskSelect() {
        E->Ask(Pick(m_baseline ? L"Select base dimension" : L"Select continued dimension", kAcceptEmpty));
        m_step = Select;
    }
    void AskNext() {
        const DimData* d = Base();
        if (!d) return AskSelect();
        E->Ask(From(P(L"Specify second extension line origin", kAcceptPoint | kAcceptEmpty,
                      {{L"U", L"Undo"}, {L"S", L"Select"}}),
                    m_baseline ? d->pts[0] : d->pts[1]));
        m_step = Next;
    }
    DimData Make(Vec2 p) const {
        const DimData& b = *Base();
        DimData d = b;
        d.kind = DimData::Linear;
        d.angle = AngleOf(DimDirection(b));
        d.text.clear();
        d.rays = false;
        d.pts[0] = m_baseline ? b.pts[0] : b.pts[1];
        d.refs[0] = m_baseline ? b.refs[0] : b.refs[1];
        d.pts[1] = p;
        d.refs[1] = FindRef(E->Doc(), p);
        if (m_baseline) {
            // A row further out from the measured points.
            const Vec2 n = Perp(DimDirection(b));
            const double side = Dot(b.loc - b.pts[0], n) >= 0 ? 1 : -1;
            d.loc = b.loc + n * (side * 2 * b.style.textHeight);
        }
        return d;
    }

public:
    explicit DimChainCmd(bool baseline) : m_baseline(baseline) {}
    void Start() override {
        m_base = E->lastDim;
        if (Base()) AskNext();
        else AskSelect();
    }
    void OnPoint(Vec2 p) override {
        if (m_step == Select) {
            m_base = E->PickEntity(p);
            if (!Base()) {
                E->Msg(L"Pick a linear or aligned dimension.");
                return;
            }
            AskNext();
            return;
        }
        const DimData d = Make(p);
        if (DimMeasure(d) <= 1e-12) {
            E->Msg(L"That point measures nothing from the last one.");
            return;
        }
        E->Doc().BeginChange(L"Dimension");
        const uint64_t id = E->Doc().Add(MakeDimension(d));
        E->Doc().EndChange();
        m_made.push_back(id);
        m_bases.push_back(m_base);
        m_base = id;
        E->lastDim = id;
        E->Msg(L"Dimension: " + DimDisplayText(d));
        AskNext();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"S") return AskSelect();
        if (k == L"U") {
            if (m_made.empty()) {
                E->Msg(L"Nothing to undo.");
            } else {
                E->Doc().Undo();
                m_base = m_bases.back();
                m_made.pop_back();
                m_bases.pop_back();
                E->lastDim = m_base;
            }
            AskNext();
        }
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Next || !Base()) return;
        const DimData d = Make(c);
        if (DimMeasure(d) > 1e-12) out.entities.push_back(MakeDimension(d));
    }
};

// ---------------------------------------------------------------------------
// DIMEDIT: a new value (driving) or new text
// ---------------------------------------------------------------------------
class DimEditCmd : public Command {
    uint64_t m_id = 0;
    bool m_text = false;

    const DimData* Dim() const {
        const Entity* e = E->Doc().Find(m_id);
        return e && e->type == EntityType::Dimension ? e->dim.get() : nullptr;
    }
    void AskValue() {
        const DimData& d = *Dim();
        const double m = DimMeasure(d);
        E->Ask(P(L"Enter the new value (the geometry changes to it)", kAcceptNumber | kAcceptEmpty,
                 {{L"T", L"Text"}, {L"R", L"Reset text"}},
                 DimNumber(d.kind == DimData::Angular ? ToDegrees(m) : m, d.style)));
        m_text = false;
    }

public:
    void Start() override {
        const auto& sel = E->Selection();
        if (sel.size() == 1) {
            m_id = *sel.begin();
            if (Dim()) return AskValue();
        }
        E->Ask(Pick(L"Select dimension"));
    }
    void OnPoint(Vec2 p) override {
        m_id = E->PickEntity(p);
        if (!Dim()) {
            E->Msg(L"Pick a dimension.");
            return;
        }
        AskValue();
    }
    void OnNumber(double v) override {
        const DimData* d = Dim();
        if (!d) return;
        if (d->kind == DimData::Angular) v = ToRadians(v);
        E->Doc().BeginChange(L"Dimension value");
        std::wstring why;
        if (!DriveDimension(E->Doc(), m_id, v, why)) {
            E->Doc().CancelChange();
            E->Msg(why);
            return;
        }
        E->Doc().EndChange();
        if (const DimData* now = Dim()) E->Msg(L"The dimension now reads " + DimDisplayText(*now) + L".");
        E->Finish();
    }
    bool OnText(const std::wstring& t) override {
        if (!m_text) return false;
        if (const Entity* e = E->Doc().Find(m_id)) {
            Entity x = *e;
            DimData d = *x.dim;
            d.text = t;
            x.dim = std::make_shared<const DimData>(std::move(d));
            E->Doc().BeginChange(L"Dimension text");
            E->Doc().Replace(x);
            E->Doc().EndChange();
        }
        E->Finish();
        return true;
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"T") {
            E->Ask(TextPrompt(L"Enter dimension text (<> stands for the measured value)"));
            m_text = true;
        } else if (k == L"R") {
            m_text = true;
            OnText(L"");
        }
    }
};

// ---------------------------------------------------------------------------
// COPYCLIP, CUTCLIP and PASTECLIP
// ---------------------------------------------------------------------------
constexpr char kClipMagic[] = "FeatherCADClip";

class CopyClipCmd : public Command {
    bool m_cut;

public:
    explicit CopyClipCmd(bool cut) : m_cut(cut) {}
    void Start() override { E->AskSelection(); }
    void OnSelected() override {
        std::vector<Entity> ents;
        Box box;
        for (uint64_t id : SelectionIds(E))
            if (const Entity* e = E->Doc().Find(id)) {
                ents.push_back(*e);
                box.Add(Bounds(*e));
            }
        if (ents.empty()) return E->Finish();
        // The lower left corner is where pasted objects hang from.
        const Vec2 base = box.Empty() ? Vec2{} : Vec2{box.minX, box.minY};
        E->Host().SetClipboard(std::string(kClipMagic) + " " + FormatNumber(base.x) + " " + FormatNumber(base.y) + "\n" +
                               E->Doc().ObjectsToText(ents));
        if (m_cut) {
            E->Doc().BeginChange(L"Cut");
            for (const Entity& e : ents) E->Doc().Erase(e.id);
            E->ClearSelection();
        }
        E->Msg(std::to_wstring(ents.size()) + (m_cut ? L" object(s) cut" : L" object(s) copied") +
               L" to the clipboard. Ctrl+V pastes them, also into another drawing.");
        E->Finish();
    }
};

class PasteClipCmd : public Command {
    std::vector<Entity> m_ents;
    std::vector<Layer> m_layers;  // the layers they came from
    BlockTable m_blocks;          // and the blocks they use
    Vec2 m_base;

public:
    void Start() override {
        std::string text;
        if (!E->Host().GetClipboard(text) || text.compare(0, sizeof(kClipMagic) - 1, kClipMagic) != 0) {
            E->Msg(L"There are no Feather CAD objects on the clipboard. Copy some first (Ctrl+C).");
            return E->Finish();
        }
        const size_t nl = text.find('\n');
        double x = 0, y = 0;
        {
            const std::string head = text.substr(0, nl);
            const size_t s1 = head.find(' '), s2 = head.find(' ', s1 + 1);
            if (s1 != std::string::npos && s2 != std::string::npos) {
                ParseNumber(head.substr(s1 + 1, s2 - s1 - 1), x);
                ParseNumber(head.substr(s2 + 1), y);
            }
        }
        m_base = {x, y};
        if (nl == std::string::npos || !Document::ObjectsFromText(text.substr(nl + 1), m_ents, &m_layers, &m_blocks) || m_ents.empty()) {
            E->Msg(L"The clipboard could not be read.");
            return E->Finish();
        }
        E->Ask(P(L"Specify insertion point", kAcceptPoint));
    }
    void OnPoint(Vec2 p) override {
        for (Entity& e : m_ents) Translate(e, p - m_base);
        E->Doc().BeginChange(L"Paste");
        // Onto the layers (and blocks) of the same names here (made if missing).
        const std::vector<uint64_t> ids = E->Doc().AddForeign(std::move(m_ents), m_layers, m_blocks);
        E->Doc().EndChange();
        E->ClearSelection();
        E->AddToSelection(ids);
        E->Msg(std::to_wstring(ids.size()) + L" object(s) pasted.");
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        for (size_t i = 0; i < m_ents.size() && i < kMaxPreview; ++i) {
            Entity e = m_ents[i];
            Translate(e, c - m_base);
            out.entities.push_back(std::move(e));
        }
    }
};
}  // namespace

std::unique_ptr<Command> CreateDimCommand(const std::wstring& name) {
    if (name == L"DIM") return std::make_unique<DimCmd>(DimMode::Smart);
    if (name == L"DIMLINEAR") return std::make_unique<DimCmd>(DimMode::Linear);
    if (name == L"DIMALIGNED") return std::make_unique<DimCmd>(DimMode::Aligned);
    if (name == L"DIMRADIUS") return std::make_unique<DimCmd>(DimMode::Radius);
    if (name == L"DIMDIAMETER") return std::make_unique<DimCmd>(DimMode::Diameter);
    if (name == L"DIMANGULAR") return std::make_unique<DimCmd>(DimMode::Angular);
    if (name == L"DIMCONTINUE") return std::make_unique<DimChainCmd>(false);
    if (name == L"DIMBASELINE") return std::make_unique<DimChainCmd>(true);
    if (name == L"DIMEDIT") return std::make_unique<DimEditCmd>();
    if (name == L"COPYCLIP") return std::make_unique<CopyClipCmd>(false);
    if (name == L"CUTCLIP") return std::make_unique<CopyClipCmd>(true);
    if (name == L"PASTECLIP") return std::make_unique<PasteClipCmd>();
    return nullptr;
}
