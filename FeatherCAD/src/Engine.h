// Engine.h - the command engine: runs drawing and editing commands the
// way CAD users expect. A command asks for input with a Prompt
// ("Specify next point or [Close/Undo]:"); the answer comes from a click
// in the drawing (already snapped), from text typed in the command line
// (coordinates "10,20", "@5,0", "@50<30", a distance, a number, an option
// letter), or from Enter / Esc.
//
// The engine also owns the selection and the drafting aids (grid, snap,
// ortho, object snaps). It has no Windows code: the window side implements
// EngineHost, and the unit tests drive it with typed input alone.
#pragma once
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Document.h"
#include "Snap.h"

struct Keyword {
    std::wstring key;   // what is typed: "C"
    std::wstring name;  // what is shown: "Close"
};

enum Accept : unsigned {
    kAcceptPoint = 1,     // a click or typed coordinates
    kAcceptNumber = 2,    // a number (distance, radius, angle...)
    kAcceptKeyword = 4,   // one of the prompt's options
    kAcceptEmpty = 8,     // Enter on its own
    kAcceptSelect = 16,   // picking objects
};

struct Prompt {
    std::wstring text;              // "Specify next point"
    std::vector<Keyword> keywords;  // [Close/Undo]
    std::wstring defaultText;       // <10.00>
    unsigned accept = 0;
    bool hasBase = false;  // rubber band origin: ortho and direct distances use it
    Vec2 base;
    std::wstring FullText() const;  // "Specify next point or [Close/Undo] <10.00>:"
};

// Temporary geometry shown while a command runs.
struct Preview {
    std::vector<Entity> entities;
    std::vector<std::pair<Vec2, Vec2>> guides;  // dashed helper lines
    void Clear() { entities.clear(); guides.clear(); }
};

// Grid, snap, ortho and object snap settings.
struct Drafting {
    bool gridOn = true;
    double gridSpacing = 10;
    bool snapOn = false;
    double snapSpacing = 10;
    bool orthoOn = false;
    bool osnapOn = true;
    unsigned osnapModes = kSnapDefault;
};

class EngineHost {
public:
    virtual ~EngineHost() = default;
    virtual void Message(const std::wstring& line) = 0;  // a line in the command history
    // Where the cursor is now (ortho applied): direct distances point that way.
    virtual Vec2 TrackedCursor() = 0;
    // Commands the window handles itself (OPEN, SAVE, UNITS...). True if handled.
    virtual bool HostCommand(const std::wstring& name) = 0;
    virtual void ZoomExtents() = 0;
    virtual void ZoomWindow(const Box& box) = 0;
    virtual void ZoomPrevious() = 0;
    virtual void ZoomFactor(double factor) = 0;
    virtual void DraftingChanged() = 0;  // status bar toggles
};

struct CommandInfo {
    std::wstring name;                 // "LINE"
    std::vector<std::wstring> aliases; // "L"
    std::wstring title;                // "Line"
    std::wstring description;          // shown in the command search
    bool host = false;                 // handled by EngineHost::HostCommand
};
const std::vector<CommandInfo>& AllCommands();
const CommandInfo* FindCommand(const std::wstring& nameOrAlias);

class Command;

class Engine {
public:
    Engine(Document& doc, EngineHost& host);
    ~Engine();

    // ---- input --------------------------------------------------------------
    void Run(const std::wstring& name);   // start a command (menus, toolbar)
    void Text(const std::wstring& typed);  // Enter in the command line
    void Point(Vec2 p);                    // a click in the drawing
    void Enter();                          // Enter / Space / right-click "Enter"
    void Cancel();                         // Esc
    void Keyword(const std::wstring& key); // an option chosen from a menu

