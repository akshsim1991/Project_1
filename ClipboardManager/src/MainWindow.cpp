// MainWindow.cpp - window, recording, list, actions, tray and shortcut.
#include "MainWindow.h"

#include <algorithm>
#include <ctime>
#include <cwctype>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "ClipboardIO.h"
#include "Dialogs.h"
#include "Images.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr UINT_PTR kCaptureTimer = 1;  // shortly after the clipboard changed
constexpr UINT_PTR kSearchTimer = 2;   // after typing in the search box
constexpr UINT_PTR kPasteTimer = 3;    // Ctrl+V once the other program is in front
constexpr UINT_PTR kCleanupTimer = 4;  // deleting old items (hourly)
constexpr UINT_PTR kFlashTimer = 5;    // status bar messages
constexpr UINT_PTR kPreviewTimer = 6;  // selection changed
constexpr UINT_PTR kClockTimer = 7;    // "Today 10:15" labels
constexpr int kHotkeyId = 1;
constexpr int kSplitterDip = 6;
constexpr int kRecentInTray = 10;

MainWindow* g_main = nullptr;

const wchar_t* FilterName(int f) {
    switch (f) {
        case kFilterText: return L"Text";
        case kFilterImages: return L"Pictures";
        case kFilterFiles: return L"Files";
        case kFilterPinned: return L"Pinned";
        case kFilterToday: return L"Copied today";
        default: return L"Everything";
    }
}

int64_t LocalMidnightMs() {
    const time_t now = time(nullptr);
    tm t{};
    localtime_s(&t, &now);
    t.tm_hour = t.tm_min = t.tm_sec = 0;
    return (int64_t)mktime(&t) * 1000;
}

std::vector<std::wstring> SplitWords(const std::wstring& s) {
    std::vector<std::wstring> words;
    std::wstring w;
    for (wchar_t c : s) {
        if (iswspace(c)) {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) words.push_back(w);
    return words;
}

std::vector<std::wstring> FileLines(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t e = s.find(L'\n', pos);
        if (e == std::wstring::npos) e = s.size();
        std::wstring l = s.substr(pos, e - pos);
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        if (!l.empty()) out.push_back(l);
        pos = e + 1;
    }
    return out;
}

// Windows that are not where the user wants to paste.
bool IsShellWindow(HWND w) {
    wchar_t cls[64] = L"";
    GetClassNameW(w, cls, 64);
    for (const wchar_t* c : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow",
                             L"TopLevelWindowForOverflowXamlIsland", L"Windows.UI.Core.CoreWindow",
                             L"XamlExplorerHostIslandWindow", L"ForegroundStaging", L"MultitaskingViewFrame",
                             L"TaskSwitcherWnd", L"#32768", L"Progman", L"WorkerW"})
        if (lstrcmpW(cls, c) == 0) return true;
    return false;
}
}  // namespace

// ===========================================================================
// Creation
// ===========================================================================
bool MainWindow::Create(HINSTANCE inst, bool startHidden) {
    m_inst = inst;
    g_main = this;
    ReloadTheme((ThemeMode)m_settings.themeMode);
    m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    m_smallIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    wc.hIconSm = m_smallIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = APP_WINDOW_CLASS;
    if (!RegisterClassExW(&wc)) return false;

    const int dpi = GetWindowDpi(nullptr);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = std::min(Dpi(1040, dpi), (int)(work.right - work.left) * 9 / 10);
    const int h = std::min(Dpi(700, dpi), (int)(work.bottom - work.top) * 9 / 10);
    m_hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, APP_WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);

    m_toolbar.Create(m_hwnd, m_hwnd, 44);
    m_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10,
                               m_hwnd, (HMENU)(INT_PTR)ID_SEARCH, inst, nullptr);
    SendMessageW(m_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search everything you copied  (Ctrl+F)");
    m_toolbar.AddChild(m_search, -200);
    m_toolbar.AddTextButton(ID_FILTER_MENU, L"", L"Show only text, pictures, files, pinned or today's items", 132);
    m_toolbar.AddSeparator();
    m_toolbar.AddTextButton(ID_PASTE, L"Paste", L"Paste into the program you were using (Enter)", 58);
    m_toolbar.AddTextButton(ID_COPY, L"Copy", L"Put it on the clipboard (Ctrl+C)", 52);
    m_toolbar.AddTextButton(ID_PIN, L"Pin", L"Pinned items stay at hand and are never deleted automatically (Ctrl+P)", 56);
    m_toolbar.AddTextButton(ID_DELETE, L"Delete", L"Delete the selected items and their files (Del)", 60);
    m_toolbar.AddSeparator();
    m_toolbar.AddTextButton(ID_OPEN_FOLDER, L"Open folder", L"Open the \x201C" APP_FOLDER_NAME L"\x201D folder", 90);
    m_toolbar.AddTextButton(ID_MORE_MENU, L"More", L"Save as, pause recording, settings, About", 52);

    m_list.Create(m_hwnd, 1);
    m_preview.Create(m_hwnd);

    m_status.Create(m_hwnd, m_hwnd, 28);
    m_status.SetBorderTop(true);
    m_status.AddLabel(ID_STATUS_TEXT, -100, false, nullptr);
    m_status.AddLabel(ID_STATUS_PAUSE, 170, true, L"Click to pause or resume recording");
    m_status.SetRightAligned(ID_STATUS_PAUSE, true);

    if (m_settings.hasPlacement) {
        WINDOWPLACEMENT wp = m_settings.placement;
        wp.flags = 0;
        wp.showCmd = SW_HIDE;
        SetWindowPlacement(m_hwnd, &wp);
    }
    ApplyWindowTheme(m_hwnd);
    SetFilter(m_settings.filter);

    if (!m_store.Open(m_storeNote))
        MessageBoxW(nullptr, (m_storeNote + L"\n\nWhat you copy can not be kept until this is solved.").c_str(), APP_NAME,
                    MB_ICONWARNING);
    Cleanup();
    Refresh(true);

    m_tray.Add(m_hwnd, WM_APP_TRAY, m_smallIcon);
    AddClipboardFormatListener(m_hwnd);
    // What is on the clipboard now is kept too.
    SetTimer(m_hwnd, kCaptureTimer, 300, nullptr);
    m_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                       &MainWindow::ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
    m_lastApp = GetForegroundWindow();
    RegisterShortcut(!startHidden);
    SetTimer(m_hwnd, kCleanupTimer, 60 * 60 * 1000, nullptr);
    SetTimer(m_hwnd, kClockTimer, 60 * 1000, nullptr);
    UpdateStatus();

    if (!startHidden) {
        ShowWindow(m_hwnd, m_settings.hasPlacement && m_settings.placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED
                                                                                                       : SW_SHOWNORMAL);
        UpdateWindow(m_hwnd);
        SetFocus(m_search);
        AskAutostartOnce();
    }
    if (!m_storeNote.empty()) Flash(m_storeNote);
    return true;
}

