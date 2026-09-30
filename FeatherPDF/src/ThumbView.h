// ThumbView.h - scrolling list of page thumbnails (sidebar).
//
// Thumbnails are rendered lazily by the render worker on its own low
// priority list: only the thumbnails currently visible in the sidebar (plus
// a few below) are requested, and a small LRU cache (16 MB) keeps the most
// recently shown ones. Nothing is rendered while the panel is hidden.
//
// Pages can be selected (click, Ctrl+click, Shift+click, Ctrl+A) and
// dragged to a new position; the main window is told through
// WM_APP_SIDEBAR (kSidebarPagesMoved / kSidebarPagesMenu /
// kSidebarDeletePages) and performs the edit.
#pragma once
#include <unordered_map>

#include "RenderTypes.h"

class RenderWorker;

class ThumbView {
public:
    ~ThumbView();
    bool Create(HWND parent, HWND target, RenderWorker* worker);
    HWND Hwnd() const { return m_hwnd; }

    void SetDocument(uint32_t docId, const std::vector<SizeF>& sizes, int rotation, int colors);
    void Clear();
    // The document was edited: new id and pages, same scroll position.
    void Reload(uint32_t docId, const std::vector<SizeF>& sizes);
    void SetCurrentPage(int page);

    // Selected pages, ascending (empty: none).
    std::vector<int> Selection() const;
    void SetSelection(const std::vector<int>& pages);
    void OnThumbReady(TileResult* result);  // takes ownership
    void TrimMemory();
    void OnDpiChanged();

private:
    struct Item {
        int top = 0;
        int w = 0, h = 0;  // thumbnail size in pixels
    };
    struct Entry {
        PixelBuffer pixels;
        uint64_t lastUse = 0;
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Paint(HDC hdc);
    void ScrollTo(int y);
    int ItemAt(int y) const;
    int GapAt(int y) const;  // drop position (insert before this page)
    void OnButtonDown(POINT pt, bool right);
    void OnDragMove(POINT pt);
    void OnButtonUp();
    void Select(int item, bool ctrl, bool shift);
    bool DropIsNoOp(int gap) const;
    void EvictIfNeeded();
    void ClearCache();

    HWND m_hwnd = nullptr;
    HWND m_target = nullptr;
    RenderWorker* m_worker = nullptr;
    int m_dpi = 96;
    HFONT m_font = nullptr;

    uint32_t m_docId = 0;
    std::vector<SizeF> m_sizes;  // unrotated points
    int m_rotation = 0, m_colors = 0;
    std::vector<Item> m_items;
    int m_totalH = 0, m_scroll = 0, m_labelH = 16;
    int m_current = -1;

    // selection and drag-to-reorder
    std::vector<char> m_selected;  // per page
    int m_anchor = -1;             // Shift+click range start
    bool m_pressed = false, m_dragging = false;
    POINT m_pressPt{};
    int m_pressItem = -1;
    bool m_selectOnUp = false;     // plain click on a selected page (maybe a drag)
    int m_gap = -1;                // current drop position while dragging

    std::unordered_map<int, Entry> m_cache;
    size_t m_bytes = 0;
    uint64_t m_clock = 0;
};
