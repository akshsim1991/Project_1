// Dialogs.cpp - the dialog boxes.
#include "Dialogs.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <commctrl.h>
#include <commdlg.h>

#include "Data.h"
#include "NumFormat.h"
#include "Util.h"
#include "resource.h"

namespace {
std::wstring ItemText(HWND dlg, int id) { return GetWindowString(GetDlgItem(dlg, id)); }

// Empty -> `empty`; a number -> it; otherwise false.
bool ReadOptional(HWND dlg, int id, double empty, double& out) {
    const std::wstring s = ItemText(dlg, id);
    bool blank = true;
    for (wchar_t c : s)
        if (!iswspace(c)) blank = false;
    if (blank) {
        out = empty;
        return true;
    }
    std::wstring t = s;
    std::replace(t.begin(), t.end(), L',', L'.');
    return ParseNumber(t, out);
}

std::wstring OptionalText(double v) { return std::isfinite(v) ? FormatExact(v) : L""; }

void AddColumn(HWND list, int index, const wchar_t* title, int widthDu, HWND dlg) {
    RECT r = {0, 0, widthDu, 0};
    MapDialogRect(dlg, &r);
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.cx = r.right;
    c.pszText = (LPWSTR)title;
    ListView_InsertColumn(list, index, &c);
}

void SetCell(HWND list, int row, int col, const std::wstring& text) {
    if (col == 0) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = row;
        it.pszText = (LPWSTR)text.c_str();
        if (row >= ListView_GetItemCount(list)) ListView_InsertItem(list, &it);
        else ListView_SetItemText(list, row, 0, (LPWSTR)text.c_str());
    } else {
        ListView_SetItemText(list, row, col, (LPWSTR)text.c_str());
    }
}

std::wstring Escape(const std::wstring& s, wchar_t sep) { return Import::ToCsvField(s, sep); }
}  // namespace

std::wstring JoinTsv(const std::vector<std::wstring>& headers, const std::vector<std::vector<std::wstring>>& rows) {
    std::wstring out;
    for (size_t c = 0; c < headers.size(); ++c) out += (c ? L"\t" : L"") + headers[c];
    out += L"\r\n";
    for (const auto& r : rows) {
        for (size_t c = 0; c < r.size(); ++c) out += (c ? L"\t" : L"") + r[c];
        out += L"\r\n";
    }
    return out;
}

std::wstring JoinCsv(const std::vector<std::wstring>& headers, const std::vector<std::vector<std::wstring>>& rows) {
    std::wstring out;
    for (size_t c = 0; c < headers.size(); ++c) out += (c ? L"," : L"") + Escape(headers[c], L',');
    out += L"\r\n";
    for (const auto& r : rows) {
        for (size_t c = 0; c < r.size(); ++c) out += (c ? L"," : L"") + Escape(r[c], L',');
        out += L"\r\n";
    }
    return out;
}

// ===========================================================================
// File pickers
// ===========================================================================
bool PickOpenFiles(HWND owner, const wchar_t* filter, std::wstring& folder, std::vector<std::wstring>& out,
                   bool multiple) {
    std::vector<wchar_t> buf(65536, L'\0');
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                (multiple ? OFN_ALLOWMULTISELECT : 0);
    if (!GetOpenFileNameW(&ofn)) return false;
    out.clear();
    const std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (!multiple || *p == 0) {
        out.push_back(first);
    } else {
        while (*p) {
            out.push_back(first + L"\\" + p);
            p += wcslen(p) + 1;
        }
    }
    if (!out.empty()) folder = DirectoryFromPath(out[0]);
    return !out.empty();
}

