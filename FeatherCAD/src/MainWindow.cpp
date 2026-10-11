// MainWindow.cpp - see MainWindow.h.
#include "MainWindow.h"

#include <algorithm>
#include <cwctype>

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include "Dialogs.h"
#include "Dimension.h"
#include "DxfImport.h"
#include "Icons.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr UINT_PTR kAutosaveTimer = 1;
constexpr int kMenuBarDip = 30, kToolBarDip = 40;
constexpr wchar_t kClipFormatName[] = L"FeatherCAD.Objects";

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
    {ID_MIRROR, L"MIRROR", nullptr},
    {ID_SCALE, L"SCALE", nullptr},
    {ID_STRETCH, L"STRETCH", nullptr},
    {ID_TRIM, L"TRIM", nullptr},
    {ID_EXTEND, L"EXTEND", nullptr},
    {ID_OFFSET, L"OFFSET", nullptr},
    {ID_FILLET, L"FILLET", nullptr},
    {ID_CHAMFER, L"CHAMFER", nullptr},
    {ID_LENGTHEN, L"LENGTHEN", nullptr},
    {ID_BREAK, L"BREAK", nullptr},
    {ID_BREAK_AT_POINT, L"BREAKATPOINT", nullptr},
    {ID_JOIN, L"JOIN", nullptr},
    {ID_EXPLODE, L"EXPLODE", nullptr},
    {ID_ARRAY_RECT, L"ARRAYRECT", nullptr},
    {ID_ARRAY_POLAR, L"ARRAYPOLAR", nullptr},
    {ID_ARRAY_PATH, L"ARRAYPATH", nullptr},
    {ID_ARRAY_EDIT, L"ARRAYEDIT", nullptr},
    {ID_QSELECT, L"QSELECT", nullptr},
    {ID_SELECT_SIMILAR, L"SELECTSIMILAR", nullptr},
    {ID_POLYGON, L"POLYGON", nullptr},
    {ID_ELLIPSE, L"ELLIPSE", nullptr},
    {ID_ELLIPSE_CENTRE, L"ELLIPSE", L"C"},
    {ID_ELLIPSE_ARC, L"ELLIPSE", L"A"},
    {ID_SPLINE, L"SPLINE", nullptr},
    {ID_POINT, L"POINT", nullptr},
    {ID_ZOOM_WINDOW, L"ZOOM", L"W"},
    {ID_PAN, L"PAN", nullptr},
    {ID_DIST, L"DIST", nullptr},
    {ID_ID_POINT, L"ID", nullptr},
    {ID_UNITS, L"UNITS", nullptr},
    {ID_OSNAP_SETTINGS, L"OSNAP", nullptr},
    {ID_ABOUT, L"ABOUT", nullptr},
    {ID_IMPORT_DXF, L"IMPORT", nullptr},
    {ID_CLOSE_DRAWING, L"CLOSE", nullptr},
    {ID_DIM_SMART, L"DIM", nullptr},
    {ID_DIM_LINEAR, L"DIMLINEAR", nullptr},
    {ID_DIM_ALIGNED, L"DIMALIGNED", nullptr},
    {ID_DIM_RADIUS, L"DIMRADIUS", nullptr},
    {ID_DIM_DIAMETER, L"DIMDIAMETER", nullptr},
    {ID_DIM_ANGULAR, L"DIMANGULAR", nullptr},
    {ID_DIM_CONTINUE, L"DIMCONTINUE", nullptr},
    {ID_DIM_BASELINE, L"DIMBASELINE", nullptr},
    {ID_DIM_EDIT, L"DIMEDIT", nullptr},
    {ID_DIM_STYLE, L"DIMSTYLE", nullptr},
    {ID_TANLINE, L"TANLINE", nullptr},
    {ID_CIRCLE_TTR, L"CIRCLE", L"T"},
};

const unsigned kSnapBits[] = {kSnapEndpoint, kSnapMidpoint, kSnapCentre, kSnapQuadrant,
                              kSnapIntersection, kSnapPerpendicular, kSnapTangent, kSnapNearest};

void Append(HMENU m, int id, const std::wstring& text, bool enabled = true, bool checked = false) {
    AppendMenuW(m, MF_STRING | (enabled ? 0 : MF_GRAYED) | (checked ? MF_CHECKED : 0), (UINT_PTR)id, text.c_str());
}
void Separator(HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); }
void Submenu(HMENU m, HMENU sub, const wchar_t* text) { AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, text); }
}  // namespace

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() {
    if (m_accel) DestroyAcceleratorTable(m_accel);
}

