// Dialogs.cpp - see Dialogs.h.
#include "Dialogs.h"

#include <cstring>
#include <iterator>
#include <string_view>
#include <algorithm>

#include <commctrl.h>
#include <commdlg.h>

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

constexpr EntityType kQsTypes[] = {EntityType::Line,  EntityType::Circle,    EntityType::Arc,    EntityType::Polyline,
                                   EntityType::Ellipse, EntityType::Spline, EntityType::Point,   EntityType::Array,
                                   EntityType::Dimension, EntityType::Text, EntityType::Leader,  EntityType::Hatch,
                                   EntityType::Insert};

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


// ---- dimension style and one dimension -------------------------------------------
struct DimStyleData {
    DimStyle* style;
    bool* apply;
};

void SetNumber(HWND dlg, int id, double v) { SetDlgItemTextW(dlg, id, Utf8ToWide(FormatNumber(v)).c_str()); }

bool ReadNonNegative(HWND dlg, int id, double& out) {
    double v;
    if (!ParseNumber(WideToUtf8(GetText(dlg, id)), v) || v < 0 || v > 1e9) {
        MessageBoxW(dlg, L"Please enter a number of zero or more (with a dot for decimals).", APP_NAME, MB_ICONWARNING);
        SetFocus(GetDlgItem(dlg, id));
        SendDlgItemMessageW(dlg, id, EM_SETSEL, 0, -1);
        return false;
    }
    out = v;
    return true;
}

void FillDecimals(HWND dlg, int id, int current) {
    for (int p = 0; p <= 8; ++p)
        SendDlgItemMessageW(dlg, id, CB_ADDSTRING, 0, (LPARAM)FormatDecimal(0, p).c_str());
    SendDlgItemMessageW(dlg, id, CB_SETCURSEL, std::clamp(current, 0, 8), 0);
}

INT_PTR CALLBACK DimStyleProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (DimStyleData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (DimStyleData*)lp;
            InitDialog(dlg);
            const DimStyle& s = *d->style;
            SetNumber(dlg, IDC_DS_TEXT, s.textHeight);
            SetNumber(dlg, IDC_DS_ARROW, s.arrowSize);
            SetNumber(dlg, IDC_DS_EXTOFFSET, s.extOffset);
            SetNumber(dlg, IDC_DS_EXTBEYOND, s.extBeyond);
            SetNumber(dlg, IDC_DS_GAP, s.textGap);
            FillDecimals(dlg, IDC_DS_DECIMALS, s.precision);
            Check(dlg, IDC_DS_TRIM, s.trimZeros);
            Check(dlg, IDC_DS_APPLY, *d->apply);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                DimStyle s = *d->style;
                if (!ReadPositive(dlg, IDC_DS_TEXT, s.textHeight) || !ReadNonNegative(dlg, IDC_DS_ARROW, s.arrowSize) ||
                    !ReadNonNegative(dlg, IDC_DS_EXTOFFSET, s.extOffset) ||
                    !ReadNonNegative(dlg, IDC_DS_EXTBEYOND, s.extBeyond) || !ReadNonNegative(dlg, IDC_DS_GAP, s.textGap))
                    return TRUE;
                s.precision = std::max(0, (int)SendDlgItemMessageW(dlg, IDC_DS_DECIMALS, CB_GETCURSEL, 0, 0));
                s.trimZeros = Checked(dlg, IDC_DS_TRIM);
                *d->style = s;
                *d->apply = Checked(dlg, IDC_DS_APPLY);
                EndDialog(dlg, IDOK);
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}

struct DimEditData {
    const DimData* dim;
    bool attached;
    DimEditResult* out;
    std::wstring shown;  // the value as first shown
};

