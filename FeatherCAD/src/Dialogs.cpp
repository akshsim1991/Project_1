// Dialogs.cpp - see Dialogs.h.
#include "Dialogs.h"

#include <cstring>
#include <iterator>
#include <string_view>
#include <algorithm>

#include <commctrl.h>

#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr Units kUnitList[] = {Units::Millimetres, Units::Centimetres, Units::Metres,
                               Units::Inches,      Units::Feet,        Units::Unitless};

std::wstring GetText(HWND dlg, int id) {
    HWND h = GetDlgItem(dlg, id);
    std::wstring s((size_t)GetWindowTextLengthW(h) + 1, L'\0');
    s.resize((size_t)GetWindowTextW(h, s.data(), (int)s.size()));
    return s;
}

bool ReadPositive(HWND dlg, int id, double& out) {
    double v;
    if (!ParseNumber(WideToUtf8(GetText(dlg, id)), v) || v <= 0 || v > 1e9) {
        MessageBoxW(dlg, L"Please enter a positive number (with a dot for decimals).", APP_NAME, MB_ICONWARNING);
        SetFocus(GetDlgItem(dlg, id));
        SendDlgItemMessageW(dlg, id, EM_SETSEL, 0, -1);
        return false;
    }
    out = v;
    return true;
}

void Check(HWND dlg, int id, bool on) { CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED); }
bool Checked(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }

void InitDialog(HWND dlg) { ApplyWindowTheme(dlg); }

// ---- units ----------------------------------------------------------------
struct UnitsData {
    DrawingSettings* s;
    bool* forNew;
};

void UpdateSample(HWND dlg) {
    const int prec = (int)SendDlgItemMessageW(dlg, IDC_PRECISION_COMBO, CB_GETCURSEL, 0, 0);
    const int u = (int)SendDlgItemMessageW(dlg, IDC_UNITS_COMBO, CB_GETCURSEL, 0, 0);
    std::wstring sample = L"Example: " + FormatDecimal(1234.56789, prec < 0 ? 2 : prec);
    if (u >= 0 && kUnitList[u] != Units::Unitless) sample += std::wstring(L" ") + UnitsSymbol(kUnitList[u]);
    SetDlgItemTextW(dlg, IDC_UNITS_SAMPLE, sample.c_str());
}

INT_PTR CALLBACK UnitsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (UnitsData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (UnitsData*)lp;
            InitDialog(dlg);
            for (size_t i = 0; i < std::size(kUnitList); ++i) {
                SendDlgItemMessageW(dlg, IDC_UNITS_COMBO, CB_ADDSTRING, 0, (LPARAM)UnitsName(kUnitList[i]));
                if (kUnitList[i] == d->s->units) SendDlgItemMessageW(dlg, IDC_UNITS_COMBO, CB_SETCURSEL, i, 0);
            }
            for (int p = 0; p <= 8; ++p)
                SendDlgItemMessageW(dlg, IDC_PRECISION_COMBO, CB_ADDSTRING, 0, (LPARAM)FormatDecimal(0, p).c_str());
            SendDlgItemMessageW(dlg, IDC_PRECISION_COMBO, CB_SETCURSEL, d->s->precision, 0);
            Check(dlg, IDC_UNITS_DEFAULT, *d->forNew);
            UpdateSample(dlg);
            return TRUE;
        }
        case WM_COMMAND:
            if (HIWORD(wp) == CBN_SELCHANGE) UpdateSample(dlg);
            if (LOWORD(wp) == IDOK) {
                const int u = (int)SendDlgItemMessageW(dlg, IDC_UNITS_COMBO, CB_GETCURSEL, 0, 0);
                if (u >= 0) d->s->units = kUnitList[u];
                d->s->precision = (int)SendDlgItemMessageW(dlg, IDC_PRECISION_COMBO, CB_GETCURSEL, 0, 0);
                *d->forNew = Checked(dlg, IDC_UNITS_DEFAULT);
                EndDialog(dlg, IDOK);
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}

