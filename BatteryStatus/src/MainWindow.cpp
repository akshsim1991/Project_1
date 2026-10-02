// MainWindow.cpp - window, tray, polling, alerts and commands.
#include "MainWindow.h"

#include <algorithm>

#include <commdlg.h>
#include <shellapi.h>

#include "Dialogs.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr UINT_PTR kPollTimer = 1;     // percentage, state (every few seconds)
constexpr UINT_PTR kDetailTimer = 2;   // driver details (every 30 s)
constexpr UINT_PTR kRedrawTimer = 3;   // durations in the window (every 30 s)

int HoursFor(int id) {
    switch (id) {
        case ID_RANGE_1H: return 1;
        case ID_RANGE_6H: return 6;
        case ID_RANGE_7D: return 168;
        default: return 24;
    }
}
}  // namespace

// ===========================================================================
// Creation
// ===========================================================================
bool MainWindow::Create(HINSTANCE inst, bool startHidden) {
    m_inst = inst;
    ReloadTheme((ThemeMode)m_settings.themeMode);
    m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

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

    const int dpi = GetWindowDpi(nullptr);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = std::min(Dpi(900, dpi), (int)(work.right - work.left) * 9 / 10);
    const int h = std::min(Dpi(760, dpi), (int)(work.bottom - work.top) * 9 / 10);
    m_hwnd = CreateWindowExW(0, APP_WINDOW_CLASS, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                             CW_USEDEFAULT, w, h, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    m_dpi = GetWindowDpi(m_hwnd);

    m_toolbar.Create(m_hwnd, m_hwnd, 42);
    m_toolbar.AddTextButton(ID_RANGE_1H, L"1 hour", L"Show the last hour in the graph", 64);
    m_toolbar.AddTextButton(ID_RANGE_6H, L"6 hours", L"Show the last 6 hours", 66);
    m_toolbar.AddTextButton(ID_RANGE_24H, L"24 hours", L"Show the last 24 hours", 72);
    m_toolbar.AddTextButton(ID_RANGE_7D, L"7 days", L"Show the last 7 days", 62);
    m_toolbar.AddSpacer();
    m_toolbar.AddTextButton(ID_REPORT, L"Battery report", L"Windows' detailed battery report (capacity history, usage)", 112);
    m_toolbar.AddTextButton(ID_EXPORT, L"Export\x2026", L"Save the battery history as a CSV file for Excel", 70);
    m_toolbar.AddTextButton(ID_MORE_MENU, L"More", L"Settings, alerts, About", 52);
    m_dashboard.Create(m_hwnd);

    if (m_settings.hasPlacement) {
        WINDOWPLACEMENT wp = m_settings.placement;
        wp.flags = 0;
        wp.showCmd = SW_HIDE;
        SetWindowPlacement(m_hwnd, &wp);
    }
    ApplyWindowTheme(m_hwnd);
    m_history.Load(m_settings.keepDays, Battery::Demo());
    m_tray.Add(m_hwnd, WM_APP_TRAY);
    Poll(true);
    UpdateRangeButtons();
    SetTimers();
    SetTimer(m_hwnd, kDetailTimer, 30 * 1000, nullptr);
    SetTimer(m_hwnd, kRedrawTimer, 30 * 1000, nullptr);
    if (!startHidden) {
        ShowWindow(m_hwnd, m_settings.hasPlacement && m_settings.placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED
                                                                                                       : SW_SHOWNORMAL);
        UpdateWindow(m_hwnd);
        AskAutostartOnce();
    }
    return true;
}

void MainWindow::SetTimers() {
    SetTimer(m_hwnd, kPollTimer, (UINT)std::max(1, m_settings.refreshSeconds) * 1000, nullptr);
}

void MainWindow::AskAutostartOnce() {
    if (m_settings.askedAutostart) return;
    m_settings.askedAutostart = true;
    m_settings.Save();
    if (AutostartEnabled()) return;
    std::wstring msg = L"Start " APP_NAME L" with Windows?\n\nIt then waits quietly in the notification area and warns "
                       L"you when the battery is low or charged.";
    if (OldVersionAutostart()) msg += L"\n\nThis also replaces the start-up entry of Battery Status 1.2.";
    if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) SetAutostart(true);
}

