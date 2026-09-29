// TabBar.cpp - self-drawn tab strip (see TabBar.h).
#include "TabBar.h"

#include <algorithm>

#include <commctrl.h>
#include <windowsx.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClassName[] = L"FeatherTabBar";
constexpr int kMinTabDip = 72;
constexpr int kMaxTabDip = 220;
constexpr int kNewButtonDip = 32;
constexpr int kCloseDip = 20;
const wchar_t kGlyphClose[] = L"\xE711";
const wchar_t kGlyphAdd[] = L"\xE710";

void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
}

void FillRound(HDC dc, const RECT& r, COLORREF c, int radius) {
    HBRUSH brush = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(dc, brush), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(brush);
    DeleteObject(pen);
}

const wchar_t* IconFont() {
    static const wchar_t* name = [] {
        HDC hdc = GetDC(nullptr);
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
        bool found = false;
        EnumFontFamiliesExW(
            hdc, &lf,
            [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
                *(bool*)p = true;
                return 0;
            },
            (LPARAM)&found, 0);
        ReleaseDC(nullptr, hdc);
        return found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }();
    return name;
}
}  // namespace

TabBar::~TabBar() {
    if (m_textFont) DeleteObject(m_textFont);
    if (m_iconFont) DeleteObject(m_iconFont);
}

bool TabBar::Create(HWND parent, HWND target, int heightDip) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &TabBar::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_target = target;
    m_heightDip = heightDip;
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
                             nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    return true;
}

int TabBar::Height() const { return Dpi(m_heightDip, m_dpi); }

