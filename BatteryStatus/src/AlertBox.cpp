// AlertBox.cpp - a small always-on-top alert window.
#include "AlertBox.h"

#include <algorithm>

#include "Util.h"
#include "resource.h"

namespace {
const wchar_t kClass[] = L"BsAlertBox";
constexpr int kWidthDip = 420, kHeightDip = 172;
}  // namespace

AlertBox::~AlertBox() {
    Close();
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
}

void AlertBox::CreateFonts() {
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
    m_font = CreateMessageFont(m_dpi);
    LOGFONTW lf{};
    GetObjectW(m_font, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    lf.lfHeight = MulDiv(lf.lfHeight, 115, 100);
    m_bold = CreateFontIndirectW(&lf);
}

void AlertBox::Show(HWND owner, const std::wstring& title, const std::wstring& text, bool critical) {
    m_title = title;
    m_text = text;
    m_critical = critical;
    if (m_hwnd) {
        // Already open: show the newest message instead of another window.
        InvalidateRect(m_hwnd, nullptr, TRUE);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        FlashWindow(m_hwnd, TRUE);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &AlertBox::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_dpi = GetWindowDpi(nullptr);
    CreateFonts();
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT r = {0, 0, Dpi(kWidthDip, m_dpi), Dpi(kHeightDip, m_dpi)};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD ex = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    if (owner && !IsWindow(owner)) owner = nullptr;
    AdjustWindowRectEx(&r, style, FALSE, ex);
    const int w = r.right - r.left, h = r.bottom - r.top;
    // Owned by the main window: no extra taskbar button, and it still shows
    // when the main window is hidden in the tray.
    m_hwnd = CreateWindowExW(ex, kClass, APP_NAME, style, work.left + (work.right - work.left - w) / 2,
                             work.top + (work.bottom - work.top - h) / 3, w, h, owner, nullptr, GetModuleHandleW(nullptr),
                             this);
    if (!m_hwnd) return;
    m_ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, m_hwnd,
                           (HMENU)(INT_PTR)IDOK, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(m_ok, WM_SETFONT, (WPARAM)m_font, TRUE);
    Layout();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_ok);
    FlashWindow(m_hwnd, TRUE);
}

void AlertBox::Close() {
    if (!m_hwnd) return;
    HWND h = m_hwnd;
    m_hwnd = nullptr;
    DestroyWindow(h);
}

void AlertBox::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int bw = Dpi(88, m_dpi), bh = Dpi(28, m_dpi), m = Dpi(14, m_dpi);
    MoveWindow(m_ok, rc.right - m - bw, rc.bottom - m - bh, bw, bh, TRUE);
}

LRESULT CALLBACK AlertBox::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AlertBox* self;
    if (msg == WM_NCCREATE) {
        self = (AlertBox*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (AlertBox*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self && msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self->m_hwnd == hwnd) self->m_hwnd = nullptr;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self ? self->Handle(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AlertBox::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) Close();
            return 0;
        case WM_CLOSE:
            Close();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int m = Dpi(18, m_dpi), icon = Dpi(32, m_dpi);
            HICON ic = LoadIconW(nullptr, m_critical ? IDI_ERROR : IDI_WARNING);
            DrawIconEx(hdc, m, m, ic, icon, icon, 0, nullptr, DI_NORMAL);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            const int tx = m + icon + Dpi(14, m_dpi);
            RECT tr = {tx, m - Dpi(2, m_dpi), rc.right - m, m + Dpi(24, m_dpi)};
            HGDIOBJ old = SelectObject(hdc, m_bold);
            DrawTextW(hdc, m_title.c_str(), -1, &tr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(hdc, m_font);
            RECT br = {tx, m + Dpi(28, m_dpi), rc.right - m, rc.bottom - Dpi(52, m_dpi)};
            const std::wstring body = m_text + L"\n\nThis closes by itself when the charger is plugged in.";
            DrawTextW(hdc, body.c_str(), -1, &br, DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, old);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DPICHANGED: {
            m_dpi = HIWORD(wp);
            CreateFonts();
            SendMessageW(m_ok, WM_SETFONT, (WPARAM)m_font, TRUE);
            const RECT* r = (const RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
            Layout();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
