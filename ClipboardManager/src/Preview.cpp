// Preview.cpp - heading, text box and picture.
#include "Preview.h"

#include <algorithm>

#include "Images.h"
#include "ItemList.h"
#include "Theme.h"
#include "Util.h"

using namespace Gdiplus;

namespace {
const wchar_t kClass[] = L"CmPreview";
constexpr size_t kShowChars = 256000;  // the text box shows the start of very long texts

std::wstring WindowsLineEnds(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + s.size() / 32);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\n' && (i == 0 || s[i - 1] != L'\r')) out += L'\r';
        if (s[i] == L'\r' && (i + 1 == s.size() || s[i + 1] != L'\n')) {
            out += L"\r\n";
            continue;
        }
        if (s[i] == 0) continue;
        out += s[i];
    }
    return out;
}

size_t CountLines(const std::wstring& s) {
    if (s.empty()) return 0;
    size_t n = 1;
    for (wchar_t c : s)
        if (c == L'\n') ++n;
    if (s.back() == L'\n') --n;
    return n;
}
}  // namespace

Preview::~Preview() {
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
    if (m_textFont) DeleteObject(m_textFont);
}

bool Preview::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &Preview::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 100, 100, parent, nullptr,
                    GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_edit = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL |
                                 ES_AUTOVSCROLL,
                             0, 0, 10, 10, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(m_edit, EM_SETLIMITTEXT, 0, 0);
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    OnThemeChanged();
    return true;
}

void Preview::CreateFonts() {
    if (m_font) DeleteObject(m_font);
    if (m_bold) DeleteObject(m_bold);
    if (m_textFont) DeleteObject(m_textFont);
    m_font = CreateMessageFont(m_dpi);
    m_textFont = CreateMessageFont(m_dpi, 105);
    LOGFONTW lf{};
    GetObjectW(m_textFont, sizeof(lf), &lf);
    lf.lfWeight = FW_SEMIBOLD;
    m_bold = CreateFontIndirectW(&lf);
    SendMessageW(m_edit, WM_SETFONT, (WPARAM)m_textFont, TRUE);
    const int margin = Dpi(6, m_dpi);
    SendMessageW(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
}

int Preview::HeaderHeight() const { return Dpi(62, m_dpi); }

void Preview::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    Layout();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void Preview::OnThemeChanged() {
    ApplyScrollbarTheme(m_edit);
    InvalidateRect(m_hwnd, nullptr, TRUE);
    InvalidateRect(m_edit, nullptr, TRUE);
}

void Preview::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int pad = Dpi(12, m_dpi);
    const int top = HeaderHeight();
    MoveWindow(m_edit, pad, top, std::max(0, (int)rc.right - 2 * pad), std::max(0, (int)rc.bottom - top - pad), TRUE);
}

