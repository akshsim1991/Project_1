// GuideBar.h - the strip above the drawing that says, in plain words, what
// to do next, with buttons for the choices a command offers (Close, Undo...),
// Done and Cancel; with objects selected, buttons for what can be done with
// them. On its right: colour swatches and the line thickness, for the
// selected objects or for new ones.
//
// Clicks are delivered to the command target as WM_COMMAND(id).
#pragma once
#include <string>
#include <vector>

#include "Common.h"

class GuideBar {
public:
    struct Button {
        int id = 0;
        std::wstring text, tip;
        int style = 0;  // 0 plain, 1 main (Done), 2 cancel, 3 drop-down
    };
    struct Swatch {
        int id = 0;
        COLORREF color = 0;
        std::wstring tip;
    };

    ~GuideBar();
    bool Create(HWND parent, HWND commandTarget);
    HWND Hwnd() const { return m_hwnd; }
    int Height() const;

    // What to do now: a short title ("LINE"), the instruction and buttons;
    // `active` (a command running) gives the strip a colour of its own.
    void SetGuide(const std::wstring& title, const std::wstring& text, std::vector<Button> buttons, bool active);
    // The colour swatches (the one in use marked) and the thickness button.
    void SetStyle(std::vector<Swatch> swatches, int chosenId, int moreId, int weightId, const std::wstring& weightText);
    RECT ItemScreenRect(int id) const;

    void OnDpiChanged();
    void OnThemeChanged();

private:
    enum class Kind { Title, Text, Button, Label, Swatch };
    struct Item {
        Kind kind;
        int id = 0;
        std::wstring text, tip;
        int style = 0;
        COLORREF color = 0;
        bool chosen = false;
        RECT rc{};
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Rebuild();
    void Paint(HDC hdc);
    int HitTest(POINT pt) const;
    int TextWidth(HFONT font, const std::wstring& s) const;
    void CreateFonts();

    HWND m_hwnd = nullptr;
    HWND m_target = nullptr;
    HWND m_tooltip = nullptr;
    int m_dpi = 96;
    HFONT m_font = nullptr, m_bold = nullptr;
    std::wstring m_title, m_text;
    bool m_active = false;
    std::vector<Button> m_buttons;
    std::vector<Swatch> m_swatches;
    int m_chosen = 0, m_moreId = 0, m_weightId = 0;
    std::wstring m_weightText;
    std::vector<Item> m_items;
    int m_hot = -1, m_pressed = -1;
    bool m_tracking = false;
};
