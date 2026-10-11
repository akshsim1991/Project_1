// PropertiesPanel.cpp - see PropertiesPanel.h.
#include "PropertiesPanel.h"

#include <algorithm>

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClass[] = L"FeatherCadProperties";
constexpr int kWidthDip = 250, kRowDip = 26, kPadDip = 8;
constexpr UINT_PTR kEditSubclass = 3;
}  // namespace

PropertiesPanel::~PropertiesPanel() {
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
    if (m_bg) DeleteObject(m_bg);
}

bool PropertiesPanel::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &PropertiesPanel::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 0, 0, parent,
                             nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_title = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 0,
                              0, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    OnDpiChanged();
    OnThemeChanged();
    return true;
}

int PropertiesPanel::Width() const { return Dpi(kWidthDip, m_dpi); }

void PropertiesPanel::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
    m_font = CreateMessageFont(m_dpi);
    LOGFONTW lf{};
    GetObjectW(m_font, sizeof(lf), &lf);
    lf.lfWeight = FW_SEMIBOLD;
    m_bold = CreateFontIndirectW(&lf);
    SendMessageW(m_title, WM_SETFONT, (WPARAM)m_bold, FALSE);
    for (Live& l : m_rows) {
        SendMessageW(l.label, WM_SETFONT, (WPARAM)(l.row.heading ? m_bold : m_font), FALSE);
        if (l.ctrl) SendMessageW(l.ctrl, WM_SETFONT, (WPARAM)m_font, FALSE);
    }
    Layout();
}

void PropertiesPanel::OnThemeChanged() {
    if (m_bg) DeleteObject(m_bg);
    m_bg = CreateSolidBrush(CurrentTheme().barBg);
    ApplyScrollbarTheme(m_hwnd);
    for (Live& l : m_rows)
        if (l.ctrl) SetWindowTheme(l.ctrl, CurrentTheme().dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

bool PropertiesPanel::Editing() const {
    HWND f = GetFocus();
    return f && (f == m_hwnd || IsChild(m_hwnd, f));
}

void PropertiesPanel::Clear() {
    for (Live& l : m_rows) {
        if (l.label) DestroyWindow(l.label);
        if (l.ctrl) DestroyWindow(l.ctrl);
    }
    m_rows.clear();
}

void PropertiesPanel::SetContent(const std::wstring& title, std::vector<Row> rows) {
    m_building = true;
    SendMessageW(m_hwnd, WM_SETREDRAW, FALSE, 0);
    Clear();
    SetWindowTextW(m_title, title.c_str());
    HINSTANCE inst = GetModuleHandleW(nullptr);
    for (Row& r : rows) {
        Live l;
        l.row = std::move(r);
        const Row& row = l.row;
        const bool wide = row.heading || (row.kind == Row::Check);
        l.label = CreateWindowExW(0, L"STATIC", wide && row.kind == Row::Check ? L"" : row.label.c_str(),
                                  WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS | SS_CENTERIMAGE, 0, 0,
                                  0, 0, m_hwnd, nullptr, inst, nullptr);
        SendMessageW(l.label, WM_SETFONT, (WPARAM)(row.heading ? m_bold : m_font), FALSE);
        const int id = 100 + (int)m_rows.size();
        switch (row.kind) {
            case Row::Info:
                if (!row.heading) {
                    l.ctrl = CreateWindowExW(0, L"EDIT", row.value.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                             0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)id, inst, nullptr);
                }
                break;
            case Row::Text:
                l.ctrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", row.value.c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hwnd,
                                         (HMENU)(INT_PTR)id, inst, nullptr);
                SetWindowSubclass(l.ctrl, &PropertiesPanel::EditProc, kEditSubclass, (DWORD_PTR)this);
                break;
            case Row::Choice: {
                l.ctrl = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                         0, 0, 0, Dpi(300, m_dpi), m_hwnd, (HMENU)(INT_PTR)id, inst, nullptr);
                for (const std::wstring& c : row.choices) SendMessageW(l.ctrl, CB_ADDSTRING, 0, (LPARAM)c.c_str());
                SendMessageW(l.ctrl, CB_SETCURSEL, (WPARAM)row.choice, 0);
                break;
            }
            case Row::Check:
                l.ctrl = CreateWindowExW(0, L"BUTTON", row.label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)id, inst, nullptr);
                SendMessageW(l.ctrl, BM_SETCHECK, row.checked ? BST_CHECKED : BST_UNCHECKED, 0);
                break;
        }
        if (l.ctrl) {
            SendMessageW(l.ctrl, WM_SETFONT, (WPARAM)m_font, FALSE);
            SetWindowTheme(l.ctrl, CurrentTheme().dark ? L"DarkMode_CFD" : L"Explorer", nullptr);
        }
        m_rows.push_back(std::move(l));
    }
    m_scroll = 0;
    Layout();
    SendMessageW(m_hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
    m_building = false;
}