void MainWindow::AskAutostartOnce() {
    if (m_settings.askedAutostart) return;
    m_settings.askedAutostart = true;
    m_settings.Save();
    if (AutostartEnabled()) return;
    if (MessageBoxW(m_hwnd,
                    L"Start " APP_NAME L" with Windows?\n\nIt can only keep what you copy while it is running. It then "
                    L"waits quietly in the notification area.",
                    APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES)
        SetAutostart(true);
}

void MainWindow::RegisterShortcut(bool complain) {
    if (m_hotkeyOn) UnregisterHotKey(m_hwnd, kHotkeyId);
    m_hotkeyOn = false;
    UINT mods = 0, vk = 0;
    if (!HotkeyKeys(m_settings.hotkey, mods, vk)) return;
    m_hotkeyOn = RegisterHotKey(m_hwnd, kHotkeyId, mods | MOD_NOREPEAT, vk) != 0;
    if (!m_hotkeyOn && complain)
        MessageBoxW(m_hwnd,
                    (std::wstring(HotkeyName(m_settings.hotkey)) +
                     L" is already used by another program, so it can not open " APP_NAME
                     L".\n\nChoose another shortcut in More \x203A Settings.")
                        .c_str(),
                    APP_NAME, MB_ICONINFORMATION);
}

RECT MainWindow::SplitterRect() const {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int top = m_toolbar.Height(), bottom = rc.bottom - m_status.Height();
    const int gap = Dpi(kSplitterDip, m_dpi);
    const int maxList = std::max<int>(Dpi(200, m_dpi), rc.right - Dpi(220, m_dpi) - gap);
    const int listW = std::clamp(Dpi(m_settings.listWidthDip, m_dpi), Dpi(200, m_dpi), maxList);
    return RECT{listW, top, listW + gap, bottom};
}

void MainWindow::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int th = m_toolbar.Height(), sh = m_status.Height();
    MoveWindow(m_toolbar.Hwnd(), 0, 0, rc.right, th, TRUE);
    MoveWindow(m_status.Hwnd(), 0, rc.bottom - sh, rc.right, sh, TRUE);
    const RECT split = SplitterRect();
    const int h = std::max(0, (int)(rc.bottom - th - sh));
    MoveWindow(m_list.Hwnd(), 0, th, split.left, h, TRUE);
    MoveWindow(m_preview.Hwnd(), split.right, th, std::max(0, (int)(rc.right - split.right)), h, TRUE);
    m_list.OnSize();
    InvalidateRect(m_hwnd, &split, FALSE);
}

// ===========================================================================
// Recording
// ===========================================================================
void MainWindow::CaptureClipboard() {
    const DWORD seq = GetClipboardSequenceNumber();
    if (seq == m_lastSequence) return;
    // Our own copies (an item copied back) are already in the history.
    if (GetClipboardOwner() == m_hwnd) {
        m_lastSequence = seq;
        return;
    }
    if (m_paused) {
        m_lastSequence = seq;
        return;
    }
    Capture cap;
    const auto result = ClipboardIO::Read(m_hwnd, m_settings.recordImages, m_settings.recordFiles, cap);
    if (result == ClipboardIO::ReadResult::Busy) {
        // Another program has it open: try again a little later.
        if (++m_captureTries <= 10) SetTimer(m_hwnd, kCaptureTimer, 150, nullptr);
        return;
    }
    m_captureTries = 0;
    m_lastSequence = seq;
    if (cap.excluded || cap.kind == Capture::None) return;

    int64_t id = 0;
    switch (cap.kind) {
        case Capture::Text: id = m_store.AddText(cap.text, cap.source, false); break;
        case Capture::Files: id = m_store.AddText(cap.text, cap.source, true); break;
        case Capture::Image: id = m_store.AddImage(cap.png, cap.pixelHash, cap.width, cap.height, cap.source); break;
        default: break;
    }
    if (!id) {
        Flash(L"What you copied could not be saved in " + m_store.Folder() + L" (is the disk full?).");
        return;
    }
    Refresh();
}

void MainWindow::Cleanup() {
    if (m_settings.deleteAfterDays <= 0) return;
    if (m_store.RemoveUnpinnedOlderThan(m_settings.deleteAfterDays) > 0 && m_list.Hwnd()) Refresh();
}

