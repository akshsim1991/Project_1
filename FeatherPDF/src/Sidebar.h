// Sidebar.h - optional left panel showing either the document outline
// (bookmarks, a native TreeView) or page thumbnails (ThumbView).
//
// Clicks are reported to the target window as WM_APP_SIDEBAR with
// kSidebarOutlineClicked (lParam = outline index) or kSidebarPageClicked
// (lParam = page index).
#pragma once
#include "RenderTypes.h"
#include "ThumbView.h"

enum class SidebarMode { None = 0, Bookmarks = 1, Thumbnails = 2 };

class Sidebar {
public:
    ~Sidebar();
    bool Create(HWND parent, HWND target, RenderWorker* worker);
    HWND Hwnd() const { return m_hwnd; }

    void SetMode(SidebarMode mode);
    SidebarMode Mode() const { return m_mode; }

    // Content of the active tab.
    void SetOutline(const std::vector<OutlineItem>* outline);
    ThumbView& Thumbs() { return m_thumbs; }

    void OnDpiChanged();
    void OnThemeChanged();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void ApplyTreeTheme();

    HWND m_hwnd = nullptr;
    HWND m_target = nullptr;
    HWND m_tree = nullptr;
    HFONT m_font = nullptr;
    ThumbView m_thumbs;
    SidebarMode m_mode = SidebarMode::None;
    const std::vector<OutlineItem>* m_outline = nullptr;
    bool m_filling = false;  // ignore selection changes while (re)building
};
