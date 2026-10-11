// Document.cpp - see Document.h.
#include "Document.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <sstream>

namespace {
constexpr size_t kMaxUndo = 1000;
constexpr char kMagic[] = "FeatherCAD";
constexpr int kFormatVersion = 1;

const char* UnitsKey(Units u) {
    switch (u) {
        case Units::Unitless: return "none";
        case Units::Millimetres: return "mm";
        case Units::Centimetres: return "cm";
        case Units::Metres: return "m";
        case Units::Inches: return "in";
        case Units::Feet: return "ft";
    }
    return "mm";
}

int DxfInsUnits(Units u) {
    switch (u) {
        case Units::Unitless: return 0;
        case Units::Inches: return 1;
        case Units::Feet: return 2;
        case Units::Millimetres: return 4;
        case Units::Centimetres: return 5;
        case Units::Metres: return 6;
    }
    return 0;
}

std::vector<std::string> SplitWords(const std::string& line) {
    std::vector<std::string> words;
    std::istringstream in(line);
    std::string w;
    while (in >> w) words.push_back(w);
    return words;
}
}  // namespace

const wchar_t* UnitsName(Units u) {
    switch (u) {
        case Units::Unitless: return L"Unitless";
        case Units::Millimetres: return L"Millimetres";
        case Units::Centimetres: return L"Centimetres";
        case Units::Metres: return L"Metres";
        case Units::Inches: return L"Inches";
        case Units::Feet: return L"Feet";
    }
    return L"";
}

const wchar_t* UnitsSymbol(Units u) {
    switch (u) {
        case Units::Unitless: return L"";
        case Units::Millimetres: return L"mm";
        case Units::Centimetres: return L"cm";
        case Units::Metres: return L"m";
        case Units::Inches: return L"in";
        case Units::Feet: return L"ft";
    }
    return L"";
}

std::string FormatNumber(double v) {
    if (v == 0 || !std::isfinite(v)) return "0";
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, res.ptr);
}

bool ParseNumber(const std::string& s, double& v) {
    const char* b = s.data();
    const char* e = b + s.size();
    if (b != e && *b == '+') ++b;
    auto res = std::from_chars(b, e, v);
    return res.ec == std::errc() && res.ptr == e && std::isfinite(v);
}

// ---------------------------------------------------------------------------
// Changes and undo
// ---------------------------------------------------------------------------

const Entity* Document::Find(uint64_t id) const {
    auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : &it->second;
}

void Document::BeginChange(const std::wstring& name) {
    if (m_open) EndChange();
    m_open = Step{name, {}, 0};
    m_openIndex.clear();
}