void MainWindow::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int th = m_toolbar.Height();
    MoveWindow(m_toolbar.Hwnd(), 0, 0, rc.right, th, TRUE);
    MoveWindow(m_dashboard.Hwnd(), 0, th, rc.right, std::max(0, (int)rc.bottom - th), TRUE);
}

void MainWindow::UpdateRangeButtons() {
    for (int id : {ID_RANGE_1H, ID_RANGE_6H, ID_RANGE_24H, ID_RANGE_7D})
        m_toolbar.SetChecked(id, HoursFor(id) == m_settings.graphHours);
}

// ===========================================================================
// Polling
// ===========================================================================
void MainWindow::Poll(bool details) {
    PowerSnapshot fresh;
    Battery::Read(fresh, details || m_power.batteries.empty());
    // Keep the last driver details between detail reads.
    if (!details && fresh.batteries.empty() && !Battery::Demo()) fresh.batteries = m_power.batteries;
    m_power = std::move(fresh);
    const int64_t now = History::Now();
    if (m_power.hasBattery && m_power.percent >= 0) {
        Sample s;
        s.time = now;
        s.percent = m_power.percent;
        s.ac = m_power.onAc;
        s.charging = m_power.charging;
        s.rate = m_power.RateKnown() ? m_power.Rate() : 0;
        m_history.Record(s);
    }
    m_summary = Summarize(m_power, m_history, m_settings, now, m_alerts.PausedUntil());
    m_tray.Update(m_power.hasBattery, m_power.percent, m_power.charging, m_power.onAc, m_settings.lowPercent,
                  m_settings.trayStyle, m_summary.Tooltip());
    if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd))
        m_dashboard.SetData(m_summary, &m_history, m_settings.graphHours, m_settings.lowPercent, m_settings.fullPercent,
                            m_settings.lowAlert, m_settings.fullAlert);
    std::wstring title = APP_NAME;
    if (m_power.hasBattery) title = std::to_wstring(m_power.percent) + L"% \x2014 " APP_NAME;
    if (Battery::Demo()) title += L" (demo)";
    SetWindowTextW(m_hwnd, title.c_str());
    HandleAlerts();
}

