// PropertiesPanel.h - the Properties panel at the right of the drawing: a
// list of rows (a label and a box to change the value), filled by the main
// window for the selected objects, or for new objects when nothing is
// selected. A changed value is handed to the row's `apply`.
#pragma once
#include <functional>

#include "Common.h"

class PropertiesPanel {
public:
    struct Row {
        enum Kind { Info, Text, Choice, Check } kind = Info;
        std::wstring label;
        std::wstring value;                // Info, Text: the text shown
        std::vector<std::wstring> choices; // Choice
        int choice = -1;                   // Choice: the one chosen (-1: they differ)
        bool checked = false;              // Check
        bool heading = false;              // Info: a section heading
        std::function<void(const Row& edited)> apply;  // gets the row with the new value
    };

    ~PropertiesPanel();
    bool Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }
    int Width() const;
    void SetContent(const std::wstring& title, std::vector<Row> rows);
    bool Editing() const;  // the keyboard is in one of its boxes
    void OnThemeChanged();
    void OnDpiChanged();

private:
    struct Live {
        Row row;
        HWND label = nullptr, ctrl = nullptr;
    };
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Commit(size_t index);
    void Clear();

    HWND m_hwnd = nullptr, m_title = nullptr;
    HFONT m_font = nullptr, m_bold = nullptr;
    HBRUSH m_bg = nullptr;
    int m_dpi = 96;
    int m_scroll = 0, m_contentHeight = 0;
    std::vector<Live> m_rows;
    bool m_building = false;
};
