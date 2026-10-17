// GuideBar.cpp - the plain-words guide strip above the drawing (see GuideBar.h).
#include "GuideBar.h"

#include <algorithm>

#include <commctrl.h>
#include <windowsx.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClassName[] = L"FeatherCadGuideBar";
constexpr int kHeightDip = 38;
constexpr int kButtonHeightDip = 28;
constexpr int kSwatchDip = 20;

void Fill(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, brush), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(brush);
    DeleteObject(pen);
}

COLORREF Mix(COLORREF a, COLORREF b, int percentB) {
    auto m = [&](int x, int y) { return (x * (100 - percentB) + y * percentB) / 100; };
    return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}
}  // namespace

GuideBar::~GuideBar() {
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
}

bool GuideBar::Create(HWND parent, HWND commandTarget) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &GuideBar::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_target = commandTarget;
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    return true;
}

int GuideBar::Height() const { return Dpi(kHeightDip, m_dpi); }

void GuideBar::CreateFonts() {
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
    m_font = CreateMessageFont(m_dpi, 110);
    LOGFONTW lf{};
    GetObjectW(m_font, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    m_bold = CreateFontIndirectW(&lf);
}

int GuideBar::TextWidth(HFONT font, const std::wstring& s) const {
    HDC dc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(m_hwnd, dc);
    return sz.cx;
}

void GuideBar::SetGuide(const std::wstring& title, const std::wstring& text, std::vector<Button> buttons, bool active) {
    bool same = title == m_title && text == m_text && active == m_active && buttons.size() == m_buttons.size();
    for (size_t i = 0; same && i < buttons.size(); ++i)
        same = buttons[i].id == m_buttons[i].id && buttons[i].text == m_buttons[i].text && buttons[i].style == m_buttons[i].style;
    if (same) return;  // no flicker while the mouse moves
    m_title = title;
    m_text = text;
    m_buttons = std::move(buttons);
    m_active = active;
    Rebuild();
}

void GuideBar::SetStyle(std::vector<Swatch> swatches, int chosenId, int moreId, int weightId, const std::wstring& weightText) {
    bool same = chosenId == m_chosen && moreId == m_moreId && weightId == m_weightId && weightText == m_weightText &&
                swatches.size() == m_swatches.size();
    for (size_t i = 0; same && i < swatches.size(); ++i) same = swatches[i].id == m_swatches[i].id && swatches[i].color == m_swatches[i].color;
    if (same) return;
    m_swatches = std::move(swatches);
    m_chosen = chosenId;
    m_moreId = moreId;
    m_weightId = weightId;
    m_weightText = weightText;
    Rebuild();
}

void GuideBar::Rebuild() {
    if (!m_hwnd) return;
    // Tooltips of the old items go.
    if (m_tooltip) {
        const int n = (int)SendMessageW(m_tooltip, TTM_GETTOOLCOUNT, 0, 0);
        for (int i = n - 1; i >= 0; --i) {
            TTTOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            if (SendMessageW(m_tooltip, TTM_ENUMTOOLSW, (WPARAM)i, (LPARAM)&ti)) SendMessageW(m_tooltip, TTM_DELTOOLW, 0, (LPARAM)&ti);
        }
    }
    m_items.clear();
    m_hot = m_pressed = -1;
    RECT client;
    GetClientRect(m_hwnd, &client);
    const int h = client.bottom, gap = Dpi(6, m_dpi), pad = Dpi(12, m_dpi);
    const int bh = Dpi(kButtonHeightDip, m_dpi), top = (h - bh) / 2;
    const int sw = Dpi(kSwatchDip, m_dpi), stop = (h - sw) / 2;

    // Right side, from the right edge leftwards: thickness, More, swatches, "Colour".
    std::vector<Item> right;
    int x = client.right - gap;
    if (m_weightId) {
        Item it{Kind::Button};
        it.id = m_weightId;
        it.text = L"Thickness: " + m_weightText;
        it.style = 3;  // with a drop-down arrow
        it.tip = L"How thick lines are drawn and printed (for the selected objects, or for new ones)";
        const int w = TextWidth(m_font, it.text) + pad + Dpi(14, m_dpi);
        it.rc = {x - w, top, x, top + bh};
        x -= w + gap * 2;
        right.push_back(it);
    }
    if (m_moreId) {
        Item it{Kind::Button};
        it.id = m_moreId;
        it.text = L"More...";
        it.tip = L"Any colour";
        const int w = TextWidth(m_font, it.text) + pad;
        it.rc = {x - w, top, x, top + bh};
        x -= w + gap;
        right.push_back(it);
    }
    for (size_t i = m_swatches.size(); i-- > 0;) {
        Item it{Kind::Swatch};
        it.id = m_swatches[i].id;
        it.color = m_swatches[i].color;
        it.tip = m_swatches[i].tip;
        it.chosen = it.id == m_chosen;
        it.rc = {x - sw, stop, x, stop + sw};
        x -= sw + Dpi(3, m_dpi);
        right.push_back(it);
    }
    if (!m_swatches.empty()) {
        Item it{Kind::Label};
        it.text = L"Colour:";
        const int w = TextWidth(m_font, it.text) + gap;
        it.rc = {x - w, 0, x, h};
        x -= w + gap;
        right.push_back(it);
    }
    const int rightEdge = x - gap * 2;

    // Left side: title, instruction, buttons.
    x = gap * 2;
    if (!m_title.empty()) {
        Item it{Kind::Title};
        it.text = m_title;
        const int w = TextWidth(m_bold, it.text) + pad;
        it.rc = {x, top, x + w, top + bh};
        x += w + gap * 2;
        m_items.push_back(it);
    }
    int buttonsW = 0;
    std::vector<int> widths;
    for (const Button& b : m_buttons) {
        widths.push_back(TextWidth(b.style == 1 ? m_bold : m_font, b.text) + pad * 2);
        buttonsW += widths.back() + gap;
    }
    const int textW = std::max(0, std::min(TextWidth(m_font, m_text) + gap, rightEdge - x - buttonsW - gap * 2));
    if (!m_text.empty()) {
        Item it{Kind::Text};
        it.text = m_text;
        it.rc = {x, 0, x + textW, h};
        x += textW + gap * 2;
        m_items.push_back(it);
    }
    for (size_t i = 0; i < m_buttons.size(); ++i) {
        Item it{Kind::Button};
        it.id = m_buttons[i].id;
        it.text = m_buttons[i].text;
        it.tip = m_buttons[i].tip;
        it.style = m_buttons[i].style;
        it.rc = {x, top, x + widths[i], top + bh};
        x += widths[i] + gap;
        if (it.rc.right > rightEdge) break;  // no room: the rest is in the right-click menu
        m_items.push_back(it);
    }
    m_items.insert(m_items.end(), right.begin(), right.end());

    if (m_tooltip)
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (m_items[i].tip.empty()) continue;
            TTTOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = m_hwnd;
            ti.uId = i + 1;
            ti.rect = m_items[i].rc;
            ti.lpszText = const_cast<wchar_t*>(m_items[i].tip.c_str());
            SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

RECT GuideBar::ItemScreenRect(int id) const {
    RECT rc{};
    for (const Item& it : m_items)
        if (it.id == id && (it.kind == Kind::Button || it.kind == Kind::Swatch)) {
            rc = it.rc;
            MapWindowPoints(m_hwnd, nullptr, (POINT*)&rc, 2);
        }
    return rc;
}

int GuideBar::HitTest(POINT pt) const {
    for (size_t i = 0; i < m_items.size(); ++i)
        if ((m_items[i].kind == Kind::Button || m_items[i].kind == Kind::Swatch) && PtInRect(&m_items[i].rc, pt)) return (int)i;
    return -1;
}

void GuideBar::Paint(HDC hdc) {
    const Theme& th = CurrentTheme();
    RECT client;
    GetClientRect(m_hwnd, &client);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, client.right), std::max(1L, client.bottom));
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    // While a command runs the strip is tinted, so the eye goes to it.
    const COLORREF bg = m_active ? Mix(th.barBg, th.accent, th.dark ? 18 : 10) : th.barBg;
    HBRUSH b = CreateSolidBrush(bg);
    FillRect(mem, &client, b);
    DeleteObject(b);
    RECT line = {0, client.bottom - 1, client.right, client.bottom};
    b = CreateSolidBrush(th.barBorder);
    FillRect(mem, &line, b);
    DeleteObject(b);
    SetBkMode(mem, TRANSPARENT);
    const int radius = Dpi(8, m_dpi);

    for (size_t i = 0; i < m_items.size(); ++i) {
        const Item& it = m_items[i];
        const bool hot = (int)i == m_hot, pressed = (int)i == m_pressed;
        RECT r = it.rc;
        switch (it.kind) {
            case Kind::Title:
                Fill(mem, r, th.accent, th.accent, radius);
                SetTextColor(mem, RGB(255, 255, 255));
                SelectObject(mem, m_bold);
                DrawTextW(mem, it.text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                break;
            case Kind::Text:
            case Kind::Label:
                SetTextColor(mem, th.barText);
                SelectObject(mem, m_font);
                DrawTextW(mem, it.text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                break;
            case Kind::Button: {
                COLORREF fill = it.style == 1 ? th.accent : Mix(bg, th.barText, 6);
                if (hot) fill = it.style == 1 ? Mix(th.accent, RGB(0, 0, 0), 15) : th.barHover;
                if (pressed) fill = it.style == 1 ? Mix(th.accent, RGB(0, 0, 0), 30) : th.barPressed;
                Fill(mem, r, fill, it.style == 1 ? fill : th.barBorder, radius);
                SetTextColor(mem, it.style == 1 ? RGB(255, 255, 255) : it.style == 2 ? RGB(200, 40, 40) : th.barText);
                if (th.dark && it.style == 2) SetTextColor(mem, RGB(255, 120, 120));
                SelectObject(mem, it.style == 1 ? m_bold : m_font);
                if (it.style == 3) {  // a drop-down: the text, then a small arrow
                    const int aw = Dpi(14, m_dpi);
                    RECT t = r;
                    t.right -= aw;
                    DrawTextW(mem, it.text.c_str(), -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    const int cx = r.right - aw + Dpi(3, m_dpi), cy = (r.top + r.bottom) / 2, s = Dpi(4, m_dpi);
                    const POINT tri[3] = {{cx - s, cy - s / 2}, {cx + s, cy - s / 2}, {cx, cy + s / 2 + 1}};
                    HBRUSH ab = CreateSolidBrush(th.barText);
                    HPEN ap = CreatePen(PS_SOLID, 1, th.barText);
                    HGDIOBJ obr = SelectObject(mem, ab), opn = SelectObject(mem, ap);
                    Polygon(mem, tri, 3);
                    SelectObject(mem, obr);
                    SelectObject(mem, opn);
                    DeleteObject(ab);
                    DeleteObject(ap);
                } else {
                    DrawTextW(mem, it.text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                }
                break;
            }
            case Kind::Swatch: {
                if (it.chosen || hot) {
                    RECT o = r;
                    InflateRect(&o, Dpi(3, m_dpi), Dpi(3, m_dpi));
                    Fill(mem, o, bg, it.chosen ? th.accent : th.barBorder, Dpi(6, m_dpi));
                    if (it.chosen) {
                        InflateRect(&o, -1, -1);
                        Fill(mem, o, bg, th.accent, Dpi(6, m_dpi));
                    }
                }
                Fill(mem, r, it.color, Mix(it.color, th.barText, 35), Dpi(4, m_dpi));
                break;
            }
        }
    }
    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

void GuideBar::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    Rebuild();
}

void GuideBar::OnThemeChanged() { InvalidateRect(m_hwnd, nullptr, TRUE); }

LRESULT CALLBACK GuideBar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    GuideBar* self;
    if (msg == WM_NCCREATE) {
        self = (GuideBar*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (GuideBar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT GuideBar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    auto redraw = [this](int i) {
        if (i >= 0 && i < (int)m_items.size()) {
            RECT r = m_items[(size_t)i].rc;
            InflateRect(&r, Dpi(4, m_dpi), Dpi(4, m_dpi));
            InvalidateRect(m_hwnd, &r, FALSE);
        }
    };
    switch (msg) {
        case WM_SIZE:
            Rebuild();
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
                redraw(m_hot);
                m_hot = hit;
                redraw(m_hot);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m_tracking = false;
            redraw(m_hot);
            m_hot = -1;
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const int hit = HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (hit >= 0) {
                m_pressed = hit;
                SetCapture(m_hwnd);
                redraw(hit);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (m_pressed < 0) return 0;
            const int pressed = m_pressed;
            m_pressed = -1;
            ReleaseCapture();
            redraw(pressed);
            if (HitTest({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == pressed)
                PostMessageW(m_target, WM_COMMAND, MAKEWPARAM(m_items[(size_t)pressed].id, 0), 0);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (m_pressed >= 0) {
                redraw(m_pressed);
                m_pressed = -1;
            }
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