INT_PTR CALLBACK DimEditProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (DimEditData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (DimEditData*)lp;
            InitDialog(dlg);
            const DimData& dim = *d->dim;
            const bool angle = dim.kind == DimData::Angular;
            const double m = DimMeasure(dim);
            DimStyle exact = dim.style;
            exact.precision = 8;  // show the value as it is, not rounded
            d->shown = DimNumber(angle ? ToDegrees(m) : m, exact);
            std::wstring kind = std::wstring(DimKindName(dim.kind)) + L" dimension";
            if (angle) kind += L" (degrees)";
            SetDlgItemTextW(dlg, IDC_DE_KIND, kind.c_str());
            SetDlgItemTextW(dlg, IDC_DE_VALUE, d->shown.c_str());
            SetDlgItemTextW(dlg, IDC_DE_NOTE,
                            d->attached ? L"It is attached to the drawing: a new value changes the objects to that size."
                                        : L"It is not attached to any object, so its value cannot be changed here. "
                                          L"Change its text instead.");
            EnableWindow(GetDlgItem(dlg, IDC_DE_VALUE), d->attached);
            SetDlgItemTextW(dlg, IDC_DE_TEXT, dim.text.c_str());
            FillDecimals(dlg, IDC_DE_DECIMALS, dim.style.precision);
            if (d->attached) {
                SetFocus(GetDlgItem(dlg, IDC_DE_VALUE));
                SendDlgItemMessageW(dlg, IDC_DE_VALUE, EM_SETSEL, 0, -1);
                return FALSE;
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                DimEditResult& r = *d->out;
                const std::wstring typed = GetText(dlg, IDC_DE_VALUE);
                r.valueChanged = d->attached && typed != d->shown;
                if (r.valueChanged && !ReadPositive(dlg, IDC_DE_VALUE, r.value)) return TRUE;
                r.text = GetText(dlg, IDC_DE_TEXT);
                r.precision = std::max(0, (int)SendDlgItemMessageW(dlg, IDC_DE_DECIMALS, CB_GETCURSEL, 0, 0));
                EndDialog(dlg, IDOK);
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
    }
    return FALSE;
}


// ---- text editor -------------------------------------------------------------------
const wchar_t* const kTextSymbols[] = {L"\x00B0", L"\x00D8", L"\x00B1", L"\x2104", L"\x00B2", L"\x00B3", L"\x00B5",
                                       L"\x03A9", L"\x2248", L"\x2264", L"\x2265", L"\x2206", L"\x00D7", L"\x221A"};
UINT PopupAtCursor(HWND owner, HMENU m);

struct TextEditData {
    TextData* t;
    std::wstring title;
};

int CALLBACK AddFont(const LOGFONTW* lf, const TEXTMETRICW*, DWORD type, LPARAM lp) {
    if (!(type & TRUETYPE_FONTTYPE) || lf->lfFaceName[0] == L'@') return 1;
    HWND combo = (HWND)lp;
    if (SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)lf->lfFaceName) == CB_ERR)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);
    return 1;
}

