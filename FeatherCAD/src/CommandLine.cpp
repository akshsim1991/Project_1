// CommandLine.cpp - see CommandLine.h.
#include "CommandLine.h"

#include <algorithm>
#include <cwctype>

#include <commctrl.h>

#include "Engine.h"
#include "Theme.h"
#include "Util.h"
#include "Document.h"

namespace {
const wchar_t kClass[] = L"FeatherCadCommandLine";
constexpr int kHistoryLines = 3, kExpandedLines = 12, kMaxHistoryLines = 400;
}  // namespace

CommandLine::~CommandLine() {
    if (m_font) DeleteObject(m_font);
    if (m_bgBrush) DeleteObject(m_bgBrush);
}

bool CommandLine::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &CommandLine::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    m_history = CreateWindowExW(0, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                0, 0, 0, 0, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    m_input = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hwnd, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(m_input, &CommandLine::EditProc, 1, (DWORD_PTR)this);
    SetWindowSubclass(m_history, &CommandLine::HistoryProc, 2, (DWORD_PTR)this);
    OnDpiChanged();
    OnThemeChanged();
    return true;
}

int CommandLine::LineHeight() const {
    HDC hdc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(hdc, m_font);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);
    return tm.tmHeight + tm.tmExternalLeading;
}

int CommandLine::Height() const {
    const int lines = m_expanded ? kExpandedLines : kHistoryLines;
    return LineHeight() * (lines + 1) + Dpi(16, m_dpi);
}

int CommandLine::PromptWidth() const {
    HDC hdc = GetDC(m_hwnd);
    HGDIOBJ old = SelectObject(hdc, m_font);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, m_prompt.c_str(), (int)m_prompt.size(), &sz);
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);
    return sz.cx;
}

void CommandLine::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int lh = LineHeight(), pad = Dpi(6, m_dpi);
    const int inputTop = rc.bottom - lh - pad;
    MoveWindow(m_history, pad, Dpi(3, m_dpi), rc.right - 2 * pad, inputTop - Dpi(6, m_dpi), TRUE);
    // The prompt is painted before the input; long prompts leave at least a third for typing.
    const int pw = std::min(PromptWidth() + Dpi(6, m_dpi), (int)(rc.right * 2 / 3));
    MoveWindow(m_input, pad + pw, inputTop, std::max(10, (int)rc.right - 2 * pad - pw), lh, TRUE);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void CommandLine::AddLine(const std::wstring& text) {
    if (!m_historyText.empty()) m_historyText += L"\r\n";
    m_historyText += text;
    if (++m_historyLines > kMaxHistoryLines) {
        const size_t cut = m_historyText.find(L"\r\n");
        if (cut != std::wstring::npos) m_historyText.erase(0, cut + 2);
        --m_historyLines;
    }
    SetWindowTextW(m_history, m_historyText.c_str());
    const int len = GetWindowTextLengthW(m_history);
    SendMessageW(m_history, EM_SETSEL, len, len);
    SendMessageW(m_history, EM_SCROLLCARET, 0, 0);
}

void CommandLine::SetPrompt(const std::wstring& prompt, bool idle) {
    m_idle = idle;
    if (prompt == m_prompt) return;
    m_prompt = prompt;
    Layout();
}

void CommandLine::Focus() {
    if (GetFocus() != m_input) SetFocus(m_input);
}

void CommandLine::ToggleExpanded() { m_expanded = !m_expanded; }

void CommandLine::OnThemeChanged() {
    if (m_bgBrush) DeleteObject(m_bgBrush);
    m_bgBrush = CreateSolidBrush(CurrentTheme().editBg);
    ApplyScrollbarTheme(m_history);
    InvalidateRect(m_hwnd, nullptr, TRUE);
    InvalidateRect(m_history, nullptr, TRUE);
    InvalidateRect(m_input, nullptr, TRUE);
}

void CommandLine::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    if (m_font) DeleteObject(m_font);
    m_font = CreateMessageFont(m_dpi);
    SendMessageW(m_history, WM_SETFONT, (WPARAM)m_font, FALSE);
    SendMessageW(m_input, WM_SETFONT, (WPARAM)m_font, FALSE);
    Layout();
}

std::wstring CommandLine::InputText() const {
    std::wstring text((size_t)GetWindowTextLengthW(m_input) + 1, L'\0');
    text.resize((size_t)GetWindowTextW(m_input, text.data(), (int)text.size()));
    return text;
}

void CommandLine::Send() {
    std::wstring text = InputText();
    SetWindowTextW(m_input, L"");
    if (!text.empty()) {
        if (m_typed.empty() || m_typed.back() != text) m_typed.push_back(text);
        if (m_typed.size() > 100) m_typed.erase(m_typed.begin());
    }
    m_typedPos = (int)m_typed.size();
    SendMessageW(GetParent(m_hwnd), WM_APP_COMMAND_TEXT, 0, (LPARAM)&text);
}

