// DataGrid.h - the editable data table (a virtual ListView with a cell
// cursor and in-place editing, like a small spreadsheet).
//
//   Arrows move, Enter or F2 or typing edits a cell, Enter/Tab commits and
//   moves on, Esc cancels, Delete clears the selected cells. The last row is
//   always empty: typing there adds a row.
#pragma once
#include <functional>

#include "Data.h"

#include <commctrl.h>

class DataGrid {
public:
    bool Create(HWND parent, Document* doc);
    HWND Hwnd() const { return m_list; }

    void Refresh(bool columnsChanged);  // after the document changed
    int CurrentRow() const;
    int CurrentCol() const { return m_col; }  // table column
    void SetCurrent(int row, int col);
    std::vector<int> SelectedRows() const;    // table rows only (not the empty one)
    void SelectRows(int first, int last);

    bool IsEditing() const { return m_edit != nullptr; }
    HWND EditHwnd() const { return m_edit; }
    void BeginEdit(const wchar_t* initial);
    void CommitEdit(int moveRow = 0, int moveCol = 0);
    void CancelEdit();

    void OnThemeChanged();
    void OnDpiChanged();
    bool OnNotify(NMHDR* hdr, LRESULT& result);

    std::function<void()> onBeforeChange;            // take an undo snapshot
    std::function<void()> onChanged;                 // cells changed
    std::function<void()> onCursorMoved;
    std::function<void(POINT screen, int col)> onHeaderMenu;  // col -1: row-number column
    std::function<void(POINT screen)> onContextMenu;

private:
    static LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT DrawHeader(NMCUSTOMDRAW* cd);
    void BuildColumns();
    void EnsureColumnVisible(int col);
    void ClearSelectedCells();
    std::wstring HeaderText(int col) const;

    HWND m_list = nullptr;
    HWND m_edit = nullptr;
    int m_editRow = -1, m_editCol = -1;
    HFONT m_font = nullptr;
    int m_dpi = 96;
    Document* m_doc = nullptr;
    int m_col = 0;
    int m_builtCols = -1;
    std::wstring m_textBuf;
};
