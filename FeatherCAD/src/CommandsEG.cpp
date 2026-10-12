// CommandsEG.cpp - commands for engineering drawing (EngGraphics.h):
// DIVIDE, MEASURE, ISODRAFT, ISOPLANE, ISOCIRCLE, ISOSCALE, DEVELOP,
// TRUELENGTH, CUTPLANE, TRUESHAPE, FIRSTANGLE and BISSETUP.
#include <algorithm>
#include <cmath>

#include "CommandUtil.h"
#include "Dimension.h"
#include "EngGraphics.h"
#include "Style.h"

namespace {
// Objects added as one undo step, on the current layer in the current
// colour, keeping the line weights and types they were made with (thick
// outlines, thin projectors, chain lines).
void AddAll(Engine* E, const wchar_t* step, const std::vector<Entity>& ents) {
    Document& d = E->Doc();
    const CurrentStyle& cur = d.Current();
    d.BeginChange(step);
    for (Entity e : ents) {
        if (e.layer < 0) {
            e.layer = cur.layer;
            if (e.color == kByLayer) e.color = cur.color;
            if (e.linetype == kLtByLayer) e.linetype = (int16_t)cur.linetype;
            if (e.lineweight == kLwByLayer) e.lineweight = (int16_t)cur.lineweight;
        }
        d.Add(e);
    }
    d.EndChange();
}

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

// ---------------------------------------------------------------------------
// DIVIDE and MEASURE: points at equal parts (or every so far) along an object
// ---------------------------------------------------------------------------
bool g_numbered = false;
int g_lastParts = 12;
double g_lastStep = 10;

class DivideCmd : public Command {
    bool m_measure;
    uint64_t m_id = 0;
    Vec2 m_pick;

    // Where the points start: a quadrant or corner of a closed shape when the
    // click was near one (within `within`), else where it was clicked.
    Vec2 StartNear(const Entity& e, double within) const {
        std::vector<Vec2> special;
        if (e.type == EntityType::Circle || (e.type == EntityType::Ellipse && e.closed)) Quadrants(e, special);
        else if (e.type == EntityType::Polyline && e.closed)
            for (const Vertex& vx : e.verts) special.push_back(vx.p);
        Vec2 best = m_pick;
        double bestD = within;
        for (Vec2 q : special)
            if (const double dd = Dist(q, m_pick); dd <= bestD) bestD = dd, best = q;
        return best;
    }
    void AskCount() {
        Prompt p = m_measure ? P(L"Type the length between points", kAcceptNumber | kAcceptEmpty,
                                 {{L"N", g_numbered ? L"Numbered: on" : L"Numbered: off"}}, E->Len(g_lastStep))
                             : P(L"Type the number of equal parts", kAcceptNumber | kAcceptEmpty,
                                 {{L"N", g_numbered ? L"Numbered: on" : L"Numbered: off"}}, std::to_wstring(g_lastParts));
        E->Ask(p);
    }
    void Make(double v) {
        const Entity* e = E->Doc().Find(m_id);
        if (!e) return E->Finish();
        std::vector<Vec2> pts;
        bool ok;
        if (m_measure) {
            ok = MeasurePoints(*e, v, StartNear(*e, v / 2), pts);
            if (ok) g_lastStep = v;
        } else {
            const int n = (int)std::lround(v);
            if (n < 2 || n > 1000) return E->Msg(L"Between 2 and 1000 parts, please.");
            ok = DividePoints(*e, n, StartNear(*e, LengthOf(*e) / n / 2), pts);
            if (ok) g_lastParts = n;
        }
        if (!ok || pts.empty()) {
            E->Msg(m_measure ? L"That length does not fit along it." : L"That cannot be divided.");
            return E->Finish();
        }
        std::vector<Entity> ents;
        for (Vec2 q : pts) ents.push_back(Entity::Point(q));
        if (g_numbered) {
            const double h = TextSize(E);
            const Box b = Bounds(*e);
            const Vec2 c = b.Center();
            Entity probe;
            std::vector<Vec2> all = pts;
            std::vector<Vec2> ends;
            EndPoints(*e, ends);
            const bool closed = ends.empty() || (ends.size() == 2 && Near(ends[0], ends[1], 1e-9)) ||
                                e->type == EntityType::Circle || (e->closed && e->type != EntityType::Arc);
            int first = 1;
            if (!closed && ends.size() >= 2) {
                // The ends too: 0 at the one picked nearer, the last at the other.
                const bool startNear = Dist(m_pick, ends[0]) <= Dist(m_pick, ends.back());
                const Vec2 nearEnd = startNear ? ends[0] : ends.back(), farEnd = startNear ? ends.back() : ends[0];
                all.insert(all.begin(), nearEnd);
                if (!m_measure) all.push_back(farEnd);
                first = 0;
            }
            Vec2 side = Perp(Normalized((all.back() - all.front()) + Vec2{1e-12, 0}));
            if (side.y < 0 || (std::fabs(side.y) < 1e-9 && side.x > 0)) side = side * -1;
            for (size_t i = 0; i < all.size(); ++i) {
                Vec2 dir = closed ? all[i] - c : side;
                if (Length(dir) < 1e-9) dir = {0, 1};
                ents.push_back(MakePointLabel(all[i], std::to_wstring(first + (int)i), h, dir));
            }
        }
        AddAll(E, m_measure ? L"Measure" : L"Divide", ents);
        E->Msg(std::to_wstring(pts.size()) + L" point(s) placed." +
               (g_numbered ? L"" : L" Type N next time to number them."));
        E->Finish();
    }

public:
    explicit DivideCmd(bool measure) : m_measure(measure) {}
    void Start() override {
        E->Ask(Pick(m_measure ? L"Click the object to mark along (near the end to start from)"
                              : L"Click the object to divide (a circle: near where point 1 goes)"));
    }
    void OnPoint(Vec2 p) override {
        if (m_id) return;
        const uint64_t id = E->PickEntity(p, true);
        const Entity* e = id ? E->Doc().Find(id) : nullptr;
        if (!e || !Dividable(*e)) return E->Msg(L"Click a line, arc, circle, polyline, ellipse or spline.");
        m_id = id;
        m_pick = p;
        AskCount();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"N") g_numbered = !g_numbered;
        AskCount();
    }
    void OnNumber(double v) override {
        if (m_measure && !Positive(E, v)) return;
        Make(v);
    }
    void OnEnter() override {
        if (!m_id) return E->Finish();
        Make(m_measure ? g_lastStep : g_lastParts);
    }
};

