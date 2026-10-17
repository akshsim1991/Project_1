// CommandsCurves.cpp - SYMBOL and WIRE (Symbols.h); the engineering curves
// CONIC, CYCLOID, EPICYCLOID, HYPOCYCLOID, INVOLUTE, SPIRAL, HELIX and
// TANNORM (EngCurves.h).
#include <cmath>
#include <cwctype>
#include <functional>

#include "Annot.h"
#include "CommandUtil.h"
#include "EngCurves.h"
#include "EngGraphics.h"
#include "Symbols.h"

namespace {
double TextSize(Engine* E) { return E->Doc().Settings().dimStyle.textHeight; }

// "2/3" or "1.5": a number (fractions for eccentricities).
bool ReadFraction(const std::wstring& s, double& v) {
    const size_t slash = s.find(L'/');
    if (slash == std::wstring::npos) return false;
    try {
        size_t used = 0;
        const double a = std::stod(s.substr(0, slash), &used);
        if (used != slash) return false;
        const std::wstring rest = s.substr(slash + 1);
        const double b = std::stod(rest, &used);
        if (used != rest.size() || b == 0) return false;
        v = a / b;
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// SYMBOL: a symbol from the library, placed as often as wanted
// ---------------------------------------------------------------------------
class SymbolCmd : public Command {
    std::wstring m_name;
    std::shared_ptr<const BlockData> m_def;
    bool m_hasAttr = false, m_askedLabel = false;
    std::wstring m_label, m_tag;
    double m_rot = 0;

    void AskName() {
        Prompt p = TextPrompt(L"Type the symbol's name (such as RESISTOR, AND, PROCESS, ROUTER, DOOR), or ? to list them");
        p.accept = kAcceptEmpty;
        E->Ask(p);
    }
    void Chosen(const std::wstring& name) {
        Document& d = E->Doc();
        m_def = SymbolBlock(d, name);
        if (!m_def) {
            E->Msg(L"There is no symbol \"" + name + L"\". Type ? to list them.");
            return AskName();
        }
        m_name = name;
        for (const Entity& e : m_def->items)
            if (const TextData* t = TextOf(e); t && !t->tag.empty()) m_hasAttr = true, m_tag = t->tag;
        if (m_hasAttr) return AskLabel();
        AskPlace();
    }
    void AskLabel() {
        m_askedLabel = true;
        E->Ask(TextPrompt(m_tag == L"TEXT" ? L"Type the text inside it (Enter: none)" : L"Type its label, such as R1 or 10 k (Enter: none)"));
    }
    void AskPlace() {
        m_askedLabel = false;
        const SymbolInfo* info = FindSymbol(m_name);
        E->Ask(P((std::wstring(L"Click where the ") + (info ? info->title : m_name.c_str()) + L" goes").c_str(), kAcceptPoint | kAcceptEmpty,
                 {{L"R", L"Rotate 90\x00B0"}, {L"L", L"Label"}}));
    }
    Entity Make(Vec2 at) const {
        std::vector<std::wstring> values;
        if (m_hasAttr) values.push_back(m_label);
        return MakeInsert(m_def, at, 1, m_rot, values);
    }

public:
    void Start() override {
        std::wstring name;
        const int chosen = E->Host().ChooseSymbol(name);
        if (chosen == 1) return Chosen(name);
        if (chosen == 0) return E->Finish();  // cancelled
        AskName();
    }
    bool OnText(const std::wstring& s) override {
        if (m_def && !m_askedLabel) return false;
        if (m_askedLabel) {
            m_label = s;
            AskPlace();
            return true;
        }
        if (s == L"?") {
            for (const std::wstring& c : SymbolCategories()) {
                std::wstring line = c + L":";
                for (const SymbolInfo& i : SymbolLibrary())
                    if (c == i.category) line += L" " + SymbolBlockName(i);
                E->Msg(line);
            }
            AskName();
            return true;
        }
        Chosen(s);
        return true;
    }
    void OnKeyword(const std::wstring& k) override {
        if (k == L"R") {
            m_rot = std::fmod(m_rot + kPi / 2, kTau);
            return AskPlace();
        }
        if (k == L"L" && m_hasAttr) return AskLabel();
        AskPlace();
    }
    void OnEnter() override {
        if (m_askedLabel) {
            m_label.clear();
            return AskPlace();
        }
        E->Finish();
    }
    void OnPoint(Vec2 p) override {
        if (!m_def || m_askedLabel) return;
        AddKeepingStyle(E, L"Symbol", {Make(p)});
        if (!m_label.empty()) m_label = NextName(m_label);  // R1, R2...
        AskPlace();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_def && !m_askedLabel) out.entities.push_back(Make(c));
    }
};

// ---------------------------------------------------------------------------
// WIRE: connectors between symbols' terminals that stay joined
// ---------------------------------------------------------------------------
int g_arrow = 0, g_straight = 0;  // 0: as suits the symbols, 1: on, 2: off

class WireCmd : public Command {
    bool m_have = false;
    DimRef m_from;
    Vec2 m_a;

    double Tol() const { return std::max(E->Host().PickRadius() * 2, 0.5); }
    std::wstring Category(const DimRef& r) const {
        const Entity* e = r.id ? E->Doc().Find(r.id) : nullptr;
        if (!e || !e->annot || !e->annot->block) return L"";
        const std::wstring n = e->annot->block->name;
        if (n == L"NETWORK_SWITCH") return L"Network";
        const SymbolInfo* s = FindSymbol(n);
        return s ? s->category : L"";
    }
    bool Arrow(const DimRef& r) const { return g_arrow == 1 || (g_arrow == 0 && Category(r) == L"Flowchart"); }
    bool Straight(const DimRef& r) const { return g_straight == 1 || (g_straight == 0 && Category(r) == L"Network"); }
    void Ask() {
        static const wchar_t* const mode[] = {L"auto", L"on", L"off"};
        std::vector<Keyword> kw = {{L"S", std::wstring(L"Straight: ") + mode[g_straight]}, {L"A", std::wstring(L"Arrow: ") + mode[g_arrow]}};
        if (!m_have) E->Ask(P(L"Click the first terminal (the end of a lead) or any point", kAcceptPoint | kAcceptEmpty, kw));
        else E->Ask(From(P(L"Click the other terminal or point", kAcceptPoint | kAcceptEmpty, kw), m_a));
    }
    Entity Make(Vec2 b, const DimRef& to) const {
        const DimRef& any = m_from.id ? m_from : to;
        return MakeConnector(E->Doc(), m_from, m_a, to, b, Straight(any), Arrow(any) ? E->Doc().Settings().dimStyle.arrowSize : 0);
    }

public:
    void Start() override { Ask(); }
    void OnKeyword(const std::wstring& k) override {
        int& m = k == L"S" ? g_straight : g_arrow;
        m = (m + 1) % 3;
        Ask();
    }
    void OnPoint(Vec2 p) override {
        Vec2 at = p;
        const DimRef port = NearestPort(E->Doc(), p, Tol(), &at);
        if (!m_have) {
            m_have = true;
            m_from = port;
            m_a = at;
            if (!port.id) E->Msg(L"Not on a terminal: this end stays where it is.");
            return Ask();
        }
        if (Near(at, m_a, 1e-9)) return;
        AddKeepingStyle(E, L"Wire", {Make(at, port)});
        if (m_from.id || port.id) E->Msg(L"Wire drawn. It stays joined when the symbols move.");
        m_have = false;
        m_from = DimRef{};
        Ask();
    }
    void OnEnter() override { E->Finish(); }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_have) out.entities.push_back(Make(c, DimRef{}));
    }
};

