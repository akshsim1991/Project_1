// MainWindow.cpp - window, layout, documents, undo and fitting. Menus,
// data editing, tools and export are in MainActions.cpp.
#include "MainWindow.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include "Dialogs.h"
#include "NumFormat.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr UINT_PTR kFitTimer = 1;
constexpr size_t kMaxUndo = 100;
constexpr int kSplitDip = 6;

std::wstring FileName(const std::wstring& path) {
    const size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? path : path.substr(s + 1);
}

bool EndsWithNoCase(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}
}  // namespace

// ===========================================================================
// Creation
// ===========================================================================
bool MainWindow::Create(HINSTANCE inst, int showCmd, const std::wstring& openPath) {
    m_inst = inst;
    ReloadTheme((ThemeMode)m_settings.themeMode);
    m_doc.table.names = {L"x", L"y"};

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = APP_WINDOW_CLASS;
    if (!RegisterClassExW(&wc)) return false;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int dpi = GetWindowDpi(nullptr);
    const int w = std::min(Dpi(1360, dpi), (int)(work.right - work.left) * 9 / 10);
    const int h = std::min(Dpi(860, dpi), (int)(work.bottom - work.top) * 9 / 10);
    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, APP_WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    ApplyWindowTheme(m_hwnd);
    CreateChildren();
    CreateAccelerators();

    if (m_settings.hasPlacement) {
        WINDOWPLACEMENT wp = m_settings.placement;
        wp.flags = 0;
        wp.showCmd = wp.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        if (showCmd == SW_SHOWMINIMIZED || showCmd == SW_MINIMIZE || showCmd == SW_SHOWMINNOACTIVE)
            wp.showCmd = (UINT)showCmd;
        SetWindowPlacement(m_hwnd, &wp);
    } else {
        ShowWindow(m_hwnd, showCmd);
    }
    ApplyTheme();
    UpdateWindow(m_hwnd);
    if (!openPath.empty()) OpenPath(openPath);
    ApplyDocument(true);
    SetFocus(m_grid.Hwnd());
    return true;
}

void MainWindow::CreateChildren() {
    // Row 1: file, model and tools.
    m_toolbar.Create(m_hwnd, m_hwnd, 44);
    m_toolbar.AddTextButton(ID_NEW, L"New", L"Start with an empty table (Ctrl+N)", 48);
    m_toolbar.AddTextButton(ID_OPEN, L"Open\x2026", L"Open a CSV, text or CurveForge file (Ctrl+O)", 64);
    m_toolbar.AddTextButton(ID_SAVE, L"Save", L"Save the project: data, model and settings (Ctrl+S)", 50);
    m_toolbar.AddSeparator();
    m_toolbar.AddLabel(ID_MODEL_MENU, 250, true, L"Choose the model to fit (Ctrl+M)");
    m_toolbar.AddLabel(ID_ORDER_MENU, 104, true, L"Polynomial degree or number of peaks");
    m_toolbar.AddTextButton(ID_FIT, L"Fit", L"Fit the model now (F5)", 46);
    m_toolbar.AddTextButton(ID_BEST_FIT, L"Best fit\x2026", L"Try every model and rank them (Ctrl+B)", 76);
    m_toolbar.AddSeparator();
    m_toolbar.AddTextButton(ID_PARAMS, L"Parameters\x2026", L"Starting values, fixed values and limits (Ctrl+P)", 96);
    m_toolbar.AddTextButton(ID_PREDICT, L"Predict\x2026", L"y for x, x for y, slope, area and tables of values (Ctrl+R)", 74);
    m_toolbar.AddTextButton(ID_BATCH, L"Batch\x2026", L"Fit many columns or files at once", 64);
    m_toolbar.AddSpacer();
    m_toolbar.AddTextButton(ID_EXPORT_MENU, L"Export \x25BE", L"Save or copy the results, graph and report", 74);
    m_toolbar.AddTextButton(ID_MORE_MENU, L"More", L"Examples, recent files, settings and About", 52);

    // Row 2: what is fitted, the equation and the graph options.
    m_setup.Create(m_hwnd, m_hwnd, 40);
    m_setup.AddLabel(ID_X_MENU, 130, true, L"The column used as x");
    m_setup.AddLabel(ID_Y_MENU, 130, true, L"The column used as y");
    m_setup.AddLabel(ID_SIGMA_MENU, 140, true, L"Optional column with the uncertainty (\x03C3) of each y: points are weighted by 1/\x03C3\xB2");
    m_setup.AddSeparator();
    m_equation = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                 m_setup.Hwnd(), (HMENU)(INT_PTR)ID_EQUATION_EDIT, m_inst, nullptr);
    SendMessageW(m_equation, EM_SETCUEBANNER, TRUE, (LPARAM)L"Type your own equation, e.g. y = a*exp(-b*x) + c");
    m_setup.AddChild(m_equation, -180);
    m_setup.AddSeparator();
    m_setup.AddTextButton(ID_ROBUST, L"Robust", L"Ignore outliers (robust fitting)", 58);
    m_setup.AddTextButton(ID_CONF_BAND, L"Confidence", L"Show the confidence band of the curve", 78);
    m_setup.AddTextButton(ID_PRED_BAND, L"Prediction", L"Show the prediction band (where new points are expected)", 74);
    m_setup.AddTextButton(ID_RESIDUALS, L"Residuals", L"Show the residual plot under the graph", 70);
    m_setup.AddTextButton(ID_LOG_X, L"Log x", L"Logarithmic x axis", 46);
    m_setup.AddTextButton(ID_LOG_Y, L"Log y", L"Logarithmic y axis", 46);

    m_grid.Create(m_hwnd, &m_doc);
    m_grid.onBeforeChange = [this] { BeginChange(); };
    m_grid.onChanged = [this] { Changed(false, true); };
    m_grid.onCursorMoved = [this] { UpdateStatus(); };
    m_grid.onHeaderMenu = [this](POINT pt, int col) { ShowHeaderMenu(pt, col); };
    m_grid.onContextMenu = [this](POINT pt) { ShowTableMenu(pt); };

    m_chart.Create(m_hwnd);
    m_chart.onTogglePoint = [this](int i) { TogglePoint(i); };
    m_chart.onHover = [this](const std::wstring& t) {
        m_hoverText = t;
        UpdateStatus();
    };
    m_chart.onContextMenu = [this](POINT pt) { ShowChartMenu(pt); };

    m_results = CreateWindowExW(0, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                                    ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                0, 0, 0, 0, m_hwnd, nullptr, m_inst, nullptr);

    m_status.Create(m_hwnd, m_hwnd, 28);
    m_status.SetBorderTop(true);
    m_status.AddLabel(ID_STATUS_TEXT, 520, false, nullptr);
    m_status.AddSpacer();
    m_status.AddLabel(ID_STATUS_HOVER, 640, false, nullptr);
    m_status.SetRightAligned(ID_STATUS_HOVER, true);
}