INT_PTR CALLBACK TextEditProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (TextEditData*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (TextEditData*)lp;
            InitDialog(dlg);
            const TextData& t = *d->t;
            SetWindowTextW(dlg, d->title.c_str());
            std::wstring crlf;
            for (wchar_t c : t.content) crlf += c == L'\n' ? std::wstring(L"\r\n") : std::wstring(1, c);
            SetDlgItemTextW(dlg, IDC_TE_TEXT, crlf.c_str());
            for (int i = 0; i < (int)std::size(kTextSymbols); ++i) {
                SetDlgItemTextW(dlg, IDC_TE_SYMBOL + i, kTextSymbols[i]);
            }
            HWND fonts = GetDlgItem(dlg, IDC_TE_FONT);
            HDC dc = GetDC(dlg);
            LOGFONTW lf{};
            lf.lfCharSet = DEFAULT_CHARSET;
            EnumFontFamiliesExW(dc, &lf, AddFont, (LPARAM)fonts, 0);
            ReleaseDC(dlg, dc);
            SetWindowTextW(fonts, t.font.c_str());
            SetDlgItemTextW(dlg, IDC_TE_HEIGHT, FormatDecimal(t.height, 4).c_str());
            SetDlgItemTextW(dlg, IDC_TE_ROTATION, FormatDegrees(t.rotation, 4).c_str());
            SetDlgItemTextW(dlg, IDC_TE_WIDTH, FormatDecimal(t.widthFactor, 3).c_str());
            for (const wchar_t* j : {L"Top left", L"Top centre", L"Top right", L"Middle left", L"Middle centre",
                                     L"Middle right", L"Bottom left", L"Bottom centre", L"Bottom right"})
                SendDlgItemMessageW(dlg, IDC_TE_JUSTIFY, CB_ADDSTRING, 0, (LPARAM)j);
            SendDlgItemMessageW(dlg, IDC_TE_JUSTIFY, CB_SETCURSEL, (WPARAM)std::clamp(t.justify, 0, 8), 0);
            for (const wchar_t* f : {L"None", L"Box", L"Circle (balloon)"})
                SendDlgItemMessageW(dlg, IDC_TE_FRAME, CB_ADDSTRING, 0, (LPARAM)f);
            SendDlgItemMessageW(dlg, IDC_TE_FRAME, CB_SETCURSEL, (WPARAM)std::clamp(t.frame, 0, 2), 0);
            SetFocus(GetDlgItem(dlg, IDC_TE_TEXT));
            SendDlgItemMessageW(dlg, IDC_TE_TEXT, EM_SETSEL, 0, -1);
            return FALSE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id >= IDC_TE_SYMBOL && id < IDC_TE_SYMBOL + (int)std::size(kTextSymbols)) {
                SendDlgItemMessageW(dlg, IDC_TE_TEXT, EM_REPLACESEL, TRUE, (LPARAM)kTextSymbols[id - IDC_TE_SYMBOL]);
                SetFocus(GetDlgItem(dlg, IDC_TE_TEXT));
            } else if (id == IDC_TE_FIELD) {
                HMENU m = CreatePopupMenu();
                AppendMenuW(m, MF_STRING, 1, L"&Date (today)");
                AppendMenuW(m, MF_STRING, 2, L"&Time");
                AppendMenuW(m, MF_STRING, 3, L"&File name");
                AppendMenuW(m, MF_STRING, 4, L"&Author (Windows user)");
                const UINT f = PopupAtCursor(dlg, m);
                const wchar_t* field = f == 1 ? L"{date}" : f == 2 ? L"{time}" : f == 3 ? L"{filename}" : f == 4 ? L"{author}" : nullptr;
                if (field) SendDlgItemMessageW(dlg, IDC_TE_TEXT, EM_REPLACESEL, TRUE, (LPARAM)field);
                SetFocus(GetDlgItem(dlg, IDC_TE_TEXT));
            } else if (id == IDOK) {
                TextData t = *d->t;
                double h, w;
                if (!ReadPositive(dlg, IDC_TE_HEIGHT, h) || !ReadPositive(dlg, IDC_TE_WIDTH, w)) return TRUE;
                double rot = 0;
                if (!ParseNumber(WideToUtf8(GetText(dlg, IDC_TE_ROTATION)), rot)) {
                    MessageBoxW(dlg, L"Please enter the rotation in degrees.", APP_NAME, MB_ICONWARNING);
                    return TRUE;
                }
                std::wstring content;
                for (wchar_t c : GetText(dlg, IDC_TE_TEXT))
                    if (c != L'\r') content += c;
                t.content = content;
                const std::wstring font = GetText(dlg, IDC_TE_FONT);
                if (!font.empty()) t.font = font;
                t.height = h;
                t.widthFactor = w;
                t.rotation = ToRadians(rot);
                t.justify = std::max(0, (int)SendDlgItemMessageW(dlg, IDC_TE_JUSTIFY, CB_GETCURSEL, 0, 0));
                t.frame = std::max(0, (int)SendDlgItemMessageW(dlg, IDC_TE_FRAME, CB_GETCURSEL, 0, 0));
                *d->t = t;
                EndDialog(dlg, IDOK);
            } else if (id == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
            }
            return TRUE;
        }
    }
    return FALSE;
}

// ---- colours and layers ---------------------------------------------------------
UINT PopupAtCursor(HWND owner, HMENU m) {
    POINT pt;
    GetCursorPos(&pt);
    const UINT id = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m);
    return id;
}

struct LayerData {
    Document* doc;
    const std::function<void()>* changed;
};

enum LayerColumn { kColCurrent, kColName, kColOn, kColFreeze, kColLock, kColColor, kColLinetype, kColWeight, kColPlot, kColUse };

