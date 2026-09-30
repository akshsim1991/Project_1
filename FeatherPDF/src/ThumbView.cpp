// ThumbView.cpp - lazily rendered page thumbnails (see ThumbView.h).
#include "ThumbView.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#include <windowsx.h>

#include "RenderWorker.h"
#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClassName[] = L"FeatherThumbView";
constexpr size_t kCacheBudget = 16u << 20;  // bytes of thumbnail pixels kept
constexpr int kPrefetch = 6;                // thumbnails rendered below the visible ones
constexpr UINT_PTR kDragScrollTimer = 1;    // scrolls while dragging near an edge

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
    if (!sameDoc) {
        m_selected.assign(sizes.size(), 0);
        m_anchor = -1;
    }
    m_docId = docId;
    m_sizes = sizes;
    m_rotation = rotation;
    m_colors = colors;
    Layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::Reload(uint32_t docId, const std::vector<SizeF>& sizes) {
    ClearCache();  // page contents changed
    m_docId = docId;
    m_sizes = sizes;
    m_selected.assign(sizes.size(), 0);
    m_anchor = -1;
    if (m_current >= (int)sizes.size()) m_current = (int)sizes.size() - 1;
    Layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

std::vector<int> ThumbView::Selection() const {
    std::vector<int> out;
    for (size_t i = 0; i < m_selected.size(); ++i)
        if (m_selected[i]) out.push_back((int)i);
    return out;
}

void ThumbView::SetSelection(const std::vector<int>& pages) {
    m_selected.assign(m_sizes.size(), 0);
    for (int p : pages)
        if (p >= 0 && p < (int)m_selected.size()) m_selected[(size_t)p] = 1;
    m_anchor = pages.empty() ? -1 : pages.front();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::Clear() {
    m_docId = 0;
    m_selected.clear();
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

// ---------------------------------------------------------------------------
// Selection and drag-to-reorder
// ---------------------------------------------------------------------------
void ThumbView::Select(int item, bool ctrl, bool shift) {
    if (m_selected.size() != m_items.size()) m_selected.assign(m_items.size(), 0);
    if (shift && m_anchor >= 0) {
        if (!ctrl) std::fill(m_selected.begin(), m_selected.end(), 0);
        for (int i = std::min(m_anchor, item); i <= std::max(m_anchor, item); ++i)
            m_selected[(size_t)i] = 1;
    } else if (ctrl) {
        m_selected[(size_t)item] ^= 1;
        m_anchor = item;
    } else {
        std::fill(m_selected.begin(), m_selected.end(), 0);
        m_selected[(size_t)item] = 1;
        m_anchor = item;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void ThumbView::OnButtonDown(POINT pt, bool right) {
    if (m_items.empty()) return;
    const int y = pt.y + m_scroll;
    const int i = ItemAt(y);
    const Item& it = m_items[(size_t)i];
    if (y > it.top + it.h + m_labelH) return;  // below the last page
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    const bool selected = i < (int)m_selected.size() && m_selected[(size_t)i];
    if (right) {
        // Right-click keeps a selection it lands on (for the page menu).
        if (!selected) Select(i, false, false);
        return;
    }
    // A plain click on a selected page may start dragging the whole
    // selection, so it only narrows the selection on release.
    m_selectOnUp = selected && !ctrl && !shift;
    if (!m_selectOnUp) Select(i, ctrl, shift);
    if (!ctrl && !shift) PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarPageClicked, i);
    if (ctrl && !selected) return;  // Ctrl+click only toggles
    m_pressed = true;
    m_dragging = false;
    m_pressPt = pt;
    m_pressItem = i;
    SetCapture(m_hwnd);
}

int ThumbView::GapAt(int y) const {
    if (m_items.empty()) return -1;
    const int i = ItemAt(y);
    const Item& it = m_items[(size_t)i];
    return y < it.top + (it.h + m_labelH) / 2 ? i : i + 1;
}

bool ThumbView::DropIsNoOp(int gap) const {
    // Dropping a contiguous block just before, inside or after itself.
    const std::vector<int> sel = Selection();
    if (sel.empty()) return true;
    if (sel.back() - sel.front() + 1 != (int)sel.size()) return false;
    return gap >= sel.front() && gap <= sel.back() + 1;
}

void ThumbView::OnDragMove(POINT pt) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    if (!m_dragging) {
        const int slop = GetSystemMetrics(SM_CYDRAG);
        if (std::abs(pt.y - m_pressPt.y) < slop && std::abs(pt.x - m_pressPt.x) < slop) return;
        m_dragging = true;
        m_selectOnUp = false;
        SetTimer(m_hwnd, kDragScrollTimer, 50, nullptr);
    }
    // Scroll when dragging near the top or bottom edge.
    const int edge = Dpi(32, m_dpi);
    if (pt.y < edge)
        ScrollTo(m_scroll - Dpi(12, m_dpi));
    else if (pt.y > rc.bottom - edge)
        ScrollTo(m_scroll + Dpi(12, m_dpi));
    const int gap = GapAt(std::max(0, (int)pt.y) + m_scroll);
    const int shown = DropIsNoOp(gap) ? -1 : gap;
    if (shown != m_gap) {
        m_gap = shown;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    SetCursor(LoadCursorW(nullptr, shown >= 0 ? IDC_HAND : IDC_NO));
}

void ThumbView::OnButtonUp() {
    const bool dragged = m_dragging;
    const int gap = m_gap;
    if (m_selectOnUp && !dragged) Select(m_pressItem, false, false);
    ReleaseCapture();  // resets the drag state (WM_CAPTURECHANGED)
    if (dragged && gap >= 0)
        PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarPagesMoved, gap);
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
            if (i < (int)m_selected.size() && m_selected[(size_t)i]) {
                const int pad = Dpi(6, m_dpi);
                RECT sel = {x - pad, y - pad, x + it.w + pad, y + it.h + m_labelH};
                Fill(mem, sel, th.barPressed);
            }
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

    // Drop position while dragging pages: a bar between two thumbnails.
    if (m_dragging && m_gap >= 0 && !m_items.empty()) {
        const int y = m_gap < (int)m_items.size()
                          ? m_items[(size_t)m_gap].top - Dpi(8, m_dpi)
                          : m_items.back().top + m_items.back().h + m_labelH;
        const int bar = Dpi(3, m_dpi);
        RECT line = {Dpi(6, m_dpi), y - m_scroll - bar / 2, rc.right - Dpi(6, m_dpi),
                     y - m_scroll - bar / 2 + bar};
        Fill(mem, line, th.accent);
    }

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
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            SetFocus(m_hwnd);
            OnButtonDown({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, msg == WM_RBUTTONDOWN);
            return 0;
        case WM_MOUSEMOVE:
            if (m_pressed) OnDragMove({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_LBUTTONUP:
            if (m_pressed) OnButtonUp();
            return 0;
        case WM_RBUTTONUP: {
            if (m_items.empty()) return 0;
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(m_hwnd, &pt);
            PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarPagesMenu, MAKELPARAM(pt.x, pt.y));
            return 0;
        }
        case WM_CONTEXTMENU:  // Shift+F10 / menu key
            if ((HWND)wp == m_hwnd && !m_items.empty() && GET_X_LPARAM(lp) == -1) {
                RECT rc;
                GetWindowRect(m_hwnd, &rc);
                PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarPagesMenu,
                             MAKELPARAM(rc.left + Dpi(24, m_dpi), rc.top + Dpi(24, m_dpi)));
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (m_pressed) {
                m_pressed = m_dragging = false;
                m_gap = -1;
                KillTimer(m_hwnd, kDragScrollTimer);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_TIMER:
            if (wp == kDragScrollTimer && m_dragging) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);
                OnDragMove(pt);
            }
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_KEYDOWN: {
            if (m_items.empty()) break;
            const bool ctrl = GetKeyState(VK_CONTROL) < 0;
            if (wp == VK_DELETE) {
                PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarDeletePages, 0);
                return 0;
            }
            if (ctrl && wp == 'A') {
                std::vector<int> all(m_items.size());
                for (size_t i = 0; i < all.size(); ++i) all[i] = (int)i;
                SetSelection(all);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                SetSelection({});
                return 0;
            }
            if (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END) {
                const int last = (int)m_items.size() - 1;
                int cur = m_anchor >= 0 ? m_anchor : std::max(0, m_current);
                if (wp == VK_UP) cur = std::max(0, cur - 1);
                if (wp == VK_DOWN) cur = std::min(last, cur + 1);
                if (wp == VK_HOME) cur = 0;
                if (wp == VK_END) cur = last;
                Select(cur, false, GetKeyState(VK_SHIFT) < 0);
                PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarPageClicked, cur);
                return 0;
            }
            break;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
