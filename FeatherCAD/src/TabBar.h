// TabBar.h - the strip of drawing tabs above the drawing area: one tab per
// open drawing (with a * when it has unsaved changes), a close button on
// each tab and a + button for a new drawing.
//
// Clicks go to the command target as WM_COMMAND: ID_TAB_FIRST + index to
// switch, ID_TAB_CLOSE_FIRST + index to close (also a middle click on the
// tab), ID_NEW for the + button.
#pragma once
#include "Common.h"

class TabBar {
public:
    struct Tab {
        std::wstring title;
        std::wstring tip;  // full path
        bool modified = false;
        bool operator==(const Tab& o) const { return title == o.title && tip == o.tip && modified == o.modified; }
    };

    // The commands a tab bar sends (the drawing tabs' by default).
    struct Ids {
        int tabFirst = ID_TAB_FIRST;         // + index: switch to it
        int closeFirst = ID_TAB_CLOSE_FIRST; // + index: close it (0: tabs have no close button)
        int plus = ID_NEW;                   // the + button
        int menuFirst = 0;                   // + index: right click on a tab (0: none)
        const wchar_t* plusTip = L"New drawing (Ctrl+N)";
        bool bottom = false;                 // under the drawing (the edge and marker below)
    };

    ~TabBar();
    bool Create(HWND parent, HWND commandTarget);  // drawing tabs
    bool Create(HWND parent, HWND commandTarget, const Ids& ids);
    HWND Hwnd() const { return m_hwnd; }
    int Height() const;
    void SetTabs(const std::vector<Tab>& tabs, int active);  // repaints only if something changed
    void OnDpiChanged();
    void OnThemeChanged();

private:
    enum Part { None, TabBody, TabClose, Plus };
    struct Hit {
        Part part = None;
        int index = -1;
        bool operator==(const Hit& o) const { return part == o.part && index == o.index; }
    };
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Paint(HDC dc);
    Hit HitTest(POINT pt) const;
    RECT CloseRect(const RECT& tab) const;

    HWND m_hwnd = nullptr, m_target = nullptr, m_tooltip = nullptr;
    Ids m_ids;
    HFONT m_font = nullptr;
    int m_dpi = 96;
    std::vector<Tab> m_tabs;
    std::vector<RECT> m_rects;
    RECT m_plus{};
    int m_active = 0;
    Hit m_hot, m_pressed;
    bool m_tracking = false;
};