void MainWindow::CreateAccelerators() {
    ACCEL acc[] = {
        {FCONTROL | FVIRTKEY, 'N', ID_NEW},
        {FCONTROL | FVIRTKEY, 'O', ID_OPEN},
        {FCONTROL | FVIRTKEY, 'S', ID_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_SAVE_AS},
        {FCONTROL | FVIRTKEY, 'Z', ID_UNDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_REDO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'Z', ID_REDO},
        {FCONTROL | FVIRTKEY, 'C', ID_COPY},
        {FCONTROL | FVIRTKEY, 'X', ID_CUT},
        {FCONTROL | FVIRTKEY, 'V', ID_PASTE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'V', ID_PASTE_NEW},
        {FCONTROL | FVIRTKEY, 'A', ID_SELECT_ALL},
        {FVIRTKEY, VK_INSERT, ID_INSERT_ROW},
        {FCONTROL | FVIRTKEY, VK_DELETE, ID_DELETE_ROWS},
        {FCONTROL | FVIRTKEY, 'E', ID_EXCLUDE_ROWS},
        {FVIRTKEY, VK_F5, ID_FIT},
        {FCONTROL | FVIRTKEY, 'B', ID_BEST_FIT},
        {FCONTROL | FVIRTKEY, 'P', ID_PARAMS},
        {FCONTROL | FVIRTKEY, 'R', ID_PREDICT},
        {FCONTROL | FVIRTKEY, 'M', ID_MODEL_MENU},
        {FCONTROL | FSHIFT | FVIRTKEY, 'E', ID_EXPORT_CSV},
        {FCONTROL | FSHIFT | FVIRTKEY, 'R', ID_REPORT},
        {FCONTROL | FSHIFT | FVIRTKEY, 'C', ID_COPY_CHART},
        {FCONTROL | FVIRTKEY, '0', ID_RESET_VIEW},
        {FCONTROL | FVIRTKEY, '1', ID_FOCUS_TABLE},
        {FCONTROL | FVIRTKEY, '2', ID_FOCUS_EQUATION},
        {FCONTROL | FVIRTKEY, VK_OEM_COMMA, ID_SETTINGS},
        {FVIRTKEY, VK_F1, ID_ABOUT},
    };
    m_accel = CreateAcceleratorTableW(acc, (int)(sizeof(acc) / sizeof(acc[0])));
}

RECT MainWindow::ContentRect() const {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    rc.top += m_toolbar.Height() + m_setup.Height();
    rc.bottom -= m_status.Height();
    return rc;
}

void MainWindow::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int th = m_toolbar.Height(), sh = m_setup.Height(), bh = m_status.Height();
    MoveWindow(m_toolbar.Hwnd(), 0, 0, rc.right, th, TRUE);
    MoveWindow(m_setup.Hwnd(), 0, th, rc.right, sh, TRUE);
    MoveWindow(m_status.Hwnd(), 0, rc.bottom - bh, rc.right, bh, TRUE);
    const RECT c = ContentRect();
    const int split = Dpi(kSplitDip, m_dpi);
    const int width = c.right - c.left, height = std::max(0L, c.bottom - c.top);
    int tableW = std::min(Dpi(m_settings.tableWidthDip, m_dpi), std::max(Dpi(120, m_dpi), width - Dpi(300, m_dpi)));
    tableW = std::max(tableW, Dpi(100, m_dpi));
    int resultsH = std::min(Dpi(m_settings.resultsHeightDip, m_dpi), std::max(Dpi(60, m_dpi), height - Dpi(200, m_dpi)));
    resultsH = std::max(resultsH, Dpi(50, m_dpi));
    MoveWindow(m_grid.Hwnd(), c.left, c.top, tableW, height, TRUE);
    const int rx = c.left + tableW + split, rw = std::max(0, width - tableW - split);
    MoveWindow(m_chart.Hwnd(), rx, c.top, rw, std::max(0, height - resultsH - split), TRUE);
    MoveWindow(m_results, rx, c.bottom - resultsH, rw, resultsH, TRUE);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

