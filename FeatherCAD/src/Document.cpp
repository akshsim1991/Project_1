// Document.cpp - see Document.h.
#include "Document.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <functional>
#include <set>
#include <sstream>

#include "DxfImport.h"
#include "Relate.h"

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

Document::Document() {
    Layer zero;
    zero.name = L"0";
    m_layers = {zero};
    IndexLayers();
}

const Entity* Document::Find(uint64_t id) const {
    auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : &it->second;
}

void Document::BeginChange(const std::wstring& name) {
    if (m_open) EndChange();
    m_open = Step{name, {}, 0, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
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
    if (step.changes.empty() && !step.layersAfter && !step.blocksAfter && !step.varsAfter) return;
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

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

void Document::IndexLayers() {
    int maxId = 0;
    for (const Layer& l : m_layers) maxId = std::max(maxId, l.id);
    m_layerIndex.assign((size_t)maxId + 1, -1);
    for (size_t i = 0; i < m_layers.size(); ++i) m_layerIndex[(size_t)m_layers[i].id] = (int)i;
}

const Layer* Document::FindLayer(int id) const {
    if (id < 0 || (size_t)id >= m_layerIndex.size() || m_layerIndex[(size_t)id] < 0) return nullptr;
    return &m_layers[(size_t)m_layerIndex[(size_t)id]];
}

namespace {
std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}
}  // namespace

const Layer* Document::LayerByName(const std::wstring& name) const {
    const std::wstring n = Lower(name);
    for (const Layer& l : m_layers)
        if (Lower(l.name) == n) return &l;
    return nullptr;
}

void Document::SetLayers(std::vector<Layer> layers, const std::wstring& stepName) {
    const bool implicit = !m_open;
    if (implicit) BeginChange(stepName);
    if (!m_open->layersBefore) m_open->layersBefore = m_layers;
    m_layers = std::move(layers);
    IndexLayers();
    m_open->layersAfter = m_layers;
    Touch();
    if (implicit) EndChange();
}

int Document::AddLayer(Layer layer) {
    int id = 0;
    for (const Layer& l : m_layers) id = std::max(id, l.id + 1);
    layer.id = id;
    if (layer.name.empty()) layer.name = L"Layer" + std::to_wstring(id);
    const std::wstring base = layer.name;
    for (int k = 2; LayerByName(layer.name); ++k) layer.name = base + L" (" + std::to_wstring(k) + L")";
    std::vector<Layer> ls = m_layers;
    ls.push_back(layer);
    SetLayers(std::move(ls), L"New layer");
    return id;
}

void Document::UpdateLayer(const Layer& layer) {
    std::vector<Layer> ls = m_layers;
    for (Layer& l : ls)
        if (l.id == layer.id) l = layer;
    if (layer.id == 0) ls[(size_t)m_layerIndex[0]].name = L"0";  // layer 0 keeps its name
    SetLayers(std::move(ls), L"Layer");
}

size_t Document::LayerUse(int id) const {
    size_t n = 0;
    for (const auto& kv : m_entities) n += kv.second.layer == id;
    return n;
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

void Document::SetVariables(std::vector<Variable> vars, const std::wstring& stepName) {
    const bool implicit = !m_open;
    if (implicit) BeginChange(stepName);
    if (!m_open->varsBefore) m_open->varsBefore = m_vars;
    m_vars = std::move(vars);
    m_open->varsAfter = m_vars;
    Touch();
    if (implicit) EndChange();
}

void Document::SetVariableValues(const std::vector<double>& values) {
    for (size_t i = 0; i < values.size() && i < m_vars.size(); ++i) m_vars[i].value = values[i];
}

void Document::SetProperty(const std::wstring& key, const std::wstring& value) {
    auto it = m_props.find(key);
    if (value.empty() ? it == m_props.end() : (it != m_props.end() && it->second == value)) return;
    if (value.empty()) m_props.erase(it);
    else m_props[key] = value;
    m_settingsChanged = true;
    Touch();
}

std::shared_ptr<const BlockData> Document::FindBlock(const std::wstring& name) const {
    for (const auto& b : m_blocks)
        if (SameName(b->name, name)) return b;
    return nullptr;
}

void Document::SetBlocks(BlockTable blocks, const std::wstring& stepName) {
    const bool implicit = !m_open;
    if (implicit) BeginChange(stepName);
    if (!m_open->blocksBefore) m_open->blocksBefore = m_blocks;
    m_blocks = std::move(blocks);
    m_open->blocksAfter = m_blocks;
    Touch();
    if (implicit) EndChange();
}

std::shared_ptr<const BlockData> Document::DefineBlock(BlockData b) {
    const bool implicit = !m_open;
    if (implicit) BeginChange(L"Block");
    auto def = std::make_shared<const BlockData>(std::move(b));
    BlockTable table = m_blocks;
    bool replaced = false;
    for (auto& x : table)
        if (SameName(x->name, def->name)) x = def, replaced = true;
    if (!replaced) table.push_back(def);
    SetBlocks(std::move(table), L"Block");
    if (replaced) {
        // Its references show the new definition.
        std::vector<Entity> refs;
        for (const auto& kv : m_entities)
            if (kv.second.type == EntityType::Insert && kv.second.annot && kv.second.annot->block &&
                SameName(kv.second.annot->block->name, def->name))
                refs.push_back(kv.second);
        for (Entity& e : refs) {
            EditAnnot(e).block = def;
            Replace(e);
        }
    }
    if (implicit) EndChange();
    return def;
}

size_t Document::BlockUse(const std::wstring& name) const {
    size_t n = 0;
    for (const auto& kv : m_entities)
        if (kv.second.type == EntityType::Insert && kv.second.annot && kv.second.annot->block &&
            (SameName(kv.second.annot->block->name, name) || BlockUses(*kv.second.annot->block, name)))
            ++n;
    return n;
}

bool Document::DeleteBlock(const std::wstring& name, std::wstring& why) {
    if (!FindBlock(name)) {
        why = L"There is no block \"" + name + L"\".";
        return false;
    }
    if (BlockUse(name)) {
        why = L"Block \"" + name + L"\" is used in the drawing.";
        return false;
    }
    for (const auto& b : m_blocks)
        if (!SameName(b->name, name) && BlockUses(*b, name)) {
            why = L"Block \"" + name + L"\" is used inside block \"" + b->name + L"\".";
            return false;
        }
    BlockTable table;
    for (const auto& b : m_blocks)
        if (!SameName(b->name, name)) table.push_back(b);
    SetBlocks(std::move(table), L"Delete block");
    return true;
}

void Document::MergeBlocks(const BlockTable& from, std::vector<Entity>* ents) {
    BlockTable table = m_blocks;
    bool added = false;
    for (const auto& b : from) {
        if (FindBlock(b->name)) continue;
        table.push_back(b);
        added = true;
    }
    if (added) SetBlocks(std::move(table), L"Blocks");
    if (!ents) return;
    for (Entity& e : *ents)
        if (e.type == EntityType::Insert && e.annot && e.annot->block)
            if (auto mine = FindBlock(e.annot->block->name)) EditAnnot(e).block = mine;
}

std::vector<uint64_t> Document::AddForeign(std::vector<Entity> ents, const std::vector<Layer>& layers,
                                           const BlockTable& blocks) {
    const bool implicit = !m_open;
    if (implicit) BeginChange(L"Add");
    const std::map<int, int> layerIds = MergeLayers(layers);
    auto mapLayer = [&](int id) {
        auto it = layerIds.find(id);
        return it == layerIds.end() ? 0 : it->second;
    };
    std::function<void(Entity&, int)> fix;
    std::function<std::shared_ptr<const BlockData>(const std::shared_ptr<const BlockData>&, int)> adopt =
        [&](const std::shared_ptr<const BlockData>& b, int depth) {
            if (auto mine = FindBlock(b->name)) return mine;  // the one here wins
            if (depth > 16) return b;
            BlockData copy = *b;
            for (Entity& x : copy.items) fix(x, depth + 1);
            return DefineBlock(std::move(copy));
        };
    fix = [&](Entity& e, int depth) {
        e.layer = mapLayer(e.layer);
        if (e.type == EntityType::Insert && e.annot && e.annot->block) {
            EditAnnot(e).block = adopt(e.annot->block, depth);
        } else if (e.type == EntityType::Array && e.array) {
            ArrayData a = *e.array;
            for (Entity& x : a.items) fix(x, depth + 1);
            for (Entity& x : a.path) fix(x, depth + 1);
            e.array = std::make_shared<const ArrayData>(std::move(a));
        }
    };
    for (const auto& b : blocks) adopt(b, 0);
    for (Entity& e : ents) fix(e, 0);
    std::vector<uint64_t> ids = AddGroup(std::move(ents));
    if (implicit) EndChange();
    return ids;
}

bool Document::DeleteLayer(int id, std::wstring& why) {
    if (id == 0) {
        why = L"Layer 0 cannot be deleted.";
        return false;
    }
    if (id == m_current.layer) {
        why = L"The current layer cannot be deleted. Make another one current first.";
        return false;
    }
    if (const size_t n = LayerUse(id)) {
        why = L"The layer has " + std::to_wstring(n) + L" object(s) on it. Move or erase them first.";
        return false;
    }
    std::vector<Layer> ls;
    for (const Layer& l : m_layers)
        if (l.id != id) ls.push_back(l);
    SetLayers(std::move(ls), L"Delete layer");
    return true;
}

void Document::SetCurrent(const CurrentStyle& c) {
    m_current = c;
    if (!FindLayer(m_current.layer)) m_current.layer = 0;
    m_settingsChanged = true;
    Touch();
}

void Document::SaveLayerState(const std::wstring& name) {
    for (LayerState& s : m_states)
        if (Lower(s.name) == Lower(name)) {
            s.layers = m_layers;
            m_settingsChanged = true;
            return;
        }
    m_states.push_back({name, m_layers});
    m_settingsChanged = true;
}

bool Document::RestoreLayerState(const std::wstring& name) {
    for (const LayerState& s : m_states) {
        if (Lower(s.name) != Lower(name)) continue;
        std::vector<Layer> ls = m_layers;
        for (Layer& l : ls)
            for (const Layer& saved : s.layers)
                if (Lower(saved.name) == Lower(l.name)) {
                    const int id = l.id;
                    const std::wstring nm = l.name;
                    l = saved;
                    l.id = id;
                    l.name = nm;
                }
        SetLayers(std::move(ls), L"Restore layer state");
        return true;
    }
    return false;
}

void Document::DeleteLayerState(const std::wstring& name) {
    m_states.erase(std::remove_if(m_states.begin(), m_states.end(),
                                  [&](const LayerState& s) { return Lower(s.name) == Lower(name); }),
                   m_states.end());
    m_settingsChanged = true;
}

bool Document::Visible(const Entity& e) const {
    if (e.type == EntityType::Relation && !m_settings.showRelations) return false;
    const Layer* l = FindLayer(e.layer);
    return !l || (l->on && !l->frozen);
}

bool Document::Editable(const Entity& e) const {
    const Layer* l = FindLayer(e.layer);
    return !l || (l->on && !l->frozen && !l->locked);
}

Look Document::LookOf(const Entity& e, const Entity* block) const {
    Look k;
    const Layer* l = FindLayer(e.layer);
    static const Layer zero = [] {
        Layer z;
        z.name = L"0";
        return z;
    }();
    if (!l) l = &zero;
    // ByBlock takes the block reference's own (or its layer's) setting.
    int32_t color = e.color;
    int lt = e.linetype, lw = e.lineweight;
    const Layer* bl = block ? FindLayer(block->layer) : nullptr;
    if (color == kByBlock) color = block ? (block->color == kByLayer ? (bl ? bl->color : 7) : block->color) : 7;
    if (color == kByLayer || color == kByBlock) color = l->color;
    if (lt == kLtByBlock) lt = block ? (block->linetype == kLtByLayer ? (bl ? bl->linetype : 0) : block->linetype) : 0;
    if (lt < 0) lt = l->linetype;
    if (lw == kLwByBlock) lw = block ? (block->lineweight == kLwByLayer ? (bl ? bl->lineweight : kLwDefault) : block->lineweight)
                                     : kLwDefault;
    if (lw == kLwByLayer) lw = l->lineweight;
    if (lw < 0) lw = kDefaultLineWeight;
    k.foreground = color == 7 || color == 255 || color == (kTrueColor | 0xFFFFFF);
    k.rgb = ColorRgb(color);
    k.color = color;
    k.linetype = lt;
    k.lineweight = lw;
    k.ltscale = m_settings.ltscale * e.ltscale * (block ? block->ltscale : 1.0);
    k.construction = e.construction;
    k.locked = l->locked;
    return k;
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
    if (e.layer < 0 || !FindLayer(e.layer)) {
        // A new object: the current layer and style.
        const bool fresh = e.layer < 0;
        e.layer = m_current.layer;
        if (fresh) {
            e.color = m_current.color;
            e.linetype = (int16_t)m_current.linetype;
            e.lineweight = (int16_t)m_current.lineweight;
            e.ltscale = (float)m_current.ltscale;
        }
    }
    if (e.type == EntityType::Dimension && e.dim && e.dim->name.empty()) {
        // A name for formulas: D1, D2...
        int n = 0;
        for (const auto& kv : m_entities)
            if (kv.second.type == EntityType::Dimension && kv.second.dim) {
                const std::wstring& nm = kv.second.dim->name;
                if (nm.size() > 1 && nm[0] == L'D' && std::all_of(nm.begin() + 1, nm.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                    n = std::max(n, std::stoi(nm.substr(1)));
            }
        DimData d = *e.dim;
        d.name = L"D" + std::to_wstring(n + 1);
        e.dim = std::make_shared<const DimData>(std::move(d));
    }
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

void Document::Replace(const Entity& eIn) {
    auto it = m_entities.find(eIn.id);
    if (it == m_entities.end()) return;
    Entity e = eIn;
    if (e.layer < 0) CopyStyle(it->second, e);  // a newly made shape in its place keeps the look
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

void Document::ReplaceAll(std::vector<Entity> entities, const std::wstring& name, const BlockTable* blocks) {
    BeginChange(name);
    if (blocks) SetBlocks(*blocks, name);
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
    auto attached = [](const Entity& e) {
        return e.type == EntityType::Dimension || e.type == EntityType::Relation ||
               (e.type == EntityType::Hatch && e.annot && !e.annot->hatch.refs.empty());
    };
    for (size_t i = 0; i < entities.size(); ++i) {
        if (attached(entities[i])) continue;
        const uint64_t old = entities[i].id;
        ids[i] = Add(std::move(entities[i]));
        if (old) idMap[old] = ids[i];
    }
    for (size_t i = 0; i < entities.size(); ++i) {
        if (!attached(entities[i])) continue;
        Entity& e = entities[i];
        if (e.type == EntityType::Relation && e.annot) {
            // Only with all of its objects.
            RelationData r = e.annot->rel;
            bool all = true;
            for (DimRef& ref : r.refs) {
                if (!ref.id) continue;
                auto it = idMap.find(ref.id);
                if (it == idMap.end()) all = false;
                else ref.id = it->second;
            }
            if (!all) continue;
            EditAnnot(e).rel = r;
        }
        if (e.type == EntityType::Hatch) {
            HatchData& h = EditAnnot(e).hatch;
            std::vector<uint64_t> refs;
            for (uint64_t r : h.refs) {
                auto it = idMap.find(r);
                if (it != idMap.end()) refs.push_back(it->second);
            }
            h.refs = refs;
        }
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

void Document::SolveStep() {
    std::set<uint64_t> changed, keep;
    for (const Change& c : m_open->changes) {
        changed.insert(c.id);
        if (!c.after) continue;
        const Entity& x = *c.after;
        if (x.type == EntityType::Relation && x.annot) {
            if (x.annot->rel.refs[0].id) keep.insert(x.annot->rel.refs[0].id);  // the first element stays, the others follow
        } else if (x.type == EntityType::Dimension) {
            if (x.dim && x.dim->locked && x.dim->refs[0].id) keep.insert(x.dim->refs[0].id);
        } else if (!IsAnnotation(x)) {
            keep.insert(c.id);
        }
    }
    std::wstring formulaError;
    EvaluateFormulas(*this, changed, formulaError);
    m_lastSolve = SolveReport{};
    if (m_settings.maintainRelations) m_lastSolve = SolveRelations(*this, changed, keep);
    if (!formulaError.empty()) {
        m_lastSolve.ok = false;
        m_lastSolve.message = formulaError;
    }
    // Relationship marks follow their objects; those whose objects are gone go too.
    std::set<uint64_t> now;
    for (const Change& c : m_open->changes) now.insert(c.id);
    std::vector<uint64_t> rels;
    for (const auto& kv : m_entities)
        if (kv.second.type == EntityType::Relation && kv.second.annot) {
            bool touched = now.count(kv.first) > 0;
            for (const DimRef& r : kv.second.annot->rel.refs) touched |= r.id && now.count(r.id);
            if (touched) rels.push_back(kv.first);
        }
    for (uint64_t id : rels) {
        Entity e = m_entities[id];
        const std::vector<Vec2> before = e.annot->pts;
        if (!PlaceRelationMarks(*this, e)) Erase(id);
        else if (e.annot->pts != before) Replace(e);
    }
}

void Document::UpdateDimensions() {
    if (!m_open || (m_open->changes.empty() && !m_open->varsAfter)) return;
    if (!m_solving) {
        m_solving = true;
        SolveStep();
        m_solving = false;
    }
    if (m_open->changes.empty()) return;
    std::set<uint64_t> changed, touchedDims, touchedHatches;
    bool anyDims = false;
    for (const Change& c : m_open->changes) {
        const std::optional<Entity>& x = c.after ? c.after : c.before;
        if (x && x->type == EntityType::Dimension) {
            if (c.after) touchedDims.insert(c.id);
        } else {
            changed.insert(c.id);
            if (x && x->type == EntityType::Hatch && c.before && c.after) touchedHatches.insert(c.id);
        }
    }
    UpdateHatches(changed, touchedHatches);
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

void Document::UpdateHatches(const std::set<uint64_t>& changed, const std::set<uint64_t>& touched) {
    std::vector<Entity> updates;
    for (const auto& kv : m_entities) {
        const Entity& e = kv.second;
        if (e.type != EntityType::Hatch || !e.annot || e.annot->hatch.refs.empty()) continue;
        const HatchData& h = e.annot->hatch;
        bool depends = false;
        for (uint64_t r : h.refs) depends |= changed.count(r) > 0;
        Entity copy = e;
        if (!depends) {
            // Moved or edited on its own: no longer attached.
            if (touched.count(kv.first)) {
                EditAnnot(copy).hatch.refs.clear();
                updates.push_back(std::move(copy));
            }
            continue;
        }
        HatchData& nh = EditAnnot(copy).hatch;
        std::vector<const Entity*> objs;
        std::vector<uint64_t> alive;
        for (uint64_t r : h.refs)
            if (const Entity* x = Find(r)) objs.push_back(x), alive.push_back(r);
        std::vector<std::vector<Vec2>> loops;
        bool ok = false;
        if (h.seeded) {
            ok = !objs.empty() && FindArea(objs, h.seed, loops);
        } else {
            for (const Entity* x : objs) {
                std::vector<Vec2> l;
                if (ClosedOutline(*x, l)) loops.push_back(std::move(l));
            }
            ok = !loops.empty();
        }
        if (ok) {
            nh.loops = std::move(loops);
            nh.refs = alive;
        } else {
            nh.refs.clear();  // its outline is gone: it stays as it was
        }
        updates.push_back(std::move(copy));
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
    if (const auto& layers = forward ? step.layersAfter : step.layersBefore) {
        m_layers = *layers;
        IndexLayers();
        if (!FindLayer(m_current.layer)) m_current.layer = 0;
    }
    if (const auto& blocks = forward ? step.blocksAfter : step.blocksBefore) m_blocks = *blocks;
    if (const auto& vars = forward ? step.varsAfter : step.varsBefore) m_vars = *vars;
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
    if (s == m_settings) return;
    m_settings = s;
    m_settings.precision = std::clamp(m_settings.precision, 0, 8);
    m_settingsChanged = true;
    Touch();
}

void Document::Clear() {
    m_open.reset();
    m_openIndex.clear();
    m_entities.clear();
    m_layers.resize(1);
    m_layers[0] = Layer{};
    m_layers[0].name = L"0";
    IndexLayers();
    m_current = CurrentStyle{};
    m_states.clear();
    m_blocks.clear();
    m_props.clear();
    m_vars.clear();
    m_lastSolve = SolveReport{};
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
        for (const auto& kv : m_entities)
            if (Visible(kv.second)) m_extents.Add(Bounds(kv.second));
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
const char* const kDimKinds[] = {"linear", "aligned", "radius", "diameter", "angular", "ordinate", "angcoord"};

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

}  // namespace

std::string FcadEncode(const std::wstring& w) { return EncodeWord(w); }
std::wstring FcadDecode(const std::string& s) { return DecodeWord(s); }

namespace {
using Ordinals = std::map<uint64_t, size_t>;

void WriteStyled(const Entity& e, std::string& out, const Ordinals* ordinals);

// A text's settings as key=value words (the content last).
std::string TextWords(const TextData& t) {
    std::string w = " th=" + FormatNumber(t.height);
    if (t.rotation != 0) w += " tr=" + FormatNumber(ToDegrees(t.rotation));
    if (t.widthFactor != 1) w += " tw=" + FormatNumber(t.widthFactor);
    if (t.lineSpacing != 1) w += " tl=" + FormatNumber(t.lineSpacing);
    if (t.justify) w += " tj=" + std::to_string(t.justify);
    if (t.frame) w += " tf=" + std::to_string(t.frame);
    if (t.font != L"Arial") w += " font=" + EncodeWord(t.font);
    if (!t.tag.empty()) w += " tag=" + EncodeWord(t.tag);
    if (!t.prompt.empty()) w += " prompt=" + EncodeWord(t.prompt);
    w += " content=" + EncodeWord(t.content);
    return w;
}

void ReadTextWords(const std::vector<std::string>& w, TextData& t) {
    for (const std::string& word : w) {
        const size_t eq = word.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = word.substr(0, eq), val = word.substr(eq + 1);
        double v = 0;
        const bool isNum = ParseNumber(val, v);
        if (key == "th" && isNum && v > 0) t.height = v;
        else if (key == "tr" && isNum) t.rotation = ToRadians(v);
        else if (key == "tw" && isNum && v > 0) t.widthFactor = v;
        else if (key == "tl" && isNum && v > 0) t.lineSpacing = v;
        else if (key == "tj" && isNum) t.justify = std::clamp((int)v, 0, 8);
        else if (key == "tf" && isNum) t.frame = std::clamp((int)v, 0, 2);
        else if (key == "font") t.font = DecodeWord(val);
        else if (key == "tag") t.tag = DecodeWord(val);
        else if (key == "prompt") t.prompt = DecodeWord(val);
        else if (key == "content") t.content = DecodeWord(val);
    }
    if (t.font.empty()) t.font = L"Arial";
}

size_t OrdinalOf(uint64_t id, const Ordinals* ordinals) {
    if (!id || !ordinals) return 0;
    auto it = ordinals->find(id);
    return it == ordinals->end() ? 0 : it->second;
}

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
            if (d.driven) out += " driven=1";
            if (!d.name.empty()) out += " name=" + EncodeWord(d.name);
            if (d.locked) out += " locked=" + FormatNumber(d.value);
            if (!d.formula.empty()) out += " formula=" + EncodeWord(d.formula);
            break;
        }
        case EntityType::Text:
            out += "text";
            num(e.p0.x); num(e.p0.y);
            out += TextWords(e.annot->text);
            break;
        case EntityType::Leader: {
            const Annot& a = *e.annot;
            out += "leader";
            num(a.arrow); num((double)a.pts.size());
            for (Vec2 p : a.pts) {
                num(p.x); num(p.y);
            }
            out += TextWords(a.text);
            break;
        }
        case EntityType::Hatch: {
            const HatchData& h = e.annot->hatch;
            out += "hatch";
            num(h.scale); num(ToDegrees(h.angle)); num(h.origin.x); num(h.origin.y);
            num(h.seeded ? 1 : 0); num(h.seed.x); num(h.seed.y);
            num((double)h.loops.size());
            std::vector<size_t> refs;
            for (uint64_t r : h.refs)
                if (size_t o = OrdinalOf(r, ordinals)) refs.push_back(o);
            num((double)refs.size());
            for (size_t o : refs) num((double)o);
            out += " pattern=" + EncodeWord(h.pattern);
            for (const auto& l : h.loops) {
                out += "\nloop " + std::to_string(l.size());
                for (Vec2 p : l) {
                    num(p.x); num(p.y);
                }
            }
            break;
        }
        case EntityType::Relation: {
            const Annot& a = *e.annot;
            out += "rel";
            num((double)a.rel.kind);
            for (const DimRef& r : a.rel.refs) {
                const size_t o = OrdinalOf(r.id, ordinals);
                num((double)o);
                num(o ? r.grip : 0);
            }
            num(a.rel.param);
            num(a.arrow);
            break;
        }
        case EntityType::Viewport: {
            const Annot& a = *e.annot;
            out += "viewport";
            num(e.p0.x); num(e.p0.y); num(e.p1.x); num(e.p1.y);
            num(a.viewCentre.x); num(a.viewCentre.y); num(a.viewScale); num(a.viewLocked ? 1 : 0);
            break;
        }
        case EntityType::Insert: {
            const Annot& a = *e.annot;
            out += "insert";
            num(e.p0.x); num(e.p0.y); num(e.radius); num(ToDegrees(e.startAngle)); num(e.closed ? 1 : 0);
            out += " name=" + EncodeWord(a.block ? a.block->name : L"?");
            for (const std::wstring& v : a.values) out += " v=" + EncodeWord(v);
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
                WriteStyled(a.path.empty() ? Entity::Line({0, 0}, {1, 0}) : a.path[0], out, nullptr);
                out += '\n';
            }
            for (size_t i = 0; i < a.items.size(); ++i) {
                WriteStyled(a.items[i], out, nullptr);
                if (i + 1 < a.items.size()) out += '\n';
            }
            break;
        }
    }
}

// How an object looks, as key=value words: L layer, C colour, T line type,
// W line weight, S line type scale, K construction.
std::string StyleWords(const Entity& e) {
    std::string w;
    if (e.layer > 0) w += " L=" + std::to_string(e.layer);
    if (e.color != kByLayer) w += " C=" + std::to_string(e.color);
    if (e.linetype != kLtByLayer) w += " T=" + std::to_string(e.linetype);
    if (e.lineweight != kLwByLayer) w += " W=" + std::to_string(e.lineweight);
    if (e.ltscale != 1) w += " S=" + FormatNumber(e.ltscale);
    if (e.construction) w += " K=1";
    return w;
}

void WriteStyled(const Entity& e, std::string& out, const Ordinals* ordinals) {
    const size_t start = out.size();
    WriteEntity(e, out, ordinals);
    const std::string words = StyleWords(e);
    if (words.empty()) return;
    const size_t nl = out.find('\n', start);  // an array: on its first line
    if (nl == std::string::npos) out += words;
    else out.insert(nl, words);
}

// Text for DXF: the usual symbols as %% codes, other non-ASCII letters as \\U+XXXX.
std::string DxfText(const std::wstring& w) {
    std::string t;
    for (wchar_t c : w) {
        if (c == L'\x2300' || c == L'\x00D8') t += "%%c";
        else if (c == L'\x00B0') t += "%%d";
        else if (c == L'\x00B1') t += "%%p";
        else if (c >= 32 && c < 127) t += (char)c;
        else if (c >= 32) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "\\U+%04X", (unsigned)c);
            t += buf;
        }
    }
    return t;
}

void WriteBlock(const BlockData& b, std::string& out) {
    out += "block " + FormatNumber(b.base.x) + " " + FormatNumber(b.base.y) + " " + std::to_string(b.items.size()) +
           " name=" + EncodeWord(b.name) + "\n";
    for (const Entity& e : b.items) {
        WriteStyled(e, out, nullptr);
        out += '\n';
    }
}

// The blocks `e` uses (and those they use), added to `out` once each.
void UsedBlocks(const Entity& e, BlockTable& out, int depth = 0) {
    if (depth > 16) return;
    if (e.type == EntityType::Array && e.array) {
        for (const Entity& x : e.array->items) UsedBlocks(x, out, depth + 1);
        return;
    }
    if (e.type != EntityType::Insert || !e.annot || !e.annot->block) return;
    const auto& b = e.annot->block;
    for (const auto& x : out)
        if (SameName(x->name, b->name)) return;
    out.push_back(b);
    for (const Entity& x : b->items) UsedBlocks(x, out, depth + 1);
}

std::string LayerLine(const char* key, const Layer& l) {
    const int flags = (l.on ? 0 : 1) | (l.frozen ? 2 : 0) | (l.locked ? 4 : 0) | (l.plot ? 0 : 8);
    return std::string(key) + " " + std::to_string(l.id) + " " + std::to_string(flags) + " " + std::to_string(l.color) +
           " " + std::to_string(l.linetype) + " " + std::to_string(l.lineweight) + " name=" + EncodeWord(l.name) + "\n";
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
    static void ApplyStyle(const std::vector<std::string>& w, Entity& e) {
        e.layer = 0;
        for (const std::string& word : w) {
            if (word.size() < 3 || word[1] != '=') continue;
            double v;
            if (!ParseNumber(word.substr(2), v)) continue;
            switch (word[0]) {
                case 'L': e.layer = std::max(0, (int)v); break;
                case 'C': e.color = (int32_t)v; break;
                case 'T': e.linetype = (int16_t)std::clamp((int)v, -2, (int)LineTypes().size() - 1); break;
                case 'W': e.lineweight = (int16_t)std::clamp((int)v, -3, 211); break;
                case 'S': e.ltscale = v > 0 ? (float)v : 1.0f; break;
                case 'K': e.construction = v != 0; break;
                default: break;
            }
        }
    }
    bool ObjectStyled(const std::vector<std::string>& w, Entity& out, bool& known, int depth = 0) {
        if (!Object(w, out, known, depth)) return false;
        if (known) ApplyStyle(w, out);
        return true;
    }
    // "layer id flags colour linetype lineweight name=..."
    bool ReadLayer(const std::vector<std::string>& w, Layer& l) {
        std::vector<double> v;
        if (!Numbers(w, 1, v) || v.size() < 5) return error.empty() ? Fail(L"a layer needs 5 numbers") : false;
        l.id = std::max(0, (int)v[0]);
        const int flags = (int)v[1];
        l.on = !(flags & 1);
        l.frozen = (flags & 2) != 0;
        l.locked = (flags & 4) != 0;
        l.plot = !(flags & 8);
        l.color = (int32_t)v[2];
        if (l.color == kByLayer || l.color == kByBlock) l.color = 7;
        l.linetype = std::clamp((int)v[3], 0, (int)LineTypes().size() - 1);
        l.lineweight = std::clamp((int)v[4], -3, 211);
        for (const std::string& word : w)
            if (word.compare(0, 5, "name=") == 0) l.name = DecodeWord(word.substr(5));
        if (l.name.empty()) l.name = L"Layer" + std::to_wstring(l.id);
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
                if (!ObjectStyled(iw, item, k, depth + 1)) return false;
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
            for (int k = 0; k < 7; ++k)
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
            for (const std::string& word : w) {
                if (word.compare(0, 5, "text=") == 0) d.text = DecodeWord(word.substr(5));
                if (word == "driven=1") d.driven = true;
                if (word.compare(0, 5, "name=") == 0) d.name = DecodeWord(word.substr(5));
                if (word.compare(0, 8, "formula=") == 0) d.formula = DecodeWord(word.substr(8));
                double lv;
                if (word.compare(0, 7, "locked=") == 0 && ParseNumber(word.substr(7), lv)) d.locked = true, d.value = lv;
            }
            if (depth > 0)
                for (DimRef& r : d.refs) r = {};
            out = MakeDimension(std::move(d));
            return true;
        }
        if (!Numbers(w, 1, v)) return false;
        auto need = [&](size_t k) { return v.size() >= k; };
        if (key == "text") {
            if (!need(2)) return Fail(L"a text needs its position");
            TextData t;
            ReadTextWords(w, t);
            out = MakeText({v[0], v[1]}, std::move(t));
            return true;
        }
        if (key == "leader") {
            if (!need(2) || v[1] < 1 || v[1] > 10000 || v.size() < 2 + 2 * (size_t)v[1]) return Fail(L"a leader needs its points");
            std::vector<Vec2> pts;
            for (size_t i = 0; i < (size_t)v[1]; ++i) pts.push_back({v[2 + 2 * i], v[3 + 2 * i]});
            TextData t;
            ReadTextWords(w, t);
            out = MakeLeader(std::move(pts), std::move(t), std::max(0.0, v[0]));
            return true;
        }
        if (key == "hatch") {
            if (!need(9)) return Fail(L"a hatch needs 9 numbers");
            HatchData h;
            h.scale = v[0] > 0 ? v[0] : 1;
            h.angle = ToRadians(v[1]);
            h.origin = {v[2], v[3]};
            h.seeded = v[4] != 0;
            h.seed = {v[5], v[6]};
            const size_t loops = (size_t)std::max(0.0, v[7]), refs = (size_t)std::max(0.0, v[8]);
            if (loops > 100000 || v.size() < 9 + refs) return Fail(L"wrong hatch outline or reference count");
            for (size_t i = 0; i < refs; ++i)
                if (depth == 0 && v[9 + i] >= 1) h.refs.push_back((uint64_t)v[9 + i]);  // file places for now
            for (const std::string& word : w)
                if (word.compare(0, 8, "pattern=") == 0) h.pattern = DecodeWord(word.substr(8));
            for (size_t i = 0; i < loops; ++i) {
                std::vector<std::string> lw;
                std::vector<double> lv;
                if (!Next(lw) || lw[0] != "loop" || !Numbers(lw, 1, lv) || lv.empty() || lv.size() != 1 + 2 * (size_t)lv[0])
                    return error.empty() ? Fail(L"a hatch outline is missing or wrong") : false;
                std::vector<Vec2> l;
                for (size_t k = 0; k < (size_t)lv[0]; ++k) l.push_back({lv[1 + 2 * k], lv[2 + 2 * k]});
                h.loops.push_back(std::move(l));
            }
            out = MakeHatch(std::move(h));
            return true;
        }
        if (key == "rel") {
            if (!need(9) || v[0] < 0 || v[0] > (double)RelKind::Midpoint) return Fail(L"a relationship needs its kind and elements");
            if (depth > 0) {  // inside a block or array: nothing to relate
                known = false;
                return true;
            }
            out = Entity{};
            out.type = EntityType::Relation;
            auto a = std::make_shared<Annot>();
            a->rel.kind = (RelKind)(int)v[0];
            for (size_t i = 0; i < 3; ++i) a->rel.refs[i] = {v[1 + 2 * i] >= 1 ? (uint64_t)v[1 + 2 * i] : 0, (int)v[2 + 2 * i]};
            a->rel.param = v[7];
            a->arrow = v[8] > 0 ? v[8] : 2.5;
            out.annot = std::move(a);
            return true;
        }
        if (key == "viewport") {
            if (!need(8) || v[2] == v[0] || v[3] == v[1] || !(v[6] > 0)) return Fail(L"a viewport needs two corners, a centre and a scale");
            out = MakeViewport({v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]}, v[6], v[7] != 0);
            return true;
        }
        if (key == "insert") {
            if (!need(5)) return Fail(L"a block reference needs 5 numbers");
            auto b = std::make_shared<BlockData>();  // only its name: the block is found later
            std::vector<std::wstring> values;
            for (const std::string& word : w) {
                if (word.compare(0, 5, "name=") == 0) b->name = DecodeWord(word.substr(5));
                else if (word.compare(0, 2, "v=") == 0) values.push_back(DecodeWord(word.substr(2)));
            }
            out = MakeInsert(b, {v[0], v[1]}, v[2], ToRadians(v[3]), std::move(values), v[4] != 0);
            return true;
        }
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
        } else if (key == "loop") {
            return Fail(L"a hatch outline on its own");
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
    if (m_settings.ltscale != 1) out += "ltscale " + FormatNumber(m_settings.ltscale) + "\n";
    if (m_settings.origin != Vec2{}) out += "origin " + FormatNumber(m_settings.origin.x) + " " + FormatNumber(m_settings.origin.y) + "\n";
    if (!m_settings.maintainRelations || m_settings.autoRelations || !m_settings.showRelations)
        out += std::string("relations ") + (m_settings.maintainRelations ? "1 " : "0 ") + (m_settings.autoRelations ? "1 " : "0 ") +
               (m_settings.showRelations ? "1" : "0") + "\n";
    for (const Variable& v : m_vars)
        out += "var name=" + EncodeWord(v.name) + " formula=" + EncodeWord(v.formula) +
               (v.comment.empty() ? "" : " comment=" + EncodeWord(v.comment)) + "\n";
    for (const Layer& l : m_layers) out += LayerLine("layer", l);
    out += "current " + std::to_string(m_current.layer) + " " + std::to_string(m_current.color) + " " +
           std::to_string(m_current.linetype) + " " + std::to_string(m_current.lineweight) + " " +
           FormatNumber(m_current.ltscale) + "\n";
    for (const LayerState& ls : m_states) {
        out += "layerstate " + std::to_string(ls.layers.size()) + " name=" + EncodeWord(ls.name) + "\n";
        for (const Layer& l : ls.layers) out += LayerLine("statelayer", l);
    }
    for (const auto& kv : m_props) out += "prop key=" + EncodeWord(kv.first) + " value=" + EncodeWord(kv.second) + "\n";
    BlockTable blocks = m_blocks;
    for (const auto& kv : m_entities) UsedBlocks(kv.second, blocks);
    for (const auto& b : blocks) WriteBlock(*b, out);
    Ordinals ordinals;
    size_t ord = 0;
    for (const auto& kv : m_entities) ordinals[kv.first] = ++ord;
    for (const auto& kv : m_entities) {
        WriteStyled(kv.second, out, &ordinals);
        out += '\n';
    }
    out += "end\n";
    return out;
}

std::string Document::ObjectsToText(const std::vector<Entity>& entities) const {
    std::string out = std::string(kMagic) + " " + std::to_string(kFormatVersion) + "\n";
    // The layers they are on, so another drawing can match them by name.
    std::set<int> used;
    for (const Entity& e : entities) used.insert(e.layer);
    for (const Layer& l : m_layers)
        if (used.count(l.id)) out += LayerLine("layer", l);
    BlockTable blocks;
    for (const Entity& e : entities) UsedBlocks(e, blocks);
    for (const auto& b : blocks) WriteBlock(*b, out);
    Ordinals ordinals;
    size_t ord = 0;
    for (const Entity& e : entities) ordinals[e.id] = ++ord;
    for (const Entity& e : entities) {
        WriteStyled(e, out, &ordinals);
        out += '\n';
    }
    out += "end\n";
    return out;
}

bool Document::ObjectsFromText(const std::string& text, std::vector<Entity>& out, std::vector<Layer>* layers,
                               BlockTable* blocks) {
    Document d;
    std::wstring error;
    if (!d.FromText(text, error)) return false;
    out.clear();
    for (const auto& kv : d.Entities()) out.push_back(kv.second);  // ids 1..n, refs among them
    if (layers) *layers = d.Layers();
    if (blocks) *blocks = d.Blocks();
    return true;
}

std::map<int, int> Document::MergeLayers(const std::vector<Layer>& from) {
    std::map<int, int> ids;
    for (const Layer& l : from) {
        if (const Layer* mine = LayerByName(l.name)) ids[l.id] = mine->id;
        else ids[l.id] = AddLayer(l);
    }
    return ids;
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
    std::vector<Layer> layers;
    std::vector<LayerState> states;
    CurrentStyle current;
    std::vector<Entity> ents;
    std::vector<long long> ordinalToIndex;
    std::vector<std::shared_ptr<BlockData>> rawBlocks;
    std::map<std::wstring, std::wstring> props;
    std::vector<Variable> vars;
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
        } else if (key == "layer" || key == "statelayer") {
            Layer l;
            if (!r.ReadLayer(w, l)) {
                error = r.error;
                return false;
            }
            if (key == "layer") layers.push_back(l);
            else if (!states.empty()) states.back().layers.push_back(l);
        } else if (key == "layerstate") {
            LayerState ls;
            for (const std::string& word : w)
                if (word.compare(0, 5, "name=") == 0) ls.name = DecodeWord(word.substr(5));
            states.push_back(ls);
        } else if (key == "var") {
            Variable var;
            for (const std::string& word : w) {
                if (word.compare(0, 5, "name=") == 0) var.name = DecodeWord(word.substr(5));
                if (word.compare(0, 8, "formula=") == 0) var.formula = DecodeWord(word.substr(8));
                if (word.compare(0, 8, "comment=") == 0) var.comment = DecodeWord(word.substr(8));
            }
            if (!var.name.empty()) vars.push_back(var);
        } else if (key == "current" || key == "ltscale" || key == "origin" || key == "relations") {
            std::vector<double> v;
            if (!r.Numbers(w, 1, v)) {
                error = r.error;
                return false;
            }
            if (key == "current" && v.size() >= 5) {
                current.layer = (int)v[0];
                current.color = (int32_t)v[1];
                current.linetype = std::clamp((int)v[2], -2, (int)LineTypes().size() - 1);
                current.lineweight = std::clamp((int)v[3], -3, 211);
                current.ltscale = v[4] > 0 ? v[4] : 1;
            } else if (key == "ltscale" && !v.empty() && v[0] > 0) {
                settings.ltscale = v[0];
            } else if (key == "origin" && v.size() >= 2) {
                settings.origin = {v[0], v[1]};
            } else if (key == "relations" && v.size() >= 3) {
                settings.maintainRelations = v[0] != 0;
                settings.autoRelations = v[1] != 0;
                settings.showRelations = v[2] != 0;
            }
        } else if (key == "prop") {
            std::wstring k, val;
            for (const std::string& word : w) {
                if (word.compare(0, 4, "key=") == 0) k = DecodeWord(word.substr(4));
                if (word.compare(0, 6, "value=") == 0) val = DecodeWord(word.substr(6));
            }
            if (!k.empty() && !val.empty()) props[k] = val;
        } else if (key == "block") {
            std::vector<double> v;
            if (!r.Numbers(w, 1, v) || v.size() < 3 || v[2] < 0 || v[2] > 1e7) {
                error = r.error.empty() ? L"Line " + std::to_wstring(r.pos) + L": a block needs its base point and size" : r.error;
                return false;
            }
            auto b = std::make_shared<BlockData>();
            b->base = {v[0], v[1]};
            for (const std::string& word : w)
                if (word.compare(0, 5, "name=") == 0) b->name = DecodeWord(word.substr(5));
            for (size_t i = 0; i < (size_t)v[2]; ++i) {
                std::vector<std::string> iw;
                Entity item;
                bool k = false;
                if (!r.Next(iw)) {
                    error = L"The block \"" + b->name + L"\" is incomplete.";
                    return false;
                }
                if (!r.ObjectStyled(iw, item, k, 1)) {
                    error = r.error;
                    return false;
                }
                if (k) b->items.push_back(std::move(item));
            }
            rawBlocks.push_back(std::move(b));
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
            if (!r.ObjectStyled(w, e, known)) {
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
    // Block references find their blocks by name (blocks inside blocks too).
    BlockTable table;
    std::set<std::wstring> busy;
    std::function<void(Entity&)> fix;
    std::function<std::shared_ptr<const BlockData>(const std::wstring&)> resolve = [&](const std::wstring& name) {
        for (const auto& b : table)
            if (SameName(b->name, name)) return b;
        std::wstring key = name;
        for (wchar_t& c : key) c = (wchar_t)std::towupper(c);
        if (busy.count(key)) return std::shared_ptr<const BlockData>();  // a block inside itself
        for (const auto& raw : rawBlocks) {
            if (!SameName(raw->name, name)) continue;
            busy.insert(key);
            BlockData copy = *raw;
            for (Entity& x : copy.items) fix(x);
            busy.erase(key);
            auto def = std::make_shared<const BlockData>(std::move(copy));
            table.push_back(def);
            return def;
        }
        return std::shared_ptr<const BlockData>();
    };
    fix = [&](Entity& e) {
        if (e.type == EntityType::Insert && e.annot && e.annot->block) {
            if (auto b = resolve(e.annot->block->name)) EditAnnot(e).block = b;
        } else if (e.type == EntityType::Array && e.array) {
            ArrayData a = *e.array;
            for (Entity& x : a.items) fix(x);
            e.array = std::make_shared<const ArrayData>(std::move(a));
        }
    };
    for (const auto& raw : rawBlocks) resolve(raw->name);
    for (Entity& e : ents) fix(e);
    // Objects get the ids 1, 2, 3... in file order: dimensions and hatches point at them.
    auto idOf = [&](uint64_t ord) -> uint64_t {
        if (ord >= 1 && ord <= ordinalToIndex.size() && ordinalToIndex[ord - 1] >= 0) {
            const size_t idx = (size_t)ordinalToIndex[ord - 1];
            if (ents[idx].type != EntityType::Dimension && ents[idx].type != EntityType::Hatch) return idx + 1;
        }
        return 0;
    };
    for (Entity& e : ents) {
        if (e.type != EntityType::Relation || !e.annot) continue;
        RelationData r = e.annot->rel;
        for (DimRef& ref : r.refs) ref.id = ref.id ? idOf(ref.id) : 0;
        EditAnnot(e).rel = r;
    }
    for (Entity& e : ents) {
        if (e.type != EntityType::Hatch || !e.annot || e.annot->hatch.refs.empty()) continue;
        HatchData& h = EditAnnot(e).hatch;
        std::vector<uint64_t> refs;
        for (uint64_t o : h.refs)
            if (uint64_t id = idOf(o)) refs.push_back(id);
        h.refs = refs;
    }
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
    if (!layers.empty()) {
        // Layer 0 is always there.
        bool zero = false;
        for (const Layer& l : layers) zero |= l.id == 0;
        if (!zero) layers.insert(layers.begin(), m_layers[0]);
        m_layers = layers;
        IndexLayers();
    }
    m_states = states;
    m_blocks = table;
    m_props = props;
    m_vars = vars;
    m_current = current;
    if (!FindLayer(m_current.layer)) m_current.layer = 0;
    for (Entity& e : ents) {
        if (!FindLayer(e.layer)) e.layer = 0;
        e.id = m_nextId++;
        m_entities[e.id] = std::move(e);
    }
    // Relationship marks next to their objects (not kept in the file).
    std::vector<uint64_t> gone;
    for (auto& kv : m_entities)
        if (kv.second.type == EntityType::Relation && !PlaceRelationMarks(*this, kv.second)) gone.push_back(kv.first);
    for (uint64_t id : gone) m_entities.erase(id);
    Touch();
    return true;
}

// ---------------------------------------------------------------------------
// DXF (release 12, AC1009): HEADER, TABLES (line type and layer 0) and
// ENTITIES. Polylines become POLYLINE/VERTEX/SEQEND with bulges.
// ---------------------------------------------------------------------------

bool Document::FromDxf(const std::string& text, std::wstring& error, std::wstring& notes) {
    DxfResult r;
    return FromDxf(text, error, notes, r);
}

bool Document::FromDxf(const std::string& text, std::wstring& error, std::wstring& notes, DxfResult& r) {
    if (!ReadDxf(text, r, error)) return false;
    DrawingSettings settings = m_settings;
    if (r.hasUnits) settings.units = r.units;
    if (r.hasDimStyle) settings.dimStyle = r.dimStyle;
    Clear();
    m_settings = settings;
    m_settings.precision = std::clamp(m_settings.precision, 0, 8);
    m_settings.ltscale = r.ltscale;
    m_layers = r.layers;
    IndexLayers();
    if (const Layer* cur = LayerByName(r.currentLayer)) m_current.layer = cur->id;
    m_blocks = r.blocks;
    for (Entity& e : r.entities) {
        e.id = m_nextId++;
        m_entities[e.id] = std::move(e);
    }
    r.entities.clear();
    notes = r.Notes();
    Touch();
    return true;
}

std::string Document::ToDxf(const Document* paper, Vec2 paperSize) const {
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
    // Line type patterns are in millimetres; DXF wants drawing units.
    double unitsPerMm = 1;
    switch (m_settings.units) {
        case Units::Centimetres: unitsPerMm = 0.1; break;
        case Units::Metres: unitsPerMm = 0.001; break;
        case Units::Inches: unitsPerMm = 1 / 25.4; break;
        case Units::Feet: unitsPerMm = 1 / 304.8; break;
        default: break;
    }
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
    group(9, "$LTSCALE");
    num(40, m_settings.ltscale);
    group(9, "$MEASUREMENT");
    group(70, (m_settings.units == Units::Inches || m_settings.units == Units::Feet) ? "0" : "1");
    const Box ext = Extents();
    if (!ext.Empty()) {
        group(9, "$EXTMIN");
        point(10, {ext.minX, ext.minY});
        group(9, "$EXTMAX");
        point(10, {ext.maxX, ext.maxY});
    }
    if (paper) {
        group(9, "$TILEMODE");
        group(70, "1");  // opens on the drawing; the sheet is the paper space tab
        group(9, "$PLIMMIN");
        num(10, 0.0);
        num(20, 0.0);
        group(9, "$PLIMMAX");
        num(10, paperSize.x);
        num(20, paperSize.y);
    }
    group(0, "ENDSEC");

    group(0, "SECTION");
    group(2, "TABLES");
    // Line types in use (and CONTINUOUS), in drawing units.
    std::set<int> usedLt = {0};
    for (const Layer& l : m_layers) usedLt.insert(l.linetype);
    for (const auto& kv : m_entities)
        if (kv.second.linetype >= 0) usedLt.insert(kv.second.linetype);
    group(0, "TABLE");
    group(2, "LTYPE");
    group(70, std::to_string(usedLt.size()));
    for (int lt : usedLt) {
        const LineTypeDef& def = LineTypes()[(size_t)lt];
        group(0, "LTYPE");
        group(2, TextToUtf8(def.name));
        group(70, "0");
        group(3, TextToUtf8(def.description));
        group(72, "65");
        group(73, std::to_string(def.pattern.size()));
        double total = 0;
        for (double d : def.pattern) total += std::fabs(d);
        num(40, total * unitsPerMm);
        for (double d : def.pattern) num(49, d * unitsPerMm);
    }
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
    bool anyConstruction = false;
    for (const auto& kv : m_entities) anyConstruction |= kv.second.construction;
    if (paper) {  // viewports carry their view as ACAD extended data
        group(0, "TABLE");
        group(2, "APPID");
        group(70, "1");
        group(0, "APPID");
        group(2, "ACAD");
        group(70, "0");
        group(0, "ENDTAB");
    }
    std::vector<Layer> layers = m_layers;
    if (paper)
        for (const Layer& l : paper->Layers())
            if (!LayerByName(l.name)) layers.push_back(l);
    group(0, "TABLE");
    group(2, "LAYER");
    group(70, std::to_string(layers.size() + (anyConstruction ? 1 : 0)));
    for (const Layer& l : layers) {
        group(0, "LAYER");
        group(2, TextToUtf8(l.name));
        group(70, std::to_string((l.frozen ? 1 : 0) | (l.locked ? 4 : 0)));
        const int aci = (l.color & kTrueColor) ? NearestAci((uint32_t)l.color & 0xFFFFFF) : std::clamp(l.color, 1, 255);
        group(62, std::to_string(l.on ? aci : -aci));  // negative: off
        group(6, TextToUtf8(LineTypeName(l.linetype)));
    }
    if (anyConstruction) {  // construction geometry: on the layer AutoCAD never prints
        group(0, "LAYER");
        group(2, "Defpoints");
        group(70, "0");
        group(62, "8");
        group(6, "CONTINUOUS");
    }
    group(0, "ENDTAB");
    group(0, "ENDSEC");

    // Layer, colour, line type and scale of an object.
    const Document* src = this;  // whose objects are being written (the sheet's: paper space)
    auto common = [&](const Entity& e) {
        const Layer* l = src->FindLayer(e.layer);
        group(8, e.construction ? "Defpoints" : TextToUtf8(l ? l->name : L"0"));
        if (src != this) group(67, "1");
        if (e.color == kByBlock) group(62, "0");
        else if (e.color != kByLayer)
            group(62, std::to_string((e.color & kTrueColor) ? NearestAci((uint32_t)e.color & 0xFFFFFF)
                                                           : std::clamp((int)e.color, 1, 255)));
        if (e.linetype >= 0 || e.linetype == kLtByBlock) group(6, TextToUtf8(LineTypeName(e.linetype)));
        if (e.ltscale != 1) num(48, e.ltscale);
    };
    std::function<void(const Entity&)> write;
    int viewportId = 1;
    // A paper space viewport (R12: its view as ACAD "MVIEW" extended data).
    auto writeViewport = [&](Vec2 a, Vec2 b, Vec2 viewCentre, double viewHeight, const Entity& e) {
        group(0, "VIEWPORT");
        if (viewportId == 1) {
            group(8, "0");
            group(67, "1");
        } else {
            common(e);
        }
        point(10, Lerp(a, b, 0.5));
        num(40, b.x - a.x);
        num(41, b.y - a.y);
        group(68, std::to_string(viewportId == 1 ? 1 : viewportId));
        group(69, std::to_string(viewportId));
        ++viewportId;
        group(1001, "ACAD");
        group(1000, "MVIEW");
        group(1002, "{");
        group(1070, "16");
        group(1010, "0.0"); group(1020, "0.0"); group(1030, "0.0");  // view target
        group(1010, "0.0"); group(1020, "0.0"); group(1030, "1.0");  // view direction
        num(1040, 0.0);                                              // twist
        num(1040, viewHeight);
        num(1040, viewCentre.x);
        num(1040, viewCentre.y);
        num(1040, 50.0);  // lens
        num(1040, 0.0);   // front and back clip
        num(1040, 0.0);
        group(1070, "0");    // view mode
        group(1070, "100");  // circle zoom
        group(1070, "1");    // fast zoom
        group(1070, "3");    // UCS icon
        group(1070, "0");    // snap, grid, snap style, isometric plane
        group(1070, "0");
        group(1070, "0");
        group(1070, "0");
        num(1040, 0.0);  // snap angle and base
        num(1040, 0.0);
        num(1040, 0.0);
        num(1040, 10.0);  // snap and grid spacing
        num(1040, 10.0);
        num(1040, 10.0);
        num(1040, 10.0);
        group(1070, "0");  // hidden lines
        group(1002, "{");  // frozen layers: none
        group(1002, "}");
        group(1002, "}");
    };
    // Lines, arcs, filled shapes and texts of an annotation.
    auto graphics = [&](const Entity& e, const AnnotGraphics& g) {
        for (const Prim& p : g.lines) {
            if (p.kind == Prim::Segment && Near(p.a, p.b, 1e-12)) {
                group(0, "POINT");
                common(e);
                point(10, p.a);
            } else if (p.kind == Prim::Segment) {
                group(0, "LINE");
                common(e);
                point(10, p.a);
                point(11, p.b);
            } else if (p.FullCircle()) {
                group(0, "CIRCLE");
                common(e);
                point(10, p.c);
                num(40, p.r);
            } else {
                group(0, "ARC");
                common(e);
                point(10, p.c);
                num(40, p.r);
                num(50, ToDegrees(NormalizeAngle(p.start)));
                num(51, ToDegrees(NormalizeAngle(p.start + p.sweep)));
            }
        }
        for (const auto& f : g.fills) {
            if (f.size() < 3) continue;
            group(0, "SOLID");
            common(e);
            point(10, f[0]);
            point(11, f[1]);
            point(12, f[2]);
            point(13, f.size() > 3 ? f[3] : f[2]);
        }
        for (const TextItem& t : g.texts) {
            if (t.text.empty()) continue;
            group(0, "TEXT");
            common(e);
            point(10, t.pos);
            num(40, t.height);
            group(1, DxfText(t.text));
            num(50, ToDegrees(t.angle));
            if (t.widthFactor != 1) num(41, t.widthFactor);
            group(72, std::to_string(t.align));
            point(11, t.pos);
            group(73, "2");
        }
    };
    // A solid fill (even-odd) as level strips of four-cornered SOLIDs.
    auto solidFill = [&](const Entity& e, const std::vector<std::vector<Vec2>>& loops) {
        std::vector<double> ys;
        for (const auto& l : loops)
            for (Vec2 p : l) ys.push_back(p.y);
        std::sort(ys.begin(), ys.end());
        ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
        struct X {
            double xm, x0, x1;
        };
        std::vector<X> xs;
        for (size_t i = 0; i + 1 < ys.size(); ++i) {
            const double y0 = ys[i], y1 = ys[i + 1], ym = (y0 + y1) / 2;
            if (y1 - y0 < 1e-12) continue;
            xs.clear();
            for (const auto& l : loops)
                for (size_t k = 0; k < l.size(); ++k) {
                    const Vec2 p = l[k], q = l[(k + 1) % l.size()];
                    if ((p.y > ym) == (q.y > ym)) continue;
                    auto at = [&](double y) { return p.x + (q.x - p.x) * (y - p.y) / (q.y - p.y); };
                    xs.push_back({at(ym), at(y0), at(y1)});
                }
            std::sort(xs.begin(), xs.end(), [](const X& a, const X& b) { return a.xm < b.xm; });
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                group(0, "SOLID");
                common(e);
                point(10, {xs[k].x0, y0});
                point(11, {xs[k + 1].x0, y0});
                point(12, {xs[k].x1, y1});
                point(13, {xs[k + 1].x1, y1});
            }
        }
    };
    write = [&](const Entity& e) {
        switch (e.type) {
            case EntityType::Line:
                group(0, "LINE");
                common(e);
                point(10, e.p0);
                point(11, e.p1);
                break;
            case EntityType::Circle:
                group(0, "CIRCLE");
                common(e);
                point(10, e.p0);
                num(40, e.radius);
                break;
            case EntityType::Arc:
                group(0, "ARC");
                common(e);
                point(10, e.p0);
                num(40, e.radius);
                num(50, ToDegrees(e.startAngle));
                num(51, ToDegrees(e.endAngle));
                break;
            case EntityType::Polyline:
                group(0, "POLYLINE");
                common(e);
                group(66, "1");
                point(10, {0, 0});
                group(70, e.closed ? "1" : "0");
                for (const Vertex& v : e.verts) {
                    group(0, "VERTEX");
                    common(e);
                    point(10, v.p);
                    if (v.bulge != 0) num(42, v.bulge);
                }
                group(0, "SEQEND");
                common(e);
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
                common(e);
                group(66, "1");
                point(10, {0, 0});
                group(70, closed ? "1" : "0");
                for (Vec2 p : pts) {
                    group(0, "VERTEX");
                    common(e);
                    point(10, p);
                }
                group(0, "SEQEND");
                common(e);
                break;
            }
            case EntityType::Point:
                group(0, "POINT");
                common(e);
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
                        common(e);
                        point(10, p.a);
                        point(11, p.b);
                    } else {
                        group(0, "ARC");
                        common(e);
                        point(10, p.c);
                        num(40, p.r);
                        num(50, ToDegrees(NormalizeAngle(p.start)));
                        num(51, ToDegrees(NormalizeAngle(p.start + p.sweep)));
                    }
                }
                for (const auto& a : g.arrows) {
                    group(0, "SOLID");
                    common(e);
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
                    common(e);
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
            case EntityType::Array: {
                std::vector<Entity> copies;
                ExpandArray(e, copies);
                for (const Entity& c : copies) write(c);
                break;
            }
            case EntityType::Text:
            case EntityType::Leader: {
                AnnotGraphics g;
                AnnotLayout(e, nullptr, g);
                graphics(e, g);
                break;
            }
            case EntityType::Hatch: {
                AnnotGraphics g;
                AnnotLayout(e, nullptr, g);
                if (g.solid) solidFill(e, g.fills);
                else graphics(e, g);
                break;
            }
            case EntityType::Relation:  // not in DXF
                break;
            case EntityType::Viewport:
                if (src != this && e.annot) writeViewport(e.p0, e.p1, e.annot->viewCentre, (e.p1.y - e.p0.y) / e.annot->viewScale, e);
                break;
            case EntityType::Insert: {
                if (!e.annot || !e.annot->block) break;
                const BlockData& b = *e.annot->block;
                const std::vector<const TextData*> defs = AttributeDefs(b);
                group(0, "INSERT");
                common(e);
                if (!defs.empty()) group(66, "1");
                group(2, TextToUtf8(b.name));
                point(10, e.p0);
                num(41, e.closed ? -e.radius : e.radius);
                num(42, e.radius);
                num(43, e.radius);
                num(50, ToDegrees(e.startAngle));
                if (defs.empty()) break;
                // The attributes, where the block puts them.
                size_t k = 0;
                for (const Entity& item : b.items) {
                    if (item.type != EntityType::Text || !item.annot || item.annot->text.tag.empty()) continue;
                    Entity x = item;
                    Translate(x, -b.base);
                    if (e.closed) Mirror(x, {0, 0}, {0, 1});
                    Scale(x, {0, 0}, e.radius > 0 ? e.radius : 1);
                    Rotate(x, {0, 0}, e.startAngle);
                    Translate(x, e.p0);
                    AnnotGraphics g;
                    TextData t = x.annot->text;
                    t.content = k < e.annot->values.size() ? e.annot->values[k] : t.content;
                    t.tag.clear();
                    LayoutText(t, x.p0, nullptr, g);
                    ++k;
                    if (g.texts.empty()) continue;
                    const TextItem& ti = g.texts[0];
                    group(0, "ATTRIB");
                    common(e);
                    point(10, ti.pos);
                    num(40, ti.height);
                    group(1, DxfText(SplitLines(t.content)[0]));
                    group(2, TextToUtf8(x.annot->text.tag));
                    group(70, "0");
                    num(50, ToDegrees(ti.angle));
                    if (ti.widthFactor != 1) num(41, ti.widthFactor);
                    group(72, std::to_string(ti.align));
                    point(11, ti.pos);
                    group(74, "2");
                }
                group(0, "SEQEND");
                common(e);
                break;
            }
        }
    };

    // Blocks: their objects (attribute definitions as ATTDEF).
    BlockTable blocks = m_blocks;
    for (const auto& kv : m_entities) UsedBlocks(kv.second, blocks);
    if (paper) {
        BlockTable more = paper->Blocks();
        for (const auto& kv : paper->Entities()) UsedBlocks(kv.second, more);
        for (const auto& b : more)
            if (std::none_of(blocks.begin(), blocks.end(), [&](const auto& x) { return SameName(x->name, b->name); }))
                blocks.push_back(b);
    }
    group(0, "SECTION");
    group(2, "BLOCKS");
    for (const auto& b : blocks) {
        group(0, "BLOCK");
        group(8, "0");
        group(2, TextToUtf8(b->name));
        group(70, AttributeDefs(*b).empty() ? "0" : "2");
        point(10, b->base);
        group(3, TextToUtf8(b->name));
        for (const Entity& item : b->items) {
            if (item.type != EntityType::Text || !item.annot || item.annot->text.tag.empty()) {
                write(item);
                continue;
            }
            const TextData& t = item.annot->text;
            AnnotGraphics g;
            TextData shown = t;
            shown.tag.clear();
            LayoutText(shown, item.p0, nullptr, g);
            if (g.texts.empty()) continue;
            const TextItem& ti = g.texts[0];
            group(0, "ATTDEF");
            common(item);
            point(10, ti.pos);
            num(40, ti.height);
            group(1, DxfText(SplitLines(t.content)[0]));
            group(3, TextToUtf8(t.prompt.empty() ? t.tag : t.prompt));
            group(2, TextToUtf8(t.tag));
            group(70, "0");
            num(50, ToDegrees(ti.angle));
            group(72, std::to_string(ti.align));
            point(11, ti.pos);
            group(74, "2");
        }
        group(0, "ENDBLK");
        group(8, "0");
    }
    group(0, "ENDSEC");

    group(0, "SECTION");
    group(2, "ENTITIES");
    for (const auto& kv : m_entities) write(kv.second);
    if (paper) {
        // The sheet: the paper space viewport (number 1, the page itself), then its objects.
        src = paper;
        writeViewport({0, 0}, paperSize, paperSize * 0.5, paperSize.y, Entity{});
        for (const auto& kv : paper->Entities()) write(kv.second);
        src = this;
    }
    group(0, "ENDSEC");
    group(0, "EOF");
    return out;
}
