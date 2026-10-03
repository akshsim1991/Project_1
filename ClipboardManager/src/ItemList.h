// ItemList.h - the history list: a virtual, owner-drawn list view with a
// picture thumbnail or a symbol, the start of the text, and when and where
// it was copied. Only the rows on screen are drawn, so the list stays quick
// with any number of items.
#pragma once
#include <map>
#include <memory>

#include "GdiPlusInc.h"
#include "Store.h"

#include <commctrl.h>

class ItemList {
public:
    ~ItemList();
    bool Create(HWND parent, int id);
    HWND Hwnd() const { return m_hwnd; }

    // `view` holds indices into the store's items, in display order.
    void SetItems(const Store* store, std::vector<int> view);
    const std::vector<int>& View() const { return m_view; }
    int Count() const { return (int)m_view.size(); }

    int Focused() const;                      // row with the keyboard focus (or the first selected), -1
    std::vector<int> Selected() const;        // selected rows
    void Select(const std::vector<int>& rows, int focus);
    void SelectAll();

    bool OnDrawItem(const DRAWITEMSTRUCT* dis);
    void OnSize();
    void OnDpiChanged();
    void OnThemeChanged();
    void ForgetThumbnails();

private:
    Gdiplus::Bitmap* Thumbnail(const ClipItem& it, int boxW, int boxH);
    void CreateFonts();
    void SetRowHeight();

    HWND m_hwnd = nullptr;
    const Store* m_store = nullptr;
    std::vector<int> m_view;
    int m_dpi = 96;
    HFONT m_font = nullptr, m_smallFont = nullptr, m_iconFont = nullptr, m_pinFont = nullptr;
    HIMAGELIST m_rowImages = nullptr;
    struct Thumb {
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        uint64_t used = 0;
        bool failed = false;
    };
    std::map<int64_t, Thumb> m_thumbs;  // by item id
    uint64_t m_clock = 0;
};

// One line describing an item: the start of the text, file names, or
// "Picture, 1920 × 1080".
std::wstring ItemTitle(const ClipItem& it, size_t maxChars = 300);
// "Today 10:15 · Notepad · 1,234 characters"
std::wstring ItemDetails(const ClipItem& it);
std::wstring FormatCount(uint64_t n);  // 1,234
