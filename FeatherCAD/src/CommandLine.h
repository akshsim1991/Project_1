// CommandLine.h - the command line at the bottom of the window: a few
// lines of history above an input line that starts with the current
// prompt ("Command:", "Specify next point or [Close/Undo]:").
//
// The input line keeps the keyboard focus, so typing anywhere goes there,
// as in other CAD programs. Enter or Space sends the text (WM_APP_COMMAND_TEXT
// to the parent); Esc cancels (WM_COMMAND ID_ESCAPE); Up and Down recall
// earlier input; Tab completes a command name.
#pragma once
#include "Common.h"

class CommandLine {
public:
    ~CommandLine();
    bool Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }
    HWND Input() const { return m_input; }  // the edit box
    int Height() const;

    void AddLine(const std::wstring& text);
    void SetPrompt(const std::wstring& prompt, bool idle);
    void SetSpaces(bool on) { m_spaces = on; }  // space types a space instead of acting as Enter
    void Focus();
    void ClearInput();  // what was typed and not yet sent goes
    void ToggleExpanded();  // F2: more history lines
    void OnThemeChanged();
    void OnDpiChanged();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK HistoryProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Send();
    int LineHeight() const;
    int PromptWidth() const;
    std::wstring InputText() const;

    HWND m_hwnd = nullptr, m_history = nullptr, m_input = nullptr;
    HFONT m_font = nullptr;
    int m_dpi = 96;
    bool m_expanded = false;
    bool m_idle = true;
    bool m_spaces = false;
    std::wstring m_prompt = L"Command:";
    std::wstring m_historyText;
    int m_historyLines = 0;
    std::vector<std::wstring> m_typed;  // earlier input, for Up/Down
    int m_typedPos = 0;
    HBRUSH m_bgBrush = nullptr;
};
