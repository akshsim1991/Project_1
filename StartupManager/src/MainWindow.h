// MainWindow.h - the top-level window: toolbar, entry table, status bar,
// menus and the change flow (confirmation, results, undo).
#pragma once
#include "EntryView.h"
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
    void UpdateUi();
    void UpdateStatus();
    void UpdateTitle();

    // changes
    void SetEnabled(bool enable);
    void DeleteSelected();
    void AddEntry();
    void RestoreDeleted();
    void Launch(OpRequest&& request);
    void OnDone(OpResult* r);
    void Undo();

    // menus and dialogs
    void ShowFilterMenu();
    void ShowMoreMenu();
    void ShowContextMenu(POINT screen);
    void ShowSettings();
    void ShowAbout();
    void ShowDetails();
    void CopyNames();
    void CopyDetails();

    void SetRefreshTimer();
    void ApplyTheme();

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HACCEL m_accel = nullptr;
    Settings m_settings;
    Toolbar m_toolbar;
    Toolbar m_status;
    HWND m_search = nullptr;
    EntryView m_list;
    Worker m_worker;
    bool m_elevated = false;
    bool m_loaded = false;          // first list arrived
    bool m_running = false;         // a change is in progress
    std::wstring m_opText;          // shown in the status bar
    std::vector<std::wstring> m_readErrors;

    // Changes that can be undone (newest last).
    struct UndoStep {
        OpKind kind;                         // what was done
        std::vector<StartupEntry> entries;   // Enable/Disable: the entries
        std::vector<std::wstring> backups;   // Delete: backup folders
        std::wstring addedName;              // Add: what was created
        bool addedAllUsers = false, addedShortcut = false;
    };
    std::vector<UndoStep> m_undo;
};
