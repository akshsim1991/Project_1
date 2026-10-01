// ServiceList.cpp - the virtual service table (see ServiceList.h).
#include "ServiceList.h"

#include <algorithm>
#include <unordered_set>

#include <commctrl.h>
#include <uxtheme.h>

#include "Theme.h"
#include "Util.h"

namespace {
// Sort ranks, so "Running" sorts before "Stopped" and so on.
int StateRank(DWORD state) {
    switch (state) {
        case SERVICE_RUNNING: return 0;
        case SERVICE_START_PENDING:
        case SERVICE_STOP_PENDING:
        case SERVICE_PAUSE_PENDING:
        case SERVICE_CONTINUE_PENDING: return 1;
        case SERVICE_PAUSED: return 2;
        default: return 3;
    }
}

int StartRank(const ServiceInfo& s) {
    if (!s.configKnown) return 9;
    switch (s.startType) {
        case SERVICE_BOOT_START: return 0;
        case SERVICE_SYSTEM_START: return 1;
        case SERVICE_AUTO_START: return s.delayed ? 3 : 2;
        case SERVICE_DEMAND_START: return 4;
        case SERVICE_DISABLED: return 5;
        default: return 9;
    }
}

int CompareText(const std::wstring& a, const std::wstring& b) {
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                           a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), nullptr, nullptr, 0) -
           CSTR_EQUAL;
}
}  // namespace

const wchar_t* ServiceList::ColumnTitle(int column) {
    switch (column) {
        case kColDisplayName: return L"Name";
        case kColName: return L"Service name";
        case kColStatus: return L"Status";
        case kColStartType: return L"Start type";
        case kColSafety: return L"Safety";
        case kColPid: return L"PID";
        case kColAccount: return L"Log on as";
        case kColDescription: return L"Description";
        case kColPath: return L"Program";
        case kColCompany: return L"Company";
        default: return L"";
    }
}

std::wstring ServiceList::CellText(const ServiceInfo& s, int column) {
    switch (column) {
        case kColDisplayName: return s.displayName;
        case kColName: return s.name;
        case kColStatus: return Svc::StateText(s.state);
        case kColStartType: return Svc::StartTypeText(s);
        case kColSafety: return Svc::SafetyText(s.safety);
        case kColPid: return s.pid ? std::to_wstring(s.pid) : std::wstring();
        case kColAccount: return s.account;
        case kColDescription: return s.description;
        case kColPath: return s.binaryPath;
        case kColCompany: return s.company;
        default: return {};
    }
}

bool ServiceList::Create(HWND parent, Settings* settings) {
    m_settings = settings;
    m_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
                                 LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_list) return false;
    ListView_SetExtendedListViewStyle(m_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                  LVS_EX_HEADERDRAGDROP | LVS_EX_INFOTIP |
                                                  LVS_EX_LABELTIP);
    SetWindowSubclass(m_list, &ServiceList::ListProc, 1, (DWORD_PTR)this);
    m_dpi = GetWindowDpi(m_list);
    m_font = CreateMessageFont(m_dpi);
    SendMessageW(m_list, WM_SETFONT, (WPARAM)m_font, FALSE);
    BuildColumns();
    ApplyTheme();
    return true;
}

// ---------------------------------------------------------------------------
// Columns
// ---------------------------------------------------------------------------
void ServiceList::BuildColumns() {
    while (ListView_DeleteColumn(m_list, 0)) {
    }
    m_cols.clear();
    for (int c = 0; c < kColumnCount; ++c) {
        if (!(m_settings->visibleColumns & (1u << c))) continue;
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = c == kColPid ? LVCFMT_RIGHT : LVCFMT_LEFT;
        col.cx = Dpi(m_settings->columnWidth[c], m_dpi);
        col.pszText = const_cast<wchar_t*>(ColumnTitle(c));
        ListView_InsertColumn(m_list, (int)m_cols.size(), &col);
        m_cols.push_back(c);
    }
    // Restore the saved order among the visible columns.
    std::vector<int> order;
    for (int i = 0; i < kColumnCount; ++i) {
        auto it = std::find(m_cols.begin(), m_cols.end(), m_settings->columnOrder[i]);
        if (it != m_cols.end()) order.push_back((int)(it - m_cols.begin()));
    }
    if (order.size() == m_cols.size()) ListView_SetColumnOrderArray(m_list, (int)order.size(), order.data());
    UpdateSortArrow();
}

