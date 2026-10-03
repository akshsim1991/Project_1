// Preview.h - the pane beside the list: a heading with the details of the
// selected item, and its whole text (selectable, to copy a part) or its
// picture, fitted to the pane.
#pragma once
#include <memory>

#include "GdiPlusInc.h"
#include "Store.h"

class Preview {
public:
    ~Preview();
    bool Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }

    // `item` nullptr: nothing selected; `selectedCount` > 1 shows a summary.
    void Show(const Store& store, const ClipItem* item, int selectedCount, const std::wstring& emptyText);
    void Clear() { m_id = 0; }  // the next Show reloads
    HWND TextBox() const { return m_edit; }

    void OnDpiChanged();
    void OnThemeChanged();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Paint(HDC dc);
    void CreateFonts();
    int HeaderHeight() const;

    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    int m_dpi = 96;
    HFONT m_font = nullptr, m_bold = nullptr, m_textFont = nullptr;
    int64_t m_id = 0;
    bool m_pinned = false;
    std::wstring m_heading, m_details, m_message;
    std::unique_ptr<Gdiplus::Bitmap> m_image, m_scaled;
};
