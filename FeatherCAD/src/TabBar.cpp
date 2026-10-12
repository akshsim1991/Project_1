// TabBar.cpp - see TabBar.h.
#include "TabBar.h"

#include <algorithm>

#include <commctrl.h>
#include <windowsx.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClassName[] = L"FeatherCadTabBar";
constexpr int kHeightDip = 30;
constexpr int kMaxTabDip = 220, kMinTabDip = 70, kPlusDip = 30, kCloseDip = 16, kPadDip = 10;

void Fill(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

COLORREF Mix(COLORREF a, COLORREF b, double t) {
    return RGB((int)(GetRValue(a) * (1 - t) + GetRValue(b) * t), (int)(GetGValue(a) * (1 - t) + GetGValue(b) * t),
               (int)(GetBValue(a) * (1 - t) + GetBValue(b) * t));
}
}  // namespace

TabBar::~TabBar() {
    if (m_font) DeleteObject(m_font);
}

bool TabBar::Create(HWND parent, HWND target) { return Create(parent, target, Ids{}); }

bool TabBar::Create(HWND parent, HWND target, const Ids& ids) {
    m_ids = ids;
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &TabBar::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        registered = RegisterClassExW(&wc) != 0;
    }
    m_target = target;
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    m_font = CreateMessageFont(m_dpi);
    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (m_tooltip) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = m_hwnd;
        ti.uId = (UINT_PTR)m_hwnd;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(m_tooltip, TTM_SETMAXTIPWIDTH, 0, 600);
    }
    return true;
}

int TabBar::Height() const { return Dpi(kHeightDip, m_dpi); }

void TabBar::SetTabs(const std::vector<Tab>& tabs, int active) {
    if (tabs == m_tabs && active == m_active) return;
    m_tabs = tabs;
    m_active = active;
    Layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TabBar::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    if (m_font) DeleteObject(m_font);
    m_font = CreateMessageFont(m_dpi);
    Layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TabBar::OnThemeChanged() { InvalidateRect(m_hwnd, nullptr, FALSE); }

void TabBar::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_rects.clear();
    const int n = (int)m_tabs.size();
    const int avail = std::max(0, (int)rc.right - Dpi(kPlusDip + 8, m_dpi));
    // Each tab as wide as its name needs, all narrower if they do not fit.
    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, m_font);
    std::vector<int> want;
    int total = 0;
    for (const Tab& t : m_tabs) {
        SIZE sz{};
        const std::wstring label = t.title + (t.modified ? L"*" : L"");
        GetTextExtentPoint32W(dc, label.c_str(), (int)label.size(), &sz);
        const int w = std::clamp((int)sz.cx + Dpi(2 * kPadDip + kCloseDip + 6, m_dpi), Dpi(kMinTabDip, m_dpi),
                                 Dpi(kMaxTabDip, m_dpi));
        want.push_back(w);
        total += w;
    }
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    const double shrink = total > avail && total > 0 ? (double)avail / total : 1.0;
    int x = 0;
    for (int i = 0; i < n; ++i) {
        const int w = std::max(Dpi(40, m_dpi), (int)(want[(size_t)i] * shrink));
        m_rects.push_back({x, 0, x + w, rc.bottom});
        x += w;
    }
    m_plus = {x + Dpi(2, m_dpi), Dpi(3, m_dpi), x + Dpi(2 + kPlusDip, m_dpi), rc.bottom - Dpi(3, m_dpi)};
}

RECT TabBar::CloseRect(const RECT& tab) const {
    if (!m_ids.closeFirst) return {tab.right, tab.top, tab.right, tab.top};  // none
    const int s = Dpi(kCloseDip, m_dpi);
    const int cy = (tab.top + tab.bottom) / 2;
    const int right = tab.right - Dpi(6, m_dpi);
    return {right - s, cy - s / 2, right, cy - s / 2 + s};
}

TabBar::Hit TabBar::HitTest(POINT pt) const {
    for (size_t i = 0; i < m_rects.size(); ++i) {
        if (!PtInRect(&m_rects[i], pt)) continue;
        const RECT c = CloseRect(m_rects[i]);
        if (m_ids.closeFirst && PtInRect(&c, pt)) return {TabClose, (int)i};
        return {TabBody, (int)i};
    }
    if (PtInRect(&m_plus, pt)) return {Plus, -1};
    return {};
}

