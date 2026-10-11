// StatusBar.h - the bar at the bottom: cursor coordinates on the left,
// drafting toggles (GRID, SNAP, ORTHO, POLAR, OSNAP with its E, M, I and C
// snaps, OTRACK, DYN) and the units on the right.
// Clicking a toggle sends WM_COMMAND with its id; right-clicking one opens
// its settings.
#pragma once
#include "Common.h"

class StatusBar {
public:
    StatusBar();
    ~StatusBar();
    bool Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }
    int Height() const;

    void SetCoordinates(const std::wstring& text);
    void SetInfo(const std::wstring& text);  // middle: selection count, snap...
    // GRID, SNAP, ORTHO, POLAR, OSNAP, E, M, I, C, OTRACK, DYN
    void SetToggles(const std::vector<bool>& states);
    void SetUnits(const std::wstring& text);
    void OnThemeChanged();
    void OnDpiChanged();

private:
    struct Button {
        int id, rightId;
        std::wstring text, tip;
        bool on = false;
        RECT rc{};
    };
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Paint(HDC hdc);
    int HitTest(POINT pt) const;

    HWND m_hwnd = nullptr, m_tooltip = nullptr;
    HFONT m_font = nullptr, m_bold = nullptr;
    int m_dpi = 96;
    std::wstring m_coords, m_info;
    std::vector<Button> m_buttons;
    int m_hot = -1;
    bool m_tracking = false;
};