void FillLayerList(HWND dlg, const Document& doc, int selectId) {
    HWND list = GetDlgItem(dlg, IDC_LY_LIST);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);
    int sel = 0;
    for (size_t i = 0; i < doc.Layers().size(); ++i) {
        const Layer& l = doc.Layers()[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.lParam = l.id;
        std::wstring cur = l.id == doc.Current().layer ? L"\x2022" : L"";
        it.pszText = cur.data();
        ListView_InsertItem(list, &it);
        auto set = [&](int col, const std::wstring& text) {
            ListView_SetItemText(list, (int)i, col, const_cast<wchar_t*>(text.c_str()));
        };
        set(kColName, l.name);
        set(kColOn, l.on ? L"On" : L"Off");
        set(kColFreeze, l.frozen ? L"Frozen" : L"-");
        set(kColLock, l.locked ? L"Locked" : L"-");
        set(kColColor, ColorName(l.color));
        set(kColLinetype, LineTypeName(l.linetype));
        set(kColWeight, LineWeightName(l.lineweight));
        set(kColPlot, l.plot ? L"Yes" : L"No");
        set(kColUse, std::to_wstring(doc.LayerUse(l.id)));
        if (l.id == selectId) sel = (int)i;
    }
    ListView_SetItemState(list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, sel, FALSE);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    // Layer states
    HWND states = GetDlgItem(dlg, IDC_LY_STATE);
    std::wstring typed = GetText(dlg, IDC_LY_STATE);
    SendMessageW(states, CB_RESETCONTENT, 0, 0);
    for (const LayerState& st : doc.LayerStates()) SendMessageW(states, CB_ADDSTRING, 0, (LPARAM)st.name.c_str());
    SetWindowTextW(states, typed.c_str());
}

int SelectedLayerId(HWND dlg) {
    HWND list = GetDlgItem(dlg, IDC_LY_LIST);
    const int i = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (i < 0) return -1;
    LVITEMW it{};
    it.mask = LVIF_PARAM;
    it.iItem = i;
    ListView_GetItem(list, &it);
    return (int)it.lParam;
}

