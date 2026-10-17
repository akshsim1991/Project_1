// Dashboard.h - the window's content: a large battery gauge with the
// percentage, state and advice; "Battery" and "This session" cards; and a
// graph of the battery level over time (hover it for exact values).
#pragma once
#include "History.h"
#include "Summary.h"

class Dashboard {
public:
    bool Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }
    void SetData(const Summary& s, const History* history, int graphHours, int lowPercent, int fullPercent,
                 bool lowLine, bool fullLine);
    void OnThemeChanged() { InvalidateRect(m_hwnd, nullptr, FALSE); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HDC hdc);

    HWND m_hwnd = nullptr;
    Summary m_summary;
    const History* m_history = nullptr;
    int m_hours = 24, m_low = 20, m_full = 99;
    bool m_lowLine = true, m_fullLine = true;
    int m_dpi = 96;
    // graph geometry from the last paint, for hovering
    RECT m_plot{};
    int64_t m_t0 = 0, m_t1 = 1;
    int m_hoverX = -1;
};