// ---- drafting -------------------------------------------------------------
INT_PTR CALLBACK DraftingProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (Drafting*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (Drafting*)lp;
            InitDialog(dlg);
            Check(dlg, IDC_GRID_ON, d->gridOn);
            SetDlgItemTextW(dlg, IDC_GRID_SPACING, Utf8ToWide(FormatNumber(d->gridSpacing)).c_str());
            Check(dlg, IDC_SNAP_ON, d->snapOn);
            SetDlgItemTextW(dlg, IDC_SNAP_SPACING, Utf8ToWide(FormatNumber(d->snapSpacing)).c_str());
            Check(dlg, IDC_ORTHO_ON, d->orthoOn);
            Check(dlg, IDC_OSNAP_ON, d->osnapOn);
            Check(dlg, IDC_POLAR_ON, d->polarOn);
            Check(dlg, IDC_OTRACK_ON, d->otrackOn);
            Check(dlg, IDC_DYN_ON, d->dynOn);
            for (const wchar_t* a : {L"5", L"10", L"15", L"18", L"22.5", L"30", L"45", L"90"})
                SendDlgItemMessageW(dlg, IDC_POLAR_ANGLE, CB_ADDSTRING, 0, (LPARAM)a);
            SetDlgItemTextW(dlg, IDC_POLAR_ANGLE, Utf8ToWide(FormatNumber(d->polarAngle)).c_str());
            for (int i = 0; i < kSnapModeCount; ++i) Check(dlg, IDC_OSNAP_FIRST + i, (d->osnapModes >> i) & 1);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_OSNAP_ALL:
                case IDC_OSNAP_NONE:
                    for (int i = 0; i < kSnapModeCount; ++i) Check(dlg, IDC_OSNAP_FIRST + i, LOWORD(wp) == IDC_OSNAP_ALL);
                    return TRUE;
                case IDOK: {
                    Drafting n = *d;
                    if (!ReadPositive(dlg, IDC_GRID_SPACING, n.gridSpacing) ||
                        !ReadPositive(dlg, IDC_SNAP_SPACING, n.snapSpacing))
                        return TRUE;
                    n.gridOn = Checked(dlg, IDC_GRID_ON);
                    n.snapOn = Checked(dlg, IDC_SNAP_ON);
                    n.orthoOn = Checked(dlg, IDC_ORTHO_ON);
                    n.osnapOn = Checked(dlg, IDC_OSNAP_ON);
                    n.polarOn = Checked(dlg, IDC_POLAR_ON);
                    n.otrackOn = Checked(dlg, IDC_OTRACK_ON);
                    n.dynOn = Checked(dlg, IDC_DYN_ON);
                    if (!ReadPositive(dlg, IDC_POLAR_ANGLE, n.polarAngle)) return TRUE;
                    if (n.polarAngle > 180) {
                        MessageBoxW(dlg, L"The polar angle must be 180 degrees or less.", APP_NAME, MB_ICONWARNING);
                        return TRUE;
                    }
                    if (n.orthoOn && n.polarOn) n.polarOn = false;  // ortho wins, as with F8
                    n.osnapModes = 0;
                    for (int i = 0; i < kSnapModeCount; ++i)
                        if (Checked(dlg, IDC_OSNAP_FIRST + i)) n.osnapModes |= 1u << i;
                    *d = n;
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

// ---- options --------------------------------------------------------------
struct OptionsData {
    int* minutes;
    int* keep;
    const std::wstring* folder;
};

INT_PTR CALLBACK OptionsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (OptionsData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (OptionsData*)lp;
            InitDialog(dlg);
            SetDlgItemInt(dlg, IDC_AUTOSAVE_MINUTES, (UINT)*d->minutes, FALSE);
            SetDlgItemInt(dlg, IDC_KEEP_VERSIONS, (UINT)*d->keep, FALSE);
            SetDlgItemTextW(dlg, IDC_HISTORY_FOLDER, d->folder->c_str());
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                BOOL ok1 = FALSE, ok2 = FALSE;
                const UINT m = GetDlgItemInt(dlg, IDC_AUTOSAVE_MINUTES, &ok1, FALSE);
                const UINT k = GetDlgItemInt(dlg, IDC_KEEP_VERSIONS, &ok2, FALSE);
                if (!ok1 || m > 120 || !ok2 || k < 5 || k > 500) {
                    MessageBoxW(dlg, L"Auto-save: 0 to 120 minutes. Versions: 5 to 500.", APP_NAME, MB_ICONWARNING);
                    return TRUE;
                }
                *d->minutes = (int)m;
                *d->keep = (int)k;
                EndDialog(dlg, IDOK);
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}

// ---- version lists ------------------------------------------------------------
std::wstring SizeText(uint64_t bytes) {
    wchar_t buf[32];
    if (bytes < 1024) swprintf(buf, 32, L"%llu bytes", (unsigned long long)bytes);
    else if (bytes < 1024 * 1024) swprintf(buf, 32, L"%.1f KB", bytes / 1024.0);
    else swprintf(buf, 32, L"%.1f MB", bytes / 1048576.0);
    return buf;
}

// Number of objects in a saved copy (lines that start with an object keyword).
size_t CountObjects(const std::wstring& file) {
    std::string text;
    if (!ReadWholeFile(file, text)) return 0;
    size_t n = 0, pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string_view line(text.data() + pos, end - pos);
        for (const char* k : {"line ", "circle ", "arc ", "pline "})
            if (line.compare(0, strlen(k), k) == 0) ++n;
        pos = end + 1;
    }
    return n;
}

void AddColumn(HWND list, int index, const wchar_t* title, int widthDip, int dpi) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = Dpi(widthDip, dpi);
    ListView_InsertColumn(list, index, &col);
}

