// MainWindow.cpp - top-level window and the action flow.
#include "MainWindow.h"

#include <algorithm>
#include <memory>
#include <set>

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

const wchar_t* FilterTitle(int f) {
    switch (f) {
        case kFilterRunning: return L"Running";
        case kFilterStopped: return L"Stopped";
        case kFilterAutoStopped: return L"Automatic but not running";
        case kFilterDisabled: return L"Disabled";
        case kFilterThirdParty: return L"Third-party only";
        case kFilterCritical: return L"Critical to Windows";
        default: return L"All services";
    }
}

const wchar_t* ModeTitle(StartMode m) {
    switch (m) {
        case StartMode::Automatic: return L"Automatic";
        case StartMode::AutomaticDelayed: return L"Automatic (delayed start)";
        case StartMode::Manual: return L"Manual";
        default: return L"Disabled";
    }
}

// "A, B, C and 4 more" style list, one name per line.
std::wstring NameList(const std::vector<std::wstring>& names, size_t max = 8) {
    std::wstring out;
    for (size_t i = 0; i < names.size() && i < max; ++i) out += L"  \x2022 " + names[i] + L"\n";
    if (names.size() > max) out += L"  \x2026 and " + std::to_wstring(names.size() - max) + L" more\n";
    return out;
}

bool Risky(OpKind kind, StartMode mode) {
    return kind == OpKind::Stop || kind == OpKind::Restart || kind == OpKind::Pause ||
           kind == OpKind::Kill ||
           (kind == OpKind::SetStartMode && (mode == StartMode::Manual || mode == StartMode::Disabled));
}

