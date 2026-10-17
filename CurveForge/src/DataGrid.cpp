// DataGrid.cpp - spreadsheet-like editing on a virtual ListView.
#include "DataGrid.h"

#include <uxtheme.h>

#include <algorithm>

#include "Theme.h"
#include "Util.h"

namespace {
constexpr UINT WM_APP_COMMIT_LATER = WM_APP + 50;
constexpr int kRowNumberDip = 52;
constexpr int kColumnDip = 112;
}  // namespace

bool DataGrid::Create(HWND parent, Document* doc) {
    m_doc = doc;
    m_dpi = GetWindowDpi(parent);
    m_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | LVS_REPORT | LVS_OWNERDATA |
                                 LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_list) return false;
    ListView_SetExtendedListViewStyle(m_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    m_font = CreateMessageFont(m_dpi);
    SendMessageW(m_list, WM_SETFONT, (WPARAM)m_font, TRUE);
    SetWindowSubclass(m_list, &DataGrid::ListProc, 1, (DWORD_PTR)this);
    OnThemeChanged();
    Refresh(true);
    return true;
}

std::wstring DataGrid::HeaderText(int c) const {
    std::wstring t = m_doc->table.names[(size_t)c];
    if (c == m_doc->xCol) t += L"  (X)";
    else if (c == m_doc->yCol) t += L"  (Y)";
    else if (c == m_doc->sigmaCol) t += L"  (\x03C3)";
    return t;
}

void DataGrid::BuildColumns() {
    while (ListView_DeleteColumn(m_list, 0)) {
    }
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_RIGHT;
    col.cx = Dpi(kRowNumberDip, m_dpi);
    col.pszText = (LPWSTR)L"#";
    ListView_InsertColumn(m_list, 0, &col);
    for (int c = 0; c < m_doc->table.Cols(); ++c) {
        std::wstring t = HeaderText(c);
        col.fmt = LVCFMT_LEFT;
        col.cx = Dpi(kColumnDip, m_dpi);
        col.pszText = t.data();
        ListView_InsertColumn(m_list, c + 1, &col);
    }
    m_builtCols = m_doc->table.Cols();
}