// ---------------------------------------------------------------------------
// The curves: a few sizes asked in turn (Enter keeps the last), then a click
// to place the construction, previewed at the cursor.
// ---------------------------------------------------------------------------
class CurveCmd : public Command {
protected:
    struct Q {
        std::wstring text;
        double* value;
        double min;  // the value must be at least this (and more than it when strict)
        bool strict;
    };
    std::vector<Q> m_qs;
    size_t m_i = 0;

    virtual bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) = 0;
    virtual std::wstring PlaceText() const = 0;
    virtual const wchar_t* Step() const = 0;
    virtual void Prepare() {}  // after the answers, before placing
    void Ask() {
        if (m_i < m_qs.size()) {
            const Q& q = m_qs[m_i];
            Prompt p = P(q.text.c_str(), kAcceptNumber | kAcceptEmpty, {}, E->Len(*q.value));
            return E->Ask(p);
        }
        Prepare();
        E->Ask(P(PlaceText().c_str(), kAcceptPoint));
    }

public:
    void Start() override { Ask(); }
    void OnNumber(double v) override {
        if (m_i >= m_qs.size()) return;
        const Q& q = m_qs[m_i];
        if (v < q.min || (q.strict && v <= q.min)) return E->Msg(L"That value is too small.");
        *q.value = v;
        ++m_i;
        Ask();
    }
    bool OnText(const std::wstring& s) override {
        double v;
        if (m_i < m_qs.size() && ReadFraction(s, v)) {
            OnNumber(v);
            return true;
        }
        return false;
    }
    void OnEnter() override {
        if (m_i < m_qs.size()) {
            ++m_i;
            return Ask();
        }
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
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        if (m_i < m_qs.size()) return;
        std::wstring why;
        Build(c, out.entities, why);
    }
};

