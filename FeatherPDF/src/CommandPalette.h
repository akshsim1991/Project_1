// CommandPalette.h - Ctrl+K: type what you want to do ("rotate", "dark",
// "page 72", "find bearing", "zoom 150") and press Enter.
#pragma once
#include <functional>

#include "Common.h"

struct PaletteCommand {
    int id = 0;
    std::wstring name;      // shown
    std::wstring keys;      // shortcut, shown on the right
    std::wstring keywords;  // other words that find it
    bool enabled = true;
};

// Results that are not menu commands (`arg` holds the number or text).
enum : int { kPaletteGoTo = -1, kPaletteZoom = -2, kPaletteFind = -3 };

class CommandPalette {
public:
    ~CommandPalette();
    void Show(HWND owner, std::vector<PaletteCommand> commands,
              std::function<void(int id, const std::wstring& arg)> run);
    void Close();
    bool IsOpen() const { return m_hwnd != nullptr; }

private:
    struct Entry {
        int id;
        std::wstring name, keys, arg;
    };
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Filter();
    void Run(int index);

    HWND m_hwnd = nullptr, m_edit = nullptr, m_list = nullptr;
    HFONT m_font = nullptr, m_small = nullptr;
    int m_dpi = 96;
    std::vector<PaletteCommand> m_commands;
    std::vector<Entry> m_shown;
    std::function<void(int, const std::wstring&)> m_run;
};
