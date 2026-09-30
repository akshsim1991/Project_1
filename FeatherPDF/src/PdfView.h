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
#include <unordered_map>
#include <unordered_set>

#include "PageCache.h"
#include "Search.h"

class RenderWorker;

enum class ZoomMode { Custom = 0, FitWidth = 1, FitPage = 2 };
enum class ViewMode { Single = 0, Continuous = 1, TwoPage = 2 };

// Zoom steps used by zoom in / zoom out (1.0 == 100 %).
extern const double kZoomPresets[];
extern const int kZoomPresetCount;

class PdfView {
public:
    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 16.0;

    PdfView() = default;
    ~PdfView() {
        if (m_hwnd && IsWindow(m_hwnd)) DestroyWindow(m_hwnd);
    }
    PdfView(const PdfView&) = delete;
    PdfView& operator=(const PdfView&) = delete;

    bool Create(HWND parent, RenderWorker* worker);
    HWND Hwnd() const { return m_hwnd; }

    // --- document ----------------------------------------------------------
    void SetDocument(uint32_t docId, std::vector<SizeF>&& sizes, int startPage);
    void CloseDocument();
    bool HasDocument() const { return !m_sizes.empty(); }
    int PageCount() const { return (int)m_sizes.size(); }
    uint32_t DocId() const { return m_docId; }
    const std::vector<SizeF>& PageSizes() const { return m_sizes; }  // unrotated
    void SetMessage(const std::wstring& text);  // shown when no document

    // --- editing -------------------------------------------------------------
    // An edit was sent to the worker under `newDocId`: keep showing the
    // current pages as placeholders until EndEdit delivers the result.
    void BeginEdit(uint32_t newDocId);
    // The edited document's page sizes; shows `focusPage` if >= 0 (and not
    // already visible), otherwise stays where the reader is.
    void EndEdit(std::vector<SizeF>&& sizes, int focusPage);

    // --- navigation --------------------------------------------------------
    int CurrentPage() const;
    void GoToPage(int page);
    void NextPage();
    void PrevPage();
    void GoToTarget(const LinkTarget& target);  // bookmark / link destination

    // --- zoom --------------------------------------------------------------
    double Zoom() const { return m_zoom; }  // 1.0 == 100 % (actual size)
    ZoomMode GetZoomMode() const { return m_mode; }
    void SetZoom(double zoom);  // switches to custom zoom, anchored at centre
    void SetZoomMode(ZoomMode mode);
    void ZoomIn();
    void ZoomOut();

    // --- view mode, rotation, page colours -----------------------------------
    ViewMode GetViewMode() const { return m_viewMode; }
    void SetViewMode(ViewMode mode);
    bool CoverPage() const { return m_cover; }  // two-page: first page alone
    void SetCoverPage(bool cover);
    int Rotation() const { return m_rotation; }  // quarter turns clockwise
    void Rotate(int quarterTurns);
    int GetPageColors() const { return m_colors; }
    void SetPageColors(int mode);  // PageColors

    // --- search highlights -------------------------------------------------
    void SetSearch(const SearchState* search) { m_search = search; }
    void ScrollToHit(const SearchHit& hit);

    // --- text selection ----------------------------------------------------
    bool HasSelection() const { return m_hasSel && !(m_selAnchor == m_selFocus); }
    bool GetSelection(TextPos& from, TextPos& to) const;  // false if none
    void CopySelection();  // text arrives as WM_APP_TEXT_COPIED
    void SelectAll();
    void ClearSelection();

    // --- page images for the clipboard (arrive as WM_APP_IMAGE_READY) ---
    void CopyPageImage();
    void StartAreaCopy();  // the next drag selects the area to copy

    // --- events from the main window -------------------------------------
    void OnTileReady(TileResult* result);      // takes ownership
    void OnTextLayer(TextLayerResult* result); // takes ownership
    void OnDpiChanged();
    void OnThemeChanged();
    void TrimMemory();  // drop all cached tiles (e.g. when minimised)

    // Called whenever the current page, page count or zoom may have changed.
    std::function<void()> onViewChanged;

private:
    struct PageLayout {
        int64_t left = 0, top = 0;         // document position of the page
        int w = 0, h = 0;                  // page size in pixels (rotated)
        int64_t rowTop = 0, rowBottom = 0; // extent of the row it sits in
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
    int PageAtY(int64_t docY) const;                // last page of the row at docY
    int PageAt(int64_t docX, int64_t docY) const;   // page nearest to a point
    int RowFirst(int page) const;                    // first page of page's row
    bool IsPaged() const { return m_viewMode == ViewMode::Single; }
    float DispW(int page) const;                     // rotated size in points
    float DispH(int page) const;
    RectF ToView(const RectF& r, int page) const;    // unrotated -> rotated points
    void FromView(float& x, float& y, int page) const;
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

    // text selection
    struct TextLayer {
        std::vector<TextChar> chars;
        std::vector<LinkInfo> links;
    };
    const LinkInfo* HitLink(POINT pt);
    void UpdateLinkTip(const LinkInfo* link);
    void FollowLink(const LinkTarget& target);
    void RequestImage(int page, RECT pagePixels);  // rect in page pixels at m_scale
    const TextLayer* GetTextLayer(int page, bool request);
    // Maps a client point to a caret position. `strict` only succeeds when
    // the point is on (or right next to) a line of text.
    bool HitText(POINT pt, bool strict, TextPos* caret, int* charIndex = nullptr);
    void SelectionBounds(TextPos& start, TextPos& end) const;
    void DrawSelection(HDC dc, int page, int64_t left, int64_t top, const RECT& vis);
    void SelectWordAt(POINT pt);
    void UpdateSelectionTo(POINT pt);
    void OnAutoScroll();
    void ShowContextMenu(LPARAM lp);

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
    ViewMode m_viewMode = ViewMode::Continuous;
    bool m_cover = true;
    int m_rotation = 0;
    int m_colors = 0;
    int m_forcedPage = -1;  // page explicitly navigated to (see CurrentPage)
    bool m_inSize = false, m_sizeDirty = false;
    bool m_editPending = false;  // between BeginEdit and EndEdit

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

    // text layers (LRU of pages' character boxes) and selection
    std::unordered_map<int, TextLayer> m_text;
    std::vector<int> m_textOrder;              // front = most recently used
    std::unordered_set<int> m_textPending;
    bool m_hasSel = false;
    bool m_selecting = false;
    TextPos m_selAnchor, m_selFocus;

    // links
    bool m_linkPressed = false;
    LinkTarget m_pressedLink;
    HWND m_linkTip = nullptr;
    std::wstring m_linkTipText;

    // "copy area as image" mode
    bool m_areaMode = false, m_areaDragging = false;
    POINT m_areaStart{}, m_areaEnd{};

    // mouse
    bool m_dragging = false;
    POINT m_dragStart{};
    int64_t m_dragScrollX = 0, m_dragScrollY = 0;
    ULONGLONG m_lastFlip = 0;
};