// ---------------------------------------------------------------------------
// ISODRAFT, ISOPLANE: isometric drawing aids
// ---------------------------------------------------------------------------
class IsoDraftCmd : public Command {
public:
    void Start() override {
        const Drafting& d = E->Draft();
        E->Ask(P(d.isoOn ? L"Isometric drawing is on" : L"Isometric drawing is off", kAcceptKeyword | kAcceptEmpty,
                 {{L"ON", L"On"}, {L"OFF", L"Off"}, {L"S", d.isoScale ? L"Scale: isometric (0.816)" : L"Scale: true lengths"}},
                 d.isoOn ? L"Off" : L"On"));
    }
    void Set(bool on) {
        Drafting& d = E->Draft();
        d.isoOn = on;
        if (on) d.orthoOn = true, d.polarOn = false;
        E->Msg(on ? std::wstring(L"Isometric drawing on: lines go along the 30\x00B0 and upright axes of the ") +
                        IsoPlaneName((IsoPlane)d.isoPlane) + L" plane (F5 changes the plane)." +
                        (d.isoScale ? L" Lengths typed along the axes are drawn \x00D7 0.816." : L"")
                  : L"Isometric drawing off.");
        E->Host().DraftingChanged();
        E->Finish();
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"S") {
            Drafting& d = E->Draft();
            d.isoScale = !d.isoScale;
            E->Msg(d.isoScale ? L"Isometric scale: lengths typed along the isometric axes are drawn \x00D7 0.816 "
                                L"(isometric projection)."
                              : L"True lengths: lengths are drawn as typed (isometric view).");
            E->Host().DraftingChanged();
            return Start();
        }
        Set(k == L"ON");
    }
    void OnEnter() override { Set(!E->Draft().isoOn); }
};

