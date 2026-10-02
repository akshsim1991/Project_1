// MainWindow.cpp - top-level window and the change flow.
#include "MainWindow.h"

#include <algorithm>
#include <memory>

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include "Dialogs.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kSearchTimer = 2;  // search-as-you-type debounce
constexpr size_t kMaxUndo = 20;

const wchar_t* FilterTitle(int f) {
    switch (f) {
        case kFilterEnabled: return L"Enabled";
        case kFilterDisabled: return L"Disabled";
        case kFilterAttention: return L"Needs attention (warnings)";
        case kFilterThirdParty: return L"Not from Microsoft";
        case kFilterRegistry: return L"Registry (Run keys)";
        case kFilterStartupFolder: return L"Startup folders";
        case kFilterTasks: return L"Scheduled tasks";
        default: return L"Everything";
    }
}

std::wstring NameList(const std::vector<std::wstring>& names, size_t max = 8) {
    std::wstring out;
    for (size_t i = 0; i < names.size() && i < max; ++i) out += L"  \x2022 " + names[i] + L"\n";
    if (names.size() > max) out += L"  \x2026 and " + std::to_wstring(names.size() - max) + L" more\n";
    return out;
}

const wchar_t* PastTense(OpKind k) {
    switch (k) {
        case OpKind::Enable: return L"enabled";
        case OpKind::Disable: return L"disabled";
        case OpKind::Delete: return L"deleted";
        case OpKind::Restore: return L"restored";
        default: return L"added";
    }
}

const wchar_t* Progress(OpKind k) {
    switch (k) {
        case OpKind::Enable: return L"Enabling";
        case OpKind::Disable: return L"Disabling";
        case OpKind::Delete: return L"Deleting";
        case OpKind::Restore: return L"Restoring";
        default: return L"Adding";
    }
}
}  // namespace