void PropertiesPanel::Layout() {
    if (!m_hwnd) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int pad = Dpi(kPadDip, m_dpi), rowH = Dpi(kRowDip, m_dpi), w = rc.right;
    const int labelW = (w - 3 * pad) * 45 / 100;
    int y = pad - m_scroll;
    MoveWindow(m_title, pad, y, w - 2 * pad, rowH, TRUE);
    y += rowH + pad / 2;
    for (Live& l : m_rows) {
        const int ctrlH = rowH - Dpi(4, m_dpi);
        if (l.row.heading) {
            y += pad / 2;
            MoveWindow(l.label, pad, y, w - 2 * pad, rowH, TRUE);
        } else if (l.row.kind == Row::Check) {
            MoveWindow(l.label, 0, 0, 0, 0, FALSE);
            MoveWindow(l.ctrl, pad, y + 2, w - 2 * pad, ctrlH, TRUE);
        } else {
            MoveWindow(l.label, pad, y, labelW, rowH, TRUE);
            if (l.ctrl) MoveWindow(l.ctrl, 2 * pad + labelW, y + Dpi(2, m_dpi), w - 3 * pad - labelW, ctrlH, TRUE);
        }
        y += rowH;
    }
    m_contentHeight = y + m_scroll + pad;
    SCROLLINFO si{sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, m_contentHeight - 1);
    si.nPage = (UINT)std::max(1L, rc.bottom);
    si.nPos = m_scroll;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

void PropertiesPanel::Commit(size_t index) {
    if (m_building || index >= m_rows.size()) return;
    Live& l = m_rows[index];
    if (!l.row.apply || !l.ctrl) return;
    Row edited = l.row;
    switch (l.row.kind) {
        case Row::Text: {
            const int n = GetWindowTextLengthW(l.ctrl);
            std::wstring t((size_t)n + 1, L'\0');
            t.resize((size_t)GetWindowTextW(l.ctrl, t.data(), n + 1));
            if (t == l.row.value) return;
            edited.value = t;
            break;
        }
        case Row::Choice: {
            const int i = (int)SendMessageW(l.ctrl, CB_GETCURSEL, 0, 0);
            if (i < 0 || i == l.row.choice) return;
            edited.choice = i;
            break;
        }
        case Row::Check:
            edited.checked = SendMessageW(l.ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        default:
            return;
    }
    // The owner may rebuild the panel from inside apply: keep a copy of it.
    const auto apply = l.row.apply;
    apply(edited);
}

LRESULT CALLBACK PropertiesPanel::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = (PropertiesPanel*)ref;
    if (msg == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, msg, wp, lp);
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        for (size_t i = 0; i < self->m_rows.size(); ++i)
            if (self->m_rows[i].ctrl == hwnd) {
                if (wp == VK_RETURN) {
                    self->Commit(i);
                } else {
                    SetWindowTextW(hwnd, self->m_rows[i].row.value.c_str());
                }
                // Back to the drawing: typing goes to the command line again.
                SetFocus(GetParent(self->m_hwnd));
                return 0;
            }
    }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK PropertiesPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PropertiesPanel* self;
    if (msg == WM_NCCREATE) {
        self = (PropertiesPanel*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (PropertiesPanel*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PropertiesPanel::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            FillRect((HDC)wp, &rc, m_bg);
            RECT edge = {0, 0, 1, rc.bottom};
            HBRUSH b = CreateSolidBrush(CurrentTheme().barBorder);
            FillRect((HDC)wp, &edge, b);
            DeleteObject(b);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN: {
            const Theme& th = CurrentTheme();
            HDC dc = (HDC)wp;
            const bool input = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
            SetTextColor(dc, th.barText);
            SetBkColor(dc, input ? th.editBg : th.barBg);
            return (LRESULT)(input ? th.editBrush : m_bg);
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp) - 100;
            if (id < 0 || id >= (int)m_rows.size()) break;
            const int code = HIWORD(wp);
            const Row::Kind kind = m_rows[(size_t)id].row.kind;
            if ((kind == Row::Text && code == EN_KILLFOCUS) || (kind == Row::Choice && code == CBN_SELCHANGE) ||
                (kind == Row::Check && code == BN_CLICKED))
                Commit((size_t)id);
            return 0;
        }
        case WM_VSCROLL: {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            int pos = m_scroll;
            const int line = Dpi(kRowDip, m_dpi);
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= line; break;
                case SB_LINEDOWN: pos += line; break;
                case SB_PAGEUP: pos -= rc.bottom; break;
                case SB_PAGEDOWN: pos += rc.bottom; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: pos = HIWORD(wp); break;
            }
            m_scroll = std::clamp(pos, 0, std::max(0, m_contentHeight - (int)rc.bottom));
            Layout();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            m_scroll = std::clamp(m_scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * Dpi(kRowDip, m_dpi) * 2, 0,
                                  std::max(0, m_contentHeight - (int)rc.bottom));
            Layout();
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
