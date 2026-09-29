// PdfView.cpp - page canvas (see PdfView.h for the architecture).
#include "PdfView.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <cwctype>
#include <memory>

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include "RenderWorker.h"
#include "Theme.h"
#include "Util.h"

const double kZoomPresets[] = {0.10, 0.25, 0.33, 0.50, 0.67, 0.75, 0.90, 1.00, 1.10, 1.25, 1.50,
                               1.75, 2.00, 2.50, 3.00, 4.00, 5.00, 6.00, 8.00, 10.0, 12.0, 16.0};
const int kZoomPresetCount = (int)(sizeof(kZoomPresets) / sizeof(kZoomPresets[0]));

namespace {
const wchar_t kClassName[] = L"FeatherPageView";
constexpr int kTileH = 512;            // tile height in pixels
constexpr int kTileMaxRowW = 2048;     // pages up to this width use full-width tiles
constexpr int kTileColW = 1024;        // column width for wider pages
constexpr UINT_PTR kSettleTimer = 1;   // re-render delay after wheel zoom
constexpr UINT kSettleMs = 150;
constexpr UINT_PTR kAutoScrollTimer = 2;  // scrolls while drag-selecting past an edge
constexpr size_t kMaxTextLayers = 32;     // pages whose character boxes are kept
constexpr float kTextSlackPt = 12.0f;     // how far from a glyph still counts as "on text"
constexpr COLORREF kSelectionColor = RGB(150, 200, 255);

bool IsWordChar(uint32_t cp) {
    return cp == '_' || (cp < 0x10000 && iswalnum((wint_t)cp)) || cp >= 0x10000;
}
constexpr DWORD kRopDestAndPattern = 0x00A000C9;  // "multiply" highlight

template <typename T>
T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

void FillSolid(HDC dc, const RECT& r, COLORREF c) {
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
}

void DrawPixels(HDC dc, int x, int y, int w, int h, const PixelBuffer& px) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = px.width;
    bmi.bmiHeader.biHeight = -px.height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, x, y, w, h, 0, 0, px.width, px.height, px.bits, &bmi, DIB_RGB_COLORS,
                  SRCCOPY);
}
}  // namespace

// ===========================================================================
// Creation & window procedure
// ===========================================================================
bool PdfView::Create(HWND parent, RenderWorker* worker) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &PdfView::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_worker = worker;
    m_hwnd = CreateWindowExW(0, kClassName, L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 0, 0, 0,
                             0, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);
    m_messageFont = CreateMessageFont(m_dpi, 110);
    ApplyScrollbarTheme(m_hwnd);
    UpdateScale();
    UpdateScrollBars();
    return true;
}

