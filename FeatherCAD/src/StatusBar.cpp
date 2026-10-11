// StatusBar.cpp - see StatusBar.h.
#include "StatusBar.h"

#include <algorithm>

#include <commctrl.h>
#include <windowsx.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClass[] = L"FeatherCadStatusBar";
}

StatusBar::StatusBar() {
    m_buttons = {
        {ID_TOGGLE_GRID, ID_DRAFTING_SETTINGS, L"GRID", L"Grid (F7). Right-click for settings."},
        {ID_TOGGLE_SNAP, ID_DRAFTING_SETTINGS, L"SNAP", L"Snap to the grid spacing (F9). Right-click for settings."},
        {ID_TOGGLE_ORTHO, ID_DRAFTING_SETTINGS, L"ORTHO", L"Horizontal and vertical only (F8, or hold Shift)"},
        {ID_TOGGLE_OSNAP, ID_OSNAP_SETTINGS, L"OSNAP", L"Object snaps (F3). Right-click to choose them."},
        {ID_UNITS, ID_UNITS, L"mm", L"Drawing units and decimal places"},
    };
}

StatusBar::~StatusBar() {
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
}

bool StatusBar::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &StatusBar::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = m_hwnd;
        ti.uId = i + 1;
        ti.lpszText = const_cast<wchar_t*>(m_buttons[i].tip.c_str());
        SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    OnDpiChanged();
    return true;
}

int StatusBar::Height() const { return Dpi(28, m_dpi); }

void StatusBar::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
    m_font = CreateMessageFont(m_dpi, 95);
    LOGFONTW lf{};
    GetObjectW(m_font, sizeof(lf), &lf);
    lf.lfWeight = FW_SEMIBOLD;
    m_bold = CreateFontIndirectW(&lf);
    Layout();
}

void StatusBar::OnThemeChanged() { InvalidateRect(m_hwnd, nullptr, TRUE); }

void StatusBar::SetCoordinates(const std::wstring& text) {
    if (text == m_coords) return;
    m_coords = text;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void StatusBar::SetInfo(const std::wstring& text) {
    if (text == m_info) return;
    m_info = text;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void StatusBar::SetToggles(bool grid, bool snap, bool ortho, bool osnap) {
    const bool states[4] = {grid, snap, ortho, osnap};
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        if (m_buttons[i].on != states[i]) changed = true;
        m_buttons[i].on = states[i];
    }
    if (changed && m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void StatusBar::SetUnits(const std::wstring& text) {
    if (m_buttons.back().text == text) return;
    m_buttons.back().text = text.empty() ? L"units" : text;
    Layout();
}

void StatusBar::Layout() {
    if (!m_hwnd) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    HDC hdc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(hdc, m_bold);
    int x = rc.right - Dpi(6, m_dpi);
    for (int i = (int)m_buttons.size() - 1; i >= 0; --i) {
        Button& b = m_buttons[(size_t)i];
        SIZE sz{};
        GetTextExtentPoint32W(hdc, b.text.c_str(), (int)b.text.size(), &sz);
        const int w = sz.cx + Dpi(18, m_dpi);
        b.rc = {x - w, Dpi(3, m_dpi), x, rc.bottom - Dpi(3, m_dpi)};
        x -= w + Dpi(4, m_dpi);
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = m_hwnd;
        ti.uId = (UINT_PTR)i + 1;
        ti.rect = b.rc;
        SendMessageW(m_tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
    }
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

int StatusBar::HitTest(POINT pt) const {
    for (size_t i = 0; i < m_buttons.size(); ++i)
        if (PtInRect(&m_buttons[i].rc, pt)) return (int)i;
    return -1;
}

void StatusBar::Paint(HDC hdc) {
    const Theme& th = CurrentTheme();
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(th.barBg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    RECT top = {0, 0, rc.right, 1};
    HBRUSH border = CreateSolidBrush(th.barBorder);
    FillRect(mem, &top, border);
    DeleteObject(border);
    SetBkMode(mem, TRANSPARENT);

    HGDIOBJ oldFont = SelectObject(mem, m_font);
    SetTextColor(mem, th.barText);
    RECT cr = {Dpi(10, m_dpi), 0, Dpi(230, m_dpi), rc.bottom};
    DrawTextW(mem, m_coords.c_str(), (int)m_coords.size(), &cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(mem, th.barTextDisabled);
    const int right = m_buttons.empty() ? rc.right : m_buttons.front().rc.left - Dpi(10, m_dpi);
    RECT ir = {cr.right, 0, right, rc.bottom};
    DrawTextW(mem, m_info.c_str(), (int)m_info.size(), &ir,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SelectObject(mem, m_bold);
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        const Button& b = m_buttons[i];
        const bool isToggle = i < 4;
        const bool on = isToggle && b.on;
        if (on || (int)i == m_hot) {
            HBRUSH fill = CreateSolidBrush(on ? th.barPressed : th.barHover);
            HPEN pen = CreatePen(PS_SOLID, 1, on ? th.barPressed : th.barHover);
            HGDIOBJ ob = SelectObject(mem, fill), op = SelectObject(mem, pen);
            RoundRect(mem, b.rc.left, b.rc.top, b.rc.right, b.rc.bottom, Dpi(6, m_dpi), Dpi(6, m_dpi));
            SelectObject(mem, ob);
            SelectObject(mem, op);
            DeleteObject(fill);
            DeleteObject(pen);
        }
        SetTextColor(mem, on ? th.accent : isToggle ? th.barTextDisabled : th.barText);
        RECT r = b.rc;
        DrawTextW(mem, b.text.c_str(), (int)b.text.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(mem, oldFont);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK StatusBar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StatusBar* self;
    if (msg == WM_NCCREATE) {
        self = (StatusBar*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (StatusBar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT StatusBar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
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
            const int hit = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (hit != m_hot) {
                m_hot = hit;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            m_hot = -1;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            const int hit = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (hit >= 0) {
                const Button& b = m_buttons[(size_t)hit];
                SendMessageW(GetParent(m_hwnd), WM_COMMAND, msg == WM_LBUTTONUP ? b.id : b.rightId, 0);
            }
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
