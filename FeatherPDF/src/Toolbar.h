// Toolbar.h - a flat, self-drawn toolbar strip.
//
// Standard Win32 toolbars do not follow the dark theme and need image
// lists. This small custom control draws icon glyphs from the Segoe icon
// font that ships with Windows 10/11 (no image resources, crisp at any
// DPI), follows the light/dark theme, and hosts child controls such as the
// page-number and search edit boxes.
//
// Clicks are delivered to the command target as WM_COMMAND(id). WM_COMMAND
// notifications from hosted child controls are forwarded too.
#pragma once
#include "Common.h"

class Toolbar {
public:
    ~Toolbar();

    bool Create(HWND parent, HWND commandTarget, int heightDip);
    HWND Hwnd() const { return m_hwnd; }
    int Height() const;

    // Items are laid out left to right in the order they are added.
    void AddButton(int id, const wchar_t* glyph, const wchar_t* tip);
    // e.g. "Aa" toggle; `widthDip` 0: a square button
    void AddTextButton(int id, const wchar_t* text, const wchar_t* tip, int widthDip = 0);
    void AddLabel(int id, int widthDip, bool clickable, const wchar_t* tip);
    void AddChild(HWND child, int widthDip);
    void AddSeparator();
    void AddSpacer();  // flexible gap: pushes the following items right

    void SetText(int id, const std::wstring& text);
    void SetEnabled(int id, bool enabled);
    void SetChecked(int id, bool checked);
    RECT ItemScreenRect(int id) const;

    void OnDpiChanged();    // recreate fonts and relayout
    void OnThemeChanged();  // repaint with the new palette
    HFONT TextFont() const { return m_textFont; }

private:
    enum class Kind { Button, TextButton, Label, Child, Separator, Spacer };
    struct Item {
        Kind kind;
        int id = 0;
        std::wstring text;  // glyph or text
        std::wstring tip;
        int widthDip = 0;
        HWND child = nullptr;
        bool enabled = true;
        bool checked = false;
        bool clickable = true;
        RECT rc{};
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Add(Item item);
    Item* Find(int id);
    const Item* Find(int id) const;
    int HitTest(POINT pt) const;
    void Layout();
    void Paint(HDC hdc);
    void CreateFonts();
    void InvalidateItem(int index);

    HWND m_hwnd = nullptr;
    HWND m_target = nullptr;
    HWND m_tooltip = nullptr;
    int m_heightDip = 40;
    int m_dpi = 96;
    HFONT m_iconFont = nullptr;
    HFONT m_textFont = nullptr;
    std::vector<Item> m_items;
    int m_hot = -1;
    int m_pressed = -1;
    bool m_tracking = false;
};