    // ---- state ---------------------------------------------------------------
    bool Active() const { return (bool)m_cmd; }
    std::wstring ActiveName() const { return m_cmdName; }
    const Prompt& CurrentPrompt() const { return m_prompt; }
    bool WantsPoint() const { return m_cmd && (m_prompt.accept & kAcceptPoint); }
    // Clicks pick objects (while selecting for a command, or with no command).
    bool WantsSelection() const { return !m_cmd || m_selecting; }
    bool PanMode() const { return m_panMode; }
    void GetPreview(Vec2 cursor, Preview& out);
    std::wstring LastCommand() const { return m_lastCommand; }
    const std::vector<std::wstring>& RecentCommands() const { return m_recent; }
    Vec2 LastPoint() const { return m_lastPoint; }

    // One-shot object snap typed at a point prompt (END, MID...), or 0.
    unsigned SnapOverride() const { return m_snapOverride; }
    void SetSnapOverride(unsigned mode) { m_snapOverride = mode; }
    Drafting& Draft() { return m_draft; }

    // ---- selection ----------------------------------------------------------
    const std::set<uint64_t>& Selection() const { return m_selection; }
    uint64_t SelectionRevision() const { return m_selRevision; }
    void AddToSelection(const std::vector<uint64_t>& ids);
    void RemoveFromSelection(const std::vector<uint64_t>& ids);
    void SelectAll();
    void ClearSelection();
    void PruneSelection();  // after undo/redo: drop objects that are gone

    // ---- for commands ---------------------------------------------------------
    Document& Doc() { return m_doc; }
    EngineHost& Host() { return m_host; }
    void Ask(Prompt p);
    void AskSelection();  // uses the current selection if there is one
    void Finish();        // the command is done
    void Msg(const std::wstring& line) { m_host.Message(line); }
    std::wstring Len(double v) const;    // formatted with the drawing's precision
    std::wstring Ang(double rad) const;  // degrees
    std::wstring Pt(Vec2 p) const;
    void SetPanMode(bool on) { m_panMode = on; }
    void SetLastPoint(Vec2 p) { m_lastPoint = p; }
    // Remembered between commands, like other CAD programs do.
    double lastRadius = 0, lastRotation = 0, lastRectLength = 0, lastRectWidth = 0;

private:
    bool ParsePoint(const std::wstring& text, Vec2& out) const;
    bool MatchKeyword(const std::wstring& text, std::wstring& key) const;
    void Accepted(Vec2 p);
    void Echo(const std::wstring& typed);
    void Start(const CommandInfo& info);

    Document& m_doc;
    EngineHost& m_host;
    std::unique_ptr<Command> m_cmd;
    std::wstring m_cmdName;
    Prompt m_prompt;
    bool m_selecting = false;
    bool m_panMode = false;
    std::wstring m_lastCommand;
    std::vector<std::wstring> m_recent;
    Vec2 m_lastPoint;
    unsigned m_snapOverride = 0;
    Drafting m_draft;
    std::set<uint64_t> m_selection;
    uint64_t m_selRevision = 1;
};

// A running command. Each one is a small state machine; see Commands.cpp.
class Command {
public:
    virtual ~Command() = default;
    virtual void Start() = 0;
    virtual void OnPoint(Vec2) {}
    virtual void OnNumber(double) {}
    virtual void OnKeyword(const std::wstring&) {}
    virtual void OnEnter() { E->Finish(); }
    virtual void OnSelected() {}
    // Typed text the command reads itself (before the usual parsing). True if used.
    virtual bool OnText(const std::wstring&) { return false; }
    virtual void OnCancel() {}  // before the engine ends the command
    virtual void GetPreview(Vec2, Preview&) {}

    Engine* E = nullptr;
};

std::unique_ptr<Command> CreateCommand(const std::wstring& name);

// "12.50" with `precision` decimals; "-0.00" is shown as "0.00".
std::wstring FormatDecimal(double v, int precision);
// Degrees with up to `precision` decimals, trailing zeros removed: "30", "12.5".
std::wstring FormatDegrees(double radians, int precision);
