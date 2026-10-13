// Engine.cpp - input handling for the command engine (see Engine.h).
// The commands themselves are in Commands.cpp.
#include "Engine.h"

#include <algorithm>
#include <set>
#include <cwctype>

#include "EngGraphics.h"

namespace {
std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::iswspace(s[b])) ++b;
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring Upper(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)std::towupper(c);
    return s;
}

// Numbers are typed with a dot as the decimal separator, as in every CAD
// program (the comma separates X and Y).
bool ParseNum(const std::wstring& w, double& v) {
    if (w.empty() || w.size() > 64) return false;
    std::string s;
    for (wchar_t c : w) {
        if (c > 127) return false;
        s.push_back((char)c);
    }
    return ParseNumber(s, v);
}

constexpr unsigned kSnapModesList[] = {kSnapEndpoint, kSnapMidpoint, kSnapCentre, kSnapQuadrant,
                                       kSnapIntersection, kSnapPerpendicular, kSnapTangent, kSnapNearest};

// Ends a command object only after the call that finished it has returned.
struct DeferredDelete {
    std::unique_ptr<Command>& dead;
    ~DeferredDelete() { dead.reset(); }
};
std::unique_ptr<Command> g_dead;
}  // namespace

std::wstring FormatDecimal(double v, int precision) {
    precision = std::clamp(precision, 0, 10);
    wchar_t buf[64];
    swprintf(buf, 64, L"%.*f", precision, v);
    std::wstring s = buf;
    if (s[0] == L'-' && s.find_first_not_of(L"-0.") == std::wstring::npos) s.erase(0, 1);
    return s;
}