LRESULT CALLBACK PdfView::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PdfView* self;
    if (msg == WM_NCCREATE) {
        self = (PdfView*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (PdfView*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PdfView::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            OnSize();
            return 0;
        case WM_ERASEBKGND:
            return 1;  // everything is painted in WM_PAINT (no flicker)
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            Paint(hdc);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_VSCROLL:
            OnScrollBar(SB_VERT, LOWORD(wp));
            return 0;
        case WM_HSCROLL:
            OnScrollBar(SB_HORZ, LOWORD(wp));
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            const WORD keys = GET_KEYSTATE_WPARAM(wp);
            if (keys & MK_CONTROL) {  // Ctrl+wheel (and touchpad pinch) zooms at the cursor
                POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(m_hwnd, &pt);
                m_mode = ZoomMode::Custom;
                ApplyZoom(m_zoom * std::pow(1.1, delta / (double)WHEEL_DELTA), pt, true);
                return 0;
            }
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            int64_t step = (lines == WHEEL_PAGESCROLL) ? ClientH() : (int64_t)lines * LineStep();
            int64_t amount = -(int64_t)delta * step / WHEEL_DELTA;
            if (keys & MK_SHIFT)
                ScrollBy(amount, 0);
            else
                ScrollOrFlip(amount);
            return 0;
        }
        case WM_MOUSEHWHEEL:
            ScrollBy((int64_t)GET_WHEEL_DELTA_WPARAM(wp) * LineStep() / WHEEL_DELTA, 0);
            return 0;
        case WM_KEYDOWN: {
            const bool shift = GetKeyState(VK_SHIFT) < 0;
            if (GetKeyState(VK_CONTROL) < 0) {
                if (wp == 'C' || wp == VK_INSERT) {
                    CopySelection();
                    return 0;
                }
                if (wp == 'A') {
                    SelectAll();
                    return 0;
                }
            }
            switch (wp) {
                case VK_UP: ScrollOrFlip(-LineStep()); return 0;
                case VK_DOWN: ScrollOrFlip(LineStep()); return 0;
                case VK_PRIOR: PrevPage(); return 0;
                case VK_NEXT: NextPage(); return 0;
                case VK_LEFT: PrevPage(); return 0;
                case VK_RIGHT: NextPage(); return 0;
                case VK_HOME: GoToPage(0); return 0;
                case VK_END: GoToPage(PageCount() - 1); return 0;
                case VK_SPACE:
                    ScrollOrFlip(shift ? -(ClientH() - LineStep()) : (ClientH() - LineStep()));
                    return 0;
                case VK_ESCAPE:
                    if (m_areaMode) {
                        m_areaMode = false;
                        if (m_areaDragging) ReleaseCapture();
                    } else if (HasSelection())
                        ClearSelection();
                    else
                        SendMessageW(GetParent(m_hwnd), WM_COMMAND, ID_ESCAPE, 0);
                    return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(m_hwnd);
            if (!HasDocument()) return 0;
            const POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (m_areaMode) {  // "copy area as image": start the rectangle
                m_areaDragging = true;
                m_areaStart = m_areaEnd = pt;
                SetCapture(m_hwnd);
                return 0;
            }
            if (const LinkInfo* link = HitLink(pt)) {  // followed on button-up
                m_linkPressed = true;
                m_pressedLink = link->target;
                SetCapture(m_hwnd);
                return 0;
            }
            TextPos pos;
            const bool extend = (wp & MK_SHIFT) && m_hasSel;
            if (HitText(pt, !extend, &pos)) {
                // On text: start (or with Shift, extend) a selection.
                if (!extend) m_selAnchor = pos;
                m_selFocus = pos;
                m_hasSel = true;
                m_selecting = true;
                SetCapture(m_hwnd);
                SetTimer(m_hwnd, kAutoScrollTimer, 30, nullptr);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }
            // Elsewhere: drag to pan.
            ClearSelection();
            m_dragging = true;
            m_dragStart = pt;
            m_dragScrollX = m_scrollX;
            m_dragScrollY = m_scrollY;
            SetCapture(m_hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return 0;
        }
        case WM_LBUTTONDBLCLK:
            SelectWordAt({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_MOUSEMOVE:
            if (m_areaDragging) {
                m_areaEnd = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (!m_dragging && !m_selecting && !m_linkPressed && HasDocument()) {
                UpdateLinkTip(HitLink({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
            }
            if (m_dragging) {
                ScrollTo(m_dragScrollX - (GET_X_LPARAM(lp) - m_dragStart.x),
                         m_dragScrollY - (GET_Y_LPARAM(lp) - m_dragStart.y));
            } else if (m_selecting) {
                UpdateSelectionTo({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            }
            return 0;
        case WM_LBUTTONUP: {
            const POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (m_areaDragging) {
                // Copy the part of the page under the rectangle's start point.
                const int64_t sx = m_scrollX + m_areaStart.x, sy = m_scrollY + m_areaStart.y;
                const int page = PageAt(sx, sy);
                const PageLayout& L = m_layout[(size_t)page];
                RECT r = {(LONG)(std::min(m_areaStart.x, pt.x) + m_scrollX - L.left),
                          (LONG)(std::min(m_areaStart.y, pt.y) + m_scrollY - L.top),
                          (LONG)(std::max(m_areaStart.x, pt.x) + m_scrollX - L.left),
                          (LONG)(std::max(m_areaStart.y, pt.y) + m_scrollY - L.top)};
                r.left = std::max(0L, r.left);
                r.top = std::max(0L, r.top);
                r.right = std::min((LONG)L.w, r.right);
                r.bottom = std::min((LONG)L.h, r.bottom);
                m_areaMode = m_areaDragging = false;
                ReleaseCapture();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                if (r.right - r.left > 2 && r.bottom - r.top > 2) RequestImage(page, r);
                return 0;
            }
            if (m_linkPressed) {
                m_linkPressed = false;
                ReleaseCapture();
                const LinkInfo* link = HitLink(pt);
                if (link && link->target.page == m_pressedLink.page &&
                    link->target.uri == m_pressedLink.uri)
                    FollowLink(m_pressedLink);
                return 0;
            }
            if (m_dragging || m_selecting) ReleaseCapture();
            return 0;
        }
        case WM_CAPTURECHANGED:
            m_dragging = false;
            m_selecting = false;
            m_linkPressed = false;
            if (m_areaDragging) {
                m_areaDragging = false;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            KillTimer(m_hwnd, kAutoScrollTimer);
            return 0;
        case WM_MOUSELEAVE:
            UpdateLinkTip(nullptr);
            return 0;
        case WM_CONTEXTMENU:
            ShowContextMenu(lp);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                LPCWSTR cursor = IDC_ARROW;
                POINT cpt;
                GetCursorPos(&cpt);
                ScreenToClient(m_hwnd, &cpt);
                if (m_areaMode) {
                    cursor = IDC_CROSS;
                } else if (m_linkPressed || (!m_dragging && !m_selecting && HasDocument() &&
                                             HitLink(cpt))) {
                    cursor = IDC_HAND;
                } else if (m_dragging) {
                    cursor = IDC_SIZEALL;
                } else if (m_selecting) {
                    cursor = IDC_IBEAM;
                } else if (HasDocument()) {
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(m_hwnd, &pt);
                    TextPos pos;
                    if (HitText(pt, true, &pos)) cursor = IDC_IBEAM;
                }
                SetCursor(LoadCursorW(nullptr, cursor));
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (wp == kAutoScrollTimer) {
                OnAutoScroll();
                return 0;
            }
            if (wp == kSettleTimer) {
                KillTimer(m_hwnd, kSettleTimer);
                m_zoomSettling = false;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_DESTROY:
            if (m_linkTip) DestroyWindow(m_linkTip);
            m_linkTip = nullptr;
            m_cache.Clear();
            if (m_backDC) DeleteDC(m_backDC);
            if (m_backBmp) DeleteObject(m_backBmp);
            if (m_messageFont) DeleteObject(m_messageFont);
            m_backDC = nullptr;
            m_backBmp = nullptr;
            m_messageFont = nullptr;
            break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ===========================================================================
// Document
// ===========================================================================
void PdfView::SetDocument(uint32_t docId, std::vector<SizeF>&& sizes, int startPage) {
    m_docId = docId;
    m_sizes = std::move(sizes);
    m_cache.Clear();
    m_failed.clear();
    m_text.clear();
    m_textOrder.clear();
    m_textPending.clear();
    m_hasSel = m_selecting = false;
    m_message.clear();
    m_forcedPage = -1;
    m_scrollX = m_scrollY = 0;
    startPage = Clamp(startPage, 0, PageCount() - 1);
    m_singlePage = startPage;
    if (m_mode != ZoomMode::Custom) m_zoom = FitZoom(m_mode, startPage);
    UpdateScale();
    Relayout();
    UpdateScrollBars();
    GoToPage(startPage);
    CenterHorizontally(startPage);
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::CloseDocument() {
    m_docId = 0;
    m_sizes.clear();
    m_layout.clear();
    m_cache.Clear();
    m_failed.clear();
    m_text.clear();
    m_textOrder.clear();
    m_textPending.clear();
    m_hasSel = m_selecting = false;
    m_scrollX = m_scrollY = 0;
    Relayout();
    UpdateScrollBars();
    if (m_worker) m_worker->SetWantedTiles(0, {});
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::SetMessage(const std::wstring& text) {
    m_message = text;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ===========================================================================
// Layout
// ===========================================================================
void PdfView::UpdateScale() {
    m_scaleKey = std::max(1, (int)std::lround(m_zoom * m_dpi / 72.0 * 1000.0));
    m_scale = m_scaleKey / 1000.0;
}

float PdfView::DispW(int page) const {
    const SizeF& sz = m_sizes[(size_t)page];
    return (m_rotation & 1) ? sz.h : sz.w;
}

float PdfView::DispH(int page) const {
    const SizeF& sz = m_sizes[(size_t)page];
    return (m_rotation & 1) ? sz.w : sz.h;
}

// Text, search and link rectangles come from the worker in unrotated page
// points; these two map them to and from the rotated view.
RectF PdfView::ToView(const RectF& r, int page) const {
    const float w = m_sizes[(size_t)page].w, h = m_sizes[(size_t)page].h;
    switch (m_rotation & 3) {
        case 1: return {h - r.bottom, r.left, h - r.top, r.right};
        case 2: return {w - r.right, h - r.bottom, w - r.left, h - r.top};
        case 3: return {r.top, w - r.right, r.bottom, w - r.left};
        default: return r;
    }
}

void PdfView::FromView(float& x, float& y, int page) const {
    const float w = m_sizes[(size_t)page].w, h = m_sizes[(size_t)page].h;
    const float vx = x, vy = y;
    switch (m_rotation & 3) {
        case 1: x = vy; y = h - vx; break;
        case 2: x = w - vx; y = h - vy; break;
        case 3: x = w - vy; y = vx; break;
        default: break;
    }
}

void PdfView::Relayout() {
    m_margin = Dpi(8, m_dpi);
    const int n = PageCount();
    if (n == 0) {
        m_first = 0;
        m_last = -1;
        m_docW = m_docH = 0;
        return;
    }
    m_layout.resize((size_t)n);
    if (IsPaged()) {
        m_singlePage = Clamp(m_singlePage, 0, n - 1);
        m_first = m_last = m_singlePage;
    } else {
        m_first = 0;
        m_last = n - 1;
    }
    // Pages are arranged in rows: one page per row, or two side by side in
    // two-page mode (optionally with the cover page alone). One O(n) pass of
    // integer math; nothing is rendered or allocated.
    const bool two = m_viewMode == ViewMode::TwoPage;
    struct Row { int first, last; int64_t width; };
    std::vector<Row> rows;
    int64_t y = m_margin, maxRowW = 0;
    for (int i = m_first; i <= m_last;) {
        int last = i;
        if (two && !(m_cover && i == 0) && i + 1 <= m_last) last = i + 1;
        int64_t rowW = 0, rowH = 0;
        for (int k = i; k <= last; ++k) {
            PageLayout& L = m_layout[(size_t)k];
            L.w = (int)std::max<long long>(1, std::llround(DispW(k) * m_scale));
            L.h = (int)std::max<long long>(1, std::llround(DispH(k) * m_scale));
            rowW += L.w + (k > i ? m_margin : 0);
            rowH = std::max<int64_t>(rowH, L.h);
        }
        for (int k = i; k <= last; ++k) {
            PageLayout& L = m_layout[(size_t)k];
            L.rowTop = y;
            L.rowBottom = y + rowH;
            L.top = y + (rowH - L.h) / 2;
        }
        rows.push_back({i, last, rowW});
        maxRowW = std::max(maxRowW, rowW);
        y += rowH + m_margin;
        i = last + 1;
    }
    m_docW = maxRowW + 2 * m_margin;
    m_docH = y;
    // Centre each row in the wider of document and window.
    const int64_t area = std::max<int64_t>(m_docW, ClientW());
    for (const Row& r : rows) {
        int64_t x = (area - r.width) / 2;
        if (two && r.first == r.last && !(m_cover && r.first == 0)) {
            // A lone last page sits on the left, where its partner would be.
            x = (area - maxRowW) / 2;
        }
        for (int k = r.first; k <= r.last; ++k) {
            m_layout[(size_t)k].left = x;
            x += m_layout[(size_t)k].w + m_margin;
        }
    }
}

int64_t PdfView::MaxScrollX() const { return std::max<int64_t>(0, m_docW - ClientW()); }
int64_t PdfView::MaxScrollY() const { return std::max<int64_t>(0, m_docH - ClientH()); }

int64_t PdfView::PageLeft(int page) const { return m_layout[(size_t)page].left; }

// Documents mixing narrow and wide pages are wider than a narrow page; show
// the given page centred rather than scrolled to the document's left edge.
void PdfView::CenterHorizontally(int page) {
    if (page < m_first || page > m_last) return;
    const int64_t x = PageLeft(page) + m_layout[(size_t)page].w / 2 - ClientW() / 2;
    m_scrollX = Clamp<int64_t>(x, 0, MaxScrollX());
    UpdateScrollBars();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

int PdfView::PageAtY(int64_t docY) const {
    // Last laid-out page whose row starts at or above docY.
    int lo = m_first, hi = m_last;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (m_layout[(size_t)mid].rowTop <= docY)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

int PdfView::RowFirst(int page) const {
    while (page > m_first && m_layout[(size_t)page - 1].rowTop == m_layout[(size_t)page].rowTop)
        --page;
    return page;
}

int PdfView::PageAt(int64_t docX, int64_t docY) const {
    const int last = PageAtY(docY);
    int best = last;
    int64_t bestDist = INT64_MAX;
    for (int p = RowFirst(last); p <= last; ++p) {
        const PageLayout& L = m_layout[(size_t)p];
        const int64_t d = docX < L.left ? L.left - docX : (docX > L.left + L.w ? docX - L.left - L.w : 0);
        if (d < bestDist) {
            bestDist = d;
            best = p;
        }
    }
    return best;
}

void PdfView::VisibleRange(int64_t y0, int64_t y1, int& first, int& last) const {
    first = 0;
    last = -1;
    if (m_last < m_first) return;
    // First page whose row ends below y0 (binary search; rows are ordered).
    int lo = m_first, hi = m_last + 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (m_layout[(size_t)mid].rowBottom <= y0)
            lo = mid + 1;
        else
            hi = mid;
    }
    first = lo;
    last = first - 1;
    for (int i = first; i <= m_last && m_layout[(size_t)i].rowTop < y1; ++i) last = i;
}

void PdfView::UpdateScrollBars() {
    // Scrollbars are 32-bit; for gigantic documents use coarser units.
    m_sbUnit = 1;
    while (m_docH / m_sbUnit > 0x3FFFFFFF) m_sbUnit *= 2;

    SCROLLINFO si{sizeof(si)};
    // Vertical bar is always shown (possibly disabled) so the client width
    // does not jump when pages are added, which would disturb fit-width.
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = m_docH > 0 ? (int)((m_docH - 1) / m_sbUnit) : 0;
    si.nPage = (UINT)(ClientH() / m_sbUnit);
    si.nPos = (int)(m_scrollY / m_sbUnit);
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);

    si.fMask = SIF_ALL;
    si.nMax = m_docW > 0 ? (int)(m_docW - 1) : 0;
    si.nPage = (UINT)ClientW();
    si.nPos = (int)m_scrollX;
    SetScrollInfo(m_hwnd, SB_HORZ, &si, TRUE);
}

void PdfView::OnSize() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_clientW = rc.right;
    m_clientH = rc.bottom;
    // Changing scrollbar visibility re-enters WM_SIZE; iterate instead of
    // recursing until the layout is stable.
    if (m_inSize) {
        m_sizeDirty = true;
        return;
    }
    m_inSize = true;
    for (int i = 0; i < 3; ++i) {
        m_sizeDirty = false;
        if (HasDocument() && m_mode != ZoomMode::Custom) {
            ApplyZoom(FitZoom(m_mode, CurrentPage()), {ClientW() / 2, 0}, false);
        } else {
            Relayout();
            m_scrollX = Clamp<int64_t>(m_scrollX, 0, MaxScrollX());
            m_scrollY = Clamp<int64_t>(m_scrollY, 0, MaxScrollY());
            UpdateScrollBars();
        }
        if (!m_sizeDirty) break;
    }
    m_inSize = false;
    UpdateCacheBudget();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::UpdateCacheBudget() {
    // Enough for the visible tiles plus ~1.5 screens of prefetch, bounded
    // so that large or multiple monitors can not make the cache balloon.
    const size_t screen = (size_t)std::max(1, ClientW()) * (size_t)std::max(1, ClientH()) * 4;
    m_cache.SetBudget(Clamp<size_t>(screen * 5 + (16u << 20), 64u << 20, 256u << 20));
}

int PdfView::LineStep() const { return Dpi(40, m_dpi); }

// ===========================================================================
// Scrolling & navigation
// ===========================================================================
void PdfView::ScrollTo(int64_t x, int64_t y) {
    x = Clamp<int64_t>(x, 0, MaxScrollX());
    y = Clamp<int64_t>(y, 0, MaxScrollY());
    if (x == m_scrollX && y == m_scrollY) return;
    m_forcedPage = -1;
    m_scrollX = x;
    m_scrollY = y;
    SCROLLINFO si{sizeof(si)};
    si.fMask = SIF_POS;
    si.nPos = (int)(m_scrollY / m_sbUnit);
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
    si.nPos = (int)m_scrollX;
    SetScrollInfo(m_hwnd, SB_HORZ, &si, TRUE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::ScrollOrFlip(int64_t dy) {
    if (IsPaged() && HasDocument()) {
        // Single page mode: scrolling past the page edge turns the page.
        // A short cooldown stops touchpad inertia from flipping many pages.
        const ULONGLONG now = GetTickCount64();
        if (dy > 0 && m_scrollY >= MaxScrollY() && m_singlePage < PageCount() - 1) {
            if (now - m_lastFlip > 300) {
                m_lastFlip = now;
                GoToPage(m_singlePage + 1);
            }
            return;
        }
        if (dy < 0 && m_scrollY <= 0 && m_singlePage > 0) {
            if (now - m_lastFlip > 300) {
                m_lastFlip = now;
                GoToPage(m_singlePage - 1);
                ScrollTo(m_scrollX, MaxScrollY());
            }
            return;
        }
    }
    ScrollBy(0, dy);
}

void PdfView::OnScrollBar(int bar, int code) {
    SCROLLINFO si{sizeof(si)};
    si.fMask = SIF_ALL;
    GetScrollInfo(m_hwnd, bar, &si);
    const bool vert = bar == SB_VERT;
    const int64_t unit = vert ? m_sbUnit : 1;
    const int64_t page = vert ? ClientH() : ClientW();
    int64_t pos = vert ? m_scrollY : m_scrollX;
    switch (code) {
        case SB_LINEUP: pos -= LineStep(); break;
        case SB_LINEDOWN: pos += LineStep(); break;
        case SB_PAGEUP: pos -= page - LineStep(); break;
        case SB_PAGEDOWN: pos += page - LineStep(); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = (int64_t)si.nTrackPos * unit; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = INT64_MAX / 2; break;
        default: return;
    }
    if (vert)
        ScrollTo(m_scrollX, pos);
    else
        ScrollTo(pos, m_scrollY);
}

int PdfView::CurrentPage() const {
    if (!HasDocument()) return 0;
    if (IsPaged()) return m_singlePage;
    if (m_forcedPage >= 0 && m_forcedPage < PageCount()) return m_forcedPage;
    // Otherwise: the page occupying most of the viewport.
    const int64_t y0 = m_scrollY, y1 = m_scrollY + ClientH();
    int first, last;
    VisibleRange(y0, y1, first, last);
    if (last < first) return Clamp(first, 0, PageCount() - 1);
    int best = first;
    int64_t bestVisible = -1;
    for (int i = first; i <= last; ++i) {
        const PageLayout& L = m_layout[(size_t)i];
        int64_t visible = std::min(L.top + L.h, y1) - std::max(L.top, y0);
        if (visible > bestVisible) {
            bestVisible = visible;
            best = i;
        }
    }
    return best;
}

void PdfView::GoToPage(int page) {
    if (!HasDocument()) return;
    page = Clamp(page, 0, PageCount() - 1);
    if (IsPaged()) {
        if (page != m_singlePage) {
            m_singlePage = page;
            if (m_mode != ZoomMode::Custom) {  // pages may differ in size
                m_zoom = FitZoom(m_mode, page);
                UpdateScale();
            }
            Relayout();
            UpdateScrollBars();
        }
        m_scrollY = 0;
        m_scrollX = Clamp<int64_t>(m_scrollX, 0, MaxScrollX());
        UpdateScrollBars();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        Notify();
        return;
    }
    ScrollTo(m_scrollX, m_layout[(size_t)page].rowTop - m_margin);
    m_forcedPage = page;  // so the page counter shows exactly this page
    Notify();
}

// Next/previous move by rows, so two-page mode turns a whole spread.
void PdfView::NextPage() {
    if (!HasDocument()) return;
    const int cur = CurrentPage();
    if (IsPaged()) {
        GoToPage(cur + 1);
        return;
    }
    int p = cur;
    while (p < m_last && m_layout[(size_t)p].rowTop == m_layout[(size_t)cur].rowTop) ++p;
    if (m_layout[(size_t)p].rowTop != m_layout[(size_t)cur].rowTop) GoToPage(p);
}

void PdfView::PrevPage() {
    if (!HasDocument()) return;
    const int cur = CurrentPage();
    if (IsPaged()) {
        GoToPage(cur - 1);
        return;
    }
    const int first = RowFirst(cur);
    if (first > m_first) GoToPage(RowFirst(first - 1));
}

void PdfView::GoToTarget(const LinkTarget& target) {
    if (!HasDocument() || target.page < 0 || target.page >= PageCount()) return;
    GoToPage(target.page);
    if (target.destY < 0 || IsPaged() || m_rotation != 0) return;
    // Destination y is in PDF user space (from the bottom); approximate the
    // page's origin as its bottom-left corner, which holds for most files.
    const float fromTop = Clamp(m_sizes[(size_t)target.page].h - target.destY, 0.0f,
                                m_sizes[(size_t)target.page].h);
    ScrollTo(m_scrollX, m_layout[(size_t)target.page].top + (int64_t)(fromTop * m_scale) - m_margin);
    m_forcedPage = target.page;
    Notify();
}

void PdfView::SetViewMode(ViewMode mode) {
    if (mode == m_viewMode) return;
    const int page = CurrentPage();
    m_viewMode = mode;
    m_singlePage = page;
    m_cache.Clear();  // tiles of other pages are no longer useful
    if (HasDocument() && m_mode != ZoomMode::Custom) {
        m_zoom = FitZoom(m_mode, page);
        UpdateScale();
    }
    Relayout();
    UpdateScrollBars();
    GoToPage(page);
    CenterHorizontally(page);
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::SetCoverPage(bool cover) {
    if (cover == m_cover) return;
    m_cover = cover;
    if (m_viewMode != ViewMode::TwoPage) return;
    const int page = CurrentPage();
    Relayout();
    UpdateScrollBars();
    GoToPage(page);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::Rotate(int quarterTurns) {
    const int page = CurrentPage();
    m_rotation = ((m_rotation + quarterTurns) % 4 + 4) % 4;
    m_cache.Clear();
    m_failed.clear();
    if (HasDocument() && m_mode != ZoomMode::Custom) {
        m_zoom = FitZoom(m_mode, page);
        UpdateScale();
    }
    Relayout();
    UpdateScrollBars();
    if (HasDocument()) {
        GoToPage(page);
        CenterHorizontally(page);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::SetPageColors(int mode) {
    if (mode == m_colors) return;
    m_colors = mode;
    m_cache.Clear();  // tiles are re-rendered in the new colours
    m_failed.clear();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::ScrollToHit(const SearchHit& hit) {
    if (!HasDocument() || hit.rects.empty() || hit.page < 0 || hit.page >= PageCount()) return;
    if (IsPaged() && hit.page != m_singlePage) GoToPage(hit.page);
    if (hit.page < m_first || hit.page > m_last) return;
    RectF u = ToView(hit.rects.front(), hit.page);
    for (const RectF& raw : hit.rects) {
        const RectF r = ToView(raw, hit.page);
        u.left = std::min(u.left, r.left);
        u.top = std::min(u.top, r.top);
        u.right = std::max(u.right, r.right);
        u.bottom = std::max(u.bottom, r.bottom);
    }
    const PageLayout& L = m_layout[(size_t)hit.page];
    const int64_t x0 = PageLeft(hit.page) + (int64_t)(u.left * m_scale);
    const int64_t x1 = PageLeft(hit.page) + (int64_t)(u.right * m_scale);
    const int64_t y0 = L.top + (int64_t)(u.top * m_scale);
    const int64_t y1 = L.top + (int64_t)(u.bottom * m_scale);
    int64_t nx = m_scrollX, ny = m_scrollY;
    // Only scroll if the match is not already fully visible.
    if (y0 < m_scrollY || y1 > m_scrollY + ClientH()) ny = y0 - ClientH() / 3;
    if (x0 < m_scrollX || x1 > m_scrollX + ClientW()) nx = x0 - ClientW() / 3;
    ScrollTo(nx, ny);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ===========================================================================
// Zoom
// ===========================================================================
double PdfView::FitZoom(ZoomMode mode, int page) const {
    if (!HasDocument()) return m_zoom;
    page = Clamp(page, 0, PageCount() - 1);
    const double pxPerPt = m_dpi / 72.0;  // at 100 %
    const int margin = Dpi(8, m_dpi);
    // Fit a whole row. In two-page mode that is always a full spread, even
    // on the cover or a lone last page, so the zoom does not jump while
    // scrolling through the document.
    double rowW = DispW(page), rowH = DispH(page);
    int gaps = 0;
    if (m_viewMode == ViewMode::TwoPage) {
        int first = m_cover ? (page == 0 ? 1 : (page - 1) / 2 * 2 + 1) : page / 2 * 2;
        if (first + 1 >= PageCount()) first = std::max(0, PageCount() - 2);
        if (first + 1 < PageCount()) {
            rowW = DispW(first) + DispW(first + 1);
            rowH = std::max(DispH(first), DispH(first + 1));
        } else {
            rowW = 2 * DispW(page);  // one-page document
        }
        gaps = 1;
    }
    const double availW = std::max(50, ClientW() - (2 + gaps) * margin - 2);  // -2: rounding slack
    const double availH = std::max(50, ClientH() - 2 * margin);
    double z = availW / (rowW * pxPerPt);
    if (mode == ZoomMode::FitPage) z = std::min(z, availH / (rowH * pxPerPt));
    return Clamp(z, kMinZoom, kMaxZoom);
}

void PdfView::ApplyZoom(double zoom, POINT anchor, bool settle) {
    zoom = Clamp(zoom, kMinZoom, kMaxZoom);
    if (!HasDocument()) {
        m_zoom = zoom;
        UpdateScale();
        Notify();
        return;
    }
    // Remember which point of which page is under the anchor...
    const int64_t docX = m_scrollX + anchor.x, docY = m_scrollY + anchor.y;
    const int page = PageAt(docX, docY);
    const double ptX = (docX - PageLeft(page)) / m_scale;
    const double ptY = (docY - m_layout[(size_t)page].top) / m_scale;

    const int oldKey = m_scaleKey;
    m_zoom = zoom;
    UpdateScale();
    Relayout();

    // ...and keep that point under the anchor after the zoom.
    const int64_t nx = PageLeft(page) + std::llround(ptX * m_scale) - anchor.x;
    const int64_t ny = m_layout[(size_t)page].top + std::llround(ptY * m_scale) - anchor.y;
    m_scrollX = Clamp<int64_t>(nx, 0, MaxScrollX());
    m_scrollY = Clamp<int64_t>(ny, 0, MaxScrollY());
    UpdateScrollBars();
    m_scrollX = Clamp<int64_t>(m_scrollX, 0, MaxScrollX());  // client may have changed
    m_scrollY = Clamp<int64_t>(m_scrollY, 0, MaxScrollY());

    if (m_scaleKey != oldKey) {
        m_failed.clear();
        // While the wheel is still turning, show stretched old tiles and
        // wait a moment before rendering, instead of rendering every step.
        if (settle) {
            m_zoomSettling = true;
            SetTimer(m_hwnd, kSettleTimer, kSettleMs, nullptr);
        }
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::SetZoom(double zoom) {
    m_mode = ZoomMode::Custom;
    ApplyZoom(zoom, {ClientW() / 2, ClientH() / 2}, false);
}

void PdfView::SetZoomMode(ZoomMode mode) {
    m_mode = mode;
    if (mode == ZoomMode::Custom || !HasDocument()) {
        Notify();
        return;
    }
    const int page = CurrentPage();
    ApplyZoom(FitZoom(mode, page), {ClientW() / 2, 0}, false);
    if (mode == ZoomMode::FitPage) GoToPage(page);
    CenterHorizontally(page);
}

void PdfView::ZoomIn() {
    double next = kMaxZoom;
    for (int i = 0; i < kZoomPresetCount; ++i) {
        if (kZoomPresets[i] > m_zoom * 1.001) {
            next = kZoomPresets[i];
            break;
        }
    }
    SetZoom(next);
}

void PdfView::ZoomOut() {
    double next = kMinZoom;
    for (int i = kZoomPresetCount - 1; i >= 0; --i) {
        if (kZoomPresets[i] < m_zoom * 0.999) {
            next = kZoomPresets[i];
            break;
        }
    }
    SetZoom(next);
}

void PdfView::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    if (m_messageFont) DeleteObject(m_messageFont);
    m_messageFont = CreateMessageFont(m_dpi, 110);
    const int page = CurrentPage();
    m_cache.Clear();
    UpdateScale();
    Relayout();
    UpdateScrollBars();
    if (HasDocument()) GoToPage(page);
    UpdateCacheBudget();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::OnThemeChanged() {
    ApplyScrollbarTheme(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::TrimMemory() {
    m_cache.Clear();
    m_text.clear();
    m_textOrder.clear();
    m_textPending.clear();
    if (m_backDC) {
        DeleteDC(m_backDC);
        m_backDC = nullptr;
    }
    if (m_backBmp) {
        DeleteObject(m_backBmp);
        m_backBmp = nullptr;
    }
    m_backW = m_backH = 0;
    if (m_worker) {
        m_worker->SetWantedTiles(m_docId, {});
        m_worker->TrimMemory();
    }
}

// ===========================================================================
// Tiles
// ===========================================================================
int PdfView::TileW(int pageW) const { return pageW <= kTileMaxRowW ? pageW : kTileColW; }

void PdfView::OnTileReady(TileResult* result) {
    std::unique_ptr<TileResult> res(result);
    const TileRequest& r = res->req;
    if (res->docId != m_docId || !HasDocument()) return;
    if (r.page < m_first || r.page > m_last) return;
    const TileKey key{r.page, r.scaleKey, r.tx, r.ty};
    if (!res->pixels.bits) {  // out of memory: do not ask again at this scale
        m_failed.insert(key);
        return;
    }
    // Drop results for a zoom level the user has already left.
    if (r.scaleKey != m_scaleKey || r.pageW != m_layout[(size_t)r.page].w ||
        r.rotate != m_rotation || r.colorMode != m_colors)
        return;
    m_cache.Insert(key, r.x, r.y, std::move(res->pixels));
    InvalidateTile(r);
}

void PdfView::InvalidateTile(const TileRequest& r) {
    const int64_t left = PageLeft(r.page) - m_scrollX + r.x;
    const int64_t top = m_layout[(size_t)r.page].top - m_scrollY + r.y;
    if (left >= ClientW() || top >= ClientH() || left + r.w <= 0 || top + r.h <= 0)
        return;  // prefetched tile outside the viewport: nothing to repaint
    RECT rc = {(int)std::max<int64_t>(0, left), (int)std::max<int64_t>(0, top),
               (int)std::min<int64_t>(ClientW(), left + r.w),
               (int)std::min<int64_t>(ClientH(), top + r.h)};
    InvalidateRect(m_hwnd, &rc, FALSE);
}

void PdfView::AddPrefetch(int64_t y0, int64_t y1, size_t& budgetLeft,
                          std::vector<TileRequest>& out) {
    int first, last;
    VisibleRange(y0, y1, first, last);
    for (int i = first; i <= last; ++i) {
        const PageLayout& L = m_layout[(size_t)i];
        const int64_t left = PageLeft(i) - m_scrollX;
        const int64_t vx0 = std::max<int64_t>(0, -left);
        const int64_t vx1 = std::min<int64_t>(L.w, ClientW() - left);
        const int64_t vy0 = std::max<int64_t>(0, y0 - L.top);
        const int64_t vy1 = std::min<int64_t>(L.h, y1 - L.top);
        if (vx0 >= vx1 || vy0 >= vy1) continue;
        const int tw = TileW(L.w);
        for (int ty = (int)(vy0 / kTileH); ty <= (int)((vy1 - 1) / kTileH); ++ty) {
            for (int tx = (int)(vx0 / tw); tx <= (int)((vx1 - 1) / tw); ++tx) {
                const TileKey key{i, m_scaleKey, tx, ty};
                const int x = tx * tw, y = ty * kTileH;
                const int w = std::min(tw, L.w - x), h = std::min(kTileH, L.h - y);
                const size_t bytes = (size_t)w * h * 4;
                if (bytes > budgetLeft) return;  // cache would thrash: stop here
                budgetLeft -= bytes;
                if (m_cache.Touch(key) || m_failed.count(key)) continue;
                out.push_back({i, m_scaleKey, tx, ty, x, y, w, h, L.w, L.h, m_rotation, m_colors});
            }
        }
    }
}

void PdfView::RequestTiles(std::vector<std::pair<int64_t, TileRequest>>& missing,
                           size_t visibleBytes) {
    if (!m_worker) return;
    if (m_zoomSettling) {  // wait until the wheel stops
        m_worker->SetWantedTiles(m_docId, {});
        return;
    }
    // Visible tiles first, nearest to the viewport centre first...
    std::sort(missing.begin(), missing.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<TileRequest> list;
    list.reserve(missing.size() + 16);
    for (auto& m : missing) list.push_back(m.second);

    // ...then prefetch the next screen (reading direction) and half a
    // screen above, but only as much as fits in the cache budget.
    const size_t budget = m_cache.Budget() / 10 * 9;
    size_t left = budget > visibleBytes ? budget - visibleBytes : 0;
    AddPrefetch(m_scrollY + ClientH(), m_scrollY + 2 * ClientH(), left, list);
    AddPrefetch(m_scrollY - ClientH() / 2, m_scrollY, left, list);
    m_worker->SetWantedTiles(m_docId, std::move(list));
}

// ===========================================================================
// Painting
// ===========================================================================
void PdfView::Paint(HDC hdc) {
    const Theme& th = CurrentTheme();
    const int w = std::max(1, ClientW()), h = std::max(1, ClientH());

    // Persistent back buffer (one screen) -> flicker-free painting.
    if (!m_backDC) m_backDC = CreateCompatibleDC(hdc);
    if (!m_backBmp || m_backW != w || m_backH != h) {
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        SelectObject(m_backDC, bmp);
        if (m_backBmp) DeleteObject(m_backBmp);
        m_backBmp = bmp;
        m_backW = w;
        m_backH = h;
    }
    HDC dc = m_backDC;
    const RECT client = {0, 0, w, h};
    FillSolid(dc, client, th.canvasBg);

    if (!HasDocument()) {
        const std::wstring text =
            m_message.empty() ? L"Open a PDF with Ctrl+O, or drop a file here." : m_message;
        HGDIOBJ old = SelectObject(dc, m_messageFont);
        SetTextColor(dc, th.canvasText);
        SetBkMode(dc, TRANSPARENT);
        RECT calc = {Dpi(24, m_dpi), 0, w - Dpi(24, m_dpi), 0};
        DrawTextW(dc, text.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
        RECT r = {Dpi(24, m_dpi), (h - calc.bottom) / 2, w - Dpi(24, m_dpi), h};
        DrawTextW(dc, text.c_str(), -1, &r, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
        SelectObject(dc, old);
        BitBlt(hdc, 0, 0, w, h, dc, 0, 0, SRCCOPY);
        return;
    }

    m_cache.BeginFrame();
    std::vector<std::pair<int64_t, TileRequest>> missing;
    size_t visibleBytes = 0;
    int first, last;
    VisibleRange(m_scrollY, m_scrollY + h, first, last);
    for (int i = first; i <= last; ++i) PaintPage(dc, i, missing, visibleBytes);

    RequestTiles(missing, visibleBytes);
    // Once everything visible is sharp, placeholder tiles from previous
    // zoom levels are released.
    if (missing.empty()) m_cache.DropScalesOtherThan(m_scaleKey);

    if (m_areaDragging) {  // rubber band for "copy area as image"
        RECT band = {std::min(m_areaStart.x, m_areaEnd.x), std::min(m_areaStart.y, m_areaEnd.y),
                     std::max(m_areaStart.x, m_areaEnd.x), std::max(m_areaStart.y, m_areaEnd.y)};
        HBRUSH tint = CreateSolidBrush(kSelectionColor);
        HGDIOBJ old = SelectObject(dc, tint);
        BitBlt(dc, band.left, band.top, band.right - band.left, band.bottom - band.top, nullptr, 0,
               0, kRopDestAndPattern);
        SelectObject(dc, old);
        DeleteObject(tint);
        FrameRect(dc, &band, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    BitBlt(hdc, 0, 0, w, h, dc, 0, 0, SRCCOPY);
}

void PdfView::PaintPage(HDC dc, int page, std::vector<std::pair<int64_t, TileRequest>>& missing,
                        size_t& visibleBytes) {
    const Theme& th = CurrentTheme();
    const PageLayout& L = m_layout[(size_t)page];
    const int64_t left = PageLeft(page) - m_scrollX;
    const int64_t top = L.top - m_scrollY;

    // Visible part of the page, in page pixel coordinates.
    const int64_t vx0 = std::max<int64_t>(0, -left), vx1 = std::min<int64_t>(L.w, ClientW() - left);
    const int64_t vy0 = std::max<int64_t>(0, -top), vy1 = std::min<int64_t>(L.h, ClientH() - top);
    if (vx0 >= vx1 || vy0 >= vy1) return;
    const RECT vis = {(int)(left + vx0), (int)(top + vy0), (int)(left + vx1), (int)(top + vy1)};

    // Thin border + white paper (shown until tiles arrive).
    RECT outer = vis;
    if (vx0 == 0) outer.left -= 1;
    if (vx1 == L.w) outer.right += 1;
    if (vy0 == 0) outer.top -= 1;
    if (vy1 == L.h) outer.bottom += 1;
    FillSolid(dc, outer, th.pageBorder);
    const COLORREF paper = m_colors == kColorsDark  ? RGB(30, 30, 30)
                           : m_colors == kColorsDim ? RGB(200, 200, 200)
                                                    : RGB(255, 255, 255);
    FillSolid(dc, vis, paper);

    HRGN clip = CreateRectRgnIndirect(&vis);
    SelectClipRgn(dc, clip);

    // 1. Placeholders: tiles rendered at another zoom level, stretched.
    m_cache.ForEachOtherScale(page, m_scaleKey, [&](const Tile& t) {
        const double f = m_scale / (t.key.scaleKey / 1000.0);
        const int64_t x0 = left + std::llround(t.x * f), y0 = top + std::llround(t.y * f);
        const int64_t x1 = left + std::llround((t.x + t.pixels.width) * f);
        const int64_t y1 = top + std::llround((t.y + t.pixels.height) * f);
        if (x1 <= vis.left || x0 >= vis.right || y1 <= vis.top || y0 >= vis.bottom) return;
        if (std::llabs(x0) > (1 << 26) || std::llabs(y1) > (1 << 26)) return;  // GDI limits
        SetStretchBltMode(dc, f < 1.0 ? HALFTONE : COLORONCOLOR);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        DrawPixels(dc, (int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0), t.pixels);
    });

    // 2. Sharp tiles at the current scale; collect the missing ones.
    SetStretchBltMode(dc, COLORONCOLOR);
    const int tw = TileW(L.w);
    const int64_t cx = ClientW() / 2, cy = ClientH() / 2;
    for (int ty = (int)(vy0 / kTileH); ty <= (int)((vy1 - 1) / kTileH); ++ty) {
        for (int tx = (int)(vx0 / tw); tx <= (int)((vx1 - 1) / tw); ++tx) {
            const int x = tx * tw, y = ty * kTileH;
            const int tileW = std::min(tw, L.w - x), tileH = std::min(kTileH, L.h - y);
            visibleBytes += (size_t)tileW * tileH * 4;
            const TileKey key{page, m_scaleKey, tx, ty};
            if (const Tile* t = m_cache.Use(key)) {
                DrawPixels(dc, (int)(left + x), (int)(top + y), tileW, tileH, t->pixels);
            } else if (!m_failed.count(key)) {
                const int64_t dist = std::llabs(left + x + tileW / 2 - cx) +
                                     std::llabs(top + y + tileH / 2 - cy);
                missing.push_back({dist, {page, m_scaleKey, tx, ty, x, y, tileW, tileH, L.w, L.h,
                                          m_rotation, m_colors}});
            }
        }
    }

    // 3. Search highlights, drawn as a "multiply" so text stays readable.
    if (m_search && !m_search->matches.empty()) {
        auto range = m_search->RangeForPage(page);
        if (range.first < range.second) {
            HBRUSH normal = CreateSolidBrush(RGB(255, 226, 64));
            HBRUSH current = CreateSolidBrush(RGB(255, 150, 40));
            for (size_t k = range.first; k < range.second; ++k) {
                HGDIOBJ old = SelectObject(dc, (int)k == m_search->current ? current : normal);
                for (const RectF& raw : m_search->matches[k].rects) {
                    const RectF r = ToView(raw, page);
                    const int64_t x0 = left + (int64_t)std::floor(r.left * m_scale) - 1;
                    const int64_t y0 = top + (int64_t)std::floor(r.top * m_scale) - 1;
                    const int64_t x1 = left + (int64_t)std::ceil(r.right * m_scale) + 1;
                    const int64_t y1 = top + (int64_t)std::ceil(r.bottom * m_scale) + 1;
                    if (x1 <= vis.left || x0 >= vis.right || y1 <= vis.top || y0 >= vis.bottom)
                        continue;
                    const int ix0 = (int)std::max<int64_t>(x0, vis.left);
                    const int iy0 = (int)std::max<int64_t>(y0, vis.top);
                    const int ix1 = (int)std::min<int64_t>(x1, vis.right);
                    const int iy1 = (int)std::min<int64_t>(y1, vis.bottom);
                    BitBlt(dc, ix0, iy0, ix1 - ix0, iy1 - iy0, nullptr, 0, 0, kRopDestAndPattern);
                }
                SelectObject(dc, old);
            }
            DeleteObject(normal);
            DeleteObject(current);
        }
    }

    // 4. Text selection (same "multiply" technique, in blue).
    if (HasSelection()) DrawSelection(dc, page, left, top, vis);

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// ===========================================================================
// Text selection
//
// Hit-testing and highlighting run on the UI thread using a small cache of
// per-page character boxes ("text layers") fetched from the worker the
// first time the mouse moves over a page. Copying is done by the worker
// (FPDFText_GetText), so selections spanning many pages never need all of
// their text layers in memory.
// ===========================================================================
const PdfView::TextLayer* PdfView::GetTextLayer(int page, bool request) {
    auto it = m_text.find(page);
    if (it != m_text.end()) {
        auto pos = std::find(m_textOrder.begin(), m_textOrder.end(), page);
        if (pos != m_textOrder.begin() && pos != m_textOrder.end()) {
            m_textOrder.erase(pos);
            m_textOrder.insert(m_textOrder.begin(), page);
        }
        return &it->second;
    }
    if (request && m_worker && !m_textPending.count(page)) {
        m_textPending.insert(page);
        m_worker->RequestTextLayer(m_docId, page);
    }
    return nullptr;
}

void PdfView::OnTextLayer(TextLayerResult* result) {
    std::unique_ptr<TextLayerResult> res(result);
    if (res->docId != m_docId || !HasDocument()) return;
    m_textPending.erase(res->page);
    TextLayer& layer = m_text[res->page];
    layer.chars = std::move(res->chars);
    layer.links = std::move(res->links);
    m_textOrder.erase(std::remove(m_textOrder.begin(), m_textOrder.end(), res->page),
                      m_textOrder.end());
    m_textOrder.insert(m_textOrder.begin(), res->page);
    while (m_textOrder.size() > kMaxTextLayers) {
        m_text.erase(m_textOrder.back());
        m_textOrder.pop_back();
    }
    if (HasSelection()) InvalidateRect(m_hwnd, nullptr, FALSE);
}

bool PdfView::HitText(POINT pt, bool strict, TextPos* caret, int* charIndex) {
    if (!HasDocument() || m_last < m_first) return false;
    const int64_t docX = m_scrollX + pt.x, docY = m_scrollY + pt.y;
    const int page = PageAt(docX, docY);
    const TextLayer* layer = GetTextLayer(page, true);
    if (!layer) return false;
    float px = (float)((docX - PageLeft(page)) / m_scale);
    float py = (float)((docY - m_layout[(size_t)page].top) / m_scale);
    FromView(px, py, page);

    // Nearest glyph, strongly preferring the line the point is on.
    int best = -1;
    float bestScore = 0, bestDx = 0, bestDy = 0;
    for (size_t i = 0; i < layer->chars.size(); ++i) {
        const TextChar& c = layer->chars[i];
        if (!c.hasBox) continue;
        const float dx = std::max({0.0f, c.box.left - px, px - c.box.right});
        const float dy = std::max({0.0f, c.box.top - py, py - c.box.bottom});
        const float score = dy * 1000.0f + dx;
        if (best < 0 || score < bestScore) {
            best = (int)i;
            bestScore = score;
            bestDx = dx;
            bestDy = dy;
        }
    }
    if (best < 0) return false;  // no text on this page (e.g. a scan)
    if (strict && (bestDy > 1.0f || bestDx > kTextSlackPt)) return false;
    const RectF& b = layer->chars[(size_t)best].box;
    caret->page = page;
    caret->index = px < (b.left + b.right) / 2 ? best : best + 1;
    if (charIndex) *charIndex = best;
    return true;
}

void PdfView::SelectionBounds(TextPos& start, TextPos& end) const {
    start = std::min(m_selAnchor, m_selFocus);
    end = std::max(m_selAnchor, m_selFocus);
}

void PdfView::UpdateSelectionTo(POINT pt) {
    // Clamp into the viewport so dragging past an edge keeps selecting.
    pt.x = Clamp<LONG>(pt.x, 0, std::max(0, ClientW() - 1));
    pt.y = Clamp<LONG>(pt.y, 0, std::max(0, ClientH() - 1));
    TextPos pos;
    if (HitText(pt, false, &pos) && !(pos == m_selFocus)) {
        m_selFocus = pos;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void PdfView::OnAutoScroll() {
    if (!m_selecting) {
        KillTimer(m_hwnd, kAutoScrollTimer);
        return;
    }
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hwnd, &pt);
    int64_t dx = 0, dy = 0;
    if (pt.y < 0) dy = std::max<int64_t>(pt.y, -LineStep());
    if (pt.y >= ClientH()) dy = std::min<int64_t>(pt.y - ClientH() + 1, LineStep());
    if (pt.x < 0) dx = std::max<int64_t>(pt.x, -LineStep());
    if (pt.x >= ClientW()) dx = std::min<int64_t>(pt.x - ClientW() + 1, LineStep());
    if (dx || dy) {
        ScrollBy(dx, dy);
        UpdateSelectionTo(pt);
    }
}

void PdfView::SelectWordAt(POINT pt) {
    TextPos pos;
    int ci = -1;
    if (!HitText(pt, true, &pos, &ci)) return;
    const TextLayer* layer = GetTextLayer(pos.page, false);
    if (!layer || ci < 0) return;
    const auto& chars = layer->chars;
    int l = ci, r = ci;
    if (IsWordChar(chars[(size_t)ci].cp)) {
        while (l > 0 && chars[(size_t)l - 1].hasBox && IsWordChar(chars[(size_t)l - 1].cp)) --l;
        while (r + 1 < (int)chars.size() && chars[(size_t)r + 1].hasBox &&
               IsWordChar(chars[(size_t)r + 1].cp))
            ++r;
    }
    m_selAnchor = {pos.page, l};
    m_selFocus = {pos.page, r + 1};
    m_hasSel = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::SelectAll() {
    if (!HasDocument()) return;
    m_selAnchor = {0, 0};
    m_selFocus = {PageCount() - 1, INT_MAX / 2};  // clamped to the page's length
    m_hasSel = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::ClearSelection() {
    if (!m_hasSel) return;
    m_hasSel = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::CopySelection() {
    if (!HasSelection() || !m_worker) return;
    static uint32_t s_copyId = 0;
    TextPos start, end;
    SelectionBounds(start, end);
    m_worker->CopyText(m_docId, ++s_copyId, start, end);
}

void PdfView::DrawSelection(HDC dc, int page, int64_t left, int64_t top, const RECT& vis) {
    TextPos start, end;
    SelectionBounds(start, end);
    if (page < start.page || page > end.page) return;
    const TextLayer* layer = GetTextLayer(page, true);
    if (!layer) return;
    const int n = (int)layer->chars.size();
    const int s = page == start.page ? std::min(start.index, n) : 0;
    const int e = page == end.page ? std::min(end.index, n) : n;
    if (e <= s) return;

    HBRUSH brush = CreateSolidBrush(kSelectionColor);
    HGDIOBJ old = SelectObject(dc, brush);
    auto flush = [&](const RectF& raw) {
        const RectF r = ToView(raw, page);
        const int64_t x0 = left + (int64_t)std::floor(r.left * m_scale);
        const int64_t y0 = top + (int64_t)std::floor(r.top * m_scale);
        const int64_t x1 = left + (int64_t)std::ceil(r.right * m_scale);
        const int64_t y1 = top + (int64_t)std::ceil(r.bottom * m_scale);
        const int ix0 = (int)std::max<int64_t>(x0, vis.left), iy0 = (int)std::max<int64_t>(y0, vis.top);
        const int ix1 = (int)std::min<int64_t>(x1, vis.right), iy1 = (int)std::min<int64_t>(y1, vis.bottom);
        if (ix1 > ix0 && iy1 > iy0)
            BitBlt(dc, ix0, iy0, ix1 - ix0, iy1 - iy0, nullptr, 0, 0, kRopDestAndPattern);
    };
    // Merge consecutive glyph boxes on the same line into one band, so
    // word gaps are highlighted too.
    bool open = false;
    RectF band;
    for (int i = s; i < e; ++i) {
        const TextChar& c = layer->chars[(size_t)i];
        if (!c.hasBox) continue;
        const RectF& b = c.box;
        if (open) {
            const float overlap = std::min(band.bottom, b.bottom) - std::max(band.top, b.top);
            const float h = std::min(band.bottom - band.top, b.bottom - b.top);
            if (overlap > h * 0.5f && b.left >= band.left - 1.0f) {
                band.left = std::min(band.left, b.left);
                band.right = std::max(band.right, b.right);
                band.top = std::min(band.top, b.top);
                band.bottom = std::max(band.bottom, b.bottom);
                continue;
            }
            flush(band);
        }
        band = b;
        open = true;
    }
    if (open) flush(band);
    SelectObject(dc, old);
    DeleteObject(brush);
}

void PdfView::ShowContextMenu(LPARAM lp) {
    if (!HasDocument()) return;
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (pt.x == -1 && pt.y == -1) GetCursorPos(&pt);  // keyboard (Shift+F10)
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (HasSelection() ? 0 : MF_GRAYED), ID_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(m, MF_STRING, ID_SELECT_ALL, L"Select &all\tCtrl+A");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COPY_PAGE_IMAGE, L"Copy &page as image");
    AppendMenuW(m, MF_STRING, ID_COPY_AREA_IMAGE, L"Copy a&rea as image");
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == ID_COPY) CopySelection();
    if (cmd == ID_SELECT_ALL) SelectAll();
    if (cmd == ID_COPY_PAGE_IMAGE) CopyPageImage();
    if (cmd == ID_COPY_AREA_IMAGE) StartAreaCopy();
}

// ===========================================================================
// Links
// ===========================================================================
const LinkInfo* PdfView::HitLink(POINT pt) {
    if (!HasDocument() || m_last < m_first) return nullptr;
    const int64_t docX = m_scrollX + pt.x, docY = m_scrollY + pt.y;
    const int page = PageAt(docX, docY);
    const PageLayout& L = m_layout[(size_t)page];
    if (docX < L.left || docX >= L.left + L.w || docY < L.top || docY >= L.top + L.h)
        return nullptr;
    const TextLayer* layer = GetTextLayer(page, true);  // links arrive with the text layer
    if (!layer) return nullptr;
    float px = (float)((docX - L.left) / m_scale), py = (float)((docY - L.top) / m_scale);
    FromView(px, py, page);
    for (const LinkInfo& link : layer->links) {
        if (px >= link.rect.left && px <= link.rect.right && py >= link.rect.top &&
            py <= link.rect.bottom)
            return &link;
    }
    return nullptr;
}

// Shows the web address of an external link before it is clicked, so a
// PDF can not hide where a link really goes.
void PdfView::UpdateLinkTip(const LinkInfo* link) {
    const std::wstring text = link && !link->target.uri.empty() ? link->target.uri : L"";
    if (text == m_linkTipText) {
        if (!text.empty()) {
            POINT pt;
            GetCursorPos(&pt);
            SendMessageW(m_linkTip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x + 16, pt.y + 20));
        }
        return;
    }
    m_linkTipText = text;
    if (!m_linkTip) {
        m_linkTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                    WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, m_hwnd,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        ti.hwnd = m_hwnd;
        ti.uId = 1;
        ti.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(m_linkTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(m_linkTip, TTM_SETMAXTIPWIDTH, 0, Dpi(480, m_dpi));
    }
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = m_hwnd;
    ti.uId = 1;
    if (text.empty()) {
        SendMessageW(m_linkTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        return;
    }
    ti.lpszText = const_cast<wchar_t*>(m_linkTipText.c_str());
    SendMessageW(m_linkTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    POINT pt;
    GetCursorPos(&pt);
    SendMessageW(m_linkTip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x + 16, pt.y + 20));
    SendMessageW(m_linkTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
    TrackMouseEvent(&tme);
}

void PdfView::FollowLink(const LinkTarget& target) {
    UpdateLinkTip(nullptr);
    if (target.page >= 0) {
        GoToTarget(target);
        return;
    }
    OpenExternalLink(m_hwnd, target.uri);  // http/https/mailto only
}

// ===========================================================================
// Page images for the clipboard
// ===========================================================================
void PdfView::CopyPageImage() {
    if (!HasDocument()) return;
    const int page = CurrentPage();
    if (page < m_first || page > m_last) return;
    const PageLayout& L = m_layout[(size_t)page];
    RequestImage(page, {0, 0, L.w, L.h});
}

void PdfView::StartAreaCopy() {
    if (!HasDocument()) return;
    ClearSelection();
    m_areaMode = true;
    SetFocus(m_hwnd);
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
}

void PdfView::RequestImage(int page, RECT r) {
    if (!m_worker) return;
    // Render at 200 DPI or the current zoom, whichever is sharper, capped
    // at about 40 megapixels. Always in normal colours (a true copy).
    const double target = std::max(200.0 / 72.0, m_scale);
    double k = target / m_scale;
    const double area = (double)(r.right - r.left) * (r.bottom - r.top);
    const double maxPixels = 40e6;
    if (area * k * k > maxPixels) k = std::sqrt(maxPixels / area);
    const PageLayout& L = m_layout[(size_t)page];
    TileRequest req;
    req.page = page;
    req.pageW = std::max(1, (int)std::lround(L.w * k));
    req.pageH = std::max(1, (int)std::lround(L.h * k));
    req.x = (int)std::lround(r.left * k);
    req.y = (int)std::lround(r.top * k);
    req.w = std::max(1, std::min(req.pageW - req.x, (int)std::lround((r.right - r.left) * k)));
    req.h = std::max(1, std::min(req.pageH - req.y, (int)std::lround((r.bottom - r.top) * k)));
    req.rotate = m_rotation;
    req.colorMode = kColorsNormal;
    m_worker->RenderImage(m_docId, req);
}