LRESULT CALLBACK CommandLine::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CommandLine* self;
    if (msg == WM_NCCREATE) {
        self = (CommandLine*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (CommandLine*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CommandLine::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            FillRect((HDC)wp, &rc, m_bgBrush);
            RECT top = {0, 0, rc.right, 1};
            HBRUSH b = CreateSolidBrush(CurrentTheme().barBorder);
            FillRect((HDC)wp, &top, b);
            DeleteObject(b);
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            const int lh = LineHeight(), pad = Dpi(6, m_dpi);
            RECT pr = {pad, rc.bottom - lh - pad, rc.right * 2 / 3 + pad, rc.bottom - pad};
            // A line between the history and the input.
            RECT sep = {pad, pr.top - Dpi(4, m_dpi), rc.right - pad, pr.top - Dpi(3, m_dpi)};
            HBRUSH b = CreateSolidBrush(CurrentTheme().barBorder);
            FillRect(hdc, &sep, b);
            DeleteObject(b);
            HGDIOBJ old = SelectObject(hdc, m_font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, m_idle ? CurrentTheme().barText : CurrentTheme().accent);
            DrawTextW(hdc, m_prompt.c_str(), (int)m_prompt.size(), &pr,
                      DT_LEFT | DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(hdc, old);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            Focus();
            return 0;
        case WM_COMMAND:
            if ((HWND)lp == m_input && HIWORD(wp) == EN_CHANGE) {
                const std::wstring text = InputText();
                SendMessageW(GetParent(m_hwnd), WM_APP_COMMAND_TYPING, 0, (LPARAM)&text);
            }
            return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const Theme& th = CurrentTheme();
            HDC dc = (HDC)wp;
            SetTextColor(dc, (HWND)lp == m_history ? th.canvasText : th.editText);
            SetBkColor(dc, th.editBg);
            return (LRESULT)m_bgBrush;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// The history can be selected and copied (Ctrl+C); typing there goes back
// to the input line.
LRESULT CALLBACK CommandLine::HistoryProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = (CommandLine*)ref;
    if ((msg == WM_CHAR && (wp >= L' ' || wp == L'\r')) || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) {
        self->Focus();
        return SendMessageW(self->m_input, msg, wp, lp);
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) return 0;  // its WM_CHAR is forwarded
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, &CommandLine::HistoryProc, 2);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK CommandLine::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = (CommandLine*)ref;
    HWND parent = GetParent(self->m_hwnd);
    switch (msg) {
        case WM_KEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0;
            const bool empty = GetWindowTextLengthW(hwnd) == 0;
            switch (wp) {
                case VK_RETURN:
                    self->Send();
                    return 0;
                case VK_ESCAPE:
                    SetWindowTextW(hwnd, L"");
                    SendMessageW(parent, WM_COMMAND, ID_ESCAPE, 0);
                    return 0;
                case VK_UP:
                case VK_DOWN:
                    if (self->m_typed.empty()) return 0;
                    self->m_typedPos = std::clamp(self->m_typedPos + (wp == VK_UP ? -1 : 1), 0,
                                                  (int)self->m_typed.size());
                    SetWindowTextW(hwnd, self->m_typedPos < (int)self->m_typed.size()
                                             ? self->m_typed[(size_t)self->m_typedPos].c_str()
                                             : L"");
                    SendMessageW(hwnd, EM_SETSEL, 0x7fffffff, 0x7fffffff);
                    return 0;
                case VK_TAB: {
                    if (!self->m_idle) {
                        // A length typed at a point prompt: Tab moves on to the
                        // angle, as "@length<angle" (relative polar).
                        std::wstring t = self->InputText();
                        double v;
                        if (!t.empty() && t[0] != L'@' && t.find(L'<') == std::wstring::npos &&
                            ParseNumber(WideToUtf8(t), v)) {
                            t = L"@" + t + L"<";
                            SetWindowTextW(hwnd, t.c_str());
                            SendMessageW(hwnd, EM_SETSEL, 0x7fffffff, 0x7fffffff);
                        }
                        return 0;
                    }
                    // Complete a command name.
                    std::wstring t = self->InputText();
                    for (wchar_t& c : t) c = (wchar_t)towupper(c);
                    if (t.empty()) return 0;
                    for (const CommandInfo& c : AllCommands()) {
                        if (c.name.compare(0, t.size(), t) == 0) {
                            SetWindowTextW(hwnd, c.name.c_str());
                            SendMessageW(hwnd, EM_SETSEL, 0x7fffffff, 0x7fffffff);
                            break;
                        }
                    }
                    return 0;
                }
                case VK_DELETE:
                    if (empty) {
                        SendMessageW(parent, WM_APP_COMMAND_KEY, VK_DELETE, 0);
                        return 0;
                    }
                    break;
                case 'A':
                    if (ctrl && empty) {
                        SendMessageW(parent, WM_APP_COMMAND_KEY, 'A', 0);
                        return 0;
                    }
                    break;
            }
            break;
        }
        case WM_SYSKEYDOWN:
            if (wp == VK_F10) {  // F10 would open the window menu: polar tracking instead
                SendMessageW(parent, WM_COMMAND, ID_TOGGLE_POLAR, 0);
                return 0;
            }
            break;
        case WM_CHAR:
            // Space is Enter, as in other CAD programs (except where text is asked
            // for). Tab and Enter are handled above.
            if (wp == L' ' && self->m_spaces) break;
            if (wp == L' ' || wp == L'\r' || wp == L'\t' || wp == 27) {
                if (wp == L' ') self->Send();
                return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            // Wheel over the drawing zooms it even though the focus is here.
            POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
            HWND under = WindowFromPoint(pt);
            if (under && under != hwnd) return SendMessageW(under, msg, wp, lp);
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &CommandLine::EditProc, 1);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void CommandLine::ClearInput() {
    if (m_input) SetWindowTextW(m_input, L"");
}
