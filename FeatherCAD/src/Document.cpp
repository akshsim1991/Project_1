// Document.cpp - see Document.h.
#include "Document.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <set>
#include <sstream>

#include "DxfImport.h"

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

std::string TextToUtf8(const std::wstring& w) {
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = (uint32_t)w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size() && (uint32_t)w[i + 1] >= 0xDC00 &&
            (uint32_t)w[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint32_t)w[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) {
            out += (char)c;
        } else if (c < 0x800) {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        } else {
            out += (char)(0xF0 | (c >> 18));
            out += (char)(0x80 | ((c >> 12) & 0x3F));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::wstring TextFromUtf8(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = (unsigned char)s[i];
        uint32_t c;
        int more;
        if (b < 0x80) c = b, more = 0;
        else if ((b & 0xE0) == 0xC0) c = b & 0x1F, more = 1;
        else if ((b & 0xF0) == 0xE0) c = b & 0x0F, more = 2;
        else if ((b & 0xF8) == 0xF0) c = b & 0x07, more = 3;
        else c = b, more = 0;  // not UTF-8: taken as Latin-1
        bool ok = true;
        for (int k = 1; k <= more; ++k)
            if (i + k >= s.size() || ((unsigned char)s[i + k] & 0xC0) != 0x80) ok = false;
        if (!ok) {
            c = b;
            more = 0;
        }
        for (int k = 1; k <= more; ++k) c = (c << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += 1 + more;
        if (c >= 0x10000) {
            c -= 0x10000;
            out += (wchar_t)(0xD800 + (c >> 10));
            out += (wchar_t)(0xDC00 + (c & 0x3FF));
        } else {
            out += (wchar_t)c;
        }
    }
    return out;
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
    UpdateDimensions();
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

void Document::Restore(const Entity& e) {
    if (e.id == 0) return;
    const bool implicit = !m_open;
    if (implicit) BeginChange(L"Change");
    auto it = m_entities.find(e.id);
    Record(e.id, it == m_entities.end() ? nullptr : &it->second, &e);
    m_entities[e.id] = e;
    if (e.id >= m_nextId) m_nextId = e.id + 1;
    Touch();
    if (implicit) EndChange();
}

void Document::ReplaceAll(std::vector<Entity> entities, const std::wstring& name) {
    BeginChange(name);
    std::vector<uint64_t> ids;
    ids.reserve(m_entities.size());
    for (const auto& kv : m_entities) ids.push_back(kv.first);
    for (uint64_t id : ids) Erase(id);
    AddGroup(std::move(entities));
    EndChange();
}

std::vector<uint64_t> Document::AddGroup(std::vector<Entity> entities) {
    const bool implicit = !m_open;
    if (implicit) BeginChange(L"Add");
    std::vector<uint64_t> ids(entities.size(), 0);
    std::map<uint64_t, uint64_t> idMap;
    // Objects first, then the dimensions, pointed at the new objects.
    for (size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].type == EntityType::Dimension) continue;
        const uint64_t old = entities[i].id;
        ids[i] = Add(std::move(entities[i]));
        if (old) idMap[old] = ids[i];
    }
    for (size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].type != EntityType::Dimension) continue;
        Entity& e = entities[i];
        if (e.dim) {
            DimData d = *e.dim;
            for (DimRef& r : d.refs) {
                auto it = idMap.find(r.id);
                r.id = it == idMap.end() ? 0 : it->second;  // outside the group: not attached
            }
            e.dim = std::make_shared<const DimData>(std::move(d));
        }
        ids[i] = Add(std::move(e));
    }
    if (implicit) EndChange();
    return ids;
}

