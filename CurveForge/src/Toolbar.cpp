// Toolbar.cpp - flat self-drawn toolbar (see Toolbar.h).
#include "Toolbar.h"

#include <algorithm>

#include <commctrl.h>
#include <windowsx.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClassName[] = L"CfToolbar";
constexpr int kButtonDip = 32;   // square button size
constexpr int kItemGapDip = 2;
constexpr int kSeparatorDip = 13;
constexpr int kChildHeightDip = 28;

int CALLBACK FontExistsProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *(bool*)found = true;
    return 0;
}

// Windows 11 ships "Segoe Fluent Icons"; Windows 10 has "Segoe MDL2 Assets".
// Both use the same code points for the glyphs used here.
const wchar_t* IconFontName() {
    static const wchar_t* name = [] {
        HDC hdc = GetDC(nullptr);
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
        bool found = false;
        EnumFontFamiliesExW(hdc, &lf, FontExistsProc, (LPARAM)&found, 0);
        ReleaseDC(nullptr, hdc);
        return found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }();
    return name;
}

void FillRound(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}
}  // namespace

Toolbar::~Toolbar() {
    if (m_iconFont) DeleteObject(m_iconFont);
    if (m_textFont) DeleteObject(m_textFont);
}

bool Toolbar::Create(HWND parent, HWND commandTarget, int heightDip) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &Toolbar::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_target = commandTarget;
    m_heightDip = heightDip;
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0,
                             0, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    return true;
}

int Toolbar::Height() const { return Dpi(m_heightDip, m_dpi); }