class IsoPlaneCmd : public Command {
public:
    void Start() override {
        E->Ask(P((std::wstring(L"Isometric plane is ") + IsoPlaneName((IsoPlane)E->Draft().isoPlane)).c_str(),
                 kAcceptKeyword | kAcceptEmpty, {{L"L", L"Left"}, {L"T", L"Top"}, {L"R", L"Right"}}, L"next"));
    }
    void Set(IsoPlane p) {
        E->Draft().isoPlane = (int)p;
        E->Msg(std::wstring(L"Isometric plane: ") + IsoPlaneName(p));
        E->Host().DraftingChanged();
        E->Finish();
    }
    void OnKeyword(const std::wstring& k) override {
        Set(k == L"L" ? IsoPlane::Left : k == L"T" ? IsoPlane::Top : IsoPlane::Right);
    }
    void OnEnter() override { Set(NextIsoPlane((IsoPlane)E->Draft().isoPlane)); }
};

// ISOCIRCLE: a circle in the current isometric plane (an ellipse).
class IsoCircleCmd : public Command {
    bool m_have = false, m_diameter = false;
    Vec2 m_c;

    double Factor() const { return E->Draft().isoScale ? kIsoScale : 1; }
    IsoPlane Plane() const { return (IsoPlane)E->Draft().isoPlane; }
    void Add(double r) {
        if (!(r > 1e-9)) return;
        E->lastRadius = r;
        AddAll(E, L"Isometric circle", {IsoCircle(m_c, r, Plane())});
        E->Finish();
    }
    void AskSize() {
        E->Ask(From(P(m_diameter ? L"Type the diameter (true size) or click a point" : L"Type the radius (true size) or click a point",
                      kAcceptPoint | kAcceptNumber | kAcceptEmpty, {{L"D", L"Diameter"}, {L"P", L"Plane"}},
                      E->lastRadius > 0 ? E->Len(m_diameter ? 2 * E->lastRadius : E->lastRadius) : L""),
                    m_c));
    }

public:
    void Start() override {
        E->Ask(P((std::wstring(L"Click the centre of the isometric circle (") + IsoPlaneName(Plane()) + L" plane)").c_str(),
                 kAcceptPoint, {{L"P", L"Plane"}}));
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"P") {
            E->Draft().isoPlane = (int)NextIsoPlane(Plane());
            E->Host().DraftingChanged();
            E->Msg(std::wstring(L"Isometric plane: ") + IsoPlaneName(Plane()));
            return m_have ? AskSize() : Start();
        }
        if (k == L"D") m_diameter = !m_diameter;
        AskSize();
    }
    void OnPoint(Vec2 p) override {
        if (!m_have) {
            m_have = true;
            m_c = p;
            return AskSize();
        }
        // A point on the circle along an isometric axis: as far as it is (already isometric).
        Add(Dist(m_c, p));
    }
    void OnNumber(double v) override {
        if (!Positive(E, v)) return;
        Add((m_diameter ? v / 2 : v) * Factor());
    }
    void OnEnter() override {
        if (m_have && E->lastRadius > 0) Add(E->lastRadius);
        else E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (!m_have) return;
        const double r = Dist(m_c, c);
        if (r > 1e-9) out.entities.push_back(IsoCircle(m_c, r, Plane()));
    }
};

// ISOSCALE: the isometric scale construction.
class IsoScaleCmd : public Command {
    int m_step = 0;
    Vec2 m_at;
    double m_len = 100, m_div = 10;

public:
    void Start() override { E->Ask(P(L"Click where the isometric scale starts (its left end)", kAcceptPoint)); }
    void OnPoint(Vec2 p) override {
        if (m_step != 0) return;
        m_at = p;
        m_step = 1;
        E->Ask(P(L"Type the length of the scale", kAcceptNumber | kAcceptEmpty, {}, E->Len(m_len)));
    }
    void OnNumber(double v) override {
        if (!Positive(E, v)) return;
        if (m_step == 1) {
            m_len = v;
            m_step = 2;
            return E->Ask(P(L"Type the length of each division", kAcceptNumber | kAcceptEmpty, {}, E->Len(m_div)));
        }
        if (m_len / v > 200) return E->Msg(L"That would make too many divisions.");
        m_div = v;
        Make();
    }
    void OnEnter() override {
        if (m_step == 1) return OnNumber(m_len);
        if (m_step == 2) return Make();
        E->Finish();
    }
    void Make() {
        AddAll(E, L"Isometric scale", IsoScaleDrawing(m_at, m_len, m_div, TextSize(E)));
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == 0) out.entities = IsoScaleDrawing(c, m_len, m_div, TextSize(E));
    }
};