// ===========================================================================
// Creation
// ===========================================================================
bool MainWindow::Create(HINSTANCE inst, int showCmd) {
    m_inst = inst;
    m_elevated = IsElevated();
    ReloadTheme((ThemeMode)m_settings.themeMode);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = APP_WINDOW_CLASS;
    if (!RegisterClassExW(&wc)) return false;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int dpi = GetWindowDpi(nullptr);
    const int w = std::min(Dpi(1240, dpi), (int)(work.right - work.left) * 9 / 10);
    const int h = std::min(Dpi(760, dpi), (int)(work.bottom - work.top) * 9 / 10);
    m_hwnd = CreateWindowExW(0, APP_WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                             CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    ApplyWindowTheme(m_hwnd);
    CreateChildren();
    CreateAccelerators();
    UpdateTitle();

    if (m_settings.hasPlacement) {
        WINDOWPLACEMENT wp = m_settings.placement;
        wp.flags = 0;
        wp.showCmd = wp.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        if (showCmd == SW_SHOWMINIMIZED || showCmd == SW_MINIMIZE || showCmd == SW_SHOWMINNOACTIVE)
            wp.showCmd = (UINT)showCmd;
        SetWindowPlacement(m_hwnd, &wp);
    } else {
        ShowWindow(m_hwnd, showCmd);
    }
    UpdateWindow(m_hwnd);
    if (!m_worker.Start(m_hwnd)) return false;
    m_opText = L"Loading\x2026";
    UpdateStatus();
    m_worker.Refresh();
    SetRefreshTimer();
    SetFocus(m_list.Hwnd());
    return true;
}

void MainWindow::CreateChildren() {
    m_toolbar.Create(m_hwnd, m_hwnd, 46);
    m_toolbar.AddTextButton(ID_REFRESH, L"Refresh", L"Reload the list (F5)", 72);
    m_toolbar.AddSeparator();
    m_toolbar.AddTextButton(ID_ENABLE, L"Enable", L"Let the selected entries start with Windows", 64);
    m_toolbar.AddTextButton(ID_DISABLE, L"Disable", L"Stop the selected entries starting with Windows (kept, can be enabled again)", 68);
    m_toolbar.AddTextButton(ID_DELETE, L"Delete", L"Remove the selected entries (a backup is kept) (Del)", 62);
    m_toolbar.AddSeparator();
    m_toolbar.AddTextButton(ID_ADD, L"Add\x2026", L"Make a program start with Windows (Ctrl+N)", 56);
    m_toolbar.AddTextButton(ID_RESTORE_DELETED, L"Restore\x2026", L"Bring back deleted entries", 74);
    m_toolbar.AddTextButton(ID_UNDO, L"Undo", L"Undo the last change (Ctrl+Z)", 56);
    m_toolbar.AddSpacer();
    m_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                               m_toolbar.Hwnd(), (HMENU)(INT_PTR)ID_SEARCH_EDIT, m_inst, nullptr);
    SendMessageW(m_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search (Ctrl+F)");
    SetWindowSubclass(m_search, &MainWindow::EditProc, 1, (DWORD_PTR)this);
    m_toolbar.AddChild(m_search, 200);
    m_toolbar.AddLabel(ID_FILTER_MENU, 190, true, L"Show only some entries");
    m_toolbar.AddTextButton(ID_MORE_MENU, L"More", L"Settings, About and more", 52);

    m_list.Create(m_hwnd, &m_settings);
    m_list.onSelectionChanged = [this] { UpdateUi(); };
    m_list.onContextMenu = [this](POINT pt) { ShowContextMenu(pt); };
    m_list.onActivate = [this] { ShowDetails(); };

    m_status.Create(m_hwnd, m_hwnd, 30);
    m_status.SetBorderTop(true);
    m_status.AddLabel(ID_STATUS_TEXT, 430, false, nullptr);
    m_status.AddSpacer();
    m_status.AddLabel(ID_STATUS_OP, 460, false, nullptr);
    m_status.SetRightAligned(ID_STATUS_OP, true);
    if (!m_elevated)
        m_status.AddTextButton(ID_ELEVATE, L"Restart as administrator",
                               L"Needed to change entries for all users and most scheduled tasks", 176);
    UpdateUi();
}

void MainWindow::CreateAccelerators() {
    ACCEL acc[] = {
        {FVIRTKEY, VK_F5, ID_REFRESH},
        {FCONTROL | FVIRTKEY, 'F', ID_FOCUS_SEARCH},
        {FCONTROL | FVIRTKEY, 'A', ID_SELECT_ALL},
        {FCONTROL | FVIRTKEY, 'C', ID_COPY_NAMES},
        {FCONTROL | FSHIFT | FVIRTKEY, 'C', ID_COPY_DETAILS},
        {FCONTROL | FVIRTKEY, 'N', ID_ADD},
        {FCONTROL | FVIRTKEY, 'Z', ID_UNDO},
        {FVIRTKEY, VK_DELETE, ID_DELETE},
        {FCONTROL | FVIRTKEY, VK_OEM_COMMA, ID_SETTINGS},
        {FVIRTKEY, VK_F1, ID_ABOUT},
        {FALT | FVIRTKEY, VK_RETURN, ID_DETAILS},
    };
    m_accel = CreateAcceleratorTableW(acc, (int)(sizeof(acc) / sizeof(acc[0])));
}

void MainWindow::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int th = m_toolbar.Height(), sh = m_status.Height();
    MoveWindow(m_toolbar.Hwnd(), 0, 0, rc.right, th, TRUE);
    MoveWindow(m_list.Hwnd(), 0, th, rc.right, std::max(0, (int)rc.bottom - th - sh), TRUE);
    MoveWindow(m_status.Hwnd(), 0, rc.bottom - sh, rc.right, sh, TRUE);
}

void MainWindow::UpdateTitle() {
    std::wstring title = APP_NAME;
    if (m_elevated) title += L" (Administrator)";
    SetWindowTextW(m_hwnd, title.c_str());
}