void Document::UpdateDimensions() {
    if (!m_open || m_open->changes.empty()) return;
    std::set<uint64_t> changed, touchedDims;
    bool anyDims = false;
    for (const Change& c : m_open->changes) {
        const std::optional<Entity>& x = c.after ? c.after : c.before;
        if (x && x->type == EntityType::Dimension) {
            if (c.after) touchedDims.insert(c.id);
        } else {
            changed.insert(c.id);
        }
    }
    for (const auto& kv : m_entities)
        if (kv.second.type == EntityType::Dimension) anyDims = true;
    if (!anyDims) return;
    // Polylines and splines that gained or lost vertices: their grips are numbered anew.
    std::map<uint64_t, const Change*> renumbered;
    for (const Change& c : m_open->changes)
        if (c.before && c.after && (c.after->type == EntityType::Polyline || c.after->type == EntityType::Spline) &&
            c.before->verts.size() != c.after->verts.size())
            renumbered[c.id] = &c;
    std::vector<Entity> updates;
    for (const auto& kv : m_entities) {
        const Entity& e = kv.second;
        if (!HasRefs(e)) continue;
        Entity copy = e;
        bool mod = false;
        std::set<uint64_t> done;
        for (const DimRef& r : e.dim->refs) {
            auto it = renumbered.find(r.id);
            if (r.id && it != renumbered.end() && done.insert(r.id).second)
                mod |= RenumberRefs(copy, *it->second->before, *it->second->after);
        }
        bool depends = false;
        for (const DimRef& r : e.dim->refs)
            if (r.id && changed.count(r.id)) depends = true;
        if (depends) {
            // Its objects changed: it follows them (also when it was moved with them).
            mod |= FollowRefs(*this, copy);
        } else if (touchedDims.count(kv.first)) {
            // Moved or edited on its own: still on its objects, or no longer attached.
            mod |= CheckRefs(*this, copy);
        }
        if (mod) updates.push_back(std::move(copy));
    }
    for (const Entity& e : updates) Replace(e);
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
    if (s.units == m_settings.units && s.precision == m_settings.precision && s.dimStyle == m_settings.dimStyle) return;
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
//   array rect columns rows colx coly rowx rowy n      then n objects
//   array polar cx cy count fill rotate n              (fill in degrees)
//   array path count spacing align n                   then the path, then n objects
//   ellipse cx cy axisx axisy ratio start end full      (start, end: parameters in degrees)
//   spline closed n x y x y ... [part=from,to]          (fit points; the part kept after a
//                                                       trim, as fractions of its length)
//   point x y
//   dimstyle text arrow extoffset extbeyond gap decimals trimzeros
//   dim kind x0 y0 x1 y1 x2 y2 x3 y3 locx locy angle rays text arrow extoffset
//       extbeyond gap decimals trimzeros obj0 grip0 obj1 grip1 obj2 grip2 obj3 grip3 [text=...]
//       kind: linear, aligned, radius, diameter or angular; angle in degrees;
//       objN: the object a point is attached to, counted from 1 in this
//       file (0: none); text: UTF-8 with %XX for spaces and symbols
//   end
//
// Lines starting with # are comments. Unknown keywords are skipped, so a
// later version can add objects without breaking older readers.
// ---------------------------------------------------------------------------

namespace {
const char* const kDimKinds[] = {"linear", "aligned", "radius", "diameter", "angular"};

std::string EncodeWord(const std::wstring& w) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : TextToUtf8(w)) {
        if (c <= 0x20 || c == '%' || c == '=' || c == '#' || c >= 0x7F) {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += (char)c;
        }
    }
    return out;
}

std::wstring DecodeWord(const std::string& s) {
    std::string bytes;
    auto hexv = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
            bytes += (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2]));
            i += 2;
        } else {
            bytes += s[i];
        }
    }
    return TextFromUtf8(bytes);
}

using Ordinals = std::map<uint64_t, size_t>;