void ServiceList::SaveColumns() {
    if (!m_list) return;
    for (size_t i = 0; i < m_cols.size(); ++i) {
        const int w = ListView_GetColumnWidth(m_list, (int)i);
        if (w > 0) m_settings->columnWidth[m_cols[i]] = std::max(30, MulDiv(w, 96, m_dpi));
    }
    // Order: visible columns as arranged on screen, hidden ones after them.
    std::vector<int> order(m_cols.size());
    if (!order.empty() && ListView_GetColumnOrderArray(m_list, (int)order.size(), order.data())) {
        int n = 0;
        for (int sub : order) m_settings->columnOrder[n++] = m_cols[(size_t)sub];
        for (int c = 0; c < kColumnCount; ++c)
            if (std::find(m_cols.begin(), m_cols.end(), c) == m_cols.end()) m_settings->columnOrder[n++] = c;
    }
}

void ServiceList::ToggleColumn(int column) {
    if (column <= kColDisplayName || column >= kColumnCount) return;  // the name always stays
    SaveColumns();
    m_settings->visibleColumns ^= 1u << column;
    BuildColumns();
    InvalidateRect(m_list, nullptr, TRUE);
}

void ServiceList::ResetColumns() {
    const Settings defaults;
    for (int c = 0; c < kColumnCount; ++c) {
        m_settings->columnWidth[c] = defaults.columnWidth[c];
        m_settings->columnOrder[c] = defaults.columnOrder[c];
    }
    m_settings->visibleColumns = defaults.visibleColumns;
    BuildColumns();
    InvalidateRect(m_list, nullptr, TRUE);
}

void ServiceList::UpdateSortArrow() {
    HWND header = ListView_GetHeader(m_list);
    for (size_t i = 0; i < m_cols.size(); ++i) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        Header_GetItem(header, (int)i, &item);
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (m_cols[i] == m_settings->sortColumn)
            item.fmt |= m_settings->sortDescending ? HDF_SORTDOWN : HDF_SORTUP;
        Header_SetItem(header, (int)i, &item);
    }
    InvalidateRect(header, nullptr, TRUE);
}

void ServiceList::ShowColumnMenu(POINT screen) {
    HMENU m = CreatePopupMenu();
    for (int c = 0; c < kColumnCount; ++c) {
        UINT flags = MF_STRING;
        if (m_settings->visibleColumns & (1u << c)) flags |= MF_CHECKED;
        if (c == kColDisplayName) flags |= MF_GRAYED;
        AppendMenuW(m, flags, ID_COLUMN_FIRST + c, ColumnTitle(c));
    }
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0,
                                   GetParent(m_list), nullptr);
    DestroyMenu(m);
    if (cmd >= ID_COLUMN_FIRST && cmd < ID_COLUMN_FIRST + kColumnCount) ToggleColumn(cmd - ID_COLUMN_FIRST);
}

// ---------------------------------------------------------------------------
// Data, filter and sort
// ---------------------------------------------------------------------------
bool ServiceList::Matches(const ServiceInfo& s) const {
    switch (m_settings->filter) {
        case kFilterRunning:
            if (s.state != SERVICE_RUNNING) return false;
            break;
        case kFilterStopped:
            if (s.state != SERVICE_STOPPED) return false;
            break;
        case kFilterAutoStopped:
            if (s.startType != SERVICE_AUTO_START || s.state != SERVICE_STOPPED || !s.configKnown)
                return false;
            break;
        case kFilterDisabled:
            if (s.startType != SERVICE_DISABLED || !s.configKnown) return false;
            break;
        case kFilterThirdParty:
            if (s.safety != Safety::ThirdParty) return false;
            break;
        case kFilterCritical:
            if (s.safety != Safety::Critical) return false;
            break;
        default: break;
    }
    if (m_searchLower.empty()) return true;
    return ContainsNoCase(s.displayName, m_searchLower) || ContainsNoCase(s.name, m_searchLower) ||
           ContainsNoCase(s.description, m_searchLower) || ContainsNoCase(s.binaryPath, m_searchLower) ||
           ContainsNoCase(s.company, m_searchLower);
}