void MainWindow::SetRefreshTimer() {
    KillTimer(m_hwnd, kRefreshTimer);
    if (m_settings.refreshSeconds > 0)
        SetTimer(m_hwnd, kRefreshTimer, (UINT)m_settings.refreshSeconds * 1000, nullptr);
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

LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_ENTRIES: {
            std::unique_ptr<EntryList> list((EntryList*)lp);
            m_readErrors = std::move(list->errors);
            m_list.SetData(std::move(list->entries));
            if (!m_loaded) {
                m_loaded = true;
                if (!m_running) m_opText.clear();
            }
            UpdateUi();
            return 0;
        }
        case WM_APP_OP_DONE:
            OnDone((OpResult*)lp);
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp), (HWND)lp);
            return 0;
        case WM_NOTIFY: {
            LRESULT result = 0;
            if (m_list.OnNotify((NMHDR*)lp, result)) return result;
            break;
        }
        case WM_CONTEXTMENU:
            if ((HWND)wp == m_list.Hwnd() && GET_X_LPARAM(lp) == -1) {  // menu key / Shift+F10
                RECT rc;
                GetWindowRect(m_list.Hwnd(), &rc);
                const int dpi = GetWindowDpi(m_hwnd);
                ShowContextMenu({rc.left + Dpi(40, dpi), rc.top + Dpi(60, dpi)});
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp == kRefreshTimer) {
                if (!IsIconic(m_hwnd) && !m_running) m_worker.Refresh();
            } else if (wp == kSearchTimer) {
                KillTimer(m_hwnd, kSearchTimer);
                m_list.SetSearch(GetWindowString(m_search));
                UpdateUi();
            }
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_SETFOCUS:
            SetFocus(m_list.Hwnd());
            return 0;
        case WM_GETMINMAXINFO: {
            const int dpi = GetWindowDpi(m_hwnd);
            ((MINMAXINFO*)lp)->ptMinTrackSize = {Dpi(760, dpi), Dpi(360, dpi)};
            return 0;
        }
        case WM_DPICHANGED: {
            m_toolbar.OnDpiChanged();
            m_status.OnDpiChanged();
            m_list.OnDpiChanged();
            const RECT* r = (const RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && lstrcmpW((const wchar_t*)lp, L"ImmersiveColorSet") == 0 && m_settings.themeMode == 0)
                ApplyTheme();
            break;
        case WM_CLOSE: {
            WINDOWPLACEMENT wp{sizeof(wp)};
            GetWindowPlacement(m_hwnd, &wp);
            if (wp.showCmd == SW_SHOWMINIMIZED)
                wp.showCmd = (wp.flags & WPF_RESTORETOMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            m_settings.placement = wp;
            m_settings.hasPlacement = true;
            m_list.SaveColumns();
            m_settings.Save();
            DestroyWindow(m_hwnd);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(m_hwnd, kRefreshTimer);
            m_worker.Stop();
            if (m_accel) DestroyAcceleratorTable(m_accel);
            m_accel = nullptr;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

LRESULT CALLBACK MainWindow::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    auto* self = (MainWindow*)ref;
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                SetWindowTextW(hwnd, L"");
                SetFocus(self->m_list.Hwnd());
                return 0;
            }
            if (wp == VK_RETURN || wp == VK_DOWN) {  // jump into the results
                SetFocus(self->m_list.Hwnd());
                if (self->m_list.ShownCount() > 0 && self->m_list.Selected().empty())
                    ListView_SetItemState(self->m_list.Hwnd(), 0, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wp == L'\r' || wp == 27) return 0;  // no "ding"
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &MainWindow::EditProc, 1);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ===========================================================================
// Commands
// ===========================================================================
void MainWindow::OnCommand(int id, int code, HWND ctl) {
    if (ctl && ctl == m_search) {
        if (code == EN_CHANGE) SetTimer(m_hwnd, kSearchTimer, 150, nullptr);
        return;
    }
    if (id >= ID_FILTER_FIRST && id < ID_FILTER_FIRST + kFilterCount) {
        m_list.SetFilter(id - ID_FILTER_FIRST);
        UpdateUi();
        return;
    }
    const bool inSearch = GetFocus() == m_search;
    StartupEntry focusedCopy;
    const StartupEntry* focused = nullptr;
    if (const StartupEntry* f = m_list.Focused()) {
        focusedCopy = *f;  // the list may be replaced while a window from here is open
        focused = &focusedCopy;
    }
    switch (id) {
        case IDOK:
            // Enter (IsDialogMessage turns it into IDOK): in the search box
            // it moves to the results, in the list it opens the details.
            if (inSearch) {
                SetFocus(m_list.Hwnd());
                if (m_list.ShownCount() > 0 && m_list.Selected().empty())
                    ListView_SetItemState(m_list.Hwnd(), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            } else if (GetFocus() == m_list.Hwnd()) {
                ShowDetails();
            }
            break;
        case ID_REFRESH:
            m_worker.Refresh();
            break;
        case ID_ENABLE: SetEnabled(true); break;
        case ID_DISABLE: SetEnabled(false); break;
        case ID_DELETE:
            if (inSearch)
                SendMessageW(m_search, WM_KEYDOWN, VK_DELETE, 0);
            else
                DeleteSelected();
            break;
        case ID_ADD: AddEntry(); break;
        case ID_RESTORE_DELETED: RestoreDeleted(); break;
        case ID_UNDO:
            if (inSearch)
                SendMessageW(m_search, EM_UNDO, 0, 0);
            else
                Undo();
            break;
        case ID_DETAILS: ShowDetails(); break;
        case ID_FILTER_MENU: ShowFilterMenu(); break;
        case ID_MORE_MENU: ShowMoreMenu(); break;
        case ID_FOCUS_SEARCH:
            SetFocus(m_search);
            SendMessageW(m_search, EM_SETSEL, 0, -1);
            break;
        case ID_SELECT_ALL:
            if (inSearch)
                SendMessageW(m_search, EM_SETSEL, 0, -1);
            else
                m_list.SelectAll();
            break;
        case ID_COPY_NAMES:
            if (inSearch)
                SendMessageW(m_search, WM_COPY, 0, 0);
            else
                CopyNames();
            break;
        case ID_COPY_DETAILS: CopyDetails(); break;
        case ID_OPEN_FILE_LOCATION:
            if (focused) OpenFileLocation(m_hwnd, *focused);
            break;
        case ID_OPEN_ENTRY_LOCATION:
            if (focused) ShowWhereSet(m_hwnd, *focused);
            break;
        case ID_VIRUSTOTAL:
            if (focused) CheckOnVirusTotal(m_hwnd, *focused);
            break;
        case ID_SEARCH_ONLINE:
            if (focused) SearchEntryOnline(m_hwnd, *focused);
            break;
        case ID_OPEN_BACKUPS: {
            const std::wstring dir = Entries::BackupRoot();
            CreateDirectoryW(DirectoryFromPath(dir).c_str(), nullptr);
            CreateDirectoryW(dir.c_str(), nullptr);
            ShellExecuteW(m_hwnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case ID_SETTINGS: ShowSettings(); break;
        case ID_ABOUT: ShowAbout(); break;
        case ID_ELEVATE:
            if (RelaunchElevated(m_hwnd)) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
        case ID_EXIT: PostMessageW(m_hwnd, WM_CLOSE, 0, 0); break;
    }
}

void MainWindow::UpdateUi() {
    const auto sel = m_list.Selected();
    bool enable = false, disable = false;
    for (const StartupEntry* e : sel) {
        if (!e->canDisable) continue;
        if (e->enabled) disable = true;
        else enable = true;
    }
    const bool idle = !m_running;
    m_toolbar.SetEnabled(ID_ENABLE, idle && enable);
    m_toolbar.SetEnabled(ID_DISABLE, idle && disable);
    m_toolbar.SetEnabled(ID_DELETE, idle && !sel.empty());
    m_toolbar.SetEnabled(ID_ADD, idle);
    m_toolbar.SetEnabled(ID_RESTORE_DELETED, idle);
    m_toolbar.SetEnabled(ID_UNDO, idle && !m_undo.empty());
    m_toolbar.SetText(ID_FILTER_MENU, std::wstring(FilterTitle(m_settings.filter)) + L"  \x25BE");
    m_toolbar.SetChecked(ID_FILTER_MENU, m_settings.filter != kFilterAll);
    UpdateStatus();
}

void MainWindow::UpdateStatus() {
    const auto& all = m_list.All();
    std::wstring text;
    if (m_loaded) {
        int enabled = 0;
        for (const auto& e : all)
            if (e.enabled) ++enabled;
        text = std::to_wstring(all.size()) + L" entries  \x00B7  " + std::to_wstring(enabled) + L" enabled  \x00B7  " +
               std::to_wstring(all.size() - (size_t)enabled) + L" disabled";
        if (m_list.ShownCount() != (int)all.size())
            text += L"  \x00B7  " + std::to_wstring(m_list.ShownCount()) + L" shown";
        const size_t sel = m_list.Selected().size();
        if (sel > 1) text += L"  \x00B7  " + std::to_wstring(sel) + L" selected";
    }
    std::wstring op = m_opText;
    if (op.empty() && !m_readErrors.empty()) op = L"\x26A0 " + m_readErrors.front();
    m_status.SetText(ID_STATUS_TEXT, text);
    m_status.SetText(ID_STATUS_OP, op);
}

// ===========================================================================
// Changes
// ===========================================================================
void MainWindow::SetEnabled(bool enable) {
    if (m_running) return;
    std::vector<StartupEntry> targets;
    std::vector<std::wstring> names;
    bool runOnce = false;
    for (const StartupEntry* e : m_list.Selected()) {
        if (!e->canDisable) {
            runOnce = true;
            continue;
        }
        if (e->enabled != enable) {
            targets.push_back(*e);
            names.push_back(e->name);
        }
    }
    if (targets.empty()) {
        m_opText = runOnce ? L"\x201CRun once\x201D entries can only be deleted."
                           : enable ? L"The selected entries are already enabled."
                                    : L"The selected entries are already disabled.";
        UpdateStatus();
        return;
    }
    if (!enable && m_settings.confirmDisable) {
        const std::wstring msg = L"Stop these entries starting with Windows?\n\n" + NameList(names) +
                                 L"\nThey stay in the list and can be enabled again at any time.";
        if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    }
    OpRequest req;
    req.kind = enable ? OpKind::Enable : OpKind::Disable;
    req.entries = std::move(targets);
    Launch(std::move(req));
}

void MainWindow::DeleteSelected() {
    if (m_running) return;
    // Copies: an automatic refresh while the question is open replaces the list.
    std::vector<StartupEntry> targets;
    for (const StartupEntry* e : m_list.Selected()) targets.push_back(*e);
    if (targets.empty()) return;
    std::vector<std::wstring> names;
    bool microsoft = false;
    for (const StartupEntry& e : targets) {
        names.push_back(e.name);
        if (e.microsoft) microsoft = true;
    }
    std::wstring msg = (targets.size() == 1 ? std::wstring(L"Delete this entry?\n\n")
                                            : L"Delete these " + std::to_wstring(targets.size()) + L" entries?\n\n") +
                       NameList(names) +
                       L"\nA backup is saved first: Undo (Ctrl+Z) or \x201CRestore\x2026\x201D brings it back. "
                       L"If you only want it to stop starting with Windows, Disable is enough.";
    if (microsoft)
        msg += L"\n\n\x26A0 Some of these are part of Windows or Microsoft software. Deleting them can break "
               L"features; disabling is safer.";
    if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME,
                    MB_YESNO | (microsoft ? MB_ICONWARNING : MB_ICONQUESTION) | MB_DEFBUTTON2) != IDYES)
        return;
    OpRequest req;
    req.kind = OpKind::Delete;
    req.entries = std::move(targets);
    Launch(std::move(req));
}

void MainWindow::AddEntry() {
    if (m_running) return;
    AddRequest add;
    if (!ShowAddDialog(m_inst, m_hwnd, m_elevated, add)) return;
    OpRequest req;
    req.kind = OpKind::Add;
    req.name = add.name;
    req.program = add.program;
    req.args = add.args;
    req.allUsers = add.allUsers;
    req.shortcut = add.shortcut;
    Launch(std::move(req));
}

void MainWindow::RestoreDeleted() {
    if (m_running) return;
    std::vector<std::wstring> chosen = ShowRestoreDialog(m_inst, m_hwnd);
    if (chosen.empty()) return;
    OpRequest req;
    req.kind = OpKind::Restore;
    req.backups = std::move(chosen);
    Launch(std::move(req));
}

void MainWindow::Launch(OpRequest&& request) {
    m_running = true;
    m_opText = std::wstring(Progress(request.kind)) + L"\x2026";
    m_worker.Run(std::move(request));
    UpdateUi();
}

void MainWindow::OnDone(OpResult* r) {
    std::unique_ptr<OpResult> res(r);
    m_running = false;
    const OpRequest& req = res->request;

    auto display = [](const OpOutcome& o) { return o.name; };

    // Record what succeeded, so it can be undone.
    UndoStep step;
    step.kind = req.kind;
    std::vector<const OpOutcome*> failed;
    int ok = 0;
    for (size_t i = 0; i < res->outcomes.size(); ++i) {
        const OpOutcome& o = res->outcomes[i];
        if (o.error) {
            failed.push_back(&o);
            continue;
        }
        ++ok;
        if ((req.kind == OpKind::Enable || req.kind == OpKind::Disable) && i < req.entries.size())
            step.entries.push_back(req.entries[i]);
        if (req.kind == OpKind::Delete && !o.backup.empty()) step.backups.push_back(o.backup);
    }
    if (req.kind == OpKind::Add && ok) {
        step.addedName = req.name;
        step.addedAllUsers = req.allUsers;
        step.addedShortcut = req.shortcut;
    }
    const bool undoable = ok && req.kind != OpKind::Restore &&
                          (!step.entries.empty() || !step.backups.empty() || !step.addedName.empty());
    if (undoable && !req.forUndo) {
        m_undo.push_back(std::move(step));
        if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    }

    // Status line.
    if (res->outcomes.size() == 1 && ok == 1)
        m_opText = L"\x201C" + display(res->outcomes[0]) + L"\x201D " + PastTense(req.kind) + L".";
    else {
        m_opText = std::to_wstring(ok) + L" of " + std::to_wstring(res->outcomes.size()) + L" " + PastTense(req.kind);
        if (!failed.empty()) m_opText += L", " + std::to_wstring(failed.size()) + L" failed";
        m_opText += L".";
    }
    if (req.forUndo && ok) m_opText = L"Undone: " + m_opText;
    else if (undoable) m_opText += L"  Ctrl+Z undoes it.";
    UpdateUi();

    if (failed.empty()) return;
    std::wstring msg;
    if (res->outcomes.size() == 1) {
        msg = L"\x201C" + display(*failed[0]) + L"\x201D could not be " + PastTense(req.kind) + L".\n\n" +
              Entries::ErrorText(failed[0]->error);
    } else {
        msg = std::to_wstring(failed.size()) + L" of " + std::to_wstring(res->outcomes.size()) +
              L" entries could not be " + PastTense(req.kind) + L":\n\n";
        size_t n = 0;
        for (const OpOutcome* f : failed) {
            if (++n > 12) {
                msg += L"\x2026 and " + std::to_wstring(failed.size() - 12) + L" more";
                break;
            }
            msg += L"\x2022 " + display(*f) + L": " + Entries::ErrorText(f->error) + L"\n";
        }
    }
    MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
}

void MainWindow::Undo() {
    if (m_running) return;
    if (m_undo.empty()) {
        m_opText = L"There is nothing to undo.";
        UpdateStatus();
        return;
    }
    UndoStep step = std::move(m_undo.back());
    m_undo.pop_back();
    OpRequest req;
    req.forUndo = true;
    switch (step.kind) {
        case OpKind::Enable:
        case OpKind::Disable:
            req.kind = step.kind == OpKind::Enable ? OpKind::Disable : OpKind::Enable;
            req.entries = std::move(step.entries);
            break;
        case OpKind::Delete:
            req.kind = OpKind::Restore;
            req.backups = std::move(step.backups);
            break;
        case OpKind::Add: {
            // Find the entry that was created and delete it (with a backup).
            const EntryKind kind = step.addedShortcut ? EntryKind::StartupFolder : EntryKind::Run;
            const Scope scope = step.addedAllUsers ? Scope::Machine : Scope::User;
            for (const StartupEntry& e : m_list.All())
                if (e.kind == kind && e.scope == scope && _wcsicmp(e.name.c_str(), step.addedName.c_str()) == 0)
                    req.entries.push_back(e);
            if (req.entries.empty()) {
                m_opText = L"The added entry is no longer there; nothing to undo.";
                UpdateUi();
                return;
            }
            req.kind = OpKind::Delete;
            break;
        }
        default: return;
    }
    Launch(std::move(req));
}

// ===========================================================================
// Menus
// ===========================================================================
void MainWindow::ShowFilterMenu() {
    HMENU m = CreatePopupMenu();
    for (int f = 0; f < kFilterCount; ++f) {
        if (f == kFilterEnabled || f == kFilterAttention || f == kFilterRegistry)
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_FILTER_FIRST + f, FilterTitle(f));
    }
    CheckMenuRadioItem(m, ID_FILTER_FIRST, ID_FILTER_FIRST + kFilterCount - 1, ID_FILTER_FIRST + m_settings.filter,
                       MF_BYCOMMAND);
    const RECT r = m_toolbar.ItemScreenRect(ID_FILTER_MENU);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowMoreMenu() {
    const bool none = m_list.Selected().empty();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (none ? MF_GRAYED : 0), ID_DETAILS, L"&Details\tEnter");
    AppendMenuW(m, MF_STRING | (m_undo.empty() || m_running ? MF_GRAYED : 0), ID_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_running ? MF_GRAYED : 0), ID_ADD, L"&Add an entry\x2026\tCtrl+N");
    AppendMenuW(m, MF_STRING | (m_running ? MF_GRAYED : 0), ID_RESTORE_DELETED, L"&Restore deleted entries\x2026");
    AppendMenuW(m, MF_STRING, ID_OPEN_BACKUPS, L"Open the &backup folder");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_REFRESH, L"Re&fresh\tF5");
    AppendMenuW(m, MF_STRING, ID_SELECT_ALL, L"Select a&ll\tCtrl+A");
    AppendMenuW(m, MF_STRING | (none ? MF_GRAYED : 0), ID_COPY_DETAILS, L"&Copy details\tCtrl+Shift+C");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    if (!m_elevated) AppendMenuW(m, MF_STRING, ID_ELEVATE, L"Restart as a&dministrator");
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"&Settings\x2026\tCtrl+,");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"Ab&out " APP_NAME L"\tF1");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");
    const RECT r = m_toolbar.ItemScreenRect(ID_MORE_MENU);
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowContextMenu(POINT screen) {
    const auto sel = m_list.Selected();
    if (sel.empty()) return;
    bool enable = false, disable = false;
    for (const StartupEntry* e : sel) {
        if (!e->canDisable) continue;
        if (e->enabled) disable = true;
        else enable = true;
    }
    auto en = [this](bool on) -> UINT { return on && !m_running ? MF_ENABLED : MF_GRAYED; };
    const StartupEntry* f = m_list.Focused();
    const bool hasFile = f && !f->program.empty() && FileExists(f->program);

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_DETAILS, sel.size() > 1 ? L"&About these entries\tEnter" : L"&Details\tEnter");
    SetMenuDefaultItem(m, ID_DETAILS, FALSE);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | en(enable), ID_ENABLE, L"&Enable");
    AppendMenuW(m, MF_STRING | en(disable), ID_DISABLE, L"D&isable");
    AppendMenuW(m, MF_STRING | en(true), ID_DELETE, L"Dele&te\x2026\tDel");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (hasFile ? 0 : MF_GRAYED), ID_OPEN_FILE_LOCATION, L"Open file &location");
    AppendMenuW(m, MF_STRING | (f ? 0 : MF_GRAYED), ID_OPEN_ENTRY_LOCATION,
                f && f->kind == EntryKind::Task            ? L"S&how in Task Scheduler"
                : f && f->kind == EntryKind::StartupFolder ? L"S&how in the Startup folder"
                                                           : L"S&how in Registry Editor");
    AppendMenuW(m, MF_STRING | (hasFile ? 0 : MF_GRAYED), ID_VIRUSTOTAL, L"Check on &VirusTotal");
    AppendMenuW(m, MF_STRING, ID_SEARCH_ONLINE, L"Search &online");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COPY_NAMES, L"&Copy name\tCtrl+C");
    AppendMenuW(m, MF_STRING, ID_COPY_DETAILS, L"Copy det&ails\tCtrl+Shift+C");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