std::wstring PastTense(OpKind kind) {
    switch (kind) {
        case OpKind::Start: return L"started";
        case OpKind::Stop: return L"stopped";
        case OpKind::Restart: return L"restarted";
        case OpKind::Pause: return L"paused";
        case OpKind::Resume: return L"resumed";
        case OpKind::Kill: return L"killed";
        default: return L"changed";
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
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = APP_WINDOW_CLASS;
    if (!RegisterClassExW(&wc)) return false;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int dpi = GetWindowDpi(nullptr);
    const int w = std::min(Dpi(1280, dpi), (int)(work.right - work.left) * 9 / 10);
    const int h = std::min(Dpi(800, dpi), (int)(work.bottom - work.top) * 9 / 10);
    m_hwnd = CreateWindowExW(0, APP_WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
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
    m_opText = L"Loading services\x2026";
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
    m_toolbar.AddTextButton(ID_START, L"Start", L"Start the selected services (Ctrl+Shift+S)", 56);
    m_toolbar.AddTextButton(ID_STOP, L"Stop", L"Stop the selected services (Ctrl+Shift+T)", 56);
    m_toolbar.AddTextButton(ID_RESTART, L"Restart", L"Restart the selected services (Ctrl+Shift+R)", 68);
    m_toolbar.AddTextButton(ID_PAUSE, L"Pause", L"Pause the selected services", 60);
    m_toolbar.AddTextButton(ID_RESUME, L"Resume", L"Resume paused services", 70);
    m_toolbar.AddTextButton(ID_KILL, L"Kill", L"End the service's process immediately (Ctrl+Shift+K)", 50);
    m_toolbar.AddSeparator();
    m_toolbar.AddTextButton(ID_STARTTYPE_MENU, L"Start type \x25BE",
                            L"Automatic, delayed, manual or disabled", 104);
    m_toolbar.AddSpacer();
    m_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                               0, 0, 0, 0, m_toolbar.Hwnd(), (HMENU)(INT_PTR)ID_SEARCH_EDIT, m_inst,
                               nullptr);
    SendMessageW(m_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search services (Ctrl+F)");
    SetWindowSubclass(m_search, &MainWindow::EditProc, 1, (DWORD_PTR)this);
    m_toolbar.AddChild(m_search, 230);
    m_toolbar.AddLabel(ID_FILTER_MENU, 190, true, L"Show only some services");
    m_toolbar.AddTextButton(ID_MORE_MENU, L"More", L"Settings, About and more", 52);

    m_list.Create(m_hwnd, &m_settings);
    m_list.onSelectionChanged = [this] { UpdateUi(); };
    m_list.onContextMenu = [this](POINT pt) { ShowContextMenu(pt); };

    m_status.Create(m_hwnd, m_hwnd, 30);
    m_status.SetBorderTop(true);
    m_status.AddLabel(ID_STATUS_TEXT, 430, false, nullptr);
    m_status.AddSpacer();
    m_status.AddLabel(ID_STATUS_OP, 420, false, nullptr);
    m_status.SetRightAligned(ID_STATUS_OP, true);
    m_status.AddTextButton(ID_CANCEL_OPS, L"Cancel", L"Stop after the current service", 64);
    if (!m_elevated)
        m_status.AddTextButton(ID_ELEVATE, L"Restart as administrator",
                               L"Needed to start, stop or change services", 176);
    m_status.SetEnabled(ID_CANCEL_OPS, false);
    UpdateUi();
}

void MainWindow::CreateAccelerators() {
    ACCEL acc[] = {
        {FVIRTKEY, VK_F5, ID_REFRESH},
        {FCONTROL | FVIRTKEY, 'F', ID_FOCUS_SEARCH},
        {FCONTROL | FVIRTKEY, 'A', ID_SELECT_ALL},
        {FCONTROL | FVIRTKEY, 'C', ID_COPY_NAMES},
        {FCONTROL | FSHIFT | FVIRTKEY, 'C', ID_COPY_DETAILS},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_START},
        {FCONTROL | FSHIFT | FVIRTKEY, 'T', ID_STOP},
        {FCONTROL | FSHIFT | FVIRTKEY, 'R', ID_RESTART},
        {FCONTROL | FSHIFT | FVIRTKEY, 'K', ID_KILL},
        {FCONTROL | FVIRTKEY, VK_OEM_COMMA, ID_SETTINGS},
        {FVIRTKEY, VK_F1, ID_ABOUT},
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
    title += m_elevated ? L" (Administrator)" : L" (read-only: not administrator)";
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
        case WM_APP_SNAPSHOT: {
            std::unique_ptr<Snapshot> snap((Snapshot*)lp);
            m_listError = snap->error;
            if (!snap->error || !snap->services.empty()) m_list.SetData(std::move(snap->services));
            if (!m_loaded) {
                m_loaded = true;
                if (!m_running) m_opText.clear();
            }
            UpdateUi();
            return 0;
        }
        case WM_APP_OP_PROGRESS:
            OnProgress((OpProgress*)lp);
            return 0;
        case WM_APP_OP_DONE:
            OnDone((OpBatchResult*)lp);
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
                ShowContextMenu({rc.left + Dpi(40, GetWindowDpi(m_hwnd)), rc.top + Dpi(60, GetWindowDpi(m_hwnd))});
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp == kRefreshTimer) {
                // No refresh while minimised or while actions run (they refresh anyway).
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
            if (m_running &&
                MessageBoxW(m_hwnd,
                            L"Services are still being changed. Close anyway?\n\nThe service that is "
                            L"being changed right now will finish on its own.",
                            APP_NAME, MB_YESNO | MB_ICONWARNING) != IDYES)
                return 0;
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

LRESULT CALLBACK MainWindow::EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                                      DWORD_PTR ref) {
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
    switch (id) {
        case ID_REFRESH: m_worker.Refresh(); break;
        case ID_START: Act(OpKind::Start); break;
        case ID_STOP: Act(OpKind::Stop); break;
        case ID_RESTART: Act(OpKind::Restart); break;
        case ID_PAUSE: Act(OpKind::Pause); break;
        case ID_RESUME: Act(OpKind::Resume); break;
        case ID_KILL: Act(OpKind::Kill); break;
        case ID_SET_AUTOMATIC: Act(OpKind::SetStartMode, StartMode::Automatic); break;
        case ID_SET_DELAYED: Act(OpKind::SetStartMode, StartMode::AutomaticDelayed); break;
        case ID_SET_MANUAL: Act(OpKind::SetStartMode, StartMode::Manual); break;
        case ID_SET_DISABLED: Act(OpKind::SetStartMode, StartMode::Disabled); break;
        case ID_STARTTYPE_MENU: {
            const RECT r = m_toolbar.ItemScreenRect(ID_STARTTYPE_MENU);
            ShowStartTypeMenu({r.left, r.bottom});
            break;
        }
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
        case ID_OPEN_LOCATION: OpenLocation(); break;
        case ID_SEARCH_ONLINE: SearchOnline(); break;
        case ID_SETTINGS: ShowSettings(); break;
        case ID_ABOUT: ShowAbout(); break;
        case ID_ELEVATE:
            if (RelaunchElevated(m_hwnd)) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
        case ID_CANCEL_OPS:
            if (m_running) {
                m_worker.CancelActions();
                m_opText = L"Cancelling after the current service\x2026";
                UpdateStatus();
            }
            break;
        case ID_EXIT: PostMessageW(m_hwnd, WM_CLOSE, 0, 0); break;
    }
}

void MainWindow::UpdateUi() {
    const auto sel = m_list.Selected();
    bool start = false, stop = false, pause = false, resume = false, kill = false, config = false;
    for (const ServiceInfo* s : sel) {
        if (s->state == SERVICE_STOPPED) start = true;
        if (s->state != SERVICE_STOPPED) stop = true;
        if (s->state == SERVICE_RUNNING && (s->controls & SERVICE_ACCEPT_PAUSE_CONTINUE)) pause = true;
        if (s->state == SERVICE_PAUSED) resume = true;
        if (s->pid) kill = true;
        if (s->configKnown) config = true;
    }
    const bool idle = !m_running;
    m_toolbar.SetEnabled(ID_START, idle && start);
    m_toolbar.SetEnabled(ID_STOP, idle && stop);
    m_toolbar.SetEnabled(ID_RESTART, idle && stop);
    m_toolbar.SetEnabled(ID_PAUSE, idle && pause);
    m_toolbar.SetEnabled(ID_RESUME, idle && resume);
    m_toolbar.SetEnabled(ID_KILL, idle && kill);
    m_toolbar.SetEnabled(ID_STARTTYPE_MENU, idle && config);
    m_toolbar.SetText(ID_FILTER_MENU, std::wstring(FilterTitle(m_settings.filter)) + L"  \x25BE");
    m_toolbar.SetChecked(ID_FILTER_MENU, m_settings.filter != kFilterAll);
    UpdateStatus();
}

void MainWindow::UpdateStatus() {
    const auto& all = m_list.All();
    std::wstring text;
    if (m_listError && all.empty()) {
        text = L"The service list could not be read: " + Svc::ErrorText(m_listError);
    } else if (m_loaded) {
        int running = 0;
        for (const auto& s : all)
            if (s.state == SERVICE_RUNNING) ++running;
        text = std::to_wstring(all.size()) + L" services  \x00B7  " + std::to_wstring(running) +
               L" running";
        if (m_list.ShownCount() != (int)all.size())
            text += L"  \x00B7  " + std::to_wstring(m_list.ShownCount()) + L" shown";
        const size_t sel = m_list.Selected().size();
        if (sel > 1) text += L"  \x00B7  " + std::to_wstring(sel) + L" selected";
    }
    m_status.SetText(ID_STATUS_TEXT, text);
    m_status.SetText(ID_STATUS_OP, m_opText);
    m_status.SetEnabled(ID_CANCEL_OPS, m_running);
    m_status.SetText(ID_CANCEL_OPS, m_running ? L"Cancel" : L"");
}

// ===========================================================================
// Actions
// ===========================================================================
void MainWindow::Act(OpKind kind, StartMode mode) {
    if (m_running) return;
    std::vector<const ServiceInfo*> sel = m_list.Selected();
    if (sel.empty()) {
        m_opText = L"Select one or more services first.";
        UpdateStatus();
        return;
    }
    // Only the services the action makes sense for.
    std::vector<const ServiceInfo*> targets;
    for (const ServiceInfo* s : sel) {
        switch (kind) {
            case OpKind::Start:
                if (s->state != SERVICE_RUNNING) targets.push_back(s);
                break;
            case OpKind::Stop:
            case OpKind::Restart:
                if (s->state != SERVICE_STOPPED || kind == OpKind::Restart) targets.push_back(s);
                break;
            case OpKind::Pause:
                if (s->state == SERVICE_RUNNING) targets.push_back(s);
                break;
            case OpKind::Resume:
                if (s->state == SERVICE_PAUSED) targets.push_back(s);
                break;
            case OpKind::Kill:
                if (s->pid) targets.push_back(s);
                break;
            case OpKind::SetStartMode:
                if (s->configKnown) targets.push_back(s);
                break;
        }
    }
    if (targets.empty()) {
        m_opText = L"Nothing to do: the selected services are already in that state.";
        UpdateStatus();
        return;
    }

    // Critical services.
    std::vector<std::wstring> critical;
    for (const ServiceInfo* s : targets)
        if (s->safety == Safety::Critical) critical.push_back(s->displayName);
    const bool risky = Risky(kind, mode);
    if (risky && !critical.empty() && m_settings.protectCritical) {
        std::wstring msg = L"These services are critical to Windows. Stopping, killing or disabling "
                           L"them can make the PC unstable or stop it from starting:\n\n" +
                           NameList(critical) +
                           L"\nThey were left unchanged. To allow this anyway, turn off "
                           L"\x201CProtect critical services\x201D in Settings.";
        targets.erase(std::remove_if(targets.begin(), targets.end(),
                                     [](const ServiceInfo* s) { return s->safety == Safety::Critical; }),
                      targets.end());
        if (targets.empty()) {
            MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
            return;
        }
        msg += L"\n\nContinue with the other " + std::to_wstring(targets.size()) + L" service(s)?";
        if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONWARNING) != IDYES) return;
        critical.clear();
    }

    std::vector<std::wstring> names, displays;
    for (const ServiceInfo* s : targets) {
        names.push_back(s->name);
        displays.push_back(s->displayName);
    }

    // Confirmation, with the consequences spelled out.
    const bool confirm = m_settings.confirmActions && risky;
    if (confirm || !critical.empty() || kind == OpKind::Kill) {
        std::wstring verb = kind == OpKind::SetStartMode
                                ? std::wstring(L"Set the start type to ") + ModeTitle(mode) + L" for"
                                : std::wstring(Svc::OpVerb(kind));
        std::wstring msg = verb + (targets.size() == 1 ? L" this service?\n\n" : L" these " +
                                   std::to_wstring(targets.size()) + L" services?\n\n");
        msg += NameList(displays);
        if (kind == OpKind::Kill) {
            msg += L"\nKilling ends the process at once, without letting the service save its "
                   L"work. Windows may restart it if its recovery settings say so.";
            // Services sharing a process (svchost) all end together.
            std::set<std::wstring> chosen(names.begin(), names.end());
            std::vector<std::wstring> shared;
            for (const ServiceInfo* t : targets)
                for (const ServiceInfo& o : m_list.All())
                    if (o.pid == t->pid && !chosen.count(o.name) &&
                        std::find(shared.begin(), shared.end(), o.displayName) == shared.end())
                        shared.push_back(o.displayName);
            if (!shared.empty())
                msg += L"\n\nThe same process also runs these services, which will stop too:\n" +
                       NameList(shared);
        }
        if (!critical.empty())
            msg += L"\n\n\x26A0 Critical to Windows:\n" + NameList(critical) +
                   L"The PC may become unstable or fail to start.";
        if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME,
                        MB_YESNO | (critical.empty() && kind != OpKind::Kill ? MB_ICONQUESTION
                                                                              : MB_ICONWARNING) |
                            MB_DEFBUTTON2) != IDYES)
            return;
    }

    OpRequest req;
    req.kind = kind;
    req.mode = mode;
    req.names = std::move(names);
    req.displayNames = std::move(displays);
    req.timeoutMs = (DWORD)m_settings.timeoutSeconds * 1000;
    Launch(std::move(req));
}