void Toolbar::CreateFonts() {
    HFONT oldIcon = m_iconFont, oldText = m_textFont;
    m_iconFont = CreateFontW(-Dpi(16, m_dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, IconFontName());
    m_textFont = CreateMessageFont(m_dpi);
    for (auto& it : m_items)
        if (it.child) SendMessageW(it.child, WM_SETFONT, (WPARAM)m_textFont, FALSE);
    if (oldIcon) DeleteObject(oldIcon);
    if (oldText) DeleteObject(oldText);
}

void Toolbar::Add(Item item) {
    m_items.push_back(std::move(item));
    Item& it = m_items.back();
    if (it.child) {
        SetParent(it.child, m_hwnd);
        SendMessageW(it.child, WM_SETFONT, (WPARAM)m_textFont, FALSE);
    }
    if (m_tooltip && !it.tip.empty()) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = m_hwnd;
        ti.uId = m_items.size();  // index + 1
        ti.lpszText = const_cast<wchar_t*>(it.tip.c_str());
        SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    Layout();
}

void Toolbar::AddButton(int id, const wchar_t* glyph, const wchar_t* tip) {
    Item it{Kind::Button};
    it.id = id;
    it.text = glyph;
    it.tip = tip ? tip : L"";
    it.widthDip = kButtonDip;
    Add(std::move(it));
}

void Toolbar::AddTextButton(int id, const wchar_t* text, const wchar_t* tip, int widthDip) {
    Item it{Kind::TextButton};
    it.id = id;
    it.text = text;
    it.tip = tip ? tip : L"";
    it.widthDip = widthDip > 0 ? widthDip : kButtonDip;
    Add(std::move(it));
}

void Toolbar::AddLabel(int id, int widthDip, bool clickable, const wchar_t* tip) {
    Item it{Kind::Label};
    it.id = id;
    it.widthDip = widthDip;
    it.clickable = clickable;
    it.tip = tip ? tip : L"";
    Add(std::move(it));
}

void Toolbar::AddChild(HWND child, int widthDip) {
    Item it{Kind::Child};
    it.child = child;
    it.id = GetDlgCtrlID(child);
    it.widthDip = widthDip;
    it.clickable = false;
    Add(std::move(it));
}

void Toolbar::SetRightAligned(int id, bool right) {
    if (Item* it = Find(id)) it->rightAlign = right;
}

void Toolbar::AddSeparator() {
    Item it{Kind::Separator};
    it.widthDip = kSeparatorDip;
    it.clickable = false;
    Add(std::move(it));
}

void Toolbar::AddSpacer() {
    Item it{Kind::Spacer};
    it.clickable = false;
    Add(std::move(it));
}

Toolbar::Item* Toolbar::Find(int id) {
    for (auto& it : m_items)
        if (it.id == id && it.kind != Kind::Separator && it.kind != Kind::Spacer) return &it;
    return nullptr;
}

const Toolbar::Item* Toolbar::Find(int id) const {
    return const_cast<Toolbar*>(this)->Find(id);
}

void Toolbar::InvalidateItem(int index) {
    if (index >= 0 && index < (int)m_items.size()) InvalidateRect(m_hwnd, &m_items[index].rc, FALSE);
}

void Toolbar::SetText(int id, const std::wstring& text) {
    Item* it = Find(id);
    if (!it || it->text == text) return;  // avoid needless repaints
    it->text = text;
    InvalidateRect(m_hwnd, &it->rc, FALSE);
}

void Toolbar::SetEnabled(int id, bool enabled) {
    Item* it = Find(id);
    if (!it || it->enabled == enabled) return;
    it->enabled = enabled;
    if (it->child) EnableWindow(it->child, enabled);
    InvalidateRect(m_hwnd, &it->rc, FALSE);
}

void Toolbar::SetChecked(int id, bool checked) {
    Item* it = Find(id);
    if (!it || it->checked == checked) return;
    it->checked = checked;
    InvalidateRect(m_hwnd, &it->rc, FALSE);
}

RECT Toolbar::ItemScreenRect(int id) const {
    RECT rc{};
    if (const Item* it = Find(id)) {
        rc = it->rc;
        MapWindowPoints(m_hwnd, nullptr, (POINT*)&rc, 2);
    }
    return rc;
}

void Toolbar::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    Layout();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void Toolbar::OnThemeChanged() {
    InvalidateRect(m_hwnd, nullptr, TRUE);
    for (auto& it : m_items)
        if (it.child) InvalidateRect(it.child, nullptr, TRUE);
}

void Toolbar::Layout() {
    if (!m_hwnd) return;
    RECT client;
    GetClientRect(m_hwnd, &client);
    const int height = client.bottom;
    const int gap = Dpi(kItemGapDip, m_dpi);

    // Spacers and flexible children (negative width: the minimum) share
    // the room that is left.
    int fixed = Dpi(6, m_dpi) * 2, spacers = 0;
    for (auto& it : m_items) {
        if (it.kind == Kind::Spacer || it.widthDip < 0) ++spacers;
        if (it.kind != Kind::Spacer) fixed += (it.widthDip < 0 ? 0 : Dpi(it.widthDip, m_dpi)) + gap;
    }
    const int spacerWidth = spacers ? std::max(0, (int)(client.right - fixed) / spacers) : 0;

    int x = Dpi(6, m_dpi);
    for (size_t i = 0; i < m_items.size(); ++i) {
        Item& it = m_items[i];
        int w = it.kind == Kind::Spacer ? spacerWidth
                : it.widthDip < 0       ? std::max(spacerWidth, Dpi(-it.widthDip, m_dpi))
                                        : Dpi(it.widthDip, m_dpi);
        int h = it.kind == Kind::Child ? Dpi(kChildHeightDip, m_dpi) : Dpi(kButtonDip, m_dpi);
        int top = (height - h) / 2;
        it.rc = {x, top, x + w, top + h};
        x += w + (it.kind == Kind::Spacer ? 0 : gap);

        if (it.child) {
            // Centre a borderless edit inside the drawn frame.
            HDC hdc = GetDC(m_hwnd);
            HGDIOBJ old = SelectObject(hdc, m_textFont);
            TEXTMETRICW tm{};
            GetTextMetricsW(hdc, &tm);
            SelectObject(hdc, old);
            ReleaseDC(m_hwnd, hdc);
            int ch = tm.tmHeight + 2;
            int pad = Dpi(7, m_dpi);
            MoveWindow(it.child, it.rc.left + pad, top + (h - ch) / 2, w - 2 * pad, ch, TRUE);
        }
        if (m_tooltip && !it.tip.empty()) {
            TTTOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.hwnd = m_hwnd;
            ti.uId = i + 1;
            ti.rect = it.rc;
            SendMessageW(m_tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
        }
    }
}

int Toolbar::HitTest(POINT pt) const {
    for (size_t i = 0; i < m_items.size(); ++i) {
        const Item& it = m_items[i];
        if (it.clickable && it.enabled && PtInRect(&it.rc, pt)) return (int)i;
    }
    return -1;
}

void Toolbar::Paint(HDC hdc) {
    const Theme& th = CurrentTheme();
    RECT client;
    GetClientRect(m_hwnd, &client);

    // Double-buffer: the bar is small, so this is cheap and flicker-free.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, client.right), std::max(1L, client.bottom));
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(th.barBg);
    FillRect(mem, &client, bg);
    DeleteObject(bg);
    RECT line = m_borderTop ? RECT{0, 0, client.right, 1}
                            : RECT{0, client.bottom - 1, client.right, client.bottom};
    HBRUSH border = CreateSolidBrush(th.barBorder);
    FillRect(mem, &line, border);
    DeleteObject(border);

    SetBkMode(mem, TRANSPARENT);
    const int radius = Dpi(8, m_dpi);
    for (size_t i = 0; i < m_items.size(); ++i) {
        const Item& it = m_items[i];
        const bool hot = (int)i == m_hot, pressed = (int)i == m_pressed;
        switch (it.kind) {
            case Kind::Button:
            case Kind::TextButton:
            case Kind::Label: {
                if (it.clickable && it.enabled && (hot || pressed || it.checked)) {
                    COLORREF c = (pressed || it.checked) ? th.barPressed : th.barHover;
                    FillRound(mem, it.rc, c, c, radius);
                }
                COLORREF text = !it.enabled ? th.barTextDisabled
                                : it.checked ? th.accent
                                             : th.barText;
                SetTextColor(mem, text);
                SelectObject(mem, it.kind == Kind::Button ? m_iconFont : m_textFont);
                RECT r = it.rc;
                UINT align = (it.kind == Kind::Label && !it.clickable)
                                 ? (it.rightAlign ? DT_RIGHT : DT_LEFT)
                                 : DT_CENTER;
                DrawTextW(mem, it.text.c_str(), (int)it.text.size(), &r,
                          align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                break;
            }
            case Kind::Child:
                FillRound(mem, it.rc, th.editBg, th.editBorder, radius);
                break;
            case Kind::Separator: {
                int cx = (it.rc.left + it.rc.right) / 2;
                RECT sep = {cx, it.rc.top + Dpi(6, m_dpi), cx + 1, it.rc.bottom - Dpi(6, m_dpi)};
                HBRUSH b = CreateSolidBrush(th.barBorder);
                FillRect(mem, &sep, b);
                DeleteObject(b);
                break;
            }
            case Kind::Spacer:
                break;
        }
    }

    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK Toolbar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Toolbar* self;
    if (msg == WM_NCCREATE) {
        self = (Toolbar*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (Toolbar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Toolbar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
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
            int hit = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (hit != m_hot) {
                InvalidateItem(m_hot);
                m_hot = hit;
                InvalidateItem(m_hot);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            InvalidateItem(m_hot);
            m_hot = -1;
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            int hit = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (hit >= 0) {
                m_pressed = hit;
                SetCapture(m_hwnd);
                InvalidateItem(hit);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (m_pressed < 0) return 0;
            int pressed = m_pressed;
            m_pressed = -1;
            ReleaseCapture();
            InvalidateItem(pressed);
            if (HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == pressed)
                SendMessageW(m_target, WM_COMMAND, MAKEWPARAM(m_items[pressed].id, 0), 0);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (m_pressed >= 0) {
                InvalidateItem(m_pressed);
                m_pressed = -1;
            }
            return 0;
        case WM_COMMAND:  // notifications from hosted edits
            return SendMessageW(m_target, WM_COMMAND, wp, lp);
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const Theme& th = CurrentTheme();
            HDC dc = (HDC)wp;
            SetTextColor(dc, IsWindowEnabled((HWND)lp) ? th.editText : th.barTextDisabled);
            SetBkColor(dc, th.editBg);
            return (LRESULT)th.editBrush;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
