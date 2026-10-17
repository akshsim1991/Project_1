// MainWindow.h - the window, the tray icon, polling, alerts and commands.
#pragma once
#include "AlertBox.h"
#include "Alerts.h"
#include "Dashboard.h"
#include "History.h"
#include "Settings.h"
#include "Toolbar.h"
#include "TrayIcon.h"

class MainWindow {
public:
    bool Create(HINSTANCE inst, bool startHidden);
    HWND Hwnd() const { return m_hwnd; }
    Settings& GetSettings() { return m_settings; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void OnCommand(int id);
    void Layout();
    void Poll(bool details);
    int MeasuredRate();  // mW from the change in stored energy (0 = not yet known)
    void HandleAlerts();
    void UpdateRangeButtons();
    void ShowWindowFromTray();
    void HideToTray();
    void ShowTrayMenu();
    void ShowMoreMenu();
    void ShowSettings();
    void ShowAbout();
    void BatteryReport();
    void ExportHistory();
    void ApplyTheme();
    void SetTimers();
    void AskAutostartOnce();

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    Settings m_settings;
    Toolbar m_toolbar;
    Dashboard m_dashboard;
    TrayIcon m_tray;
    AlertBox m_alertBox;
    History m_history;
    Alerts m_alerts;
    PowerSnapshot m_power;
    Summary m_summary;
    // Stored energy over the last minutes, for batteries that do not report
    // their power: {time, mWh, on charger}.
    struct EnergySample {
        int64_t time;
        unsigned mWh;
        bool ac;
    };
    std::vector<EnergySample> m_energy;
    UINT m_taskbarCreated = 0;
    int m_dpi = 96;
    bool m_quitting = false;
};