// ---------------------------------------------------------------------------
// DEVELOP: the development of a solid's lateral surface
// ---------------------------------------------------------------------------
SolidSpec g_solid;

class DevelopCmd : public Command {
    enum Step { Kind, Sides, Base, Height, Orient, Divisions, Cut, CutAngle, CutHeight, Place } m_step = Kind;
    SolidSpec m_s = g_solid;

    bool Polygonal() const { return m_s.kind == SolidSpec::Prism || m_s.kind == SolidSpec::Pyramid; }
    void Ask() {
        switch (m_step) {
            case Kind:
                E->Ask(P(L"Which solid", kAcceptKeyword | kAcceptEmpty,
                         {{L"P", L"Prism"}, {L"C", L"Cylinder"}, {L"Y", L"pYramid"}, {L"O", L"cOne"}}, SolidName(m_s)));
                break;
            case Sides: E->Ask(P(L"Type the number of sides of the base", kAcceptNumber | kAcceptEmpty, {}, std::to_wstring(m_s.sides))); break;
            case Base:
                E->Ask(P(Polygonal() ? L"Type the side of the base" : L"Type the diameter of the base", kAcceptNumber | kAcceptEmpty, {},
                         E->Len(m_s.base)));
                break;
            case Height: E->Ask(P(L"Type the height of the axis", kAcceptNumber | kAcceptEmpty, {}, E->Len(m_s.height))); break;
            case Orient:
                E->Ask(P(L"Resting on its base with", kAcceptKeyword | kAcceptEmpty,
                         {{L"E", L"an Edge of the base parallel to VP"}, {L"C", L"a Corner towards the viewer"}},
                         m_s.cornerFront ? L"Corner" : L"Edge"));
                break;
            case Divisions:
                E->Ask(P(L"Type the number of generators (divisions of the base circle)", kAcceptNumber | kAcceptEmpty, {},
                         std::to_wstring(m_s.divisions)));
                break;
            case Cut:
                E->Ask(P(L"Is it cut by a plane", kAcceptKeyword | kAcceptEmpty, {{L"N", L"No"}, {L"Y", L"Yes"}}, m_s.cut ? L"Yes" : L"No"));
                break;
            case CutAngle:
                E->Ask(P(L"Type the angle of the cutting plane to HP (degrees; negative: falling to the right)",
                         kAcceptNumber | kAcceptEmpty, {}, E->Ang(ToRadians(m_s.cutAngle))));
                break;
            case CutHeight:
                E->Ask(P(L"Type how high above the base the plane meets the axis", kAcceptNumber | kAcceptEmpty, {},
                         E->Len(m_s.cutHeight)));
                break;
            case Place: E->Ask(P(L"Click where the development goes (its bottom left)", kAcceptPoint)); break;
        }
    }
    void Next() {
        switch (m_step) {
            case Kind: m_step = Polygonal() ? Sides : Base; break;
            case Sides: m_step = Base; break;
            case Base: m_step = Height; break;
            case Height: m_step = Polygonal() ? Orient : Divisions; break;
            case Orient:
            case Divisions: m_step = Cut; break;
            case Cut: m_step = m_s.cut ? CutAngle : Place; break;
            case CutAngle: m_step = CutHeight; break;
            case CutHeight: m_step = Place; break;
            case Place: break;
        }
        Ask();
    }

public:
    void Start() override { Ask(); }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == Kind) {
            m_s.kind = k == L"P" ? SolidSpec::Prism : k == L"C" ? SolidSpec::Cylinder : k == L"Y" ? SolidSpec::Pyramid : SolidSpec::Cone;
        } else if (m_step == Orient) {
            m_s.cornerFront = k == L"C";
        } else if (m_step == Cut) {
            m_s.cut = k == L"Y";
        }
        Next();
    }
    void OnNumber(double v) override {
        switch (m_step) {
            case Sides: {
                const int n = (int)std::lround(v);
                if (n < 3 || n > 12) return E->Msg(L"Between 3 and 12 sides, please.");
                m_s.sides = n;
                break;
            }
            case Divisions: {
                const int n = (int)std::lround(v);
                if (n < 4 || n > 48) return E->Msg(L"Between 4 and 48, please.");
                m_s.divisions = n;
                break;
            }
            case Base:
                if (!Positive(E, v)) return;
                m_s.base = v;
                break;
            case Height:
                if (!Positive(E, v)) return;
                m_s.height = v;
                break;
            case CutAngle:
                if (std::fabs(v) >= 89.9) return E->Msg(L"Less than 90 degrees, please.");
                m_s.cutAngle = v;
                break;
            case CutHeight: m_s.cutHeight = v; break;
            default: return;
        }
        Next();
    }
    void OnEnter() override {
        if (m_step == Place) return E->Finish();
        Next();
    }
    void OnPoint(Vec2 p) override {
        if (m_step != Place) return;
        std::vector<Entity> ents;
        std::wstring why;
        if (!DevelopSolid(m_s, p, TextSize(E), ents, why)) {
            E->Msg(why);
            return E->Finish();
        }
        g_solid = m_s;
        AddAll(E, L"Development", ents);
        E->Msg(L"Development of the " + SolidName(m_s) + L" drawn. Its corners (or generators) are named from the leftmost "
               L"one in the front view, going round through the front.");
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Place) return;
        std::wstring why;
        DevelopSolid(m_s, c, TextSize(E), out.entities, why);
    }
};