void Preview::Show(const Store& store, const ClipItem* item, int selectedCount, const std::wstring& emptyText) {
    if (!item || selectedCount != 1) {
        m_id = 0;
        m_image.reset();
        m_scaled.reset();
        ShowWindow(m_edit, SW_HIDE);
        SetWindowTextW(m_edit, L"");
        m_heading.clear();
        m_details.clear();
        m_message = selectedCount > 1 ? std::to_wstring(selectedCount) +
                                            L" items selected.\n\nCopy puts their text on the clipboard together, "
                                            L"one per line.\nPin and Delete work on all of them."
                                      : emptyText;
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return;
    }
    // Pinning only changes the heading: keep the scroll position.
    const bool same = item->id == m_id;
    m_pinned = item->pinned;
    m_message.clear();
    m_details = L"Copied " + FormatWhen(item->id);
    if (!item->source.empty()) m_details += L" from " + item->source;
    m_details += L"  \x00B7  " + item->file;
    if (item->pinned) m_details = L"Pinned  \x00B7  " + m_details;

    if (item->kind == ClipKind::Image) {
        m_heading = L"Picture  \x00B7  " + std::to_wstring(item->width) + L" \x00D7 " + std::to_wstring(item->height) +
                    L" pixels  \x00B7  " + FormatSize(item->bytes);
        ShowWindow(m_edit, SW_HIDE);
        if (!same || !m_image) {
            std::string data;
            m_image.reset();
            m_scaled.reset();
            if (ReadFileBytes(store.PathOf(*item), data)) m_image = DecodeImage(data);
            if (!m_image) m_message = L"The picture file could not be read.";
        }
    } else {
        m_image.reset();
        m_scaled.reset();
        if (!same) {
            std::wstring text;
            if (!store.FullText(*item, text)) text = item->text;
            const size_t total = text.size();
            const size_t lines = CountLines(text);
            if (item->kind == ClipKind::Files)
                m_heading = lines == 1 ? std::wstring(L"1 copied file") : FormatCount(lines) + L" copied files";
            else
                m_heading = L"Text  \x00B7  " + FormatCount(total) + (total == 1 ? L" character" : L" characters") +
                            L"  \x00B7  " + FormatCount(lines) + (lines == 1 ? L" line" : L" lines");
            if (text.size() > kShowChars) {
                text.resize(kShowChars);
                m_heading += L"  \x00B7  the start is shown";
                m_details += L"  \x00B7  right-click \x203A Open shows all of it";
            }
            SetWindowTextW(m_edit, WindowsLineEnds(text).c_str());
        }
        ShowWindow(m_edit, SW_SHOW);
    }
    m_id = item->id;
    Layout();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

LRESULT CALLBACK Preview::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Preview* self;
    if (msg == WM_NCCREATE) {
        self = (Preview*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (Preview*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Preview::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(m_hwnd, &ps);
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            // Draw off screen, then copy: no flicker while resizing.
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, std::max<int>(1, rc.right), std::max<int>(1, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            Paint(mem);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            const Theme& t = CurrentTheme();
            HDC dc = (HDC)wp;
            SetTextColor(dc, t.editText);
            SetBkColor(dc, t.editBg);
            return (LRESULT)t.editBrush;
        }
        case WM_COMMAND:
        case WM_NOTIFY: return SendMessageW(GetParent(m_hwnd), msg, wp, lp);
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void Preview::Paint(HDC dc) {
    const Theme& t = CurrentTheme();
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(t.editBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    const int pad = Dpi(12, m_dpi);

    if (!m_message.empty() && m_heading.empty()) {
        RECT r{pad * 2, pad * 2, rc.right - pad * 2, rc.bottom - pad};
        SelectObject(dc, m_font);
        SetTextColor(dc, t.listDim);
        RECT measure = r;
        DrawTextW(dc, m_message.c_str(), -1, &measure, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        const int h = measure.bottom - measure.top;
        r.top = std::max<int>(r.top, (rc.bottom - h) / 2 - pad * 2);
        DrawTextW(dc, m_message.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        return;
    }

    // Heading.
    RECT head{pad, Dpi(10, m_dpi), rc.right - pad, Dpi(32, m_dpi)};
    SelectObject(dc, m_bold);
    SetTextColor(dc, t.listText);
    DrawTextW(dc, m_heading.c_str(), -1, &head, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT det{pad, Dpi(32, m_dpi), rc.right - pad, Dpi(52, m_dpi)};
    SelectObject(dc, m_font);
    SetTextColor(dc, t.listDim);
    DrawTextW(dc, m_details.c_str(), -1, &det, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT line{pad, HeaderHeight() - Dpi(4, m_dpi), rc.right - pad, HeaderHeight() - Dpi(4, m_dpi) + 1};
    HBRUSH lb = CreateSolidBrush(t.editBorder);
    FillRect(dc, &line, lb);
    DeleteObject(lb);

    RECT area{pad, HeaderHeight() + Dpi(4, m_dpi), rc.right - pad, rc.bottom - pad};
    if (!m_message.empty()) {
        SetTextColor(dc, t.listDim);
        DrawTextW(dc, m_message.c_str(), -1, &area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }
    if (!m_image || area.right <= area.left || area.bottom <= area.top) return;

    // The picture, fitted (never enlarged), on a checkerboard for transparency.
    const double w = m_image->GetWidth(), h = m_image->GetHeight();
    const double aw = area.right - area.left, ah = area.bottom - area.top - Dpi(22, m_dpi);
    if (ah <= 0) return;
    const double scale = std::min({1.0, aw / w, ah / h});
    const int dw = std::max(1, (int)(w * scale + 0.5)), dh = std::max(1, (int)(h * scale + 0.5));
    const int x = area.left + (int)(aw - dw) / 2, y = area.top + (int)(ah - dh) / 2;
    Graphics g(dc);
    const int cell = Dpi(8, m_dpi);
    SolidBrush light(Color(255, 255, 255, 255)), darkCell(Color(255, 225, 225, 225));
    g.FillRectangle(&light, x, y, dw, dh);
    for (int cy = 0; cy < dh; cy += cell)
        for (int cx = ((cy / cell) % 2) * cell; cx < dw; cx += 2 * cell)
            g.FillRectangle(&darkCell, x + cx, y + cy, std::min(cell, dw - cx), std::min(cell, dh - cy));
    // Scaling a large picture well is slow: keep the scaled copy while the
    // pane keeps its size.
    Bitmap* shown = m_image.get();
    if (scale < 1.0) {
        if (!m_scaled || (int)m_scaled->GetWidth() != dw || (int)m_scaled->GetHeight() != dh) {
            m_scaled = std::make_unique<Bitmap>(dw, dh, PixelFormat32bppPARGB);
            Graphics sg(m_scaled.get());
            sg.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            sg.SetPixelOffsetMode(PixelOffsetModeHalf);
            sg.DrawImage(m_image.get(), Rect(0, 0, dw, dh), 0, 0, (INT)w, (INT)h, UnitPixel);
        }
        shown = m_scaled.get();
    }
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.DrawImage(shown, Rect(x, y, dw, dh), 0, 0, dw, dh, UnitPixel);

    const std::wstring zoom = scale < 1.0 ? L"Shown at " + std::to_wstring((int)(scale * 100 + 0.5)) + L"%" : L"Actual size";
    RECT zr{area.left, area.bottom - Dpi(20, m_dpi), area.right, area.bottom};
    SetTextColor(dc, t.listDim);
    DrawTextW(dc, zoom.c_str(), -1, &zr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