void WriteEntity(const Entity& e, std::string& out, const Ordinals* ordinals = nullptr) {
    auto num = [&](double v) {
        out += ' ';
        out += FormatNumber(v);
    };
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
        case EntityType::Ellipse:
            out += "ellipse";
            num(e.p0.x); num(e.p0.y); num(e.p1.x); num(e.p1.y); num(e.radius);
            num(ToDegrees(e.startAngle)); num(ToDegrees(e.endAngle)); num(e.closed ? 1 : 0);
            break;
        case EntityType::Spline:
            out += "spline ";
            out += e.closed ? "1 " : "0 ";
            out += std::to_string(e.verts.size());
            for (const Vertex& v : e.verts) {
                num(v.p.x); num(v.p.y);
            }
            if (SplineTrimmed(e)) out += " part=" + FormatNumber(e.startAngle) + "," + FormatNumber(e.endAngle);
            break;
        case EntityType::Point:
            out += "point";
            num(e.p0.x); num(e.p0.y);
            break;
        case EntityType::Dimension: {
            const DimData& d = *e.dim;
            out += "dim ";
            out += kDimKinds[d.kind];
            for (Vec2 p : d.pts) {
                num(p.x); num(p.y);
            }
            num(d.loc.x); num(d.loc.y); num(ToDegrees(d.angle)); num(d.rays ? 1 : 0);
            const DimStyle& st = d.style;
            num(st.textHeight); num(st.arrowSize); num(st.extOffset); num(st.extBeyond); num(st.textGap);
            num(st.precision); num(st.trimZeros ? 1 : 0);
            for (const DimRef& r : d.refs) {
                size_t ord = 0;
                if (r.id && ordinals) {
                    auto it = ordinals->find(r.id);
                    if (it != ordinals->end()) ord = it->second;
                }
                num((double)ord);
                num(ord ? r.grip : 0);
            }
            if (!d.text.empty()) out += " text=" + EncodeWord(d.text);
            break;
        }
        case EntityType::Array: {
            const ArrayData& a = *e.array;
            if (a.kind == ArrayData::Rect) {
                out += "array rect";
                num(a.columns); num(a.rows);
                num(a.columnStep.x); num(a.columnStep.y); num(a.rowStep.x); num(a.rowStep.y);
            } else if (a.kind == ArrayData::Polar) {
                out += "array polar";
                num(a.centre.x); num(a.centre.y); num(a.count); num(ToDegrees(a.fillAngle)); num(a.rotateItems ? 1 : 0);
            } else {
                out += "array path";
                num(a.count); num(a.spacing); num(a.align ? 1 : 0);
            }
            num((double)a.items.size());
            out += '\n';
            if (a.kind == ArrayData::Path) {
                WriteEntity(a.path.empty() ? Entity::Line({0, 0}, {1, 0}) : a.path[0], out);
                out += '\n';
            }
            for (size_t i = 0; i < a.items.size(); ++i) {
                WriteEntity(a.items[i], out);
                if (i + 1 < a.items.size()) out += '\n';
            }
            break;
        }
    }
}

struct Reader {
    std::vector<std::string> lines;
    size_t pos = 0;  // next line
    std::wstring error;

