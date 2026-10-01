// MainWindow.h - the top-level window: toolbar, service table, status bar,
// menus and the action flow (checks, confirmation, progress, results).
#pragma once
#include "ServiceList.h"
#include "Settings.h"
#include "Toolbar.h"
#include "Worker.h"

class MainWindow {
public:
    bool Create(HINSTANCE inst, int showCmd);
    HWND Hwnd() const { return m_hwnd; }
    HACCEL Accelerators() const { return m_accel; }
    Settings& GetSettings() { return m_settings; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    void CreateChildren();
    void CreateAccelerators();
    void Layout();
    void OnCommand(int id, int code, HWND ctl);
    void UpdateUi();       // toolbar enabled states, status text
    void UpdateStatus();
    void UpdateTitle();

    // actions
    void Act(OpKind kind, StartMode mode = StartMode::Manual);
    void Launch(OpRequest&& request);
    void OnProgress(OpProgress* p);
    void OnDone(OpBatchResult* r);

    // menus and dialogs
    void ShowStartTypeMenu(POINT pt);
    HMENU CreateStartTypeMenu();
    void ShowFilterMenu();
    void ShowMoreMenu();
    void ShowContextMenu(POINT screen);
    void ShowSettings();
    void ShowAbout();
    void CopyNames();
    void CopyDetails();
    void OpenLocation();
    void SearchOnline();

    void SetRefreshTimer();
    void ApplyTheme();

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HACCEL m_accel = nullptr;
    Settings m_settings;
    Toolbar m_toolbar;
    Toolbar m_status;
    HWND m_search = nullptr;
    ServiceList m_list;
    Worker m_worker;
    bool m_elevated = false;
    bool m_loaded = false;        // first snapshot arrived
    DWORD m_listError = 0;
    bool m_running = false;       // a batch of actions is in progress
    std::wstring m_opText;        // progress / last result shown in the status bar
};