INT_PTR CALLBACK LayerProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (LayerData*)GetWindowLongPtrW(dlg, DWLP_USER);
    auto refresh = [&](int id) {
        FillLayerList(dlg, *d->doc, id);
        (*d->changed)();
    };
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d = (LayerData*)lp;
            InitDialog(dlg);
            HWND list = GetDlgItem(dlg, IDC_LY_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            const struct {
                const wchar_t* title;
                int width;
            } cols[] = {{L"", 22}, {L"Name", 130}, {L"On", 40}, {L"Freeze", 52}, {L"Lock", 50}, {L"Colour", 92},
                        {L"Line type", 92}, {L"Weight", 70}, {L"Plot", 36}, {L"Objects", 54}};
            const int dpi = GetWindowDpi(dlg);
            for (int i = 0; i < (int)std::size(cols); ++i) {
                LVCOLUMNW c{};
                c.mask = LVCF_TEXT | LVCF_WIDTH;
                c.pszText = const_cast<wchar_t*>(cols[i].title);
                c.cx = Dpi(cols[i].width, dpi);
                ListView_InsertColumn(list, i, &c);
            }
            FillLayerList(dlg, *d->doc, d->doc->Current().layer);
            return TRUE;
        }
        case WM_NOTIFY: {
            auto* nm = (NMHDR*)lp;
            if (nm->idFrom != IDC_LY_LIST) break;
            if (nm->code == NM_CLICK || nm->code == NM_DBLCLK) {
                auto* ia = (NMITEMACTIVATE*)lp;
                if (ia->iItem < 0) return TRUE;
                LVITEMW it{};
                it.mask = LVIF_PARAM;
                it.iItem = ia->iItem;
                ListView_GetItem(GetDlgItem(dlg, IDC_LY_LIST), &it);
                const Layer* found = d->doc->FindLayer((int)it.lParam);
                if (!found) return TRUE;
                Layer l = *found;
                if (nm->code == NM_DBLCLK && (ia->iSubItem == kColCurrent || ia->iSubItem == kColName)) {
                    CurrentStyle c = d->doc->Current();
                    c.layer = l.id;
                    d->doc->SetCurrent(c);
                    refresh(l.id);
                    return TRUE;
                }
                if (nm->code != NM_CLICK) return TRUE;
                bool change = true;
                switch (ia->iSubItem) {
                    case kColOn: l.on = !l.on; break;
                    case kColFreeze:
                        if (l.id == d->doc->Current().layer && !l.frozen) {
                            MessageBoxW(dlg, L"The current layer cannot be frozen.", APP_NAME, MB_ICONINFORMATION);
                            change = false;
                        } else {
                            l.frozen = !l.frozen;
                        }
                        break;
                    case kColLock: l.locked = !l.locked; break;
                    case kColPlot: l.plot = !l.plot; break;
                    case kColColor: change = PickColor(dlg, l.color, false); break;
                    case kColLinetype: {
                        HMENU m = CreatePopupMenu();
                        for (size_t i = 0; i < LineTypes().size(); ++i)
                            AppendMenuW(m, MF_STRING | ((int)i == l.linetype ? MF_CHECKED : 0), i + 1,
                                        (std::wstring(LineTypes()[i].name) + L"\t" + LineTypes()[i].description).c_str());
                        const UINT id = PopupAtCursor(dlg, m);
                        change = id != 0;
                        if (change) l.linetype = (int)id - 1;
                        break;
                    }
                    case kColWeight: {
                        HMENU m = CreatePopupMenu();
                        AppendMenuW(m, MF_STRING | (l.lineweight < 0 ? MF_CHECKED : 0), 1000, L"Default (0.25 mm)");
                        for (int w : LineWeights())
                            AppendMenuW(m, MF_STRING | (w == l.lineweight ? MF_CHECKED : 0), (UINT_PTR)w + 1,
                                        LineWeightName(w).c_str());
                        const UINT id = PopupAtCursor(dlg, m);
                        change = id != 0;
                        if (change) l.lineweight = id == 1000 ? kLwDefault : (int)id - 1;
                        break;
                    }
                    default: change = false; break;
                }
                if (change) {
                    d->doc->UpdateLayer(l);
                    refresh(l.id);
                }
                return TRUE;
            }
            if (nm->code == LVN_BEGINLABELEDITW) {
                auto* di = (NMLVDISPINFOW*)lp;
                LVITEMW it{};
                it.mask = LVIF_PARAM;
                it.iItem = di->item.iItem;
                ListView_GetItem(GetDlgItem(dlg, IDC_LY_LIST), &it);
                SetWindowLongPtrW(dlg, DWLP_MSGRESULT, it.lParam == 0 ? TRUE : FALSE);  // layer 0 keeps its name
                return TRUE;
            }
            if (nm->code == LVN_ENDLABELEDITW) {
                auto* di = (NMLVDISPINFOW*)lp;
                if (!di->item.pszText) return TRUE;
                LVITEMW it{};
                it.mask = LVIF_PARAM;
                it.iItem = di->item.iItem;
                ListView_GetItem(GetDlgItem(dlg, IDC_LY_LIST), &it);
                const std::wstring name = di->item.pszText;
                const Layer* other = d->doc->LayerByName(name);
                const Layer* mine = d->doc->FindLayer((int)it.lParam);
                if (name.empty() || !mine || (other && other->id != mine->id)) {
                    MessageBoxW(dlg, L"Each layer needs a name of its own.", APP_NAME, MB_ICONINFORMATION);
                    return TRUE;
                }
                Layer l = *mine;
                l.name = name;
                d->doc->UpdateLayer(l);
                refresh(l.id);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_LY_NEW: {
                    Layer l;
                    l.name = L"Layer1";
                    for (int k = 1; d->doc->LayerByName(l.name); ++k) l.name = L"Layer" + std::to_wstring(k);
                    const int id = d->doc->AddLayer(l);
                    refresh(id);
                    HWND list = GetDlgItem(dlg, IDC_LY_LIST);
                    SetFocus(list);
                    ListView_EditLabel(list, ListView_GetNextItem(list, -1, LVNI_SELECTED));
                    return TRUE;
                }
                case IDC_LY_DELETE: {
                    std::wstring why;
                    const int id = SelectedLayerId(dlg);
                    if (id >= 0 && !d->doc->DeleteLayer(id, why)) MessageBoxW(dlg, why.c_str(), APP_NAME, MB_ICONINFORMATION);
                    refresh(0);
                    return TRUE;
                }
                case IDC_LY_CURRENT: {
                    const int id = SelectedLayerId(dlg);
                    const Layer* l = d->doc->FindLayer(id);
                    if (!l) return TRUE;
                    if (l->frozen) {
                        MessageBoxW(dlg, L"A frozen layer cannot be current. Thaw it first.", APP_NAME, MB_ICONINFORMATION);
                        return TRUE;
                    }
                    CurrentStyle c = d->doc->Current();
                    c.layer = id;
                    d->doc->SetCurrent(c);
                    refresh(id);
                    return TRUE;
                }
                case IDC_LY_SAVESTATE: {
                    const std::wstring name = GetText(dlg, IDC_LY_STATE);
                    if (name.empty()) {
                        MessageBoxW(dlg, L"Type a name for the layer state first.", APP_NAME, MB_ICONINFORMATION);
                        return TRUE;
                    }
                    d->doc->SaveLayerState(name);
                    refresh(SelectedLayerId(dlg));
                    return TRUE;
                }
                case IDC_LY_RESTORESTATE: {
                    if (!d->doc->RestoreLayerState(GetText(dlg, IDC_LY_STATE)))
                        MessageBoxW(dlg, L"Choose a saved layer state first.", APP_NAME, MB_ICONINFORMATION);
                    refresh(SelectedLayerId(dlg));
                    return TRUE;
                }
                case IDC_LY_DELSTATE:
                    d->doc->DeleteLayerState(GetText(dlg, IDC_LY_STATE));
                    SetDlgItemTextW(dlg, IDC_LY_STATE, L"");
                    refresh(SelectedLayerId(dlg));
                    return TRUE;
                case IDOK:
                case IDCANCEL:
                    EndDialog(dlg, IDOK);
                    return TRUE;
            }
            break;
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
                            L"Drawings are saved as .fcad text files. DXF drawings from other CAD "
                            L"programs can be opened, and any drawing exported as DXF.");
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