// ===========================================================================
// The list
// ===========================================================================
void MainWindow::Refresh(bool selectFirst) {
    std::vector<int64_t> keep = selectFirst ? std::vector<int64_t>() : SelectedIds();
    int64_t focusId = 0;
    if (!selectFirst) {
        const int r = m_list.Focused();
        if (r >= 0 && r < (int)m_viewIds.size()) focusId = m_viewIds[(size_t)r];
    }
    // Items copied again have a new id (the time): follow them.
    for (const auto& [from, to] : m_renamed) {
        std::replace(keep.begin(), keep.end(), from, to);
        if (focusId == from) focusId = to;
    }
    m_renamed.clear();

    const std::vector<std::wstring> words = SplitWords(GetWindowString(m_search));
    const int64_t midnight = LocalMidnightMs();
    const auto& items = m_store.Items();
    std::vector<int> view;
    view.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const ClipItem& it = items[i];
        switch (m_settings.filter) {
            case kFilterText:
                if (it.kind != ClipKind::Text) continue;
                break;
            case kFilterImages:
                if (it.kind != ClipKind::Image) continue;
                break;
            case kFilterFiles:
                if (it.kind != ClipKind::Files) continue;
                break;
            case kFilterPinned:
                if (!it.pinned) continue;
                break;
            case kFilterToday:
                if (it.id < midnight) continue;
                break;
        }
        bool match = true;
        for (const std::wstring& w : words) {
            if (StrStrIW(it.text.c_str(), w.c_str()) || StrStrIW(it.source.c_str(), w.c_str()) ||
                StrStrIW(it.file.c_str(), w.c_str()))
                continue;
            if (it.kind == ClipKind::Image && (StrStrIW(L"picture image screenshot", w.c_str()))) continue;
            match = false;
            break;
        }
        if (match) view.push_back((int)i);
    }
    m_viewIds.clear();
    for (int i : view) m_viewIds.push_back(items[(size_t)i].id);
    m_list.SetItems(&m_store, std::move(view));

    // Keep the selection on the same items where possible.
    std::vector<int> rows;
    int focus = -1;
    const auto& v = m_list.View();
    for (int r = 0; r < (int)v.size(); ++r) {
        const int64_t id = m_viewIds[(size_t)r];
        if (std::find(keep.begin(), keep.end(), id) != keep.end()) rows.push_back(r);
        if (id == focusId) focus = r;
    }
    if (rows.empty() && !v.empty()) rows.push_back(0);
    if (focus < 0 && !rows.empty()) focus = rows.front();
    m_list.Select(rows, focus);
    if (!rows.empty() && rows.front() == 0) ListView_EnsureVisible(m_list.Hwnd(), 0, FALSE);
    m_preview.Clear();
    UpdatePreview();
    UpdateStatus();
}

// By id: the store's order changes when an item is copied again, before
// the list is refreshed.
std::vector<int64_t> MainWindow::SelectedIds() const {
    std::vector<int64_t> ids;
    for (int r : m_list.Selected())
        if (r >= 0 && r < (int)m_viewIds.size()) ids.push_back(m_viewIds[(size_t)r]);
    return ids;
}

const ClipItem* MainWindow::FocusedItem() const {
    const int r = m_list.Focused();
    if (r < 0 || r >= (int)m_viewIds.size()) return nullptr;
    const int i = m_store.IndexOf(m_viewIds[(size_t)r]);
    return i < 0 ? nullptr : &m_store.Items()[(size_t)i];
}

void MainWindow::UpdatePreview() {
    const std::vector<int> sel = m_list.Selected();
    std::wstring empty;
    if (m_store.Items().empty())
        empty = L"Copy something \x2014 text, a picture or files \x2014 and it appears here.\n\n"
                L"Everything is kept, with no limit, in the \x201C" APP_FOLDER_NAME L"\x201D folder next to the "
                L"program.\n\n" +
                std::wstring(m_settings.hotkey != kHotkeyNone ? HotkeyName(m_settings.hotkey) : L"The tray icon") +
                L" opens this window from any program. Pick an item and press Enter to paste it.";
    else if (m_list.Count() == 0)
        empty = L"Nothing matches.\n\nTry other words, or choose \x201C" + std::wstring(FilterName(kFilterAll)) +
                L"\x201D in the filter.";
    else
        empty = L"Select an item to see all of it.";
    m_preview.Show(m_store, sel.size() == 1 ? FocusedItem() : nullptr, (int)sel.size(), empty);

    // Buttons follow the selection.
    const bool any = !sel.empty();
    m_toolbar.SetEnabled(ID_PASTE, any);
    m_toolbar.SetEnabled(ID_COPY, any);
    m_toolbar.SetEnabled(ID_PIN, any);
    m_toolbar.SetEnabled(ID_DELETE, any);
    bool allPinned = any;
    for (int64_t id : SelectedIds()) {
        const int i = m_store.IndexOf(id);
        if (i >= 0 && !m_store.Items()[(size_t)i].pinned) allPinned = false;
    }
    m_toolbar.SetText(ID_PIN, allPinned ? L"Unpin" : L"Pin");
}

void MainWindow::UpdateStatus() {
    const auto& items = m_store.Items();
    std::wstring text;
    if (!m_flash.empty()) {
        text = m_flash;
    } else {
        text = FormatCount(items.size()) + (items.size() == 1 ? L" item" : L" items") + L"  \x00B7  " +
               FormatSize(m_store.TotalBytes());
        if (m_list.Count() != (int)items.size()) text += L"  \x00B7  " + FormatCount((uint64_t)m_list.Count()) + L" shown";
        const std::wstring app = m_lastApp && IsWindow(m_lastApp) ? ClipboardIO::ProgramName(m_lastApp) : L"";
        if (m_settings.pasteOnEnter && !app.empty() && app != APP_NAME) text += L"  \x00B7  Enter pastes into " + app;
        else if (m_settings.hotkey != kHotkeyNone && m_hotkeyOn)
            text += std::wstring(L"  \x00B7  ") + HotkeyName(m_settings.hotkey) + L" opens this window";
    }
    m_status.SetText(ID_STATUS_TEXT, text);
    m_status.SetText(ID_STATUS_PAUSE, m_paused ? L"Paused \x2014 click to resume" : L"Recording (click to pause)");

    std::wstring tip = APP_NAME L"\n" + FormatCount(items.size()) + (items.size() == 1 ? L" item" : L" items");
    if (m_paused) tip += L" \x2014 paused";
    m_tray.SetTip(tip);
}