// ---------------------------------------------------------------------------
// TRUELENGTH: the true length of a line and its angles to HP and VP
// ---------------------------------------------------------------------------
class TrueLengthCmd : public Command {
    enum Step { FrontView, TopView, DrawA1, DrawA, DrawTL, DrawTheta, DrawPhi, DrawSide } m_step = FrontView;
    Vec2 m_f0, m_f1;
    Vec2 m_a1, m_a;
    double m_tl = 0, m_theta = 0, m_phi = 0;

    void Show(const LineViews& v, bool withViews) {
        const TrueLengthResult r = TrueLengthOf(v);
        AddAll(E, L"True length", TrueLengthConstruction(v, TextSize(E), withViews));
        E->Msg(L"True length " + E->Len(r.tl) + L", \x03B8 (to HP) " + E->Ang(r.theta) + L"\x00B0, \x03C6 (to VP) " +
               E->Ang(r.phi) + L"\x00B0.");
        E->Finish();
    }

public:
    void Start() override {
        E->Ask(Pick(L"Click the front view of the line (a'b')", 0, {{L"D", L"Draw it from its true length and angles"}}));
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == DrawSide) {
            LineViews v;
            std::wstring why;
            if (!ViewsFromTrueLength(m_a1, m_a, m_tl, m_theta, m_phi, k != L"L", v, why)) {
                E->Msg(why);
                return E->Finish();
            }
            return Show(v, true);
        }
        if (k == L"D") {
            m_step = DrawA1;
            E->Ask(P(L"Click a' (end A in the front view)", kAcceptPoint));
        }
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case FrontView:
                if (!PickSegment(E, p, m_f0, m_f1)) return E->Msg(L"Click a line.");
                m_step = TopView;
                return E->Ask(Pick(L"Click the top view of the line (ab)"));
            case TopView: {
                Vec2 t0, t1;
                if (!PickSegment(E, p, t0, t1)) return E->Msg(L"Click a line.");
                // a' is the left end of the front view; a is the end of the top view on its projector.
                LineViews v;
                if (m_f1.x < m_f0.x) std::swap(m_f0, m_f1);
                v.a1 = m_f0, v.b1 = m_f1;
                if (std::fabs(t0.x - m_f0.x) + std::fabs(t1.x - m_f1.x) <= std::fabs(t1.x - m_f0.x) + std::fabs(t0.x - m_f1.x))
                    v.a = t0, v.b = t1;
                else
                    v.a = t1, v.b = t0;
                if (std::fabs(v.a.x - v.a1.x) + std::fabs(v.b.x - v.b1.x) > 1e-3 * (1 + Dist(v.a1, v.b1)))
                    E->Msg(L"Note: the ends of the two views are not on the same projectors; the lengths along XY are averaged.");
                return Show(v, false);
            }
            case DrawA1:
                m_a1 = p;
                m_step = DrawA;
                return E->Ask(From(P(L"Click a (end A in the top view, below a')", kAcceptPoint), m_a1));
            case DrawA:
                m_a = {m_a1.x, p.y};
                m_step = DrawTL;
                return E->Ask(P(L"Type the true length of the line", kAcceptNumber));
            default: return;
        }
    }
    void OnNumber(double v) override {
        switch (m_step) {
            case DrawTL:
                if (!Positive(E, v)) return;
                m_tl = v;
                m_step = DrawTheta;
                return E->Ask(P(L"Type its angle to HP, \x03B8 (degrees)", kAcceptNumber));
            case DrawTheta:
                m_theta = ToRadians(v);
                m_step = DrawPhi;
                return E->Ask(P(L"Type its angle to VP, \x03C6 (degrees)", kAcceptNumber));
            case DrawPhi:
                m_phi = ToRadians(v);
                if (m_theta < 0 || m_phi < 0 || m_theta + m_phi > kPi / 2 + 1e-9)
                    return E->Msg(L"\x03B8 + \x03C6 must be 90\x00B0 or less. Type \x03C6 again.");
                m_step = DrawSide;
                return E->Ask(P(L"End B goes to the", kAcceptKeyword | kAcceptEmpty, {{L"R", L"Right"}, {L"L", L"Left"}}, L"Right"));
            default: return;
        }
    }
    void OnEnter() override {
        if (m_step == DrawSide) return OnKeyword(L"R");
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == DrawA) out.guides.push_back({m_a1, {m_a1.x, c.y}});
    }
};