void MainWindow::Launch(OpRequest&& request) {
    m_running = true;
    m_opText = std::wstring(Svc::OpProgressVerb(request.kind)) + L"\x2026";
    m_worker.Run(std::move(request));
    UpdateUi();
}

void MainWindow::OnProgress(OpProgress* p) {
    std::unique_ptr<OpProgress> progress(p);
    m_opText = std::wstring(Svc::OpProgressVerb(p->kind)) + L" " + p->displayName;
    if (p->total > 1)
        m_opText += L"  (" + std::to_wstring(p->index + 1) + L" of " + std::to_wstring(p->total) + L")";
    m_opText += L"\x2026";
    UpdateStatus();
}

void MainWindow::OnDone(OpBatchResult* r) {
    std::unique_ptr<OpBatchResult> res(r);
    m_running = false;
    const OpRequest& req = res->request;
    int ok = 0;
    std::vector<const OpOutcome*> failed;
    for (const OpOutcome& o : res->outcomes) {
        if (o.error == 0)
            ++ok;
        else if (o.error != ERROR_CANCELLED)
            failed.push_back(&o);
    }

    // Status line.
    const std::wstring done = req.kind == OpKind::SetStartMode
                                  ? std::wstring(L"set to ") + ModeTitle(req.mode)
                                  : PastTense(req.kind);
    if (res->outcomes.size() == 1 && ok == 1) {
        m_opText = res->outcomes[0].displayName + L" " + done + L".";
        if (!res->outcomes[0].note.empty()) m_opText += L" (" + res->outcomes[0].note + L")";
    } else {
        m_opText = std::to_wstring(ok) + L" of " + std::to_wstring(req.names.size()) + L" " + done;
        if (!failed.empty()) m_opText += L", " + std::to_wstring(failed.size()) + L" failed";
        if (res->cancelled) m_opText += L", cancelled";
        m_opText += L".";
    }
    UpdateUi();

    if (failed.empty()) return;

    // A single failure that can be fixed with one more step: offer it.
    if (failed.size() == 1 && res->outcomes.size() == 1) {
        const OpOutcome& f = *failed[0];
        if (req.kind == OpKind::Start && f.error == ERROR_SERVICE_DISABLED) {
            const std::wstring msg = f.displayName +
                                     L" is disabled.\n\nSet its start type to Manual and start it?";
            if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                OpRequest again = req;
                again.enableFirst = true;
                Launch(std::move(again));
            }
            return;
        }
        if ((req.kind == OpKind::Stop || req.kind == OpKind::Restart) &&
            f.error == ERROR_DEPENDENT_SERVICES_RUNNING) {
            std::wstring msg = L"These running services depend on " + f.displayName + L":\n\n" +
                               NameList(f.dependents) +
                               (req.kind == OpKind::Restart
                                    ? L"\nStop them first, restart " + f.displayName +
                                          L", and then start them again?"
                                    : L"\nStop them too?");
            if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                OpRequest again = req;
                again.withDependents = true;
                Launch(std::move(again));
            }
            return;
        }
    }

    std::wstring msg;
    if (res->outcomes.size() == 1) {
        msg = std::wstring(Svc::OpVerb(req.kind)) + L" did not work for " + failed[0]->displayName +
              L".\n\n" + Svc::ErrorText(failed[0]->error);
        if (!failed[0]->note.empty()) msg += L"\n(" + failed[0]->note + L")";
    } else {
        msg = std::to_wstring(failed.size()) + L" of " + std::to_wstring(req.names.size()) +
              L" services could not be " + done + L":\n\n";
        size_t n = 0;
        for (const OpOutcome* f : failed) {
            if (++n > 12) {
                msg += L"\x2026 and " + std::to_wstring(failed.size() - 12) + L" more";
                break;
            }
            msg += L"\x2022 " + f->displayName + L": " + Svc::ErrorText(f->error) + L"\n";
        }
    }
    MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
}

