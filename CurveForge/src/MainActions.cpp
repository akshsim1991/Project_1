// MainActions.cpp - menus, data editing, tools, export and examples.
#include "MainWindow.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <numeric>
#include <random>

#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>

#include "Dialogs.h"
#include "NumFormat.h"
#include "Theme.h"
#include "Util.h"

namespace {
std::wstring FileName(const std::wstring& path) {
    const size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? path : path.substr(s + 1);
}

std::wstring Stem(const std::wstring& path) {
    std::wstring n = path.empty() ? L"Untitled" : FileName(path);
    const size_t dot = n.rfind(L'.');
    return dot == std::wstring::npos ? n : n.substr(0, dot);
}

void Popup(HWND owner, HMENU m, POINT pt, bool rightAlign = false) {
    TrackPopupMenu(m, (rightAlign ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_TOPALIGN, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m);
}

UINT Checked(bool on) { return on ? MF_CHECKED : MF_UNCHECKED; }

std::string Base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += t[v & 63];
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        if (i + 1 < in.size()) v |= (unsigned char)in[i + 1] << 8;
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::wstring Html(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
            case L'&': o += L"&amp;"; break;
            case L'<': o += L"&lt;"; break;
            case L'>': o += L"&gt;"; break;
            case L'"': o += L"&quot;"; break;
            default: o += c;
        }
    }
    return o;
}
}  // namespace

// ===========================================================================
// Model
// ===========================================================================
void MainWindow::SetModel(ModelKind k, int order) {
    if (m_grid.IsEditing()) m_grid.CommitEdit();
    const ModelInfo& info = Models::Info(k);
    if (info.hasOrder) order = std::min(std::max(order, Models::MinOrder(k)), Models::MaxOrder(k));
    if (k == m_doc.model && (!info.hasOrder || order == m_doc.order)) return;
    BeginChange();
    m_doc.model = k;
    if (info.hasOrder) m_doc.order = order;
    Changed(false, false);
    if (k == ModelKind::Custom) {
        SetFocus(m_equation);
        SendMessageW(m_equation, EM_SETSEL, 0, -1);
    }
}

void MainWindow::ShowModelMenu() {
    struct Group {
        const wchar_t* title;
        std::vector<ModelKind> kinds;
    };
    const Group groups[] = {
        {L"Lines and polynomials", {ModelKind::Linear, ModelKind::Proportional, ModelKind::Polynomial, ModelKind::Inverse}},
        {L"Growth and decay",
         {ModelKind::Exponential, ModelKind::ExpOffset, ModelKind::DoubleExp, ModelKind::Logarithmic, ModelKind::Power}},
        {L"Peaks", {ModelKind::Gaussian, ModelKind::Lorentzian, ModelKind::GaussianPeaks}},
        {L"S-curves and saturation",
         {ModelKind::Logistic, ModelKind::DoseResponse, ModelKind::Hill, ModelKind::MichaelisMenten}},
        {L"Waves", {ModelKind::Sine, ModelKind::DampedSine}},
        {L"Other", {ModelKind::Spline, ModelKind::Custom}},
    };
    HMENU m = CreatePopupMenu();
    bool first = true;
    for (const Group& g : groups) {
        if (!first) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        first = false;
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, g.title);
        for (ModelKind k : g.kinds) {
            const ModelInfo& info = Models::Info(k);
            std::wstring label = std::wstring(L"    ") + info.name;
            const std::wstring formula = Models::Formula(k, Models::DefaultOrder(k));
            if (k != ModelKind::Custom && k != ModelKind::Spline && k != ModelKind::Polynomial && k != ModelKind::GaussianPeaks)
                label += L"\t" + formula;
            AppendMenuW(m, MF_STRING | Checked(k == m_doc.model), ID_MODEL_FIRST + (int)k, label.c_str());
        }
    }
    const RECT r = m_toolbar.ItemScreenRect(ID_MODEL_MENU);
    Popup(m_hwnd, m, {r.left, r.bottom});
}

void MainWindow::ShowOrderMenu() {
    const ModelKind k = m_doc.model;
    if (!Models::Info(k).hasOrder) return;
    HMENU m = CreatePopupMenu();
    for (int o = Models::MinOrder(k); o <= Models::MaxOrder(k); ++o) {
        std::wstring label = std::to_wstring(o);
        if (k == ModelKind::Polynomial) {
            if (o == 2) label += L"  (quadratic)";
            if (o == 3) label += L"  (cubic)";
            if (o == 4) label += L"  (quartic)";
        } else {
            label += L" peaks";
        }
        AppendMenuW(m, MF_STRING | Checked(o == m_doc.order), ID_ORDER_FIRST + o, label.c_str());
    }
    const RECT r = m_toolbar.ItemScreenRect(ID_ORDER_MENU);
    Popup(m_hwnd, m, {r.left, r.bottom});
}

void MainWindow::ShowRoleMenu(int role) {
    HMENU m = CreatePopupMenu();
    const int current = role == 0 ? m_doc.xCol : role == 1 ? m_doc.yCol : m_doc.sigmaCol;
    if (role == 2) {
        AppendMenuW(m, MF_STRING | Checked(current < 0), ID_COL_SIGMA_NONE, L"None (all points count equally)");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    const int base = role == 0 ? ID_COL_X_FIRST : role == 1 ? ID_COL_Y_FIRST : ID_COL_SIGMA_FIRST;
    for (int c = 0; c < m_doc.table.Cols() && c < 199; ++c) {
        std::wstring label = m_doc.table.names[(size_t)c];
        if (!m_doc.table.IsNumericColumn(c)) label += L"   (no numbers)";
        AppendMenuW(m, MF_STRING | Checked(c == current), (UINT_PTR)(base + c), label.c_str());
    }
    const int id = role == 0 ? ID_X_MENU : role == 1 ? ID_Y_MENU : ID_SIGMA_MENU;
    const RECT r = m_setup.ItemScreenRect(id);
    Popup(m_hwnd, m, {r.left, r.bottom});
}

void MainWindow::ShowExportMenu() {
    const UINT fit = m_fit.ok ? 0 : MF_GRAYED;
    const UINT eq = m_fit.ok && !m_fit.spline ? 0 : MF_GRAYED;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | fit, ID_EXPORT_CSV, L"Save &fitted values as CSV\x2026\tCtrl+Shift+E");
    AppendMenuW(m, MF_STRING, ID_SAVE_TABLE_CSV, L"Save the &table as CSV\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXPORT_PNG, L"Save the graph as &PNG\x2026");
    AppendMenuW(m, MF_STRING, ID_EXPORT_SVG, L"Save the graph as &SVG (sharp at any size)\x2026");
    AppendMenuW(m, MF_STRING, ID_COPY_CHART, L"Copy the &graph\tCtrl+Shift+C");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | fit, ID_COPY_RESULTS, L"Copy the &results");
    AppendMenuW(m, MF_STRING | eq, ID_COPY_EQUATION, L"Copy the &equation");
    AppendMenuW(m, MF_STRING | eq, ID_COPY_EXCEL, L"Copy the equation as an E&xcel formula");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | fit, ID_REPORT, L"Save a &report (graph, equation and statistics)\x2026\tCtrl+Shift+R");
    const RECT r = m_toolbar.ItemScreenRect(ID_EXPORT_MENU);
    Popup(m_hwnd, m, {r.right, r.bottom}, true);
}