    bool Fail(const std::wstring& why) {
        error = L"Line " + std::to_wstring(pos) + L": " + why;
        return false;
    }
    // The next line that is not empty or a comment, split into words.
    bool Next(std::vector<std::string>& w) {
        while (pos < lines.size()) {
            w = SplitWords(lines[pos++]);
            if (!w.empty() && w[0][0] != '#') return true;
        }
        return false;
    }
    // Numbers from word `from` on; key=value attributes end the list.
    bool Numbers(const std::vector<std::string>& w, size_t from, std::vector<double>& v) {
        v.clear();
        for (size_t i = from; i < w.size(); ++i) {
            if (w[i].find('=') != std::string::npos) break;
            double d;
            if (!ParseNumber(w[i], d)) return Fail(L"bad number \"" + std::wstring(w[i].begin(), w[i].end()) + L"\"");
            v.push_back(d);
        }
        return true;
    }
    // Reads one object from words `w` (more lines for arrays). `known` is
    // false for keywords of later versions (skipped).
    bool Object(const std::vector<std::string>& w, Entity& out, bool& known, int depth = 0) {
        known = true;
        const std::string& key = w[0];
        std::vector<double> v;
        if (key == "array") {
            if (depth > 8) return Fail(L"arrays nested too deeply");
            if (w.size() < 2 || !Numbers(w, 2, v)) return error.empty() ? Fail(L"an array needs its kind") : false;
            ArrayData a;
            size_t n = 0;
            if (w[1] == "rect" && v.size() >= 7) {
                a.kind = ArrayData::Rect;
                a.columns = std::max(1, (int)v[0]);
                a.rows = std::max(1, (int)v[1]);
                a.columnStep = {v[2], v[3]};
                a.rowStep = {v[4], v[5]};
                n = (size_t)v[6];
            } else if (w[1] == "polar" && v.size() >= 6) {
                a.kind = ArrayData::Polar;
                a.centre = {v[0], v[1]};
                a.count = std::max(1, (int)v[2]);
                a.fillAngle = ToRadians(v[3]);
                a.rotateItems = v[4] != 0;
                n = (size_t)v[5];
            } else if (w[1] == "path" && v.size() >= 4) {
                a.kind = ArrayData::Path;
                a.count = std::max(1, (int)v[0]);
                a.spacing = std::max(0.0, v[1]);
                a.align = v[2] != 0;
                n = (size_t)v[3];
            } else {
                return Fail(L"unknown or incomplete array");
            }
            if (n < 1 || n > 1000000 || a.columns > 100000 || a.rows > 100000 || a.count > 100000)
                return Fail(L"wrong array size");
            const size_t parts = n + (a.kind == ArrayData::Path ? 1 : 0);
            for (size_t i = 0; i < parts; ++i) {
                std::vector<std::string> iw;
                if (!Next(iw)) return Fail(L"the array's objects are missing");
                Entity item;
                bool k;
                if (!Object(iw, item, k, depth + 1)) return false;
                if (!k) return Fail(L"unknown object inside an array");
                if (a.kind == ArrayData::Path && i == 0) a.path.push_back(std::move(item));
                else a.items.push_back(std::move(item));
            }
            out = Entity::Array(std::move(a));
            return true;
        }
        if (key == "dim") {
            // The kind is a word: the numbers start after it.
            if (w.size() < 2 || !Numbers(w, 2, v)) return error.empty() ? Fail(L"a dimension needs its kind") : false;
            if (v.size() < 27) return Fail(L"a dimension needs 27 numbers");
            DimData d;
            bool kindOk = false;
            for (int k = 0; k < 5; ++k)
                if (w[1] == kDimKinds[k]) d.kind = (DimData::Kind)k, kindOk = true;
            if (!kindOk) {
                known = false;  // a kind from a newer version
                return true;
            }
            for (size_t i = 0; i < 4; ++i) d.pts[i] = {v[2 * i], v[2 * i + 1]};
            d.loc = {v[8], v[9]};
            d.angle = ToRadians(v[10]);
            d.rays = v[11] != 0;
            d.style.textHeight = v[12] > 0 ? v[12] : 3.5;
            d.style.arrowSize = std::max(0.0, v[13]);
            d.style.extOffset = std::max(0.0, v[14]);
            d.style.extBeyond = std::max(0.0, v[15]);
            d.style.textGap = std::max(0.0, v[16]);
            d.style.precision = std::clamp((int)v[17], 0, 8);
            d.style.trimZeros = v[18] != 0;
            for (size_t i = 0; i < 4; ++i) {
                // The object's place in the file for now; Document::FromText makes it an id.
                d.refs[i].id = v[19 + 2 * i] >= 1 ? (uint64_t)v[19 + 2 * i] : 0;
                d.refs[i].grip = (int)v[20 + 2 * i];
            }
            for (const std::string& word : w)
                if (word.compare(0, 5, "text=") == 0) d.text = DecodeWord(word.substr(5));
            if (depth > 0)
                for (DimRef& r : d.refs) r = {};
            out = MakeDimension(std::move(d));
            return true;
        }
        if (!Numbers(w, 1, v)) return false;
        auto need = [&](size_t k) { return v.size() >= k; };
        if (key == "line") {
            if (!need(4)) return Fail(L"a line needs 4 numbers");
            out = Entity::Line({v[0], v[1]}, {v[2], v[3]});
        } else if (key == "circle") {
            if (!need(3) || v[2] <= 0) return Fail(L"a circle needs a centre and a positive radius");
            out = Entity::Circle({v[0], v[1]}, v[2]);
        } else if (key == "arc") {
            if (!need(5) || v[2] <= 0) return Fail(L"an arc needs a centre, a positive radius and two angles");
            out = Entity::Arc({v[0], v[1]}, v[2], ToRadians(v[3]), ToRadians(v[4]));
        } else if (key == "pline") {
            if (!need(2)) return Fail(L"a polyline needs its flags and vertex count");
            const size_t n = (size_t)v[1];
            if (v[1] < 1 || v[1] > 1e7 || v.size() != 2 + 3 * n) return Fail(L"wrong number of polyline vertices");
            std::vector<Vertex> verts(n);
            for (size_t i = 0; i < n; ++i) verts[i] = {{v[2 + 3 * i], v[3 + 3 * i]}, v[4 + 3 * i]};
            out = Entity::Polyline(std::move(verts), v[0] != 0);
        } else if (key == "ellipse") {
            if (!need(8) || v[4] <= 0 || (v[2] == 0 && v[3] == 0)) return Fail(L"an ellipse needs a centre, an axis and a ratio");
            out = v[7] != 0 ? Entity::Ellipse({v[0], v[1]}, {v[2], v[3]}, v[4])
                            : Entity::EllipseArc({v[0], v[1]}, {v[2], v[3]}, v[4], ToRadians(v[5]), ToRadians(v[6]));
        } else if (key == "spline") {
            if (!need(2)) return Fail(L"a spline needs its flags and point count");
            const size_t n = (size_t)v[1];
            if (v[1] < 1 || v[1] > 1e7 || v.size() != 2 + 2 * n) return Fail(L"wrong number of spline points");
            std::vector<Vec2> fit(n);
            for (size_t i = 0; i < n; ++i) fit[i] = {v[2 + 2 * i], v[3 + 2 * i]};
            out = Entity::Spline(fit, v[0] != 0);
            // part=from,to: only that part is kept (fractions of its length)
            for (const std::string& word : w) {
                if (word.compare(0, 5, "part=") != 0) continue;
                const size_t comma = word.find(',');
                double f0, f1;
                if (comma != std::string::npos && ParseNumber(word.substr(5, comma - 5), f0) &&
                    ParseNumber(word.substr(comma + 1), f1) && f1 > 0) {
                    out.startAngle = std::clamp(f0, 0.0, 1.0);
                    out.endAngle = std::clamp(f1, 0.0, 1.0);
                }
            }
        } else if (key == "point") {
            if (!need(2)) return Fail(L"a point needs 2 numbers");
            out = Entity::Point({v[0], v[1]});
        } else {
            known = false;  // from a newer version
        }
        return true;
    }
};
}  // namespace