// ---------------------------------------------------------------------------
// CUTPLANE: a cutting-plane line with its arrows and letters
// ---------------------------------------------------------------------------
std::wstring g_letter = L"A", g_lastLetter;

class CutPlaneCmd : public Command {
    enum Step { Points, Side, Letter } m_step = Points;
    std::vector<Vec2> m_pts;
    Vec2 m_side;

    void Make() {
        AddAll(E, L"Cutting plane", CuttingPlane(m_pts, m_side, g_letter, TextSize(E)));
        g_lastLetter = g_letter;
        g_letter = NextName(g_letter);
        E->Msg(L"Cutting plane " + g_lastLetter + L"-" + g_lastLetter + L" drawn. TRUESHAPE finds the true shape of the section.");
        E->Finish();
    }

public:
    void Start() override { E->Ask(P(L"Click the first point of the cutting plane line", kAcceptPoint)); }
    void OnPoint(Vec2 p) override {
        if (m_step == Side) {
            m_side = p;
            m_step = Letter;
            return E->Ask(P(L"Type the letter of the section", kAcceptEmpty, {}, g_letter));
        }
        if (m_step != Points || (!m_pts.empty() && Near(p, m_pts.back(), 1e-9))) return;
        m_pts.push_back(p);
        E->Ask(From(P(m_pts.size() == 1 ? L"Click the other end of the cutting plane line"
                                        : L"Click the next point (where it bends), or press Enter when the line is done",
                      kAcceptPoint | (m_pts.size() >= 2 ? (unsigned)kAcceptEmpty : 0u)),
                    m_pts.back()));
    }
    void OnEnter() override {
        if (m_step == Points && m_pts.size() >= 2) {
            m_step = Side;
            return E->Ask(P(L"Click on the side the section is seen from (the arrows point that way)", kAcceptPoint));
        }
        if (m_step == Letter) return Make();
        if (m_step == Points) E->Finish();
    }
    bool OnText(const std::wstring& s) override {
        if (m_step != Letter) return false;
        g_letter = s;
        Make();
        return true;
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step == Points && !m_pts.empty()) {
            std::vector<Vec2> pts = m_pts;
            if (!Near(c, pts.back(), 1e-9)) pts.push_back(c);
            if (pts.size() >= 2) out.entities = CuttingPlane(pts, pts[0] + Perp(pts[1] - pts[0]), g_letter, TextSize(E));
        } else if (m_step == Side) {
            out.entities = CuttingPlane(m_pts, c, g_letter, TextSize(E));
        }
    }
};

// ---------------------------------------------------------------------------
// TRUESHAPE: the true shape of a section, by an auxiliary view
// ---------------------------------------------------------------------------
class TrueShapeCmd : public Command {
    enum Step { CutLine, Section, SectionPoints, XY, Place } m_step = CutLine;
    Vec2 m_a, m_b;
    std::vector<Vec2> m_section;
    bool m_curved = false, m_hasXY = false;
    double m_xy = 0;