void MainWindow::ShowMoreMenu() {
    HMENU recent = CreatePopupMenu();
    if (m_settings.recent.empty()) AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"(none yet)");
    for (size_t i = 0; i < m_settings.recent.size() && i < 10; ++i) {
        const std::wstring label = L"&" + std::to_wstring((i + 1) % 10) + L"  " + FileName(m_settings.recent[i]) +
                                   L"\t" + DirectoryFromPath(m_settings.recent[i]);
        AppendMenuW(recent, MF_STRING, ID_RECENT_FIRST + (UINT)i, label.c_str());
    }
    HMENU examples = CreatePopupMenu();
    const wchar_t* names[] = {L"Radioactive decay (exponential with offset)", L"Spectrum peak (Gaussian)",
                              L"Dose-response curve (4PL, log x)", L"Oscillation (damped sine)",
                              L"Calibration line with an outlier (robust)", L"Enzyme kinetics (Michaelis-Menten)",
                              L"Two overlapping peaks"};
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
        AppendMenuW(examples, MF_STRING, ID_EXAMPLE_FIRST + i, names[i]);

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_NEW, L"&New\tCtrl+N");
    AppendMenuW(m, MF_STRING, ID_OPEN, L"&Open\x2026\tCtrl+O");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)recent, L"&Recent files");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)examples, L"Exa&mples");
    AppendMenuW(m, MF_STRING, ID_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(m, MF_STRING, ID_SAVE_AS, L"Save &as\x2026\tCtrl+Shift+S");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_undo.empty() ? MF_GRAYED : 0), ID_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(m, MF_STRING | (m_redo.empty() ? MF_GRAYED : 0), ID_REDO, L"Re&do\tCtrl+Y");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"Se&ttings\x2026\tCtrl+,");
    AppendMenuW(m, MF_STRING, ID_SHORTCUTS, L"&Keyboard shortcuts");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"A&bout " APP_NAME L"\tF1");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");
    const RECT r = m_toolbar.ItemScreenRect(ID_MORE_MENU);
    Popup(m_hwnd, m, {r.right, r.bottom}, true);
}

void MainWindow::ShowTableMenu(POINT pt) {
    const bool rows = !m_grid.SelectedRows().empty();
    const UINT r = rows ? 0 : MF_GRAYED;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | r, ID_CUT, L"Cu&t rows\tCtrl+X");
    AppendMenuW(m, MF_STRING | r, ID_COPY, L"&Copy rows\tCtrl+C");
    AppendMenuW(m, MF_STRING, ID_PASTE, L"&Paste here\tCtrl+V");
    AppendMenuW(m, MF_STRING, ID_PASTE_NEW, L"Paste as a &new table\tCtrl+Shift+V");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_INSERT_ROW, L"&Insert a row\tInsert");
    AppendMenuW(m, MF_STRING | r, ID_DELETE_ROWS, L"&Delete rows\tCtrl+Delete");
    AppendMenuW(m, MF_STRING | r, ID_EXCLUDE_ROWS, L"&Leave out / use again\tCtrl+E");
    AppendMenuW(m, MF_STRING, ID_INCLUDE_ALL, L"&Use all rows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SET_X, L"Use this column as &X");
    AppendMenuW(m, MF_STRING, ID_SET_Y, L"Use this column as &Y");
    AppendMenuW(m, MF_STRING, ID_SET_SIGMA, L"Use this column as &errors (\x03C3)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_ADD_COLUMN, L"&Add a column\x2026");
    AppendMenuW(m, MF_STRING, ID_FORMULA_COLUMN, L"New column from a &formula\x2026");
    AppendMenuW(m, MF_STRING, ID_RENAME_COLUMN, L"Re&name this column\x2026");
    AppendMenuW(m, MF_STRING, ID_SORT_ASC, L"S&ort by this column (ascending)");
    AppendMenuW(m, MF_STRING, ID_SORT_DESC, L"Sort by this column (descendin&g)");
    AppendMenuW(m, MF_STRING | (m_doc.table.Cols() > 1 ? 0 : MF_GRAYED), ID_DELETE_COLUMN, L"Delete this col&umn");
    Popup(m_hwnd, m, pt);
}

void MainWindow::ShowHeaderMenu(POINT pt, int col) {
    if (col < 0) {
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, ID_SELECT_ALL, L"Select all rows\tCtrl+A");
        AppendMenuW(m, MF_STRING, ID_INCLUDE_ALL, L"Use all rows");
        AppendMenuW(m, MF_STRING, ID_ADD_COLUMN, L"Add a column\x2026");
        Popup(m_hwnd, m, pt);
        return;
    }
    m_grid.SetCurrent(m_grid.CurrentRow(), col);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | Checked(col == m_doc.xCol), ID_SET_X, L"Use as &X");
    AppendMenuW(m, MF_STRING | Checked(col == m_doc.yCol), ID_SET_Y, L"Use as &Y");
    AppendMenuW(m, MF_STRING | Checked(col == m_doc.sigmaCol), ID_SET_SIGMA, L"Use as &errors (\x03C3) of Y");
    if (m_doc.sigmaCol >= 0) AppendMenuW(m, MF_STRING, ID_CLEAR_SIGMA, L"Stop using errors (\x03C3)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SORT_ASC, L"Sort &ascending");
    AppendMenuW(m, MF_STRING, ID_SORT_DESC, L"Sort &descending");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_RENAME_COLUMN, L"Re&name\x2026");
    AppendMenuW(m, MF_STRING, ID_ADD_COLUMN, L"Add a &column\x2026");
    AppendMenuW(m, MF_STRING, ID_FORMULA_COLUMN, L"New column from a &formula\x2026");
    AppendMenuW(m, MF_STRING | (m_doc.table.Cols() > 1 ? 0 : MF_GRAYED), ID_DELETE_COLUMN, L"De&lete this column");
    Popup(m_hwnd, m, pt);
}