void MainWindow::Flash(const std::wstring& text) {
    m_flash = text;
    SetTimer(m_hwnd, kFlashTimer, text.size() > 60 ? 9000 : 4000, nullptr);
    UpdateStatus();
}

// ===========================================================================
// Window procedure
// ===========================================================================
LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = (MainWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

void CALLBACK MainWindow::ForegroundChanged(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    // Remember the last program used, to paste into it.
    if (!g_main || !hwnd || idObject != OBJID_WINDOW) return;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root || root == g_main->m_hwnd || !IsWindowVisible(root) || IsShellWindow(root)) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    if (pid == GetCurrentProcessId()) return;
    g_main->m_lastApp = root;
    if (IsWindowVisible(g_main->m_hwnd)) g_main->UpdateStatus();
}

bool MainWindow::PreTranslate(MSG& msg) {
    if (!m_hwnd || (msg.hwnd != m_hwnd && !IsChild(m_hwnd, msg.hwnd))) return false;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (msg.message == WM_KEYDOWN && ctrl && !alt) {
        switch (msg.wParam) {
            case 'F':
            case 'E':
                SetFocus(m_search);
                SendMessageW(m_search, EM_SETSEL, 0, -1);
                return true;
            case 'P':
                if (msg.hwnd != m_preview.TextBox()) {
                    TogglePin();
                    return true;
                }
                break;
        }
    }
    // Enter pastes, Esc clears the search or hides the window (in the search
    // box and the list; the text box keeps its own keys).
    if (msg.message == WM_KEYDOWN && (msg.hwnd == m_search || msg.hwnd == m_list.Hwnd()) && !alt &&
        (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
        OnCommand(msg.wParam == VK_RETURN ? IDOK : IDCANCEL);
        return true;
    }
    // Typing in the list searches.
    if (msg.message == WM_CHAR && msg.hwnd == m_list.Hwnd() && msg.wParam >= 0x20 && !ctrl) {
        SetFocus(m_search);
        SendMessageW(m_search, EM_SETSEL, (WPARAM)-1, -1);
        SendMessageW(m_search, WM_CHAR, msg.wParam, msg.lParam);
        return true;
    }
    // Arrow keys in the search box move through the list.
    if (msg.message == WM_KEYDOWN && msg.hwnd == m_search &&
        (msg.wParam == VK_DOWN || msg.wParam == VK_UP || msg.wParam == VK_NEXT || msg.wParam == VK_PRIOR)) {
        SendMessageW(m_list.Hwnd(), WM_KEYDOWN, msg.wParam, msg.lParam);
        return true;
    }
    return false;
}

LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == m_taskbarCreated && m_taskbarCreated) {
        m_tray.Readd();
        return 0;
    }
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == ID_SEARCH) {
                if (HIWORD(wp) == EN_CHANGE) SetTimer(m_hwnd, kSearchTimer, 120, nullptr);
                return 0;
            }
            OnCommand(LOWORD(wp));
            return 0;
        case WM_NOTIFY: return OnNotify((NMHDR*)lp);
        case WM_DRAWITEM:
            if (m_list.OnDrawItem((const DRAWITEMSTRUCT*)lp)) return TRUE;
            break;
        case WM_CLIPBOARDUPDATE:
            m_captureTries = 0;
            SetTimer(m_hwnd, kCaptureTimer, 100, nullptr);
            return 0;
        case WM_HOTKEY:
            if (wp == kHotkeyId) {
                if (GetForegroundWindow() == m_hwnd && IsWindowVisible(m_hwnd)) HideToTray();
                else ShowForPaste();
            }
            return 0;
        case WM_TIMER:
            switch (wp) {
                case kCaptureTimer:
                    KillTimer(m_hwnd, kCaptureTimer);
                    CaptureClipboard();
                    break;
                case kSearchTimer:
                    KillTimer(m_hwnd, kSearchTimer);
                    Refresh(true);
                    break;
                case kPreviewTimer:
                    KillTimer(m_hwnd, kPreviewTimer);
                    UpdatePreview();
                    break;
                case kPasteTimer:
                    if (GetForegroundWindow() == m_pasteTarget || ++m_pasteTries > 8) {
                        KillTimer(m_hwnd, kPasteTimer);
                        if (GetForegroundWindow() == m_pasteTarget) ClipboardIO::SendPaste();
                        m_pasteTarget = nullptr;
                    } else {
                        SetForegroundWindow(m_pasteTarget);
                    }
                    break;
                case kCleanupTimer: Cleanup(); break;
                case kFlashTimer:
                    KillTimer(m_hwnd, kFlashTimer);
                    m_flash.clear();
                    UpdateStatus();
                    break;
                case kClockTimer:
                    if (IsWindowVisible(m_hwnd)) InvalidateRect(m_list.Hwnd(), nullptr, FALSE);
                    break;
            }
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                // Windows sends both WM_LBUTTONUP and NIN_SELECT for one click,
                // so a click only ever opens (never toggles) the window.
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case NIN_BALLOONUSERCLICK: ShowWindowFromTray(); break;
                case WM_CONTEXTMENU:
                case WM_RBUTTONUP: ShowTrayMenu(); break;
            }
            return 0;
        case WM_APP_SHOW:
            ShowWindowFromTray();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) UpdateStatus();
            break;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lp)->ptMinTrackSize = {Dpi(640, m_dpi), Dpi(400, m_dpi)};
            return 0;
        case WM_DPICHANGED: {
            m_dpi = HIWORD(wp);
            m_toolbar.OnDpiChanged();
            m_status.OnDpiChanged();
            m_list.OnDpiChanged();
            m_preview.OnDpiChanged();
            const RECT* r = (const RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            // Only the splitter is not covered by the child windows.
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(m_hwnd, &ps);
            HBRUSH b = CreateSolidBrush(CurrentTheme().barBorder);
            FillRect(dc, &ps.rcPaint, b);
            DeleteObject(b);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR:
            if ((HWND)wp == m_hwnd && LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);
                const RECT s = SplitterRect();
                if (PtInRect(&s, pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN: {
            const POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
            const RECT s = SplitterRect();
            if (PtInRect(&s, pt)) {
                m_splitting = true;
                SetCapture(m_hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (m_splitting) {
                const int x = (short)LOWORD(lp);
                m_settings.listWidthDip = std::clamp(MulDiv(x, 96, m_dpi), 200, 2000);
                Layout();
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (m_splitting) {
                m_splitting = false;
                if (msg == WM_LBUTTONUP) ReleaseCapture();
                m_settings.listWidthDip = MulDiv(SplitterRect().left, 96, m_dpi);
                m_settings.Save();
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && lstrcmpW((const wchar_t*)lp, L"ImmersiveColorSet") == 0 && m_settings.themeMode == 0) ApplyTheme();
            break;
        case WM_CLOSE:
            if (m_settings.closeToTray && !m_quitting) {
                HideToTray();
                if (!m_settings.toldAboutTray) {
                    m_settings.toldAboutTray = true;
                    m_settings.Save();
                    m_tray.Notify(APP_NAME L" is still recording",
                                  L"It keeps what you copy from here. Right-click the icon to pause or exit.");
                }
                return 0;
            }
            {
                WINDOWPLACEMENT place{sizeof(place)};
                if (GetWindowPlacement(m_hwnd, &place)) {
                    if (place.showCmd != SW_SHOWMAXIMIZED) place.showCmd = SW_SHOWNORMAL;
                    m_settings.placement = place;
                    m_settings.hasPlacement = true;
                }
                m_settings.Save();
            }
            DestroyWindow(m_hwnd);
            return 0;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (wp) m_settings.Save();
            return 0;
        case WM_DESTROY:
            RemoveClipboardFormatListener(m_hwnd);
            if (m_hotkeyOn) UnregisterHotKey(m_hwnd, kHotkeyId);
            if (m_foregroundHook) UnhookWinEvent(m_foregroundHook);
            m_foregroundHook = nullptr;
            for (UINT_PTR t : {kCaptureTimer, kSearchTimer, kPasteTimer, kCleanupTimer, kFlashTimer, kPreviewTimer,
                               kClockTimer})
                KillTimer(m_hwnd, t);
            m_tray.Remove();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

LRESULT MainWindow::OnNotify(NMHDR* nm) {
    if (nm->hwndFrom != m_list.Hwnd()) return 0;
    switch (nm->code) {
        case LVN_ITEMCHANGED: {
            const auto* lv = (const NMLISTVIEW*)nm;
            if (lv->uChanged & LVIF_STATE) SetTimer(m_hwnd, kPreviewTimer, 30, nullptr);
            return 0;
        }
        case LVN_ODSTATECHANGED: SetTimer(m_hwnd, kPreviewTimer, 30, nullptr); return 0;
        case NM_DBLCLK: {
            const auto* ia = (const NMITEMACTIVATE*)nm;
            if (ia->iItem >= 0) Activate();
            return 0;
        }
        case NM_RCLICK: {
            POINT pt;
            GetCursorPos(&pt);
            ShowContextMenu(pt);
            return 0;
        }
        case LVN_KEYDOWN: {
            const auto* kd = (const NMLVKEYDOWN*)nm;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (kd->wVKey == VK_DELETE) DeleteSelected();
            else if (ctrl && kd->wVKey == 'C') CopySelected(false);
            else if (ctrl && kd->wVKey == 'A') m_list.SelectAll();
            else if (kd->wVKey == VK_APPS || (kd->wVKey == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000))) {
                RECT r{};
                const int f = m_list.Focused();
                if (f >= 0) ListView_GetItemRect(m_list.Hwnd(), f, &r, LVIR_BOUNDS);
                POINT pt{r.left + Dpi(40, m_dpi), r.bottom};
                ClientToScreen(m_list.Hwnd(), &pt);
                ShowContextMenu(pt);
            }
            return 0;
        }
        case NM_SETFOCUS:
        case NM_KILLFOCUS: InvalidateRect(m_list.Hwnd(), nullptr, FALSE); return 0;
    }
    return 0;
}

// ===========================================================================
// Actions
// ===========================================================================
bool MainWindow::CopyItem(int64_t id) {
    const int i = m_store.IndexOf(id);
    if (i < 0) return false;
    const ClipItem& it = m_store.Items()[(size_t)i];
    bool ok = false;
    if (it.kind == ClipKind::Image) {
        std::string png;
        ok = ReadFileBytes(m_store.PathOf(it), png) && ClipboardIO::WritePng(m_hwnd, png);
    } else {
        std::wstring text;
        if (m_store.FullText(it, text))
            ok = it.kind == ClipKind::Files ? ClipboardIO::WriteFiles(m_hwnd, FileLines(text))
                                            : ClipboardIO::WriteText(m_hwnd, text);
    }
    if (!ok) {
        Flash(L"It could not be put on the clipboard. The file may have been deleted, or another program is "
              L"using the clipboard.");
        return false;
    }
    m_lastSequence = GetClipboardSequenceNumber();
    const int64_t moved = m_store.Touch(id);  // used again: to the top
    if (moved != id) m_renamed.push_back({id, moved});
    return true;
}

bool MainWindow::CopySelected(bool plainText) {
    std::vector<int64_t> ids = SelectedIds();
    if (ids.empty()) return false;
    bool ok;
    if (ids.size() == 1 && !plainText) {
        ok = CopyItem(ids.front());
    } else {
        // Several items (or file names as text): their text, one per line.
        std::wstring all;
        int skipped = 0;
        for (int64_t id : ids) {
            const int i = m_store.IndexOf(id);
            if (i < 0) continue;
            const ClipItem& it = m_store.Items()[(size_t)i];
            std::wstring t;
            if (it.kind == ClipKind::Image) {
                ++skipped;
                continue;
            }
            if (!m_store.FullText(it, t)) continue;
            if (it.kind == ClipKind::Files) {
                std::wstring lines;
                for (const std::wstring& l : FileLines(t)) lines += (lines.empty() ? L"" : L"\r\n") + l;
                t = lines;
            }
            if (!all.empty()) all += L"\r\n";
            all += t;
        }
        if (all.empty()) {
            if (ids.size() == 1 || skipped) {
                // Only pictures: the clipboard holds one picture.
                ok = CopyItem(ids.front());
                if (ok && ids.size() > 1) Flash(L"The clipboard holds one picture at a time: the first one was copied.");
                Refresh();
                return ok;
            }
            return false;
        }
        ok = ClipboardIO::WriteText(m_hwnd, all);
        if (ok) m_lastSequence = GetClipboardSequenceNumber();
        if (ok && skipped) Flash(L"Copied the text. Pictures can not be joined with text, so they were left out.");
    }
    if (ok) {
        if (m_flash.empty()) Flash(L"Copied. Press Ctrl+V to paste it anywhere.");
        Refresh();
    }
    return ok;
}

void MainWindow::Activate() {
    if (SelectedIds().empty()) return;
    HWND target = m_lastApp;
    const bool canPaste = m_settings.pasteOnEnter && target && IsWindow(target) && IsWindowVisible(target) &&
                          !IsIconic(target) && target != m_hwnd;
    if (!canPaste) {
        CopySelected(false);
        return;
    }
    if (!CopySelected(false)) return;
    m_flash.clear();
    // Back to the program, then Ctrl+V once it is in front.
    HideToTray();
    m_pasteTarget = target;
    m_pasteTries = 0;
    SetForegroundWindow(target);
    SetTimer(m_hwnd, kPasteTimer, 80, nullptr);
}

void MainWindow::TogglePin() {
    const std::vector<int64_t> ids = SelectedIds();
    if (ids.empty()) return;
    bool allPinned = true;
    for (int64_t id : ids) {
        const int i = m_store.IndexOf(id);
        if (i >= 0 && !m_store.Items()[(size_t)i].pinned) allPinned = false;
    }
    m_store.SetPinned(ids, !allPinned);
    InvalidateRect(m_list.Hwnd(), nullptr, FALSE);
    if (m_settings.filter == kFilterPinned) Refresh();
    else {
        m_preview.Clear();
        UpdatePreview();
    }
}

void MainWindow::DeleteSelected() {
    const std::vector<int64_t> ids = SelectedIds();
    if (ids.empty()) return;
    int pinned = 0;
    for (int64_t id : ids) {
        const int i = m_store.IndexOf(id);
        if (i >= 0 && m_store.Items()[(size_t)i].pinned) ++pinned;
    }
    if (ids.size() > 1 || pinned) {
        std::wstring q = ids.size() == 1 ? L"Delete this pinned item?"
                                         : L"Delete these " + FormatCount(ids.size()) + L" items?";
        if (pinned && ids.size() > 1) q += L"\n\n" + FormatCount((uint64_t)pinned) + (pinned == 1 ? L" of them is pinned." : L" of them are pinned.");
        q += L"\n\nTheir files are deleted from the \x201C" APP_FOLDER_NAME L"\x201D folder.";
        if (MessageBoxW(m_hwnd, q.c_str(), APP_NAME, MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK) return;
    }
    // Afterwards, select the item that followed the deleted ones.
    const std::vector<int> rows = m_list.Selected();
    const int next = rows.empty() ? 0 : rows.front();
    m_store.Remove(ids);
    Refresh(true);
    const int count = m_list.Count();
    if (count > 0) m_list.Select({std::min(next, count - 1)}, std::min(next, count - 1));
    UpdatePreview();
    SetFocus(m_list.Hwnd());
}

void MainWindow::ClearUnpinned() {
    int unpinned = 0;
    for (const ClipItem& it : m_store.Items())
        if (!it.pinned) ++unpinned;
    if (!unpinned) {
        MessageBoxW(m_hwnd, L"There are no unpinned items.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    const std::wstring q = L"Delete all " + FormatCount((uint64_t)unpinned) +
                           L" unpinned items and their files?\n\nPinned items are kept. This can not be undone.";
    if (MessageBoxW(m_hwnd, q.c_str(), APP_NAME, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) return;
    m_store.RemoveAllUnpinned();
    m_list.ForgetThumbnails();
    Refresh(true);
}

void MainWindow::SaveAs() {
    const ClipItem* it = FocusedItem();
    if (!it) return;
    wchar_t buf[MAX_PATH * 2];
    std::wstring name = it->file;
    if (it->kind == ClipKind::Files) name = L"File list.txt";
    wcsncpy_s(buf, name.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = it->kind == ClipKind::Image ? L"PNG picture (*.png)\0*.png\0" : L"Text (*.txt)\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrDefExt = it->kind == ClipKind::Image ? L"png" : L"txt";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return;
    if (!CopyFileW(m_store.PathOf(*it).c_str(), buf, FALSE))
        MessageBoxW(m_hwnd, L"The file could not be saved.", APP_NAME, MB_ICONWARNING);
}

void MainWindow::ShowInFolder() {
    const ClipItem* it = FocusedItem();
    if (!it) {
        OpenFolder();
        return;
    }
    const std::wstring args = L"/select,\"" + m_store.PathOf(*it) + L"\"";
    ShellExecuteW(m_hwnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void MainWindow::OpenFolder() { ShellExecuteW(m_hwnd, L"open", m_store.Folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void MainWindow::SetFilter(int filter) {
    m_settings.filter = std::clamp(filter, 0, kFilterCount - 1);
    m_toolbar.SetText(ID_FILTER_MENU, std::wstring(L"Show: ") + FilterName(m_settings.filter));
    m_toolbar.SetChecked(ID_FILTER_MENU, m_settings.filter != kFilterAll);
}

void MainWindow::SetPaused(bool paused) {
    m_paused = paused;
    if (!paused) m_lastSequence = GetClipboardSequenceNumber();  // what was copied while paused stays out
    UpdateStatus();
    Flash(paused ? L"Recording is paused: what you copy now is not kept." : L"Recording again.");
}

// ===========================================================================
// Menus
// ===========================================================================
void MainWindow::OnCommand(int id) {
    if (id >= ID_FILTER_FIRST && id < ID_FILTER_FIRST + kFilterCount) {
        SetFilter(id - ID_FILTER_FIRST);
        m_settings.Save();
        Refresh(true);
        return;
    }
    if (id >= ID_RECENT_FIRST && id < ID_RECENT_FIRST + kRecentInTray) {
        const size_t i = (size_t)(id - ID_RECENT_FIRST);
        if (i < m_store.Items().size() && CopyItem(m_store.Items()[i].id)) {
            m_tray.Notify(L"Copied", ItemTitle(m_store.Items().front(), 120) + L"\nPress Ctrl+V to paste it.");
            Refresh();
        }
        return;
    }
    switch (id) {
        case IDOK: Activate(); break;  // Enter
        case IDCANCEL:                 // Esc
            if (GetWindowTextLengthW(m_search) > 0) {
                SetWindowTextW(m_search, L"");
                SetFocus(m_search);
            } else {
                HideToTray();
            }
            break;
        case ID_FILTER_MENU: ShowFilterMenu(); break;
        case ID_PASTE: Activate(); break;
        case ID_COPY: CopySelected(false); break;
        case ID_COPY_PLAIN_PATHS: CopySelected(true); break;
        case ID_PIN: TogglePin(); break;
        case ID_DELETE: DeleteSelected(); break;
        case ID_OPEN_FOLDER: OpenFolder(); break;
        case ID_SHOW_FILE: ShowInFolder(); break;
        case ID_OPEN_ITEM:
            if (const ClipItem* it = FocusedItem())
                ShellExecuteW(m_hwnd, L"open", m_store.PathOf(*it).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_SAVE_AS: SaveAs(); break;
        case ID_CLEAR_UNPINNED: ClearUnpinned(); break;
        case ID_MORE_MENU: ShowMoreMenu(); break;
        case ID_PAUSE:
        case ID_STATUS_PAUSE: SetPaused(!m_paused); break;
        case ID_SETTINGS: ShowSettings(); break;
        case ID_ABOUT: ShowAbout(); break;
        case ID_SELECT_ALL: m_list.SelectAll(); break;
        case ID_FOCUS_SEARCH: SetFocus(m_search); break;
        case ID_OPEN:
            if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd)) HideToTray();
            else ShowWindowFromTray();
            break;
        case ID_HIDE: HideToTray(); break;
        case ID_AUTOSTART:
            if (!SetAutostart(!AutostartEnabled()))
                MessageBoxW(m_hwnd, L"The start-up setting could not be changed.", APP_NAME, MB_ICONWARNING);
            m_settings.askedAutostart = true;
            m_settings.Save();
            break;
        case ID_EXIT:
            m_quitting = true;
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
    }
}

void MainWindow::ShowFilterMenu() {
    HMENU m = CreatePopupMenu();
    for (int f = 0; f < kFilterCount; ++f) {
        AppendMenuW(m, MF_STRING | (f == m_settings.filter ? MF_CHECKED : 0), (UINT_PTR)(ID_FILTER_FIRST + f),
                    FilterName(f));
        if (f == kFilterAll || f == kFilterFiles) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    const RECT r = m_toolbar.ItemScreenRect(ID_FILTER_MENU);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowMoreMenu() {
    const bool one = m_list.Selected().size() == 1;
    const ClipItem* it = FocusedItem();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (one ? 0 : MF_GRAYED), ID_OPEN_ITEM, L"&Open the file");
    AppendMenuW(m, MF_STRING | (one ? 0 : MF_GRAYED), ID_SAVE_AS, L"&Save as\x2026");
    AppendMenuW(m, MF_STRING | (it ? 0 : MF_GRAYED), ID_SHOW_FILE, L"Show the &file in its folder");
    AppendMenuW(m, MF_STRING, ID_SELECT_ALL, L"Select &all\tCtrl+A");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_paused ? MF_CHECKED : 0), ID_PAUSE, L"&Pause recording");
    AppendMenuW(m, MF_STRING, ID_CLEAR_UNPINNED, L"&Delete all unpinned items\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"S&ettings\x2026");
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with &Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_HIDE, L"&Hide to the tray\tEsc");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"A&bout " APP_NAME);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");
    const RECT r = m_toolbar.ItemScreenRect(ID_MORE_MENU);
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowContextMenu(POINT pt) {
    const std::vector<int> sel = m_list.Selected();
    if (sel.empty()) return;
    const ClipItem* it = FocusedItem();
    const bool one = sel.size() == 1;
    bool allPinned = true;
    for (int64_t id : SelectedIds()) {
        const int i = m_store.IndexOf(id);
        if (i >= 0 && !m_store.Items()[(size_t)i].pinned) allPinned = false;
    }
    HMENU m = CreatePopupMenu();
    const std::wstring app = m_lastApp && IsWindow(m_lastApp) ? ClipboardIO::ProgramName(m_lastApp) : L"";
    const std::wstring paste =
        m_settings.pasteOnEnter && !app.empty() ? L"&Paste into " + app + L"\tEnter" : std::wstring(L"&Copy\tEnter");
    AppendMenuW(m, MF_STRING, ID_PASTE, paste.c_str());
    SetMenuDefaultItem(m, ID_PASTE, FALSE);
    if (m_settings.pasteOnEnter && !app.empty()) AppendMenuW(m, MF_STRING, ID_COPY, L"&Copy\tCtrl+C");
    if (it && it->kind == ClipKind::Files && one)
        AppendMenuW(m, MF_STRING, ID_COPY_PLAIN_PATHS, L"Copy the file &names as text");
    AppendMenuW(m, MF_STRING, ID_PIN, allPinned ? L"U&npin\tCtrl+P" : L"P&in\tCtrl+P");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (one ? 0 : MF_GRAYED), ID_OPEN_ITEM,
                it && it->kind == ClipKind::Image ? L"&Open the picture" : L"&Open the text file");
    AppendMenuW(m, MF_STRING | (one ? 0 : MF_GRAYED), ID_SAVE_AS, L"&Save as\x2026");
    AppendMenuW(m, MF_STRING | (one ? 0 : MF_GRAYED), ID_SHOW_FILE, L"Show the &file in its folder");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_DELETE, L"&Delete\tDel");
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN,
                IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd) ? L"&Hide the window" : L"&Open " APP_NAME);
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    // The latest items, to copy without opening the window.
    HMENU recent = CreatePopupMenu();
    const auto& items = m_store.Items();
    for (int i = 0; i < kRecentInTray && i < (int)items.size(); ++i) {
        std::wstring t = ItemTitle(items[(size_t)i], 60);
        std::wstring safe;
        for (wchar_t c : t) {
            if (c == L'&') safe += L'&';
            safe += c;
        }
        AppendMenuW(recent, MF_STRING, (UINT_PTR)(ID_RECENT_FIRST + i), safe.c_str());
    }
    if (items.empty()) AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"Nothing copied yet");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)recent, L"&Copy a recent item");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_paused ? MF_CHECKED : 0), ID_PAUSE, L"&Pause recording");
    AppendMenuW(m, MF_STRING, ID_OPEN_FOLDER, L"Open the \x201C" APP_FOLDER_NAME L"\x201D &folder");
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with &Windows");
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"&Settings\x2026");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"&About " APP_NAME);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);  // so the menu closes when clicking elsewhere
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);  // destroys the submenu too
}

// ===========================================================================
// Window visibility, settings, about
// ===========================================================================
void MainWindow::ShowWindowFromTray() {
    // SW_RESTORE both shows a hidden window and un-minimises it.
    ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(m_hwnd);
    BringWindowToTop(m_hwnd);
    InvalidateRect(m_list.Hwnd(), nullptr, FALSE);
    UpdateStatus();
    AskAutostartOnce();
}

void MainWindow::ShowForPaste() {
    HWND fg = GetForegroundWindow();
    if (fg && fg != m_hwnd && !IsShellWindow(fg)) m_lastApp = fg;
    // A fresh start: no search, the newest item selected.
    SetWindowTextW(m_search, L"");
    KillTimer(m_hwnd, kSearchTimer);
    Refresh(true);
    ShowWindowFromTray();
    SetFocus(m_search);
}

void MainWindow::HideToTray() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (GetWindowPlacement(m_hwnd, &wp) && wp.showCmd != SW_SHOWMINIMIZED) {
        m_settings.placement = wp;
        m_settings.hasPlacement = true;
    }
    ShowWindow(m_hwnd, SW_HIDE);
}

