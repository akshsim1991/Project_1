// CommandPalette.cpp - the Ctrl+K command box.
#include "CommandPalette.h"

#include <algorithm>
#include <cwctype>

#include <commctrl.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClass[] = L"FeatherCommandPalette";
constexpr int kEditId = 1, kListId = 2;

std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}

std::vector<std::wstring> Words(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring w;
    for (wchar_t c : s) {
        if (iswspace(c)) {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

bool AllDigits(const std::wstring& s) {
    if (s.empty() || s.size() > 7) return false;
    for (wchar_t c : s)
        if (!iswdigit(c)) return false;
    return true;
}

// "find bearing" / "search bearing" -> "bearing"
bool AfterPrefix(const std::wstring& text, const std::wstring& lower, std::initializer_list<const wchar_t*> prefixes,
                 std::wstring& rest) {
    for (const wchar_t* p : prefixes) {
        const size_t n = wcslen(p);
        if (lower.size() > n && lower.compare(0, n, p) == 0) {
            rest = text.substr(n);
            while (!rest.empty() && iswspace(rest.front())) rest.erase(rest.begin());
            return !rest.empty();
        }
    }
    return false;
}
}  // namespace

CommandPalette::~CommandPalette() { Close(); }

void CommandPalette::Close() {
    if (m_hwnd) {
        HWND h = m_hwnd;
        m_hwnd = nullptr;
        DestroyWindow(h);
    }
    if (m_font) DeleteObject(m_font);
    if (m_small) DeleteObject(m_small);
    m_font = m_small = nullptr;
}

void CommandPalette::Show(HWND owner, std::vector<PaletteCommand> commands,
                          std::function<void(int, const std::wstring&)> run) {
    Close();
    m_commands = std::move(commands);
    m_run = std::move(run);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &CommandPalette::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        wc.style = CS_DROPSHADOW;
        registered = RegisterClassExW(&wc) != 0;
    }
    m_dpi = GetWindowDpi(owner);
    m_font = CreateMessageFont(m_dpi, 115);
    m_small = CreateMessageFont(m_dpi, 95);
    RECT orc;
    GetWindowRect(owner, &orc);
    const int w = std::min<int>(Dpi(560, m_dpi), orc.right - orc.left - Dpi(40, m_dpi));
    const int rowH = Dpi(30, m_dpi), editH = Dpi(36, m_dpi), rows = 11, pad = Dpi(8, m_dpi);
    const int h = pad * 3 + editH + rowH * rows;
    const int x = orc.left + (orc.right - orc.left - w) / 2, y = orc.top + Dpi(70, m_dpi);
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"", WS_POPUP | WS_BORDER, x, y, w, h, owner, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return;
    m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, pad, pad, w - 2 * pad - 2,
                             editH, m_hwnd, (HMENU)(INT_PTR)kEditId, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(m_edit, WM_SETFONT, (WPARAM)m_font, FALSE);
    SendMessageW(m_edit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"Type a command, a page number, \x201C" L"find \x2026\x201D or \x201Czoom 150\x201D");
    SetWindowSubclass(m_edit, &CommandPalette::EditProc, 1, (DWORD_PTR)this);
    m_list = CreateWindowExW(0, L"LISTBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
                                 LBS_NOINTEGRALHEIGHT,
                             pad, pad * 2 + editH, w - 2 * pad - 2, rowH * rows, m_hwnd, (HMENU)(INT_PTR)kListId,
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(m_list, LB_SETITEMHEIGHT, 0, rowH);
    ApplyScrollbarTheme(m_list);
    Filter();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_edit);
}

void CommandPalette::Filter() {
    std::wstring text((size_t)GetWindowTextLengthW(m_edit) + 1, L'\0');
    text.resize((size_t)GetWindowTextW(m_edit, text.data(), (int)text.size()));
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
    const std::wstring lower = Lower(text);
    m_shown.clear();

    // Page numbers, zoom levels and searches first.
    std::wstring rest;
    if (AllDigits(text)) m_shown.push_back({kPaletteGoTo, L"Go to page " + text, L"Ctrl+G", text});
    if (AfterPrefix(text, lower, {L"go to page ", L"goto page ", L"page ", L"go to "}, rest) && AllDigits(rest))
        m_shown.push_back({kPaletteGoTo, L"Go to page " + rest, L"Ctrl+G", rest});
    if (AfterPrefix(text, lower, {L"zoom to ", L"zoom "}, rest)) {
        if (!rest.empty() && rest.back() == L'%') rest.pop_back();
        if (AllDigits(rest)) m_shown.push_back({kPaletteZoom, L"Zoom to " + rest + L"%", L"", rest});
    }
    if (text.size() > 1 && text.back() == L'%' && AllDigits(text.substr(0, text.size() - 1)))
        m_shown.push_back({kPaletteZoom, L"Zoom to " + text, L"", text.substr(0, text.size() - 1)});
    std::wstring find;
    const bool explicitFind = AfterPrefix(text, lower, {L"find all ", L"find ", L"search for ", L"search "}, find);
    if (explicitFind) m_shown.push_back({kPaletteFind, L"Find \x201C" + find + L"\x201D in the document", L"Ctrl+F", find});

    // Commands whose name or keywords contain every word typed.
    const std::vector<std::wstring> words = Words(lower);
    for (const PaletteCommand& c : m_commands) {
        if (!c.enabled) continue;
        const std::wstring hay = Lower(c.name + L" " + c.keywords);
        bool all = true;
        for (const std::wstring& w : words)
            if (hay.find(w) == std::wstring::npos) all = false;
        if (all) m_shown.push_back({c.id, c.name, c.keys, L""});
    }
    if (!text.empty() && !explicitFind && !AllDigits(text))
        m_shown.push_back({kPaletteFind, L"Find \x201C" + text + L"\x201D in the document", L"Ctrl+F", text});

    SendMessageW(m_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < m_shown.size(); ++i) SendMessageW(m_list, LB_ADDSTRING, 0, (LPARAM)i);
    SendMessageW(m_list, LB_SETCURSEL, 0, 0);
    SendMessageW(m_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_list, nullptr, TRUE);
}

void CommandPalette::Run(int index) {
    if (index < 0 || index >= (int)m_shown.size()) return;
    const Entry e = m_shown[(size_t)index];
    auto run = m_run;
    HWND owner = GetWindow(m_hwnd, GW_OWNER);
    Close();
    if (owner) SetForegroundWindow(owner);
    if (run) run(e.id, e.arg);
}

LRESULT CALLBACK CommandPalette::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CommandPalette* self;
    if (msg == WM_NCCREATE) {
        self = (CommandPalette*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (CommandPalette*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CommandPalette::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    HWND hwnd = m_hwnd;
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == kEditId && HIWORD(wp) == EN_CHANGE) Filter();
            if (LOWORD(wp) == kListId && HIWORD(wp) == LBN_DBLCLK)
                Run((int)SendMessageW(m_list, LB_GETCURSEL, 0, 0));
            return 0;
        case WM_ACTIVATE:
            // Clicking elsewhere closes it.
            if (LOWORD(wp) == WA_INACTIVE && m_hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case WM_CLOSE:
            Close();
            return 0;
        case WM_CTLCOLOREDIT: {
            const Theme& t = CurrentTheme();
            SetTextColor((HDC)wp, t.editText);
            SetBkColor((HDC)wp, t.editBg);
            return (LRESULT)t.editBrush;
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(CurrentTheme().barBg);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_DRAWITEM: {
            const auto* di = (const DRAWITEMSTRUCT*)lp;
            if (di->itemID == (UINT)-1 || di->itemID >= m_shown.size()) return TRUE;
            const Theme& t = CurrentTheme();
            const Entry& e = m_shown[di->itemID];
            const bool sel = (di->itemState & ODS_SELECTED) != 0;
            HBRUSH bg = CreateSolidBrush(sel ? t.barPressed : t.barBg);
            FillRect(di->hDC, &di->rcItem, bg);
            DeleteObject(bg);
            SetBkMode(di->hDC, TRANSPARENT);
            RECT r = di->rcItem;
            r.left += Dpi(10, m_dpi);
            r.right -= Dpi(10, m_dpi);
            HGDIOBJ old = SelectObject(di->hDC, m_small);
            SetTextColor(di->hDC, t.barTextDisabled);
            DrawTextW(di->hDC, e.keys.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(di->hDC, m_font);
            SetTextColor(di->hDC, e.id < 0 ? t.accent : t.barText);
            r.right -= Dpi(120, m_dpi);
            DrawTextW(di->hDC, e.name.c_str(), -1, &r,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(di->hDC, old);
            return TRUE;
        }
        case WM_CTLCOLORLISTBOX: {
            // The empty part below the rows is cleared with the bar colour.
            static HBRUSH brush = nullptr;
            static COLORREF color = CLR_INVALID;
            if (color != CurrentTheme().barBg) {
                if (brush) DeleteObject(brush);
                color = CurrentTheme().barBg;
                brush = CreateSolidBrush(color);
            }
            return (LRESULT)brush;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK CommandPalette::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = (CommandPalette*)ref;
    if (msg == WM_KEYDOWN) {
        const int count = (int)SendMessageW(self->m_list, LB_GETCOUNT, 0, 0);
        int sel = (int)SendMessageW(self->m_list, LB_GETCURSEL, 0, 0);
        switch (wp) {
            case VK_DOWN:
            case VK_UP:
            case VK_NEXT:
            case VK_PRIOR: {
                const int step = wp == VK_DOWN ? 1 : wp == VK_UP ? -1 : wp == VK_NEXT ? 8 : -8;
                if (count > 0) SendMessageW(self->m_list, LB_SETCURSEL, std::clamp(sel + step, 0, count - 1), 0);
                return 0;
            }
            case VK_RETURN:
                self->Run(sel);
                return 0;
            case VK_ESCAPE:
                PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
                return 0;
        }
    }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, &CommandPalette::EditProc, 1);
    return DefSubclassProc(hwnd, msg, wp, lp);
}