    bool Compute(Vec2 at, TrueShapeResult& r, std::wstring& why) const {
        return TrueShape(m_a, m_b, m_section, at, m_hasXY, m_xy, r, why);
    }
    std::vector<Entity> Build(const TrueShapeResult& r, bool full) const {
        std::vector<Entity> out;
        const double h = TextSize(E);
        Entity ref = Entity::Line(r.x1, r.y1);
        ref.lineweight = 18;
        out.push_back(ref);
        for (const auto& pr : r.projectors) {
            Entity l = Entity::Line(pr.first, pr.second);
            l.lineweight = 18;
            out.push_back(l);
        }
        Entity shape;
        if (m_curved) shape = Entity::Spline(r.shape, true);
        else {
            std::vector<Vertex> v;
            for (Vec2 p : r.shape) v.push_back({p, 0});
            shape = Entity::Polyline(v, true);
        }
        shape.lineweight = 50;
        out.push_back(shape);
        if (!full) return out;
        HatchData hd;
        hd.loops.push_back(r.shape);
        hd.scale = std::max(0.5, h / 3.5);
        hd.origin = r.shape[0];
        Entity hatch = MakeHatch(hd);  // the layer's line weight, like other hatches
        out.push_back(hatch);
        const Vec2 u = Normalized(r.y1 - r.x1);
        const Vec2 n = Normalized(r.shape[0] - r.projectors[0].first);
        Vec2 side = Perp(u);
        if (Dot(side, n) > 0) side = side * -1;  // the names on the cutting plane's side of X1Y1
        out.push_back(MakePointLabel(r.x1, L"X1", h, side - u * 0.5));
        out.push_back(MakePointLabel(r.y1, L"Y1", h, side + u * 0.5));
        // The title beyond the shape, away from X1Y1.
        Vec2 c{0, 0};
        double far = -1e300;
        for (Vec2 p : r.shape) c = c + p / (double)r.shape.size();
        for (Vec2 p : r.shape) far = std::max(far, Dot(p - c, n));
        TextData t;
        t.content = g_lastLetter.empty() ? L"TRUE SHAPE OF THE SECTION" : L"TRUE SHAPE OF SECTION " + g_lastLetter + L"-" + g_lastLetter;
        t.height = h;
        t.justify = 4;
        const double w = TextWidth(t.content, h);
        out.push_back(MakeText(c + n * (far + h * 2 + std::fabs(n.x) * w / 2), t));
        return out;
    }

public:
    void Start() override {
        E->Ask(Pick(L"Click the cutting plane line (in the view where the plane is seen as a line)"));
    }
    void OnPoint(Vec2 p) override {
        switch (m_step) {
            case CutLine:
                if (!PickSegment(E, p, m_a, m_b)) return E->Msg(L"Click a straight line.");
                if (std::fabs(m_b.x - m_a.x) < 1e-9) return E->Msg(L"The cutting plane line must not be upright (along the projectors).");
                m_step = Section;
                return E->Ask(Pick(L"Click the section as seen in the other view (a closed shape)", kAcceptKeyword,
                                   {{L"P", L"Points: click its corners"}}));
            case Section: {
                const uint64_t id = E->PickEntity(p, true);
                const Entity* e = id ? E->Doc().Find(id) : nullptr;
                m_section = e ? OutlinePoints(*e, m_curved) : std::vector<Vec2>{};
                if (m_section.size() < 3) return E->Msg(L"Click a closed shape: a closed polyline, circle, ellipse or spline (or type P).");
                return AskXY();
            }
            case SectionPoints:
                m_section.push_back(p);
                return E->Ask(P(L"Click the next corner of the section, or press Enter when done", kAcceptPoint | kAcceptEmpty));
            case XY: {
                Vec2 a, b;
                if (!PickSegment(E, p, a, b)) return E->Msg(L"Click the XY line, or press Enter.");
                m_hasXY = true;
                m_xy = (a.y + b.y) / 2;
                m_step = Place;
                return AskPlace();
            }
            case Place: {
                TrueShapeResult r;
                std::wstring why;
                if (!Compute(p, r, why)) return E->Msg(why);
                AddAll(E, L"True shape", Build(r, true));
                E->Msg(L"True shape drawn and hatched. The new reference line X1Y1 is parallel to the cutting plane.");
                return E->Finish();
            }
        }
    }
    void AskXY() {
        m_step = XY;
        E->Ask(Pick(L"Click the XY line (or press Enter to measure from the section itself)", kAcceptEmpty));
    }
    void AskPlace() {
        m_step = Place;
        E->Ask(P(L"Click where the true shape goes (the new reference line X1Y1 passes here)", kAcceptPoint));
    }
    void OnKeyword(const std::wstring& k) override {
        if (m_step == Section && k == L"P") {
            m_step = SectionPoints;
            m_section.clear();
            m_curved = false;
            E->Ask(P(L"Click the first corner of the section", kAcceptPoint));
        }
    }
    void OnEnter() override {
        if (m_step == SectionPoints) {
            if (m_section.size() < 3) return E->Msg(L"Click three corners or more.");
            return AskXY();
        }
        if (m_step == XY) return AskPlace();
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_step != Place) return;
        TrueShapeResult r;
        std::wstring why;
        if (Compute(c, r, why)) out.entities = Build(r, false);
    }
};