void ServiceList::ApplyView() {
    m_view.clear();
    for (size_t i = 0; i < m_all.size(); ++i)
        if (Matches(m_all[i])) m_view.push_back((int)i);
    const int col = m_settings->sortColumn;
    const bool desc = m_settings->sortDescending;
    std::stable_sort(m_view.begin(), m_view.end(), [&](int ia, int ib) {
        const ServiceInfo& a = m_all[(size_t)ia];
        const ServiceInfo& b = m_all[(size_t)ib];
        int r = 0;
        switch (col) {
            case kColStatus: r = StateRank(a.state) - StateRank(b.state); break;
            case kColStartType: r = StartRank(a) - StartRank(b); break;
            case kColSafety: r = (int)a.safety - (int)b.safety; break;
            case kColPid: r = a.pid < b.pid ? -1 : a.pid > b.pid ? 1 : 0; break;
            default: r = CompareText(CellText(a, col), CellText(b, col)); break;
        }
        if (r == 0) r = CompareText(a.displayName, b.displayName);  // stable tie-break
        return desc ? r > 0 : r < 0;
    });
}

void ServiceList::SetData(std::vector<ServiceInfo>&& services) {
    // Remember selection, focus and the first visible row by name.
    std::unordered_set<std::wstring> selected;
    for (int i = ListView_GetNextItem(m_list, -1, LVNI_SELECTED); i >= 0;
         i = ListView_GetNextItem(m_list, i, LVNI_SELECTED))
        if (i < (int)m_view.size()) selected.insert(m_all[(size_t)m_view[(size_t)i]].name);
    const int focus = ListView_GetNextItem(m_list, -1, LVNI_FOCUSED);
    const std::wstring focusName =
        focus >= 0 && focus < (int)m_view.size() ? m_all[(size_t)m_view[(size_t)focus]].name : L"";
    const int top = ListView_GetTopIndex(m_list);
    const std::wstring topName =
        top >= 0 && top < (int)m_view.size() ? m_all[(size_t)m_view[(size_t)top]].name : L"";

    m_updating = true;
    m_all = std::move(services);
    ApplyView();
    ListView_SetItemCountEx(m_list, (int)m_view.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
    ListView_SetItemState(m_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    int newTop = -1;
    for (size_t i = 0; i < m_view.size(); ++i) {
        const std::wstring& name = m_all[(size_t)m_view[i]].name;
        UINT state = 0;
        if (selected.count(name)) state |= LVIS_SELECTED;
        if (name == focusName) state |= LVIS_FOCUSED;
        if (state) ListView_SetItemState(m_list, (int)i, state, LVIS_SELECTED | LVIS_FOCUSED);
        if (name == topName) newTop = (int)i;
    }
    // Keep the same row at the top of the list.
    const int curTop = ListView_GetTopIndex(m_list);
    if (newTop >= 0 && newTop != curTop && !m_view.empty()) {
        RECT rc{};
        if (ListView_GetItemRect(m_list, 0, &rc, LVIR_BOUNDS))
            ListView_Scroll(m_list, 0, (newTop - curTop) * (rc.bottom - rc.top));
    }
    m_updating = false;
    InvalidateRect(m_list, nullptr, FALSE);
    if (onSelectionChanged) onSelectionChanged();
}

void ServiceList::SetFilter(int filter) {
    m_settings->filter = filter;
    std::vector<ServiceInfo> copy = m_all;
    SetData(std::move(copy));
}

void ServiceList::SetSearch(const std::wstring& text) {
    const std::wstring lower = ToLower(text);
    if (lower == m_searchLower) return;
    m_searchLower = lower;
    std::vector<ServiceInfo> copy = m_all;
    SetData(std::move(copy));
}

void ServiceList::SortBy(int column) {
    if (m_settings->sortColumn == column)
        m_settings->sortDescending = !m_settings->sortDescending;
    else {
        m_settings->sortColumn = column;
        m_settings->sortDescending = false;
    }
    UpdateSortArrow();
    std::vector<ServiceInfo> copy = m_all;
    SetData(std::move(copy));
}

std::vector<const ServiceInfo*> ServiceList::Selected() const {
    std::vector<const ServiceInfo*> out;
    for (int i = ListView_GetNextItem(m_list, -1, LVNI_SELECTED); i >= 0;
         i = ListView_GetNextItem(m_list, i, LVNI_SELECTED))
        if (i < (int)m_view.size()) out.push_back(&m_all[(size_t)m_view[(size_t)i]]);
    return out;
}

const ServiceInfo* ServiceList::Focused() const {
    int i = ListView_GetNextItem(m_list, -1, LVNI_FOCUSED | LVNI_SELECTED);
    if (i < 0) i = ListView_GetNextItem(m_list, -1, LVNI_SELECTED);
    return i >= 0 && i < (int)m_view.size() ? &m_all[(size_t)m_view[(size_t)i]] : nullptr;
}

void ServiceList::SelectAll() { ListView_SetItemState(m_list, -1, LVIS_SELECTED, LVIS_SELECTED); }

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
void ServiceList::ApplyTheme() {
    const Theme& th = CurrentTheme();
    SetWindowTheme(m_list, th.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowTheme(ListView_GetHeader(m_list), th.dark ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
    ListView_SetBkColor(m_list, th.listBg);
    ListView_SetTextBkColor(m_list, th.listBg);
    ListView_SetTextColor(m_list, th.listText);
    InvalidateRect(m_list, nullptr, TRUE);
    InvalidateRect(ListView_GetHeader(m_list), nullptr, TRUE);
}

void ServiceList::OnThemeChanged() { ApplyTheme(); }

void ServiceList::OnDpiChanged() {
    SaveColumns();
    m_dpi = GetWindowDpi(m_list);
    HFONT old = m_font;
    m_font = CreateMessageFont(m_dpi);
    SendMessageW(m_list, WM_SETFONT, (WPARAM)m_font, TRUE);
    if (old) DeleteObject(old);
    BuildColumns();
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------
bool ServiceList::OnNotify(NMHDR* hdr, LRESULT& result) {
    if (hdr->hwndFrom != m_list) return false;
    switch (hdr->code) {
        case LVN_GETDISPINFOW: {
            auto* di = (NMLVDISPINFOW*)hdr;
            if ((di->item.mask & LVIF_TEXT) && di->item.iItem >= 0 &&
                di->item.iItem < (int)m_view.size()) {
                const ServiceInfo& s = m_all[(size_t)m_view[(size_t)di->item.iItem]];
                m_textBuf = CellText(s, ColumnAt(di->item.iSubItem));
                // Descriptions can contain line breaks: show them on one line.
                std::replace(m_textBuf.begin(), m_textBuf.end(), L'\n', L' ');
                std::replace(m_textBuf.begin(), m_textBuf.end(), L'\r', L' ');
                wcsncpy_s(di->item.pszText, di->item.cchTextMax, m_textBuf.c_str(), _TRUNCATE);
            }
            result = 0;
            return true;
        }
        case LVN_GETINFOTIPW: {
            auto* tip = (NMLVGETINFOTIPW*)hdr;
            if (tip->iItem >= 0 && tip->iItem < (int)m_view.size()) {
                const ServiceInfo& s = m_all[(size_t)m_view[(size_t)tip->iItem]];
                std::wstring text = s.displayName + L" (" + s.name + L")";
                if (!s.description.empty()) text += L"\n\n" + s.description;
                if (!s.binaryPath.empty()) text += L"\n\n" + s.binaryPath;
                wcsncpy_s(tip->pszText, tip->cchTextMax, text.c_str(), _TRUNCATE);
            }
            result = 0;
            return true;
        }
        case LVN_COLUMNCLICK:
            SortBy(ColumnAt(((NMLISTVIEW*)hdr)->iSubItem));
            result = 0;
            return true;
        case LVN_ITEMCHANGED:
        case LVN_ODSTATECHANGED:
            if (!m_updating && onSelectionChanged) onSelectionChanged();
            result = 0;
            return true;
        case NM_RCLICK: {
            POINT pt;
            GetCursorPos(&pt);
            if (onContextMenu) onContextMenu(pt);
            result = 0;
            return true;
        }
        case NM_CUSTOMDRAW: {
            auto* cd = (NMLVCUSTOMDRAW*)hdr;
            switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: result = CDRF_NOTIFYITEMDRAW; return true;
                case CDDS_ITEMPREPAINT: result = CDRF_NOTIFYSUBITEMDRAW; return true;
                case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                    const Theme& th = CurrentTheme();
                    const size_t row = (size_t)cd->nmcd.dwItemSpec;
                    cd->clrText = th.listText;
                    if (row < m_view.size()) {
                        const ServiceInfo& s = m_all[(size_t)m_view[row]];
                        switch (ColumnAt(cd->iSubItem)) {
                            case kColStatus:
                                cd->clrText = s.state == SERVICE_RUNNING   ? th.running
                                              : s.state == SERVICE_STOPPED ? th.stopped
                                              : s.state == SERVICE_PAUSED  ? th.paused
                                                                           : th.pending;
                                break;
                            case kColStartType:
                                if (s.startType == SERVICE_DISABLED) cd->clrText = th.listDim;
                                break;
                            case kColSafety:
                                cd->clrText = s.safety == Safety::Critical     ? th.danger
                                              : s.safety == Safety::ThirdParty ? th.thirdParty
                                                                               : th.listDim;
                                break;
                            case kColName:
                            case kColDescription:
                            case kColPath:
                            case kColCompany:
                            case kColAccount:
                            case kColPid:
                                cd->clrText = th.listDim;
                                break;
                        }
                    }
                    result = CDRF_DODEFAULT;
                    return true;
                }
            }
            result = CDRF_DODEFAULT;
            return true;
        }
    }
    return false;
}

// The header is drawn here in both themes: the system's dark header theme
// is missing on older Windows 10 builds, which would leave light text on a
// light header.
LRESULT ServiceList::DrawHeader(NMCUSTOMDRAW* cd) {
    const Theme& th = CurrentTheme();
    if (cd->dwDrawStage == CDDS_PREPAINT) {
        HBRUSH bg = CreateSolidBrush(th.barBg);
        RECT rc;
        GetClientRect(cd->hdr.hwndFrom, &rc);
        FillRect(cd->hdc, &rc, bg);
        DeleteObject(bg);
        return CDRF_NOTIFYITEMDRAW;
    }
    if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    const int sub = (int)cd->dwItemSpec;
    RECT r = cd->rc;
    HBRUSH bg = CreateSolidBrush((cd->uItemState & CDIS_SELECTED) ? th.barPressed : th.barBg);
    FillRect(cd->hdc, &r, bg);
    DeleteObject(bg);
    RECT line = {r.right - 1, r.top + Dpi(4, m_dpi), r.right, r.bottom - Dpi(4, m_dpi)};
    RECT bottom = {r.left, r.bottom - 1, r.right, r.bottom};
    HBRUSH border = CreateSolidBrush(th.barBorder);
    FillRect(cd->hdc, &line, border);
    FillRect(cd->hdc, &bottom, border);
    DeleteObject(border);

    const int column = ColumnAt(sub);
    const int pad = Dpi(6, m_dpi), arrow = Dpi(8, m_dpi);
    RECT text = {r.left + pad, r.top, r.right - pad, r.bottom};
    const bool sorted = column == m_settings->sortColumn;
    if (sorted) text.right -= arrow + Dpi(4, m_dpi);
    HGDIOBJ oldFont = SelectObject(cd->hdc, m_font);
    SetBkMode(cd->hdc, TRANSPARENT);
    SetTextColor(cd->hdc, th.barText);
    DrawTextW(cd->hdc, ColumnTitle(column), -1, &text,
              (column == kColPid ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(cd->hdc, oldFont);
    if (sorted) {
        // A small triangle: up for ascending, down for descending.
        const int cx = r.right - pad - arrow / 2, cy = (r.top + r.bottom) / 2;
        const int h = arrow / 2;
        POINT pts[3];
        if (m_settings->sortDescending) {
            pts[0] = {cx - h, cy - h / 2};
            pts[1] = {cx + h, cy - h / 2};
            pts[2] = {cx, cy + h / 2 + 1};
        } else {
            pts[0] = {cx - h, cy + h / 2};
            pts[1] = {cx + h, cy + h / 2};
            pts[2] = {cx, cy - h / 2 - 1};
        }
        HBRUSH b = CreateSolidBrush(th.barTextDisabled);
        HPEN p = CreatePen(PS_SOLID, 1, th.barTextDisabled);
        HGDIOBJ ob = SelectObject(cd->hdc, b), op = SelectObject(cd->hdc, p);
        Polygon(cd->hdc, pts, 3);
        SelectObject(cd->hdc, ob);
        SelectObject(cd->hdc, op);
        DeleteObject(b);
        DeleteObject(p);
    }
    return CDRF_SKIPDEFAULT;
}

// The header sends its notifications to the list, so the list is
// subclassed to colour the header text (dark theme) and to offer the
// column chooser on a right-click.
LRESULT CALLBACK ServiceList::ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                                       DWORD_PTR ref) {
    auto* self = (ServiceList*)ref;
    if (msg == WM_NOTIFY) {
        auto* hdr = (NMHDR*)lp;
        if (hdr->hwndFrom == ListView_GetHeader(hwnd)) {
            if (hdr->code == NM_CUSTOMDRAW) return self->DrawHeader((NMCUSTOMDRAW*)lp);
            if (hdr->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                self->ShowColumnMenu(pt);
                return TRUE;
            }
        }
    } else if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, &ServiceList::ListProc, 1);
        if (self->m_font) DeleteObject(self->m_font);
        self->m_font = nullptr;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