bool MainWindow::Create(HINSTANCE inst, int showCmd, const std::vector<std::wstring>& files) {
    m_settings.Load();
    ReloadTheme((ThemeMode)m_settings.themeMode);
    m_clipFormat = RegisterClipboardFormatW(kClipFormatName);
    m_drawings.push_back(MakeDrawing());
    m_active = 0;
    Eng().Draft() = m_settings.drafting;

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

    Message(std::wstring(APP_NAME) + L" " + APP_VERSION +
            L". Type a command (L for line, C for circle, DIM for dimensions...) or press Ctrl+K to search. "
            L"F1 lists the commands.");
    const bool crashed = PreviousRunCrashed();
    MarkRunning(true);
    History::CleanOld(30);
    m_canvas.ZoomExtents();
    for (const std::wstring& f : files) OpenFile(f);
    if (files.empty() && crashed) {
        std::vector<HistoryDrawing> all = History::AllDrawings();
        const bool unsaved = std::any_of(all.begin(), all.end(), [](const HistoryDrawing& d) { return d.latest.autosave; });
        if (unsaved) Recover(true);
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
    m_menuBar.AddTextButton(ID_MENU_DIMENSION, L"Dimension", nullptr, 78);
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
    m_tools.AddIconButton(ID_POLYGON, kIconPolygon, L"Polygon (POL)");
    m_tools.AddIconButton(ID_ELLIPSE, kIconEllipse, L"Ellipse (EL)");
    m_tools.AddIconButton(ID_SPLINE, kIconSpline, L"Spline (SPL)");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_MOVE, kIconMove, L"Move (M)");
    m_tools.AddIconButton(ID_COPY, kIconCopy, L"Copy (CO)");
    m_tools.AddIconButton(ID_ROTATE, kIconRotate, L"Rotate (RO)");
    m_tools.AddIconButton(ID_MIRROR, kIconMirror, L"Mirror (MI)");
    m_tools.AddIconButton(ID_SCALE, kIconScale, L"Scale (SC)");
    m_tools.AddIconButton(ID_ERASE, kIconErase, L"Erase (E or Delete)");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_TRIM, kIconTrim, L"Trim (TR) - Shift+click extends");
    m_tools.AddIconButton(ID_EXTEND, kIconExtend, L"Extend (EX) - Shift+click trims");
    m_tools.AddIconButton(ID_OFFSET, kIconOffset, L"Offset (O)");
    m_tools.AddIconButton(ID_FILLET, kIconFillet, L"Fillet (F)");
    m_tools.AddIconButton(ID_ARRAY_RECT, kIconArray, L"Array (AR)");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_DIM_SMART, kIconDimension,
                          L"Smart dimension (DIM): click a line, circle, arc or two lines. Double-click a dimension to edit it");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_ZOOM_EXTENTS, kIconZoomExtents, L"Zoom to the drawing (double-click the wheel)");
    m_tools.AddIconButton(ID_ZOOM_WINDOW, kIconZoomWindow, L"Zoom to a window");
    m_tools.AddIconButton(ID_DIST, kIconDistance, L"Measure a distance (DI)");

    m_tabs.Create(m_hwnd, m_hwnd);
    m_canvas.Create(m_hwnd, &Eng(), &Doc());
    m_cmdline.Create(m_hwnd);
    m_status.Create(m_hwnd);
    m_ready = true;  // children may notify from now on
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
        {FVIRTKEY | FCONTROL, 'C', ID_COPYCLIP},
        {FVIRTKEY | FCONTROL, 'X', ID_CUTCLIP},
        {FVIRTKEY | FCONTROL, 'V', ID_PASTECLIP},
        {FVIRTKEY | FCONTROL, 'W', ID_CLOSE_DRAWING},
        {FVIRTKEY | FCONTROL, VK_F4, ID_CLOSE_DRAWING},
        {FVIRTKEY | FCONTROL, VK_TAB, ID_NEXT_DRAWING},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, ID_PREV_DRAWING},
        {FVIRTKEY | FCONTROL, VK_NEXT, ID_NEXT_DRAWING},
        {FVIRTKEY | FCONTROL, VK_PRIOR, ID_PREV_DRAWING},
        {FVIRTKEY, VK_F1, ID_COMMAND_LIST},
        {FVIRTKEY, VK_F2, ID_TOGGLE_HISTORY},
        {FVIRTKEY, VK_F3, ID_TOGGLE_OSNAP},
        {FVIRTKEY, VK_F7, ID_TOGGLE_GRID},
        {FVIRTKEY, VK_F8, ID_TOGGLE_ORTHO},
        {FVIRTKEY, VK_F9, ID_TOGGLE_SNAP},
        {FVIRTKEY, VK_F10, ID_TOGGLE_POLAR},
        {FVIRTKEY, VK_F11, ID_TOGGLE_OTRACK},
        {FVIRTKEY, VK_F12, ID_TOGGLE_DYN},
        {FVIRTKEY | FALT, 'F', ID_MENU_FILE},
        {FVIRTKEY | FALT, 'E', ID_MENU_EDIT},
        {FVIRTKEY | FALT, 'V', ID_MENU_VIEW},
        {FVIRTKEY | FALT, 'D', ID_MENU_DRAW},
        {FVIRTKEY | FALT, 'M', ID_MENU_MODIFY},
        {FVIRTKEY | FALT, 'N', ID_MENU_DIMENSION},
        {FVIRTKEY | FALT, 'T', ID_MENU_TOOLS},
        {FVIRTKEY | FALT, 'H', ID_MENU_HELP},
    };
    m_accel = CreateAcceleratorTableW(a.data(), (int)a.size());
}

