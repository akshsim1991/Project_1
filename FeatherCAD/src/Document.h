// Document.h - a drawing: its entities, settings and undo history, and
// conversion to and from the Feather CAD file format (.fcad) and DXF.
//
// Every change goes through Add / Erase / Replace between BeginChange and
// EndChange; one such group is one undo step ("Undo Line"). The class is
// plain C++ (no Windows), so the unit tests use it directly.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Annot.h"
#include "Dimension.h"
#include "Entity.h"
#include "Style.h"

enum class Units : uint8_t { Unitless, Millimetres, Centimetres, Metres, Inches, Feet };

const wchar_t* UnitsName(Units u);    // "Millimetres"
const wchar_t* UnitsSymbol(Units u);  // "mm"

struct DrawingSettings {
    Units units = Units::Millimetres;
    int precision = 2;  // decimal places shown for lengths and coordinates
    DimStyle dimStyle;  // for new dimensions
    double ltscale = 1;  // all line type patterns are this many times larger
    Vec2 origin;         // grid origin: coordinates are typed and shown from here
    bool operator==(const DrawingSettings& o) const {
        return units == o.units && precision == o.precision && dimStyle == o.dimStyle && ltscale == o.ltscale &&
               origin == o.origin;
    }
};

// How an object is drawn, with "by layer" and "by block" worked out.
struct Look {
    uint32_t rgb = 0;
    bool foreground = false;  // colour 7: black or white, whichever shows
    int32_t color = 7;        // the colour number (ACI) or true colour it came from
    int linetype = 0;
    int lineweight = kDefaultLineWeight;  // hundredths of a millimetre
    double ltscale = 1;
    bool construction = false;
    bool locked = false;
};

using BlockTable = std::vector<std::shared_ptr<const BlockData>>;

class Document {
public:
    using Map = std::map<uint64_t, Entity>;  // keyed by id: id order = drawing order

    Document();  // an empty drawing with layer "0"

    const Map& Entities() const { return m_entities; }
    const Entity* Find(uint64_t id) const;
    size_t Count() const { return m_entities.size(); }

    // ---- changes ----------------------------------------------------------
    void BeginChange(const std::wstring& name);
    void EndChange();     // commits the group (an empty group leaves no step)
    void CancelChange();  // takes back everything since BeginChange
    bool InChange() const { return m_open.has_value(); }
    // Outside BeginChange/EndChange each of these is a step of its own.
    uint64_t Add(Entity e);
    void Erase(uint64_t id);
    void Replace(const Entity& e);  // same id, new geometry
    // Puts back an object as it was (same id), whether it still exists or
    // was erased: commands use it to take back one of their own steps.
    void Restore(const Entity& e);
    // Replaces the whole drawing in one undo step (restoring a version).
    void ReplaceAll(std::vector<Entity> entities, const std::wstring& name, const BlockTable* blocks = nullptr);
    // Adds objects together, keeping the dimensions among them attached to
    // the new copies (their ids in `entities` are the old ones). Returns the
    // new ids, in order.
    std::vector<uint64_t> AddGroup(std::vector<Entity> entities);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    std::wstring UndoName() const { return m_undo.empty() ? L"" : m_undo.back().name; }
    std::wstring RedoName() const { return m_redo.empty() ? L"" : m_redo.back().name; }
    bool Undo();
    bool Redo();

    // Changes with every modification (including undo and redo): views use
    // it to know when to redraw, auto-save to know when to save.
    uint64_t Revision() const { return m_revision; }
    bool Modified() const;
    void MarkSaved();
    void MarkModified() { m_settingsChanged = true; }  // e.g. a recovered copy

    const DrawingSettings& Settings() const { return m_settings; }
    void SetSettings(const DrawingSettings& s);

    void Clear();  // empty drawing, no history
    Box Extents() const;

    // ---- layers and styles --------------------------------------------------
    const std::vector<Layer>& Layers() const { return m_layers; }
    const Layer* FindLayer(int id) const;
    const Layer* LayerByName(const std::wstring& name) const;  // not case sensitive
    // Layer changes are undo steps of their own (or part of the open one).
    int AddLayer(Layer layer);  // a new id; the name is made unique
    void UpdateLayer(const Layer& layer);
    bool DeleteLayer(int id, std::wstring& why);  // not "0", not current, not used
    const CurrentStyle& Current() const { return m_current; }
    void SetCurrent(const CurrentStyle& c);
    const std::vector<LayerState>& LayerStates() const { return m_states; }
    void SaveLayerState(const std::wstring& name);
    bool RestoreLayerState(const std::wstring& name);
    void DeleteLayerState(const std::wstring& name);
    bool Visible(const Entity& e) const;   // its layer is on and not frozen
    bool Editable(const Entity& e) const;  // visible and its layer not locked
    // `block`: the block reference the object is drawn in, for "by block".
    Look LookOf(const Entity& e, const Entity* block = nullptr) const;
    size_t LayerUse(int id) const;  // objects on it

