// MainWindow.cpp - see MainWindow.h.
#include "MainWindow.h"

#include <algorithm>

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include "Dialogs.h"
#include "Icons.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr UINT_PTR kAutosaveTimer = 1;
constexpr int kMenuBarDip = 30, kToolBarDip = 40;

const wchar_t kGlyphNew[] = L"\xE7C3";
const wchar_t kGlyphOpen[] = L"\xE8E5";
const wchar_t kGlyphSave[] = L"\xE74E";
const wchar_t kGlyphUndo[] = L"\xE7A7";
const wchar_t kGlyphRedo[] = L"\xE7A6";

// Menu and toolbar ids that start an engine command.
struct IdCommand {
    int id;
    const wchar_t* name;
    const wchar_t* keyword;  // option chosen right away (e.g. CIRCLE 2P)
};
const IdCommand kIdCommands[] = {
    {ID_NEW, L"NEW", nullptr},
    {ID_OPEN, L"OPEN", nullptr},
    {ID_SAVE, L"SAVE", nullptr},
    {ID_SAVE_AS, L"SAVEAS", nullptr},
    {ID_EXPORT_DXF, L"DXFOUT", nullptr},
    {ID_HISTORY, L"HISTORY", nullptr},
    {ID_RECOVER, L"RECOVER", nullptr},
    {ID_EXIT, L"EXIT", nullptr},
    {ID_UNDO, L"UNDO", nullptr},
    {ID_REDO, L"REDO", nullptr},
    {ID_SELECT_ALL, L"SELECTALL", nullptr},
    {ID_ERASE, L"ERASE", nullptr},
    {ID_LINE, L"LINE", nullptr},
    {ID_PLINE, L"PLINE", nullptr},
    {ID_CIRCLE, L"CIRCLE", nullptr},
    {ID_CIRCLE_2P, L"CIRCLE", L"2P"},
    {ID_CIRCLE_3P, L"CIRCLE", L"3P"},
    {ID_ARC, L"ARC", nullptr},
    {ID_ARC_CENTRE, L"ARC", L"C"},
    {ID_RECTANGLE, L"RECTANG", nullptr},
    {ID_MOVE, L"MOVE", nullptr},
    {ID_COPY, L"COPY", nullptr},
    {ID_ROTATE, L"ROTATE", nullptr},
    {ID_ZOOM_WINDOW, L"ZOOM", L"W"},
    {ID_PAN, L"PAN", nullptr},
    {ID_DIST, L"DIST", nullptr},
    {ID_ID_POINT, L"ID", nullptr},
    {ID_UNITS, L"UNITS", nullptr},
    {ID_OSNAP_SETTINGS, L"OSNAP", nullptr},
    {ID_ABOUT, L"ABOUT", nullptr},
};

const unsigned kSnapBits[] = {kSnapEndpoint, kSnapMidpoint, kSnapCentre, kSnapQuadrant,
                              kSnapIntersection, kSnapPerpendicular, kSnapTangent, kSnapNearest};

void Append(HMENU m, int id, const std::wstring& text, bool enabled = true, bool checked = false) {
    AppendMenuW(m, MF_STRING | (enabled ? 0 : MF_GRAYED) | (checked ? MF_CHECKED : 0), (UINT_PTR)id, text.c_str());
}
void Separator(HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); }
void Submenu(HMENU m, HMENU sub, const wchar_t* text) { AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, text); }
}  // namespace

MainWindow::MainWindow() : m_engine(m_doc, *this) {}
MainWindow::~MainWindow() {
    if (m_accel) DestroyAcceleratorTable(m_accel);
}