void MainWindow::ShowSettings() {
    if (!IsWindowVisible(m_hwnd)) ShowWindowFromTray();
    Settings edited = m_settings;
    bool autostart = AutostartEnabled();
    const bool wasAutostart = autostart;
    if (!ShowSettingsDialog(m_inst, m_hwnd, edited, autostart, m_store.Folder())) return;
    const bool themeChanged = edited.themeMode != m_settings.themeMode;
    const bool hotkeyChanged = edited.hotkey != m_settings.hotkey;
    edited.placement = m_settings.placement;
    edited.hasPlacement = m_settings.hasPlacement;
    edited.listWidthDip = m_settings.listWidthDip;
    edited.filter = m_settings.filter;
    m_settings = edited;
    m_settings.askedAutostart = true;
    m_settings.Save();
    if (autostart != wasAutostart && !SetAutostart(autostart))
        MessageBoxW(m_hwnd, L"The start-up setting could not be changed.", APP_NAME, MB_ICONWARNING);
    if (hotkeyChanged) RegisterShortcut(true);
    if (themeChanged) ApplyTheme();
    Cleanup();
    Refresh();
}

void MainWindow::ShowAbout() {
    const std::wstring text =
        APP_NAME L"  " APP_VERSION L"\n\n" APP_COPYRIGHT L".\n"
                 L"Developed for faster experience.\n\n"
                 L"Keeps everything you copy \x2014 text, pictures and files \x2014 with no limit, as ordinary files "
                 L"in the \x201C" APP_FOLDER_NAME L"\x201D folder next to the program. Search, pin and paste any of it "
                 L"again from any program.";
    MessageBoxW(IsWindowVisible(m_hwnd) ? m_hwnd : nullptr, text.c_str(), L"About " APP_NAME, MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ApplyTheme() {
    ReloadTheme((ThemeMode)m_settings.themeMode);
    ApplyWindowTheme(m_hwnd);
    m_toolbar.OnThemeChanged();
    m_status.OnThemeChanged();
    m_list.OnThemeChanged();
    m_preview.OnThemeChanged();
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME | RDW_ERASE);
}