void TabBar::Paint(HDC dc) {
    const Theme& th = CurrentTheme();
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    Fill(dc, rc, th.tabStripBg);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, m_font);
    for (size_t i = 0; i < m_rects.size(); ++i) {
        const RECT r = m_rects[i];
        const bool active = (int)i == m_active;
        const bool hot = m_hot.index == (int)i && m_hot.part != None;
        if (active) Fill(dc, r, th.barBg);
        else if (hot) Fill(dc, r, th.tabHover);
        if (active) {
            RECT line = m_ids.bottom ? RECT{r.left, r.bottom - Dpi(2, m_dpi), r.right, r.bottom}
                                     : RECT{r.left, r.top, r.right, r.top + Dpi(2, m_dpi)};
            Fill(dc, line, th.accent);
        }
        // A thin divider between inactive tabs.
        if (!active && (int)i + 1 != m_active) {
            RECT div = {r.right - 1, r.top + Dpi(7, m_dpi), r.right, r.bottom - Dpi(7, m_dpi)};
            Fill(dc, div, th.barBorder);
        }
        const Tab& t = m_tabs[i];
        const std::wstring label = t.title + (t.modified ? L"*" : L"");
        const RECT close = CloseRect(r);
        RECT text = {r.left + Dpi(kPadDip, m_dpi), r.top, close.left - Dpi(4, m_dpi), r.bottom};
        SetTextColor(dc, active ? th.barText : Mix(th.barText, th.tabStripBg, 0.3));
        DrawTextW(dc, label.c_str(), (int)label.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (m_ids.closeFirst && (active || hot)) {
            if (m_hot.part == TabClose && m_hot.index == (int)i) Fill(dc, close, th.barPressed);
            HPEN pen = CreatePen(PS_SOLID, std::max(1, Dpi(1, m_dpi)), th.barText);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            const int m = Dpi(4, m_dpi);
            MoveToEx(dc, close.left + m, close.top + m, nullptr);
            LineTo(dc, close.right - m, close.bottom - m);
            MoveToEx(dc, close.right - m - 1, close.top + m, nullptr);
            LineTo(dc, close.left + m - 1, close.bottom - m);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
    }
    // The + button.
    if (m_hot.part == Plus) Fill(dc, m_plus, th.tabHover);
    {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, Dpi(1, m_dpi)), th.barText);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int cx = (m_plus.left + m_plus.right) / 2, cy = (m_plus.top + m_plus.bottom) / 2, h = Dpi(5, m_dpi);
        MoveToEx(dc, cx - h, cy, nullptr);
        LineTo(dc, cx + h + 1, cy);
        MoveToEx(dc, cx, cy - h, nullptr);
        LineTo(dc, cx, cy + h + 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }
    // The edge where the drawing area is.
    RECT bottom = m_ids.bottom ? RECT{rc.left, rc.top, rc.right, rc.top + 1} : RECT{rc.left, rc.bottom - 1, rc.right, rc.bottom};
    Fill(dc, bottom, th.barBorder);
    SelectObject(dc, oldFont);
}

LRESULT CALLBACK TabBar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TabBar* self;
    if (msg == WM_NCCREATE) {
        self = (TabBar*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (TabBar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TabBar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(m_hwnd, &ps);
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            // Double-buffered, so switching tabs does not flicker.
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            Paint(mem);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!m_tracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
                m_tracking = TrackMouseEvent(&tme) != FALSE;
            }
            const Hit h = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (!(h == m_hot)) {
                const bool tabChanged = h.index != m_hot.index;
                m_hot = h;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                if (tabChanged && m_tooltip) SendMessageW(m_tooltip, TTM_UPDATE, 0, 0);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hot = {};
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const Hit h = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (h.part == TabBody) SendMessageW(m_target, WM_COMMAND, m_ids.tabFirst + h.index, 0);
            else if (h.part == TabClose) SendMessageW(m_target, WM_COMMAND, m_ids.closeFirst + h.index, 0);
            else if (h.part == Plus || (msg == WM_LBUTTONDBLCLK && h.part == None)) SendMessageW(m_target, WM_COMMAND, m_ids.plus, 0);
            return 0;
        }
        case WM_MBUTTONUP: {
            const Hit h = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (m_ids.closeFirst && (h.part == TabBody || h.part == TabClose))
                SendMessageW(m_target, WM_COMMAND, m_ids.closeFirst + h.index, 0);
            return 0;
        }
        case WM_RBUTTONUP: {
            const Hit h = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (m_ids.menuFirst && h.part == TabBody) {
                SendMessageW(m_target, WM_COMMAND, m_ids.tabFirst + h.index, 0);
                SendMessageW(m_target, WM_COMMAND, m_ids.menuFirst + h.index, 0);
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = (NMHDR*)lp;
            if (nm->hwndFrom == m_tooltip && nm->code == TTN_GETDISPINFOW) {
                auto* di = (NMTTDISPINFOW*)lp;
                static std::wstring text;
                text.clear();
                if (m_hot.part == Plus) text = m_ids.plusTip;
                else if (m_hot.part == TabClose) text = L"Close (Ctrl+W)";
                else if (m_hot.index >= 0 && m_hot.index < (int)m_tabs.size()) text = m_tabs[(size_t)m_hot.index].tip;
                di->lpszText = const_cast<wchar_t*>(text.c_str());
                return 0;
            }
            break;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