void MainWindow::HandleAlerts() {
    const std::vector<AlertEvent> events = m_alerts.Update(m_power, m_settings, History::Now());
    for (const AlertEvent& e : events) {
        m_tray.Notify(e.title, e.text, e.urgent);
        PlayAlertSound(m_settings.sound, m_settings.soundFile, e.urgent);
        if (e.urgent && m_settings.messageBox)
            MessageBoxW(nullptr, (e.title + L"\n\n" + e.text).c_str(), APP_NAME,
                        MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL | MB_SETFOREGROUND);
    }
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
    if (msg == m_taskbarCreated && m_taskbarCreated) {
        m_tray.Readd();
        return 0;
    }
    switch (msg) {
        case WM_COMMAND:
            OnCommand(LOWORD(wp));
            return 0;
        case WM_TIMER:
            if (wp == kPollTimer) Poll(false);
            else if (wp == kDetailTimer) Poll(true);
            else if (wp == kRedrawTimer && IsWindowVisible(m_hwnd)) InvalidateRect(m_dashboard.Hwnd(), nullptr, FALSE);
            return 0;
        case WM_POWERBROADCAST:
            // Plugged in or out, percentage changed, or back from sleep: read at once.
            if (wp == PBT_APMPOWERSTATUSCHANGE || wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND)
                Poll(true);
            return TRUE;
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                case WM_LBUTTONUP:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    if (IsWindowVisible(m_hwnd) && !IsIconic(m_hwnd) && GetForegroundWindow() == m_hwnd) HideToTray();
                    else ShowWindowFromTray();
                    break;
                case WM_CONTEXTMENU:
                case WM_RBUTTONUP: ShowTrayMenu(); break;
                case NIN_BALLOONUSERCLICK: ShowWindowFromTray(); break;
            }
            return 0;
        case WM_APP_SHOW:
            ShowWindowFromTray();
            return 0;
        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) {
                // Like version 1.2: minimising hides the window to the tray.
                HideToTray();
                return 0;
            }
            Layout();
            if (IsWindowVisible(m_hwnd))
                m_dashboard.SetData(m_summary, &m_history, m_settings.graphHours, m_settings.lowPercent,
                                    m_settings.fullPercent, m_settings.lowAlert, m_settings.fullAlert);
            return 0;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lp)->ptMinTrackSize = {Dpi(700, m_dpi), Dpi(520, m_dpi)};
            return 0;
        case WM_DPICHANGED: {
            m_dpi = HIWORD(wp);
            m_toolbar.OnDpiChanged();
            const RECT* r = (const RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && lstrcmpW((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
                if (m_settings.themeMode == 0) ApplyTheme();
                Poll(false);  // the taskbar colour may have changed: redraw the tray icon
            }
            break;
        case WM_CLOSE:
            if (m_settings.closeToTray && !m_quitting) {
                HideToTray();
                if (!m_settings.toldAboutTray) {
                    m_settings.toldAboutTray = true;
                    m_settings.Save();
                    m_tray.Notify(APP_NAME L" is still running",
                                  L"It keeps watching the battery here. Right-click the icon to exit.", false);
                }
                return 0;
            }
            {
                WINDOWPLACEMENT wp{sizeof(wp)};
                if (GetWindowPlacement(m_hwnd, &wp)) {
                    if (wp.showCmd != SW_SHOWMAXIMIZED) wp.showCmd = SW_SHOWNORMAL;
                    m_settings.placement = wp;
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
            KillTimer(m_hwnd, kPollTimer);
            KillTimer(m_hwnd, kDetailTimer);
            KillTimer(m_hwnd, kRedrawTimer);
            m_tray.Remove();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ===========================================================================
// Tray and window visibility
// ===========================================================================
void MainWindow::ShowWindowFromTray() {
    if (!IsWindowVisible(m_hwnd)) ShowWindow(m_hwnd, SW_SHOW);
    if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    SetForegroundWindow(m_hwnd);
    Poll(false);
    AskAutostartOnce();
}

void MainWindow::HideToTray() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (GetWindowPlacement(m_hwnd, &wp) && wp.showCmd != SW_SHOWMINIMIZED) {
        m_settings.placement = wp;
        m_settings.hasPlacement = true;
    }
    ShowWindow(m_hwnd, SW_HIDE);
    if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE), ShowWindow(m_hwnd, SW_HIDE);
}

void MainWindow::ShowTrayMenu() {
    const int64_t now = History::Now();
    HMENU m = CreatePopupMenu();
    std::wstring head = m_summary.hasBattery ? std::to_wstring(m_summary.percent) + L"%  \x00B7  " + m_summary.state
                                             : std::wstring(L"No battery");
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, head.c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_OPEN, IsWindowVisible(m_hwnd) ? L"&Hide the window" : L"&Open " APP_NAME);
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    if (m_alerts.Paused(now))
        AppendMenuW(m, MF_STRING, ID_RESUME_ALERTS, (L"&Resume alerts (paused until " + FormatClock(m_alerts.PausedUntil()) + L")").c_str());
    else
        AppendMenuW(m, MF_STRING, ID_PAUSE_ALERTS, L"&Pause alerts for 1 hour");
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
    DestroyMenu(m);
}

void MainWindow::ShowMoreMenu() {
    const int64_t now = History::Now();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"&Settings\x2026");
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with &Windows");
    if (m_alerts.Paused(now))
        AppendMenuW(m, MF_STRING, ID_RESUME_ALERTS, (L"&Resume alerts (paused until " + FormatClock(m_alerts.PausedUntil()) + L")").c_str());
    else
        AppendMenuW(m, MF_STRING, ID_PAUSE_ALERTS, L"&Pause alerts for 1 hour");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COPY_SUMMARY, L"&Copy a summary");
    AppendMenuW(m, MF_STRING, ID_POWER_OPTIONS, L"Windows &power and battery settings");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_HIDE, L"&Hide to the tray");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"&About " APP_NAME);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");
    const RECT r = m_toolbar.ItemScreenRect(ID_MORE_MENU);
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_TOPALIGN, r.right, r.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

