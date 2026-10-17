// AlertBox.h - the low/critical battery message window ("Also show a
// message box" in Settings). Unlike a standard message box it does not
// block the program, is updated instead of stacking up, and closes by
// itself when the charger is plugged in.
#pragma once
#include "Common.h"

class AlertBox {
public:
    ~AlertBox();
    void Show(HWND owner, const std::wstring& title, const std::wstring& text, bool critical);
    void Close();
    bool IsOpen() const { return m_hwnd != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void CreateFonts();

    HWND m_hwnd = nullptr;
    HWND m_ok = nullptr;
    HFONT m_font = nullptr, m_bold = nullptr;
    std::wstring m_title, m_text;
    bool m_critical = false;
    int m_dpi = 96;
};
