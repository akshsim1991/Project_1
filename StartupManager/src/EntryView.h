// EntryView.h - the service table: a virtual ("owner data") ListView, so
// only visible rows are ever formatted, with sorting, search, quick
// filters, coloured status/safety columns, chooseable columns and a
// selection that survives automatic refreshes.
#pragma once
#include <functional>

#include "Entries.h"
#include "Settings.h"

#include <commctrl.h>

class EntryView {
public:
    bool Create(HWND parent, Settings* settings);
    HWND Hwnd() const { return m_list; }

    // New data from the worker. Selection, focus and scroll position are
    // kept (matched by StartupEntry::Id).
    void SetData(std::vector<StartupEntry>&& entries);
    void Reapply();  // filters or display settings changed
    void SetFilter(int filter);
    void SetSearch(const std::wstring& text);
    void SortBy(int column);  // same column again: reverse the order

    const std::vector<StartupEntry>& All() const { return m_all; }
    std::vector<const StartupEntry*> Selected() const;
    const StartupEntry* Focused() const;
    int ShownCount() const { return (int)m_view.size(); }
    void SelectAll();

    void ToggleColumn(int column);
    void ResetColumns();
    void SaveColumns();  // widths and order -> settings
    void OnThemeChanged();
    void OnDpiChanged();

    // WM_NOTIFY from the list (forwarded by the parent). Returns true and
    // sets `result` if handled.
    bool OnNotify(NMHDR* hdr, LRESULT& result);

    std::function<void()> onSelectionChanged;
    std::function<void(POINT screen)> onContextMenu;
    std::function<void()> onActivate;  // double-click or Enter

    static const wchar_t* ColumnTitle(int column);
    static std::wstring CellText(const StartupEntry& s, int column);

private:
    static LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void BuildColumns();
    void ApplyView();  // filter + sort into m_view
    void UpdateSortArrow();
    void ApplyTheme();
    void ShowColumnMenu(POINT screen);
    LRESULT DrawHeader(NMCUSTOMDRAW* cd);
    bool Matches(const StartupEntry& s) const;
    int ColumnAt(int subItem) const {
        return subItem >= 0 && subItem < (int)m_cols.size() ? m_cols[(size_t)subItem] : -1;
    }

    HWND m_list = nullptr;
    HFONT m_font = nullptr;
    int m_dpi = 96;
    Settings* m_settings = nullptr;
    std::vector<StartupEntry> m_all;
    std::vector<int> m_view;      // indices into m_all, filtered and sorted
    std::vector<int> m_cols;      // list sub-item -> Column
    std::wstring m_searchLower;
    std::wstring m_textBuf;       // LVN_GETDISPINFO text storage
    bool m_updating = false;      // suppress selection callbacks while rebuilding
};