void Document::EndChange() {
    if (!m_open) return;
    Step step = std::move(*m_open);
    m_open.reset();
    m_openIndex.clear();
    // An object added and erased again within the group leaves no trace.
    step.changes.erase(std::remove_if(step.changes.begin(), step.changes.end(),
                                      [](const Change& c) { return !c.before && !c.after; }),
                       step.changes.end());
    if (step.changes.empty()) return;
    step.serial = m_nextSerial++;
    m_undo.push_back(std::move(step));
    if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void Document::CancelChange() {
    if (!m_open) return;
    Step step = std::move(*m_open);
    m_open.reset();
    m_openIndex.clear();
    Apply(step, false);
}

void Document::Record(uint64_t id, const Entity* before, const Entity* after) {
    auto it = m_openIndex.find(id);
    if (it != m_openIndex.end()) {
        Change& c = m_open->changes[it->second];
        if (after) c.after = *after;
        else c.after.reset();
        return;
    }
    Change c{id, {}, {}};
    if (before) c.before = *before;
    if (after) c.after = *after;
    m_openIndex[id] = m_open->changes.size();
    m_open->changes.push_back(std::move(c));
}

uint64_t Document::Add(Entity e) {
    const bool implicit = !m_open;
    if (implicit) BeginChange(L"Add");
    const uint64_t id = m_nextId++;
    e.id = id;
    Entity& stored = m_entities[id];
    stored = std::move(e);
    Record(id, nullptr, &stored);
    Touch();
    if (implicit) EndChange();
    return id;
}

void Document::Erase(uint64_t id) {
    auto it = m_entities.find(id);
    if (it == m_entities.end()) return;
    const bool implicit = !m_open;
    if (implicit) BeginChange(L"Erase");
    Record(id, &it->second, nullptr);
    m_entities.erase(it);
    Touch();
    if (implicit) EndChange();
}

void Document::Replace(const Entity& e) {
    auto it = m_entities.find(e.id);
    if (it == m_entities.end()) return;
    const bool implicit = !m_open;
    if (implicit) BeginChange(L"Change");
    Record(e.id, &it->second, &e);
    it->second = e;
    Touch();
    if (implicit) EndChange();
}

void Document::ReplaceAll(std::vector<Entity> entities, const std::wstring& name) {
    BeginChange(name);
    std::vector<uint64_t> ids;
    ids.reserve(m_entities.size());
    for (const auto& kv : m_entities) ids.push_back(kv.first);
    for (uint64_t id : ids) Erase(id);
    for (Entity& e : entities) Add(std::move(e));
    EndChange();
}

void Document::Apply(const Step& step, bool forward) {
    // Backwards in reverse order, so repeated ids end in the right state.
    const size_t n = step.changes.size();
    for (size_t k = 0; k < n; ++k) {
        const Change& c = step.changes[forward ? k : n - 1 - k];
        const std::optional<Entity>& target = forward ? c.after : c.before;
        if (target) m_entities[c.id] = *target;
        else m_entities.erase(c.id);
    }
    Touch();
}

bool Document::Undo() {
    if (m_open) EndChange();
    if (m_undo.empty()) return false;
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    Apply(step, false);
    m_redo.push_back(std::move(step));
    return true;
}

bool Document::Redo() {
    if (m_open) EndChange();
    if (m_redo.empty()) return false;
    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    Apply(step, true);
    m_undo.push_back(std::move(step));
    return true;
}

bool Document::Modified() const {
    const uint64_t top = m_undo.empty() ? 0 : m_undo.back().serial;
    return top != m_savedSerial || m_settingsChanged;
}

void Document::MarkSaved() {
    m_savedSerial = m_undo.empty() ? 0 : m_undo.back().serial;
    m_settingsChanged = false;
}

void Document::SetSettings(const DrawingSettings& s) {
    if (s.units == m_settings.units && s.precision == m_settings.precision) return;
    m_settings = s;
    m_settings.precision = std::clamp(m_settings.precision, 0, 8);
    m_settingsChanged = true;
    Touch();
}

void Document::Clear() {
    m_open.reset();
    m_openIndex.clear();
    m_entities.clear();
    m_undo.clear();
    m_redo.clear();
    m_nextId = 1;
    m_savedSerial = 0;
    m_settingsChanged = false;
    Touch();
}

Box Document::Extents() const {
    if (m_extentsRevision != m_revision) {
        m_extents = Box();
        for (const auto& kv : m_entities) m_extents.Add(Bounds(kv.second));
        m_extentsRevision = m_revision;
    }
    return m_extents;
}

// ---------------------------------------------------------------------------
// Feather CAD file format
//
//   FeatherCAD 1
//   units mm
//   precision 2
//   line x1 y1 x2 y2
//   circle cx cy r
//   arc cx cy r start end              (degrees, counter-clockwise)
//   pline closed n x y bulge x y bulge ...
//   end
//
// Lines starting with # are comments. Unknown keywords are skipped, so a
// later version can add objects without breaking older readers.
// ---------------------------------------------------------------------------

std::string Document::ToText() const {
    std::string out;
    out.reserve(64 + m_entities.size() * 48);
    out += kMagic;
    out += " " + std::to_string(kFormatVersion) + "\n";
    out += std::string("units ") + UnitsKey(m_settings.units) + "\n";
    out += "precision " + std::to_string(m_settings.precision) + "\n";
    auto num = [&](double v) {
        out += ' ';
        out += FormatNumber(v);
    };
    for (const auto& kv : m_entities) {
        const Entity& e = kv.second;
        switch (e.type) {
            case EntityType::Line:
                out += "line";
                num(e.p0.x); num(e.p0.y); num(e.p1.x); num(e.p1.y);
                break;
            case EntityType::Circle:
                out += "circle";
                num(e.p0.x); num(e.p0.y); num(e.radius);
                break;
            case EntityType::Arc:
                out += "arc";
                num(e.p0.x); num(e.p0.y); num(e.radius);
                num(ToDegrees(e.startAngle)); num(ToDegrees(e.endAngle));
                break;
            case EntityType::Polyline:
                out += "pline ";
                out += e.closed ? "1 " : "0 ";
                out += std::to_string(e.verts.size());
                for (const Vertex& v : e.verts) {
                    num(v.p.x); num(v.p.y); num(v.bulge);
                }
                break;
        }
        out += '\n';
    }
    out += "end\n";
    return out;
}

bool Document::FromText(const std::string& text, std::wstring& error) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::wstring& why) {
        error = L"Line " + std::to_wstring(lineNo) + L": " + why;
        return false;
    };
    // Header.
    while (std::getline(in, line)) {
        ++lineNo;
        auto w = SplitWords(line);
        if (w.empty() || w[0][0] == '#') continue;
        if (w[0] != kMagic) {
            error = L"This is not a Feather CAD drawing.";
            return false;
        }
        // Newer versions are read as far as this one understands them.
        break;
    }
    if (lineNo == 0) {
        error = L"The file is empty.";
        return false;
    }

    DrawingSettings settings;
    std::vector<Entity> ents;
    bool ended = false;
    while (!ended && std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto w = SplitWords(line);
        if (w.empty() || w[0][0] == '#') continue;
        const std::string& key = w[0];
        // Numbers after the keyword; key=value attributes are ignored here.
        std::vector<double> v;
        for (size_t i = 1; i < w.size(); ++i) {
            if (w[i].find('=') != std::string::npos) break;
            double d;
            if (!ParseNumber(w[i], d)) {
                if (key == "units") break;
                return fail(L"bad number \"" + std::wstring(w[i].begin(), w[i].end()) + L"\"");
            }
            v.push_back(d);
        }
        auto need = [&](size_t n) { return v.size() >= n; };
        if (key == "end") {
            ended = true;
        } else if (key == "units") {
            const std::string u = w.size() > 1 ? w[1] : "";
            for (Units cand : {Units::Unitless, Units::Millimetres, Units::Centimetres, Units::Metres,
                               Units::Inches, Units::Feet})
                if (u == UnitsKey(cand)) settings.units = cand;
        } else if (key == "precision") {
            if (need(1)) settings.precision = std::clamp((int)v[0], 0, 8);
        } else if (key == "line") {
            if (!need(4)) return fail(L"a line needs 4 numbers");
            ents.push_back(Entity::Line({v[0], v[1]}, {v[2], v[3]}));
        } else if (key == "circle") {
            if (!need(3) || v[2] <= 0) return fail(L"a circle needs a centre and a positive radius");
            ents.push_back(Entity::Circle({v[0], v[1]}, v[2]));
        } else if (key == "arc") {
            if (!need(5) || v[2] <= 0) return fail(L"an arc needs a centre, a positive radius and two angles");
            ents.push_back(Entity::Arc({v[0], v[1]}, v[2], ToRadians(v[3]), ToRadians(v[4])));
        } else if (key == "pline") {
            if (!need(2)) return fail(L"a polyline needs its flags and vertex count");
            const size_t n = (size_t)v[1];
            if (v[1] < 1 || v[1] > 1e7 || v.size() != 2 + 3 * n) return fail(L"wrong number of polyline vertices");
            std::vector<Vertex> verts(n);
            for (size_t i = 0; i < n; ++i) verts[i] = {{v[2 + 3 * i], v[3 + 3 * i]}, v[4 + 3 * i]};
            ents.push_back(Entity::Polyline(std::move(verts), v[0] != 0));
        }
        // Anything else: from a newer version, skipped.
    }
    if (!ended) {
        error = L"The file is incomplete (it may have been cut off while saving).";
        return false;
    }
    Clear();
    m_settings = settings;
    for (Entity& e : ents) {
        e.id = m_nextId++;
        m_entities[e.id] = std::move(e);
    }
    Touch();
    return true;
}