// ---------------------------------------------------------------------------
// FIRSTANGLE: the first-angle projection symbol
// ---------------------------------------------------------------------------
class FirstAngleCmd : public Command {
    double m_size = 20;

public:
    void Start() override {
        E->Ask(P(L"Click where the first-angle symbol goes (its bottom left)", kAcceptPoint | kAcceptNumber, {},
                 L""));
        E->Msg(L"Type a number first to change its size (now " + E->Len(m_size) + L" across the large circle).");
    }
    void OnNumber(double v) override {
        if (!Positive(E, v)) return;
        m_size = v;
        Start();
    }
    void OnPoint(Vec2 p) override {
        std::vector<Entity> ents = FirstAngleSymbol(p, m_size);
        TextData t;
        t.content = L"FIRST ANGLE PROJECTION";
        t.height = std::max(2.5, m_size / 8);
        t.justify = 1;
        ents.push_back(MakeText({p.x + m_size * 1.25, p.y}, t));
        AddAll(E, L"First-angle symbol", ents);
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override { out.entities = FirstAngleSymbol(c, m_size); }
};

// ---------------------------------------------------------------------------
// BISSETUP: units, dimensions, text and layers as BIS SP 46 asks
// ---------------------------------------------------------------------------
class BisSetupCmd : public Command {
public:
    void Start() override {
        if (E->model) {
            E->Msg(L"BIS setup is for the drawing itself: click the Drawing tab first (then TITLEBLOCK on each sheet).");
            return E->Finish();
        }
        E->Ask(P(L"Dimensions in the", kAcceptKeyword | kAcceptEmpty,
                 {{L"A", L"Aligned system"}, {L"U", L"Unidirectional system"}}, L"Aligned"));
    }
    void OnKeyword(const std::wstring& k) override { Apply(k == L"U"); }
    void OnEnter() override { Apply(false); }
    void Apply(bool unidirectional) {
        ApplyBisSetup(E->Doc(), unidirectional);
        E->Msg(std::wstring(L"BIS (SP 46) setup: millimetres; dimension text 3.5 high, ") +
               (unidirectional ? L"unidirectional (always horizontal)" : L"aligned (along the dimension line)") +
               L"; layers for visible (0.5), hidden, centre, dimension, section, construction and cutting-plane lines; "
               L"a BIS title block on new sheets (TITLEBLOCK puts it on a sheet). FIRSTANGLE draws the projection symbol.");
        E->Finish();
    }
};
}  // namespace

std::unique_ptr<Command> CreateEgCommand(const std::wstring& name) {
    if (name == L"DIVIDE") return std::make_unique<DivideCmd>(false);
    if (name == L"MEASURE") return std::make_unique<DivideCmd>(true);
    if (name == L"ISODRAFT") return std::make_unique<IsoDraftCmd>();
    if (name == L"ISOPLANE") return std::make_unique<IsoPlaneCmd>();
    if (name == L"ISOCIRCLE") return std::make_unique<IsoCircleCmd>();
    if (name == L"ISOSCALE") return std::make_unique<IsoScaleCmd>();
    if (name == L"DEVELOP") return std::make_unique<DevelopCmd>();
    if (name == L"TRUELENGTH") return std::make_unique<TrueLengthCmd>();
    if (name == L"CUTPLANE") return std::make_unique<CutPlaneCmd>();
    if (name == L"TRUESHAPE") return std::make_unique<TrueShapeCmd>();
    if (name == L"FIRSTANGLE") return std::make_unique<FirstAngleCmd>();
    if (name == L"BISSETUP") return std::make_unique<BisSetupCmd>();
    return nullptr;
}