bool PickSaveFile(HWND owner, const wchar_t* filter, const wchar_t* ext, std::wstring& folder, std::wstring& name) {
    std::vector<wchar_t> buf(32768, L'\0');
    wcsncpy_s(buf.data(), buf.size(), name.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return false;
    name = buf.data();
    folder = DirectoryFromPath(name);
    return true;
}

// ===========================================================================
// Settings
// ===========================================================================
namespace {
struct SettingsCtx {
    Settings* s;
};

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = (SettingsCtx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = (SettingsCtx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)ctx);
            const Settings& s = *ctx->s;
            HWND theme = GetDlgItem(dlg, IDC_SE_THEME);
            for (const wchar_t* t : {L"Same as Windows", L"Light", L"Dark"}) SendMessageW(theme, CB_ADDSTRING, 0, (LPARAM)t);
            SendMessageW(theme, CB_SETCURSEL, (WPARAM)s.themeMode, 0);
            HWND digits = GetDlgItem(dlg, IDC_SE_DIGITS);
            for (int d = 3; d <= 12; ++d) SendMessageW(digits, CB_ADDSTRING, 0, (LPARAM)std::to_wstring(d).c_str());
            SendMessageW(digits, CB_SETCURSEL, (WPARAM)(s.digits - 3), 0);
            HWND conf = GetDlgItem(dlg, IDC_SE_CONF);
            for (const wchar_t* t : {L"90%", L"95% (usual)", L"99%"}) SendMessageW(conf, CB_ADDSTRING, 0, (LPARAM)t);
            SendMessageW(conf, CB_SETCURSEL, s.confidence == 90 ? 0 : s.confidence == 99 ? 2 : 1, 0);
            CheckDlgButton(dlg, IDC_SE_AUTOFIT, s.autoFit ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_SE_GRID, s.gridLines ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                Settings& s = *ctx->s;
                s.themeMode = (int)SendDlgItemMessageW(dlg, IDC_SE_THEME, CB_GETCURSEL, 0, 0);
                s.digits = 3 + (int)SendDlgItemMessageW(dlg, IDC_SE_DIGITS, CB_GETCURSEL, 0, 0);
                const int c = (int)SendDlgItemMessageW(dlg, IDC_SE_CONF, CB_GETCURSEL, 0, 0);
                s.confidence = c == 0 ? 90 : c == 2 ? 99 : 95;
                s.autoFit = IsDlgButtonChecked(dlg, IDC_SE_AUTOFIT) == BST_CHECKED;
                s.gridLines = IsDlgButtonChecked(dlg, IDC_SE_GRID) == BST_CHECKED;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s) {
    SettingsCtx ctx{&s};
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsProc, (LPARAM)&ctx) == IDOK;
}

// ===========================================================================
// Parameters
// ===========================================================================
namespace {
struct ParamsCtx {
    std::vector<std::wstring> names;
    std::vector<double> fitted;
    std::vector<ParamOption> opts;  // one per name, same order
    int current = -1;
    bool loading = false;
};

void ParamsRow(HWND dlg, ParamsCtx* c, int i) {
    HWND list = GetDlgItem(dlg, IDC_PA_LIST);
    const ParamOption& o = c->opts[(size_t)i];
    SetCell(list, i, 0, o.name);
    SetCell(list, i, 1, o.hasInit || o.fixed ? FormatNumber(o.init, 8) : L"automatic");
    SetCell(list, i, 2, o.fixed ? L"fixed" : L"");
    SetCell(list, i, 3, std::isfinite(o.lo) ? FormatNumber(o.lo, 8) : L"");
    SetCell(list, i, 4, std::isfinite(o.hi) ? FormatNumber(o.hi, 8) : L"");
    SetCell(list, i, 5, i < (int)c->fitted.size() ? FormatNumber(c->fitted[(size_t)i], 8) : L"");
}

void ParamsLoad(HWND dlg, ParamsCtx* c) {
    c->loading = true;
    const bool any = c->current >= 0;
    for (int id : {IDC_PA_INIT, IDC_PA_LO, IDC_PA_HI, IDC_PA_FIXED}) EnableWindow(GetDlgItem(dlg, id), any);
    if (any) {
        const ParamOption& o = c->opts[(size_t)c->current];
        SetDlgItemTextW(dlg, IDC_PA_INIT, o.hasInit || o.fixed ? FormatExact(o.init).c_str() : L"");
        SetDlgItemTextW(dlg, IDC_PA_LO, OptionalText(o.lo).c_str());
        SetDlgItemTextW(dlg, IDC_PA_HI, OptionalText(o.hi).c_str());
        CheckDlgButton(dlg, IDC_PA_FIXED, o.fixed ? BST_CHECKED : BST_UNCHECKED);
    }
    c->loading = false;
}

// Reads the edit boxes into the current parameter. Returns false (and
// names the box) if something is not a number.
bool ParamsStore(HWND dlg, ParamsCtx* c, std::wstring* problem) {
    if (c->current < 0 || c->loading) return true;
    ParamOption& o = c->opts[(size_t)c->current];
    double init, lo, hi;
    const bool okInit = ReadOptional(dlg, IDC_PA_INIT, NAN, init);
    const bool okLo = ReadOptional(dlg, IDC_PA_LO, -INFINITY, lo);
    const bool okHi = ReadOptional(dlg, IDC_PA_HI, INFINITY, hi);
    if (okInit) {
        o.hasInit = std::isfinite(init);
        if (o.hasInit) o.init = init;
    }
    if (okLo) o.lo = lo;
    if (okHi) o.hi = hi;
    o.fixed = IsDlgButtonChecked(dlg, IDC_PA_FIXED) == BST_CHECKED;
    if (o.fixed && !o.hasInit) {
        // Fixing without a value: keep the last fitted value.
        o.init = c->current < (int)c->fitted.size() ? c->fitted[(size_t)c->current] : 1.0;
        o.hasInit = true;
    }
    ParamsRow(dlg, c, c->current);
    if (problem) {
        if (!okInit) *problem = L"The starting value of " + o.name + L" is not a number.";
        else if (!okLo) *problem = L"The lowest allowed value of " + o.name + L" is not a number.";
        else if (!okHi) *problem = L"The highest allowed value of " + o.name + L" is not a number.";
        else if (o.lo > o.hi) *problem = L"For " + o.name + L", the lowest allowed value is above the highest.";
    }
    return okInit && okLo && okHi && o.lo <= o.hi;
}

INT_PTR CALLBACK ParamsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (ParamsCtx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            c = (ParamsCtx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            SetDlgItemTextW(dlg, IDC_PA_INFO,
                            c->names.empty() ? L"This model has no parameters."
                                             : L"Set a starting value if the fit finds a wrong solution, fix a parameter "
                                               L"to a known value, or limit the range it may take.");
            HWND list = GetDlgItem(dlg, IDC_PA_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddColumn(list, 0, L"Parameter", 60, dlg);
            AddColumn(list, 1, L"Starting value", 64, dlg);
            AddColumn(list, 2, L"Fixed", 34, dlg);
            AddColumn(list, 3, L"Lowest", 56, dlg);
            AddColumn(list, 4, L"Highest", 56, dlg);
            AddColumn(list, 5, L"Fitted value", 80, dlg);
            for (int i = 0; i < (int)c->opts.size(); ++i) ParamsRow(dlg, c, i);
            c->current = c->opts.empty() ? -1 : 0;
            if (c->current == 0) ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ParamsLoad(dlg, c);
            EnableWindow(GetDlgItem(dlg, IDC_PA_USEFIT), !c->fitted.empty());
            return TRUE;
        }
        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lp;
            if (hdr->idFrom == IDC_PA_LIST && hdr->code == LVN_ITEMCHANGED) {
                auto* nm = (NMLISTVIEW*)lp;
                if ((nm->uNewState & LVIS_SELECTED) && nm->iItem != c->current) {
                    ParamsStore(dlg, c, nullptr);
                    c->current = nm->iItem;
                    ParamsLoad(dlg, c);
                }
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_PA_INIT:
                case IDC_PA_LO:
                case IDC_PA_HI:
                    if (HIWORD(wp) == EN_KILLFOCUS) ParamsStore(dlg, c, nullptr);
                    return TRUE;
                case IDC_PA_FIXED:
                    ParamsStore(dlg, c, nullptr);
                    ParamsLoad(dlg, c);
                    return TRUE;
                case IDC_PA_USEFIT:
                    for (size_t i = 0; i < c->opts.size() && i < c->fitted.size(); ++i) {
                        c->opts[i].hasInit = std::isfinite(c->fitted[i]);
                        if (c->opts[i].hasInit) c->opts[i].init = c->fitted[i];
                        ParamsRow(dlg, c, (int)i);
                    }
                    ParamsLoad(dlg, c);
                    return TRUE;
                case IDC_PA_RESET:
                    for (size_t i = 0; i < c->opts.size(); ++i) {
                        const std::wstring n = c->opts[i].name;
                        c->opts[i] = ParamOption{};
                        c->opts[i].name = n;
                        ParamsRow(dlg, c, (int)i);
                    }
                    ParamsLoad(dlg, c);
                    return TRUE;
                case IDOK: {
                    std::wstring problem;
                    if (!ParamsStore(dlg, c, &problem)) {
                        MessageBoxW(dlg, problem.c_str(), APP_NAME, MB_ICONWARNING);
                        return TRUE;
                    }
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

bool ShowParamsDialog(HINSTANCE inst, HWND owner, const std::vector<std::wstring>& names,
                      const std::vector<double>& fitted, std::vector<ParamOption>& options) {
    ParamsCtx c;
    c.names = names;
    c.fitted = fitted;
    for (const std::wstring& n : names) {
        ParamOption o;
        o.name = n;
        for (const ParamOption& existing : options)
            if (existing.name == n) o = existing;
        c.opts.push_back(o);
    }
    if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_PARAMS), owner, ParamsProc, (LPARAM)&c) != IDOK) return false;
    // Keep options of parameters not in this equation (they may come back).
    for (const ParamOption& o : c.opts) {
        auto it = std::find_if(options.begin(), options.end(), [&](const ParamOption& e) { return e.name == o.name; });
        const bool isDefault = !o.hasInit && !o.fixed && !std::isfinite(o.lo) && !std::isfinite(o.hi);
        if (it != options.end()) {
            if (isDefault) options.erase(it);
            else *it = o;
        } else if (!isDefault) {
            options.push_back(o);
        }
    }
    return true;
}

// ===========================================================================
// Predict
// ===========================================================================
namespace {
struct PredictCtx {
    const PredictContext* p;
};

std::wstring F(const PredictContext& p, double v) { return FormatNumber(v, p.digits); }

void PredictUpdate(HWND dlg, const PredictContext& p, int changed) {
    double v;
    if (changed == 0 || changed == IDC_PR_X) {
        std::wstring out;
        if (ReadOptional(dlg, IDC_PR_X, NAN, v) && std::isfinite(v)) {
            const double y = p.f(v);
            out = p.yName + L" = " + F(p, y);
            double conf, pred;
            if (p.bands && p.bands(v, conf, pred) && std::isfinite(conf)) {
                out += L"\n" + std::to_wstring(p.confidence) + L"% confidence: " + F(p, y - conf) + L" to " + F(p, y + conf);
                out += L"\n" + std::to_wstring(p.confidence) + L"% prediction: " + F(p, y - pred) + L" to " + F(p, y + pred);
            }
            out += L"\nSlope (dy/dx): " + F(p, Fit::Derivative(p.f, v));
        }
        SetDlgItemTextW(dlg, IDC_PR_X_OUT, out.c_str());
    }
    if (changed == 0 || changed == IDC_PR_Y) {
        std::wstring out;
        if (ReadOptional(dlg, IDC_PR_Y, NAN, v) && std::isfinite(v)) {
            const double range = std::max(p.xmax - p.xmin, 1e-12);
            const std::vector<double> roots = Fit::Solve(p.f, v, p.xmin - range, p.xmax + range);
            if (roots.empty()) {
                out = L"The curve does not reach this value between " + F(p, p.xmin - range) + L" and " +
                      F(p, p.xmax + range) + L".";
            } else {
                out = p.xName + L" = ";
                for (size_t i = 0; i < roots.size() && i < 6; ++i) out += (i ? L",  " : L"") + F(p, roots[i]);
                if (roots.size() > 6) out += L",  \x2026";
                if (roots.size() > 1) out += L"\n(" + std::to_wstring(roots.size()) + L" places)";
            }
        }
        SetDlgItemTextW(dlg, IDC_PR_Y_OUT, out.c_str());
    }
    if (changed == 0 || changed == IDC_PR_A || changed == IDC_PR_B) {
        std::wstring out;
        double a, b;
        if (ReadOptional(dlg, IDC_PR_A, NAN, a) && ReadOptional(dlg, IDC_PR_B, NAN, b) && std::isfinite(a) &&
            std::isfinite(b)) {
            const double area = Fit::Integral(p.f, a, b);
            out = L"Area = " + F(p, area);
            if (b != a) out += L"\nAverage value = " + F(p, area / (b - a));
        }
        SetDlgItemTextW(dlg, IDC_PR_AREA_OUT, out.c_str());
    }
}

bool PredictTable(HWND dlg, const PredictContext& p, std::vector<double>& xs, std::vector<double>& ys) {
    double a, b, n;
    if (!ReadOptional(dlg, IDC_PR_T0, NAN, a) || !ReadOptional(dlg, IDC_PR_T1, NAN, b) ||
        !ReadOptional(dlg, IDC_PR_TN, NAN, n) || !std::isfinite(a) || !std::isfinite(b) || !(n >= 2) || n > 1e6) {
        MessageBoxW(dlg, L"Enter the first and last x and the number of points (2 or more).", APP_NAME, MB_ICONINFORMATION);
        return false;
    }
    const int count = (int)n;
    xs.clear();
    ys.clear();
    for (int i = 0; i < count; ++i) {
        const double x = a + (b - a) * i / (count - 1);
        xs.push_back(x);
        ys.push_back(p.f(x));
    }
    return true;
}

INT_PTR CALLBACK PredictProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (PredictCtx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            c = (PredictCtx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            const PredictContext& p = *c->p;
            SetDlgItemTextW(dlg, IDC_PR_X, FormatNumber((p.xmin + p.xmax) / 2, 6).c_str());
            SetDlgItemTextW(dlg, IDC_PR_A, FormatNumber(p.xmin, 6).c_str());
            SetDlgItemTextW(dlg, IDC_PR_B, FormatNumber(p.xmax, 6).c_str());
            SetDlgItemTextW(dlg, IDC_PR_T0, FormatNumber(p.xmin, 6).c_str());
            SetDlgItemTextW(dlg, IDC_PR_T1, FormatNumber(p.xmax, 6).c_str());
            SetDlgItemTextW(dlg, IDC_PR_TN, L"21");
            EnableWindow(GetDlgItem(dlg, IDC_PR_TADD), (bool)p.addColumns);
            PredictUpdate(dlg, p, 0);
            SetFocus(GetDlgItem(dlg, IDC_PR_X));
            SendDlgItemMessageW(dlg, IDC_PR_X, EM_SETSEL, 0, -1);
            return FALSE;
        }
        case WM_COMMAND: {
            const PredictContext& p = *c->p;
            const int id = LOWORD(wp);
            if (HIWORD(wp) == EN_CHANGE) {
                PredictUpdate(dlg, p, id);
                return TRUE;
            }
            switch (id) {
                case IDC_PR_TCOPY:
                case IDC_PR_TADD: {
                    std::vector<double> xs, ys;
                    if (!PredictTable(dlg, p, xs, ys)) return TRUE;
                    if (id == IDC_PR_TADD) {
                        p.addColumns(xs, ys);
                        MessageBoxW(dlg, L"The table was added to the data as two new columns.", APP_NAME,
                                    MB_ICONINFORMATION);
                        return TRUE;
                    }
                    std::vector<std::vector<std::wstring>> rows;
                    for (size_t i = 0; i < xs.size(); ++i) rows.push_back({FormatExact(xs[i]), FormatExact(ys[i])});
                    CopyToClipboard(dlg, JoinTsv({p.xName, L"Fitted " + p.yName}, rows));
                    return TRUE;
                }
                case IDC_PR_COPY: {
                    std::wstring t = L"x = " + ItemText(dlg, IDC_PR_X) + L"\r\n" + ItemText(dlg, IDC_PR_X_OUT) +
                                     L"\r\n\r\ny = " + ItemText(dlg, IDC_PR_Y) + L"\r\n" + ItemText(dlg, IDC_PR_Y_OUT) +
                                     L"\r\n\r\nFrom " + ItemText(dlg, IDC_PR_A) + L" to " + ItemText(dlg, IDC_PR_B) +
                                     L"\r\n" + ItemText(dlg, IDC_PR_AREA_OUT) + L"\r\n";
                    std::wstring fixed;
                    for (wchar_t ch : t) {
                        if (ch == L'\n' && (fixed.empty() || fixed.back() != L'\r')) fixed += L'\r';
                        fixed += ch;
                    }
                    CopyToClipboard(dlg, fixed);
                    return TRUE;
                }
                case IDOK:
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
        }
    }
    return FALSE;
}
}  // namespace

void ShowPredictDialog(HINSTANCE inst, HWND owner, const PredictContext& ctx) {
    PredictCtx c{&ctx};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_PREDICT), owner, PredictProc, (LPARAM)&c);
}

// ===========================================================================
// Results table (best fit, batch results)
// ===========================================================================
namespace {
struct TableCtx {
    const TableDialogData* d;
    std::wstring* folder;
    int chosen = -1;
    SIZE minSize{};
};

void TableLayout(HWND dlg) {
    RECT rc;
    GetClientRect(dlg, &rc);
    RECT du = {0, 0, 7, 15};
    MapDialogRect(dlg, &du);
    const int m = du.right, bh = du.bottom, gap = du.right;
    HWND info = GetDlgItem(dlg, IDC_TB_INFO), list = GetDlgItem(dlg, IDC_TB_LIST);
    RECT ri;
    GetWindowRect(info, &ri);
    const int ih = ri.bottom - ri.top;
    MoveWindow(info, m, m, rc.right - 2 * m, ih, TRUE);
    MoveWindow(list, m, m + ih + gap / 4, rc.right - 2 * m, rc.bottom - (m + ih + gap / 4) - bh - 2 * m, TRUE);
    const int by = rc.bottom - m - bh;
    int x = m;
    for (int id : {IDC_TB_USE, IDC_TB_COPY, IDC_TB_SAVE}) {
        HWND b = GetDlgItem(dlg, id);
        RECT br;
        GetWindowRect(b, &br);
        if (GetWindowLongW(b, GWL_STYLE) & WS_VISIBLE) {
            MoveWindow(b, x, by, br.right - br.left, bh, TRUE);
            x += br.right - br.left + gap / 2;
        }
    }
    HWND close = GetDlgItem(dlg, IDCANCEL);
    RECT cr;
    GetWindowRect(close, &cr);
    MoveWindow(close, rc.right - m - (cr.right - cr.left), by, cr.right - cr.left, bh, TRUE);
}

INT_PTR CALLBACK TableProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (TableCtx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            c = (TableCtx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            const TableDialogData& d = *c->d;
            SetWindowTextW(dlg, d.title.c_str());
            SetDlgItemTextW(dlg, IDC_TB_INFO, d.info.c_str());
            HWND list = GetDlgItem(dlg, IDC_TB_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            for (size_t i = 0; i < d.headers.size(); ++i) {
                const bool last = i + 1 == d.headers.size() && d.headers[i] == L"Note";
                AddColumn(list, (int)i, d.headers[i].c_str(), i == 0 ? 110 : last ? 110 : 52, dlg);
            }
            for (size_t r = 0; r < d.rows.size(); ++r)
                for (size_t col = 0; col < d.rows[r].size() && col < d.headers.size(); ++col)
                    SetCell(list, (int)r, (int)col, d.rows[r][col]);
            if (!d.rows.empty()) {
                const int sel = std::min(std::max(d.selected, 0), (int)d.rows.size() - 1);
                ListView_SetItemState(list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(list, sel, FALSE);
            }
            if (!d.canUse) {
                ShowWindow(GetDlgItem(dlg, IDC_TB_USE), SW_HIDE);
                SendMessageW(dlg, DM_SETDEFID, IDCANCEL, 0);
            }
            RECT wr;
            GetWindowRect(dlg, &wr);
            c->minSize = {wr.right - wr.left, (wr.bottom - wr.top) * 2 / 3};
            TableLayout(dlg);
            SetFocus(list);
            return FALSE;
        }
        case WM_SIZE: TableLayout(dlg); return TRUE;
        case WM_GETMINMAXINFO:
            if (c) ((MINMAXINFO*)lp)->ptMinTrackSize = {c->minSize.cx * 2 / 3, c->minSize.cy};
            return TRUE;
        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lp;
            if (hdr->idFrom == IDC_TB_LIST && hdr->code == NM_DBLCLK && c->d->canUse) {
                PostMessageW(dlg, WM_COMMAND, IDC_TB_USE, 0);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_TB_USE: {
                    const int sel = ListView_GetNextItem(GetDlgItem(dlg, IDC_TB_LIST), -1, LVNI_SELECTED);
                    if (sel < 0) return TRUE;
                    c->chosen = sel;
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDC_TB_COPY: CopyToClipboard(dlg, JoinTsv(c->d->headers, c->d->rows)); return TRUE;
                case IDC_TB_SAVE: {
                    std::wstring name = L"results.csv";
                    if (!PickSaveFile(dlg, L"CSV file (*.csv)\0*.csv\0All files\0*.*\0", L"csv", *c->folder, name))
                        return TRUE;
                    if (!Import::WriteTextFile(name, JoinCsv(c->d->headers, c->d->rows)))
                        MessageBoxW(dlg, L"The file could not be saved.", APP_NAME, MB_ICONWARNING);
                    return TRUE;
                }
                case IDOK:
                    if (c->d->canUse) PostMessageW(dlg, WM_COMMAND, IDC_TB_USE, 0);
                    else EndDialog(dlg, IDCANCEL);
                    return TRUE;
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

int ShowTableDialog(HINSTANCE inst, HWND owner, const TableDialogData& data, std::wstring& folder) {
    TableCtx c{&data, &folder};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_TABLE), owner, TableProc, (LPARAM)&c);
    return c.chosen;
}

// ===========================================================================
// Batch
// ===========================================================================
namespace {
struct BatchCtx {
    std::wstring info;
    std::wstring* folder;
    BatchChoice* choice;
};

void BatchEnable(HWND dlg) {
    const bool files = IsDlgButtonChecked(dlg, IDC_BA_FILES) == BST_CHECKED;
    for (int id : {IDC_BA_LIST, IDC_BA_ADD, IDC_BA_REMOVE}) EnableWindow(GetDlgItem(dlg, id), files);
}

INT_PTR CALLBACK BatchProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (BatchCtx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            c = (BatchCtx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            SetDlgItemTextW(dlg, IDC_BA_INFO, c->info.c_str());
            CheckRadioButton(dlg, IDC_BA_COLUMNS, IDC_BA_FILES, c->choice->columns ? IDC_BA_COLUMNS : IDC_BA_FILES);
            for (const std::wstring& f : c->choice->files)
                SendDlgItemMessageW(dlg, IDC_BA_LIST, LB_ADDSTRING, 0, (LPARAM)f.c_str());
            BatchEnable(dlg);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BA_COLUMNS:
                case IDC_BA_FILES: BatchEnable(dlg); return TRUE;
                case IDC_BA_ADD: {
                    std::vector<std::wstring> files;
                    if (PickOpenFiles(dlg, L"Data files (*.csv;*.tsv;*.txt;*.dat)\0*.csv;*.tsv;*.txt;*.dat\0All files\0*.*\0",
                                      *c->folder, files, true))
                        for (const std::wstring& f : files)
                            if (SendDlgItemMessageW(dlg, IDC_BA_LIST, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)f.c_str()) ==
                                LB_ERR)
                                SendDlgItemMessageW(dlg, IDC_BA_LIST, LB_ADDSTRING, 0, (LPARAM)f.c_str());
                    return TRUE;
                }
                case IDC_BA_REMOVE: {
                    HWND list = GetDlgItem(dlg, IDC_BA_LIST);
                    for (int i = (int)SendMessageW(list, LB_GETCOUNT, 0, 0) - 1; i >= 0; --i)
                        if (SendMessageW(list, LB_GETSEL, (WPARAM)i, 0) > 0) SendMessageW(list, LB_DELETESTRING, (WPARAM)i, 0);
                    return TRUE;
                }
                case IDOK: {
                    c->choice->columns = IsDlgButtonChecked(dlg, IDC_BA_COLUMNS) == BST_CHECKED;
                    c->choice->files.clear();
                    HWND list = GetDlgItem(dlg, IDC_BA_LIST);
                    const int n = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
                    for (int i = 0; i < n; ++i) {
                        std::wstring s((size_t)SendMessageW(list, LB_GETTEXTLEN, (WPARAM)i, 0) + 1, L'\0');
                        SendMessageW(list, LB_GETTEXT, (WPARAM)i, (LPARAM)s.data());
                        s.resize(wcslen(s.c_str()));
                        c->choice->files.push_back(s);
                    }
                    if (!c->choice->columns && c->choice->files.empty()) {
                        MessageBoxW(dlg, L"Add the files to fit first.", APP_NAME, MB_ICONINFORMATION);
                        return TRUE;
                    }
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

bool ShowBatchDialog(HINSTANCE inst, HWND owner, const std::wstring& info, std::wstring& folder, BatchChoice& choice) {
    BatchCtx c{info, &folder, &choice};
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_BATCH), owner, BatchProc, (LPARAM)&c) == IDOK;
}

// ===========================================================================
// Formula column and text prompt
// ===========================================================================
namespace {
struct TextCtx {
    std::wstring title, prompt;
    std::wstring* a;
    std::wstring* b;
    std::wstring vars;
};

INT_PTR CALLBACK FormulaProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (TextCtx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            c = (TextCtx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            SetDlgItemTextW(dlg, IDC_FO_NAME, c->a->c_str());
            SetDlgItemTextW(dlg, IDC_FO_EXPR, c->b->c_str());
            SetDlgItemTextW(dlg, IDC_FO_VARS, c->vars.c_str());
            SetFocus(GetDlgItem(dlg, IDC_FO_EXPR));
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                *c->a = ItemText(dlg, IDC_FO_NAME);
                *c->b = ItemText(dlg, IDC_FO_EXPR);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK InputProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (TextCtx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            c = (TextCtx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            SetWindowTextW(dlg, c->title.c_str());
            SetDlgItemTextW(dlg, IDC_IN_PROMPT, c->prompt.c_str());
            SetDlgItemTextW(dlg, IDC_IN_TEXT, c->a->c_str());
            SetFocus(GetDlgItem(dlg, IDC_IN_TEXT));
            SendDlgItemMessageW(dlg, IDC_IN_TEXT, EM_SETSEL, 0, -1);
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                *c->a = ItemText(dlg, IDC_IN_TEXT);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

bool ShowFormulaDialog(HINSTANCE inst, HWND owner, const std::vector<std::wstring>& variables, std::wstring& name,
                       std::wstring& formula) {
    TextCtx c{L"", L"", &name, &formula, L""};
    c.vars = L"Use the columns by these names:\n";
    for (size_t i = 0; i < variables.size(); ++i) c.vars += (i ? L",  " : L"  ") + variables[i];
    c.vars += L"\nand  row  for the row number.\n\nExamples:   ln(y)      x*1000      1/x      (y - y_blank)/y_max\n"
              L"Functions: sqrt, exp, ln, log, sin, cos, tan, abs, pow(a,b), min, max, \x2026";
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_FORMULA), owner, FormulaProc, (LPARAM)&c) == IDOK;
}

bool ShowInputDialog(HINSTANCE inst, HWND owner, const std::wstring& title, const std::wstring& prompt,
                     std::wstring& text) {
    TextCtx c{title, prompt, &text, nullptr, L""};
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_INPUT), owner, InputProc, (LPARAM)&c) == IDOK;
}