bool MainWindow::Create(HINSTANCE inst, int showCmd, const std::wstring& file) {
    m_settings.Load();
    ReloadTheme((ThemeMode)m_settings.themeMode);
    m_engine.Draft() = m_settings.drafting;
    DrawingSettings ds;
    ds.units = (Units)m_settings.newUnits;
    ds.precision = m_settings.newPrecision;
    m_doc.SetSettings(ds);
    m_doc.MarkSaved();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = APP_WINDOW_CLASS;
    RegisterClassExW(&wc);
    m_hwnd = CreateWindowExW(0, APP_WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                             CW_USEDEFAULT, 1280, 820, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    ApplyWindowTheme(m_hwnd);
    CreateChildren();
    CreateAccelerators();

    if (m_settings.hasPlacement) {
        WINDOWPLACEMENT wp = m_settings.placement;
        if (wp.showCmd == SW_SHOWMINIMIZED) wp.showCmd = SW_SHOWNORMAL;
        if (showCmd != SW_SHOWDEFAULT && showCmd != SW_SHOWNORMAL) wp.showCmd = showCmd;
        SetWindowPlacement(m_hwnd, &wp);
    } else {
        ShowWindow(m_hwnd, showCmd);
    }
    UpdateWindow(m_hwnd);

    m_history.SetDrawing(L"", m_untitledName);
    m_lastAutosave = GetTickCount64();
    Message(std::wstring(APP_NAME) + L" " + APP_VERSION +
            L". Type a command (L for line, C for circle...) or press Ctrl+K to search. F1 lists the commands.");
    const bool crashed = PreviousRunCrashed();
    MarkRunning(true);
    History::CleanOld(30);
    if (!file.empty()) {
        OpenFile(file);
    } else {
        m_canvas.ZoomExtents();
        if (crashed) {
            std::vector<HistoryDrawing> all = History::AllDrawings();
            const bool unsaved = std::any_of(all.begin(), all.end(), [](const HistoryDrawing& d) { return d.latest.autosave; });
            if (unsaved) Recover(true);
        }
    }
    SetTimer(m_hwnd, kAutosaveTimer, 30 * 1000, nullptr);
    AfterInput();
    FocusCommandLine();
    return true;
}

void MainWindow::CreateChildren() {
    m_menuBar.Create(m_hwnd, m_hwnd, kMenuBarDip);
    m_menuBar.AddTextButton(ID_MENU_FILE, L"File", nullptr, 44);
    m_menuBar.AddTextButton(ID_MENU_EDIT, L"Edit", nullptr, 44);
    m_menuBar.AddTextButton(ID_MENU_VIEW, L"View", nullptr, 48);
    m_menuBar.AddTextButton(ID_MENU_DRAW, L"Draw", nullptr, 50);
    m_menuBar.AddTextButton(ID_MENU_MODIFY, L"Modify", nullptr, 60);
    m_menuBar.AddTextButton(ID_MENU_TOOLS, L"Tools", nullptr, 50);
    m_menuBar.AddTextButton(ID_MENU_HELP, L"Help", nullptr, 48);
    m_menuBar.AddSpacer();
    m_menuBar.AddTextButton(ID_COMMAND_PALETTE, L"Search commands   Ctrl+K",
                            L"Find any command by typing what you want to do", 190);

    m_tools.Create(m_hwnd, m_hwnd, kToolBarDip);
    m_tools.AddButton(ID_NEW, kGlyphNew, L"New drawing (Ctrl+N)");
    m_tools.AddButton(ID_OPEN, kGlyphOpen, L"Open (Ctrl+O)");
    m_tools.AddButton(ID_SAVE, kGlyphSave, L"Save (Ctrl+S)");
    m_tools.AddSeparator();
    m_tools.AddButton(ID_UNDO, kGlyphUndo, L"Undo (Ctrl+Z)");
    m_tools.AddButton(ID_REDO, kGlyphRedo, L"Redo (Ctrl+Y)");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_LINE, kIconLine, L"Line (L)");
    m_tools.AddIconButton(ID_PLINE, kIconPolyline, L"Polyline (PL)");
    m_tools.AddIconButton(ID_CIRCLE, kIconCircle, L"Circle (C)");
    m_tools.AddIconButton(ID_ARC, kIconArc, L"Arc (A)");
    m_tools.AddIconButton(ID_RECTANGLE, kIconRectangle, L"Rectangle (REC)");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_MOVE, kIconMove, L"Move (M)");
    m_tools.AddIconButton(ID_COPY, kIconCopy, L"Copy (CO)");
    m_tools.AddIconButton(ID_ROTATE, kIconRotate, L"Rotate (RO)");
    m_tools.AddIconButton(ID_ERASE, kIconErase, L"Erase (E or Delete)");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_ZOOM_EXTENTS, kIconZoomExtents, L"Zoom to the drawing (double-click the wheel)");
    m_tools.AddIconButton(ID_ZOOM_WINDOW, kIconZoomWindow, L"Zoom to a window");
    m_tools.AddIconButton(ID_DIST, kIconDistance, L"Measure a distance (DI)");

    m_canvas.Create(m_hwnd, &m_engine, &m_doc);
    m_cmdline.Create(m_hwnd);
    m_status.Create(m_hwnd);
    Layout();
}