// ---------------------------------------------------------------------------
// DXF (release 12, AC1009): HEADER, TABLES (line type and layer 0) and
// ENTITIES. Polylines become POLYLINE/VERTEX/SEQEND with bulges.
// ---------------------------------------------------------------------------

std::string Document::ToDxf() const {
    std::string out;
    out.reserve(1024 + m_entities.size() * 120);
    auto group = [&](int code, const std::string& value) {
        const std::string c = std::to_string(code);
        out.append(c.size() < 3 ? 3 - c.size() : 0, ' ');
        out += c;
        out += "\r\n";
        out += value;
        out += "\r\n";
    };
    auto num = [&](int code, double v) { group(code, FormatNumber(v)); };
    auto point = [&](int code, Vec2 p) {
        num(code, p.x);
        num(code + 10, p.y);
        num(code + 20, 0.0);
    };

    group(0, "SECTION");
    group(2, "HEADER");
    group(9, "$ACADVER");
    group(1, "AC1009");
    group(9, "$INSUNITS");
    group(70, std::to_string(DxfInsUnits(m_settings.units)));
    group(9, "$MEASUREMENT");
    group(70, (m_settings.units == Units::Inches || m_settings.units == Units::Feet) ? "0" : "1");
    const Box ext = Extents();
    if (!ext.Empty()) {
        group(9, "$EXTMIN");
        point(10, {ext.minX, ext.minY});
        group(9, "$EXTMAX");
        point(10, {ext.maxX, ext.maxY});
    }
    group(0, "ENDSEC");

    group(0, "SECTION");
    group(2, "TABLES");
    group(0, "TABLE");
    group(2, "LTYPE");
    group(70, "1");
    group(0, "LTYPE");
    group(2, "CONTINUOUS");
    group(70, "0");
    group(3, "Solid line");
    group(72, "65");
    group(73, "0");
    group(40, "0.0");
    group(0, "ENDTAB");
    group(0, "TABLE");
    group(2, "LAYER");
    group(70, "1");
    group(0, "LAYER");
    group(2, "0");
    group(70, "0");
    group(62, "7");
    group(6, "CONTINUOUS");
    group(0, "ENDTAB");
    group(0, "ENDSEC");

    group(0, "SECTION");
    group(2, "BLOCKS");
    group(0, "ENDSEC");

    group(0, "SECTION");
    group(2, "ENTITIES");
    for (const auto& kv : m_entities) {
        const Entity& e = kv.second;
        switch (e.type) {
            case EntityType::Line:
                group(0, "LINE");
                group(8, "0");
                point(10, e.p0);
                point(11, e.p1);
                break;
            case EntityType::Circle:
                group(0, "CIRCLE");
                group(8, "0");
                point(10, e.p0);
                num(40, e.radius);
                break;
            case EntityType::Arc:
                group(0, "ARC");
                group(8, "0");
                point(10, e.p0);
                num(40, e.radius);
                num(50, ToDegrees(e.startAngle));
                num(51, ToDegrees(e.endAngle));
                break;
            case EntityType::Polyline:
                group(0, "POLYLINE");
                group(8, "0");
                group(66, "1");
                point(10, {0, 0});
                group(70, e.closed ? "1" : "0");
                for (const Vertex& v : e.verts) {
                    group(0, "VERTEX");
                    group(8, "0");
                    point(10, v.p);
                    if (v.bulge != 0) num(42, v.bulge);
                }
                group(0, "SEQEND");
                group(8, "0");
                break;
        }
    }
    group(0, "ENDSEC");
    group(0, "EOF");
    return out;
}