void SetCell(HWND list, int row, int col, const std::wstring& text) {
    if (col == 0) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(text.c_str());
        ListView_InsertItem(list, &item);
    } else {
        ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text.c_str()));
    }
}

struct HistoryData {
    const std::wstring* name;
    const std::vector<VersionInfo>* versions;
    int chosen = -1;
};

INT_PTR CALLBACK HistoryProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (HistoryData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (HistoryData*)lp;
            InitDialog(dlg);
            const int dpi = GetWindowDpi(dlg);
            HWND list = GetDlgItem(dlg, IDC_VERSION_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddColumn(list, 0, L"Time", 180, dpi);
            AddColumn(list, 1, L"Kind", 100, dpi);
            AddColumn(list, 2, L"Objects", 80, dpi);
            AddColumn(list, 3, L"Size", 90, dpi);
            for (size_t i = 0; i < d->versions->size(); ++i) {
                const VersionInfo& v = (*d->versions)[i];
                SetCell(list, (int)i, 0, FormatFileTime(v.time));
                SetCell(list, (int)i, 1, v.autosave ? L"Auto-saved" : L"Saved");
                SetCell(list, (int)i, 2, std::to_wstring(CountObjects(v.file)));
                SetCell(list, (int)i, 3, SizeText(v.bytes));
            }
            std::wstring info = d->versions->empty()
                                    ? L"No versions of \x201C" + *d->name +
                                          L"\x201D yet. A version is kept each time you save, and every few "
                                          L"minutes while you work."
                                    : L"Versions of \x201C" + *d->name +
                                          L"\x201D, newest first. Restoring replaces the drawing; Undo (Ctrl+Z) "
                                          L"brings back what was there.";
            SetDlgItemTextW(dlg, IDC_VERSION_INFO, info.c_str());
            EnableWindow(GetDlgItem(dlg, IDC_RESTORE), !d->versions->empty());
            if (!d->versions->empty()) ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            return TRUE;
        }
        case WM_NOTIFY: {
            const auto* nm = (const NMHDR*)lp;
            if (nm->idFrom == IDC_VERSION_LIST && nm->code == NM_DBLCLK) PostMessageW(dlg, WM_COMMAND, IDC_RESTORE, 0);
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_RESTORE) {
                const int sel = ListView_GetNextItem(GetDlgItem(dlg, IDC_VERSION_LIST), -1, LVNI_SELECTED);
                if (sel >= 0) {
                    d->chosen = sel;
                    EndDialog(dlg, IDOK);
                }
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}

struct RecoverData {
    const std::vector<HistoryDrawing>* drawings;
    bool afterCrash;
    int chosen = -1;
};

INT_PTR CALLBACK RecoverProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (RecoverData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (RecoverData*)lp;
            InitDialog(dlg);
            const int dpi = GetWindowDpi(dlg);
            HWND list = GetDlgItem(dlg, IDC_VERSION_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            AddColumn(list, 0, L"Drawing", 150, dpi);
            AddColumn(list, 1, L"Last copy", 150, dpi);
            AddColumn(list, 2, L"Kind", 80, dpi);
            AddColumn(list, 3, L"Folder", 200, dpi);
            for (size_t i = 0; i < d->drawings->size(); ++i) {
                const HistoryDrawing& h = (*d->drawings)[i];
                SetCell(list, (int)i, 0, h.title);
                SetCell(list, (int)i, 1, FormatFileTime(h.latest.time));
                SetCell(list, (int)i, 2, h.latest.autosave ? L"Auto-saved" : L"Saved");
                SetCell(list, (int)i, 3, h.source.empty() ? L"(never saved)" : DirectoryFromPath(h.source));
            }
            SetDlgItemTextW(dlg, IDC_VERSION_INFO,
                            d->afterCrash ? L"Feather CAD did not close properly last time. These drawings have "
                                            L"auto-saved copies; open one to get your work back."
                                          : L"Drawings with auto-saved copies, newest first. The copy opens as a new "
                                            L"drawing: save it to keep it.");
            if (!d->drawings->empty()) ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            EnableWindow(GetDlgItem(dlg, IDOK), !d->drawings->empty());
            return TRUE;
        }
        case WM_NOTIFY: {
            const auto* nm = (const NMHDR*)lp;
            if (nm->idFrom == IDC_VERSION_LIST && nm->code == NM_DBLCLK) PostMessageW(dlg, WM_COMMAND, IDOK, 0);
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                const int sel = ListView_GetNextItem(GetDlgItem(dlg, IDC_VERSION_LIST), -1, LVNI_SELECTED);
                if (sel >= 0) {
                    d->chosen = sel;
                    EndDialog(dlg, IDOK);
                }
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}

// ---- quick select ---------------------------------------------------------------
struct QSelectData {
    SelectQuery* q;
    QSelectMode* mode;
};

constexpr EntityType kQsTypes[] = {EntityType::Line,    EntityType::Circle,  EntityType::Arc,
                                   EntityType::Polyline, EntityType::Ellipse, EntityType::Spline,
                                   EntityType::Point,   EntityType::Array};

INT_PTR CALLBACK QSelectProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (QSelectData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (QSelectData*)lp;
            InitDialog(dlg);
            SendDlgItemMessageW(dlg, IDC_QS_TYPE, CB_ADDSTRING, 0, (LPARAM)L"Any object");
            for (EntityType t : kQsTypes) SendDlgItemMessageW(dlg, IDC_QS_TYPE, CB_ADDSTRING, 0, (LPARAM)TypeName(t));
            int typeIndex = 0;
            for (size_t i = 0; i < std::size(kQsTypes); ++i)
                if ((int)kQsTypes[i] == d->q->type) typeIndex = (int)i + 1;
            SendDlgItemMessageW(dlg, IDC_QS_TYPE, CB_SETCURSEL, typeIndex, 0);
            for (int i = 0; i < kQPropCount; ++i)
                SendDlgItemMessageW(dlg, IDC_QS_PROP, CB_ADDSTRING, 0, (LPARAM)QPropName((QProp)i));
            SendDlgItemMessageW(dlg, IDC_QS_PROP, CB_SETCURSEL, (int)d->q->prop, 0);
            for (const wchar_t* op : {L"= equal to", L"<> not equal to", L"> greater than", L"< less than"})
                SendDlgItemMessageW(dlg, IDC_QS_OP, CB_ADDSTRING, 0, (LPARAM)op);
            SendDlgItemMessageW(dlg, IDC_QS_OP, CB_SETCURSEL, (int)d->q->op, 0);
            SetDlgItemTextW(dlg, IDC_QS_VALUE, Utf8ToWide(FormatNumber(d->q->value)).c_str());
            CheckRadioButton(dlg, IDC_QS_REPLACE, IDC_QS_REMOVE,
                             *d->mode == QSelectMode::Add ? IDC_QS_ADD : *d->mode == QSelectMode::Remove ? IDC_QS_REMOVE : IDC_QS_REPLACE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                SelectQuery q;
                const int t = (int)SendDlgItemMessageW(dlg, IDC_QS_TYPE, CB_GETCURSEL, 0, 0);
                q.type = t <= 0 ? -1 : (int)kQsTypes[t - 1];
                q.prop = (QProp)std::max(0, (int)SendDlgItemMessageW(dlg, IDC_QS_PROP, CB_GETCURSEL, 0, 0));
                q.op = (QOp)std::max(0, (int)SendDlgItemMessageW(dlg, IDC_QS_OP, CB_GETCURSEL, 0, 0));
                if (q.prop != QProp::Any && !ParseNumber(WideToUtf8(GetText(dlg, IDC_QS_VALUE)), q.value)) {
                    MessageBoxW(dlg, L"Please enter a number (with a dot for decimals).", APP_NAME, MB_ICONWARNING);
                    return TRUE;
                }
                *d->q = q;
                *d->mode = Checked(dlg, IDC_QS_ADD) ? QSelectMode::Add
                           : Checked(dlg, IDC_QS_REMOVE) ? QSelectMode::Remove
                                                         : QSelectMode::Replace;
                EndDialog(dlg, IDOK);
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK AboutProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            InitDialog(dlg);
            HFONT big = CreateMessageFont(GetWindowDpi(dlg), 160);
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)big);
            SendDlgItemMessageW(dlg, IDC_ABOUT_NAME, WM_SETFONT, (WPARAM)big, TRUE);
            SetDlgItemTextW(dlg, IDC_ABOUT_NAME, APP_NAME);
            SetDlgItemTextW(dlg, IDC_ABOUT_VERSION, L"Version " APP_VERSION);
            SetDlgItemTextW(dlg, IDC_ABOUT_TEXT,
                            L"A lightweight 2D drafting program for Windows, made for learning and "
                            L"academic use.\n\n"
                            L"Created by: " APP_AUTHOR L"\n" APP_COPYRIGHT L"\n\n"
                            L"Drawings are saved as .fcad text files and can be exported as DXF for "
                            L"other CAD programs.");
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                DeleteObject((HFONT)GetWindowLongPtrW(dlg, DWLP_USER));
                EndDialog(dlg, IDOK);
            }
            return TRUE;
    }
    return FALSE;
}
}  // namespace

