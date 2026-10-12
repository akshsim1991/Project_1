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
#include "Curve.h"
#include "Select.h"
#include "Icons.h"
#include "Plot.h"
#include "Print.h"
#include "Expr.h"
#include "Relate.h"
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
    {ID_TEXT, L"TEXT", nullptr},
    {ID_TEXTEDIT, L"TEXTEDIT", nullptr},
    {ID_LEADER, L"LEADER", nullptr},
    {ID_BALLOON, L"BALLOON", nullptr},
    {ID_CENTERMARK, L"CENTERMARK", nullptr},
    {ID_CENTERLINE, L"CENTERLINE", nullptr},
    {ID_HATCH, L"HATCH", nullptr},
    {ID_FILL, L"FILL", nullptr},
    {ID_BLOCK, L"BLOCK", nullptr},
    {ID_INSERT, L"INSERT", nullptr},
    {ID_ATTDEF, L"ATTDEF", nullptr},
    {ID_ATTEDIT, L"ATTEDIT", nullptr},
    {ID_PURGE, L"PURGE", nullptr},
    {ID_REVCLOUD, L"REVCLOUD", nullptr},
    {ID_AREA, L"AREA", nullptr},
    {ID_DIM_ORDINATE, L"DIMORDINATE", nullptr},
    {ID_DIM_ANGCOORD, L"DIMANGCOORD", nullptr},
    {ID_DIM_SPACE, L"DIMSPACE", nullptr},
    {ID_DIM_DRIVEN, L"DIMDRIVEN", nullptr},
    {ID_SHEET_NEW, L"SHEETNEW", nullptr},
    {ID_PAGESETUP, L"PAGESETUP", nullptr},
    {ID_TITLEBLOCK, L"TITLEBLOCK", nullptr},
    {ID_DWGPROPS, L"DRAWINGPROPS", nullptr},
    {ID_MVIEW, L"MVIEW", nullptr},
    {ID_VPSCALE, L"VPSCALE", nullptr},
    {ID_VPPAN, L"VPPAN", nullptr},
    {ID_VPLOCK, L"VPLOCK", nullptr},
    {ID_PLOT, L"PLOT", nullptr},
    {ID_EXPORT_PDF, L"EXPORTPDF", nullptr},
    {ID_TANLINE, L"TANLINE", nullptr},
    {ID_REL_HORIZONTAL, L"HORIZONTAL", nullptr},
    {ID_REL_VERTICAL, L"VERTICAL", nullptr},
    {ID_REL_PARALLEL, L"PARALLEL", nullptr},
    {ID_REL_PERPENDICULAR, L"PERPENDICULAR", nullptr},
    {ID_REL_COLLINEAR, L"COLLINEAR", nullptr},
    {ID_REL_TANGENT, L"TANGENT", nullptr},
    {ID_REL_CONCENTRIC, L"CONCENTRIC", nullptr},
    {ID_REL_EQUAL, L"EQUAL", nullptr},
    {ID_REL_COINCIDENT, L"COINCIDENT", nullptr},
    {ID_REL_MIDPOINT, L"MIDPOINT", nullptr},
    {ID_REL_SYMMETRIC, L"SYMMETRIC", nullptr},
    {ID_REL_FIX, L"FIX", nullptr},
    {ID_REL_MAINTAIN, L"MAINTAINREL", nullptr},
    {ID_REL_AUTO, L"AUTOREL", nullptr},
    {ID_REL_SHOW, L"SHOWREL", nullptr},
    {ID_DIMLOCK, L"DIMLOCK", nullptr},
    {ID_VAR, L"VAR", nullptr},
    {ID_VARIABLES, L"VARIABLES", nullptr},
    {ID_GOALSEEK, L"GOALSEEK", nullptr},
    {ID_FCF, L"FCF", nullptr},
    {ID_DATUM, L"DATUM", nullptr},
    {ID_SURFACE, L"SURFACE", nullptr},
    {ID_WELD, L"WELD", nullptr},
    {ID_PARTSLIST, L"PARTSLIST", nullptr},
    {ID_SKETCH, L"SKETCH", nullptr},
    {ID_CIRCLE_TTR, L"CIRCLE", L"T"},
    {ID_LAYERS, L"LAYER", nullptr},
    {ID_PROPERTIES, L"PROPERTIES", nullptr},
    {ID_MATCHPROP, L"MATCHPROP", nullptr},
    {ID_CONSTRUCTION, L"CONSTRUCTION", nullptr},
    {ID_ORIGIN, L"ORIGIN", nullptr},
    {ID_LTSCALE, L"LTSCALE", nullptr},
};

const unsigned kSnapBits[] = {kSnapEndpoint, kSnapMidpoint, kSnapCentre, kSnapQuadrant,
                              kSnapIntersection, kSnapPerpendicular, kSnapTangent, kSnapNearest};

// The colours offered as swatches in the guide bar (colour indexes:
// red, orange, yellow, green, cyan, blue, magenta, black/white, greys, brown).
const int32_t kSwatchColors[] = {1, 30, 2, 3, 4, 5, 6, 7, 8, 9, 34};
const wchar_t* const kSwatchNames[] = {L"Red", L"Orange", L"Yellow", L"Green", L"Cyan", L"Blue", L"Magenta",
                                       L"Black (white on a dark background)", L"Dark grey", L"Light grey", L"Brown"};
std::wstring FriendlyColour(int32_t c) {
    for (size_t i = 0; i < std::size(kSwatchColors); ++i)
        if (kSwatchColors[i] == c) return kSwatchNames[i];
    return ColorName(c);
}

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
    m_menuBar.AddTextButton(ID_MENU_RELATE, L"Relate", nullptr, 54);
    m_menuBar.AddTextButton(ID_MENU_ANNOTATE, L"Annotate", nullptr, 72);
    m_menuBar.AddTextButton(ID_MENU_FORMAT, L"Format", nullptr, 58);
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
    m_tools.AddIconButton(ID_TEXT, kIconText, L"Text (T): several lines, fonts, symbols and fields. Double-click a text to edit it");
    m_tools.AddIconButton(ID_HATCH, kIconHatch, L"Hatch (H): click inside an area");
    m_tools.AddIconButton(ID_INSERT, kIconBlock, L"Insert block (I). BLOCK makes one from objects");
    m_tools.AddSeparator();
    // The current layer (or the selected objects' layer).
    m_layerCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0,
                                   Dpi(400, m_dpi), m_hwnd, (HMENU)(INT_PTR)ID_LAYER_COMBO, GetModuleHandleW(nullptr), nullptr);
    m_tools.AddChild(m_layerCombo, 150);
    m_tools.AddIconButton(ID_LAYERS, kIconLayers, L"Layers (LA): on/off, freeze, lock, colour, line type, layer states");
    m_tools.AddSeparator();
    m_tools.AddIconButton(ID_ZOOM_EXTENTS, kIconZoomExtents, L"Zoom to the drawing (double-click the wheel)");
    m_tools.AddIconButton(ID_ZOOM_WINDOW, kIconZoomWindow, L"Zoom to a window");
    m_tools.AddIconButton(ID_DIST, kIconDistance, L"Measure a distance (DI)");

    m_tabs.Create(m_hwnd, m_hwnd);
    m_guide.Create(m_hwnd, m_hwnd);
    {
        TabBar::Ids ids;
        ids.tabFirst = ID_SHEET_TAB_FIRST;
        ids.closeFirst = 0;
        ids.plus = ID_SHEET_NEW;
        ids.menuFirst = ID_SHEET_MENU_FIRST;
        ids.plusTip = L"New sheet: paper with a border, title block and a view of the drawing";
        ids.bottom = true;
        m_sheetTabs.Create(m_hwnd, m_hwnd, ids);
    }
    m_canvas.Create(m_hwnd, &Eng(), &Doc());
    m_props.Create(m_hwnd);
    ShowWindow(m_props.Hwnd(), m_settings.propertiesOn ? SW_SHOW : SW_HIDE);
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
        {FVIRTKEY | FCONTROL, '1', ID_PROPERTIES},
        {FVIRTKEY | FCONTROL, 'C', ID_COPYCLIP},
        {FVIRTKEY | FCONTROL, 'X', ID_CUTCLIP},
        {FVIRTKEY | FCONTROL, 'V', ID_PASTECLIP},
        {FVIRTKEY | FCONTROL, 'W', ID_CLOSE_DRAWING},
        {FVIRTKEY | FCONTROL, 'P', ID_PLOT},
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
        {FVIRTKEY | FALT, 'R', ID_MENU_RELATE},
        {FVIRTKEY | FALT, 'A', ID_MENU_ANNOTATE},
        {FVIRTKEY | FALT, 'O', ID_MENU_FORMAT},
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
    const int statusH = m_status.Height(), cmdH = m_cmdline.Height(), sheetH = m_sheetTabs.Height();
    const int guideH = m_guide.Height();
    HDWP dwp = BeginDeferWindowPos(9);
    dwp = DeferWindowPos(dwp, m_menuBar.Hwnd(), nullptr, 0, 0, rc.right, menuH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_tools.Hwnd(), nullptr, 0, menuH, rc.right, toolH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_tabs.Hwnd(), nullptr, 0, menuH + toolH, rc.right, tabH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_guide.Hwnd(), nullptr, 0, menuH + toolH + tabH, rc.right, guideH, SWP_NOZORDER);
    const int canvasTop = menuH + toolH + tabH + guideH;
    const int canvasH = std::max(10, (int)rc.bottom - canvasTop - cmdH - statusH - sheetH);
    const int panelW = m_settings.propertiesOn ? std::min(m_props.Width(), (int)rc.right / 2) : 0;
    dwp = DeferWindowPos(dwp, m_canvas.Hwnd(), nullptr, 0, canvasTop, rc.right - panelW, canvasH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_props.Hwnd(), nullptr, rc.right - panelW, canvasTop, panelW, canvasH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_sheetTabs.Hwnd(), nullptr, 0, canvasTop + canvasH, rc.right, sheetH, SWP_NOZORDER);
    dwp = DeferWindowPos(dwp, m_cmdline.Hwnd(), nullptr, 0, canvasTop + canvasH + sheetH, rc.right, cmdH, SWP_NOZORDER);
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
    else if (name == L"LAYER") ShowLayers();
    else if (name == L"SHEETNEW") NewSheetTab();
    else if (name == L"PAGESETUP") PageSetup();
    else if (name == L"TITLEBLOCK") TitleBlock();
    else if (name == L"DRAWINGPROPS") DrawingProps();
    else if (name == L"VARIABLES") ShowVariables();
    else if (name == L"PLOT") Plot();
    else if (name == L"EXPORTPDF") ExportPdf();
    else if (name == L"PROPERTIES") {
        m_settings.propertiesOn = !m_settings.propertiesOn;
        ShowWindow(m_props.Hwnd(), m_settings.propertiesOn ? SW_SHOW : SW_HIDE);
        Layout();
        UpdateProperties(true);
    }
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
    if (const SolveReport& sr = Doc().LastSolve(); !sr.ok && sr.serial != m_solveShown) {
        m_solveShown = sr.serial;
        Message(sr.message);
    }
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
    UpdateLayerCombo();
    UpdateProperties();
}