void DataGrid::Refresh(bool columnsChanged) {
    if (m_edit) CancelEdit();
    if (columnsChanged || m_builtCols != m_doc->table.Cols()) {
        BuildColumns();
    } else {
        for (int c = 0; c < m_doc->table.Cols(); ++c) {
            std::wstring t = HeaderText(c);
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT;
            col.pszText = t.data();
            ListView_SetColumn(m_list, c + 1, &col);
        }
    }
    if (m_col >= m_doc->table.Cols()) m_col = std::max(0, m_doc->table.Cols() - 1);
    ListView_SetItemCountEx(m_list, m_doc->table.Rows() + 1, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    InvalidateRect(m_list, nullptr, FALSE);
    InvalidateRect(ListView_GetHeader(m_list), nullptr, FALSE);
}

int DataGrid::CurrentRow() const {
    const int r = ListView_GetNextItem(m_list, -1, LVNI_FOCUSED);
    return r < 0 ? 0 : r;
}

void DataGrid::SetCurrent(int row, int col) {
    row = std::max(0, std::min(row, m_doc->table.Rows()));
    col = std::max(0, std::min(col, m_doc->table.Cols() - 1));
    m_col = col;
    ListView_SetItemState(m_list, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(m_list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(m_list, row);
    ListView_EnsureVisible(m_list, row, FALSE);
    EnsureColumnVisible(col);
    InvalidateRect(m_list, nullptr, FALSE);
    if (onCursorMoved) onCursorMoved();
}

void DataGrid::SelectRows(int first, int last) {
    ListView_SetItemState(m_list, -1, 0, LVIS_SELECTED);
    for (int r = first; r <= last && r < m_doc->table.Rows(); ++r) ListView_SetItemState(m_list, r, LVIS_SELECTED, LVIS_SELECTED);
    ListView_SetItemState(m_list, first, LVIS_FOCUSED, LVIS_FOCUSED);
    ListView_EnsureVisible(m_list, first, FALSE);
}

std::vector<int> DataGrid::SelectedRows() const {
    std::vector<int> rows;
    int i = -1;
    while ((i = ListView_GetNextItem(m_list, i, LVNI_SELECTED)) >= 0)
        if (i < m_doc->table.Rows()) rows.push_back(i);
    return rows;
}

void DataGrid::EnsureColumnVisible(int col) {
    RECT cell, client;
    if (!ListView_GetSubItemRect(m_list, std::max(0, ListView_GetTopIndex(m_list)), col + 1, LVIR_BOUNDS, &cell)) return;
    GetClientRect(m_list, &client);
    const int numW = Dpi(kRowNumberDip, m_dpi);
    if (cell.left < numW) ListView_Scroll(m_list, cell.left - numW, 0);
    else if (cell.right > client.right) ListView_Scroll(m_list, std::min(cell.right - client.right, cell.left - numW), 0);
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
void DataGrid::BeginEdit(const wchar_t* initial) {
    if (m_edit || m_doc->table.Cols() == 0) return;
    const int row = CurrentRow();
    ListView_EnsureVisible(m_list, row, FALSE);
    EnsureColumnVisible(m_col);
    RECT rc;
    if (!ListView_GetSubItemRect(m_list, row, m_col + 1, LVIR_BOUNDS, &rc)) return;
    m_editRow = row;
    m_editCol = m_col;
    m_edit = CreateWindowExW(0, L"EDIT", initial ? initial : m_doc->table.Cell(row, m_col).c_str(),
                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rc.left, rc.top - 1,
                             rc.right - rc.left, rc.bottom - rc.top + 2, m_list, nullptr, GetModuleHandleW(nullptr),
                             nullptr);
    SendMessageW(m_edit, WM_SETFONT, (WPARAM)m_font, TRUE);
    SetWindowSubclass(m_edit, &DataGrid::EditProc, 1, (DWORD_PTR)this);
    SetFocus(m_edit);
    if (initial) SendMessageW(m_edit, EM_SETSEL, wcslen(initial), wcslen(initial));
    else SendMessageW(m_edit, EM_SETSEL, 0, -1);
}

void DataGrid::CommitEdit(int moveRow, int moveCol) {
    if (!m_edit) return;
    const std::wstring text = GetWindowString(m_edit);
    HWND e = m_edit;
    m_edit = nullptr;
    DestroyWindow(e);
    const int row = m_editRow, col = m_editCol;
    if (text != m_doc->table.Cell(row, col)) {
        if (onBeforeChange) onBeforeChange();
        m_doc->table.SetCell(row, col, text);
        Refresh(false);
        if (onChanged) onChanged();
    }
    SetFocus(m_list);
    SetCurrent(row + moveRow, col + moveCol);
}

void DataGrid::CancelEdit() {
    if (!m_edit) return;
    HWND e = m_edit;
    m_edit = nullptr;
    DestroyWindow(e);
    SetFocus(m_list);
}

void DataGrid::ClearSelectedCells() {
    const std::vector<int> rows = SelectedRows();
    bool any = false;
    for (int r : rows)
        if (!m_doc->table.Cell(r, m_col).empty()) any = true;
    if (!any) return;
    if (onBeforeChange) onBeforeChange();
    for (int r : rows) m_doc->table.SetCell(r, m_col, L"");
    m_doc->table.TrimEmptyRows();
    Refresh(false);
    if (onChanged) onChanged();
}

LRESULT CALLBACK DataGrid::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = (DataGrid*)ref;
    switch (msg) {
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
        case WM_KEYDOWN: {
            const bool shift = GetKeyState(VK_SHIFT) < 0;
            switch (wp) {
                case VK_RETURN: self->CommitEdit(shift ? -1 : 1, 0); return 0;
                case VK_TAB: self->CommitEdit(0, shift ? -1 : 1); return 0;
                case VK_ESCAPE: self->CancelEdit(); return 0;
                case VK_UP: self->CommitEdit(-1, 0); return 0;
                case VK_DOWN: self->CommitEdit(1, 0); return 0;
            }
            break;
        }
        case WM_CHAR:
            if (wp == L'\r' || wp == L'\t' || wp == 27) return 0;
            break;
        case WM_KILLFOCUS: PostMessageW(self->m_list, WM_APP_COMMIT_LATER, 0, 0); break;
        case WM_NCDESTROY: RemoveWindowSubclass(hwnd, &DataGrid::EditProc, 1); break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// List messages
// ---------------------------------------------------------------------------
LRESULT CALLBACK DataGrid::ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = (DataGrid*)ref;
    switch (msg) {
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, msg, wp, lp);
        case WM_APP_COMMIT_LATER:
            if (self->m_edit && GetFocus() != self->m_edit) self->CommitEdit(0, 0);
            return 0;
        case WM_KEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            const int cols = self->m_doc->table.Cols();
            switch (wp) {
                case VK_LEFT:
                    if (self->m_col > 0) {
                        self->m_col--;
                        self->EnsureColumnVisible(self->m_col);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        if (self->onCursorMoved) self->onCursorMoved();
                    }
                    return 0;
                case VK_RIGHT:
                    if (self->m_col + 1 < cols) {
                        self->m_col++;
                        self->EnsureColumnVisible(self->m_col);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        if (self->onCursorMoved) self->onCursorMoved();
                    }
                    return 0;
                case VK_TAB:
                    self->SetCurrent(self->CurrentRow(), self->m_col + (shift ? -1 : 1));
                    return 0;
                case VK_RETURN:
                case VK_F2:
                    if (!ctrl) {
                        self->BeginEdit(nullptr);
                        return 0;
                    }
                    break;
                case VK_DELETE:
                    if (!ctrl) {
                        self->ClearSelectedCells();
                        return 0;
                    }
                    break;
            }
            break;
        }
        case WM_CHAR:
            if (wp >= 32 && GetKeyState(VK_CONTROL) >= 0) {
                const wchar_t s[2] = {(wchar_t)wp, 0};
                self->BeginEdit(s);
                return 0;
            }
            if (wp == L'\r' || wp == L'\t') return 0;
            break;
        case WM_HSCROLL:
        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
            if (self->m_edit) self->CommitEdit(0, 0);
            break;
        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lp;
            if (hdr->hwndFrom == ListView_GetHeader(hwnd)) {
                if (hdr->code == NM_CUSTOMDRAW) return self->DrawHeader((NMCUSTOMDRAW*)lp);
                if (hdr->code == NM_RCLICK && self->onHeaderMenu) {
                    POINT pt;
                    GetCursorPos(&pt);
                    HDHITTESTINFO hit{};
                    hit.pt = pt;
                    ScreenToClient(hdr->hwndFrom, &hit.pt);
                    SendMessageW(hdr->hwndFrom, HDM_HITTEST, 0, (LPARAM)&hit);
                    self->onHeaderMenu(pt, hit.iItem - 1);
                    return TRUE;
                }
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &DataGrid::ListProc, 1);
            if (self->m_font) DeleteObject(self->m_font);
            self->m_font = nullptr;
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

bool DataGrid::OnNotify(NMHDR* hdr, LRESULT& result) {
    if (hdr->hwndFrom != m_list) return false;
    const Theme& th = CurrentTheme();
    switch (hdr->code) {
        case LVN_GETDISPINFOW: {
            auto* di = (NMLVDISPINFOW*)hdr;
            if (!(di->item.mask & LVIF_TEXT)) return true;
            const int r = di->item.iItem, sub = di->item.iSubItem;
            if (sub == 0) {
                m_textBuf = r >= m_doc->table.Rows() ? L"*" : (m_doc->IsExcluded(r) ? L"\x2715 " : L"") + std::to_wstring(r + 1);
            } else {
                m_textBuf = m_doc->table.Cell(r, sub - 1);
            }
            di->item.pszText = m_textBuf.data();
            result = 0;
            return true;
        }
        case NM_CUSTOMDRAW: {
            auto* cd = (NMLVCUSTOMDRAW*)hdr;
            switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: result = CDRF_NOTIFYITEMDRAW; return true;
                case CDDS_ITEMPREPAINT:
                    cd->nmcd.uItemState &= ~(UINT)(CDIS_SELECTED | CDIS_FOCUS);
                    result = CDRF_NOTIFYSUBITEMDRAW;
                    return true;
                case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                    const int r = (int)cd->nmcd.dwItemSpec, sub = cd->iSubItem;
                    cd->nmcd.uItemState &= ~(UINT)(CDIS_SELECTED | CDIS_FOCUS);
                    const bool selected = ListView_GetItemState(m_list, r, LVIS_SELECTED) != 0;
                    const bool current = selected && r == CurrentRow() && sub == m_col + 1;
                    COLORREF bk = th.listBg;
                    if (current) bk = th.dark ? RGB(0, 95, 184) : RGB(153, 201, 239);
                    else if (selected) bk = th.dark ? RGB(45, 62, 82) : RGB(220, 236, 250);
                    COLORREF text = th.listText;
                    if (sub == 0 || m_doc->IsExcluded(r)) text = th.listDim;
                    if (sub > 0) {
                        const int c = sub - 1;
                        const std::wstring& s = m_doc->table.Cell(r, c);
                        double v;
                        if ((c == m_doc->xCol || c == m_doc->yCol || c == m_doc->sigmaCol) && !s.empty() &&
                            !ParseNumber(s, v))
                            text = th.danger;  // not a number in a column that is used
                    }
                    if (current && th.dark) text = RGB(255, 255, 255);
                    cd->clrText = text;
                    cd->clrTextBk = bk;
                    result = CDRF_DODEFAULT;
                    return true;
                }
            }
            result = CDRF_DODEFAULT;
            return true;
        }
        case NM_CLICK:
        case NM_DBLCLK:
        case NM_RCLICK: {
            auto* ia = (NMITEMACTIVATE*)hdr;
            if (ia->iSubItem > 0) {
                m_col = ia->iSubItem - 1;
                InvalidateRect(m_list, nullptr, FALSE);
                if (onCursorMoved) onCursorMoved();
            }
            if (hdr->code == NM_DBLCLK && ia->iItem >= 0 && ia->iSubItem > 0) BeginEdit(nullptr);
            if (hdr->code == NM_RCLICK && onContextMenu) {
                POINT pt;
                GetCursorPos(&pt);
                onContextMenu(pt);
            }
            result = 0;
            return true;
        }
        case LVN_COLUMNCLICK: {
            auto* nm = (NMLISTVIEW*)hdr;
            if (onHeaderMenu) {
                POINT pt;
                GetCursorPos(&pt);
                onHeaderMenu(pt, nm->iSubItem - 1);
            }
            result = 0;
            return true;
        }
        case LVN_ITEMCHANGED:
            if (onCursorMoved) onCursorMoved();
            InvalidateRect(m_list, nullptr, FALSE);
            result = 0;
            return true;
        case LVN_BEGINSCROLL:
            if (m_edit) CommitEdit(0, 0);
            result = 0;
            return true;
    }
    return false;
}

LRESULT DataGrid::DrawHeader(NMCUSTOMDRAW* cd) {
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
    const bool currentCol = sub == m_col + 1;
    HBRUSH bg = CreateSolidBrush((cd->uItemState & CDIS_SELECTED) ? th.barPressed : currentCol ? th.barHover : th.barBg);
    FillRect(cd->hdc, &r, bg);
    DeleteObject(bg);
    RECT line = {r.right - 1, r.top + Dpi(4, m_dpi), r.right, r.bottom - Dpi(4, m_dpi)};
    RECT bottom = {r.left, r.bottom - 1, r.right, r.bottom};
    HBRUSH border = CreateSolidBrush(th.barBorder);
    FillRect(cd->hdc, &line, border);
    FillRect(cd->hdc, &bottom, border);
    DeleteObject(border);
    const int pad = Dpi(6, m_dpi);
    RECT text = {r.left + pad, r.top, r.right - pad, r.bottom};
    std::wstring t = sub == 0 ? L"#" : (sub - 1 < m_doc->table.Cols() ? HeaderText(sub - 1) : L"");
    HGDIOBJ oldFont = SelectObject(cd->hdc, m_font);
    SetBkMode(cd->hdc, TRANSPARENT);
    const int c = sub - 1;
    const bool role = c >= 0 && (c == m_doc->xCol || c == m_doc->yCol || c == m_doc->sigmaCol);
    SetTextColor(cd->hdc, role ? th.accent : th.barText);
    DrawTextW(cd->hdc, t.c_str(), -1, &text,
              (sub == 0 ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(cd->hdc, oldFont);
    return CDRF_SKIPDEFAULT;
}

void DataGrid::OnThemeChanged() {
    const Theme& th = CurrentTheme();
    SetWindowTheme(m_list, th.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    // Grid lines are drawn in a light system colour that glares in dark mode.
    ListView_SetExtendedListViewStyleEx(m_list, LVS_EX_GRIDLINES, th.dark ? 0 : LVS_EX_GRIDLINES);
    ListView_SetBkColor(m_list, th.listBg);
    ListView_SetTextBkColor(m_list, th.listBg);
    ListView_SetTextColor(m_list, th.listText);
    InvalidateRect(m_list, nullptr, TRUE);
}

void DataGrid::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_list);
    if (m_font) DeleteObject(m_font);
    m_font = CreateMessageFont(m_dpi);
    SendMessageW(m_list, WM_SETFONT, (WPARAM)m_font, TRUE);
    BuildColumns();
}