// ===========================================================================
// Commands
// ===========================================================================
void MainWindow::OnCommand(int id) {
    switch (id) {
        case ID_RANGE_1H:
        case ID_RANGE_6H:
        case ID_RANGE_24H:
        case ID_RANGE_7D:
            m_settings.graphHours = HoursFor(id);
            UpdateRangeButtons();
            m_dashboard.SetData(m_summary, &m_history, m_settings.graphHours, m_settings.lowPercent,
                                m_settings.fullPercent, m_settings.lowAlert, m_settings.fullAlert);
            m_settings.Save();
            break;
        case ID_REPORT: BatteryReport(); break;
        case ID_EXPORT: ExportHistory(); break;
        case ID_MORE_MENU: ShowMoreMenu(); break;
        case ID_SETTINGS: ShowSettings(); break;
        case ID_ABOUT: ShowAbout(); break;
        case ID_OPEN:
            if (IsWindowVisible(m_hwnd)) HideToTray();
            else ShowWindowFromTray();
            break;
        case ID_HIDE: HideToTray(); break;
        case ID_PAUSE_ALERTS:
            m_alerts.PauseUntil(History::Now() + 3600);
            Poll(false);
            break;
        case ID_RESUME_ALERTS:
            m_alerts.PauseUntil(0);
            Poll(false);
            break;
        case ID_AUTOSTART:
            if (!SetAutostart(!AutostartEnabled()))
                MessageBoxW(m_hwnd, L"The start-up setting could not be changed.", APP_NAME, MB_ICONWARNING);
            m_settings.askedAutostart = true;
            m_settings.Save();
            break;
        case ID_COPY_SUMMARY: CopyToClipboard(m_hwnd, m_summary.Text()); break;
        case ID_POWER_OPTIONS:
            if ((INT_PTR)ShellExecuteW(m_hwnd, L"open", L"ms-settings:batterysaver", nullptr, nullptr, SW_SHOWNORMAL) <= 32)
                ShellExecuteW(m_hwnd, L"open", L"control.exe", L"powercfg.cpl", nullptr, SW_SHOWNORMAL);
            break;
        case ID_EXIT:
            m_quitting = true;
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
    }
}

void MainWindow::ShowSettings() {
    if (!IsWindowVisible(m_hwnd)) ShowWindowFromTray();
    Settings edited = m_settings;
    bool autostart = AutostartEnabled();
    const bool wasAutostart = autostart;
    if (!ShowSettingsDialog(m_inst, m_hwnd, edited, autostart)) return;
    const bool themeChanged = edited.themeMode != m_settings.themeMode;
    const bool keepChanged = edited.keepDays < m_settings.keepDays;
    edited.placement = m_settings.placement;
    edited.hasPlacement = m_settings.hasPlacement;
    m_settings = edited;
    m_settings.askedAutostart = true;
    m_settings.Save();
    if (autostart != wasAutostart && !SetAutostart(autostart))
        MessageBoxW(m_hwnd, L"The start-up setting could not be changed.", APP_NAME, MB_ICONWARNING);
    if (keepChanged) m_history.Load(m_settings.keepDays, Battery::Demo());
    if (themeChanged) ApplyTheme();
    SetTimers();
    Poll(false);
}

void MainWindow::ShowAbout() {
    const std::wstring text =
        APP_NAME L"  " APP_VERSION L"\n\n" APP_COPYRIGHT L".\n"
                 L"Developed for faster experience.\n\n"
                 L"Watches your laptop battery: level, time left, real battery health, charge cycles and history, "
                 L"with alerts when it is low or charged and a live icon in the notification area.";
    MessageBoxW(IsWindowVisible(m_hwnd) ? m_hwnd : nullptr, text.c_str(), L"About " APP_NAME, MB_OK | MB_ICONINFORMATION);
}

void MainWindow::BatteryReport() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring out = std::wstring(tmp) + L"battery-report.html";
    DeleteFileW(out.c_str());
    std::wstring cmd = L"powercfg.exe /batteryreport /output \"" + out + L"\"";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    bool ok = false;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        ok = FileExists(out);
    }
    SetCursor(old);
    if (ok) ShellExecuteW(m_hwnd, L"open", out.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else
        MessageBoxW(m_hwnd,
                    L"Windows could not create the battery report. It needs a laptop battery and "
                    L"\x201Cpowercfg\x201D, which is part of Windows.",
                    APP_NAME, MB_ICONWARNING);
}

void MainWindow::ExportHistory() {
    wchar_t buf[MAX_PATH] = L"Battery history.csv";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"CSV file (*.csv)\0*.csv\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return;
    if (!m_history.Export(buf)) MessageBoxW(m_hwnd, L"The file could not be saved.", APP_NAME, MB_ICONWARNING);
}

void MainWindow::ApplyTheme() {
    ReloadTheme((ThemeMode)m_settings.themeMode);
    ApplyWindowTheme(m_hwnd);
    m_toolbar.OnThemeChanged();
    m_dashboard.OnThemeChanged();
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}