// ===========================================================================
// Clipboard, dialogs, theme
// ===========================================================================
void MainWindow::CopyNames() {
    std::wstring text;
    for (const StartupEntry* e : m_list.Selected()) {
        if (!text.empty()) text += L"\r\n";
        text += e->name;
    }
    if (!text.empty()) CopyToClipboard(m_hwnd, text);
}

void MainWindow::CopyDetails() {
    const auto sel = m_list.Selected();
    if (sel.empty()) return;
    std::wstring text;
    for (int c = 0; c < kColumnCount; ++c) text += std::wstring(c ? L"\t" : L"") + EntryView::ColumnTitle(c);
    for (const StartupEntry* e : sel) {
        text += L"\r\n";
        for (int c = 0; c < kColumnCount; ++c) {
            std::wstring cell = EntryView::CellText(*e, c);
            for (wchar_t& ch : cell)
                if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
            text += (c ? L"\t" : L"") + cell;
        }
    }
    CopyToClipboard(m_hwnd, text);
    m_opText = L"Copied " + std::to_wstring(sel.size()) + L" row(s) (paste into Excel or Notepad).";
    UpdateStatus();
}

void MainWindow::ShowDetails() {
    const auto sel = m_list.Selected();
    if (sel.empty()) return;
    std::vector<StartupEntry> copies;
    for (const StartupEntry* e : sel) copies.push_back(*e);
    ShowDetailsDialog(m_inst, m_hwnd, copies);
}