std::wstring FormatDegrees(double radians, int precision) {
    double deg = ToDegrees(radians);
    std::wstring s = FormatDecimal(deg, std::max(precision, 0));
    if (s.find(L'.') != std::wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    if (s == L"360") s = L"0";
    return s;
}

std::wstring Prompt::FullText() const {
    std::wstring s = text;
    if (!keywords.empty()) {
        s += L" or [";
        for (size_t i = 0; i < keywords.size(); ++i) {
            if (i) s += L"/";
            s += keywords[i].name;
        }
        s += L"]";
    }
    if (!defaultText.empty()) s += L" <" + defaultText + L">";
    s += L":";
    return s;
}

const CommandInfo* FindCommand(const std::wstring& nameOrAlias) {
    const std::wstring u = Upper(Trim(nameOrAlias));
    for (const CommandInfo& c : AllCommands()) {
        if (c.name == u) return &c;
        for (const std::wstring& a : c.aliases)
            if (a == u) return &c;
    }
    return nullptr;
}

Engine::Engine(Document& doc, EngineHost& host) : m_doc(doc), m_host(host) {}
Engine::~Engine() = default;

std::wstring Engine::Len(double v) const { return FormatDecimal(v, m_doc.Settings().precision); }
std::wstring Engine::Ang(double rad) const { return FormatDegrees(rad, m_doc.Settings().precision); }
std::wstring Engine::Pt(Vec2 p) const {
    const Vec2 o = m_doc.Settings().origin;  // shown from the grid origin
    return Len(p.x - o.x) + L"," + Len(p.y - o.y);
}

void Engine::Echo(const std::wstring& typed) {
    m_host.Message((m_cmd ? m_prompt.FullText() : std::wstring(L"Command:")) + L" " + typed);
}

void Engine::Run(const std::wstring& name) {
    DeferredDelete guard{g_dead};
    const CommandInfo* info = FindCommand(name);
    if (!info) {
        m_host.Message(L"Unknown command \"" + Upper(Trim(name)) +
                       L"\". Press Ctrl+K to search the commands.");
        return;
    }
    if (m_cmd) Cancel();
    m_host.Message(L"Command: " + info->name);
    if (info->host) {
        m_host.HostCommand(info->name);
        return;
    }
    Start(*info);
}

void Engine::StartGripEdit(const Grip& hot) {
    DeferredDelete guard{g_dead};
    if (m_cmd || m_selection.empty()) return;
    // A dynamic block's flip grip: one click flips it over.
    if (hot.index >= 200) {
        const Entity* e = m_doc.Find(hot.id);
        if (e && e->type == EntityType::Insert) {
            Entity x = *e;
            const size_t k = (size_t)(hot.index - 200);
            if (SetParamValue(x, k, ParamValue(x, k) != 0 ? 0 : 1)) {
                m_doc.BeginChange(L"Flip");
                m_doc.Replace(x);
                m_doc.EndChange();
            }
            return;
        }
    }
    m_cmd = CreateGripEdit({m_selection.begin(), m_selection.end()}, hot);
    m_cmdName = L"GRIP";
    m_cmd->E = this;
    m_prompt = Prompt{};
    m_cmd->Start();
}

void Engine::Start(const CommandInfo& info) {
    m_lastCommand = info.name;
    if (info.name != L"UNDO" && info.name != L"REDO") {
        m_recent.erase(std::remove(m_recent.begin(), m_recent.end(), info.name), m_recent.end());
        m_recent.insert(m_recent.begin(), info.name);
        if (m_recent.size() > 8) m_recent.resize(8);
    }
    m_cmd = CreateCommand(info.name);
    if (!m_cmd) return;
    m_cmdName = info.name;
    m_cmd->E = this;
    m_prompt = Prompt{};
    m_cmd->Start();
}

// Prompts in plain words: "Click the first point", "Type the offset
// distance" (rather than "Specify..." and "Enter...", which beginners read
// as the Enter key).
std::wstring PlainPrompt(const std::wstring& text, unsigned accept) {
    auto starts = [&](const wchar_t* w) { return text.rfind(w, 0) == 0; };
    auto rest = [&](size_t n) {
        std::wstring r = text.substr(n);
        if (r.rfind(L"the ", 0) == 0) r.erase(0, 4);
        return r;
    };
    const bool point = (accept & kAcceptPoint) != 0, number = (accept & kAcceptNumber) != 0;
    if (starts(L"Specify ")) {
        if (point && number) return L"Click or type the " + rest(8);
        if (point) return L"Click the " + rest(8);
        return L"Type the " + rest(8);
    }
    if (starts(L"Enter ")) return L"Type " + text.substr(6);
    if (starts(L"Select ")) return L"Click " + text.substr(7);
    return text;
}

void Engine::Ask(Prompt p) {
    p.text = PlainPrompt(p.text, p.accept);
    m_prompt = std::move(p);
}

void Engine::AskSelection() {
    if (!m_selection.empty()) {
        m_selecting = false;
        m_cmd->OnSelected();
        return;
    }
    m_selecting = true;
    Prompt p;
    p.text = L"Click the objects to use (or drag a box around them), then press Enter";
    p.accept = kAcceptSelect | kAcceptEmpty;
    m_prompt = p;
}

void Engine::Finish() {
    if (m_doc.InChange()) m_doc.EndChange();
    g_dead = std::move(m_cmd);
    m_cmdName.clear();
    m_prompt = Prompt{};
    m_selecting = false;
    m_panMode = false;
    m_snapOverride = 0;
}

void Engine::Accepted(Vec2 p) {
    m_lastPoint = p;
    m_snapOverride = 0;
    m_cmd->OnPoint(p);
}

void Engine::Point(Vec2 p, bool shift) {
    DeferredDelete guard{g_dead};
    if (!m_cmd || m_selecting || !(m_prompt.accept & kAcceptPoint)) return;
    m_lastShift = shift;
    Accepted(p);
}

void Engine::Stroke(const std::vector<Vec2>& pts) {
    DeferredDelete guard{g_dead};
    if (!WantsStroke()) return;
    m_cmd->OnStroke(pts);
}

uint64_t Engine::PickEntity(Vec2 p, bool lockedToo) const {
    const double tol = m_host.PickRadius();
    const Box box = Box(p, p).Inflated(tol);
    double best = tol;
    uint64_t id = 0;
    for (const auto& kv : m_doc.Entities()) {
        if (lockedToo ? !m_doc.Visible(kv.second) : !m_doc.Editable(kv.second)) continue;
        if (!Bounds(kv.second).Intersects(box)) continue;
        const double d = DistanceTo(kv.second, p);
        if (d <= best) {  // the later (top) object wins ties
            best = d;
            id = kv.first;
        }
    }
    return id;
}

void Engine::Enter() {
    DeferredDelete guard{g_dead};
    if (!m_cmd) {
        if (!m_lastCommand.empty()) Run(m_lastCommand);
        return;
    }
    if (m_selecting) {
        m_selecting = false;
        if (m_selection.empty()) {
            if (m_cmd->OnNothingSelected()) return;
            m_host.Message(L"Nothing selected.");
            Finish();
            return;
        }
        m_cmd->OnSelected();
        return;
    }
    m_cmd->OnEnter();
}

void Engine::Cancel() {
    DeferredDelete guard{g_dead};
    if (!m_cmd) {
        ClearSelection();
        return;
    }
    m_cmd->OnCancel();
    m_host.Message(L"*Cancel*");
    Finish();
}

void Engine::Keyword(const std::wstring& key) {
    DeferredDelete guard{g_dead};
    if (!m_cmd) return;
    Echo(key);
    m_cmd->OnKeyword(key);
}

bool Engine::MatchKeyword(const std::wstring& text, std::wstring& key) const {
    const std::wstring u = Upper(text);
    for (const ::Keyword& k : m_prompt.keywords) {
        const std::wstring name = Upper(k.name), kk = Upper(k.key);
        if (u == kk || (u.size() >= kk.size() && name.compare(0, u.size(), u) == 0)) {
            key = k.key;
            return true;
        }
    }
    return false;
}

bool Engine::ParsePoint(const std::wstring& raw, Vec2& out) const {
    std::wstring s;
    for (wchar_t c : raw)
        if (!std::iswspace(c)) s.push_back(c);
    if (s.empty()) return false;
    bool relative = false;
    if (s[0] == L'@') {
        relative = true;
        s.erase(0, 1);
        if (s.empty()) {
            out = m_lastPoint;
            return true;
        }
    }
    if (s[0] == L'#') s.erase(0, 1);  // absolute, as typed with dynamic input elsewhere
    const Vec2 origin = relative ? m_lastPoint : m_doc.Settings().origin;
    size_t lt = s.find(L'<'), comma = s.find(L',');
    double a, b;
    if (lt != std::wstring::npos) {
        if (!ParseNum(s.substr(0, lt), a) || !ParseNum(s.substr(lt + 1), b)) return false;
        if (m_draft.isoOn && m_draft.isoScale && IsIsoAxis(ToRadians(b))) a *= kIsoScale;  // isometric length
        out = Polar(origin, a, ToRadians(b));
        return true;
    }
    if (comma != std::wstring::npos) {
        if (!ParseNum(s.substr(0, comma), a) || !ParseNum(s.substr(comma + 1), b)) return false;
        out = origin + Vec2{a, b};
        return true;
    }
    return false;
}

void Engine::Text(const std::wstring& typedRaw) {
    DeferredDelete guard{g_dead};
    const std::wstring typed = Trim(typedRaw);
    if (!m_cmd) {
        if (typed.empty()) {
            Enter();
            return;
        }
        Run(typed);
        return;
    }
    if (typed.empty()) {
        Enter();
        return;
    }
    Echo(typed);
    const std::wstring u = Upper(typed);

    if (m_selecting) {
        if (u == L"ALL") {
            SelectAll();
        } else {
            m_host.Message(L"Pick objects in the drawing, type ALL, or press Enter when done.");
        }
        return;
    }
    // One-shot object snap: END, MID, CEN...
    if (m_prompt.accept & kAcceptPoint) {
        if (u == L"NON" || u == L"NONE") {
            m_snapOverride = kSnapNone;  // no snap for the next point
            return;
        }
        for (unsigned mode : kSnapModesList) {
            if (u == SnapKeyword(mode) || u == Upper(SnapName(mode))) {
                m_snapOverride = mode;
                m_host.Message(std::wstring(SnapName(mode)) + L" snap for the next point.");
                return;
            }
        }
    }
    if (m_cmd->OnText(typed)) return;
    std::wstring key;
    if ((m_prompt.accept & kAcceptKeyword) && MatchKeyword(typed, key)) {
        m_cmd->OnKeyword(key);
        return;
    }
    Vec2 p;
    if ((m_prompt.accept & kAcceptPoint) && ParsePoint(typed, p)) {
        m_lastShift = false;
        Accepted(p);
        return;
    }
    double v;
    if (ParseNum(typed, v)) {
        if (m_prompt.accept & kAcceptNumber) {
            m_cmd->OnNumber(v);
            return;
        }
        if ((m_prompt.accept & kAcceptPoint) && m_prompt.hasBase) {
            // Direct distance: that far from the base, towards the cursor.
            Vec2 dir = m_host.TrackedCursor() - m_prompt.base;
            if (Length(dir) < 1e-12) dir = {1, 0};
            if (m_draft.isoOn && m_draft.isoScale && IsIsoAxis(AngleOf(dir))) v *= kIsoScale;  // isometric length
            Accepted(m_prompt.base + Normalized(dir) * v);
            return;
        }
    }
    if (m_prompt.accept & kAcceptPoint)
        m_host.Message(L"Point or option keyword required (for example 10,20  @5,0  @50<30).");
    else if (m_prompt.accept & kAcceptNumber)
        m_host.Message(L"A number or an option is required.");
    else
        m_host.Message(L"Invalid option keyword.");
}

void Engine::GetPreview(Vec2 cursor, Preview& out) {
    out.Clear();
    if (m_cmd && !m_selecting) m_cmd->GetPreview(cursor, out);
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

namespace {
// The objects, with the rest of any group one of them is in (GROUP).
std::vector<uint64_t> WithGroups(const Document& doc, const std::vector<uint64_t>& ids) {
    std::set<uint32_t> groups;
    for (uint64_t id : ids)
        if (const Entity* e = doc.Find(id); e && e->group) groups.insert(e->group);
    if (groups.empty()) return ids;
    std::vector<uint64_t> out = ids;
    std::set<uint64_t> have(ids.begin(), ids.end());
    for (const auto& kv : doc.Entities())
        if (kv.second.group && groups.count(kv.second.group) && !have.count(kv.first)) out.push_back(kv.first);
    return out;
}
}  // namespace

void Engine::AddToSelection(const std::vector<uint64_t>& picked) {
    const std::vector<uint64_t> ids = WithGroups(m_doc, picked);
    size_t added = 0;
    for (uint64_t id : ids) {
        const Entity* e = m_doc.Find(id);
        // Objects on locked, frozen or hidden layers cannot be selected.
        if (e && m_doc.Editable(*e) && m_selection.insert(id).second) ++added;
    }
    if (added) ++m_selRevision;
    if (m_selecting)
        m_host.Message(std::to_wstring(ids.size()) + L" found, " + std::to_wstring(m_selection.size()) + L" total");
}

void Engine::RemoveFromSelection(const std::vector<uint64_t>& picked) {
    const std::vector<uint64_t> ids = WithGroups(m_doc, picked);
    size_t removed = 0;
    for (uint64_t id : ids) removed += m_selection.erase(id);
    if (removed) ++m_selRevision;
    if (m_selecting && removed)
        m_host.Message(std::to_wstring(removed) + L" removed, " + std::to_wstring(m_selection.size()) + L" total");
}

void Engine::SelectAll() {
    std::vector<uint64_t> ids;
    ids.reserve(m_doc.Count());
    for (const auto& kv : m_doc.Entities()) ids.push_back(kv.first);
    AddToSelection(ids);
}

void Engine::ClearSelection() {
    if (m_selection.empty()) return;
    m_selection.clear();
    ++m_selRevision;
}

void Engine::PruneSelection() {
    for (auto it = m_selection.begin(); it != m_selection.end();) {
        if (!m_doc.Find(*it)) {
            it = m_selection.erase(it);
            ++m_selRevision;
        } else {
            ++it;
        }
    }
}