    // ---- drawing properties (title, drawing number...: title block fields) ----
    const std::map<std::wstring, std::wstring>& Properties() const { return m_props; }
    void SetProperty(const std::wstring& key, const std::wstring& value);  // empty: removed

    // ---- blocks -------------------------------------------------------------
    const BlockTable& Blocks() const { return m_blocks; }
    std::shared_ptr<const BlockData> FindBlock(const std::wstring& name) const;  // not case sensitive
    // Adds a block, or redefines the one of that name (its references in the
    // drawing change with it). An undo step of its own, or part of the open one.
    std::shared_ptr<const BlockData> DefineBlock(BlockData b);
    bool DeleteBlock(const std::wstring& name, std::wstring& why);  // only when not used
    size_t BlockUse(const std::wstring& name) const;                // references to it
    // Blocks of another drawing: those missing here are added; references in
    // `ents` are pointed at the blocks here (same name).
    void MergeBlocks(const BlockTable& from, std::vector<Entity>* ents = nullptr);
    // Objects from another drawing, with its layers and blocks: those are
    // matched here by name (missing ones are added), then the objects are
    // added as AddGroup does. Returns the new ids.
    std::vector<uint64_t> AddForeign(std::vector<Entity> ents, const std::vector<Layer>& layers, const BlockTable& blocks);

    // ---- files ------------------------------------------------------------
    std::string ToText() const;  // Feather CAD drawing (.fcad, UTF-8 text)
    // Replaces the drawing (and clears the history). On failure the
    // document is unchanged and `error` says why.
    bool FromText(const std::string& text, std::wstring& error);
    // AutoCAD DXF, release 12 (read by every CAD program).
    // With `paper`: that sheet as AutoCAD's paper space (its viewports as
    // viewports), on a page of `paperSize` millimetres.
    std::string ToDxf(const Document* paper = nullptr, Vec2 paperSize = {}) const;
    // Opens a DXF drawing (any release, ASCII) in place of this one. What
    // could not be brought in is listed in `notes`.
    bool FromDxf(const std::string& text, std::wstring& error, std::wstring& notes);
    // As FromDxf, also giving the paper space layouts (DxfImport.h).
    bool FromDxf(const std::string& text, std::wstring& error, std::wstring& notes, struct DxfResult& result);
    // Objects as text, and back (copy and paste between drawings).
    std::string ObjectsToText(const std::vector<Entity>& entities) const;  // with their layers
    static bool ObjectsFromText(const std::string& text, std::vector<Entity>& out, std::vector<Layer>* layers = nullptr,
                                BlockTable* blocks = nullptr);
    // Layers of another drawing, matched by name (missing ones are added):
    // their ids there -> ids here.
    std::map<int, int> MergeLayers(const std::vector<Layer>& from);

    // Undo step serials, for tests.
    size_t UndoDepth() const { return m_undo.size(); }

private:
    struct Change {
        uint64_t id;
        std::optional<Entity> before, after;
    };
    struct Step {
        std::wstring name;
        std::vector<Change> changes;
        uint64_t serial = 0;
        std::optional<std::vector<Layer>> layersBefore, layersAfter;
        std::optional<BlockTable> blocksBefore, blocksAfter;
    };
    void SetBlocks(BlockTable blocks, const std::wstring& stepName);
    void SetLayers(std::vector<Layer> layers, const std::wstring& stepName);
    void IndexLayers();
    void Record(uint64_t id, const Entity* before, const Entity* after);
    // Dimensions and hatches follow the objects they are attached to (before a step is kept).
    void UpdateDimensions();
    void UpdateHatches(const std::set<uint64_t>& changed, const std::set<uint64_t>& touched);
    void Apply(const Step& step, bool forward);
    void Touch() { ++m_revision; }

    Map m_entities;
    uint64_t m_nextId = 1;
    std::optional<Step> m_open;
    std::map<uint64_t, size_t> m_openIndex;  // id -> index in m_open->changes
    std::vector<Step> m_undo, m_redo;
    uint64_t m_nextSerial = 1;
    uint64_t m_savedSerial = 0;  // serial on top of the undo stack when saved
    bool m_settingsChanged = false;
    uint64_t m_revision = 1;
    mutable uint64_t m_extentsRevision = 0;
    mutable Box m_extents;
    DrawingSettings m_settings;
    std::vector<Layer> m_layers;
    std::vector<int> m_layerIndex;  // id -> index in m_layers (-1: none)
    CurrentStyle m_current;
    std::vector<LayerState> m_states;
    BlockTable m_blocks;
    std::map<std::wstring, std::wstring> m_props;
};

// Text as one word of the file format (spaces and = escaped), and back.
std::string FcadEncode(const std::wstring& w);
std::wstring FcadDecode(const std::string& s);

// UTF-8 text (without Windows), for files.
std::string TextToUtf8(const std::wstring& w);
std::wstring TextFromUtf8(const std::string& s);

// Locale-independent number text: shortest form that reads back exactly.
std::string FormatNumber(double v);
bool ParseNumber(const std::string& s, double& v);