double g_conicD = 50, g_conicE = 2.0 / 3;
class ConicCmd : public CurveCmd {
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        return ConicCurve(at, g_conicD, g_conicE, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click a point on the directrix (where the axis crosses it)"; }
    const wchar_t* Step() const override { return L"Conic"; }

public:
    ConicCmd() {
        m_qs = {{L"Type the distance of the focus from the directrix", &g_conicD, 0, true},
                {L"Type the eccentricity (less than 1: ellipse, 1: parabola, more: hyperbola; 2/3 is fine)", &g_conicE, 0, true}};
    }
};

double g_rollDia = 50, g_baseDia = 150;
class RollCmd : public CurveCmd {
    Roll m_kind;
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        return RollingCurve(m_kind, at, g_rollDia, g_baseDia, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override {
        return m_kind == Roll::Cycloid ? L"Click where the circle starts rolling (the point P on the line)"
                                       : L"Click the centre of the base circle";
    }
    const wchar_t* Step() const override {
        return m_kind == Roll::Cycloid ? L"Cycloid" : m_kind == Roll::Epicycloid ? L"Epicycloid" : L"Hypocycloid";
    }

public:
    explicit RollCmd(Roll k) : m_kind(k) {
        m_qs = {{L"Type the diameter of the rolling circle", &g_rollDia, 0, true}};
        if (k != Roll::Cycloid) m_qs.push_back({L"Type the diameter of the base (directing) circle", &g_baseDia, 0, true});
    }
};

double g_invSides = 0, g_invSize = 40;
class InvoluteCmd : public CurveCmd {
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        return InvoluteCurve(at, (int)std::lround(g_invSides), g_invSize, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click the centre of the circle (or polygon)"; }
    const wchar_t* Step() const override { return L"Involute"; }
    void Prepare() override {}

public:
    InvoluteCmd() {
        m_qs = {{L"Type the number of sides (0: the involute of a circle)", &g_invSides, 0, false},
                {L"Type the diameter of the circle (or the side of the polygon)", &g_invSize, 0, true}};
    }
};

double g_spR0 = 10, g_spR1 = 70, g_spTurns = 1;
class SpiralCmd : public CurveCmd {
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        return SpiralCurve(at, g_spR0, g_spR1, g_spTurns, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click the pole (the centre)"; }
    const wchar_t* Step() const override { return L"Spiral"; }

public:
    SpiralCmd() {
        m_qs = {{L"Type the starting distance from the pole", &g_spR0, 0, false},
                {L"Type the final distance from the pole", &g_spR1, 0, true},
                {L"Type the number of turns (convolutions)", &g_spTurns, 0, true}};
    }
};

double g_hxDia = 50, g_hxPitch = 60, g_hxTurns = 1;
class HelixCmd : public CurveCmd {
    bool Build(Vec2 at, std::vector<Entity>& out, std::wstring& why) override {
        return HelixCurve(at, g_hxDia, g_hxPitch, g_hxTurns, TextSize(E), out, why);
    }
    std::wstring PlaceText() const override { return L"Click the bottom of the axis in the front view"; }
    const wchar_t* Step() const override { return L"Helix"; }

public:
    HelixCmd() {
        m_qs = {{L"Type the diameter of the cylinder", &g_hxDia, 0, true},
                {L"Type the pitch (the rise in one turn)", &g_hxPitch, 0, true},
                {L"Type the number of turns", &g_hxTurns, 0, true}};
    }
};

// TANNORM: the tangent and the normal at a point of a curve.
double g_tnLength = 30;
class TanNormCmd : public Command {
    uint64_t m_id = 0;

public:
    void Start() override { E->Ask(Pick(L"Click the curve", kAcceptNumber)); }
    void OnNumber(double v) override {
        if (!Positive(E, v)) return;
        g_tnLength = v;
        E->Msg(L"The tangent and normal will be " + E->Len(2 * v) + L" long.");
    }
    void OnPoint(Vec2 p) override {
        if (!m_id) {
            const uint64_t id = E->PickEntity(p, true);
            const Entity* e = id ? E->Doc().Find(id) : nullptr;
            if (!e || !Dividable(*e)) return E->Msg(L"Click a curve: an ellipse, spline, arc, circle, line or polyline.");
            m_id = id;
            return E->Ask(P(L"Click the point on the curve for the tangent and normal (type a number first to change their length)",
                            kAcceptPoint | kAcceptNumber));
        }
        const Entity* e = E->Doc().Find(m_id);
        std::vector<Entity> out;
        Vec2 at;
        if (!e || !TangentNormal(*e, p, g_tnLength, TextSize(E), out, &at)) return E->Finish();
        AddKeepingStyle(E, L"Tangent and normal", out);
        E->Msg(L"Tangent T-T and normal N-N drawn at " + E->Pt(at) + L".");
        E->Finish();
    }
    void GetPreview(Vec2 c, Preview& out) override {
        const Entity* e = m_id ? E->Doc().Find(m_id) : nullptr;
        if (e) TangentNormal(*e, c, g_tnLength, TextSize(E), out.entities);
    }
};
}  // namespace

std::unique_ptr<Command> CreateCurvesCommand(const std::wstring& name) {
    if (name == L"SYMBOL") return std::make_unique<SymbolCmd>();
    if (name == L"WIRE") return std::make_unique<WireCmd>();
    if (name == L"CONIC") return std::make_unique<ConicCmd>();
    if (name == L"CYCLOID") return std::make_unique<RollCmd>(Roll::Cycloid);
    if (name == L"EPICYCLOID") return std::make_unique<RollCmd>(Roll::Epicycloid);
    if (name == L"HYPOCYCLOID") return std::make_unique<RollCmd>(Roll::Hypocycloid);
    if (name == L"INVOLUTE") return std::make_unique<InvoluteCmd>();
    if (name == L"SPIRAL") return std::make_unique<SpiralCmd>();
    if (name == L"HELIX") return std::make_unique<HelixCmd>();
    if (name == L"TANNORM") return std::make_unique<TanNormCmd>();
    return nullptr;
}
