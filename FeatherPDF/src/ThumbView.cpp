// ThumbView.cpp - lazily rendered page thumbnails (see ThumbView.h).
#include "ThumbView.h"

#include <algorithm>
#include <memory>

#include <windowsx.h>

#include "RenderWorker.h"
#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClassName[] = L"FeatherThumbView";
constexpr size_t kCacheBudget = 16u << 20;  // bytes of thumbnail pixels kept
constexpr int kPrefetch = 6;                // thumbnails rendered below the visible ones

void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
}
}  // namespace

ThumbView::~ThumbView() {
    ClearCache();
    if (m_font) DeleteObject(m_font);
}

bool ThumbView::Create(HWND parent, HWND target, RenderWorker* worker) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &ThumbView::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_target = target;
    m_worker = worker;
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VSCROLL, 0, 0, 0, 0, parent, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    m_font = CreateMessageFont(m_dpi, 90);
    ApplyScrollbarTheme(m_hwnd);
    return true;
}

void ThumbView::ClearCache() {
    for (auto& e : m_cache) e.second.pixels.Free();
    m_cache.clear();
    m_bytes = 0;
}

void ThumbView::SetDocument(uint32_t docId, const std::vector<SizeF>& sizes, int rotation,
                            int colors) {
    const bool sameDoc = docId == m_docId && sizes.size() == m_sizes.size();
    if (!sameDoc || rotation != m_rotation || colors != m_colors) ClearCache();
    if (!sameDoc) {
        m_scroll = 0;
        m_current = -1;
    }
    m_docId = docId;
    m_sizes = sizes;
    m_rotation = rotation;
    m_colors = colors;
    Layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::Clear() {
    m_docId = 0;
    m_sizes.clear();
    m_items.clear();
    m_current = -1;
    ClearCache();
    Layout();
    if (m_worker) m_worker->SetWantedThumbs(0, {});
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::TrimMemory() {
    ClearCache();
    if (m_worker) m_worker->SetWantedThumbs(0, {});
}

void ThumbView::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    if (m_font) DeleteObject(m_font);
    m_font = CreateMessageFont(m_dpi, 90);
    Layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int pad = Dpi(12, m_dpi);
    const int thumbW = std::max(Dpi(40, m_dpi), (int)rc.right - 2 * pad);
    HDC hdc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(hdc, m_font);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);
    m_labelH = tm.tmHeight + Dpi(6, m_dpi);

    m_items.resize(m_sizes.size());
    int y = pad;
    for (size_t i = 0; i < m_sizes.size(); ++i) {
        const float w = (m_rotation & 1) ? m_sizes[i].h : m_sizes[i].w;
        const float h = (m_rotation & 1) ? m_sizes[i].w : m_sizes[i].h;
        Item& it = m_items[i];
        it.top = y;
        it.w = thumbW;
        it.h = std::max(1, std::min(thumbW * 3, (int)(thumbW * h / std::max(1.0f, w))));
        y += it.h + m_labelH + Dpi(6, m_dpi);
    }
    m_totalH = y;
    m_scroll = std::max(0, std::min(m_scroll, m_totalH - (int)rc.bottom));

    SCROLLINFO si{sizeof(si)};
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMax = std::max(0, m_totalH - 1);
    si.nPage = (UINT)rc.bottom;
    si.nPos = m_scroll;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

void ThumbView::ScrollTo(int y) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    y = std::max(0, std::min(y, m_totalH - (int)rc.bottom));
    if (y == m_scroll) return;
    m_scroll = y;
    SetScrollPos(m_hwnd, SB_VERT, m_scroll, TRUE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

int ThumbView::ItemAt(int y) const {
    // Last item whose top is <= y (binary search).
    auto it = std::upper_bound(m_items.begin(), m_items.end(), y,
                               [](int v, const Item& item) { return v < item.top; });
    return it == m_items.begin() ? 0 : (int)(it - m_items.begin()) - 1;
}

void ThumbView::SetCurrentPage(int page) {
    if (page == m_current || page < 0 || page >= (int)m_items.size()) return;
    m_current = page;
    // Keep the current page's thumbnail in view.
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const Item& it = m_items[(size_t)page];
    if (it.top < m_scroll || it.top + it.h + m_labelH > m_scroll + rc.bottom)
        ScrollTo(it.top - Dpi(12, m_dpi));
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::EvictIfNeeded() {
    while (m_bytes > kCacheBudget && !m_cache.empty()) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->second.lastUse < oldest->second.lastUse) oldest = it;
        m_bytes -= oldest->second.pixels.Bytes();
        oldest->second.pixels.Free();
        m_cache.erase(oldest);
    }
}

void ThumbView::OnThumbReady(TileResult* result) {
    std::unique_ptr<TileResult> res(result);
    const TileRequest& r = res->req;
    if (res->docId != m_docId || r.page < 0 || r.page >= (int)m_items.size()) return;
    if (!res->pixels.bits || r.rotate != m_rotation || r.colorMode != m_colors ||
        r.w != m_items[(size_t)r.page].w)
        return;  // stale (the panel was resized or the view rotated since)
    Entry& e = m_cache[r.page];
    m_bytes -= e.pixels.Bytes();
    e.pixels.Free();
    e.pixels = res->pixels;
    res->pixels = PixelBuffer{};
    e.lastUse = ++m_clock;
    m_bytes += e.pixels.Bytes();
    EvictIfNeeded();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::Paint(HDC hdc) {
    const Theme& th = CurrentTheme();
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    Fill(mem, rc, th.barBg);

    std::vector<TileRequest> wanted;
    if (!m_items.empty()) {
        SelectObject(mem, m_font);
        SetBkMode(mem, TRANSPARENT);
        SetStretchBltMode(mem, HALFTONE);
        SetBrushOrgEx(mem, 0, 0, nullptr);
        const COLORREF paper = m_colors == kColorsDark  ? RGB(30, 30, 30)
                               : m_colors == kColorsDim ? RGB(200, 200, 200)
                                                        : RGB(255, 255, 255);
        const int first = ItemAt(m_scroll);
        int last = first;
        for (int i = first; i < (int)m_items.size() && m_items[(size_t)i].top < m_scroll + rc.bottom; ++i) {
            last = i;
            const Item& it = m_items[(size_t)i];
            const int x = (rc.right - it.w) / 2, y = it.top - m_scroll;
            RECT box = {x, y, x + it.w, y + it.h};
            RECT frame = box;
            const int fw = i == m_current ? Dpi(3, m_dpi) : 1;
            InflateRect(&frame, fw, fw);
            Fill(mem, frame, i == m_current ? th.accent : th.pageBorder);
            Fill(mem, box, paper);
            auto found = m_cache.find(i);
            if (found != m_cache.end()) {
                found->second.lastUse = ++m_clock;
                const PixelBuffer& px = found->second.pixels;
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
                bmi.bmiHeader.biWidth = px.width;
                bmi.bmiHeader.biHeight = -px.height;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                StretchDIBits(mem, x, y, it.w, it.h, 0, 0, px.width, px.height, px.bits, &bmi,
                              DIB_RGB_COLORS, SRCCOPY);
            } else {
                wanted.push_back({i, it.w, 0, 0, 0, 0, it.w, it.h, it.w, it.h, m_rotation, m_colors});
            }
            RECT label = {0, y + it.h + Dpi(3, m_dpi), rc.right, y + it.h + m_labelH};
            SetTextColor(mem, i == m_current ? th.accent : th.barText);
            const std::wstring num = std::to_wstring(i + 1);
            DrawTextW(mem, num.c_str(), -1, &label, DT_CENTER | DT_TOP | DT_SINGLELINE);
        }
        // A few below the visible ones, so scrolling down finds them ready.
        for (int i = last + 1; i < (int)m_items.size() && i <= last + kPrefetch; ++i) {
            const Item& it = m_items[(size_t)i];
            if (!m_cache.count(i))
                wanted.push_back({i, it.w, 0, 0, 0, 0, it.w, it.h, it.w, it.h, m_rotation, m_colors});
        }
    }
    if (m_worker && m_docId) m_worker->SetWantedThumbs(m_docId, std::move(wanted));

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK ThumbView::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ThumbView* self;
    if (msg == WM_NCCREATE) {
        self = (ThumbView*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (ThumbView*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ThumbView::Handle(UINT msg, WPARAM wp, LPARAM lp) {
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
        case WM_MOUSEWHEEL:
            ScrollTo(m_scroll - GET_WHEEL_DELTA_WPARAM(wp) * Dpi(120, m_dpi) / WHEEL_DELTA);
            return 0;
        case WM_VSCROLL: {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            SCROLLINFO si{sizeof(si)};
            si.fMask = SIF_ALL;
            GetScrollInfo(m_hwnd, SB_VERT, &si);
            int y = m_scroll;
            switch (LOWORD(wp)) {
                case SB_LINEUP: y -= Dpi(40, m_dpi); break;
                case SB_LINEDOWN: y += Dpi(40, m_dpi); break;
                case SB_PAGEUP: y -= rc.bottom; break;
                case SB_PAGEDOWN: y += rc.bottom; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: y = si.nTrackPos; break;
                case SB_TOP: y = 0; break;
                case SB_BOTTOM: y = m_totalH; break;
            }
            ScrollTo(y);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (m_items.empty()) return 0;
            const int y = GET_Y_LPARAM(lp) + m_scroll;
            const int i = ItemAt(y);
            const Item& it = m_items[(size_t)i];
            if (y <= it.top + it.h + m_labelH)
                PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarPageClicked, i);
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