// ===========================================================================
// Menus
// ===========================================================================
HMENU MainWindow::CreateStartTypeMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_SET_AUTOMATIC, L"&Automatic");
    AppendMenuW(m, MF_STRING, ID_SET_DELAYED, L"Automatic (&delayed start)");
    AppendMenuW(m, MF_STRING, ID_SET_MANUAL, L"&Manual");
    AppendMenuW(m, MF_STRING, ID_SET_DISABLED, L"D&isabled");
    // Show the current type when every selected service has the same one.
    const auto sel = m_list.Selected();
    int current = -1;
    for (const ServiceInfo* s : sel) {
        const int id = s->startType == SERVICE_AUTO_START   ? (s->delayed ? ID_SET_DELAYED : ID_SET_AUTOMATIC)
                       : s->startType == SERVICE_DEMAND_START ? ID_SET_MANUAL
                       : s->startType == SERVICE_DISABLED     ? ID_SET_DISABLED
                                                              : 0;
        if (current == -1) current = id;
        else if (current != id) current = 0;
    }
    if (current > 0) CheckMenuRadioItem(m, ID_SET_AUTOMATIC, ID_SET_DISABLED, current, MF_BYCOMMAND);
    return m;
}

void MainWindow::ShowStartTypeMenu(POINT pt) {
    HMENU m = CreateStartTypeMenu();
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowFilterMenu() {
    HMENU m = CreatePopupMenu();
    for (int f = 0; f < kFilterCount; ++f) {
        if (f == kFilterAutoStopped || f == kFilterThirdParty) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_FILTER_FIRST + f, FilterTitle(f));
    }
    CheckMenuRadioItem(m, ID_FILTER_FIRST, ID_FILTER_FIRST + kFilterCount - 1,
                       ID_FILTER_FIRST + m_settings.filter, MF_BYCOMMAND);
    const RECT r = m_toolbar.ItemScreenRect(ID_FILTER_MENU);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowMoreMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_REFRESH, L"&Refresh\tF5");
    AppendMenuW(m, MF_STRING, ID_SELECT_ALL, L"Select &all\tCtrl+A");
    AppendMenuW(m, MF_STRING | (m_list.Selected().empty() ? MF_GRAYED : 0), ID_COPY_DETAILS,
                L"&Copy details\tCtrl+Shift+C");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    if (!m_elevated) AppendMenuW(m, MF_STRING, ID_ELEVATE, L"Restart as a&dministrator");
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"&Settings\x2026\tCtrl+,");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"A&bout " APP_NAME L"\tF1");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");
    const RECT r = m_toolbar.ItemScreenRect(ID_MORE_MENU);
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowContextMenu(POINT screen) {
    const auto sel = m_list.Selected();
    if (sel.empty()) return;
    bool start = false, stop = false, pause = false, resume = false, kill = false, config = false;
    for (const ServiceInfo* s : sel) {
        if (s->state == SERVICE_STOPPED) start = true;
        if (s->state != SERVICE_STOPPED) stop = true;
        if (s->state == SERVICE_RUNNING && (s->controls & SERVICE_ACCEPT_PAUSE_CONTINUE)) pause = true;
        if (s->state == SERVICE_PAUSED) resume = true;
        if (s->pid) kill = true;
        if (s->configKnown) config = true;
    }
    auto en = [this](bool on) -> UINT { return on && !m_running ? MF_ENABLED : MF_GRAYED; };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | en(start), ID_START, L"&Start\tCtrl+Shift+S");
    AppendMenuW(m, MF_STRING | en(stop), ID_STOP, L"S&top\tCtrl+Shift+T");
    AppendMenuW(m, MF_STRING | en(stop), ID_RESTART, L"&Restart\tCtrl+Shift+R");
    AppendMenuW(m, MF_STRING | en(pause), ID_PAUSE, L"&Pause");
    AppendMenuW(m, MF_STRING | en(resume), ID_RESUME, L"Res&ume");
    AppendMenuW(m, MF_STRING | en(kill), ID_KILL, L"&Kill process\tCtrl+Shift+K");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_POPUP | en(config), (UINT_PTR)CreateStartTypeMenu(), L"Start t&ype");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COPY_NAMES, L"&Copy name\tCtrl+C");
    AppendMenuW(m, MF_STRING, ID_COPY_DETAILS, L"Copy &details\tCtrl+Shift+C");
    const ServiceInfo* f = m_list.Focused();
    AppendMenuW(m, MF_STRING | (f && FileExists(f->imagePath) ? 0 : MF_GRAYED), ID_OPEN_LOCATION,
                L"Open file &location");
    AppendMenuW(m, MF_STRING, ID_SEARCH_ONLINE, L"Search &online");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