void MainWindow::ShowChartMenu(POINT pt) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_RESET_VIEW, L"&Show everything\tCtrl+0");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | Checked(m_settings.confidenceBand), ID_CONF_BAND, L"&Confidence band");
    AppendMenuW(m, MF_STRING | Checked(m_settings.predictionBand), ID_PRED_BAND, L"&Prediction band");
    AppendMenuW(m, MF_STRING | Checked(m_settings.residuals), ID_RESIDUALS, L"&Residual plot");
    AppendMenuW(m, MF_STRING | Checked(m_settings.gridLines), ID_GRID_LINES, L"&Grid lines");
    AppendMenuW(m, MF_STRING | Checked(m_doc.logX), ID_LOG_X, L"Log &x axis");
    AppendMenuW(m, MF_STRING | Checked(m_doc.logY), ID_LOG_Y, L"Log &y axis");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COPY_CHART, L"C&opy the graph\tCtrl+Shift+C");
    AppendMenuW(m, MF_STRING, ID_EXPORT_PNG, L"Save as P&NG\x2026");
    AppendMenuW(m, MF_STRING, ID_EXPORT_SVG, L"Save as S&VG\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_INCLUDE_ALL, L"&Use all points again");
    Popup(m_hwnd, m, pt);
}

// ===========================================================================
// Data editing
// ===========================================================================
void MainWindow::CopyRows(bool cut) {
    const std::vector<int> rows = m_grid.SelectedRows();
    if (rows.empty()) return;
    const Table& t = m_doc.table;
    std::vector<std::vector<std::wstring>> out;
    for (int r : rows) {
        std::vector<std::wstring> row;
        for (int c = 0; c < t.Cols(); ++c) row.push_back(t.Cell(r, c));
        out.push_back(std::move(row));
    }
    CopyToClipboard(m_hwnd, JoinTsv(t.names, out));
    if (cut) DeleteRows();
    else SetOp(L"Copied " + std::to_wstring(rows.size()) + L" row(s) with the column names.");
}

void MainWindow::Paste(bool asNewTable) {
    if (m_grid.IsEditing()) m_grid.CommitEdit();
    std::wstring text;
    if (OpenClipboard(m_hwnd)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                text = p;
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
    }
    if (text.empty()) {
        SetOp(L"The clipboard has no text to paste.");
        return;
    }
    Table t;
    std::wstring error;
    if (!Import::ParseText(text, t, error)) {
        SetOp(L"Nothing to paste.");
        return;
    }
    if (asNewTable || m_doc.table.Rows() == 0) {
        BeginChange();
        const std::wstring path = m_path;
        const bool project = m_isProject;
        LoadTable(std::move(t), path);
        m_isProject = project;
        Changed(true, true);
        m_grid.SetCurrent(0, 0);
        SetOp(L"Pasted " + std::to_wstring(m_doc.table.Rows()) + L" rows.");
        return;
    }
    // Into the table at the cell cursor. A single text cell is pasted as is.
    std::vector<std::vector<std::wstring>> rows = t.rows;
    if (rows.empty()) rows.push_back(t.names);
    const int r0 = m_grid.CurrentRow(), c0 = m_grid.CurrentCol();
    const int colsBefore = m_doc.table.Cols();
    BeginChange();
    for (size_t r = 0; r < rows.size(); ++r)
        for (size_t c = 0; c < rows[r].size(); ++c) m_doc.table.SetCell(r0 + (int)r, c0 + (int)c, rows[r][c]);
    Changed(m_doc.table.Cols() != colsBefore, true);
    m_grid.SetCurrent(r0, c0);
    SetOp(L"Pasted " + std::to_wstring(rows.size()) + L" row(s).");
}

void MainWindow::InsertRow() {
    if (m_grid.IsEditing()) return;
    const int r = m_grid.CurrentRow();
    if (r >= m_doc.table.Rows()) {
        m_grid.BeginEdit(nullptr);
        return;
    }
    BeginChange();
    m_doc.table.rows.insert(m_doc.table.rows.begin() + r, std::vector<std::wstring>{});
    if ((int)m_doc.excluded.size() > r) m_doc.excluded.insert(m_doc.excluded.begin() + r, false);
    Changed(false, false);
    m_grid.SetCurrent(r, m_grid.CurrentCol());
}

void MainWindow::DeleteRows() {
    std::vector<int> rows = m_grid.SelectedRows();
    if (rows.empty()) return;
    BeginChange();
    std::sort(rows.rbegin(), rows.rend());
    for (int r : rows) {
        m_doc.table.rows.erase(m_doc.table.rows.begin() + r);
        if (r < (int)m_doc.excluded.size()) m_doc.excluded.erase(m_doc.excluded.begin() + r);
    }
    Changed(false, true);
    m_grid.SetCurrent(rows.back(), m_grid.CurrentCol());
    SetOp(L"Deleted " + std::to_wstring(rows.size()) + L" row(s). Ctrl+Z brings them back.");
}

void MainWindow::ToggleExcluded() {
    const std::vector<int> rows = m_grid.SelectedRows();
    if (rows.empty()) return;
    bool anyUsed = false;
    for (int r : rows)
        if (!m_doc.IsExcluded(r)) anyUsed = true;
    BeginChange();
    for (int r : rows) m_doc.SetExcluded(r, anyUsed);
    Changed(false, false);
    SetOp(std::to_wstring(rows.size()) + (anyUsed ? L" row(s) left out of the fit." : L" row(s) used again."));
}

void MainWindow::IncludeAll() {
    if (std::none_of(m_doc.excluded.begin(), m_doc.excluded.end(), [](bool b) { return b; })) return;
    BeginChange();
    m_doc.excluded.clear();
    Changed(false, false);
    SetOp(L"All rows are used again.");
}

void MainWindow::TogglePoint(int point) {
    if (point < 0 || point >= (int)m_points.rows.size()) return;
    const int row = m_points.rows[(size_t)point];
    BeginChange();
    const bool ex = !m_doc.IsExcluded(row);
    m_hoverText.clear();
    m_doc.SetExcluded(row, ex);
    Changed(false, false);
    m_grid.SetCurrent(row, m_grid.CurrentCol());
    SetOp(L"Row " + std::to_wstring(row + 1) + (ex ? L" left out. Click the point again to use it." : L" used again."));
}

void MainWindow::AddColumn() {
    std::wstring name = L"Column " + std::to_wstring(m_doc.table.Cols() + 1);
    if (!ShowInputDialog(m_inst, m_hwnd, L"Add a column", L"Name of the new column:", name) || name.empty()) return;
    BeginChange();
    m_doc.table.names.push_back(name);
    Changed(true, false);
    m_grid.SetCurrent(m_grid.CurrentRow(), m_doc.table.Cols() - 1);
}