bool DimStyleDialog(HWND owner, DimStyle& style, bool& applyExisting) {
    DimStyleData d{&style, &applyExisting};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DIMSTYLE), owner, DimStyleProc,
                           (LPARAM)&d) == IDOK;
}

bool DimEditDialog(HWND owner, const DimData& dim, bool attached, DimEditResult& out) {
    DimEditData d{&dim, attached, &out, L""};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DIMEDIT), owner, DimEditProc, (LPARAM)&d) ==
           IDOK;
}

bool TextEditDialog(HWND owner, TextData& text, const std::wstring& title) {
    TextEditData d{&text, title};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TEXTEDIT), owner, TextEditProc, (LPARAM)&d) == IDOK;
}

void LayerDialog(HWND owner, Document& doc, const std::function<void()>& changed) {
    LayerData d{&doc, &changed};
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_LAYERS), owner, LayerProc, (LPARAM)&d);
}

bool PickColor(HWND owner, int32_t& color, bool allowBy) {
    HMENU m = CreatePopupMenu();
    if (allowBy) {
        AppendMenuW(m, MF_STRING | (color == kByLayer ? MF_CHECKED : 0), 300, L"ByLayer");
        AppendMenuW(m, MF_STRING | (color == kByBlock ? MF_CHECKED : 0), 301, L"ByBlock");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    for (int i = 1; i <= 9; ++i)
        AppendMenuW(m, MF_STRING | (color == i ? MF_CHECKED : 0), (UINT_PTR)i, ColorName(i).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 400, L"Other colour...");
    const UINT id = PopupAtCursor(owner, m);
    if (id == 0) return false;
    if (id == 300) color = kByLayer;
    else if (id == 301) color = kByBlock;
    else if (id <= 9) color = (int32_t)id;
    else {
        static COLORREF custom[16] = {};
        CHOOSECOLORW cc{sizeof(cc)};
        cc.hwndOwner = owner;
        const uint32_t now = (color == kByLayer || color == kByBlock) ? 0xFFFFFF : ColorRgb(color);
        cc.rgbResult = RGB((now >> 16) & 255, (now >> 8) & 255, now & 255);
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (!ChooseColorW(&cc)) return false;
        color = kTrueColor | (int32_t)((GetRValue(cc.rgbResult) << 16) | (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult));
    }
    return true;
}