std::string Document::ToText() const {
    std::string out;
    out.reserve(64 + m_entities.size() * 48);
    out += kMagic;
    out += " " + std::to_string(kFormatVersion) + "\n";
    out += std::string("units ") + UnitsKey(m_settings.units) + "\n";
    out += "precision " + std::to_string(m_settings.precision) + "\n";
    const DimStyle& st = m_settings.dimStyle;
    out += "dimstyle " + FormatNumber(st.textHeight) + " " + FormatNumber(st.arrowSize) + " " +
           FormatNumber(st.extOffset) + " " + FormatNumber(st.extBeyond) + " " + FormatNumber(st.textGap) + " " +
           std::to_string(st.precision) + " " + (st.trimZeros ? "1" : "0") + "\n";
    Ordinals ordinals;
    size_t ord = 0;
    for (const auto& kv : m_entities) ordinals[kv.first] = ++ord;
    for (const auto& kv : m_entities) {
        WriteEntity(kv.second, out, &ordinals);
        out += '\n';
    }
    out += "end\n";
    return out;
}

std::string Document::ObjectsToText(const std::vector<Entity>& entities) {
    std::string out = std::string(kMagic) + " " + std::to_string(kFormatVersion) + "\n";
    Ordinals ordinals;
    size_t ord = 0;
    for (const Entity& e : entities) ordinals[e.id] = ++ord;
    for (const Entity& e : entities) {
        WriteEntity(e, out, &ordinals);
        out += '\n';
    }
    out += "end\n";
    return out;
}