void MainWindow::FormulaColumn() {
    const std::vector<std::wstring> vars = ColumnVariables(m_doc.table);
    std::wstring name = L"Column " + std::to_wstring(m_doc.table.Cols() + 1), formula;
    for (;;) {
        if (!ShowFormulaDialog(m_inst, m_hwnd, vars, name, formula)) return;
        std::vector<std::wstring> all = vars;
        all.push_back(L"row");
        Expr e;
        std::wstring err;
        if (!e.Parse(formula, all, err)) {
            MessageBoxW(m_hwnd, (L"The formula can not be read: " + err).c_str(), APP_NAME, MB_ICONWARNING);
            continue;
        }
        if (!e.Params().empty()) {
            std::wstring unknown;
            for (const std::wstring& p : e.Params()) unknown += (unknown.empty() ? L"" : L", ") + p;
            MessageBoxW(m_hwnd, (L"These names are not columns: " + unknown + L".").c_str(), APP_NAME, MB_ICONWARNING);
            continue;
        }
        BeginChange();
        const int col = m_doc.table.Cols();
        m_doc.table.names.push_back(name.empty() ? L"Formula" : name);
        std::vector<double> v(all.size(), NAN);
        for (int r = 0; r < m_doc.table.Rows(); ++r) {
            for (int c = 0; c < (int)vars.size(); ++c) v[(size_t)c] = m_doc.table.Value(r, c);
            v.back() = r + 1;
            const double res = e.Eval(v.data(), nullptr);
            m_doc.table.SetCell(r, col, std::isfinite(res) ? FormatExact(res) : L"");
        }
        Changed(true, false);
        m_grid.SetCurrent(m_grid.CurrentRow(), col);
        return;
    }
}

void MainWindow::RenameColumn(int col) {
    if (col < 0 || col >= m_doc.table.Cols()) return;
    std::wstring name = m_doc.table.names[(size_t)col];
    if (!ShowInputDialog(m_inst, m_hwnd, L"Rename the column", L"New name:", name) || name.empty() ||
        name == m_doc.table.names[(size_t)col])
        return;
    BeginChange();
    m_doc.table.names[(size_t)col] = name;
    Changed(false, false);
    UpdateChart();
}

void MainWindow::DeleteColumn(int col) {
    if (col < 0 || col >= m_doc.table.Cols() || m_doc.table.Cols() <= 1) return;
    BeginChange();
    m_doc.table.names.erase(m_doc.table.names.begin() + col);
    for (auto& row : m_doc.table.rows)
        if (col < (int)row.size()) row.erase(row.begin() + col);
    auto fix = [&](int& role, int fallback) {
        if (role == col) role = fallback;
        else if (role > col) --role;
    };
    fix(m_doc.xCol, 0);
    fix(m_doc.yCol, m_doc.table.Cols() > 1 ? 1 : 0);
    fix(m_doc.sigmaCol, -1);
    m_doc.table.TrimEmptyRows();
    Changed(true, true);
}

void MainWindow::SortRows(int col, bool ascending) {
    Table& t = m_doc.table;
    if (col < 0 || col >= t.Cols() || t.Rows() < 2) return;
    std::vector<int> order((size_t)t.Rows());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const double va = t.Value(a, col), vb = t.Value(b, col);
        const bool na = std::isfinite(va), nb = std::isfinite(vb);
        if (na != nb) return na;  // numbers first, text and empty cells last
        if (na) return ascending ? va < vb : va > vb;
        return ascending ? t.Cell(a, col) < t.Cell(b, col) : t.Cell(a, col) > t.Cell(b, col);
    });
    BeginChange();
    std::vector<std::vector<std::wstring>> rows;
    std::vector<bool> ex;
    for (int i : order) {
        rows.push_back(t.rows[(size_t)i]);
        ex.push_back(m_doc.IsExcluded(i));
    }
    t.rows = std::move(rows);
    m_doc.excluded = ex;
    Changed(false, false);
}

void MainWindow::SetRole(int role, int col) {
    if (col >= m_doc.table.Cols()) return;
    int& target = role == 0 ? m_doc.xCol : role == 1 ? m_doc.yCol : m_doc.sigmaCol;
    if (target == col) return;
    BeginChange();
    // Choosing the other axis' column swaps them.
    if (role == 0 && col == m_doc.yCol) m_doc.yCol = m_doc.xCol;
    if (role == 1 && col == m_doc.xCol) m_doc.xCol = m_doc.yCol;
    if (role != 2 && col == m_doc.sigmaCol) m_doc.sigmaCol = -1;
    if (role == 2 && (col == m_doc.xCol || col == m_doc.yCol)) {
        MessageBoxW(m_hwnd, L"The errors (\x03C3) need their own column, not the X or Y column.", APP_NAME, MB_ICONINFORMATION);
        m_undo.pop_back();
        return;
    }
    target = col;
    Changed(false, true);
}

void MainWindow::AddColumns(const std::vector<std::wstring>& names, const std::vector<std::vector<double>>& cols) {
    BeginChange();
    for (size_t k = 0; k < names.size() && k < cols.size(); ++k) {
        const int c = m_doc.table.Cols();
        m_doc.table.names.push_back(names[k]);
        for (size_t r = 0; r < cols[k].size(); ++r) m_doc.table.SetCell((int)r, c, FormatExact(cols[k][r]));
    }
    Changed(true, false);
}

