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
#include "Grips.h"
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
    bool pickObject = false;  // the click picks an object (pick box, no object snaps)
    bool spaces = false;      // typed text may contain spaces (space does not act as Enter)
    bool freehand = false;    // dragging with the button held draws a stroke (FreeSketch)
    bool hasClose = false;    // a click at closePoint closes the shape being drawn
    Vec2 closePoint;
    std::wstring FullText() const;  // "Specify next point or [Close/Undo] <10.00>:"
};

// Temporary geometry shown while a command runs.
struct Preview {
    std::vector<Entity> entities;
    std::vector<std::pair<Vec2, Vec2>> guides;  // dashed helper lines
    std::vector<Entity> removed;                // what would be taken away (trim)
    void Clear() {
        entities.clear();
        guides.clear();
        removed.clear();
    }
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
    bool polarOn = true;      // polar tracking (F10); ortho and polar exclude each other
    double polarAngle = 15;   // degrees between tracking directions
    bool otrackOn = true;     // object snap tracking (F11)
    bool dynOn = true;        // input and prompt at the cursor (F12)
    bool lwDisplay = true;    // show line weights (LWT)
    // Isometric drawing: grid and snap on the isometric grid, ortho along
    // the two axes of the plane (0 left, 1 top, 2 right); with isoScale,
    // lengths typed along the axes are drawn x 0.816 (isometric projection).
    bool isoOn = false;
    int isoPlane = 1;
    bool isoScale = false;
};

class EngineHost {
public:
    virtual ~EngineHost() = default;
    virtual void Message(const std::wstring& line) = 0;  // a line in the command history
    // Where the cursor is now (ortho applied): direct distances point that way.
    virtual Vec2 TrackedCursor() = 0;
    // How far from an object a click still picks it, in drawing units.
    virtual double PickRadius() = 0;
    // Commands the window handles itself (OPEN, SAVE, UNITS...). True if handled.
    virtual bool HostCommand(const std::wstring& name) = 0;
    virtual void ZoomExtents() = 0;
    virtual void ZoomWindow(const Box& box) = 0;
    virtual void ZoomPrevious() = 0;
    virtual void ZoomFactor(double factor) = 0;
    virtual void DraftingChanged() = 0;  // status bar toggles
    // The clipboard, for copy and paste between drawings (the program uses
    // the Windows clipboard; the tests this one).
    virtual void SetClipboard(const std::string& text) { m_clip = text; }
    virtual bool GetClipboard(std::string& text) {
        text = m_clip;
        return !text.empty();
    }
    // A text editor (content, font, size, symbols, fields). 1: changed,
    // 0: cancelled, -1: there is none (the command line asks instead).
    virtual int EditText(TextData& text, const std::wstring& title) {
        (void)text;
        (void)title;
        return -1;
    }
    // A picture file chosen by the user, with its size in pixels. False: none.
    // A symbol from the library (Symbols.h), chosen in a window with
    // pictures. 1: chosen, 0: cancelled, -1: there is no such window (the
    // command line asks for its name).
    virtual int ChooseSymbol(std::wstring& name) {
        (void)name;
        return -1;
    }
    virtual bool ChooseImage(std::wstring& path, int& width, int& height) {
        (void)path, (void)width, (void)height;
        return false;
    }

private:
    std::string m_clip;
};

struct CommandInfo {
    std::wstring name;                 // "LINE"
    std::vector<std::wstring> aliases; // "L"
    std::wstring title;                // "Line"
    std::wstring description;          // shown in the command search
    bool host = false;                 // handled by EngineHost::HostCommand
};
const std::vector<CommandInfo>& AllCommands();
std::wstring PlainPrompt(const std::wstring& text, unsigned accept);
const CommandInfo* FindCommand(const std::wstring& nameOrAlias);

class Command;

class Engine {
public:
    Engine(Document& doc, EngineHost& host);
    ~Engine();

    // ---- input --------------------------------------------------------------
    void Run(const std::wstring& name);   // start a command (menus, toolbar)
    // A grip of the selection was clicked: edit with it (stretch, move...).
    void StartGripEdit(const Grip& hot);
    void Text(const std::wstring& typed);  // Enter in the command line
    void Point(Vec2 p, bool shift = false);  // a click in the drawing (Shift held?)
    void Stroke(const std::vector<Vec2>& pts);  // a freehand stroke (prompt.freehand)
    void Enter();                          // Enter / Space / right-click "Enter"
    void Cancel();                         // Esc
    void Keyword(const std::wstring& key); // an option chosen from a menu

    // ---- state ---------------------------------------------------------------
    bool Active() const { return (bool)m_cmd; }
    std::wstring ActiveName() const { return m_cmdName; }
    const Prompt& CurrentPrompt() const { return m_prompt; }
    bool WantsPoint() const { return m_cmd && (m_prompt.accept & kAcceptPoint); }
    bool WantsStroke() const { return WantsPoint() && m_prompt.freehand; }
    // Clicks pick objects (while selecting for a command, or with no command).
    bool WantsSelection() const { return !m_cmd || m_selecting; }
    bool PanMode() const { return m_panMode; }
    void GetPreview(Vec2 cursor, Preview& out);
    std::wstring LastCommand() const { return m_lastCommand; }
    const std::vector<std::wstring>& RecentCommands() const { return m_recent; }
    Vec2 LastPoint() const { return m_lastPoint; }
    bool LastPointShift() const { return m_lastShift; }  // Shift was held for the last click
    // The object under p (within the pick radius), the top one if several; 0
    // if none. Objects on locked layers only when `lockedToo` (to measure,
    // not to change).
    uint64_t PickEntity(Vec2 p, bool lockedToo = false) const;

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
    double lastOffset = -1;  // < 0: through a point
    double lastFilletRadius = 0, lastChamfer1 = 0, lastChamfer2 = 0, lastChamferAngle = 0;
    bool chamferByAngle = false, cornerTrim = true;
    double lastScale = 1, lastLengthen = 0;
    int lastColumns = 4, lastRows = 3, lastPolarCount = 6, lastPathCount = 5;
    int lastSides = 4;
    bool lastInscribed = true;
    uint64_t lastDim = 0;  // the last linear or aligned dimension (DIMCONTINUE, DIMBASELINE)
    TextData lastText;     // settings of the last text (TEXT)
    HatchData lastHatch;   // pattern, scale and angle of the last hatch
    std::wstring lastBlock;
    double lastCloudArc = 15;
    // On a sheet: the drawing its viewports show (nullptr in the drawing itself).
    const Document* model = nullptr;

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
    bool m_lastShift = false;
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
    // Enter while selecting with nothing selected: true if the command goes on.
    virtual bool OnNothingSelected() { return false; }
    // Typed text the command reads itself (before the usual parsing). True if used.
    virtual bool OnText(const std::wstring&) { return false; }
    virtual void OnCancel() {}  // before the engine ends the command
    virtual void GetPreview(Vec2, Preview&) {}
    virtual void OnStroke(const std::vector<Vec2>&) {}

    Engine* E = nullptr;
};

std::unique_ptr<Command> CreateCommand(const std::wstring& name);
std::unique_ptr<Command> CreateGripEdit(const std::vector<uint64_t>& ids, const Grip& hot);

// "12.50" with `precision` decimals; "-0.00" is shown as "0.00".
std::wstring FormatDecimal(double v, int precision);
// Degrees with up to `precision` decimals, trailing zeros removed: "30", "12.5".
std::wstring FormatDegrees(double radians, int precision);