bool Document::ObjectsFromText(const std::string& text, std::vector<Entity>& out) {
    Document d;
    std::wstring error;
    if (!d.FromText(text, error)) return false;
    out.clear();
    for (const auto& kv : d.Entities()) out.push_back(kv.second);  // ids 1..n, refs among them
    return true;
}

bool Document::FromText(const std::string& text, std::wstring& error) {
    Reader r;
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            r.lines.push_back(std::move(line));
        }
    }
    std::vector<std::string> w;
    if (!r.Next(w)) {
        error = L"The file is empty.";
        return false;
    }
    if (w[0] != kMagic) {
        error = L"This is not a Feather CAD drawing.";
        return false;
    }
    // Newer versions are read as far as this one understands them.
    DrawingSettings settings;
    std::vector<Entity> ents;
    std::vector<long long> ordinalToIndex;
    bool ended = false;
    while (!ended && r.Next(w)) {
        const std::string& key = w[0];
        if (key == "end") {
            ended = true;
        } else if (key == "units") {
            const std::string u = w.size() > 1 ? w[1] : "";
            for (Units cand : {Units::Unitless, Units::Millimetres, Units::Centimetres, Units::Metres,
                               Units::Inches, Units::Feet})
                if (u == UnitsKey(cand)) settings.units = cand;
        } else if (key == "precision") {
            std::vector<double> v;
            if (!r.Numbers(w, 1, v)) {
                error = r.error;
                return false;
            }
            if (!v.empty()) settings.precision = std::clamp((int)v[0], 0, 8);
        } else if (key == "dimstyle") {
            std::vector<double> v;
            if (!r.Numbers(w, 1, v)) {
                error = r.error;
                return false;
            }
            if (v.size() >= 7) {
                DimStyle& st = settings.dimStyle;
                st.textHeight = v[0] > 0 ? v[0] : st.textHeight;
                st.arrowSize = std::max(0.0, v[1]);
                st.extOffset = std::max(0.0, v[2]);
                st.extBeyond = std::max(0.0, v[3]);
                st.textGap = std::max(0.0, v[4]);
                st.precision = std::clamp((int)v[5], 0, 8);
                st.trimZeros = v[6] != 0;
            }
        } else {
            Entity e;
            bool known = false;
            if (!r.Object(w, e, known)) {
                error = r.error;
                return false;
            }
            // Dimensions name objects by their place in the file (unknown ones count too).
            ordinalToIndex.push_back(known ? (long long)ents.size() : -1);
            if (known) ents.push_back(std::move(e));
        }
    }
    if (!ended) {
        error = L"The file is incomplete (it may have been cut off while saving).";
        return false;
    }
    // Objects get the ids 1, 2, 3... in file order: dimensions point at them.
    for (Entity& e : ents) {
        if (e.type != EntityType::Dimension || !e.dim) continue;
        DimData d = *e.dim;
        for (DimRef& ref : d.refs) {
            const uint64_t ord = ref.id;
            ref.id = 0;
            if (ord >= 1 && ord <= ordinalToIndex.size() && ordinalToIndex[ord - 1] >= 0) {
                const size_t idx = (size_t)ordinalToIndex[ord - 1];
                if (ents[idx].type != EntityType::Dimension) ref.id = idx + 1;
            }
        }
        e.dim = std::make_shared<const DimData>(std::move(d));
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

bool Document::FromDxf(const std::string& text, std::wstring& error, std::wstring& notes) {
    DxfResult r;
    if (!ReadDxf(text, r, error)) return false;
    DrawingSettings settings = m_settings;
    if (r.hasUnits) settings.units = r.units;
    if (r.hasDimStyle) settings.dimStyle = r.dimStyle;
    Clear();
    m_settings = settings;
    m_settings.precision = std::clamp(m_settings.precision, 0, 8);
    for (Entity& e : r.entities) {
        e.id = m_nextId++;
        m_entities[e.id] = std::move(e);
    }
    notes = r.Notes();
    Touch();
    return true;
}

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
    group(2, "STYLE");
    group(70, "1");
    group(0, "STYLE");
    group(2, "STANDARD");
    group(70, "0");
    group(40, "0.0");
    group(41, "1.0");
    group(50, "0.0");
    group(71, "0");
    group(42, "2.5");
    group(3, "txt");
    group(4, "");
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
    // Arrays have no equivalent in DXF R12: their copies are written.
    std::vector<Entity> flat;
    for (const auto& kv : m_entities) ExpandArray(kv.second, flat);
    for (const Entity& e : flat) {
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
            case EntityType::Ellipse:
            case EntityType::Spline: {
                // DXF R12 has neither: a polyline that follows the curve.
                std::vector<Vec2> pts;
                if (e.type == EntityType::Ellipse) EllipsePoints(e, pts);
                else SplinePoints(e, pts);
                const bool closed = e.closed && pts.size() > 2;
                if (closed) pts.pop_back();
                group(0, "POLYLINE");
                group(8, "0");
                group(66, "1");
                point(10, {0, 0});
                group(70, closed ? "1" : "0");
                for (Vec2 p : pts) {
                    group(0, "VERTEX");
                    group(8, "0");
                    point(10, p);
                }
                group(0, "SEQEND");
                group(8, "0");
                break;
            }
            case EntityType::Point:
                group(0, "POINT");
                group(8, "0");
                point(10, e.p0);
                break;
            case EntityType::Dimension: {
                // Written as what it looks like: lines, arcs, filled arrows and text.
                if (!e.dim) break;
                DimGraphics g;
                DimLayout(*e.dim, g);
                for (const Prim& p : g.lines) {
                    if (p.kind == Prim::Segment) {
                        group(0, "LINE");
                        group(8, "0");
                        point(10, p.a);
                        point(11, p.b);
                    } else {
                        group(0, "ARC");
                        group(8, "0");
                        point(10, p.c);
                        num(40, p.r);
                        num(50, ToDegrees(NormalizeAngle(p.start)));
                        num(51, ToDegrees(NormalizeAngle(p.start + p.sweep)));
                    }
                }
                for (const auto& a : g.arrows) {
                    group(0, "SOLID");
                    group(8, "0");
                    point(10, a[0]);
                    point(11, a[1]);
                    point(12, a[2]);
                    point(13, a[2]);
                }
                std::string t;
                for (wchar_t c : g.text) {
                    if (c == L'\x2300' || c == L'\x00D8') t += "%%c";
                    else if (c == L'\x00B0') t += "%%d";
                    else if (c == L'\x00B1') t += "%%p";
                    else if (c >= 32 && c < 127) t += (char)c;
                    else t += '?';
                }
                if (!t.empty()) {
                    group(0, "TEXT");
                    group(8, "0");
                    point(10, g.textPos);
                    num(40, g.textHeight);
                    group(1, t);
                    num(50, ToDegrees(g.textAngle));
                    group(72, "1");
                    point(11, g.textPos);
                    group(73, "2");
                }
                break;
            }
            case EntityType::Array:
                break;
        }
    }
    group(0, "ENDSEC");
    group(0, "EOF");
    return out;
}