void MainWindow::UpdateTabs() {
    if (!m_ready) return;
    std::vector<TabBar::Tab> tabs;
    for (const auto& d : m_drawings) {
        TabBar::Tab t;
        t.title = DrawingName(*d);
        t.tip = !d->path.empty() ? d->path : !d->suggestedPath.empty() ? d->suggestedPath + L"  (not saved yet)" : t.title + L"  (not saved yet)";
        t.modified = d->Modified();
        tabs.push_back(std::move(t));
    }
    m_tabs.SetTabs(tabs, (int)m_active);
    UpdateSheetTabs();
}

// ---------------------------------------------------------------------------
// Sheets
// ---------------------------------------------------------------------------

void MainWindow::SetSheets(Drawing& d, SheetList sheets) {
    d.space = -1;
    d.sheets.clear();
    d.spaces.clear();
    for (auto& s : sheets) d.AddSheet(std::move(s), *this);
}

void MainWindow::AttachCanvas() {
    Drawing& d = Cur();
    m_canvas.SetSheet(d.space >= 0 ? d.sheets[(size_t)d.space].get() : nullptr, &d.doc);
    m_canvas.SetDrawing(&Eng(), &Doc());
    m_fields.clear();
    UpdateFields();
    m_canvas.SetView(d.ActiveView());
    UpdateSheetTabs();
}

void MainWindow::UpdateSheetTabs() {
    if (!m_ready) return;
    std::vector<TabBar::Tab> tabs;
    TabBar::Tab model;
    model.title = L"Drawing";
    model.tip = L"The drawing itself (model), at full size";
    tabs.push_back(model);
    for (const auto& s : Cur().sheets) {
        TabBar::Tab t;
        t.title = s->setup.name;
        t.tip = s->setup.paper + (s->setup.landscape ? L" landscape" : L" portrait") + L" sheet. Right-click: page setup, delete";
        tabs.push_back(t);
    }
    m_sheetTabs.SetTabs(tabs, Cur().space + 1);
}

void MainWindow::ShowSpace(int space) {
    Drawing& d = Cur();
    if (space < -1 || space >= (int)d.sheets.size()) return;
    if (space != d.space) {
        Eng().Cancel();  // a command belongs to its sheet
        d.ActiveView() = m_canvas.GetView();
        const Drafting draft = Eng().Draft();
        d.space = space;
        Eng().Draft() = draft;
        AttachCanvas();
    }
    AfterInput();
    FocusCommandLine();
}

void MainWindow::NewSheetTab() {
    Drawing& d = Cur();
    Eng().Cancel();
    const Units u = d.doc.Settings().units;
    const bool imperial = u == Units::Inches || u == Units::Feet;
    std::wstring name;
    for (size_t n = d.sheets.size() + 1;; ++n) {
        name = L"Sheet" + std::to_wstring(n);
        if (std::none_of(d.sheets.begin(), d.sheets.end(), [&](const auto& s) { return SameName(s->setup.name, name); })) break;
    }
    d.AddSheet(NewSheet(d.doc, name, imperial ? L"ANSI B (Tabloid)" : L"A3", true), *this);
    d.sheetsChanged = true;
    ShowSpace((int)d.sheets.size() - 1);
    const Entity* vp = nullptr;
    for (const auto& kv : d.sheets.back()->doc.Entities())
        if (kv.second.type == EntityType::Viewport) vp = &kv.second;
    Message(L"New sheet \x201C" + name + L"\x201D: " + d.sheets.back()->setup.paper + L" landscape, with a border, title block and the drawing at " +
            (vp ? ScaleText(ViewportRatio(*vp, d.doc)) : L"1:1") +
            L". Right-click the tab for page setup; MV adds viewports, VPS changes a scale; Ctrl+P prints.");
}

void MainWindow::SheetMenu(int index) {
    Drawing& d = Cur();
    if (index < 0 || index >= (int)d.sheets.size()) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"&Page setup (name, paper)...");
    AppendMenuW(m, MF_STRING, 2, L"&Border and title block again");
    AppendMenuW(m, MF_STRING, 3, L"Pr&int...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"&Delete sheet");
    POINT pt;
    GetCursorPos(&pt);
    const int id = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    if (id == 1) PageSetup();
    else if (id == 2) TitleBlock();
    else if (id == 3) Plot();
    else if (id == 4) DeleteSheet(index);
}

void MainWindow::PageSetup() {
    Drawing& d = Cur();
    if (d.space < 0) {
        Message(L"Page setup belongs to a sheet: click a sheet tab under the drawing (or + for a new sheet).");
        return;
    }
    Eng().Cancel();
    Sheet& sh = *d.sheets[(size_t)d.space];
    SheetSetup s = sh.setup;
    bool redo = false;
    if (!PageSetupDialog(m_hwnd, s, redo)) return;
    const bool resized = s.width != sh.setup.width || s.height != sh.setup.height;
    sh.setup = s;
    d.sheetsChanged = true;
    if (redo) AddBorder(sh);
    AttachCanvas();
    if (resized) m_canvas.ZoomExtents();
    AfterInput();
}

void MainWindow::TitleBlock() {
    Drawing& d = Cur();
    if (d.space < 0) {
        Message(L"The border and title block go on a sheet: click a sheet tab under the drawing first.");
        return;
    }
    Eng().Cancel();
    AddBorder(*d.sheets[(size_t)d.space]);
    Message(L"Border and title block made for " + d.sheets[(size_t)d.space]->setup.paper +
            L". Its fields come from File > Drawing properties.");
    AfterInput();
}