void MainWindow::Layout() {
    if (!m_hwnd || !m_ready) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int menuH = m_menuBar.Height(), toolH = m_tools.Height(), tabH = m_tabs.Height();
    const int statusH = m_status.Height(), cmdH = m_cmdline.Height();
    HDWP dwp = BeginDeferWindowPos(6);
    dwp = DeferWindowPos(dwp, m_menuBar.Hwnd(), nullptr, 0, 0, rc.right, menuH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_tools.Hwnd(), nullptr, 0, menuH, rc.right, toolH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_tabs.Hwnd(), nullptr, 0, menuH + toolH, rc.right, tabH, SWP_NOZORDER);
    const int canvasTop = menuH + toolH + tabH;
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
double MainWindow::PickRadius() { return m_canvas.PickRadius(); }
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
    else if (name == L"QSELECT") QuickSelect();
    else if (name == L"IMPORT") ImportDxf();
    else if (name == L"CLOSE") CloseDrawing(m_active);
    else if (name == L"DIMSTYLE") DimensionStyle();
    else if (name == L"UNITS") {
        DrawingSettings s = Doc().Settings();
        bool forNew = s.units == (Units)m_settings.newUnits && s.precision == m_settings.newPrecision;
        if (UnitsDialog(m_hwnd, s, forNew)) {
            Doc().SetSettings(s);
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
    Eng().Run(name);
    if (keyword && Eng().Active() && Eng().ActiveName() == name) Eng().Keyword(keyword);
    AfterInput();
    FocusCommandLine();
}

void MainWindow::FocusCommandLine() {
    // Not while the command search is open: it would close it.
    if (!m_palette.IsOpen()) m_cmdline.Focus();
}

void MainWindow::AfterInput() {
    if (!m_ready) return;
    m_canvas.Refresh();
    UpdatePrompt();
    UpdateStatus();
    UpdateTitle();
    m_tools.SetEnabled(ID_UNDO, Doc().CanUndo());
    m_tools.SetEnabled(ID_REDO, Doc().CanRedo());
    const std::wstring cmd = Eng().ActiveName();
    m_tools.SetChecked(ID_LINE, cmd == L"LINE");
    m_tools.SetChecked(ID_PLINE, cmd == L"PLINE");
    m_tools.SetChecked(ID_CIRCLE, cmd == L"CIRCLE");
    m_tools.SetChecked(ID_ARC, cmd == L"ARC");
    m_tools.SetChecked(ID_RECTANGLE, cmd == L"RECTANG");
    m_tools.SetChecked(ID_POLYGON, cmd == L"POLYGON");
    m_tools.SetChecked(ID_ELLIPSE, cmd == L"ELLIPSE");
    m_tools.SetChecked(ID_SPLINE, cmd == L"SPLINE");
    m_tools.SetChecked(ID_MOVE, cmd == L"MOVE");
    m_tools.SetChecked(ID_COPY, cmd == L"COPY");
    m_tools.SetChecked(ID_ROTATE, cmd == L"ROTATE");
    m_tools.SetChecked(ID_MIRROR, cmd == L"MIRROR");
    m_tools.SetChecked(ID_SCALE, cmd == L"SCALE");
    m_tools.SetChecked(ID_TRIM, cmd == L"TRIM");
    m_tools.SetChecked(ID_EXTEND, cmd == L"EXTEND");
    m_tools.SetChecked(ID_OFFSET, cmd == L"OFFSET");
    m_tools.SetChecked(ID_FILLET, cmd == L"FILLET");
    m_tools.SetChecked(ID_ARRAY_RECT, cmd.compare(0, 5, L"ARRAY") == 0);
    m_tools.SetChecked(ID_ZOOM_WINDOW, cmd == L"ZOOM");
    m_tools.SetChecked(ID_DIST, cmd == L"DIST");
    m_tools.SetChecked(ID_DIM_SMART, cmd.compare(0, 3, L"DIM") == 0);
    UpdateTabs();
}

void MainWindow::UpdateTabs() {
    if (!m_ready) return;
    std::vector<TabBar::Tab> tabs;
    for (const auto& d : m_drawings) {
        TabBar::Tab t;
        t.title = DrawingName(*d);
        t.tip = !d->path.empty() ? d->path : !d->suggestedPath.empty() ? d->suggestedPath + L"  (not saved yet)" : t.title + L"  (not saved yet)";
        t.modified = d->doc.Modified();
        tabs.push_back(std::move(t));
    }
    m_tabs.SetTabs(tabs, (int)m_active);
}

void MainWindow::UpdatePrompt() {
    if (!m_ready) return;
    if (Eng().Active()) m_cmdline.SetPrompt(Eng().CurrentPrompt().FullText(), false);
    else m_cmdline.SetPrompt(L"Command:", true);
}

void MainWindow::UpdateStatus() {
    if (!m_ready) return;
    Vec2 p;
    const bool inside = m_canvas.CursorPoint(p);
    m_status.SetCoordinates(inside ? Eng().Len(p.x) + L",  " + Eng().Len(p.y) : L"");
    std::wstring info;
    const size_t n = Eng().Selection().size();
    if (n) info = std::to_wstring(n) + (n == 1 ? L" object selected" : L" objects selected");
    if (!Eng().Active() && n) info += L"  \x2022  Delete erases, M moves, CO copies, RO rotates, Esc deselects";
    if (Eng().Active()) info = std::wstring(L"Running ") + Eng().ActiveName() + (n ? L"  \x2022  " + info : L"");
    m_status.SetInfo(info);
    const Drafting& d = Eng().Draft();
    // The E, M, I and C snaps light up while object snaps are on.
    auto mode = [&](unsigned bit) { return d.osnapOn && (d.osnapModes & bit) != 0; };
    m_status.SetToggles({d.gridOn, d.snapOn, d.orthoOn, d.polarOn, d.osnapOn, mode(kSnapEndpoint), mode(kSnapMidpoint),
                         mode(kSnapIntersection), mode(kSnapCentre), d.otrackOn, d.dynOn});
    m_status.SetUnits(UnitsSymbol(Doc().Settings().units));
}

std::wstring MainWindow::DrawingName(const Drawing& d) const {
    return d.path.empty() ? d.untitledName : StripExtension(FileNameFromPath(d.path));
}

void MainWindow::UpdateTitle() {
    const std::wstring title = DrawingName() + (Doc().Modified() ? L"*" : L"") + L" - " + APP_NAME;
    if (title != m_shownTitle) {
        m_shownTitle = title;
        SetWindowTextW(m_hwnd, title.c_str());
    }
}

void MainWindow::ToggleDrafting(int id) {
    Drafting& d = Eng().Draft();
    const wchar_t* name = L"";
    bool on = false;
    switch (id) {
        case ID_TOGGLE_GRID: on = d.gridOn = !d.gridOn; name = L"Grid"; break;
        case ID_TOGGLE_SNAP: on = d.snapOn = !d.snapOn; name = L"Snap"; break;
        case ID_TOGGLE_ORTHO:
            on = d.orthoOn = !d.orthoOn;
            if (on) d.polarOn = false;  // one or the other, as in other CAD programs
            name = L"Ortho";
            break;
        case ID_TOGGLE_POLAR:
            on = d.polarOn = !d.polarOn;
            if (on) d.orthoOn = false;
            name = L"Polar";
            break;
        case ID_TOGGLE_OSNAP: on = d.osnapOn = !d.osnapOn; name = L"Object snaps"; break;
        case ID_TOGGLE_OTRACK: on = d.otrackOn = !d.otrackOn; name = L"Object snap tracking"; break;
        case ID_TOGGLE_DYN: on = d.dynOn = !d.dynOn; name = L"Dynamic input"; break;
    }
    Message(std::wstring(L"<") + name + (on ? L" on>" : L" off>"));
    DraftingChanged();
}

void MainWindow::QuickSelect() {
    static SelectQuery query;
    static QSelectMode mode = QSelectMode::Replace;
    if (!QSelectDialog(m_hwnd, query, mode)) return;
    std::vector<uint64_t> ids;
    for (const auto& kv : Doc().Entities())
        if (Matches(kv.second, query)) ids.push_back(kv.first);
    if (mode == QSelectMode::Replace) Eng().ClearSelection();
    if (mode == QSelectMode::Remove) Eng().RemoveFromSelection(ids);
    else Eng().AddToSelection(ids);
    Message(std::to_wstring(ids.size()) + L" object(s) matched; " + std::to_wstring(Eng().Selection().size()) +
            L" selected.");
}

void MainWindow::OnCommand(int id) {
    if (id == ID_DIM_EDIT && Eng().Selection().size() == 1 && !Eng().Active()) {
        // One dimension selected: its dialog.
        const Entity* e = Doc().Find(*Eng().Selection().begin());
        if (e && e->type == EntityType::Dimension) {
            EditDimension(e->id);
            return;
        }
    }
    for (const IdCommand& c : kIdCommands) {
        if (c.id == id) {
            Run(c.name, c.keyword);
            return;
        }
    }
    if (id >= ID_MENU_FILE && id <= ID_MENU_HELP) {  // includes ID_MENU_DIMENSION
        ShowMenu(id);
        return;
    }
    if (id >= ID_RECENT_FIRST && id < ID_RECENT_FIRST + 10) {
        const size_t i = (size_t)(id - ID_RECENT_FIRST);
        if (i < m_settings.recent.size()) {
            const std::wstring path = m_settings.recent[i];
            Eng().Cancel();
            OpenFile(path);
        }
        AfterInput();
        FocusCommandLine();
        return;
    }
    if (id >= ID_TAB_FIRST && id < ID_TAB_FIRST + kMaxTabs) {
        if ((size_t)(id - ID_TAB_FIRST) < m_drawings.size()) SwitchTo((size_t)(id - ID_TAB_FIRST));
        FocusCommandLine();
        return;
    }
    if (id >= ID_TAB_CLOSE_FIRST && id < ID_TAB_CLOSE_FIRST + kMaxTabs) {
        if ((size_t)(id - ID_TAB_CLOSE_FIRST) < m_drawings.size()) CloseDrawing((size_t)(id - ID_TAB_CLOSE_FIRST));
        FocusCommandLine();
        return;
    }
    if (id >= ID_CTX_KEYWORD && id < ID_CTX_KEYWORD + 30) {
        const auto& kw = Eng().CurrentPrompt().keywords;
        const size_t i = (size_t)(id - ID_CTX_KEYWORD);
        if (i < kw.size()) Eng().Keyword(kw[i].key);
        AfterInput();
        return;
    }
    if (id >= ID_CTX_RECENT_CMD && id < ID_CTX_RECENT_CMD + 10) {
        const size_t i = (size_t)(id - ID_CTX_RECENT_CMD);
        if (i < Eng().RecentCommands().size()) Run(Eng().RecentCommands()[i]);
        return;
    }
    if (id >= ID_SNAPMODE_FIRST && id < ID_SNAPMODE_FIRST + kSnapModeCount) {
        // A status bar snap letter: that snap on or off (and object snaps on).
        Drafting& d = Eng().Draft();
        const unsigned bit = kSnapBits[id - ID_SNAPMODE_FIRST];
        const bool on = !(d.osnapOn && (d.osnapModes & bit));
        if (on) {
            d.osnapModes |= bit;
            d.osnapOn = true;
        } else {
            d.osnapModes &= ~bit;
        }
        Message(std::wstring(L"<") + SnapName(bit) + (on ? L" snap on>" : L" snap off>"));
        DraftingChanged();
        FocusCommandLine();
        return;
    }
    if (id >= ID_OSNAP_FIRST && id < ID_OSNAP_FIRST + kSnapModeCount) {
        const unsigned mode = kSnapBits[id - ID_OSNAP_FIRST];
        Eng().SetSnapOverride(mode);
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
        case ID_COPYCLIP:
        case ID_CUTCLIP:
        case ID_PASTECLIP:
            ClipboardCommand(id);
            return;
        case ID_NEXT_DRAWING:
        case ID_PREV_DRAWING:
            if (m_drawings.size() > 1) {
                const size_t n = m_drawings.size();
                SwitchTo(id == ID_NEXT_DRAWING ? (m_active + 1) % n : (m_active + n - 1) % n);
            }
            break;
        case ID_ESCAPE:
            Eng().Cancel();
            AfterInput();
            break;
        case ID_CTX_ENTER:
            Eng().Enter();
            AfterInput();
            break;
        case ID_CTX_CANCEL:
            Eng().Cancel();
            AfterInput();
            break;
        case ID_CTX_REPEAT:
            if (!Eng().LastCommand().empty()) Run(Eng().LastCommand());
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
        case ID_TOGGLE_POLAR:
        case ID_TOGGLE_OTRACK:
        case ID_TOGGLE_DYN:
            ToggleDrafting(id);
            break;
        case ID_DRAFTING_SETTINGS: {
            Drafting d = Eng().Draft();
            if (DraftingDialog(m_hwnd, d)) {
                Eng().Draft() = d;
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
    const Drafting& d = Eng().Draft();
    switch (menuId) {
        case ID_MENU_FILE: {
            Append(m, ID_NEW, L"&New\tCtrl+N");
            Append(m, ID_OPEN, L"&Open (drawing or DXF)...\tCtrl+O");
            HMENU recent = CreatePopupMenu();
            for (size_t i = 0; i < m_settings.recent.size(); ++i)
                Append(recent, ID_RECENT_FIRST + (int)i, std::to_wstring(i + 1) + L"  " + m_settings.recent[i]);
            if (m_settings.recent.empty()) Append(recent, 0, L"(none yet)", false);
            Submenu(m, recent, L"Open &recent");
            Append(m, ID_IMPORT_DXF, L"&Import DXF into this drawing...");
            Append(m, ID_CLOSE_DRAWING, L"&Close drawing\tCtrl+W");
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
            Append(m, ID_UNDO, Doc().CanUndo() ? L"&Undo " + Doc().UndoName() + L"\tCtrl+Z" : L"&Undo\tCtrl+Z",
                   Doc().CanUndo());
            Append(m, ID_REDO, Doc().CanRedo() ? L"&Redo " + Doc().RedoName() + L"\tCtrl+Y" : L"&Redo\tCtrl+Y",
                   Doc().CanRedo());
            Separator(m);
            Append(m, ID_CUTCLIP, L"Cu&t\tCtrl+X", !Eng().Selection().empty());
            Append(m, ID_COPYCLIP, L"&Copy\tCtrl+C", !Eng().Selection().empty());
            Append(m, ID_PASTECLIP, L"&Paste\tCtrl+V", m_clipFormat && IsClipboardFormatAvailable(m_clipFormat));
            Separator(m);
            Append(m, ID_SELECT_ALL, L"Select &all\tCtrl+A");
            Append(m, ID_QSELECT, L"&Quick select...");
            Append(m, ID_SELECT_SIMILAR, L"Select si&milar", !Eng().Selection().empty());
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
            Append(m, ID_NEXT_DRAWING, L"&Next drawing\tCtrl+Tab", m_drawings.size() > 1);
            Append(m, ID_PREV_DRAWING, L"Pre&vious drawing\tCtrl+Shift+Tab", m_drawings.size() > 1);
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
            Append(m, ID_TANLINE, L"&Tangent line (two circles)\tTL");
            HMENU circle = CreatePopupMenu();
            Append(circle, ID_CIRCLE, L"&Centre, radius\tC");
            Append(circle, ID_CIRCLE_2P, L"&2 points");
            Append(circle, ID_CIRCLE_3P, L"&3 points");
            Append(circle, ID_CIRCLE_TTR, L"&Tangent, tangent, radius");
            Submenu(m, circle, L"&Circle");
            HMENU arc = CreatePopupMenu();
            Append(arc, ID_ARC, L"&3 points\tA");
            Append(arc, ID_ARC_CENTRE, L"&Centre, start, end");
            Submenu(m, arc, L"&Arc");
            Append(m, ID_RECTANGLE, L"&Rectangle\tREC");
            Append(m, ID_POLYGON, L"Pol&ygon\tPOL");
            {
                HMENU el = CreatePopupMenu();
                Append(el, ID_ELLIPSE, L"&Axis, end\tEL");
                Append(el, ID_ELLIPSE_CENTRE, L"&Centre");
                Append(el, ID_ELLIPSE_ARC, L"Elliptical &arc");
                Submenu(m, el, L"&Ellipse");
            }
            Append(m, ID_SPLINE, L"&Spline\tSPL");
            Append(m, ID_POINT, L"P&oint\tPO");
            break;
        }
        case ID_MENU_MODIFY:
            Append(m, ID_MOVE, L"&Move\tM");
            Append(m, ID_COPY, L"&Copy\tCO");
            Append(m, ID_ROTATE, L"&Rotate\tRO");
            Append(m, ID_MIRROR, L"M&irror\tMI");
            Append(m, ID_SCALE, L"&Scale\tSC");
            Append(m, ID_STRETCH, L"Stretc&h\tS");
            Separator(m);
            Append(m, ID_TRIM, L"&Trim\tTR");
            Append(m, ID_EXTEND, L"E&xtend\tEX");
            Append(m, ID_OFFSET, L"&Offset\tO");
            Append(m, ID_FILLET, L"&Fillet\tF");
            Append(m, ID_CHAMFER, L"Cham&fer\tCHA");
            Append(m, ID_LENGTHEN, L"&Lengthen\tLEN");
            Separator(m);
            Append(m, ID_BREAK, L"&Break\tBR");
            Append(m, ID_BREAK_AT_POINT, L"Break at &point");
            Append(m, ID_JOIN, L"&Join\tJ");
            Append(m, ID_EXPLODE, L"Ex&plode\tX");
            Separator(m);
            {
                HMENU arr = CreatePopupMenu();
                Append(arr, ID_ARRAY_RECT, L"&Rectangular\tAR");
                Append(arr, ID_ARRAY_POLAR, L"&Polar");
                Append(arr, ID_ARRAY_PATH, L"P&ath");
                Separator(arr);
                Append(arr, ID_ARRAY_EDIT, L"&Edit array");
                Submenu(m, arr, L"&Array");
            }
            Separator(m);
            Append(m, ID_ERASE, L"&Erase\tE");
            break;
        case ID_MENU_DIMENSION:
            Append(m, ID_DIM_SMART, L"&Smart dimension\tDIM");
            Separator(m);
            Append(m, ID_DIM_LINEAR, L"&Linear\tDLI");
            Append(m, ID_DIM_ALIGNED, L"&Aligned\tDAL");
            Append(m, ID_DIM_RADIUS, L"&Radius\tDRA");
            Append(m, ID_DIM_DIAMETER, L"&Diameter\tDDI");
            Append(m, ID_DIM_ANGULAR, L"An&gular\tDAN");
            Separator(m);
            Append(m, ID_DIM_CONTINUE, L"&Continue\tDCO");
            Append(m, ID_DIM_BASELINE, L"&Baseline\tDBA");
            Separator(m);
            Append(m, ID_DIM_EDIT, L"&Edit dimension (or double-click it)\tDED");
            Append(m, ID_DIM_STYLE, L"Dimension st&yle...\tD");
            break;
        case ID_MENU_TOOLS:
            Append(m, ID_DIST, L"Measure &distance\tDI");
            Append(m, ID_ID_POINT, L"Point &coordinates\tID");
            Separator(m);
            Append(m, ID_TOGGLE_GRID, L"&Grid\tF7", true, d.gridOn);
            Append(m, ID_TOGGLE_SNAP, L"&Snap\tF9", true, d.snapOn);
            Append(m, ID_TOGGLE_ORTHO, L"&Ortho\tF8", true, d.orthoOn);
            Append(m, ID_TOGGLE_POLAR, L"&Polar tracking\tF10", true, d.polarOn);
            Append(m, ID_TOGGLE_OSNAP, L"Object s&naps\tF3", true, d.osnapOn);
            Append(m, ID_TOGGLE_OTRACK, L"Object snap &tracking\tF11", true, d.otrackOn);
            Append(m, ID_TOGGLE_DYN, L"D&ynamic input\tF12", true, d.dynOn);
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
    Eng().Cancel();  // menus start commands of their own
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
    if (Eng().Active()) {
        Append(m, ID_CTX_ENTER, L"&Enter");
        Append(m, ID_CTX_CANCEL, L"&Cancel\tEsc");
        const Prompt& p = Eng().CurrentPrompt();
        if (!p.keywords.empty()) Separator(m);
        for (size_t i = 0; i < p.keywords.size() && i < 30; ++i)
            Append(m, ID_CTX_KEYWORD + (int)i, p.keywords[i].name);
        if (Eng().WantsPoint()) {
            HMENU snaps = CreatePopupMenu();
            for (int i = 0; i < kSnapModeCount; ++i) Append(snaps, ID_OSNAP_FIRST + i, SnapName(kSnapBits[i]));
            Separator(m);
            Submenu(m, snaps, L"&Snap override");
        }
    } else {
        if (!Eng().LastCommand().empty()) Append(m, ID_CTX_REPEAT, L"&Repeat " + Eng().LastCommand());
        if (!Eng().RecentCommands().empty()) {
            HMENU recent = CreatePopupMenu();
            for (size_t i = 0; i < Eng().RecentCommands().size(); ++i)
                Append(recent, ID_CTX_RECENT_CMD + (int)i, Eng().RecentCommands()[i]);
            Submenu(m, recent, L"Recent &commands");
        }
        if (!Eng().Selection().empty()) {
            Separator(m);
            Append(m, ID_MOVE, L"&Move");
            Append(m, ID_COPY, L"C&opy");
            Append(m, ID_ROTATE, L"R&otate");
            Append(m, ID_MIRROR, L"M&irror");
            Append(m, ID_SCALE, L"&Scale");
            Append(m, ID_EXPLODE, L"Ex&plode");
            Append(m, ID_ERASE, L"&Erase\tDelete");
            Separator(m);
            Append(m, ID_CUTCLIP, L"Cu&t\tCtrl+X");
            Append(m, ID_COPYCLIP, L"Cop&y to clipboard\tCtrl+C");
            if (Eng().Selection().size() == 1) {
                const Entity* e = Doc().Find(*Eng().Selection().begin());
                if (e && e->type == EntityType::Dimension) Append(m, ID_DIM_EDIT, L"Edit &dimension...");
            }
            Append(m, ID_SELECT_SIMILAR, L"Select si&milar");
            Append(m, ID_ESCAPE, L"&Deselect all\tEsc");
        }
        Separator(m);
        Append(m, ID_UNDO, L"&Undo\tCtrl+Z", Doc().CanUndo());
        Append(m, ID_REDO, L"Re&do\tCtrl+Y", Doc().CanRedo());
        Append(m, ID_SELECT_ALL, L"Select &all\tCtrl+A");
        if (m_clipFormat && IsClipboardFormatAvailable(m_clipFormat)) Append(m, ID_PASTECLIP, L"&Paste\tCtrl+V");
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
    list.push_back({ID_TOGGLE_POLAR, L"Polar tracking on or off", L"F10", L"angle tracking"});
    list.push_back({ID_TOGGLE_OTRACK, L"Object snap tracking on or off", L"F11", L"otrack align"});
    list.push_back({ID_TOGGLE_DYN, L"Dynamic input on or off", L"F12", L"cursor tooltip dyn"});
    list.push_back({ID_DRAFTING_SETTINGS, L"Drafting settings", L"", L"grid snap spacing object snaps"});
    list.push_back({ID_AUTOSAVE_SETTINGS, L"Auto-save and versions", L"", L"options backup history"});
    list.push_back({ID_THEME_DARK, L"Dark theme", L"", L"colours night black"});
    list.push_back({ID_THEME_LIGHT, L"Light theme", L"", L"colours day white"});
    list.push_back({ID_THEME_SYSTEM, L"Follow the Windows theme", L"", L"colours system"});
    list.push_back({ID_CIRCLE_2P, L"Circle from 2 points", L"", L"diameter"});
    list.push_back({ID_CIRCLE_3P, L"Circle through 3 points", L"", L""});
    list.push_back({ID_CIRCLE_TTR, L"Circle tangent to two objects with a radius", L"C, T", L"ttr tangent tangent radius"});
    list.push_back({ID_ARC_CENTRE, L"Arc from centre, start and end", L"", L""});
    list.push_back({ID_NEXT_DRAWING, L"Next drawing (tab)", L"Ctrl+Tab", L"switch tab"});
    list.push_back({ID_PREV_DRAWING, L"Previous drawing (tab)", L"Ctrl+Shift+Tab", L"switch tab"});
    Eng().Cancel();
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
                L"  \x2022 END, MID, CEN, QUA, INT, PER, TAN, NEA: one object snap for the next point.\n\n"
                L"Dimensions\n"
                L"  \x2022 DIM: click a line, circle or arc (or two lines for an angle), then where the dimension goes.\n"
                L"  \x2022 While placing it, type a number: the object becomes that size.\n"
                L"  \x2022 Double-click a dimension to change its value (the drawing follows) or its text.\n\n"
                L"Drawings\n"
                L"  \x2022 Each drawing has a tab. Ctrl+Tab switches, Ctrl+W closes. Ctrl+C and Ctrl+V copy between them.",
                L"Mouse and keyboard tips", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

// One drawing per tab.
std::unique_ptr<MainWindow::Drawing> MainWindow::MakeDrawing() {
    auto d = std::make_unique<Drawing>(*this);
    DrawingSettings ds;
    ds.units = (Units)m_settings.newUnits;
    ds.precision = m_settings.newPrecision;
    d->doc.SetSettings(ds);
    d->doc.MarkSaved();
    d->untitledName = L"Drawing" + std::to_wstring(++m_untitledCount);
    d->history.SetDrawing(L"", d->untitledName);
    d->autosavedRevision = d->doc.Revision();
    d->lastAutosave = GetTickCount64();
    if (!m_drawings.empty()) d->engine.Draft() = Eng().Draft();  // drafting settings are the same everywhere
    return d;
}

void MainWindow::AddDrawing(std::unique_ptr<Drawing> d) {
    d->engine.Draft() = Eng().Draft();
    if (Cur().Pristine() && !Eng().Active()) {
        // The empty drawing nobody used yet makes room.
        m_drawings[m_active] = std::move(d);
        m_canvas.SetDrawing(&Eng(), &Doc());
        m_canvas.SetView(Cur().view);
    } else {
        if (m_drawings.size() >= (size_t)kMaxTabs) {
            MessageBoxW(m_hwnd, L"Too many drawings are open. Close some first.", APP_NAME, MB_ICONINFORMATION);
            return;
        }
        m_drawings.push_back(std::move(d));
        SwitchTo(m_drawings.size() - 1);
    }
    AfterInput();
}

void MainWindow::SwitchTo(size_t index) {
    if (index >= m_drawings.size()) return;
    if (index != m_active) {
        Eng().Cancel();  // a command belongs to its drawing
        Cur().view = m_canvas.GetView();
        const Drafting draft = Eng().Draft();
        m_active = index;
        Eng().Draft() = draft;
        m_canvas.SetDrawing(&Eng(), &Doc());
        m_canvas.SetView(Cur().view);
    }
    AfterInput();
    FocusCommandLine();
}

bool MainWindow::CloseDrawing(size_t index) {
    if (index >= m_drawings.size()) return false;
    if (index != m_active) SwitchTo(index);
    Eng().Cancel();
    if (!ConfirmDiscard(index)) return false;
    if (m_drawings.size() == 1) {
        // The last tab: an empty drawing takes its place.
        const Drafting draft = Eng().Draft();
        m_drawings[0] = MakeDrawing();
        m_drawings[0]->engine.Draft() = draft;
        m_canvas.SetDrawing(&Eng(), &Doc());
        m_canvas.SetView(Cur().view);
    } else {
        const Drafting draft = Eng().Draft();
        m_drawings.erase(m_drawings.begin() + (ptrdiff_t)index);
        m_active = std::min(index, m_drawings.size() - 1);
        Eng().Draft() = draft;
        m_canvas.SetDrawing(&Eng(), &Doc());
        m_canvas.SetView(Cur().view);
    }
    AfterInput();
    return true;
}

int MainWindow::FindOpen(const std::wstring& path) const {
    for (size_t i = 0; i < m_drawings.size(); ++i)
        if (!m_drawings[i]->path.empty() && SamePath(m_drawings[i]->path, path)) return (int)i;
    return -1;
}

bool MainWindow::ConfirmDiscard(size_t index) {
    Drawing& d = *m_drawings[index];
    if (!d.doc.Modified()) return true;
    if (index != m_active) SwitchTo(index);
    const std::wstring q = L"Save changes to \x201C" + DrawingName(d) + L"\x201D?";
    const int r = MessageBoxW(m_hwnd, q.c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return Save(false);
    return true;
}

void MainWindow::NewDrawing() {
    Eng().Cancel();
    auto d = MakeDrawing();
    if (m_drawings.size() >= (size_t)kMaxTabs) {
        MessageBoxW(m_hwnd, L"Too many drawings are open. Close some first.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    m_drawings.push_back(std::move(d));
    SwitchTo(m_drawings.size() - 1);
}

void MainWindow::OpenDialog() {
    wchar_t file[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Drawings (*.fcad, *.dxf)\0*.fcad;*.dxf\0Feather CAD drawings (*.fcad)\0*.fcad\0"
                      L"DXF drawings (*.dxf)\0*.dxf\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;
    // Several files: the folder, then each name, separated by zeros.
    std::vector<std::wstring> paths;
    const std::wstring first = file;
    const wchar_t* p = file + first.size() + 1;
    if (*p == 0) {
        paths.push_back(first);
    } else {
        for (; *p; p += wcslen(p) + 1) paths.push_back(first + L"\\" + p);
    }
    for (const std::wstring& path : paths) OpenFile(path);
}

namespace {
bool LooksLikeDxf(const std::wstring& path, const std::string& text) {
    std::wstring ext = path.size() > 4 ? path.substr(path.size() - 4) : L"";
    for (wchar_t& c : ext) c = (wchar_t)std::towlower(c);
    if (ext == L".dxf") return true;
    if (text.compare(0, 10, "FeatherCAD") == 0) return false;
    // "  0\nSECTION" near the start
    const size_t sec = text.find("SECTION");
    return sec != std::string::npos && sec < 64;
}
}  // namespace

bool MainWindow::OpenFile(const std::wstring& pathIn) {
    const std::wstring path = FullPath(pathIn);
    const int open = FindOpen(path);
    if (open >= 0) {  // already open: show it
        SwitchTo((size_t)open);
        Message(FileNameFromPath(path) + L" is already open.");
        return true;
    }
    std::string text;
    if (!ReadWholeFile(path, text)) {
        Message(L"Could not open " + path + L": " + LastErrorText(GetLastError()));
        MessageBoxW(m_hwnd, (L"Could not open\n" + path).c_str(), APP_NAME, MB_ICONERROR);
        return false;
    }
    auto d = MakeDrawing();
    --m_untitledCount;  // it has a name of its own
    std::wstring error, notes;
    const bool dxf = LooksLikeDxf(path, text);
    const bool ok = dxf ? d->doc.FromDxf(text, error, notes) : d->doc.FromText(text, error);
    if (!ok) {
        Message(L"Could not read " + FileNameFromPath(path) + L": " + error);
        MessageBoxW(m_hwnd, (L"Could not read \x201C" + FileNameFromPath(path) + L"\x201D.\n\n" + error).c_str(),
                    APP_NAME, MB_ICONERROR);
        return false;
    }
    if (dxf) {
        // A DXF is brought in as a new drawing: Save asks where to keep it as .fcad.
        d->untitledName = FileNameFromPath(path);
        d->suggestedPath = StripExtension(path) + APP_FILE_EXT;
        d->history.SetDrawing(L"", d->untitledName);
    } else {
        d->path = path;
        d->history.SetDrawing(path, FileNameFromPath(path));
    }
    d->autosavedRevision = d->doc.Revision();
    d->lastAutosave = GetTickCount64();
    m_settings.AddRecent(path);
    const size_t count = d->doc.Count();
    AddDrawing(std::move(d));
    m_canvas.ZoomExtents();
    Message((dxf ? L"Opened the DXF drawing " : L"Opened ") + FileNameFromPath(path) + L" (" + std::to_wstring(count) +
            L" objects)." + (notes.empty() ? L"" : L" " + notes));
    AfterInput();
    return true;
}

bool MainWindow::Save(bool saveAs) {
    Eng().Cancel();
    if (!saveAs && !Cur().path.empty()) return SaveTo(Cur().path);
    wchar_t file[MAX_PATH * 4] = L"";
    const std::wstring initial =
        !Cur().path.empty() ? Cur().path : !Cur().suggestedPath.empty() ? Cur().suggestedPath : Cur().untitledName + APP_FILE_EXT;
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
    const int other = FindOpen(path);
    if (other >= 0 && (size_t)other != m_active) {
        MessageBoxW(m_hwnd, L"That drawing is open in another tab. Close it first, or choose another name.", APP_NAME,
                    MB_ICONWARNING);
        return false;
    }
    const bool newPath = Cur().path.empty() || !SamePath(path, Cur().path);
    if (!SaveTo(path)) return false;
    if (newPath) {
        Cur().history.SetDrawing(path, FileNameFromPath(path));
        Cur().history.AddVersion(Doc().ToText(), false, m_settings.keepVersions);
    }
    return true;
}

bool MainWindow::SaveTo(const std::wstring& path) {
    const std::string text = Doc().ToText();
    std::wstring error;
    if (!WriteFileSafely(path, text, &error)) {
        Message(L"Could not save " + path + L": " + error);
        MessageBoxW(m_hwnd, (L"Could not save\n" + path + L"\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return false;
    }
    const bool samePath = !Cur().path.empty() && SamePath(path, Cur().path);
    Cur().path = path;
    Cur().suggestedPath.clear();
    Doc().MarkSaved();
    m_settings.AddRecent(path);
    if (samePath) Cur().history.AddVersion(text, false, m_settings.keepVersions);
    Cur().autosavedRevision = Doc().Revision();
    Message(L"Saved " + path);
    AfterInput();
    return true;
}

void MainWindow::ExportDxf() {
    wchar_t file[MAX_PATH * 4] = L"";
    const std::wstring base = !Cur().path.empty() ? Cur().path : !Cur().suggestedPath.empty() ? Cur().suggestedPath : Cur().untitledName;
    wcsncpy_s(file, (StripExtension(base) + L".dxf").c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"DXF drawing (*.dxf)\0*.dxf\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.lpstrDefExt = L"dxf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return;
    std::wstring error;
    if (!WriteFileSafely(file, Doc().ToDxf(), &error)) {
        MessageBoxW(m_hwnd, (L"Could not export\n" + std::wstring(file) + L"\n\n" + error).c_str(), APP_NAME,
                    MB_ICONERROR);
        return;
    }
    Message(L"Exported " + std::to_wstring(Doc().Count()) + L" objects to " + file +
            L" (DXF R12: opens in AutoCAD, LibreCAD, DraftSight and others).");
}

void MainWindow::ImportDxf() {
    wchar_t file[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"DXF drawings (*.dxf)\0*.dxf\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;
    std::string text;
    DxfResult r;
    std::wstring error;
    if (!ReadWholeFile(file, text) || !ReadDxf(text, r, error)) {
        MessageBoxW(m_hwnd, (L"Could not import \x201C" + FileNameFromPath(file) + L"\x201D.\n\n" + error).c_str(),
                    APP_NAME, MB_ICONERROR);
        return;
    }
    const size_t n = r.entities.size();
    Doc().BeginChange(L"Import DXF");
    const std::vector<uint64_t> ids = Doc().AddGroup(std::move(r.entities));
    Doc().EndChange();
    Eng().ClearSelection();
    Eng().AddToSelection(ids);
    Box box;
    for (uint64_t id : ids)
        if (const Entity* e = Doc().Find(id)) box.Add(Bounds(*e));
    if (!box.Empty()) m_canvas.ZoomWindow(box.Inflated(std::max(box.Width(), box.Height()) * 0.05 + 1e-6));
    Message(L"Imported " + std::to_wstring(n) + L" objects from " + FileNameFromPath(file) +
            L" (selected: move them with M). " + r.Notes());
    AfterInput();
}

void MainWindow::ShowHistory() {
    const std::vector<VersionInfo> versions = Cur().history.Versions();
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
    Eng().ClearSelection();
    Doc().ReplaceAll(std::move(ents), L"Restore version");
    Message(L"Restored the version from " + FormatFileTime(versions[(size_t)i].time) +
            L". Undo (Ctrl+Z) brings back what was there before.");
    m_canvas.ZoomExtents();
    AfterInput();
}

void MainWindow::Recover(bool afterCrash) {
    std::vector<HistoryDrawing> all = History::AllDrawings();
    const int i = RecoverDialog(m_hwnd, all, afterCrash);
    if (i < 0 || i >= (int)all.size()) return;
    const HistoryDrawing& h = all[(size_t)i];
    std::string text;
    std::wstring error;
    auto d = MakeDrawing();
    --m_untitledCount;
    if (!ReadWholeFile(h.latest.file, text) || !d->doc.FromText(text, error)) {
        MessageBoxW(m_hwnd, (L"The copy could not be read.\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return;
    }
    d->suggestedPath = h.source;
    d->untitledName = L"Recovered " + StripExtension(h.title);
    d->history.SetDrawing(h.source, h.title);
    d->doc.MarkModified();
    d->lastAutosave = GetTickCount64();
    AddDrawing(std::move(d));
    m_canvas.ZoomExtents();
    Message(L"Recovered \x201C" + h.title + L"\x201D from " + FormatFileTime(h.latest.time) + L". Save it to keep it.");
    AfterInput();
}

void MainWindow::Autosave() {
    if (m_settings.autosaveMinutes <= 0) return;
    const ULONGLONG now = GetTickCount64();
    for (auto& d : m_drawings) {
        if (!d->doc.Modified() || d->doc.Revision() == d->autosavedRevision) continue;
        if (now - d->lastAutosave < (ULONGLONG)m_settings.autosaveMinutes * 60000) continue;
        if (d->history.AddVersion(d->doc.ToText(), true, m_settings.keepVersions)) {
            d->autosavedRevision = d->doc.Revision();
            d->lastAutosave = now;
        }
    }
}

// ---------------------------------------------------------------------------
// Dimensions and the clipboard
// ---------------------------------------------------------------------------

void MainWindow::EditDimension(uint64_t id) {
    const Entity* e = Doc().Find(id);
    if (!e || e->type != EntityType::Dimension || !e->dim) return;
    Eng().Cancel();
    const DimData before = *e->dim;
    DimEditResult r;
    if (!DimEditDialog(m_hwnd, before, HasRefs(*e), r)) {
        FocusCommandLine();
        return;
    }
    Doc().BeginChange(L"Edit dimension");
    if (r.text != before.text || r.precision != before.style.precision) {
        Entity x = *Doc().Find(id);
        DimData d = *x.dim;
        d.text = r.text;
        d.style.precision = r.precision;
        x.dim = std::make_shared<const DimData>(std::move(d));
        Doc().Replace(x);
    }
    std::wstring why;
    bool ok = true;
    if (r.valueChanged) {
        const double v = before.kind == DimData::Angular ? ToRadians(r.value) : r.value;
        ok = DriveDimension(Doc(), id, v, why);
    }
    Doc().EndChange();
    if (!ok) {
        Message(why);
        MessageBoxW(m_hwnd, why.c_str(), APP_NAME, MB_ICONINFORMATION);
    } else if (const Entity* now = Doc().Find(id)) {
        Message(L"The dimension now reads " + DimDisplayText(*now->dim) + L".");
    }
    AfterInput();
    FocusCommandLine();
}

void MainWindow::DimensionStyle() {
    DrawingSettings s = Doc().Settings();
    bool apply = false;
    if (!DimStyleDialog(m_hwnd, s.dimStyle, apply)) return;
    Doc().SetSettings(s);
    if (apply) {
        Doc().BeginChange(L"Dimension style");
        std::vector<Entity> changed;
        for (const auto& kv : Doc().Entities()) {
            if (kv.second.type != EntityType::Dimension || !kv.second.dim) continue;
            Entity x = kv.second;
            DimData d = *x.dim;
            d.style = s.dimStyle;
            x.dim = std::make_shared<const DimData>(std::move(d));
            changed.push_back(std::move(x));
        }
        for (const Entity& x : changed) Doc().Replace(x);
        Doc().EndChange();
        Message(L"Dimension style changed for new dimensions and " + std::to_wstring(changed.size()) +
                L" existing one(s).");
    } else {
        Message(L"Dimension style changed for new dimensions.");
    }
    AfterInput();
}

void MainWindow::ClipboardCommand(int id) {
    // Text being typed in the command line is copied and pasted as text.
    HWND input = m_cmdline.Input();
    if (GetFocus() == input) {
        DWORD a = 0, b = 0;
        SendMessageW(input, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
        if ((id == ID_COPYCLIP || id == ID_CUTCLIP) && a != b) {
            SendMessageW(input, id == ID_COPYCLIP ? WM_COPY : WM_CUT, 0, 0);
            return;
        }
        if (id == ID_PASTECLIP && !(m_clipFormat && IsClipboardFormatAvailable(m_clipFormat)) &&
            IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            SendMessageW(input, WM_PASTE, 0, 0);
            return;
        }
    }
    Run(id == ID_COPYCLIP ? L"COPYCLIP" : id == ID_CUTCLIP ? L"CUTCLIP" : L"PASTECLIP");
}

void MainWindow::SetClipboard(const std::string& text) {
    if (!m_clipFormat || !OpenClipboard(m_hwnd)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (mem) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), text.size() + 1);
            GlobalUnlock(mem);
            if (!SetClipboardData(m_clipFormat, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

bool MainWindow::GetClipboard(std::string& text) {
    text.clear();
    if (!m_clipFormat || !IsClipboardFormatAvailable(m_clipFormat) || !OpenClipboard(m_hwnd)) return false;
    if (HANDLE h = GetClipboardData(m_clipFormat)) {
        if (const char* p = (const char*)GlobalLock(h)) {
            text.assign(p, strnlen(p, GlobalSize(h)));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return !text.empty();
}

void MainWindow::ApplyTheme(int mode) {
    m_settings.themeMode = mode;
    ReloadTheme((ThemeMode)mode);
    ApplyWindowTheme(m_hwnd);
    m_menuBar.OnThemeChanged();
    m_tools.OnThemeChanged();
    m_tabs.OnThemeChanged();
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
            Eng().Text(*(const std::wstring*)lp);
            AfterInput();
            return 0;
        case WM_APP_COMMAND_KEY:
            if (wp == VK_DELETE && !Eng().Selection().empty() && !Eng().Active()) Run(L"ERASE");
            if (wp == 'A' && Eng().WantsSelection()) {
                Eng().SelectAll();
                AfterInput();
            }
            return 0;
        case WM_APP_COMMAND_TYPING:
            m_canvas.SetTypedText(*(const std::wstring*)lp);
            return 0;
        case WM_APP_CANVAS_STATE:
            UpdateStatus();
            UpdatePrompt();
            return 0;
        case WM_APP_CANVAS_MENU:
            ShowContextMenu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_APP_CANVAS_EDIT:
            EditDimension(*(const uint64_t*)lp);
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
            m_tabs.OnDpiChanged();
            m_cmdline.OnDpiChanged();
            m_status.OnDpiChanged();
            m_canvas.OnDpiChanged();
            Layout();
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t file[MAX_PATH * 4];
            std::vector<std::wstring> files;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i)
                if (DragQueryFileW(drop, i, file, (UINT)std::size(file))) files.push_back(file);
            DragFinish(drop);
            Eng().Cancel();
            for (const std::wstring& f : files) OpenFile(f);  // each in a tab
            return 0;
        }
        case WM_CREATE:
            DragAcceptFiles(m_hwnd, TRUE);
            return 0;
        case WM_CLOSE:
            Eng().Cancel();
            // Every drawing with unsaved changes asks.
            for (size_t i = 0; i < m_drawings.size(); ++i)
                if (!ConfirmDiscard(i)) return 0;
            m_settings.drafting = Eng().Draft();
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