void MainWindow::ShowSettings() {
    Settings edited = m_settings;
    bool resetColumns = false;
    if (!ShowSettingsDialog(m_inst, m_hwnd, edited, resetColumns)) return;
    const bool themeChanged = edited.themeMode != m_settings.themeMode;
    m_settings.themeMode = edited.themeMode;
    m_settings.refreshSeconds = edited.refreshSeconds;
    m_settings.confirmDisable = edited.confirmDisable;
    m_settings.showMicrosoftTasks = edited.showMicrosoftTasks;
    m_settings.showRunOnce = edited.showRunOnce;
    m_settings.alwaysElevate = edited.alwaysElevate;
    if (resetColumns) m_list.ResetColumns();
    m_list.Reapply();
    SetRefreshTimer();
    if (themeChanged) ApplyTheme();
    m_settings.Save();
    UpdateUi();
}

void MainWindow::ShowAbout() {
    const std::wstring text =
        std::wstring(APP_NAME L"  " APP_VERSION L"\n\n" APP_COPYRIGHT L".\n"
                     L"Developed for faster experience.\n\n"
                     L"See everything that starts with Windows (registry Run keys, Startup folders and "
                     L"scheduled tasks) and enable, disable, delete or add entries. Deleted entries are "
                     L"backed up and can be restored.\n\n"
                     L"Running as: ") +
        (m_elevated ? L"Administrator" : L"standard user (changes for all users need administrator)");
    MessageBoxW(m_hwnd, text.c_str(), L"About " APP_NAME, MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ApplyTheme() {
    ReloadTheme((ThemeMode)m_settings.themeMode);
    ApplyWindowTheme(m_hwnd);
    m_toolbar.OnThemeChanged();
    m_status.OnThemeChanged();
    m_list.OnThemeChanged();
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}
