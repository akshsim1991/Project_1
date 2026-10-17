// TabBar.h - a flat, self-drawn tab strip (one tab per open document).
//
// Notifies its target window with WM_APP_TABBAR(wParam = TabAction,
// lParam = tab index). Follows the light/dark theme like Toolbar.
#pragma once
#include "Common.h"

enum class TabAction : WPARAM { Select = 0, Close = 1, New = 2 };

class TabBar {
public:
    struct TabInfo {
        std::wstring title;
        std::wstring tip;  // full path, shown as a tooltip
    };

    ~TabBar();
    bool Create(HWND parent, HWND target, int heightDip);
    HWND Hwnd() const { return m_hwnd; }
    int Height() const;

    void SetTabs(std::vector<TabInfo> tabs, int active);
    void OnDpiChanged();
    void OnThemeChanged();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Paint(HDC hdc);
    void CreateFonts();
    void UpdateTooltips();
    // Returns the tab index under pt (-1 none); onClose / onNew report
    // whether pt is on that tab's close button or on the "+" button.
    int HitTest(POINT pt, bool* onClose, bool* onNew) const;
    void Notify(TabAction action, int index);

    HWND m_hwnd = nullptr;
    HWND m_target = nullptr;
    HWND m_tooltip = nullptr;
    int m_heightDip = 34;
    int m_dpi = 96;
    HFONT m_textFont = nullptr;
    HFONT m_iconFont = nullptr;
    std::vector<TabInfo> m_tabs;
    std::vector<RECT> m_tabRects;
    RECT m_newRect{};
    int m_active = 0;
    int m_hot = -1;
    bool m_hotClose = false;
    bool m_hotNew = false;
    int m_pressedClose = -1;
    bool m_tracking = false;
    int m_toolCount = 0;
};