void MainWindow::DeleteSheet(int index) {
    Drawing& d = Cur();
    if (index < 0 || index >= (int)d.sheets.size()) return;
    const std::wstring q = L"Delete sheet \x201C" + d.sheets[(size_t)index]->setup.name + L"\x201D? This cannot be undone.";
    if (MessageBoxW(m_hwnd, q.c_str(), APP_NAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) return;
    ShowSpace(-1);
    d.sheets.erase(d.sheets.begin() + index);
    d.spaces.erase(d.spaces.begin() + index);
    d.sheetsChanged = true;
    UpdateSheetTabs();
    AfterInput();
}

void MainWindow::DrawingProps() {
    Drawing& d = Cur();
    std::vector<std::wstring> v;
    for (const PropertyKey& k : DrawingPropertyKeys()) {
        auto it = d.doc.Properties().find(k.key);
        v.push_back(it == d.doc.Properties().end() ? L"" : it->second);
    }
    if (!DrawingPropsDialog(m_hwnd, v)) return;
    for (size_t i = 0; i < v.size() && i < DrawingPropertyKeys().size(); ++i) d.doc.SetProperty(DrawingPropertyKeys()[i].key, v[i]);
    UpdateFields();
    m_canvas.Refresh();
    AfterInput();
}

void MainWindow::ShowVariables() {
    Document& d = Doc();
    std::vector<VariableRow> rows;
    for (const Variable& v : d.Variables()) {
        VariableRow r;
        r.name = v.name;
        r.formula = v.formula;
        r.comment = v.comment;
        r.value = v.value;
        rows.push_back(r);
    }
    for (const auto& kv : d.Entities()) {
        const Entity& e = kv.second;
        if (e.type != EntityType::Dimension || !e.dim || e.dim->name.empty()) continue;
        VariableRow r;
        r.name = e.dim->name;
        r.formula = e.dim->formula;
        r.dimension = true;
        r.attached = HasRefs(e);
        r.comment = e.dim->locked ? L"locked" : r.attached ? L"" : L"not attached";
        LookupName(d, r.name, r.value);
        rows.push_back(r);
    }
    if (!VariablesDialog(m_hwnd, rows, [this](double v) { return Eng().Len(v); })) return;
    std::vector<Variable> vars;
    for (const VariableRow& r : rows)
        if (!r.dimension) {
            Variable v;
            v.name = r.name;
            v.formula = r.formula;
            v.comment = r.comment;
            vars.push_back(v);
        }
    d.BeginChange(L"Variables");
    if (!(vars == d.Variables())) d.SetVariables(vars, L"Variables");
    for (const VariableRow& r : rows) {
        if (!r.dimension) continue;
        for (const auto& kv : d.Entities()) {
            const Entity& e = kv.second;
            if (e.type != EntityType::Dimension || !e.dim || !SameName(e.dim->name, r.name) || e.dim->formula == r.formula) continue;
            Entity x = e;
            DimData dd = *x.dim;
            dd.formula = r.formula;
            if (!r.formula.empty()) dd.locked = true, dd.value = DimMeasure(dd);
            x.dim = std::make_shared<const DimData>(dd);
            d.Replace(x);
            break;
        }
    }
    d.EndChange();
    m_canvas.Refresh();
    AfterInput();
}

std::vector<PlotPage> MainWindow::BuildPages(const PlotRequest& r) {
    Drawing& d = Cur();
    std::vector<PlotPage> pages;
    PlotStyle style;
    style.colourMode = r.colourMode;
    style.lineWeights = r.lineWeights;
    auto sheet = [&](size_t i) {
        PlotStyle st = StyleOf(d.sheets[i]->setup);
        st.colourMode = r.colourMode;
        st.lineWeights = r.lineWeights;
        PlotPage p;
        PlotSheet(*d.sheets[i], d.doc, FieldsFor((int)i), p, &st);
        pages.push_back(std::move(p));
    };
    if (r.what == PlotRequest::ThisSheet && d.space >= 0) {
        sheet((size_t)d.space);
    } else if (r.what == PlotRequest::AllSheets) {
        for (size_t i = 0; i < d.sheets.size(); ++i) sheet(i);
    } else {
        Box area = d.doc.Extents();
        if (r.what == PlotRequest::DrawingView && d.space < 0) area = m_canvas.ViewBox();
        if (area.Empty()) return pages;
        if (r.what == PlotRequest::DrawingAll) area = area.Inflated(std::max(area.Width(), area.Height()) * 0.01);
        SheetSetup s;
        s.paper = r.paper;
        s.landscape = r.landscape;
        ApplyPaper(s);
        PlotPage p;
        PlotDrawing(d.doc, area, s.width, s.height, 10, r.ratio * MmPerUnit(d.doc.Settings().units), style, FieldsFor(-1), p);
        p.name = DrawingName() + (r.ratio > 0 ? L" at " + ScaleText(r.ratio) : L"");
        pages.push_back(std::move(p));
    }
    return pages;
}

void MainWindow::Plot() {
    Drawing& d = Cur();
    Eng().Cancel();
    static PlotRequest r;
    r.what = d.space >= 0 ? PlotRequest::ThisSheet : d.sheets.empty() ? PlotRequest::DrawingView : PlotRequest::AllSheets;
    if (d.space >= 0) {
        r.colourMode = d.sheets[(size_t)d.space]->setup.colourMode;
        r.lineWeights = d.sheets[(size_t)d.space]->setup.lineWeights;
    }
    const int action = PlotDialog(m_hwnd, r, !d.sheets.empty(), d.space >= 0, [this](const PlotRequest& q) { return BuildPages(q); });
    if (action == 0) return;
    const std::vector<PlotPage> pages = BuildPages(r);
    if (pages.empty()) {
        Message(L"There is nothing to print.");
        return;
    }
    if (action == 2) {
        ExportPdf(&pages);
        return;
    }
    std::wstring error;
    if (PrintPages(m_hwnd, pages, r.fit, DrawingName(), error))
        Message(L"Sent " + std::to_wstring(pages.size()) + L" page(s) to the printer.");
    else if (!error.empty())
        MessageBoxW(m_hwnd, error.c_str(), APP_NAME, MB_ICONERROR);
}

void MainWindow::ExportPdf(const std::vector<PlotPage>* given) {
    Drawing& d = Cur();
    std::vector<PlotPage> pages;
    if (given) {
        pages = *given;
    } else {
        PlotRequest r;
        r.what = d.sheets.empty() ? PlotRequest::DrawingAll : PlotRequest::AllSheets;
        pages = BuildPages(r);
    }
    if (pages.empty()) {
        Message(L"There is nothing to put in a PDF: the drawing is empty.");
        return;
    }
    wchar_t file[MAX_PATH * 4] = L"";
    const std::wstring base = !d.path.empty() ? d.path : !d.suggestedPath.empty() ? d.suggestedPath : d.untitledName;
    wcsncpy_s(file, (StripExtension(base) + L".pdf").c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"PDF document (*.pdf)\0*.pdf\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)std::size(file);
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return;
    std::wstring error;
    if (!WriteFileSafely(file, WritePdf(pages, DrawingName()), &error)) {
        MessageBoxW(m_hwnd, (L"Could not save\n" + std::wstring(file) + L"\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return;
    }
    Message(L"Saved " + std::to_wstring(pages.size()) + L" page(s) as " + file + L".");
}

void MainWindow::UpdatePrompt() {
    if (!m_ready) return;
    if (Eng().Active()) m_cmdline.SetPrompt(Eng().CurrentPrompt().FullText(), false);
    else m_cmdline.SetPrompt(L"Command:", true);
    m_cmdline.SetSpaces(Eng().Active() && Eng().CurrentPrompt().spaces);
    UpdateGuide();
}

void MainWindow::UpdateGuide() {
    if (!m_ready) return;
    using B = GuideBar::Button;
    std::vector<B> buttons;
    std::wstring title, text;
    const bool active = Eng().Active();
    if (active) {
        const Prompt& p = Eng().CurrentPrompt();
        const CommandInfo* info = FindCommand(Eng().ActiveName());
        title = info ? info->title : Eng().ActiveName();
        text = p.text;
        if (!p.text.empty() && (p.accept & kAcceptPoint) && !p.pickObject && !(p.accept & kAcceptSelect))
            text += (p.hasBase ? L" (or type a length)" : L"");
        for (size_t i = 0; i < p.keywords.size() && i < 30; ++i) {
            // "cuTting edges" (the typed letter in capitals) reads as "Cutting edges" on a button.
            std::wstring name = p.keywords[i].name;
            if (!name.empty() && std::iswlower(name[0])) {
                for (wchar_t& ch : name) ch = (wchar_t)std::towlower(ch);
                name[0] = (wchar_t)std::towupper(name[0]);
            }
            buttons.push_back({ID_CTX_KEYWORD + (int)i, name, L"Choose " + name + L" (or type " + p.keywords[i].key + L")", 0});
        }
        if (p.accept & kAcceptEmpty) {
            if (!p.defaultText.empty() && !(p.accept & kAcceptSelect))
                buttons.push_back({ID_CTX_ENTER, L"Use " + p.defaultText, L"Take the value shown (Enter)", 1});
            else
                buttons.push_back({ID_CTX_ENTER, L"Done", L"Finish this step (Enter, or right-click > Enter)", 1});
        }
        buttons.push_back({ID_CTX_CANCEL, L"Cancel", L"Stop this command (Esc)", 2});
    } else if (const size_t n = Eng().Selection().size()) {
        title = std::to_wstring(n) + (n == 1 ? L" object selected" : L" objects selected");
        text = L"What to do with it:";
        buttons = {{ID_MOVE, L"Move", L"Move to another place (M)", 0},     {ID_COPY, L"Copy", L"Make copies (CO)", 0},
                   {ID_ROTATE, L"Rotate", L"Turn about a point (RO)", 0},  {ID_MIRROR, L"Mirror", L"Mirror about a line (MI)", 0},
                   {ID_SCALE, L"Scale", L"Make bigger or smaller (SC)", 0}, {ID_ERASE, L"Erase", L"Delete (Delete key)", 0},
                   {ID_ESCAPE, L"Clear selection", L"Select nothing (Esc)", 2}};
    } else {
        text = L"Choose a tool above (Line, Circle, Rectangle...) or type its name below (L, C, REC) and press Enter. "
               L"Click an object to select it.";
    }
    m_guide.SetGuide(title, text, std::move(buttons), active);

    // Colour and thickness: of the selection, or for new objects.
    int32_t color = Doc().Current().color;
    int lw = Doc().Current().lineweight;
    bool first = true;
    for (uint64_t id : Eng().Selection())
        if (const Entity* e = Doc().Find(id)) {
            if (first) color = e->color, lw = e->lineweight, first = false;
            else {
                if (e->color != color) color = INT32_MIN;
                if (e->lineweight != lw) lw = INT32_MIN;
            }
        }
    std::vector<GuideBar::Swatch> sw;
    int chosen = 0;
    const Theme& th = CurrentTheme();
    for (size_t i = 0; i < std::size(kSwatchColors); ++i) {
        const int32_t c = kSwatchColors[i];
        uint32_t rgb = ColorRgb(c);
        COLORREF cr = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
        if (c == 7) cr = th.dark ? RGB(255, 255, 255) : RGB(0, 0, 0);  // white on dark paper, black on light
        sw.push_back({ID_SWATCH_FIRST + (int)i, cr, kSwatchNames[i]});
        if (c == color) chosen = ID_SWATCH_FIRST + (int)i;
    }
    std::wstring wt = lw == INT32_MIN ? L"(mixed)" : lw == kLwByLayer ? L"as layer" : lw == kLwDefault ? L"normal" : LineWeightName(lw);
    m_guide.SetStyle(std::move(sw), chosen, ID_SWATCH_MORE, ID_WEIGHT_MENU, wt);
}

void MainWindow::ApplyColour(int32_t color) {
    if (Eng().Selection().empty()) {
        CurrentStyle c = Doc().Current();
        c.color = color;
        Doc().SetCurrent(c);
        Message(L"New objects will be " + FriendlyColour(color) + L".");
    } else {
        ChangeSelected(L"Change colour", [color](Entity& x) { x.color = color; });
    }
    UpdateProperties(true);
    AfterInput();
}

void MainWindow::ThicknessMenu() {
    struct Choice {
        int lw;
        const wchar_t* name;
    };
    static const Choice choices[] = {{kLwByLayer, L"As the layer"}, {13, L"Very thin  (0.13 mm)"}, {18, L"Thin  (0.18 mm)"},
                                     {25, L"Normal  (0.25 mm)"},     {35, L"Medium  (0.35 mm)"},   {50, L"Thick  (0.50 mm)"},
                                     {70, L"Thicker  (0.70 mm)"},   {100, L"Very thick  (1.00 mm)"}, {140, L"Heavy  (1.40 mm)"},
                                     {200, L"Extra heavy  (2.00 mm)"}};
    int now = Doc().Current().lineweight;
    if (!Eng().Selection().empty())
        if (const Entity* e = Doc().Find(*Eng().Selection().begin())) now = e->lineweight;
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < std::size(choices); ++i) {
        AppendMenuW(m, MF_STRING | (choices[i].lw == now ? MF_CHECKED : 0), i + 1, choices[i].name);
        if (i == 0) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    const RECT r = m_guide.ItemScreenRect(ID_WEIGHT_MENU);
    const int pick = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, r.right, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    if (pick <= 0) return;
    const int lw = choices[pick - 1].lw;
    if (!Eng().Draft().lwDisplay) {  // thickness that cannot be seen is no use
        Eng().Draft().lwDisplay = true;
        DraftingChanged();
    }
    if (Eng().Selection().empty()) {
        CurrentStyle c = Doc().Current();
        c.lineweight = lw;
        Doc().SetCurrent(c);
        Message(std::wstring(L"New objects will be drawn ") + choices[pick - 1].name + L".");
    } else {
        ChangeSelected(L"Change thickness", [lw](Entity& x) { x.lineweight = (int16_t)lw; });
    }
    UpdateProperties(true);
    AfterInput();
}

void MainWindow::UpdateStatus() {
    if (!m_ready) return;
    Vec2 p;
    const bool inside = m_canvas.CursorPoint(p);
    const Vec2 o = Doc().Settings().origin;  // shown from the grid origin
    m_status.SetCoordinates(inside ? Eng().Len(p.x - o.x) + L",  " + Eng().Len(p.y - o.y) : L"");
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
                         mode(kSnapIntersection), mode(kSnapCentre), d.otrackOn, d.dynOn, d.lwDisplay});
    m_status.SetUnits(UnitsSymbol(Doc().Settings().units));
    UpdateGuide();
}

std::wstring MainWindow::DrawingName(const Drawing& d) const {
    return d.path.empty() ? d.untitledName : StripExtension(FileNameFromPath(d.path));
}

void MainWindow::UpdateTitle() {
    UpdateFields();
    const std::wstring title = DrawingName() + (Cur().Modified() ? L"*" : L"") + L" - " + APP_NAME;
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
        case ID_TOGGLE_LWT: on = d.lwDisplay = !d.lwDisplay; name = L"Line weights"; break;
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
    if (id >= ID_SHEET_TAB_FIRST && id < ID_SHEET_MENU_FIRST) {
        ShowSpace(id - ID_SHEET_TAB_FIRST - 1);
        return;
    }
    if (id >= ID_SHEET_MENU_FIRST && id < ID_SHEET_MENU_FIRST + 100) {
        SheetMenu(id - ID_SHEET_MENU_FIRST - 1);
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
    if (id >= ID_SWATCH_FIRST && id < ID_SWATCH_MORE) {
        const size_t i = (size_t)(id - ID_SWATCH_FIRST);
        if (i < std::size(kSwatchColors)) ApplyColour(kSwatchColors[i]);
        return;
    }
    if (id == ID_SWATCH_MORE) {
        int32_t c = Doc().Current().color;
        if (PickColor(m_hwnd, c, true)) ApplyColour(c);
        return;
    }
    if (id == ID_WEIGHT_MENU) {
        ThicknessMenu();
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
        case ID_TOGGLE_LWT:
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
            Append(m, ID_EXPORT_PDF, L"Export P&DF...");
            Separator(m);
            Append(m, ID_PLOT, L"&Print...\tCtrl+P");
            Append(m, ID_PAGESETUP, L"Pa&ge setup...", Cur().space >= 0);
            Append(m, ID_DWGPROPS, L"Drawing pro&perties (title block)...");
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
            {
                HMENU sh = CreatePopupMenu();
                Append(sh, ID_SHEET_NEW, L"&New sheet");
                Append(sh, ID_PAGESETUP, L"&Page setup...", Cur().space >= 0);
                Append(sh, ID_TITLEBLOCK, L"&Border and title block", Cur().space >= 0);
                Separator(sh);
                Append(sh, ID_MVIEW, L"&Viewport\tMV", Cur().space >= 0);
                Append(sh, ID_VPSCALE, L"Viewport &scale\tVPS", Cur().space >= 0);
                Append(sh, ID_VPPAN, L"P&an viewport\tVPP", Cur().space >= 0);
                Append(sh, ID_VPLOCK, L"&Lock viewport\tVPLOCK", Cur().space >= 0);
                Submenu(m, sh, L"&Sheets");
            }
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
            Append(m, ID_SKETCH, L"&FreeSketch (draw by hand)\tFS");
            Separator(m);
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
            Append(m, ID_DIM_ORDINATE, L"&Ordinate (coordinate)\tDOR");
            Append(m, ID_DIM_ANGCOORD, L"Angular coordina&te\tDAC");
            Separator(m);
            Append(m, ID_DIM_CONTINUE, L"&Continue\tDCO");
            Append(m, ID_DIM_BASELINE, L"&Baseline\tDBA");
            Append(m, ID_DIM_SPACE, L"S&pace evenly\tDSP");
            Append(m, ID_DIM_DRIVEN, L"Dri&ven / driving\tDDR");
            Separator(m);
            Append(m, ID_DIM_EDIT, L"&Edit dimension (or double-click it)\tDED");
            Append(m, ID_DIMLOCK, L"Loc&k / unlock (holds its value)\tDLO");
            Append(m, ID_DIM_STYLE, L"Dimension st&yle...\tD");
            break;
        case ID_MENU_RELATE: {
            const DrawingSettings& ds = Doc().Settings();
            Append(m, ID_REL_COINCIDENT, L"&Connect (coincident)\tCOIN");
            Append(m, ID_REL_HORIZONTAL, L"&Horizontal\tHOR");
            Append(m, ID_REL_VERTICAL, L"&Vertical\tVER");
            Append(m, ID_REL_PARALLEL, L"&Parallel\tPAR");
            Append(m, ID_REL_PERPENDICULAR, L"Pe&rpendicular\tPERP");
            Append(m, ID_REL_COLLINEAR, L"Co&llinear\tCOLL");
            Append(m, ID_REL_TANGENT, L"&Tangent\tTANG");
            Append(m, ID_REL_CONCENTRIC, L"Co&ncentric\tCONC");
            Append(m, ID_REL_EQUAL, L"&Equal\tEQ");
            Append(m, ID_REL_MIDPOINT, L"&Midpoint\tMIDP");
            Append(m, ID_REL_SYMMETRIC, L"&Symmetric\tSYMM");
            Append(m, ID_REL_FIX, L"&Fix\tFIX");
            Separator(m);
            Append(m, ID_DIMLOCK, L"Lock &dimensions\tDLO");
            Append(m, ID_VARIABLES, L"Varia&bles...\tVARIABLES");
            Append(m, ID_GOALSEEK, L"&Goal seek\tGS");
            Separator(m);
            Append(m, ID_REL_MAINTAIN, L"M&aintain relationships", true, ds.maintainRelations);
            Append(m, ID_REL_AUTO, L"Add relationships while &drawing", true, ds.autoRelations);
            Append(m, ID_REL_SHOW, L"Sh&ow relationships", true, ds.showRelations);
            break;
        }
        case ID_MENU_ANNOTATE:
            Append(m, ID_TEXT, L"&Text\tT");
            Append(m, ID_TEXTEDIT, L"&Edit text (or double-click it)\tED");
            Separator(m);
            Append(m, ID_LEADER, L"&Leader\tLE");
            Append(m, ID_BALLOON, L"&Balloon (callout)\tBALLOON");
            Append(m, ID_CENTERMARK, L"Centre &mark\tCM");
            Append(m, ID_CENTERLINE, L"Centre li&ne\tCL");
            Append(m, ID_REVCLOUD, L"&Revision cloud\tRC");
            Separator(m);
            Append(m, ID_FCF, L"Feature &control frame (GD&&T)\tFCF");
            Append(m, ID_DATUM, L"&Datum\tDATUM");
            Append(m, ID_SURFACE, L"&Surface texture\tSURFACE");
            Append(m, ID_WELD, L"&Weld symbol\tWELD");
            Append(m, ID_PARTSLIST, L"Parts &list\tBOM");
            Separator(m);
            Append(m, ID_HATCH, L"&Hatch\tH");
            Append(m, ID_FILL, L"&Fill\tFILL");
            Separator(m);
            Append(m, ID_BLOCK, L"Make bloc&k\tB");
            Append(m, ID_INSERT, L"&Insert block\tI");
            Append(m, ID_ATTDEF, L"&Attribute definition\tATT");
            Append(m, ID_ATTEDIT, L"Edit attri&butes\tATE");
            Append(m, ID_PURGE, L"&Purge unused blocks\tPU");
            break;
        case ID_MENU_FORMAT:
            Append(m, ID_LAYERS, L"&Layers...\tLA");
            Append(m, ID_PROPERTIES, L"&Properties panel\tCtrl+1", true, m_settings.propertiesOn);
            Append(m, ID_MATCHPROP, L"&Match properties\tMA");
            Separator(m);
            Append(m, ID_CONSTRUCTION, L"&Construction geometry on/off\tCONS");
            Append(m, ID_LTSCALE, L"Line type &scale...\tLTS");
            Append(m, ID_TOGGLE_LWT, L"Show line &weights\tLWT", true, d.lwDisplay);
            Separator(m);
            Append(m, ID_ORIGIN, L"Grid &origin...\tORIGIN");
            Append(m, ID_DIM_STYLE, L"&Dimension style...\tD");
            Append(m, ID_UNITS, L"&Units...");
            break;
        case ID_MENU_TOOLS:
            Append(m, ID_DIST, L"Measure &distance\tDI");
            Append(m, ID_AREA, L"Measure &area\tAA");
            Append(m, ID_ID_POINT, L"Point &coordinates\tID");
            Append(m, ID_VARIABLES, L"&Variables...\tVARIABLES");
            Append(m, ID_GOALSEEK, L"Goal s&eek\tGS");
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
    d->autosavedRevision = d->Revision();
    d->lastAutosave = GetTickCount64();
    if (!m_drawings.empty()) d->engine.Draft() = Eng().Draft();  // drafting settings are the same everywhere
    return d;
}

void MainWindow::AddDrawing(std::unique_ptr<Drawing> d) {
    d->engine.Draft() = Eng().Draft();
    if (Cur().Pristine() && !Eng().Active()) {
        // The empty drawing nobody used yet makes room.
        m_drawings[m_active] = std::move(d);
        AttachCanvas();
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
        Cur().ActiveView() = m_canvas.GetView();
        const Drafting draft = Eng().Draft();
        m_active = index;
        Eng().Draft() = draft;
        AttachCanvas();
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
        AttachCanvas();
    } else {
        const Drafting draft = Eng().Draft();
        m_drawings.erase(m_drawings.begin() + (ptrdiff_t)index);
        m_active = std::min(index, m_drawings.size() - 1);
        Eng().Draft() = draft;
        AttachCanvas();
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
    if (!d.Modified()) return true;
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
    SheetList sheets;
    const bool ok = dxf ? DrawingFromDxf(text, d->doc, sheets, error, notes) : DrawingFromText(text, d->doc, sheets, error);
    if (ok) SetSheets(*d, std::move(sheets));
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
    d->autosavedRevision = d->Revision();
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
        Cur().history.AddVersion(DrawingToText(Cur().doc, Cur().sheets), false, m_settings.keepVersions);
    }
    return true;
}

bool MainWindow::SaveTo(const std::wstring& path) {
    const std::string text = DrawingToText(Cur().doc, Cur().sheets);
    std::wstring error;
    if (!WriteFileSafely(path, text, &error)) {
        Message(L"Could not save " + path + L": " + error);
        MessageBoxW(m_hwnd, (L"Could not save\n" + path + L"\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return false;
    }
    const bool samePath = !Cur().path.empty() && SamePath(path, Cur().path);
    Cur().path = path;
    Cur().suggestedPath.clear();
    Cur().MarkSaved();
    m_settings.AddRecent(path);
    if (samePath) Cur().history.AddVersion(text, false, m_settings.keepVersions);
    Cur().autosavedRevision = Cur().Revision();
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
    if (!WriteFileSafely(file, DrawingToDxf(Cur().doc, Cur().sheets), &error)) {
        MessageBoxW(m_hwnd, (L"Could not export\n" + std::wstring(file) + L"\n\n" + error).c_str(), APP_NAME,
                    MB_ICONERROR);
        return;
    }
    Message(L"Exported " + std::to_wstring(Cur().doc.Count()) + L" objects" +
            (Cur().sheets.empty() ? L"" : L" and sheet \x201C" + Cur().sheets[0]->setup.name + L"\x201D (as paper space)") + L" to " + file +
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
    ShowSpace(-1);  // into the drawing (not a sheet)
    Doc().BeginChange(L"Import DXF");
    const std::vector<uint64_t> ids = Doc().AddForeign(std::move(r.entities), r.layers, r.blocks);
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
    SheetList copySheets;
    std::wstring error;
    if (!ReadWholeFile(versions[(size_t)i].file, text) || !DrawingFromText(text, copy, copySheets, error)) {
        MessageBoxW(m_hwnd, (L"That version could not be read.\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return;
    }
    std::vector<Entity> ents;
    for (const auto& kv : copy.Entities()) ents.push_back(kv.second);
    ShowSpace(-1);
    Eng().ClearSelection();
    Doc().ReplaceAll(std::move(ents), L"Restore version", &copy.Blocks());
    SetSheets(Cur(), std::move(copySheets));
    Cur().sheetsChanged = true;
    AttachCanvas();
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
    SheetList sheets;
    if (!ReadWholeFile(h.latest.file, text) || !DrawingFromText(text, d->doc, sheets, error)) {
        MessageBoxW(m_hwnd, (L"The copy could not be read.\n\n" + error).c_str(), APP_NAME, MB_ICONERROR);
        return;
    }
    d->suggestedPath = h.source;
    d->untitledName = L"Recovered " + StripExtension(h.title);
    d->history.SetDrawing(h.source, h.title);
    SetSheets(*d, std::move(sheets));
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
        if (!d->Modified() || d->Revision() == d->autosavedRevision) continue;
        if (now - d->lastAutosave < (ULONGLONG)m_settings.autosaveMinutes * 60000) continue;
        if (d->history.AddVersion(DrawingToText(d->doc, d->sheets), true, m_settings.keepVersions)) {
            d->autosavedRevision = d->Revision();
            d->lastAutosave = now;
        }
    }
}

// ---------------------------------------------------------------------------
// Dimensions and the clipboard
// ---------------------------------------------------------------------------

int MainWindow::EditText(TextData& text, const std::wstring& title) {
    return TextEditDialog(m_hwnd, text, title) ? 1 : 0;
}

void MainWindow::EditObject(uint64_t id) {
    const Entity* e = Doc().Find(id);
    if (!e) return;
    if (e->type == EntityType::Dimension) return EditDimension(id);
    if (const TextData* t = TextOf(*e)) {
        Eng().Cancel();
        TextData edited = *t;
        if (!TextEditDialog(m_hwnd, edited, e->type == EntityType::Leader ? L"Leader note" : L"Edit text") || edited == *t)
            return;
        Entity x = *Doc().Find(id);
        EditAnnot(x).text = edited;
        Doc().BeginChange(L"Edit text");
        Doc().Replace(x);
        Doc().EndChange();
        AfterInput();
        return;
    }
    if (e->type == EntityType::Insert) {  // its attributes are in the Properties panel
        Eng().Cancel();
        Eng().ClearSelection();
        Eng().AddToSelection({id});
        if (!m_settings.propertiesOn) OnCommand(ID_PROPERTIES);
        AfterInput();
    }
}

FieldValues MainWindow::FieldsFor(int space) const {
    FieldValues f;
    f[L"filename"] = DrawingName();
    wchar_t buf[128] = L"";
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, buf, 128, nullptr)) f[L"date"] = buf;
    if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, buf, 128)) f[L"time"] = buf;
    DWORD n = 128;
    if (GetUserNameW(buf, &n)) f[L"author"] = buf;
    for (const auto& kv : Cur().doc.Properties()) f[kv.first] = kv.second;
    if (space >= 0) f = SheetFields(Cur().doc, Cur().sheets, (size_t)space, f);
    return f;
}

void MainWindow::UpdateFields() {
    const FieldValues f = FieldsFor(Cur().space);
    if (f != m_fields) {
        m_fields = f;
        m_canvas.SetFields(f);
    }
}

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


// ---------------------------------------------------------------------------
// Layers and the Properties panel
// ---------------------------------------------------------------------------

void MainWindow::ShowLayers() {
    Eng().Cancel();
    LayerDialog(m_hwnd, Doc(), [this] {
        Eng().PruneSelection();
        AfterInput();
        UpdateWindow(m_canvas.Hwnd());
    });
    AfterInput();
}

void MainWindow::ChangeSelected(const wchar_t* stepName, const std::function<void(Entity&)>& change) {
    const std::vector<uint64_t> ids(Eng().Selection().begin(), Eng().Selection().end());
    if (ids.empty()) return;
    Doc().BeginChange(stepName);
    for (uint64_t id : ids) {
        const Entity* e = Doc().Find(id);
        if (!e) continue;
        Entity x = *e;
        change(x);
        Doc().Replace(x);
    }
    Doc().EndChange();
}

void MainWindow::UpdateLayerCombo() {
    if (!m_layerCombo) return;
    std::vector<std::wstring> names;
    for (const Layer& l : Doc().Layers()) names.push_back(l.name);
    if (names != m_comboLayers) {
        m_comboLayers = names;
        SendMessageW(m_layerCombo, CB_RESETCONTENT, 0, 0);
        for (const std::wstring& n : names) SendMessageW(m_layerCombo, CB_ADDSTRING, 0, (LPARAM)n.c_str());
    }
    // The selected objects' layer when they share one, else the current layer.
    int show = Doc().Current().layer;
    if (!Eng().Selection().empty()) {
        int common = -2;
        for (uint64_t id : Eng().Selection())
            if (const Entity* e = Doc().Find(id)) common = common == -2 ? e->layer : (common == e->layer ? common : -1);
        show = common;
    }
    int index = -1;
    for (size_t i = 0; i < Doc().Layers().size(); ++i)
        if (Doc().Layers()[i].id == show) index = (int)i;
    if ((int)SendMessageW(m_layerCombo, CB_GETCURSEL, 0, 0) != index)
        SendMessageW(m_layerCombo, CB_SETCURSEL, (WPARAM)index, 0);
}

namespace {
bool ReadNumber(const std::wstring& text, double& v) { return ParseNumber(WideToUtf8(text), v) && std::isfinite(v); }

// Choices for colour, line type and line weight, with "by layer" and "by block" first.
std::vector<int32_t> ColorChoices() {
    std::vector<int32_t> c = {kByLayer, kByBlock};
    for (int i = 1; i <= 9; ++i) c.push_back(i);
    return c;
}
std::vector<int> LineTypeChoices() {
    std::vector<int> c = {kLtByLayer, kLtByBlock};
    for (size_t i = 0; i < LineTypes().size(); ++i) c.push_back((int)i);
    return c;
}
std::vector<int> WeightChoices() {
    std::vector<int> c = {kLwByLayer, kLwByBlock, kLwDefault};
    for (int w : LineWeights()) c.push_back(w);
    return c;
}
}  // namespace

namespace {
const std::vector<std::wstring>& CommonFonts() {
    static const std::vector<std::wstring> f = {L"Arial", L"Calibri", L"Cambria", L"Consolas", L"Courier New",
                                                L"Georgia", L"ISOCPEUR", L"Segoe UI", L"Tahoma", L"Times New Roman",
                                                L"Verdana"};
    return f;
}
// Lines of text in one line of the panel: a new line is shown as \P.
std::wstring OneLine(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) o += c == L'\n' ? std::wstring(L"\\P") : std::wstring(1, c);
    return o;
}
std::wstring FromOneLine(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size() && (s[i + 1] == L'P' || s[i + 1] == L'p')) {
            o += L'\n';
            ++i;
        } else {
            o += s[i];
        }
    }
    return o;
}
}  // namespace

void MainWindow::AnnotationRows(const Entity& e, std::vector<PropertiesPanel::Row>& rows) {
    using Row = PropertiesPanel::Row;
    const uint64_t id = e.id;
    const auto refresh = [this] { PostMessageW(m_hwnd, WM_APP_PROPERTIES, 0, 0); };
    // A row that changes the object through `set` (false: not possible).
    auto add = [&](Row r, std::function<bool(Entity&, const Row&)> set) {
        r.apply = [this, id, set, refresh](const Row& edited) {
            if (const Entity* cur = Doc().Find(id)) {
                Entity x = *cur;
                if (set(x, edited)) {
                    Doc().BeginChange(L"Properties");
                    Doc().Replace(x);
                    Doc().EndChange();
                } else {
                    Message(L"That value cannot be used here.");
                }
            }
            AfterInput();
            refresh();
        };
        rows.push_back(std::move(r));
    };
    auto text = [&](const wchar_t* label, const std::wstring& value, std::function<bool(Entity&, const std::wstring&)> set) {
        Row r;
        r.kind = Row::Text;
        r.label = label;
        r.value = value;
        add(r, [set](Entity& x, const Row& ed) { return set(x, ed.value); });
    };
    auto number = [&](const wchar_t* label, double value, std::function<bool(Entity&, double)> set) {
        text(label, Eng().Len(value), [set](Entity& x, const std::wstring& t) {
            double v;
            return ReadNumber(t, v) && set(x, v);
        });
    };
    auto choice = [&](const wchar_t* label, std::vector<std::wstring> choices, int now, std::function<bool(Entity&, int)> set) {
        Row r;
        r.kind = Row::Choice;
        r.label = label;
        r.choices = std::move(choices);
        r.choice = now;
        add(r, [set](Entity& x, const Row& ed) { return ed.choice >= 0 && set(x, ed.choice); });
    };
    auto info = [&](const wchar_t* label, const std::wstring& value) {
        Row r;
        r.label = label;
        r.value = value;
        rows.push_back(r);
    };
    if (!e.annot) return;
    const Annot& a = *e.annot;
    if (e.type == EntityType::Text || e.type == EntityType::Leader) {
        const TextData& t = a.text;
        if (!t.tag.empty()) {
            text(L"Tag", t.tag, [](Entity& x, const std::wstring& v) { return !v.empty() && (EditAnnot(x).text.tag = v, true); });
            text(L"Question", t.prompt, [](Entity& x, const std::wstring& v) { return EditAnnot(x).text.prompt = v, true; });
        }
        text(t.tag.empty() ? L"Text (\\P: new line)" : L"Default value", OneLine(t.content),
             [](Entity& x, const std::wstring& v) { return EditAnnot(x).text.content = FromOneLine(v), true; });
        std::vector<std::wstring> fonts = CommonFonts();
        int f = -1;
        for (size_t i = 0; i < fonts.size(); ++i)
            if (fonts[i] == t.font) f = (int)i;
        if (f < 0) fonts.push_back(t.font), f = (int)fonts.size() - 1;
        choice(L"Font", fonts, f, [fonts](Entity& x, int i) { return EditAnnot(x).text.font = fonts[(size_t)i], true; });
        number(L"Height", t.height, [](Entity& x, double v) { return v > 0 && (EditAnnot(x).text.height = v, true); });
        if (e.type == EntityType::Text) {
            number(L"Rotation", ToDegrees(t.rotation), [](Entity& x, double v) { return EditAnnot(x).text.rotation = ToRadians(v), true; });
            choice(L"Justify",
                   {L"Top left", L"Top centre", L"Top right", L"Middle left", L"Middle centre", L"Middle right", L"Bottom left",
                    L"Bottom centre", L"Bottom right"},
                   t.justify, [](Entity& x, int i) { return EditAnnot(x).text.justify = i, true; });
        }
        number(L"Width factor", t.widthFactor, [](Entity& x, double v) { return v > 0 && (EditAnnot(x).text.widthFactor = v, true); });
        number(L"Line spacing", t.lineSpacing, [](Entity& x, double v) { return v > 0 && (EditAnnot(x).text.lineSpacing = v, true); });
        choice(L"Frame", {L"None", L"Box", L"Circle", L"Feature control frame", L"Datum", L"Surface texture", L"Weld"}, t.frame,
               [](Entity& x, int i) { return EditAnnot(x).text.frame = i, true; });
        if (e.type == EntityType::Leader)
            number(L"Arrow size", a.arrow, [](Entity& x, double v) { return v >= 0 && (EditAnnot(x).arrow = v, true); });
        else
            AnnotPointRows(rows, e, L"X", L"Y", e.p0, [](Entity& x, Vec2 p) { Translate(x, p - x.p0); });
    } else if (e.type == EntityType::Hatch) {
        const HatchData& h = a.hatch;
        std::vector<std::wstring> names;
        int now = -1;
        for (const HatchPattern& p : HatchPatterns()) {
            if (SameName(p.name, h.pattern)) now = (int)names.size();
            names.push_back(p.name);
        }
        choice(L"Pattern", names, now, [names](Entity& x, int i) { return EditAnnot(x).hatch.pattern = names[(size_t)i], true; });
        if (!IsSolid(h)) {
            number(L"Scale", h.scale, [](Entity& x, double v) { return v > 0 && (EditAnnot(x).hatch.scale = v, true); });
            number(L"Angle", ToDegrees(h.angle), [](Entity& x, double v) { return EditAnnot(x).hatch.angle = ToRadians(v), true; });
        }
        info(L"Area", Eng().Len(HatchArea(h)));
        info(L"Outlines", std::to_wstring(h.loops.size()));
        info(L"Associative", h.refs.empty() ? L"No" : L"Yes (follows its outline)");
    } else if (e.type == EntityType::Viewport) {
        const Document& model = Cur().doc;
        const double mm = MmPerUnit(model.Settings().units);
        const double ratio = ViewportRatio(e, model);
        std::vector<std::wstring> scales;
        int now = -1;
        for (double r : StandardScales()) {
            if (std::fabs(r - ratio) < 1e-9 * r) now = (int)scales.size();
            scales.push_back(ScaleText(r));
        }
        if (now < 0) scales.push_back(ScaleText(ratio)), now = (int)scales.size() - 1;
        choice(L"Scale", scales, now, [mm](Entity& x, int i) {
            if ((size_t)i >= StandardScales().size() || x.annot->viewLocked) return false;
            EditAnnot(x).viewScale = StandardScales()[(size_t)i] * mm;
            return true;
        });
        info(L"Size on paper", Eng().Len(e.p1.x - e.p0.x) + L" x " + Eng().Len(e.p1.y - e.p0.y) + L" mm");
        number(L"View centre X", a.viewCentre.x, [](Entity& x, double v) { return !x.annot->viewLocked && (EditAnnot(x).viewCentre.x = v, true); });
        number(L"View centre Y", a.viewCentre.y, [](Entity& x, double v) { return !x.annot->viewLocked && (EditAnnot(x).viewCentre.y = v, true); });
        Row lock;
        lock.kind = Row::Check;
        lock.label = L"Locked";
        lock.checked = a.viewLocked;
        add(lock, [](Entity& x, const Row& ed) { return EditAnnot(x).viewLocked = ed.checked, true; });
    } else if (e.type == EntityType::Insert && a.block) {
        info(L"Block", a.block->name);
        AnnotPointRows(rows, e, L"Position X", L"Position Y", e.p0, [](Entity& x, Vec2 p) { Translate(x, p - x.p0); });
        number(L"Scale", e.radius, [](Entity& x, double v) { return v > 0 && (x.radius = v, true); });
        number(L"Rotation", ToDegrees(e.startAngle), [](Entity& x, double v) { return x.startAngle = ToRadians(v), true; });
        Row m;
        m.kind = Row::Check;
        m.label = L"Mirrored";
        m.checked = e.closed;
        add(m, [](Entity& x, const Row& ed) { return x.closed = ed.checked, true; });
        const std::vector<const TextData*> defs = AttributeDefs(*a.block);
        if (!defs.empty()) {
            Row h;
            h.heading = true;
            h.label = L"Attributes";
            rows.push_back(h);
        }
        for (size_t i = 0; i < defs.size(); ++i) {
            const std::wstring v = i < a.values.size() ? a.values[i] : defs[i]->content;
            text(defs[i]->tag.c_str(), v, [i, n = defs.size()](Entity& x, const std::wstring& nv) {
                Annot& an = EditAnnot(x);
                if (an.values.size() < n) an.values.resize(n);
                an.values[i] = nv;
                return true;
            });
        }
    }
}

void MainWindow::AnnotPointRows(std::vector<PropertiesPanel::Row>& rows, const Entity& e, const wchar_t* lx, const wchar_t* ly, Vec2 p,
                           std::function<void(Entity&, Vec2)> set) {
    using Row = PropertiesPanel::Row;
    const uint64_t id = e.id;
    const Vec2 org = Doc().Settings().origin;
    for (int k = 0; k < 2; ++k) {
        Row r;
        r.kind = Row::Text;
        r.label = k == 0 ? lx : ly;
        r.value = Eng().Len(k == 0 ? p.x - org.x : p.y - org.y);
        r.apply = [this, id, k, org, set](const Row& edited) {
            double v;
            if (const Entity* cur = Doc().Find(id); cur && ReadNumber(edited.value, v)) {
                Entity x = *cur;
                Vec2 q = x.p0;
                (k == 0 ? q.x : q.y) = v + (k == 0 ? org.x : org.y);
                set(x, q);
                Doc().BeginChange(L"Properties");
                Doc().Replace(x);
                Doc().EndChange();
            }
            AfterInput();
            PostMessageW(m_hwnd, WM_APP_PROPERTIES, 0, 0);
        };
        rows.push_back(r);
    }
}

void MainWindow::UpdateProperties(bool force) {
    if (!m_ready || !m_settings.propertiesOn) return;
    const PropsKey key{m_active, Doc().Revision(), Eng().SelectionRevision()};
    if (!force && (key == m_propsShown || m_props.Editing())) return;
    m_propsShown = key;
    using Row = PropertiesPanel::Row;
    std::vector<Row> rows;
    const auto refresh = [this] { PostMessageW(m_hwnd, WM_APP_PROPERTIES, 0, 0); };
    const Vec2 org = Doc().Settings().origin;

    std::vector<const Entity*> sel;
    for (uint64_t id : Eng().Selection())
        if (const Entity* e = Doc().Find(id)) sel.push_back(e);

    // The style rows: of the selection, or for new objects.
    int layer = Doc().Current().layer;
    int32_t color = Doc().Current().color;
    int lt = Doc().Current().linetype, lw = Doc().Current().lineweight;
    double lts = Doc().Current().ltscale;
    bool sameLayer = true, sameColor = true, sameLt = true, sameLw = true, sameLts = true, allCons = true;
    if (!sel.empty()) {
        layer = sel[0]->layer, color = sel[0]->color, lt = sel[0]->linetype, lw = sel[0]->lineweight, lts = sel[0]->ltscale;
        for (const Entity* e : sel) {
            sameLayer &= e->layer == layer;
            sameColor &= e->color == color;
            sameLt &= e->linetype == lt;
            sameLw &= e->lineweight == lw;
            sameLts &= e->ltscale == lts;
            allCons &= e->construction;
        }
    }
    const bool forNew = sel.empty();
    auto setStyle = [this, forNew, refresh](const wchar_t* step, const std::function<void(Entity&)>& onObject,
                                            const std::function<void(CurrentStyle&)>& onCurrent) {
        if (forNew) {
            CurrentStyle c = Doc().Current();
            onCurrent(c);
            Doc().SetCurrent(c);
        } else {
            ChangeSelected(step, onObject);
        }
        AfterInput();
        refresh();
    };

    std::wstring title;
    if (forNew) {
        title = L"No selection: for new objects";
    } else if (sel.size() == 1) {
        title = TypeName(sel[0]->type);
    } else {
        std::map<std::wstring, int> counts;
        for (const Entity* e : sel) ++counts[TypeName(e->type)];
        title = std::to_wstring(sel.size()) + L" objects (";
        bool first = true;
        for (const auto& kv : counts) {
            title += (first ? L"" : L", ") + std::to_wstring(kv.second) + L" " + kv.first;
            first = false;
        }
        title += L")";
    }

    {
        Row r;
        r.kind = Row::Choice;
        r.label = L"Layer";
        for (const Layer& l : Doc().Layers()) {
            r.choices.push_back(l.name);
            if (sameLayer && l.id == layer) r.choice = (int)r.choices.size() - 1;
        }
        r.apply = [this, setStyle](const Row& e) {
            if (e.choice < 0 || (size_t)e.choice >= Doc().Layers().size()) return;
            const int id = Doc().Layers()[(size_t)e.choice].id;
            setStyle(L"Change layer", [id](Entity& x) { x.layer = id; }, [id](CurrentStyle& c) { c.layer = id; });
        };
        rows.push_back(r);
    }
    {
        Row r;
        r.kind = Row::Choice;
        r.label = L"Colour";
        const std::vector<int32_t> list = ColorChoices();
        for (int32_t c : list) {
            r.choices.push_back(ColorName(c));
            if (sameColor && c == color) r.choice = (int)r.choices.size() - 1;
        }
        // A colour not in the list (a true colour) is shown too.
        if (sameColor && r.choice < 0) {
            r.choices.push_back(ColorName(color));
            r.choice = (int)r.choices.size() - 1;
        }
        r.choices.push_back(L"Other colour...");
        r.apply = [this, setStyle, list, color](const Row& e) {
            int32_t c;
            if (e.choice == (int)e.choices.size() - 1) {  // Other...
                c = color;
                if (!PickColor(m_hwnd, c, true)) {
                    PostMessageW(m_hwnd, WM_APP_PROPERTIES, 0, 0);
                    return;
                }
            } else if (e.choice >= 0 && (size_t)e.choice < list.size()) {
                c = list[(size_t)e.choice];
            } else {
                return;
            }
            setStyle(L"Change colour", [c](Entity& x) { x.color = c; }, [c](CurrentStyle& s) { s.color = c; });
        };
        rows.push_back(r);
    }
    {
        Row r;
        r.kind = Row::Choice;
        r.label = L"Line type";
        const std::vector<int> list = LineTypeChoices();
        for (int v : list) {
            r.choices.push_back(LineTypeName(v));
            if (sameLt && v == lt) r.choice = (int)r.choices.size() - 1;
        }
        r.apply = [setStyle, list](const Row& e) {
            if (e.choice < 0) return;
            const int v = list[(size_t)e.choice];
            setStyle(L"Change line type", [v](Entity& x) { x.linetype = (int16_t)v; }, [v](CurrentStyle& s) { s.linetype = v; });
        };
        rows.push_back(r);
    }
    {
        Row r;
        r.kind = Row::Text;
        r.label = L"Line type scale";
        r.value = sameLts ? FormatDecimal(lts, 3) : L"";
        r.apply = [this, setStyle](const Row& e) {
            double v;
            if (!ReadNumber(e.value, v) || v <= 0) {
                Message(L"The line type scale must be a positive number.");
                PostMessageW(m_hwnd, WM_APP_PROPERTIES, 0, 0);
                return;
            }
            setStyle(L"Change line type scale", [v](Entity& x) { x.ltscale = (float)v; }, [v](CurrentStyle& s) { s.ltscale = v; });
        };
        rows.push_back(r);
    }
    {
        Row r;
        r.kind = Row::Choice;
        r.label = L"Thickness";
        const std::vector<int> list = WeightChoices();
        for (int v : list) {
            r.choices.push_back(LineWeightName(v));
            if (sameLw && v == lw) r.choice = (int)r.choices.size() - 1;
        }
        r.apply = [setStyle, list](const Row& e) {
            if (e.choice < 0) return;
            const int v = list[(size_t)e.choice];
            setStyle(L"Change line weight", [v](Entity& x) { x.lineweight = (int16_t)v; }, [v](CurrentStyle& s) { s.lineweight = v; });
        };
        rows.push_back(r);
    }
    if (!forNew) {
        Row r;
        r.kind = Row::Check;
        r.label = L"Construction geometry";
        r.checked = allCons;
        r.apply = [this, refresh](const Row& e) {
            const bool on = e.checked;
            ChangeSelected(L"Construction", [on](Entity& x) { x.construction = on; });
            AfterInput();
            refresh();
        };
        rows.push_back(r);
    }

    // The shape of one object, to read and change.
    if (sel.size() == 1) {
        const Entity& e = *sel[0];
        const uint64_t id = e.id;
        Row h;
        h.heading = true;
        h.label = L"Geometry";
        rows.push_back(h);
        auto info = [&](const wchar_t* label, const std::wstring& value) {
            Row r;
            r.label = label;
            r.value = value;
            rows.push_back(r);
        };
        // A number that changes the object through `set` (false: not possible).
        auto number = [&](const wchar_t* label, double value, std::function<bool(Entity&, double)> set) {
            Row r;
            r.kind = Row::Text;
            r.label = label;
            r.value = Eng().Len(value);
            r.apply = [this, id, set, refresh](const Row& edited) {
                double v;
                const Entity* cur = Doc().Find(id);
                if (!cur) return;
                Entity x = *cur;
                if (!ReadNumber(edited.value, v) || !set(x, v)) {
                    Message(L"That value cannot be used here.");
                } else {
                    Doc().BeginChange(L"Properties");
                    Doc().Replace(x);
                    Doc().EndChange();
                }
                AfterInput();
                refresh();
            };
            rows.push_back(r);
        };
        auto pointRows = [&](const wchar_t* lx, const wchar_t* ly, Vec2 p, std::function<void(Entity&, Vec2)> set) {
            number(lx, p.x - org.x, [org, p, set](Entity& x, double v) { set(x, {v + org.x, p.y}); return true; });
            number(ly, p.y - org.y, [org, p, set](Entity& x, double v) { set(x, {p.x, v + org.y}); return true; });
        };
        switch (e.type) {
            case EntityType::Line: {
                pointRows(L"Start X", L"Start Y", e.p0, [](Entity& x, Vec2 p) { x.p0 = p; });
                pointRows(L"End X", L"End Y", e.p1, [](Entity& x, Vec2 p) { x.p1 = p; });
                number(L"Length", Dist(e.p0, e.p1), [](Entity& x, double v) {
                    if (v <= 0) return false;
                    x.p1 = x.p0 + Normalized(x.p1 - x.p0) * v;
                    return true;
                });
                number(L"Angle", ToDegrees(NormalizeAngle(AngleOf(e.p1 - e.p0))), [](Entity& x, double v) {
                    x.p1 = Polar(x.p0, Dist(x.p0, x.p1), ToRadians(v));
                    return true;
                });
                break;
            }
            case EntityType::Circle:
            case EntityType::Arc: {
                pointRows(L"Centre X", L"Centre Y", e.p0, [](Entity& x, Vec2 p) { x.p0 = p; });
                number(L"Radius", e.radius, [](Entity& x, double v) { return v > 0 ? (x.radius = v, true) : false; });
                number(L"Diameter", 2 * e.radius, [](Entity& x, double v) { return v > 0 ? (x.radius = v / 2, true) : false; });
                if (e.type == EntityType::Arc) {
                    number(L"Start angle", ToDegrees(e.startAngle), [](Entity& x, double v) {
                        x.startAngle = NormalizeAngle(ToRadians(v));
                        return true;
                    });
                    number(L"End angle", ToDegrees(e.endAngle), [](Entity& x, double v) {
                        x.endAngle = NormalizeAngle(ToRadians(v));
                        return true;
                    });
                    info(L"Arc length", Eng().Len(CurveLength(e)));
                } else {
                    info(L"Circumference", Eng().Len(kTau * e.radius));
                    info(L"Area", Eng().Len(kPi * e.radius * e.radius));
                }
                break;
            }
            case EntityType::Polyline: {
                info(L"Vertices", std::to_wstring(e.verts.size()));
                info(L"Length", Eng().Len(CurveLength(e)));
                double area;
                if (AreaOf(e, area)) info(L"Area", Eng().Len(area));
                Row r;
                r.kind = Row::Check;
                r.label = L"Closed";
                r.checked = e.closed;
                r.apply = [this, id, refresh](const Row& edited) {
                    if (const Entity* cur = Doc().Find(id)) {
                        Entity x = *cur;
                        x.closed = edited.checked;
                        Doc().BeginChange(L"Properties");
                        Doc().Replace(x);
                        Doc().EndChange();
                    }
                    AfterInput();
                    refresh();
                };
                rows.push_back(r);
                break;
            }
            case EntityType::Ellipse: {
                pointRows(L"Centre X", L"Centre Y", e.p0, [](Entity& x, Vec2 p) { x.p0 = p; });
                number(L"Major radius", Length(e.p1), [](Entity& x, double v) {
                    if (v <= 0) return false;
                    const double minor = Length(x.p1) * x.radius;
                    x.p1 = Normalized(x.p1) * v;
                    x.radius = minor / v;
                    return x.radius <= 1;
                });
                number(L"Minor radius", Length(e.p1) * e.radius, [](Entity& x, double v) {
                    if (v <= 0 || v > Length(x.p1)) return false;
                    x.radius = v / Length(x.p1);
                    return true;
                });
                info(L"Rotation", Eng().Ang(NormalizeAngle(AngleOf(e.p1))) + L"\x00B0");
                break;
            }
            case EntityType::Spline:
                info(L"Fit points", std::to_wstring(e.verts.size()));
                info(L"Length", Eng().Len(CurveLength(e)));
                break;
            case EntityType::Point:
                pointRows(L"X", L"Y", e.p0, [](Entity& x, Vec2 p) { x.p0 = p; });
                break;
            case EntityType::Dimension: {
                if (!e.dim) break;
                const DimData& d = *e.dim;
                info(L"Kind", DimKindName(d.kind));
                info(L"Measured", DimValueText(d));
                info(L"Attached", HasRefs(e) ? L"Yes (follows the objects)" : L"No");
                auto dimEdit = [&](const wchar_t* label, const std::wstring& value, std::function<bool(DimData&, const std::wstring&)> set) {
                    Row r;
                    r.kind = Row::Text;
                    r.label = label;
                    r.value = value;
                    r.apply = [this, id, set, refresh](const Row& edited) {
                        if (const Entity* cur = Doc().Find(id)) {
                            Entity x = *cur;
                            DimData nd = *x.dim;
                            if (set(nd, edited.value)) {
                                x.dim = std::make_shared<const DimData>(std::move(nd));
                                Doc().BeginChange(L"Dimension");
                                Doc().Replace(x);
                                Doc().EndChange();
                            } else {
                                Message(L"That value cannot be used here.");
                            }
                        }
                        AfterInput();
                        refresh();
                    };
                    rows.push_back(r);
                };
                dimEdit(L"Name", d.name, [](DimData& nd, const std::wstring& t) { return ValidName(t) && (nd.name = t, true); });
                if (HasRefs(e))
                    dimEdit(L"Formula", d.formula, [](DimData& nd, const std::wstring& t) {
                        nd.formula = t;
                        if (!t.empty()) nd.locked = true, nd.value = DimMeasure(nd);
                        return true;
                    });
                dimEdit(L"Text", d.text, [](DimData& nd, const std::wstring& t) { nd.text = t; return true; });
                dimEdit(L"Text height", Eng().Len(d.style.textHeight), [](DimData& nd, const std::wstring& t) {
                    double v;
                    return ReadNumber(t, v) && v > 0 && (nd.style.textHeight = v, true);
                });
                dimEdit(L"Arrow size", Eng().Len(d.style.arrowSize), [](DimData& nd, const std::wstring& t) {
                    double v;
                    return ReadNumber(t, v) && v >= 0 && (nd.style.arrowSize = v, true);
                });
                dimEdit(L"Decimal places", std::to_wstring(d.style.precision), [](DimData& nd, const std::wstring& t) {
                    double v;
                    return ReadNumber(t, v) && v >= 0 && v <= 8 && (nd.style.precision = (int)v, true);
                });
                Row r;
                r.kind = Row::Check;
                r.label = L"Driven (reports only)";
                r.checked = d.driven;
                r.apply = [this, id, refresh](const Row& edited) {
                    if (const Entity* cur = Doc().Find(id)) {
                        Entity x = *cur;
                        DimData nd = *x.dim;
                        nd.driven = edited.checked;
                        x.dim = std::make_shared<const DimData>(std::move(nd));
                        Doc().BeginChange(L"Dimension");
                        Doc().Replace(x);
                        Doc().EndChange();
                    }
                    AfterInput();
                    refresh();
                };
                rows.push_back(r);
                if (HasRefs(e)) {
                    Row lk;
                    lk.kind = Row::Check;
                    lk.label = L"Locked (holds its value)";
                    lk.checked = d.locked;
                    lk.apply = [this, id, refresh](const Row& edited) {
                        if (const Entity* cur = Doc().Find(id)) {
                            Entity x = *cur;
                            DimData nd = *x.dim;
                            nd.locked = edited.checked;
                            nd.value = DimMeasure(nd);
                            if (!nd.locked) nd.formula.clear();
                            x.dim = std::make_shared<const DimData>(std::move(nd));
                            Doc().BeginChange(L"Dimension");
                            Doc().Replace(x);
                            Doc().EndChange();
                        }
                        AfterInput();
                        refresh();
                    };
                    rows.push_back(lk);
                }
                break;
            }
            case EntityType::Relation: {
                if (!e.annot) break;
                const RelationData& rd = e.annot->rel;
                info(L"Relationship", RelKindName(rd.kind));
                std::wstring what;
                for (int i = 0; i < RelElementCount(rd.kind); ++i) {
                    const Entity* o = Doc().Find(rd.refs[(size_t)i].id);
                    const Element el = ElementOf(Doc(), rd.refs[(size_t)i]);
                    what += (i ? L", " : L"") + std::wstring(o ? TypeName(o->type) : L"?") +
                            (el.kind == ElemKind::Point ? L" point" : L"");
                }
                info(L"Between", what);
                info(L"Kept", Doc().Settings().maintainRelations ? L"Yes" : L"No (Relate > Maintain relationships)");
                break;
            }
            case EntityType::Text:
            case EntityType::Leader:
            case EntityType::Hatch:
            case EntityType::Insert:
            case EntityType::Viewport:
                AnnotationRows(e, rows);
                break;
            case EntityType::Array:
                info(L"Kind", e.array && e.array->kind == ArrayData::Rect    ? L"Rectangular"
                              : e.array && e.array->kind == ArrayData::Polar ? L"Polar"
                                                                             : L"Path");
                info(L"Copies", e.array ? std::to_wstring(ArrayCopyCount(*e.array)) : L"");
                break;
        }
    }
    m_props.SetContent(title, std::move(rows));
}

void MainWindow::ApplyTheme(int mode) {
    m_settings.themeMode = mode;
    ReloadTheme((ThemeMode)mode);
    ApplyWindowTheme(m_hwnd);
    m_menuBar.OnThemeChanged();
    m_tools.OnThemeChanged();
    m_tabs.OnThemeChanged();
    m_guide.OnThemeChanged();
    m_props.OnThemeChanged();
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
            if (LOWORD(wp) == ID_LAYER_COMBO) {
                if (HIWORD(wp) == CBN_SELCHANGE) {
                    const int i = (int)SendMessageW(m_layerCombo, CB_GETCURSEL, 0, 0);
                    if (i >= 0 && (size_t)i < Doc().Layers().size()) {
                        const int id = Doc().Layers()[(size_t)i].id;
                        if (!Eng().Selection().empty() && !Eng().Active()) {
                            ChangeSelected(L"Change layer", [&](Entity& e) { e.layer = id; });
                        } else {
                            CurrentStyle c = Doc().Current();
                            c.layer = id;
                            Doc().SetCurrent(c);
                        }
                    }
                    AfterInput();
                    FocusCommandLine();
                }
                return 0;
            }
            OnCommand(LOWORD(wp));
            return 0;
        case WM_APP_PROPERTIES:
            UpdateProperties(true);
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
            EditObject(*(const uint64_t*)lp);
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
            m_guide.OnDpiChanged();
            m_props.OnDpiChanged();
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