// ===========================================================================
// Clipboard, Explorer, web
// ===========================================================================
void MainWindow::CopyNames() {
    std::wstring text;
    for (const ServiceInfo* s : m_list.Selected()) {
        if (!text.empty()) text += L"\r\n";
        text += s->name;
    }
    CopyToClipboard(m_hwnd, text);
}

void MainWindow::CopyDetails() {
    const auto sel = m_list.Selected();
    if (sel.empty()) return;
    std::wstring text;
    for (int c = 0; c < kColumnCount; ++c) text += std::wstring(c ? L"\t" : L"") + ServiceList::ColumnTitle(c);
    for (const ServiceInfo* s : sel) {
        text += L"\r\n";
        for (int c = 0; c < kColumnCount; ++c) {
            std::wstring cell = ServiceList::CellText(*s, c);
            for (wchar_t& ch : cell)
                if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
            text += (c ? L"\t" : L"") + cell;
        }
    }
    CopyToClipboard(m_hwnd, text);
    m_opText = L"Copied " + std::to_wstring(sel.size()) + L" row(s) (paste into Excel or Notepad).";
    UpdateStatus();
}

void MainWindow::OpenLocation() {
    const ServiceInfo* s = m_list.Focused();
    if (!s || !FileExists(s->imagePath)) return;
    const std::wstring args = L"/select,\"" + s->imagePath + L"\"";
    ShellExecuteW(m_hwnd, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void MainWindow::SearchOnline() {
    const ServiceInfo* s = m_list.Focused();
    if (!s) return;
    const std::wstring query = L"\"" + s->name + L"\" " + s->displayName + L" Windows service";
    std::wstring url = L"https://www.bing.com/search?q=";
    for (wchar_t c : query) {
        if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.') {
            url += c;
        } else {
            // Percent-encode the UTF-8 bytes.
            char utf8[8];
            const int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), nullptr, nullptr);
            for (int i = 0; i < n; ++i) {
                wchar_t hex[4];
                swprintf_s(hex, L"%%%02X", (unsigned char)utf8[i]);
                url += hex;
            }
        }
    }
    ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ===========================================================================