void TabBar::CreateFonts() {
    if (m_textFont) DeleteObject(m_textFont);
    if (m_iconFont) DeleteObject(m_iconFont);
    m_textFont = CreateMessageFont(m_dpi);
    m_iconFont = CreateFontW(-Dpi(10, m_dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, IconFont());
}

void TabBar::SetTabs(std::vector<TabInfo> tabs, int active) {
    m_tabs = std::move(tabs);
    m_active = active;
    m_hot = -1;
    Layout();
    UpdateTooltips();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TabBar::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    Layout();
    UpdateTooltips();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TabBar::OnThemeChanged() { InvalidateRect(m_hwnd, nullptr, FALSE); }

void TabBar::Layout() {
    RECT client;
    GetClientRect(m_hwnd, &client);
    const int pad = Dpi(6, m_dpi);
    const int top = Dpi(5, m_dpi);
    const int n = (int)m_tabs.size();
    const int avail = client.right - 2 * pad - Dpi(kNewButtonDip, m_dpi);
    int w = n ? avail / n : 0;
    w = std::max(Dpi(kMinTabDip, m_dpi), std::min(Dpi(kMaxTabDip, m_dpi), w));
    m_tabRects.assign((size_t)n, RECT{});
    int x = pad;
    for (int i = 0; i < n; ++i) {
        m_tabRects[(size_t)i] = {x, top, x + w, client.bottom};
        x += w;
    }
    const int nb = Dpi(kNewButtonDip - 4, m_dpi);
    const int y = top + (client.bottom - top - nb) / 2;
    m_newRect = {x + Dpi(4, m_dpi), y, x + Dpi(4, m_dpi) + nb, y + nb};
}

void TabBar::UpdateTooltips() {
    if (!m_tooltip) return;
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = m_hwnd;
    for (int i = 0; i < m_toolCount; ++i) {
        ti.uId = (UINT_PTR)i + 1;
        SendMessageW(m_tooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    m_toolCount = (int)m_tabs.size();
    for (int i = 0; i < m_toolCount; ++i) {
        ti.uFlags = TTF_SUBCLASS;
        ti.uId = (UINT_PTR)i + 1;
        ti.rect = m_tabRects[(size_t)i];
        ti.lpszText = const_cast<wchar_t*>(m_tabs[(size_t)i].tip.c_str());
        SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

int TabBar::HitTest(POINT pt, bool* onClose, bool* onNew) const {
    *onClose = false;
    *onNew = PtInRect(&m_newRect, pt) != FALSE;
    for (size_t i = 0; i < m_tabRects.size(); ++i) {
        const RECT& r = m_tabRects[i];
        if (!PtInRect(&r, pt)) continue;
        const int cs = Dpi(kCloseDip, m_dpi);
        const int cx = r.right - Dpi(6, m_dpi) - cs;
        const int cy = r.top + (r.bottom - r.top - cs) / 2;
        *onClose = pt.x >= cx && pt.x < cx + cs && pt.y >= cy && pt.y < cy + cs;
        return (int)i;
    }
    return -1;
}

void TabBar::Notify(TabAction action, int index) {
    PostMessageW(m_target, WM_APP_TABBAR, (WPARAM)action, index);
}

void TabBar::Paint(HDC hdc) {
    const Theme& th = CurrentTheme();
    RECT client;
    GetClientRect(m_hwnd, &client);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, client.right), std::max(1L, client.bottom));
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    Fill(mem, client, th.tabStripBg);
    SetBkMode(mem, TRANSPARENT);

    const int radius = Dpi(8, m_dpi);
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        RECT r = m_tabRects[i];
        const bool active = (int)i == m_active, hot = (int)i == m_hot;
        if (active || hot) {
            // Rounded top corners: round rect extended below the strip.
            RECT rr = {r.left, r.top, r.right, r.bottom + radius};
            FillRound(mem, rr, active ? th.barBg : th.tabHover, radius);
        } else if (i + 1 < m_tabs.size() && (int)i + 1 != m_active && (int)i + 1 != m_hot) {
            RECT sep = {r.right - 1, r.top + Dpi(8, m_dpi), r.right, r.bottom - Dpi(6, m_dpi)};
            Fill(mem, sep, th.barBorder);
        }
        // Title
        const int cs = Dpi(kCloseDip, m_dpi);
        RECT text = {r.left + Dpi(12, m_dpi), r.top, r.right - Dpi(10, m_dpi) - cs, r.bottom};
        SelectObject(mem, m_textFont);
        SetTextColor(mem, th.barText);
        DrawTextW(mem, m_tabs[i].title.c_str(), -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        // Close button
        RECT close = {r.right - Dpi(6, m_dpi) - cs, r.top + (r.bottom - r.top - cs) / 2, 0, 0};
        close.right = close.left + cs;
        close.bottom = close.top + cs;
        if (hot && m_hotClose) FillRound(mem, close, th.barPressed, Dpi(4, m_dpi));
        SelectObject(mem, m_iconFont);
        SetTextColor(mem, th.barText);
        DrawTextW(mem, kGlyphClose, -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // "+" (open in new tab)
    if (m_hotNew) FillRound(mem, m_newRect, th.tabHover, radius);
    SelectObject(mem, m_iconFont);
    SetTextColor(mem, th.barText);
    RECT nr = m_newRect;
    DrawTextW(mem, kGlyphAdd, -1, &nr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
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
    const POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    bool onClose = false, onNew = false;
    switch (msg) {
        case WM_SIZE:
            Layout();
            UpdateTooltips();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            Paint(hdc);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!m_tracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
                m_tracking = TrackMouseEvent(&tme) != FALSE;
            }
            int hit = HitTest(pt, &onClose, &onNew);
            if (hit != m_hot || onClose != m_hotClose || onNew != m_hotNew) {
                m_hot = hit;
                m_hotClose = onClose;
                m_hotNew = onNew;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hot = -1;
            m_hotClose = m_hotNew = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            int hit = HitTest(pt, &onClose, &onNew);
            if (onNew) {
                Notify(TabAction::New, -1);
            } else if (hit >= 0 && onClose) {
                m_pressedClose = hit;  // close on button-up, like browsers
            } else if (hit >= 0) {
                Notify(TabAction::Select, hit);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            int hit = HitTest(pt, &onClose, &onNew);
            if (m_pressedClose >= 0 && hit == m_pressedClose && onClose)
                Notify(TabAction::Close, hit);
            m_pressedClose = -1;
            return 0;
        }
        case WM_MBUTTONUP: {  // middle click closes a tab
            int hit = HitTest(pt, &onClose, &onNew);
            if (hit >= 0) Notify(TabAction::Close, hit);
            return 0;
        }
        case WM_LBUTTONDBLCLK:  // double click on empty strip: open a file
            if (HitTest(pt, &onClose, &onNew) < 0 && !onNew) Notify(TabAction::New, -1);
            return 0;
        case WM_MOUSEWHEEL:  // wheel over the strip switches tabs
            if (!m_tabs.empty()) {
                int next = m_active + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1);
                if (next >= 0 && next < (int)m_tabs.size()) Notify(TabAction::Select, next);
            }
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