int MainWindow::Splitter(POINT pt) const {
    RECT g, ch;
    GetWindowRect(m_grid.Hwnd(), &g);
    GetWindowRect(m_chart.Hwnd(), &ch);
    MapWindowPoints(nullptr, m_hwnd, (POINT*)&g, 2);
    MapWindowPoints(nullptr, m_hwnd, (POINT*)&ch, 2);
    const RECT c = ContentRect();
    if (pt.y < c.top || pt.y >= c.bottom) return 0;
    if (pt.x >= g.right && pt.x < ch.left) return 1;
    if (pt.x >= ch.left && pt.y >= ch.bottom) return 2;
    return 0;
}

void MainWindow::UpdateTitle() {
    std::wstring name = m_path.empty() ? L"Untitled" : FileName(m_path);
    std::wstring title = name + (m_modified ? L" \x2022" : L"") + L" \x2014 " APP_NAME;
    SetWindowTextW(m_hwnd, title.c_str());
}

// ===========================================================================
// Window procedure
// ===========================================================================
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
        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp), (HWND)lp);
            return 0;
        case WM_NOTIFY: {
            LRESULT result = 0;
            if (m_grid.OnNotify((NMHDR*)lp, result)) return result;
            break;
        }
        case WM_TIMER:
            if (wp == kFitTimer) {
                KillTimer(m_hwnd, kFitTimer);
                RunFit();
            }
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            HBRUSH b = CreateSolidBrush(CurrentTheme().barBg);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            if ((HWND)lp == m_results) {
                const Theme& th = CurrentTheme();
                SetTextColor((HDC)wp, th.listText);
                SetBkColor((HDC)wp, th.listBg);
                if (m_bgBrush) DeleteObject(m_bgBrush);
                m_bgBrush = CreateSolidBrush(th.listBg);
                return (LRESULT)m_bgBrush;
            }
            break;
        case WM_SETCURSOR:
            if ((HWND)wp == m_hwnd && LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);
                const int s = Splitter(pt);
                if (s) {
                    SetCursor(LoadCursorW(nullptr, s == 1 ? IDC_SIZEWE : IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN: {
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            m_dragSplitter = Splitter(pt);
            if (m_dragSplitter) SetCapture(m_hwnd);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (m_dragSplitter && GetCapture() == m_hwnd) {
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                const RECT c = ContentRect();
                if (m_dragSplitter == 1)
                    m_settings.tableWidthDip = std::max(120, MulDiv(pt.x - c.left, 96, m_dpi));
                else
                    m_settings.resultsHeightDip = std::max(60, MulDiv(c.bottom - pt.y, 96, m_dpi));
                Layout();
            }
            return 0;
        case WM_LBUTTONUP:
            if (m_dragSplitter) {
                m_dragSplitter = 0;
                ReleaseCapture();
            }
            return 0;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t path[MAX_PATH * 4];
            const bool got = DragQueryFileW(drop, 0, path, (UINT)(sizeof(path) / sizeof(path[0]))) > 0;
            DragFinish(drop);
            if (got && ConfirmDiscard()) OpenPath(path);
            return 0;
        }
        case WM_SETFOCUS:
            SetFocus(m_grid.Hwnd());
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lp)->ptMinTrackSize = {Dpi(900, m_dpi), Dpi(520, m_dpi)};
            return 0;
        case WM_DPICHANGED: {
            m_dpi = HIWORD(wp);
            m_toolbar.OnDpiChanged();
            m_setup.OnDpiChanged();
            m_status.OnDpiChanged();
            m_grid.OnDpiChanged();
            ApplyTheme();
            const RECT* r = (const RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && lstrcmpW((const wchar_t*)lp, L"ImmersiveColorSet") == 0 && m_settings.themeMode == 0) ApplyTheme();
            break;
        case WM_CLOSE: {
            if (!ConfirmDiscard()) return 0;
            WINDOWPLACEMENT wp{sizeof(wp)};
            GetWindowPlacement(m_hwnd, &wp);
            if (wp.showCmd == SW_SHOWMINIMIZED)
                wp.showCmd = (wp.flags & WPF_RESTORETOMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            m_settings.placement = wp;
            m_settings.hasPlacement = true;
            m_settings.Save();
            DestroyWindow(m_hwnd);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(m_hwnd, kFitTimer);
            if (m_accel) DestroyAcceleratorTable(m_accel);
            m_accel = nullptr;
            if (m_monoFont) DeleteObject(m_monoFont);
            if (m_bgBrush) DeleteObject(m_bgBrush);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ===========================================================================
// Commands
// ===========================================================================
void MainWindow::OnCommand(int id, int code, HWND ctl) {
    if (ctl && ctl == m_equation) {
        if (code == EN_SETFOCUS) m_equationUndoTaken = false;
        if (code == EN_CHANGE && !m_settingEquation) {
            if (!m_equationUndoTaken) {
                BeginChange();
                m_equationUndoTaken = true;
            }
            m_doc.model = ModelKind::Custom;
            m_doc.customEquation = GetWindowString(m_equation);
            m_modified = true;
            UpdateTitle();
            UpdateUi();
            ScheduleFit(false);
        }
        return;
    }
    if (id >= ID_MODEL_FIRST && id < ID_MODEL_FIRST + (int)ModelKind::Count) {
        const ModelKind k = (ModelKind)(id - ID_MODEL_FIRST);
        SetModel(k, Models::Info(k).hasOrder ? std::max(m_doc.order, Models::MinOrder(k)) : m_doc.order);
        return;
    }
    if (id >= ID_ORDER_FIRST && id < ID_ORDER_FIRST + 20) {
        SetModel(m_doc.model, id - ID_ORDER_FIRST);
        return;
    }
    if (id >= ID_EXAMPLE_FIRST && id < ID_EXAMPLE_FIRST + 20) {
        if (ConfirmDiscard()) LoadExample(id - ID_EXAMPLE_FIRST);
        return;
    }
    if (id >= ID_COL_X_FIRST && id < ID_COL_X_FIRST + 200) return SetRole(0, id - ID_COL_X_FIRST);
    if (id >= ID_COL_Y_FIRST && id < ID_COL_Y_FIRST + 199) return SetRole(1, id - ID_COL_Y_FIRST);
    if (id == ID_COL_SIGMA_NONE) return SetRole(2, -1);
    if (id >= ID_COL_SIGMA_FIRST && id < ID_COL_SIGMA_FIRST + 200) return SetRole(2, id - ID_COL_SIGMA_FIRST);
    if (id >= ID_RECENT_FIRST && id < ID_RECENT_FIRST + 10) {
        const size_t i = (size_t)(id - ID_RECENT_FIRST);
        if (i < m_settings.recent.size() && ConfirmDiscard()) {
            const std::wstring p = m_settings.recent[i];
            OpenPath(p);
        }
        return;
    }

    // Edit boxes keep their own clipboard and undo keys.
    HWND focus = GetFocus();
    wchar_t cls[16] = L"";
    if (focus) GetClassNameW(focus, cls, 16);
    const bool inEdit = _wcsicmp(cls, L"Edit") == 0;
    if (inEdit) {
        switch (id) {
            case ID_COPY:
                if (focus == m_results) {
                    DWORD a = 0, b = 0;
                    SendMessageW(m_results, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
                    if (a == b) {
                        CopyResults();
                        return;
                    }
                }
                SendMessageW(focus, WM_COPY, 0, 0);
                return;
            case ID_CUT: SendMessageW(focus, WM_CUT, 0, 0); return;
            case ID_PASTE: SendMessageW(focus, WM_PASTE, 0, 0); return;
            case ID_SELECT_ALL: SendMessageW(focus, EM_SETSEL, 0, -1); return;
            case ID_UNDO:
                if (focus != m_equation || !m_equationUndoTaken) {
                    SendMessageW(focus, EM_UNDO, 0, 0);
                    return;
                }
                break;
            case ID_INSERT_ROW:
            case ID_DELETE_ROWS:
            case ID_EXCLUDE_ROWS: return;
        }
    }

    switch (id) {
        case IDOK:
            // Enter in the equation box: fit now.
            if (focus == m_equation) RunFit();
            break;
        case ID_NEW:
            if (ConfirmDiscard()) NewDocument();
            break;
        case ID_OPEN:
            if (ConfirmDiscard()) OpenDialog();
            break;
        case ID_SAVE: Save(false); break;
        case ID_SAVE_AS: Save(true); break;
        case ID_SAVE_TABLE_CSV: SaveTableCsv(); break;
        case ID_MODEL_MENU: ShowModelMenu(); break;
        case ID_ORDER_MENU: ShowOrderMenu(); break;
        case ID_FIT:
            if (m_grid.IsEditing()) m_grid.CommitEdit();
            RunFit();
            break;
        case ID_BEST_FIT: BestFit(); break;
        case ID_PARAMS: Parameters(); break;
        case ID_PREDICT: Predict(); break;
        case ID_BATCH: Batch(); break;
        case ID_X_MENU: ShowRoleMenu(0); break;
        case ID_Y_MENU: ShowRoleMenu(1); break;
        case ID_SIGMA_MENU: ShowRoleMenu(2); break;
        case ID_ROBUST:
            BeginChange();
            m_doc.robust = !m_doc.robust;
            Changed(false, false);
            break;
        case ID_CONF_BAND:
            m_settings.confidenceBand = !m_settings.confidenceBand;
            UpdateChart();
            UpdateUi();
            break;
        case ID_PRED_BAND:
            m_settings.predictionBand = !m_settings.predictionBand;
            UpdateChart();
            UpdateUi();
            break;
        case ID_RESIDUALS:
            m_settings.residuals = !m_settings.residuals;
            UpdateChart();
            UpdateUi();
            break;
        case ID_GRID_LINES:
            m_settings.gridLines = !m_settings.gridLines;
            UpdateChart();
            break;
        case ID_LOG_X:
        case ID_LOG_Y:
            BeginChange();
            if (id == ID_LOG_X) m_doc.logX = !m_doc.logX;
            else m_doc.logY = !m_doc.logY;
            m_modified = true;
            UpdateTitle();
            UpdateChart();
            UpdateUi();
            break;
        case ID_RESET_VIEW: m_chart.ResetView(); break;
        case ID_EXPORT_MENU: ShowExportMenu(); break;
        case ID_EXPORT_CSV: ExportFittedCsv(); break;
        case ID_EXPORT_PNG: ExportChart(false); break;
        case ID_EXPORT_SVG: ExportChart(true); break;
        case ID_COPY_CHART: {
            RECT rc;
            GetClientRect(m_chart.Hwnd(), &rc);
            const double f = 96.0 / m_dpi;
            if (m_chart.CopyToClipboard(m_hwnd, std::max(800, (int)(rc.right * f * 2)), std::max(500, (int)(rc.bottom * f * 2))))
                SetOp(L"Graph copied (paste it into Word, PowerPoint or an e-mail).");
            break;
        }
        case ID_COPY_RESULTS: CopyResults(); break;
        case ID_COPY_EQUATION:
        case ID_COPY_EXCEL: {
            const std::wstring eq = FittedEquation(id == ID_COPY_EXCEL);
            if (eq.empty()) break;
            CopyToClipboard(m_hwnd, eq);
            SetOp(id == ID_COPY_EXCEL ? L"Excel formula copied: x is cell A2." : L"Equation copied.");
            break;
        }
        case ID_REPORT: SaveReport(); break;
        case ID_UNDO: Undo(); break;
        case ID_REDO: Redo(); break;
        case ID_COPY:
            if (focus == m_chart.Hwnd()) OnCommand(ID_COPY_CHART, 0, nullptr);
            else if (focus == m_results) CopyResults();
            else CopyRows(false);
            break;
        case ID_CUT: CopyRows(true); break;
        case ID_PASTE: Paste(false); break;
        case ID_PASTE_NEW: Paste(true); break;
        case ID_SELECT_ALL:
            if (focus == m_results) SendMessageW(m_results, EM_SETSEL, 0, -1);
            else m_grid.SelectRows(0, m_doc.table.Rows() - 1);
            break;
        case ID_INSERT_ROW: InsertRow(); break;
        case ID_DELETE_ROWS: DeleteRows(); break;
        case ID_EXCLUDE_ROWS: ToggleExcluded(); break;
        case ID_INCLUDE_ALL: IncludeAll(); break;
        case ID_ADD_COLUMN: AddColumn(); break;
        case ID_FORMULA_COLUMN: FormulaColumn(); break;
        case ID_RENAME_COLUMN: RenameColumn(m_grid.CurrentCol()); break;
        case ID_DELETE_COLUMN: DeleteColumn(m_grid.CurrentCol()); break;
        case ID_SORT_ASC: SortRows(m_grid.CurrentCol(), true); break;
        case ID_SORT_DESC: SortRows(m_grid.CurrentCol(), false); break;
        case ID_SET_X: SetRole(0, m_grid.CurrentCol()); break;
        case ID_SET_Y: SetRole(1, m_grid.CurrentCol()); break;
        case ID_SET_SIGMA: SetRole(2, m_grid.CurrentCol()); break;
        case ID_CLEAR_SIGMA: SetRole(2, -1); break;
        case ID_MORE_MENU: ShowMoreMenu(); break;
        case ID_SETTINGS: ShowSettings(); break;
        case ID_SHORTCUTS: ShowShortcuts(); break;
        case ID_ABOUT: ShowAbout(); break;
        case ID_FOCUS_TABLE: SetFocus(m_grid.Hwnd()); break;
        case ID_FOCUS_EQUATION:
            SetFocus(m_equation);
            SendMessageW(m_equation, EM_SETSEL, 0, -1);
            break;
        case ID_EXIT: PostMessageW(m_hwnd, WM_CLOSE, 0, 0); break;
    }
}

// ===========================================================================
// State shown in the window
// ===========================================================================
void MainWindow::SetEquationText() {
    const std::wstring eq = m_doc.Equation();
    if (GetWindowString(m_equation) == eq) return;
    m_settingEquation = true;
    SetWindowTextW(m_equation, eq.c_str());
    m_settingEquation = false;
}

void MainWindow::UpdateUi() {
    const ModelInfo& info = Models::Info(m_doc.model);
    m_toolbar.SetText(ID_MODEL_MENU, std::wstring(L"Model: ") + info.name + L"  \x25BE");
    if (info.hasOrder)
        m_toolbar.SetText(ID_ORDER_MENU, std::wstring(m_doc.model == ModelKind::Polynomial ? L"Degree: " : L"Peaks: ") +
                                             std::to_wstring(m_doc.order) + L"  \x25BE");
    else
        m_toolbar.SetText(ID_ORDER_MENU, L"");
    m_toolbar.SetEnabled(ID_ORDER_MENU, info.hasOrder);
    const Table& t = m_doc.table;
    auto colName = [&](int c) { return c >= 0 && c < t.Cols() ? t.names[(size_t)c] : std::wstring(L"none"); };
    m_setup.SetText(ID_X_MENU, L"X: " + colName(m_doc.xCol) + L"  \x25BE");
    m_setup.SetText(ID_Y_MENU, L"Y: " + colName(m_doc.yCol) + L"  \x25BE");
    m_setup.SetText(ID_SIGMA_MENU, L"Errors (\x03C3): " + colName(m_doc.sigmaCol) + L"  \x25BE");
    m_setup.SetChecked(ID_SIGMA_MENU, m_doc.sigmaCol >= 0);
    m_setup.SetChecked(ID_ROBUST, m_doc.robust);
    m_setup.SetChecked(ID_CONF_BAND, m_settings.confidenceBand);
    m_setup.SetChecked(ID_PRED_BAND, m_settings.predictionBand);
    m_setup.SetChecked(ID_RESIDUALS, m_settings.residuals);
    m_setup.SetChecked(ID_LOG_X, m_doc.logX);
    m_setup.SetChecked(ID_LOG_Y, m_doc.logY);
    const bool spline = m_doc.model == ModelKind::Spline;
    m_setup.SetEnabled(ID_ROBUST, !spline);
    m_toolbar.SetEnabled(ID_PARAMS, !spline);
    m_toolbar.SetEnabled(ID_PREDICT, m_fit.ok);
    SetEquationText();
    UpdateStatus();
}

void MainWindow::UpdateStatus() {
    std::wstring text;
    int used = 0, left = 0;
    for (size_t i = 0; i < m_points.x.size(); ++i) (m_points.excluded[i] ? left : used)++;
    text = std::to_wstring(m_doc.table.Rows()) + L" rows  \x00B7  " + std::to_wstring(used) + L" points used";
    if (left) text += L"  \x00B7  " + std::to_wstring(left) + L" left out";
    if (m_fit.ok && !m_fit.spline) text += L"  \x00B7  R\xB2 = " + FormatNumber(m_fit.r.r2, 6);
    const auto sel = m_grid.SelectedRows();
    if (sel.size() > 1) text += L"  \x00B7  " + std::to_wstring(sel.size()) + L" rows selected";
    m_status.SetText(ID_STATUS_TEXT, text);
    m_status.SetText(ID_STATUS_HOVER, m_hoverText.empty() ? m_opText : m_hoverText);
}

void MainWindow::SetOp(const std::wstring& text) {
    m_opText = text;
    UpdateStatus();
}

// ===========================================================================
// Documents
// ===========================================================================
bool MainWindow::ConfirmDiscard() {
    if (m_grid.IsEditing()) m_grid.CommitEdit();
    if (!m_modified) return true;
    const std::wstring name = m_path.empty() ? L"Untitled" : FileName(m_path);
    const std::wstring msg = L"Save the changes to " + name + L"?";
    const int r = MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return Save(false);
    return true;
}

void MainWindow::NewDocument() {
    m_doc = Document{};
    m_doc.table.names = {L"x", L"y"};
    m_path.clear();
    m_isProject = false;
    m_modified = false;
    m_undo.clear();
    m_redo.clear();
    ApplyDocument(true);
    SetOp(L"New table: type values, paste from Excel (Ctrl+V) or open a file (Ctrl+O).");
}

void MainWindow::OpenDialog() {
    std::vector<std::wstring> files;
    if (!PickOpenFiles(m_hwnd,
                       L"Data and projects (*.csv;*.tsv;*.txt;*.dat;*.cforge)\0*.csv;*.tsv;*.txt;*.dat;*.prn;*.cforge\0"
                       L"CurveForge projects (*.cforge)\0*.cforge\0All files (*.*)\0*.*\0",
                       m_settings.lastFolder, files, false))
        return;
    OpenPath(files[0]);
}

bool MainWindow::OpenPath(const std::wstring& path) {
    std::wstring text;
    if (!Import::ReadTextFile(path, text)) {
        const std::wstring msg = L"\x201C" + FileName(path) + L"\x201D could not be opened.";
        MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
        return false;
    }
    m_settings.lastFolder = DirectoryFromPath(path);
    std::wstring error;
    if (EndsWithNoCase(path, L"." APP_FILE_EXT) || text.rfind(L"CurveForge project", 0) == 0) {
        Document d;
        if (!d.Deserialize(text, error)) {
            MessageBoxW(m_hwnd, error.c_str(), APP_NAME, MB_ICONWARNING);
            return false;
        }
        m_doc = std::move(d);
        m_path = path;
        m_isProject = true;
    } else {
        Table t;
        if (!Import::ParseText(text, t, error)) {
            const std::wstring msg = L"\x201C" + FileName(path) + L"\x201D has no table to read. " + error;
            MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
            return false;
        }
        LoadTable(std::move(t), path);
    }
    m_modified = false;
    m_undo.clear();
    m_redo.clear();
    m_settings.AddRecent(path);
    m_settings.Save();
    ApplyDocument(true);
    SetOp(L"Opened " + FileName(path) + L".");
    return true;
}

void MainWindow::LoadTable(Table&& t, const std::wstring& path) {
    Document d;
    // Keep the model the user chose: new data, same analysis.
    d.model = m_doc.model;
    d.order = m_doc.order;
    d.customEquation = m_doc.customEquation;
    d.robust = m_doc.robust;
    d.table = std::move(t);
    // A single column: number the rows to use as x.
    if (d.table.Cols() == 1) {
        d.table.names.insert(d.table.names.begin(), L"Row");
        for (int r = 0; r < d.table.Rows(); ++r) d.table.rows[(size_t)r].insert(d.table.rows[(size_t)r].begin(), std::to_wstring(r + 1));
    }
    // X and Y: the first two number columns.
    std::vector<int> numeric;
    for (int c = 0; c < d.table.Cols(); ++c)
        if (d.table.IsNumericColumn(c)) numeric.push_back(c);
    d.xCol = numeric.size() > 0 ? numeric[0] : 0;
    d.yCol = numeric.size() > 1 ? numeric[1] : (d.table.Cols() > 1 ? 1 : 0);
    d.sigmaCol = -1;
    m_doc = std::move(d);
    m_path = path;
    m_isProject = false;
}

bool MainWindow::Save(bool saveAs) {
    if (m_grid.IsEditing()) m_grid.CommitEdit();
    std::wstring path = m_path;
    if (saveAs || !m_isProject || path.empty()) {
        std::wstring name = m_path.empty() ? L"Untitled" : FileName(m_path);
        const size_t dot = name.rfind(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        name += L"." APP_FILE_EXT;
        if (!PickSaveFile(m_hwnd, L"CurveForge project (*.cforge)\0*.cforge\0", APP_FILE_EXT, m_settings.lastFolder, name))
            return false;
        path = name;
    }
    if (!Import::WriteTextFile(path, m_doc.Serialize())) {
        MessageBoxW(m_hwnd, (L"The project could not be saved to " + path + L".").c_str(), APP_NAME, MB_ICONWARNING);
        return false;
    }
    m_path = path;
    m_isProject = true;
    m_modified = false;
    m_settings.AddRecent(path);
    m_settings.Save();
    UpdateTitle();
    SetOp(L"Saved " + FileName(path) + L".");
    return true;
}

// ===========================================================================
// Changes and undo
// ===========================================================================
void MainWindow::BeginChange() {
    m_undo.push_back(m_doc);
    if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void MainWindow::Changed(bool columns, bool resetView) {
    m_modified = true;
    if (m_doc.excluded.size() > (size_t)m_doc.table.Rows()) m_doc.excluded.resize((size_t)m_doc.table.Rows());
    m_grid.Refresh(columns);
    UpdateTitle();
    UpdateUi();
    ScheduleFit(resetView);
}

void MainWindow::ApplyDocument(bool resetView) {
    m_grid.Refresh(true);
    UpdateTitle();
    UpdateUi();
    m_pendingReset = m_pendingReset || resetView;
    RunFit();
}

void MainWindow::Undo() {
    if (m_grid.IsEditing()) {
        m_grid.CancelEdit();
        return;
    }
    if (m_undo.empty()) {
        SetOp(L"There is nothing to undo.");
        return;
    }
    m_redo.push_back(m_doc);
    const bool cols = m_undo.back().table.Cols() != m_doc.table.Cols();
    m_doc = std::move(m_undo.back());
    m_undo.pop_back();
    m_modified = true;
    m_grid.Refresh(cols);
    ApplyDocument(false);
    SetOp(L"Undone.");
}

void MainWindow::Redo() {
    if (m_redo.empty()) {
        SetOp(L"There is nothing to redo.");
        return;
    }
    m_undo.push_back(m_doc);
    const bool cols = m_redo.back().table.Cols() != m_doc.table.Cols();
    m_doc = std::move(m_redo.back());
    m_redo.pop_back();
    m_modified = true;
    m_grid.Refresh(cols);
    ApplyDocument(false);
    SetOp(L"Redone.");
}

// ===========================================================================
// Fitting
// ===========================================================================
void MainWindow::ScheduleFit(bool resetView) {
    m_pendingReset = m_pendingReset || resetView;
    if (m_settings.autoFit) {
        SetTimer(m_hwnd, kFitTimer, 150, nullptr);
    } else {
        // Still show the new data, without a fit.
        m_points = GatherPoints(m_doc.table, m_doc.xCol, m_doc.yCol, m_doc.sigmaCol, &m_doc.excluded);
        UpdateChart();
        UpdateStatus();
    }
}

void MainWindow::RunFit() {
    KillTimer(m_hwnd, kFitTimer);
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    m_points = GatherPoints(m_doc.table, m_doc.xCol, m_doc.yCol, m_doc.sigmaCol, &m_doc.excluded);
    std::vector<double> x, y, w;
    for (size_t i = 0; i < m_points.x.size(); ++i) {
        if (m_points.excluded[i]) continue;
        x.push_back(m_points.x[i]);
        y.push_back(m_points.y[i]);
        w.push_back(m_points.w[i]);
    }
    if (x.empty()) {
        m_fit = FitRun{};
        m_fit.error = m_doc.table.Rows() == 0 ? L"" : L"There are no points to fit: the X and Y columns need numbers.";
    } else {
        m_fit = FitModel(m_doc.model, m_doc.order, m_doc.customEquation, m_doc.params, m_doc.robust,
                         m_settings.confidence, x, y, w);
    }
    SetCursor(old);
    SetWindowTextW(m_results, ResultsText().c_str());
    UpdateChart();
    UpdateUi();
}

void MainWindow::UpdateChart() {
    ChartModel cm;
    cm.x = m_points.x;
    cm.y = m_points.y;
    cm.excluded = m_points.excluded;
    if (m_fit.ok && !m_fit.r.robustWeights.empty()) {
        // Robust weights belong to the included points; spread them back.
        size_t k = 0;
        cm.robustWeight.assign(m_points.x.size(), 1.0);
        for (size_t i = 0; i < m_points.x.size(); ++i)
            if (!m_points.excluded[i] && k < m_fit.r.robustWeights.size()) cm.robustWeight[i] = m_fit.r.robustWeights[k++];
    }
    const Table& t = m_doc.table;
    cm.xTitle = m_doc.xCol < t.Cols() ? t.names[(size_t)m_doc.xCol] : L"x";
    cm.yTitle = m_doc.yCol < t.Cols() ? t.names[(size_t)m_doc.yCol] : L"y";
    cm.confidencePercent = m_settings.confidence;
    if (m_fit.ok) {
        const FitRun fit = m_fit;  // the chart keeps its own copy
        cm.fit = [fit](double x) { return fit.Eval(x); };
        cm.fitLabel = std::wstring(L"Fit: ") + Models::Info(m_doc.model).name;
        if (!fit.spline)
            cm.bands = [fit](double x, double& conf, double& pred) {
                double y;
                return Fit::Bands(*fit.expr, fit.r, x, y, conf, pred);
            };
    }
    ChartOptions o;
    o.confidenceBand = m_settings.confidenceBand;
    o.predictionBand = m_settings.predictionBand;
    o.residuals = m_settings.residuals;
    o.grid = m_settings.gridLines;
    o.logX = m_doc.logX;
    o.logY = m_doc.logY;
    m_chart.SetOptions(o);
    m_chart.SetModel(std::move(cm), m_pendingReset);
    m_pendingReset = false;
}

std::wstring MainWindow::FittedEquation(bool excel) const {
    if (!m_fit.ok || m_fit.spline || !m_fit.expr) return L"";
    std::vector<std::wstring> values;
    for (double v : m_fit.r.p) values.push_back(excel ? FormatExact(v) : FormatNumber(v, m_settings.digits));
    std::wstring rhs = Expr::RightHandSide(m_doc.Equation());
    while (!rhs.empty() && rhs[0] == L' ') rhs.erase(0, 1);
    std::wstring s = Expr::Substitute(rhs, m_fit.r.names, values);
    if (excel) {
        s = Expr::ReplaceWord(s, L"x", L"A2");
        s = Expr::ReplaceWord(s, L"log", L"LOG10");
        s = Expr::ReplaceWord(s, L"pi", L"PI()");
        return L"=" + s;
    }
    return L"y = " + s;
}

std::wstring MainWindow::ResultsText() const {
    const int d = m_settings.digits;
    const Table& t = m_doc.table;
    std::wstring s;
    const ModelInfo& info = Models::Info(m_doc.model);
    if (t.Rows() == 0) {
        return L"Welcome to CurveForge.\r\n\r\n"
               L"1. Put your data in the table: open a file (Ctrl+O), paste from Excel (Ctrl+V), or type values.\r\n"
               L"2. Choose the X and Y columns and a model (or type your own equation).\r\n"
               L"3. The curve is fitted at once. Click points on the graph to leave them out.\r\n\r\n"
               L"More \x203A Examples has ready-made data to try.";
    }
    s += std::wstring(L"Model:      ") + info.name + L"\r\n";
    if (m_doc.model != ModelKind::Spline) s += L"Equation:   " + m_doc.Equation() + L"\r\n";
    if (!m_fit.ok) {
        s += L"\r\n" + (m_fit.error.empty() ? std::wstring(L"Nothing to fit yet.") : L"\x26A0 " + m_fit.error) + L"\r\n";
        return s;
    }
    if (m_fit.spline) {
        s += L"\r\nA smooth curve through all " + std::to_wstring(m_fit.r.n) +
             L" points (points with the same x are averaged).\r\n"
             L"Use Predict (Ctrl+R) to read values between the points, the slope or the area.\r\n"
             L"A spline has no parameters or goodness-of-fit numbers: it passes through every point.\r\n";
        return s;
    }
    const FitResult& r = m_fit.r;
    s += L"Result:     " + FittedEquation(false) + L"\r\n";
    if (*info.about && m_doc.model != ModelKind::Custom) s += std::wstring(L"            (") + info.about + L")\r\n";
    s += L"\r\n";
    const std::wstring pct = std::to_wstring(m_settings.confidence) + L"%";
    auto pad = [](std::wstring v, size_t w) {
        if (v.size() < w) v.append(w - v.size(), L' ');
        return v;
    };
    s += pad(L"Parameter", 12) + pad(L"Value", 16) + pad(L"Std. error", 14) + pct + L" confidence interval\r\n";
    bool missingSe = false;
    for (size_t i = 0; i < r.p.size(); ++i) {
        std::wstring line = pad(r.names[i], 12) + pad(FormatNumber(r.p[i], d), 16);
        if (r.fixed[i]) {
            line += L"(fixed)";
        } else {
            if (!std::isfinite(r.se[i])) missingSe = true;
            line += pad(FormatNumber(r.se[i], 3), 14);
            if (std::isfinite(r.ciLo[i])) line += FormatNumber(r.ciLo[i], d) + L"  to  " + FormatNumber(r.ciHi[i], d);
            if (std::isfinite(r.se[i]) && r.p[i] != 0 && std::fabs(r.se[i]) > std::fabs(r.p[i]))
                line += L"   (uncertain)";
        }
        s += line + L"\r\n";
    }
    s += L"\r\nGoodness of fit\r\n";
    s += L"  R\xB2                 " + FormatNumber(r.r2, d) + L"\r\n";
    s += L"  Adjusted R\xB2        " + FormatNumber(r.adjR2, d) + L"\r\n";
    if (m_doc.sigmaCol >= 0) {
        s += L"  RMSE               " + FormatNumber(r.rmse, d) + L"   (in units of \x03C3)\r\n";
        s += L"  Reduced \x03C7\xB2         " + FormatNumber(r.sigma2, d) +
             L"   (about 1 when the \x03C3 values match the scatter)\r\n";
    } else {
        s += L"  RMSE               " + FormatNumber(r.rmse, d) + L"   (typical distance of a point from the curve)\r\n";
    }
    s += L"  Std. error of fit  " + FormatNumber(std::sqrt(r.sigma2), d) + L"\r\n";
    s += L"  Sum of squares     " + FormatNumber(r.sse, d) + L"\r\n";
    s += L"  AICc               " + FormatNumber(r.aicc, d) + L"   (to compare models: lower is better)\r\n";
    s += L"  BIC                " + FormatNumber(r.bic, d) + L"\r\n";
    int left = 0;
    for (char e : m_points.excluded) left += e;
    s += L"\r\nPoints used: " + std::to_wstring(r.n);
    if (left) s += L"  (" + std::to_wstring(left) + L" left out)";
    s += L"     Degrees of freedom: " + std::to_wstring(r.dof) + L"\r\n";
    if (m_doc.sigmaCol >= 0) {
        s += L"Weighted by 1/\x03C3\xB2 from the column \x201C" + t.names[(size_t)m_doc.sigmaCol] + L"\x201D.";
        if (m_points.badSigma) s += L" " + std::to_wstring(m_points.badSigma) + L" row(s) have no valid \x03C3 and are not used.";
        s += L"\r\n";
    }
    if (m_doc.robust) {
        int ignored = 0;
        for (double w : r.robustWeights)
            if (w < 0.1) ++ignored;
        s += L"Robust fitting: on (" + std::to_wstring(ignored) + L" outlier(s) ignored, circled in red on the graph).\r\n";
    }
    if (!r.converged) s += L"\x26A0 The fit did not fully settle. Try Parameters (Ctrl+P) to give starting values.\r\n";
    if (missingSe)
        s += L"\x26A0 Some standard errors could not be calculated: the parameters depend too strongly on each other "
             L"(for a polynomial, try a lower degree).\r\n";
    if (r.dof <= 0) s += L"\x26A0 There are as many parameters as points, so the curve fits exactly and has no statistics.\r\n";
    return s;
}