// Settings, About, theme
// ===========================================================================
void MainWindow::ShowSettings() {
    Settings edited = m_settings;
    bool resetColumns = false;
    if (!ShowSettingsDialog(m_inst, m_hwnd, edited, resetColumns)) return;
    const bool themeChanged = edited.themeMode != m_settings.themeMode;
    m_settings.themeMode = edited.themeMode;
    m_settings.refreshSeconds = edited.refreshSeconds;
    m_settings.timeoutSeconds = edited.timeoutSeconds;
    m_settings.confirmActions = edited.confirmActions;
    m_settings.protectCritical = edited.protectCritical;
    m_settings.alwaysElevate = edited.alwaysElevate;
    if (resetColumns) m_list.ResetColumns();
    SetRefreshTimer();
    if (themeChanged) ApplyTheme();
    m_settings.Save();
}

void MainWindow::ShowAbout() {
    const std::wstring text =
        std::wstring(APP_NAME L"  " APP_VERSION L"\n\n" APP_COPYRIGHT L".\n"
                     L"Developed for faster experience.\n\n"
                     L"View, start, stop, restart, pause, resume and kill Windows services, and "
                     L"change how they start, with protection for services Windows needs.\n\n"
                     L"Running as: ") +
        (m_elevated ? L"Administrator" : L"standard user (read-only)");
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