bool UnitsDialog(HWND owner, DrawingSettings& s, bool& forNew) {
    UnitsData d{&s, &forNew};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_UNITS), owner, UnitsProc, (LPARAM)&d) == IDOK;
}

bool DraftingDialog(HWND owner, Drafting& d) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DRAFTING), owner, DraftingProc,
                           (LPARAM)&d) == IDOK;
}

bool OptionsDialog(HWND owner, int& autosaveMinutes, int& keepVersions, const std::wstring& folder) {
    OptionsData d{&autosaveMinutes, &keepVersions, &folder};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_OPTIONS), owner, OptionsProc,
                           (LPARAM)&d) == IDOK;
}

int HistoryDialog(HWND owner, const std::wstring& drawingName, const std::vector<VersionInfo>& versions) {
    HistoryData d{&drawingName, &versions};
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_HISTORY), owner, HistoryProc, (LPARAM)&d);
    return d.chosen;
}

int RecoverDialog(HWND owner, const std::vector<HistoryDrawing>& drawings, bool afterCrash) {
    RecoverData d{&drawings, afterCrash};
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RECOVER), owner, RecoverProc, (LPARAM)&d);
    return d.chosen;
}

bool QSelectDialog(HWND owner, SelectQuery& query, QSelectMode& mode) {
    QSelectData d{&query, &mode};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_QSELECT), owner, QSelectProc, (LPARAM)&d) == IDOK;
}

void AboutDialog(HWND owner) {
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutProc, 0);
}