// ===========================================================================
// Tools
// ===========================================================================
void MainWindow::BestFit() {
    if (m_grid.IsEditing()) m_grid.CommitEdit();
    std::vector<double> x, y, w;
    for (size_t i = 0; i < m_points.x.size(); ++i) {
        if (m_points.excluded[i]) continue;
        x.push_back(m_points.x[i]);
        y.push_back(m_points.y[i]);
        w.push_back(m_points.w[i]);
    }
    if (x.size() < 4) {
        MessageBoxW(m_hwnd, L"Best fit needs at least 4 points.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    const bool positive = std::all_of(x.begin(), x.end(), [](double v) { return v > 0; });
    const bool zero = std::any_of(x.begin(), x.end(), [](double v) { return v == 0; });
    struct Candidate {
        ModelKind kind;
        int order;
    };
    std::vector<Candidate> cands = {{ModelKind::Linear, 0}, {ModelKind::Proportional, 0}};
    for (int d = 2; d <= 6 && d + 2 < (int)x.size(); ++d) cands.push_back({ModelKind::Polynomial, d});
    for (ModelKind k : {ModelKind::Inverse, ModelKind::Exponential, ModelKind::ExpOffset, ModelKind::DoubleExp,
                        ModelKind::Logarithmic, ModelKind::Power, ModelKind::Gaussian, ModelKind::Lorentzian,
                        ModelKind::Logistic, ModelKind::DoseResponse, ModelKind::Hill, ModelKind::MichaelisMenten,
                        ModelKind::Sine, ModelKind::DampedSine}) {
        if (Models::Info(k).positiveX && !positive) continue;
        if (k == ModelKind::Inverse && zero) continue;
        cands.push_back({k, 0});
    }
    cands.push_back({ModelKind::GaussianPeaks, 2});
    if (m_doc.model == ModelKind::Custom) cands.push_back({ModelKind::Custom, 0});

    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    struct Row {
        Candidate c;
        FitRun run;
        std::wstring label;
    };
    std::vector<Row> rows;
    for (const Candidate& c : cands) {
        Row r{c, FitModel(c.kind, c.order, m_doc.customEquation, c.kind == ModelKind::Custom ? m_doc.params : std::vector<ParamOption>{},
                          m_doc.robust, m_settings.confidence, x, y, w),
              Models::Info(c.kind).name};
        if (c.kind == ModelKind::Polynomial) r.label += L", degree " + std::to_wstring(c.order);
        if (c.kind == ModelKind::GaussianPeaks) r.label += L" (2)";
        if (r.run.ok && r.run.r.dof > 0 && std::isfinite(r.run.r.sse)) rows.push_back(std::move(r));
    }
    SetCursor(old);
    if (rows.empty()) {
        MessageBoxW(m_hwnd, L"No model could be fitted to these data.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    auto key = [](const Row& r) { return std::isfinite(r.run.r.aicc) ? r.run.r.aicc : r.run.r.aic; };
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) { return key(a) < key(b); });
    const double best = key(rows[0]);

    TableDialogData d;
    d.title = L"Best fit";
    d.info = L"Every model was fitted to the " + std::to_wstring(x.size()) +
             L" points and ranked by AICc, which rewards a close fit but penalises extra parameters. "
             L"\x0394" L"AICc below 2: about as good as the best. Double-click a model to use it.";
    d.headers = {L"Model", L"\x0394" L"AICc", L"R\xB2", L"Adj. R\xB2", L"RMSE", L"Parameters", L"Note"};
    d.canUse = true;
    int currentRow = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const FitResult& r = rows[i].run.r;
        std::wstring note;
        if (!r.converged) note = L"did not settle";
        for (size_t k = 0; k < r.se.size(); ++k)
            if (!r.fixed[k] && (!std::isfinite(r.se[k]) || (r.p[k] != 0 && r.se[k] > std::fabs(r.p[k])))) {
                note = note.empty() ? L"uncertain parameters" : note + L", uncertain parameters";
                break;
            }
        d.rows.push_back({rows[i].label, FormatNumber(key(rows[i]) - best, 3), FormatNumber(r.r2, 6),
                          FormatNumber(r.adjR2, 6), FormatNumber(r.rmse, 4), std::to_wstring(r.k), note});
        if (rows[i].c.kind == m_doc.model && (rows[i].c.order == m_doc.order || !Models::Info(m_doc.model).hasOrder))
            currentRow = (int)i;
    }
    d.selected = 0;
    (void)currentRow;
    const int chosen = ShowTableDialog(m_inst, m_hwnd, d, m_settings.lastFolder);
    if (chosen >= 0 && chosen < (int)rows.size()) SetModel(rows[(size_t)chosen].c.kind, rows[(size_t)chosen].c.order ? rows[(size_t)chosen].c.order : m_doc.order);
}

void MainWindow::Parameters() {
    if (m_doc.model == ModelKind::Spline) {
        MessageBoxW(m_hwnd, L"A spline has no parameters.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    Expr e;
    std::wstring err;
    if (!e.Parse(m_doc.Equation(), {L"x"}, err)) {
        MessageBoxW(m_hwnd, (L"Fix the equation first: " + err).c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }
    std::vector<double> fitted;
    if (m_fit.ok && !m_fit.spline && m_fit.r.names == e.Params()) fitted = m_fit.r.p;
    std::vector<ParamOption> opts = m_doc.params;
    if (!ShowParamsDialog(m_inst, m_hwnd, e.Params(), fitted, opts)) return;
    BeginChange();
    m_doc.params = std::move(opts);
    Changed(false, false);
}

void MainWindow::Predict() {
    if (!m_fit.ok) {
        MessageBoxW(m_hwnd, L"Fit a model first.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    PredictContext c;
    const FitRun fit = m_fit;
    c.f = [fit](double x) { return fit.Eval(x); };
    if (!fit.spline)
        c.bands = [fit](double x, double& conf, double& pred) {
            double y;
            return Fit::Bands(*fit.expr, fit.r, x, y, conf, pred);
        };
    c.xmin = INFINITY;
    c.xmax = -INFINITY;
    for (size_t i = 0; i < m_points.x.size(); ++i) {
        if (m_points.excluded[i]) continue;
        c.xmin = std::min(c.xmin, m_points.x[i]);
        c.xmax = std::max(c.xmax, m_points.x[i]);
    }
    if (!std::isfinite(c.xmin)) {
        c.xmin = 0;
        c.xmax = 1;
    }
    c.digits = m_settings.digits;
    c.confidence = m_settings.confidence;
    const Table& t = m_doc.table;
    c.xName = m_doc.xCol < t.Cols() ? t.names[(size_t)m_doc.xCol] : L"x";
    c.yName = m_doc.yCol < t.Cols() ? t.names[(size_t)m_doc.yCol] : L"y";
    c.addColumns = [this, c](const std::vector<double>& xs, const std::vector<double>& ys) {
        AddColumns({c.xName + L" (table)", L"Fitted " + c.yName}, {xs, ys});
    };
    ShowPredictDialog(m_inst, m_hwnd, c);
}

void MainWindow::Batch() {
    if (m_grid.IsEditing()) m_grid.CommitEdit();
    BatchChoice choice;
    const std::wstring info = std::wstring(L"Fits the current model (") + Models::Info(m_doc.model).name +
                              L") to many data sets and lists the results side by side.";
    if (!ShowBatchDialog(m_inst, m_hwnd, info, m_settings.lastFolder, choice)) return;

    std::vector<std::wstring> names;
    {
        Expr e;
        std::wstring err;
        if (m_doc.model != ModelKind::Spline && e.Parse(m_doc.Equation(), {L"x"}, err)) names = e.Params();
    }
    TableDialogData d;
    d.title = L"Batch fit results";
    d.headers = {L"Data set", L"Points", L"R\xB2", L"Adj. R\xB2", L"RMSE"};
    for (const std::wstring& n : names) {
        d.headers.push_back(n);
        d.headers.push_back(n + L" std. error");
    }
    d.headers.push_back(L"Note");

    auto addRow = [&](const std::wstring& source, const Points& p) {
        std::vector<double> x, y, w;
        for (size_t i = 0; i < p.x.size(); ++i) {
            if (p.excluded[i]) continue;
            x.push_back(p.x[i]);
            y.push_back(p.y[i]);
            w.push_back(p.w[i]);
        }
        std::vector<std::wstring> row = {source, std::to_wstring(x.size())};
        FitRun run;
        if (!x.empty())
            run = FitModel(m_doc.model, m_doc.order, m_doc.customEquation, m_doc.params, m_doc.robust,
                           m_settings.confidence, x, y, w);
        else
            run.error = L"no numbers";
        const int dg = m_settings.digits;
        if (run.ok && !run.spline) {
            row.push_back(FormatNumber(run.r.r2, dg));
            row.push_back(FormatNumber(run.r.adjR2, dg));
            row.push_back(FormatNumber(run.r.rmse, dg));
            for (size_t k = 0; k < names.size(); ++k) {
                row.push_back(k < run.r.p.size() ? FormatNumber(run.r.p[k], dg) : L"");
                row.push_back(k < run.r.se.size() ? FormatNumber(run.r.se[k], 3) : L"");
            }
            row.push_back(run.r.converged ? L"" : L"did not settle");
        } else {
            for (size_t k = 0; k < 3 + 2 * names.size(); ++k) row.push_back(L"");
            row.push_back(run.ok ? L"spline: no parameters" : run.error);
        }
        d.rows.push_back(std::move(row));
    };

    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const Table& t = m_doc.table;
    if (choice.columns) {
        for (int c = 0; c < t.Cols(); ++c) {
            if (c == m_doc.xCol || c == m_doc.sigmaCol || !t.IsNumericColumn(c)) continue;
            addRow(t.names[(size_t)c], GatherPoints(t, m_doc.xCol, c, -1, &m_doc.excluded));
        }
        d.info = L"Each number column of the table fitted as Y against " +
                 (m_doc.xCol < t.Cols() ? t.names[(size_t)m_doc.xCol] : std::wstring(L"x")) +
                 L". Errors (\x03C3) are not used here.";
    } else {
        const std::wstring xName = m_doc.xCol < t.Cols() ? t.names[(size_t)m_doc.xCol] : L"";
        const std::wstring yName = m_doc.yCol < t.Cols() ? t.names[(size_t)m_doc.yCol] : L"";
        for (const std::wstring& f : choice.files) {
            std::wstring text, err;
            Table ft;
            if (!Import::ReadTextFile(f, text) || !Import::ParseText(text, ft, err)) {
                d.rows.push_back({FileName(f), L"0"});
                for (size_t k = 0; k < 3 + 2 * names.size(); ++k) d.rows.back().push_back(L"");
                d.rows.back().push_back(L"could not be read");
                continue;
            }
            auto find = [&](const std::wstring& name, int fallback) {
                for (int c = 0; c < ft.Cols(); ++c)
                    if (_wcsicmp(ft.names[(size_t)c].c_str(), name.c_str()) == 0) return c;
                return fallback < ft.Cols() ? fallback : ft.Cols() - 1;
            };
            addRow(FileName(f), GatherPoints(ft, find(xName, m_doc.xCol), find(yName, m_doc.yCol), -1, nullptr));
        }
        d.info = L"Each file fitted with the X column \x201C" + xName + L"\x201D and the Y column \x201C" + yName +
                 L"\x201D (or the same positions when the names differ).";
    }
    SetCursor(old);
    if (d.rows.empty()) {
        MessageBoxW(m_hwnd, L"There was nothing to fit: the table has no other number columns.", APP_NAME,
                    MB_ICONINFORMATION);
        return;
    }
    ShowTableDialog(m_inst, m_hwnd, d, m_settings.lastFolder);
}

// ===========================================================================
// Export
// ===========================================================================
void MainWindow::ExportFittedCsv() {
    if (!m_fit.ok) return;
    std::wstring name = Stem(m_path) + L" fitted.csv";
    if (!PickSaveFile(m_hwnd, L"CSV file (*.csv)\0*.csv\0All files\0*.*\0", L"csv", m_settings.lastFolder, name)) return;
    const Table& t = m_doc.table;
    const std::wstring xn = t.names[(size_t)m_doc.xCol], yn = t.names[(size_t)m_doc.yCol];
    const bool bands = !m_fit.spline;
    const std::wstring pct = std::to_wstring(m_settings.confidence) + L"%";
    std::vector<std::wstring> headers = {xn, yn, L"Fitted", L"Residual"};
    if (bands)
        for (const wchar_t* h : {L"confidence low", L"confidence high", L"prediction low", L"prediction high"})
            headers.push_back(pct + L" " + h);
    headers.push_back(L"Used");
    std::vector<std::vector<std::wstring>> rows;
    for (size_t i = 0; i < m_points.x.size(); ++i) {
        const double x = m_points.x[i], y = m_points.y[i], f = m_fit.Eval(x);
        std::vector<std::wstring> row = {FormatExact(x), FormatExact(y), FormatExact(f), FormatExact(y - f)};
        if (bands) {
            double yy, conf, pred;
            if (Fit::Bands(*m_fit.expr, m_fit.r, x, yy, conf, pred))
                for (double v : {f - conf, f + conf, f - pred, f + pred}) row.push_back(FormatExact(v));
            else
                for (int k = 0; k < 4; ++k) row.push_back(L"");
        }
        row.push_back(m_points.excluded[i] ? L"no" : L"yes");
        rows.push_back(std::move(row));
    }
    if (Import::WriteTextFile(name, JoinCsv(headers, rows))) SetOp(L"Saved " + FileName(name) + L".");
    else MessageBoxW(m_hwnd, L"The file could not be saved.", APP_NAME, MB_ICONWARNING);
}

void MainWindow::ExportChart(bool svg) {
    std::wstring name = Stem(m_path) + (svg ? L" graph.svg" : L" graph.png");
    if (!PickSaveFile(m_hwnd, svg ? L"SVG image (*.svg)\0*.svg\0" : L"PNG image (*.png)\0*.png\0", svg ? L"svg" : L"png",
                      m_settings.lastFolder, name))
        return;
    RECT rc;
    GetClientRect(m_chart.Hwnd(), &rc);
    const double ratio = rc.right > 0 ? (double)rc.bottom / rc.right : 0.62;
    const int w = svg ? 1000 : 2000;
    const int h = std::max(300, (int)(w * std::min(std::max(ratio, 0.4), 1.2)));
    const bool ok = svg ? m_chart.SaveSvg(name, w, h) : m_chart.SavePng(name, w, h);
    if (ok) SetOp(L"Saved " + FileName(name) + L".");
    else MessageBoxW(m_hwnd, L"The image could not be saved.", APP_NAME, MB_ICONWARNING);
}

void MainWindow::CopyResults() {
    CopyToClipboard(m_hwnd, GetWindowString(m_results));
    SetOp(L"Results copied.");
}

void MainWindow::SaveTableCsv() {
    std::wstring name = Stem(m_path) + L".csv";
    if (!PickSaveFile(m_hwnd, L"CSV file (*.csv)\0*.csv\0All files\0*.*\0", L"csv", m_settings.lastFolder, name)) return;
    if (Import::WriteTextFile(name, JoinCsv(m_doc.table.names, m_doc.table.rows))) SetOp(L"Saved " + FileName(name) + L".");
    else MessageBoxW(m_hwnd, L"The file could not be saved.", APP_NAME, MB_ICONWARNING);
}

void MainWindow::SaveReport() {
    if (!m_fit.ok) return;
    std::wstring name = Stem(m_path) + L" report.html";
    if (!PickSaveFile(m_hwnd, L"Web page (*.html)\0*.html\0", L"html", m_settings.lastFolder, name)) return;
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const Table& t = m_doc.table;
    const int d = m_settings.digits;
    const ModelInfo& info = Models::Info(m_doc.model);
    wchar_t date[64];
    const time_t now = time(nullptr);
    tm local{};
    localtime_s(&local, &now);
    wcsftime(date, 64, L"%Y-%m-%d %H:%M", &local);

    std::wstring h = L"<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>" +
                     Html(Stem(m_path)) + L" \x2014 curve fit report</title>\n<style>"
                     L"body{font-family:'Segoe UI',Arial,sans-serif;margin:32px auto;max-width:1000px;color:#222;padding:0 16px}"
                     L"h1{font-weight:600}h2{margin-top:28px;font-weight:600;border-bottom:1px solid #ddd;padding-bottom:4px}"
                     L"table{border-collapse:collapse;margin:8px 0}td,th{padding:4px 12px;border-bottom:1px solid #e3e3e3;text-align:right}"
                     L"th{background:#f4f4f4}td:first-child,th:first-child{text-align:left}"
                     L".eq{font-family:Consolas,monospace;background:#f6f6f6;padding:8px 12px;border-radius:6px;margin:6px 0}"
                     L"img{max-width:100%;border:1px solid #ddd}.dim{color:#777}</style></head><body>\n";
    h += L"<h1>Curve fit report</h1>\n<p class=\"dim\">" + Html(m_path.empty() ? L"Untitled data" : FileName(m_path)) +
         L" \x00B7 " + date + L"</p>\n";
    h += L"<h2>Model</h2>\n<p>" + Html(info.name) + L" \x2014 Y: <b>" + Html(t.names[(size_t)m_doc.yCol]) +
         L"</b>, X: <b>" + Html(t.names[(size_t)m_doc.xCol]) + L"</b></p>\n";
    if (!m_fit.spline) {
        h += L"<div class=\"eq\">" + Html(m_doc.Equation()) + L"</div>\n<div class=\"eq\">" + Html(FittedEquation(false)) +
             L"</div>\n";
        if (*info.about && m_doc.model != ModelKind::Custom) h += L"<p class=\"dim\">" + Html(info.about) + L"</p>\n";
    }
    RECT rc;
    GetClientRect(m_chart.Hwnd(), &rc);
    const double ratio = rc.right > 0 ? std::min(std::max((double)rc.bottom / rc.right, 0.45), 1.0) : 0.62;
    const std::string png = m_chart.PngBytes(1800, (int)(1800 * ratio));
    if (!png.empty()) {
        const std::string b64 = Base64(png);
        h += L"<h2>Graph</h2>\n<img alt=\"Graph of the data and the fitted curve\" src=\"data:image/png;base64,";
        h += std::wstring(b64.begin(), b64.end());
        h += L"\">\n";
    }
    if (!m_fit.spline) {
        const FitResult& r = m_fit.r;
        const std::wstring pct = std::to_wstring(m_settings.confidence) + L"%";
        h += L"<h2>Parameters</h2>\n<table><tr><th>Parameter</th><th>Value</th><th>Std. error</th><th>" + pct +
             L" confidence interval</th></tr>\n";
        for (size_t i = 0; i < r.p.size(); ++i) {
            h += L"<tr><td>" + Html(r.names[i]) + L"</td><td>" + FormatNumber(r.p[i], d) + L"</td><td>" +
                 (r.fixed[i] ? std::wstring(L"fixed") : FormatNumber(r.se[i], 3)) + L"</td><td>" +
                 (r.fixed[i] || !std::isfinite(r.ciLo[i]) ? std::wstring(L"")
                                                         : FormatNumber(r.ciLo[i], d) + L" to " + FormatNumber(r.ciHi[i], d)) +
                 L"</td></tr>\n";
        }
        h += L"</table>\n<h2>Goodness of fit</h2>\n<table>\n";
        auto stat = [&](const wchar_t* n, double v) {
            h += std::wstring(L"<tr><td>") + n + L"</td><td>" + FormatNumber(v, d) + L"</td></tr>\n";
        };
        stat(L"R\xB2", r.r2);
        stat(L"Adjusted R\xB2", r.adjR2);
        stat(L"RMSE", r.rmse);
        stat(L"Std. error of fit", std::sqrt(r.sigma2));
        stat(L"Sum of squares", r.sse);
        stat(L"AICc", r.aicc);
        stat(L"BIC", r.bic);
        h += L"<tr><td>Points used</td><td>" + std::to_wstring(r.n) + L"</td></tr>\n<tr><td>Degrees of freedom</td><td>" +
             std::to_wstring(r.dof) + L"</td></tr>\n</table>\n";
        if (m_doc.robust) h += L"<p>Robust fitting was used: outliers were ignored.</p>\n";
        if (m_doc.sigmaCol >= 0)
            h += L"<p>Points were weighted by 1/\x03C3\xB2 from the column \x201C" + Html(t.names[(size_t)m_doc.sigmaCol]) +
                 L"\x201D.</p>\n";
    }
    h += L"<h2>Data</h2>\n<table><tr><th>#</th><th>" + Html(t.names[(size_t)m_doc.xCol]) + L"</th><th>" +
         Html(t.names[(size_t)m_doc.yCol]) + L"</th><th>Fitted</th><th>Residual</th><th>Used</th></tr>\n";
    const size_t limit = std::min<size_t>(m_points.x.size(), 5000);
    for (size_t i = 0; i < limit; ++i) {
        const double x = m_points.x[i], y = m_points.y[i], f = m_fit.Eval(x);
        h += L"<tr><td>" + std::to_wstring(m_points.rows[i] + 1) + L"</td><td>" + FormatNumber(x, d) + L"</td><td>" +
             FormatNumber(y, d) + L"</td><td>" + FormatNumber(f, d) + L"</td><td>" + FormatNumber(y - f, d) + L"</td><td>" +
             (m_points.excluded[i] ? L"no" : L"yes") + L"</td></tr>\n";
    }
    h += L"</table>\n";
    if (m_points.x.size() > limit)
        h += L"<p class=\"dim\">The first " + std::to_wstring(limit) + L" of " + std::to_wstring(m_points.x.size()) +
             L" points are listed.</p>\n";
    h += L"<p class=\"dim\">Made with " APP_NAME L" " APP_VERSION L".</p>\n</body></html>\n";
    const bool ok = Import::WriteTextFile(name, h);
    SetCursor(old);
    if (!ok) {
        MessageBoxW(m_hwnd, L"The report could not be saved.", APP_NAME, MB_ICONWARNING);
        return;
    }
    SetOp(L"Saved " + FileName(name) + L".");
    if (MessageBoxW(m_hwnd, L"The report was saved. Open it now?\n\nTip: print it from the browser to make a PDF.",
                    APP_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ShellExecuteW(m_hwnd, L"open", name.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ===========================================================================
// Settings, About, theme
// ===========================================================================
void MainWindow::ShowSettings() {
    Settings edited = m_settings;
    if (!ShowSettingsDialog(m_inst, m_hwnd, edited)) return;
    const bool themeChanged = edited.themeMode != m_settings.themeMode;
    m_settings.themeMode = edited.themeMode;
    m_settings.digits = edited.digits;
    m_settings.confidence = edited.confidence;
    m_settings.autoFit = edited.autoFit;
    m_settings.gridLines = edited.gridLines;
    m_settings.Save();
    if (themeChanged) ApplyTheme();
    RunFit();
}

void MainWindow::ShowAbout() {
    const std::wstring text = APP_NAME L"  " APP_VERSION L"\n\n" APP_COPYRIGHT L".\n"
                              L"Developed for faster experience.\n\n"
                              L"Fit curves to your data: 20 built-in models or your own equation, with parameter "
                              L"errors, confidence and prediction bands, residuals, best-fit ranking, predictions, "
                              L"batch fitting and reports.";
    MessageBoxW(m_hwnd, text.c_str(), L"About " APP_NAME, MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ShowShortcuts() {
    MessageBoxW(m_hwnd,
                L"Files\n"
                L"   Ctrl+N  New        Ctrl+O  Open        Ctrl+S  Save        Ctrl+Shift+S  Save as\n\n"
                L"Fitting\n"
                L"   F5  Fit now        Ctrl+M  Model        Ctrl+B  Best fit\n"
                L"   Ctrl+P  Parameters        Ctrl+R  Predict and calculate\n\n"
                L"Table\n"
                L"   Arrows  move        Enter or F2  edit        Esc  cancel        Delete  clear cells\n"
                L"   Insert  insert a row        Ctrl+Delete  delete rows        Ctrl+E  leave out / use rows\n"
                L"   Ctrl+C / Ctrl+X / Ctrl+V  copy, cut, paste        Ctrl+Shift+V  paste as a new table\n"
                L"   Ctrl+Z / Ctrl+Y  undo, redo        Ctrl+A  select all\n\n"
                L"Graph\n"
                L"   Wheel  zoom (Shift: only x, Ctrl: only y)        Drag  move        Ctrl+drag  zoom into a box\n"
                L"   Click a point  leave it out / use it        Double-click or Ctrl+0  show everything\n\n"
                L"Export\n"
                L"   Ctrl+Shift+E  fitted values (CSV)        Ctrl+Shift+C  copy the graph        Ctrl+Shift+R  report\n\n"
                L"Elsewhere\n"
                L"   Ctrl+1  table        Ctrl+2  equation        Ctrl+,  Settings        F1  About",
                L"Keyboard shortcuts", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ApplyTheme() {
    ReloadTheme((ThemeMode)m_settings.themeMode);
    ApplyWindowTheme(m_hwnd);
    m_toolbar.OnThemeChanged();
    m_setup.OnThemeChanged();
    m_status.OnThemeChanged();
    m_grid.OnThemeChanged();
    m_chart.OnThemeChanged();
    if (m_monoFont) DeleteObject(m_monoFont);
    m_monoFont = CreateFontW(-Dpi(13, m_dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(m_results, WM_SETFONT, (WPARAM)m_monoFont, TRUE);
    SetWindowTheme(m_results, CurrentTheme().dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

// ===========================================================================
// Examples
// ===========================================================================
void MainWindow::LoadExample(int index) {
    std::mt19937 rng(42 + (unsigned)index);
    std::normal_distribution<double> noise(0, 1);
    Document d;
    std::vector<std::vector<double>> cols;
    std::wstring title;
    auto add = [&](double x, double y) {
        cols.push_back({x, y});
    };
    switch (index) {
        case 0:
            title = L"Radioactive decay";
            d.table.names = {L"Time (min)", L"Counts"};
            for (double t = 0; t <= 20.001; t += 0.5) add(t, 100 * std::exp(-0.23 * t) + 5 + 2 * noise(rng));
            d.model = ModelKind::ExpOffset;
            break;
        case 1:
            title = L"Spectrum peak";
            d.table.names = {L"Wavelength (nm)", L"Absorbance"};
            for (double x = 400; x <= 600.001; x += 4)
                add(x, 0.8 * std::exp(-std::pow((x - 505) / 18, 2) / 2) + 0.12 + 0.025 * noise(rng));
            d.model = ModelKind::Gaussian;
            break;
        case 2:
            title = L"Dose-response";
            d.table.names = {L"Concentration (\x00B5M)", L"Response (%)"};
            for (int i = 0; i < 12; ++i) {
                const double c = 0.01 * std::pow(10.0, i * 4.0 / 11);
                for (int rep = 0; rep < 2; ++rep)
                    add(c, 5 + (100 - 5) / (1 + std::pow(c / 1.8, 1.2)) + 3 * noise(rng));
            }
            d.model = ModelKind::DoseResponse;
            d.logX = true;
            break;
        case 3:
            title = L"Damped oscillation";
            d.table.names = {L"Time (s)", L"Displacement (mm)"};
            for (double t = 0; t <= 10.001; t += 0.1) add(t, 5 * std::exp(-0.3 * t) * std::sin(4 * t + 1) + 0.5 + 0.15 * noise(rng));
            d.model = ModelKind::DampedSine;
            break;
        case 4:
            title = L"Calibration line";
            d.table.names = {L"Concentration (mg/L)", L"Signal"};
            for (int i = 0; i <= 10; ++i) add(i, 2.1 * i + 0.4 + 0.25 * noise(rng) + (i == 7 ? 6 : 0));
            d.model = ModelKind::Linear;
            d.robust = true;
            break;
        case 5:
            title = L"Enzyme kinetics";
            d.table.names = {L"Substrate (mM)", L"Rate (\x00B5mol/min)"};
            for (double s : {0.2, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 20.0})
                add(s, 12 * s / (2.5 + s) * (1 + 0.03 * noise(rng)));
            d.model = ModelKind::MichaelisMenten;
            break;
        default:
            title = L"Two overlapping peaks";
            d.table.names = {L"Retention time (min)", L"Detector signal"};
            for (double x = 0; x <= 10.001; x += 0.1)
                add(x, 10 * std::exp(-std::pow((x - 4) / 0.5, 2) / 2) + 6 * std::exp(-std::pow((x - 5.6) / 0.7, 2) / 2) + 1 +
                           0.2 * noise(rng));
            d.model = ModelKind::GaussianPeaks;
            d.order = 2;
            break;
    }
    for (const auto& row : cols) d.table.rows.push_back({FormatNumber(row[0], 6), FormatNumber(row[1], 6)});
    m_doc = std::move(d);
    m_path.clear();
    m_isProject = false;
    m_modified = false;
    m_undo.clear();
    m_redo.clear();
    ApplyDocument(true);
    SetOp(L"Example: " + title + L". Change the model, click points to leave them out, or try Best fit (Ctrl+B).");
}