void MainWindow::CreateAccelerators() {
    std::vector<ACCEL> a = {
        {FVIRTKEY | FCONTROL, 'N', ID_NEW},
        {FVIRTKEY | FCONTROL, 'O', ID_OPEN},
        {FVIRTKEY | FCONTROL, 'S', ID_SAVE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', ID_SAVE_AS},
        {FVIRTKEY | FCONTROL, 'Z', ID_UNDO},
        {FVIRTKEY | FCONTROL, 'Y', ID_REDO},
        {FVIRTKEY | FCONTROL | FSHIFT, 'Z', ID_REDO},
        {FVIRTKEY | FCONTROL, 'K', ID_COMMAND_PALETTE},
        {FVIRTKEY, VK_F1, ID_COMMAND_LIST},
        {FVIRTKEY, VK_F2, ID_TOGGLE_HISTORY},
        {FVIRTKEY, VK_F3, ID_TOGGLE_OSNAP},
        {FVIRTKEY, VK_F7, ID_TOGGLE_GRID},
        {FVIRTKEY, VK_F8, ID_TOGGLE_ORTHO},
        {FVIRTKEY, VK_F9, ID_TOGGLE_SNAP},
        {FVIRTKEY | FALT, 'F', ID_MENU_FILE},
        {FVIRTKEY | FALT, 'E', ID_MENU_EDIT},
        {FVIRTKEY | FALT, 'V', ID_MENU_VIEW},
        {FVIRTKEY | FALT, 'D', ID_MENU_DRAW},
        {FVIRTKEY | FALT, 'M', ID_MENU_MODIFY},
        {FVIRTKEY | FALT, 'T', ID_MENU_TOOLS},
        {FVIRTKEY | FALT, 'H', ID_MENU_HELP},
    };
    m_accel = CreateAcceleratorTableW(a.data(), (int)a.size());
}

void MainWindow::Layout() {
    if (!m_hwnd || !m_canvas.Hwnd()) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int menuH = m_menuBar.Height(), toolH = m_tools.Height();
    const int statusH = m_status.Height(), cmdH = m_cmdline.Height();
    HDWP dwp = BeginDeferWindowPos(5);
    dwp = DeferWindowPos(dwp, m_menuBar.Hwnd(), nullptr, 0, 0, rc.right, menuH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_tools.Hwnd(), nullptr, 0, menuH, rc.right, toolH, SWP_NOZORDER);
    const int canvasTop = menuH + toolH;
    const int canvasH = std::max(10, (int)rc.bottom - canvasTop - cmdH - statusH);
    dwp = DeferWindowPos(dwp, m_canvas.Hwnd(), nullptr, 0, canvasTop, rc.right, canvasH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_cmdline.Hwnd(), nullptr, 0, canvasTop + canvasH, rc.right, cmdH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_status.Hwnd(), nullptr, 0, rc.bottom - statusH, rc.right, statusH, SWP_NOZORDER);
    EndDeferWindowPos(dwp);
}

// ---------------------------------------------------------------------------
// EngineHost
// ---------------------------------------------------------------------------

void MainWindow::Message(const std::wstring& line) { m_cmdline.AddLine(line); }
Vec2 MainWindow::TrackedCursor() { return m_canvas.TrackedCursor(); }
void MainWindow::ZoomExtents() { m_canvas.ZoomExtents(); }
void MainWindow::ZoomWindow(const Box& box) { m_canvas.ZoomWindow(box); }
void MainWindow::ZoomPrevious() { m_canvas.ZoomPrevious(); }
void MainWindow::ZoomFactor(double factor) { m_canvas.ZoomFactor(factor); }

void MainWindow::DraftingChanged() {
    UpdateStatus();
    m_canvas.Refresh();
}

bool MainWindow::HostCommand(const std::wstring& name) {
    if (name == L"NEW") NewDrawing();
    else if (name == L"OPEN") OpenDialog();
    else if (name == L"SAVE") Save(false);
    else if (name == L"SAVEAS") Save(true);
    else if (name == L"DXFOUT") ExportDxf();
    else if (name == L"HISTORY") ShowHistory();
    else if (name == L"RECOVER") Recover(false);
    else if (name == L"COMMANDS") ShowPalette();
    else if (name == L"ABOUT") AboutDialog(m_hwnd);
    else if (name == L"EXIT") PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    else if (name == L"OSNAP") OnCommand(ID_DRAFTING_SETTINGS);
    else if (name == L"UNITS") {
        DrawingSettings s = m_doc.Settings();
        bool forNew = s.units == (Units)m_settings.newUnits && s.precision == m_settings.newPrecision;
        if (UnitsDialog(m_hwnd, s, forNew)) {
            m_doc.SetSettings(s);
            if (forNew) {
                m_settings.newUnits = (int)s.units;
                m_settings.newPrecision = s.precision;
            }
            Message(std::wstring(L"Units: ") + UnitsName(s.units) + L", " + std::to_wstring(s.precision) +
                    L" decimal places.");
        }
    } else return false;
    AfterInput();
    return true;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void MainWindow::Run(const std::wstring& name, const wchar_t* keyword) {
    m_engine.Run(name);
    if (keyword && m_engine.Active() && m_engine.ActiveName() == name) m_engine.Keyword(keyword);
    AfterInput();
    FocusCommandLine();
}

void MainWindow::FocusCommandLine() {
    // Not while the command search is open: it would close it.
    if (!m_palette.IsOpen()) m_cmdline.Focus();
}

void MainWindow::AfterInput() {
    m_canvas.Refresh();
    UpdatePrompt();
    UpdateStatus();
    UpdateTitle();
    m_tools.SetEnabled(ID_UNDO, m_doc.CanUndo());
    m_tools.SetEnabled(ID_REDO, m_doc.CanRedo());
    const std::wstring cmd = m_engine.ActiveName();
    m_tools.SetChecked(ID_LINE, cmd == L"LINE");
    m_tools.SetChecked(ID_PLINE, cmd == L"PLINE");
    m_tools.SetChecked(ID_CIRCLE, cmd == L"CIRCLE");
    m_tools.SetChecked(ID_ARC, cmd == L"ARC");
    m_tools.SetChecked(ID_RECTANGLE, cmd == L"RECTANG");
    m_tools.SetChecked(ID_MOVE, cmd == L"MOVE");
    m_tools.SetChecked(ID_COPY, cmd == L"COPY");
    m_tools.SetChecked(ID_ROTATE, cmd == L"ROTATE");
    m_tools.SetChecked(ID_ZOOM_WINDOW, cmd == L"ZOOM");
    m_tools.SetChecked(ID_DIST, cmd == L"DIST");
}

void MainWindow::UpdatePrompt() {
    if (m_engine.Active()) m_cmdline.SetPrompt(m_engine.CurrentPrompt().FullText(), false);
    else m_cmdline.SetPrompt(L"Command:", true);
}

void MainWindow::UpdateStatus() {
    Vec2 p;
    const bool inside = m_canvas.CursorPoint(p);
    m_status.SetCoordinates(inside ? m_engine.Len(p.x) + L",  " + m_engine.Len(p.y) : L"");
    std::wstring info;
    const size_t n = m_engine.Selection().size();
    if (n) info = std::to_wstring(n) + (n == 1 ? L" object selected" : L" objects selected");
    if (!m_engine.Active() && n) info += L"  \x2022  Delete erases, M moves, CO copies, RO rotates, Esc deselects";
    if (m_engine.Active()) info = std::wstring(L"Running ") + m_engine.ActiveName() + (n ? L"  \x2022  " + info : L"");
    m_status.SetInfo(info);
    const Drafting& d = m_engine.Draft();
    m_status.SetToggles(d.gridOn, d.snapOn, d.orthoOn, d.osnapOn);
    m_status.SetUnits(UnitsSymbol(m_doc.Settings().units));
}

std::wstring MainWindow::DrawingName() const {
    return m_path.empty() ? m_untitledName : StripExtension(FileNameFromPath(m_path));
}

void MainWindow::UpdateTitle() {
    const std::wstring title = DrawingName() + (m_doc.Modified() ? L"*" : L"") + L" - " + APP_NAME;
    if (title != m_shownTitle) {
        m_shownTitle = title;
        SetWindowTextW(m_hwnd, title.c_str());
    }
}

void MainWindow::ToggleDrafting(int id) {
    Drafting& d = m_engine.Draft();
    const wchar_t* name = L"";
    bool on = false;
    switch (id) {
        case ID_TOGGLE_GRID: on = d.gridOn = !d.gridOn; name = L"Grid"; break;
        case ID_TOGGLE_SNAP: on = d.snapOn = !d.snapOn; name = L"Snap"; break;
        case ID_TOGGLE_ORTHO: on = d.orthoOn = !d.orthoOn; name = L"Ortho"; break;
        case ID_TOGGLE_OSNAP: on = d.osnapOn = !d.osnapOn; name = L"Object snaps"; break;
    }
    Message(std::wstring(L"<") + name + (on ? L" on>" : L" off>"));
    DraftingChanged();
}

void MainWindow::OnCommand(int id) {
    for (const IdCommand& c : kIdCommands) {
        if (c.id == id) {
            Run(c.name, c.keyword);
            return;
        }
    }
    if (id >= ID_MENU_FILE && id <= ID_MENU_HELP) {
        ShowMenu(id);
        return;
    }
    if (id >= ID_RECENT_FIRST && id < ID_RECENT_FIRST + 10) {
        const size_t i = (size_t)(id - ID_RECENT_FIRST);
        if (i < m_settings.recent.size()) {
            const std::wstring path = m_settings.recent[i];
            m_engine.Cancel();
            if (ConfirmDiscard()) OpenFile(path);
        }
        AfterInput();
        return;
    }
    if (id >= ID_CTX_KEYWORD && id < ID_CTX_KEYWORD + 30) {
        const auto& kw = m_engine.CurrentPrompt().keywords;
        const size_t i = (size_t)(id - ID_CTX_KEYWORD);
        if (i < kw.size()) m_engine.Keyword(kw[i].key);
        AfterInput();
        return;
    }
    if (id >= ID_CTX_RECENT_CMD && id < ID_CTX_RECENT_CMD + 10) {
        const size_t i = (size_t)(id - ID_CTX_RECENT_CMD);
        if (i < m_engine.RecentCommands().size()) Run(m_engine.RecentCommands()[i]);
        return;
    }
    if (id >= ID_OSNAP_FIRST && id < ID_OSNAP_FIRST + kSnapModeCount) {
        const unsigned mode = kSnapBits[id - ID_OSNAP_FIRST];
        m_engine.SetSnapOverride(mode);
        Message(std::wstring(SnapName(mode)) + L" snap for the next point.");
        AfterInput();
        return;
    }
    if (id >= ID_PALETTE_CMD && id < ID_PALETTE_CMD + (int)AllCommands().size()) {
        Run(AllCommands()[(size_t)(id - ID_PALETTE_CMD)].name);
        return;
    }
    switch (id) {
        case ID_COMMAND_PALETTE:
        case ID_COMMAND_LIST:
            ShowPalette();
            return;
        case ID_ESCAPE:
            m_engine.Cancel();
            AfterInput();
            break;
        case ID_CTX_ENTER:
            m_engine.Enter();
            AfterInput();
            break;
        case ID_CTX_CANCEL:
            m_engine.Cancel();
            AfterInput();
            break;
        case ID_CTX_REPEAT:
            if (!m_engine.LastCommand().empty()) Run(m_engine.LastCommand());
            break;
        case ID_ZOOM_EXTENTS:
            m_canvas.ZoomExtents();
            break;
        case ID_ZOOM_PREVIOUS:
            m_canvas.ZoomPrevious();
            break;
        case ID_ZOOM_IN:
            m_canvas.ZoomFactor(1.5);
            break;
        case ID_ZOOM_OUT:
            m_canvas.ZoomFactor(1 / 1.5);
            break;
        case ID_TOGGLE_GRID:
        case ID_TOGGLE_SNAP:
        case ID_TOGGLE_ORTHO:
        case ID_TOGGLE_OSNAP:
            ToggleDrafting(id);
            break;
        case ID_DRAFTING_SETTINGS: {
            Drafting d = m_engine.Draft();
            if (DraftingDialog(m_hwnd, d)) {
                m_engine.Draft() = d;
                DraftingChanged();
            }
            break;
        }
        case ID_AUTOSAVE_SETTINGS:
            OptionsDialog(m_hwnd, m_settings.autosaveMinutes, m_settings.keepVersions, AppDataDir(L"History"));
            break;
        case ID_THEME_SYSTEM:
            ApplyTheme(0);
            break;
        case ID_THEME_LIGHT:
            ApplyTheme(1);
            break;
        case ID_THEME_DARK:
            ApplyTheme(2);
            break;
        case ID_TOGGLE_HISTORY:  // F2: more or fewer history lines
            m_cmdline.ToggleExpanded();
            Layout();
            break;
    }
    FocusCommandLine();
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

HMENU MainWindow::BuildMenu(int menuId) {
    HMENU m = CreatePopupMenu();
    const Drafting& d = m_engine.Draft();
    switch (menuId) {
        case ID_MENU_FILE: {
            Append(m, ID_NEW, L"&New\tCtrl+N");
            Append(m, ID_OPEN, L"&Open...\tCtrl+O");
            HMENU recent = CreatePopupMenu();
            for (size_t i = 0; i < m_settings.recent.size(); ++i)
                Append(recent, ID_RECENT_FIRST + (int)i, std::to_wstring(i + 1) + L"  " + m_settings.recent[i]);
            if (m_settings.recent.empty()) Append(recent, 0, L"(none yet)", false);
            Submenu(m, recent, L"Open &recent");
            Separator(m);
            Append(m, ID_SAVE, L"&Save\tCtrl+S");
            Append(m, ID_SAVE_AS, L"Save &as...\tCtrl+Shift+S");
            Append(m, ID_EXPORT_DXF, L"&Export DXF (for AutoCAD and others)...");
            Separator(m);
            Append(m, ID_HISTORY, L"&Version history...");
            Append(m, ID_RECOVER, L"Re&cover drawings...");
            Separator(m);
            Append(m, ID_EXIT, L"E&xit");
            break;
        }
        case ID_MENU_EDIT:
            Append(m, ID_UNDO, m_doc.CanUndo() ? L"&Undo " + m_doc.UndoName() + L"\tCtrl+Z" : L"&Undo\tCtrl+Z",
                   m_doc.CanUndo());
            Append(m, ID_REDO, m_doc.CanRedo() ? L"&Redo " + m_doc.RedoName() + L"\tCtrl+Y" : L"&Redo\tCtrl+Y",
                   m_doc.CanRedo());
            Separator(m);
            Append(m, ID_SELECT_ALL, L"Select &all\tCtrl+A");
            Append(m, ID_ERASE, L"&Erase\tDelete");
            Separator(m);
            Append(m, ID_COMMAND_PALETTE, L"&Search commands...\tCtrl+K");
            break;
        case ID_MENU_VIEW: {
            Append(m, ID_ZOOM_EXTENTS, L"Zoom &extents\tZ, E");
            Append(m, ID_ZOOM_WINDOW, L"Zoom &window\tZ, W");
            Append(m, ID_ZOOM_PREVIOUS, L"Zoom &previous\tZ, P");
            Append(m, ID_ZOOM_IN, L"Zoom &in\tWheel");
            Append(m, ID_ZOOM_OUT, L"Zoom &out\tWheel");
            Append(m, ID_PAN, L"P&an\tMiddle drag");
            Separator(m);
            HMENU theme = CreatePopupMenu();
            Append(theme, ID_THEME_SYSTEM, L"Follow &Windows", true, m_settings.themeMode == 0);
            Append(theme, ID_THEME_LIGHT, L"&Light", true, m_settings.themeMode == 1);
            Append(theme, ID_THEME_DARK, L"&Dark", true, m_settings.themeMode == 2);
            Submenu(m, theme, L"&Theme");
            Append(m, ID_TOGGLE_HISTORY, L"Larger command &history\tF2");
            break;
        }
        case ID_MENU_DRAW: {
            Append(m, ID_LINE, L"&Line\tL");
            Append(m, ID_PLINE, L"&Polyline\tPL");
            HMENU circle = CreatePopupMenu();
            Append(circle, ID_CIRCLE, L"&Centre, radius\tC");
            Append(circle, ID_CIRCLE_2P, L"&2 points");
            Append(circle, ID_CIRCLE_3P, L"&3 points");
            Submenu(m, circle, L"&Circle");
            HMENU arc = CreatePopupMenu();
            Append(arc, ID_ARC, L"&3 points\tA");
            Append(arc, ID_ARC_CENTRE, L"&Centre, start, end");
            Submenu(m, arc, L"&Arc");
            Append(m, ID_RECTANGLE, L"&Rectangle\tREC");
            break;
        }
        case ID_MENU_MODIFY:
            Append(m, ID_MOVE, L"&Move\tM");
            Append(m, ID_COPY, L"&Copy\tCO");
            Append(m, ID_ROTATE, L"&Rotate\tRO");
            Append(m, ID_ERASE, L"&Erase\tE");
            break;
        case ID_MENU_TOOLS:
            Append(m, ID_DIST, L"Measure &distance\tDI");
            Append(m, ID_ID_POINT, L"Point &coordinates\tID");
            Separator(m);
            Append(m, ID_TOGGLE_GRID, L"&Grid\tF7", true, d.gridOn);
            Append(m, ID_TOGGLE_SNAP, L"&Snap\tF9", true, d.snapOn);
            Append(m, ID_TOGGLE_ORTHO, L"&Ortho\tF8", true, d.orthoOn);
            Append(m, ID_TOGGLE_OSNAP, L"Object s&naps\tF3", true, d.osnapOn);
            Separator(m);
            Append(m, ID_DRAFTING_SETTINGS, L"&Drafting settings (grid, snaps)...");
            Append(m, ID_UNITS, L"&Units...");
            Append(m, ID_AUTOSAVE_SETTINGS, L"&Auto-save and versions...");
            break;
        case ID_MENU_HELP:
            Append(m, ID_COMMAND_LIST, L"&Commands...\tF1");
            Append(m, ID_TIPS, L"&Mouse and keyboard tips");
            Separator(m);
            Append(m, ID_ABOUT, L"&About Feather CAD");
            break;
    }
    return m;
}

void MainWindow::ShowMenu(int menuId) {
    m_engine.Cancel();  // menus start commands of their own
    AfterInput();
    HMENU m = BuildMenu(menuId);
    const RECT r = m_menuBar.ItemScreenRect(menuId);
    m_menuBar.SetChecked(menuId, true);
    const int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, r.left, r.bottom,
                                       0, m_hwnd, nullptr);
    m_menuBar.SetChecked(menuId, false);
    DestroyMenu(m);
    if (id == ID_TIPS) ShowTips();
    else if (id) OnCommand(id);
}

void MainWindow::ShowContextMenu(int x, int y) {
    HMENU m = CreatePopupMenu();
    if (m_engine.Active()) {
        Append(m, ID_CTX_ENTER, L"&Enter");
        Append(m, ID_CTX_CANCEL, L"&Cancel\tEsc");
        const Prompt& p = m_engine.CurrentPrompt();
        if (!p.keywords.empty()) Separator(m);
        for (size_t i = 0; i < p.keywords.size() && i < 30; ++i)
            Append(m, ID_CTX_KEYWORD + (int)i, p.keywords[i].name);
        if (m_engine.WantsPoint()) {
            HMENU snaps = CreatePopupMenu();
            for (int i = 0; i < kSnapModeCount; ++i) Append(snaps, ID_OSNAP_FIRST + i, SnapName(kSnapBits[i]));
            Separator(m);
            Submenu(m, snaps, L"&Snap override");
        }
    } else {
        if (!m_engine.LastCommand().empty()) Append(m, ID_CTX_REPEAT, L"&Repeat " + m_engine.LastCommand());
        if (!m_engine.RecentCommands().empty()) {
            HMENU recent = CreatePopupMenu();
            for (size_t i = 0; i < m_engine.RecentCommands().size(); ++i)
                Append(recent, ID_CTX_RECENT_CMD + (int)i, m_engine.RecentCommands()[i]);
            Submenu(m, recent, L"Recent &commands");
        }
        if (!m_engine.Selection().empty()) {
            Separator(m);
            Append(m, ID_MOVE, L"&Move");
            Append(m, ID_COPY, L"C&opy");
            Append(m, ID_ROTATE, L"R&otate");
            Append(m, ID_ERASE, L"&Erase\tDelete");
            Append(m, ID_ESCAPE, L"&Deselect all\tEsc");
        }
        Separator(m);
        Append(m, ID_UNDO, L"&Undo\tCtrl+Z", m_doc.CanUndo());
        Append(m, ID_REDO, L"Re&do\tCtrl+Y", m_doc.CanRedo());
        Append(m, ID_SELECT_ALL, L"Select &all\tCtrl+A");
    }
    Separator(m);
    Append(m, ID_ZOOM_EXTENTS, L"Zoom e&xtents");
    Append(m, ID_ZOOM_PREVIOUS, L"Zoom &previous");
    const int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, x, y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    if (id) OnCommand(id);
}

void MainWindow::ShowPalette() {
    std::vector<PaletteCommand> list;
    const auto& all = AllCommands();
    for (size_t i = 0; i < all.size(); ++i) {
        std::wstring keys = all[i].name;
        for (const std::wstring& a : all[i].aliases) keys += L", " + a;
        list.push_back({ID_PALETTE_CMD + (int)i, all[i].title, keys, all[i].description + L" " + all[i].name});
    }
    list.push_back({ID_ZOOM_EXTENTS, L"Zoom extents", L"Z, E", L"view fit whole drawing"});
    list.push_back({ID_ZOOM_PREVIOUS, L"Zoom previous", L"Z, P", L"view back"});
    list.push_back({ID_TOGGLE_GRID, L"Grid on or off", L"F7", L"show hide"});
    list.push_back({ID_TOGGLE_SNAP, L"Snap on or off", L"F9", L"grid snap"});
    list.push_back({ID_TOGGLE_ORTHO, L"Ortho on or off", L"F8", L"horizontal vertical"});
    list.push_back({ID_TOGGLE_OSNAP, L"Object snaps on or off", L"F3", L"osnap endpoint midpoint"});
    list.push_back({ID_DRAFTING_SETTINGS, L"Drafting settings", L"", L"grid snap spacing object snaps"});
    list.push_back({ID_AUTOSAVE_SETTINGS, L"Auto-save and versions", L"", L"options backup history"});
    list.push_back({ID_THEME_DARK, L"Dark theme", L"", L"colours night black"});
    list.push_back({ID_THEME_LIGHT, L"Light theme", L"", L"colours day white"});
    list.push_back({ID_THEME_SYSTEM, L"Follow the Windows theme", L"", L"colours system"});
    list.push_back({ID_CIRCLE_2P, L"Circle from 2 points", L"", L"diameter"});
    list.push_back({ID_CIRCLE_3P, L"Circle through 3 points", L"", L""});
    list.push_back({ID_ARC_CENTRE, L"Arc from centre, start and end", L"", L""});
    m_engine.Cancel();
    AfterInput();
    m_palette.Show(m_hwnd, std::move(list), [this](int id, const std::wstring&) { OnCommand(id); });
}

void MainWindow::ShowTips() {
    MessageBoxW(m_hwnd,
                L"Mouse\n"
                L"  \x2022 Wheel: zoom at the cursor. Middle-button drag: pan. Middle double-click: whole drawing.\n"
                L"  \x2022 Click objects to select them. Drag (or click twice) for a window: left to right "
                L"selects what is inside, right to left what it crosses. Shift+click removes.\n"
                L"  \x2022 Right-click: Enter, options and recent commands.\n\n"
                L"Keyboard\n"
                L"  \x2022 Type a command or its short name and press Enter or Space: L line, PL polyline, "
                L"C circle, A arc, REC rectangle, M move, CO copy, RO rotate, E erase, DI distance, Z zoom.\n"
                L"  \x2022 Enter on its own repeats the last command. Esc cancels. Up and Down recall what you typed.\n"
                L"  \x2022 F3 object snaps, F7 grid, F8 ortho (or hold Shift), F9 snap, Ctrl+K search.\n\n"
                L"Typing points\n"
                L"  \x2022 10,20  absolute.   @10,0  relative to the last point.   @50<30  distance 50 at 30\x00B0.\n"
                L"  \x2022 A single number is a distance in the direction of the cursor.\n"
                L"  \x2022 END, MID, CEN, QUA, INT, PER, TAN, NEA: one object snap for the next point.",
                L"Mouse and keyboard tips", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool MainWindow::ConfirmDiscard() {
    if (!m_doc.Modified()) return true;
    const std::wstring q = L"Save changes to \x201C" + DrawingName() + L"\x201D?";
    const int r = MessageBoxW(m_hwnd, q.c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return Save(false);
    return true;
}

void MainWindow::NewDrawing() {
    if (!ConfirmDiscard()) return;
    m_doc.Clear();
    DrawingSettings ds;
    ds.units = (Units)m_settings.newUnits;
    ds.precision = m_settings.newPrecision;
    m_doc.SetSettings(ds);
    m_doc.MarkSaved();
    m_engine.ClearSelection();
    m_path.clear();
    m_suggestedPath.clear();
    m_untitledName = L"Drawing" + std::to_wstring(++m_untitledCount);
    m_history.SetDrawing(L"", m_untitledName);
    m_autosavedRevision = m_doc.Revision();
    m_lastAutosave = GetTickCount64();
    m_canvas.ZoomExtents();
    AfterInput();
}

void MainWindow::OpenDialog() {
    if (!ConfirmDiscard()) return;
    wchar_t file[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Feather CAD drawings (*.fcad)\0*.fcad\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) OpenFile(file);
}

bool MainWindow::OpenFile(const std::wstring& pathIn) {
    const std::wstring path = FullPath(pathIn);
    std::string text;
    if (!ReadWholeFile(path, text)) {
        Message(L"Could not open " + path + L": " + LastErrorText(GetLastError()));
        MessageBoxW(m_hwnd, (L"Could not open\n" + path).c_str(), APP_NAME, MB_ICONERROR);
        return false;
    }
    std::wstring error;
    if (!m_doc.FromText(text, error)) {
        Message(L"Could not read " + FileNameFromPath(path) + L": " + error);
        MessageBoxW(m_hwnd, (L"Could not read \x201C" + FileNameFromPath(path) + L"\x201D.\n\n" + error).c_str(),
                    APP_NAME, MB_ICONERROR);
        return false;
    }
    m_engine.ClearSelection();
    m_path = path;
    m_suggestedPath.clear();
    m_settings.AddRecent(path);
    m_history.SetDrawing(path, FileNameFromPath(path));
    m_autosavedRevision = m_doc.Revision();
    m_lastAutosave = GetTickCount64();
    m_canvas.ZoomExtents();
    Message(L"Opened " + FileNameFromPath(path) + L" (" + std::to_wstring(m_doc.Count()) + L" objects).");
    AfterInput();
    return true;
}

bool MainWindow::Save(bool saveAs) {
    m_engine.Cancel();
    if (!saveAs && !m_path.empty()) return SaveTo(m_path);
    wchar_t file[MAX_PATH * 4] = L"";
    const std::wstring initial =
        !m_path.empty() ? m_path : !m_suggestedPath.empty() ? m_suggestedPath : m_untitledName + APP_FILE_EXT;
    wcsncpy_s(file, initial.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Feather CAD drawing (*.fcad)\0*.fcad\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.lpstrDefExt = L"fcad";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return false;
    const std::wstring path = FullPath(file);
    const bool newPath = m_path.empty() || !SamePath(path, m_path);
    if (!SaveTo(path)) return false;
    if (newPath) {
        m_history.SetDrawing(path, FileNameFromPath(path));
        m_history.AddVersion(m_doc.ToText(), false, m_settings.keepVersions);
    }
    return true;
}

bool MainWindow::SaveTo(const std::wstring& path) {
    const std::string text = m_doc.ToText();
    std::wstring error;
    if (!WriteFileSafely(path, text, &error)) {
        Message(L"Could not save " + path + L": " + error);
        MessageBoxW(m_hwnd, (L"Could not save\n" + path + L"\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return false;
    }
    const bool samePath = !m_path.empty() && SamePath(path, m_path);
    m_path = path;
    m_suggestedPath.clear();
    m_doc.MarkSaved();
    m_settings.AddRecent(path);
    if (samePath) m_history.AddVersion(text, false, m_settings.keepVersions);
    m_autosavedRevision = m_doc.Revision();
    Message(L"Saved " + path);
    AfterInput();
    return true;
}

void MainWindow::ExportDxf() {
    wchar_t file[MAX_PATH * 4] = L"";
    wcsncpy_s(file, (StripExtension(m_path.empty() ? m_untitledName : m_path) + L".dxf").c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"DXF drawing (*.dxf)\0*.dxf\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.lpstrDefExt = L"dxf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return;
    std::wstring error;
    if (!WriteFileSafely(file, m_doc.ToDxf(), &error)) {
        MessageBoxW(m_hwnd, (L"Could not export\n" + std::wstring(file) + L"\n\n" + error).c_str(), APP_NAME,
                    MB_ICONERROR);
        return;
    }
    Message(L"Exported " + std::to_wstring(m_doc.Count()) + L" objects to " + file +
            L" (DXF R12: opens in AutoCAD, LibreCAD, DraftSight and others).");
}

void MainWindow::ShowHistory() {
    const std::vector<VersionInfo> versions = m_history.Versions();
    const int i = HistoryDialog(m_hwnd, DrawingName(), versions);
    if (i < 0 || i >= (int)versions.size()) return;
    std::string text;
    Document copy;
    std::wstring error;
    if (!ReadWholeFile(versions[(size_t)i].file, text) || !copy.FromText(text, error)) {
        MessageBoxW(m_hwnd, (L"That version could not be read.\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return;
    }
    std::vector<Entity> ents;
    for (const auto& kv : copy.Entities()) ents.push_back(kv.second);
    m_engine.ClearSelection();
    m_doc.ReplaceAll(std::move(ents), L"Restore version");
    Message(L"Restored the version from " + FormatFileTime(versions[(size_t)i].time) +
            L". Undo (Ctrl+Z) brings back what was there before.");
    m_canvas.ZoomExtents();
    AfterInput();
}

void MainWindow::Recover(bool afterCrash) {
    std::vector<HistoryDrawing> all = History::AllDrawings();
    const int i = RecoverDialog(m_hwnd, all, afterCrash);
    if (i < 0 || i >= (int)all.size()) return;
    if (!ConfirmDiscard()) return;
    const HistoryDrawing& h = all[(size_t)i];
    std::string text;
    std::wstring error;
    if (!ReadWholeFile(h.latest.file, text) || !m_doc.FromText(text, error)) {
        MessageBoxW(m_hwnd, (L"The copy could not be read.\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return;
    }
    m_engine.ClearSelection();
    m_path.clear();
    m_suggestedPath = h.source;
    m_untitledName = L"Recovered " + StripExtension(h.title);
    m_history.SetDrawing(h.source, h.title);
    m_doc.MarkModified();
    m_lastAutosave = GetTickCount64();
    m_canvas.ZoomExtents();
    Message(L"Recovered \x201C" + h.title + L"\x201D from " + FormatFileTime(h.latest.time) + L". Save it to keep it.");
    AfterInput();
}

void MainWindow::Autosave() {
    if (m_settings.autosaveMinutes <= 0 || !m_doc.Modified() || m_doc.Revision() == m_autosavedRevision) return;
    const ULONGLONG now = GetTickCount64();
    if (now - m_lastAutosave < (ULONGLONG)m_settings.autosaveMinutes * 60000) return;
    if (m_history.AddVersion(m_doc.ToText(), true, m_settings.keepVersions)) {
        m_autosavedRevision = m_doc.Revision();
        m_lastAutosave = now;
    }
}

void MainWindow::ApplyTheme(int mode) {
    m_settings.themeMode = mode;
    ReloadTheme((ThemeMode)mode);
    ApplyWindowTheme(m_hwnd);
    m_menuBar.OnThemeChanged();
    m_tools.OnThemeChanged();
    m_cmdline.OnThemeChanged();
    m_status.OnThemeChanged();
    m_canvas.OnThemeChanged();
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = (MainWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize = {Dpi(640, m_dpi), Dpi(420, m_dpi)};
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            OnCommand(LOWORD(wp));
            return 0;
        case WM_APP_COMMAND_TEXT:
            m_engine.Text(*(const std::wstring*)lp);
            AfterInput();
            return 0;
        case WM_APP_COMMAND_KEY:
            if (wp == VK_DELETE && !m_engine.Selection().empty() && !m_engine.Active()) Run(L"ERASE");
            if (wp == 'A' && m_engine.WantsSelection()) {
                m_engine.SelectAll();
                AfterInput();
            }
            return 0;
        case WM_APP_CANVAS_STATE:
            UpdateStatus();
            UpdatePrompt();
            return 0;
        case WM_APP_CANVAS_MENU:
            ShowContextMenu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_PARENTNOTIFY:
            // A click in the drawing keeps typing going to the command line.
            if (LOWORD(wp) == WM_LBUTTONDOWN || LOWORD(wp) == WM_MBUTTONDOWN) {
                FocusCommandLine();
                AfterInput();
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) FocusCommandLine();
            return 0;
        case WM_SETFOCUS:
            FocusCommandLine();
            return 0;
        case WM_TIMER:
            if (wp == kAutosaveTimer) Autosave();
            return 0;
        case WM_SETTINGCHANGE:
            if (m_settings.themeMode == 0 && lp && lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) ApplyTheme(0);
            return 0;
        case WM_DPICHANGED: {
            m_dpi = HIWORD(wp);
            const RECT* r = (const RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            m_menuBar.OnDpiChanged();
            m_tools.OnDpiChanged();
            m_cmdline.OnDpiChanged();
            m_status.OnDpiChanged();
            m_canvas.OnDpiChanged();
            Layout();
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t file[MAX_PATH * 4];
            if (DragQueryFileW(drop, 0, file, (UINT)std::size(file))) {
                m_engine.Cancel();
                if (ConfirmDiscard()) OpenFile(file);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_CREATE:
            DragAcceptFiles(m_hwnd, TRUE);
            return 0;
        case WM_CLOSE:
            m_engine.Cancel();
            if (!ConfirmDiscard()) return 0;
            m_settings.drafting = m_engine.Draft();
            m_settings.placement.length = sizeof(m_settings.placement);
            m_settings.hasPlacement = GetWindowPlacement(m_hwnd, &m_settings.placement) != FALSE;
            m_settings.Save();
            MarkRunning(false);
            DestroyWindow(m_hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(m_hwnd, kAutosaveTimer);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
