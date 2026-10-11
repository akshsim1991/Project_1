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
#include <string>
#include <vector>

#include "Dimension.h"
#include "Entity.h"

enum class Units : uint8_t { Unitless, Millimetres, Centimetres, Metres, Inches, Feet };

const wchar_t* UnitsName(Units u);    // "Millimetres"
const wchar_t* UnitsSymbol(Units u);  // "mm"

struct DrawingSettings {
    Units units = Units::Millimetres;
    int precision = 2;  // decimal places shown for lengths and coordinates
    DimStyle dimStyle;  // for new dimensions
};

class Document {
public:
    using Map = std::map<uint64_t, Entity>;  // keyed by id: id order = drawing order

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
    void ReplaceAll(std::vector<Entity> entities, const std::wstring& name);
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

    // ---- files ------------------------------------------------------------
    std::string ToText() const;  // Feather CAD drawing (.fcad, UTF-8 text)
    // Replaces the drawing (and clears the history). On failure the
    // document is unchanged and `error` says why.
    bool FromText(const std::string& text, std::wstring& error);
    // AutoCAD DXF, release 12 (read by every CAD program).
    std::string ToDxf() const;
    // Opens a DXF drawing (any release, ASCII) in place of this one. What
    // could not be brought in is listed in `notes`.
    bool FromDxf(const std::string& text, std::wstring& error, std::wstring& notes);
    // Objects as text, and back (copy and paste between drawings).
    static std::string ObjectsToText(const std::vector<Entity>& entities);
    static bool ObjectsFromText(const std::string& text, std::vector<Entity>& out);

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
    };
    void Record(uint64_t id, const Entity* before, const Entity* after);
    // Dimensions follow the objects they are attached to (before a step is kept).
    void UpdateDimensions();
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
};

// UTF-8 text (without Windows), for files.
std::string TextToUtf8(const std::wstring& w);
std::wstring TextFromUtf8(const std::string& s);

// Locale-independent number text: shortest form that reads back exactly.
std::string FormatNumber(double v);
bool ParseNumber(const std::string& s, double& v);
