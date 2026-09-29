// PdfView.h - the page canvas: layout, scrolling, zoom, navigation,
// tile requests and painting.
//
// RENDERING PIPELINE
// ------------------
//   PdfView::Paint
//     1. finds the visible pages (binary search over page offsets, so it is
//        O(log n) even for 10,000-page documents);
//     2. draws cached tiles for them (and stretched lower/higher resolution
//        tiles as placeholders while a zoom change is being re-rendered);
//     3. sends the list of missing visible tiles, followed by a budgeted
//        prefetch band below/above the viewport, to the RenderWorker, which
//        replaces its previous list;
//   RenderWorker renders tiles one by one and posts them back;
//   PdfView::OnTileReady inserts them into the PageCache and invalidates
//   just that tile's rectangle.
//
// LAYOUT
// ------
// All page geometry is integer pixels at the current scale. Page offsets
// are 64-bit so that very long documents at high zoom can not overflow;
// scrollbar positions are divided down if the document is taller than the
// 32-bit scrollbar range.
#pragma once
#include <functional>
#include <unordered_set>

#include "PageCache.h"
#include "Search.h"

class RenderWorker;

enum class ZoomMode { Custom = 0, FitWidth = 1, FitPage = 2 };

// Zoom steps used by zoom in / zoom out (1.0 == 100 %).
extern const double kZoomPresets[];
extern const int kZoomPresetCount;

class PdfView {
public:
    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 16.0;

    bool Create(HWND parent, RenderWorker* worker);
    HWND Hwnd() const { return m_hwnd; }

    // --- document ----------------------------------------------------------
    void SetDocument(uint32_t docId, std::vector<SizeF>&& sizes, int startPage);
    void CloseDocument();
    bool HasDocument() const { return !m_sizes.empty(); }
    int PageCount() const { return (int)m_sizes.size(); }
    void SetMessage(const std::wstring& text);  // shown when no document

    // --- navigation --------------------------------------------------------
    int CurrentPage() const;
    void GoToPage(int page);
    void NextPage();
    void PrevPage();

    // --- zoom --------------------------------------------------------------
    double Zoom() const { return m_zoom; }  // 1.0 == 100 % (actual size)
    ZoomMode GetZoomMode() const { return m_mode; }
    void SetZoom(double zoom);  // switches to custom zoom, anchored at centre
    void SetZoomMode(ZoomMode mode);
    void ZoomIn();
    void ZoomOut();

    // --- view mode ---------------------------------------------------------
    bool Continuous() const { return m_continuous; }
    void SetContinuous(bool continuous);

    // --- search highlights -------------------------------------------------
    void SetSearch(const SearchState* search) { m_search = search; }
    void ScrollToHit(const SearchHit& hit);

    // --- events from the main window -------------------------------------
    void OnTileReady(TileResult* result);  // takes ownership
    void OnDpiChanged();
    void OnThemeChanged();
    void TrimMemory();  // drop all cached tiles (e.g. when minimised)

    // Called whenever the current page, page count or zoom may have changed.
    std::function<void()> onViewChanged;

private:
    struct PageLayout {
        int64_t top = 0;  // document y of the page's top edge
        int w = 0, h = 0; // page size in pixels at the current scale
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // layout & scrolling
    void Relayout();
    void OnSize();
    void UpdateScrollBars();
    void ScrollTo(int64_t x, int64_t y);
    void ScrollBy(int64_t dx, int64_t dy) { ScrollTo(m_scrollX + dx, m_scrollY + dy); }
    void ScrollOrFlip(int64_t dy);  // single-page mode flips at the edges
    void OnScrollBar(int bar, int code);
    int64_t MaxScrollX() const;
    int64_t MaxScrollY() const;
    int64_t PageLeft(int page) const;  // document x of the page's left edge
    void CenterHorizontally(int page);
    int PageAtY(int64_t docY) const;
    void VisibleRange(int64_t y0, int64_t y1, int& first, int& last) const;
    int ClientW() const { return m_clientW; }
    int ClientH() const { return m_clientH; }
    int LineStep() const;

    // zoom
    void ApplyZoom(double zoom, POINT anchor, bool settle);
    double FitZoom(ZoomMode mode, int page) const;
    void UpdateScale();
    void UpdateCacheBudget();

    // painting
    void Paint(HDC hdc);
    void PaintPage(HDC dc, int page, std::vector<std::pair<int64_t, TileRequest>>& missing,
                   size_t& visibleBytes);
    void RequestTiles(std::vector<std::pair<int64_t, TileRequest>>& missing, size_t visibleBytes);
    void AddPrefetch(int64_t y0, int64_t y1, size_t& budgetLeft, std::vector<TileRequest>& out);
    int TileW(int pageW) const;
    void InvalidateTile(const TileRequest& r);
    void Notify() {
        if (onViewChanged) onViewChanged();
    }

    HWND m_hwnd = nullptr;
    RenderWorker* m_worker = nullptr;
    int m_dpi = 96;
    int m_clientW = 0, m_clientH = 0;

    // document
    uint32_t m_docId = 0;
    std::vector<SizeF> m_sizes;  // in points
    std::wstring m_message;

    // layout
    std::vector<PageLayout> m_layout;
    int m_first = 0, m_last = -1;  // laid-out page range (single page mode: one page)
    int m_singlePage = 0;
    int64_t m_docW = 0, m_docH = 0;
    int64_t m_scrollX = 0, m_scrollY = 0;
    int64_t m_sbUnit = 1;  // pixels per scrollbar unit
    int m_margin = 8;
    bool m_continuous = true;
    int m_forcedPage = -1;  // page explicitly navigated to (see CurrentPage)
    bool m_inSize = false, m_sizeDirty = false;

    // zoom
    ZoomMode m_mode = ZoomMode::FitWidth;
    double m_zoom = 1.0;
    int m_scaleKey = 1000;  // pixels per point * 1000 (quantised)
    double m_scale = 1.0;   // m_scaleKey / 1000.0
    bool m_zoomSettling = false;

    // rendering
    PageCache m_cache;
    std::unordered_set<TileKey, TileKeyHash> m_failed;  // allocation failures
    const SearchState* m_search = nullptr;
    HDC m_backDC = nullptr;
    HBITMAP m_backBmp = nullptr;
    int m_backW = 0, m_backH = 0;
    HFONT m_messageFont = nullptr;

    // mouse
    bool m_dragging = false;
    POINT m_dragStart{};
    int64_t m_dragScrollX = 0, m_dragScrollY = 0;
    ULONGLONG m_lastFlip = 0;
};
