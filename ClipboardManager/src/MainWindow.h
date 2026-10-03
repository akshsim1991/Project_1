// MainWindow.h - the window: search and filters, the history list, the
// preview, the tray icon, the shortcut, and recording the clipboard.
#pragma once
#include "ItemList.h"
#include "Preview.h"
#include "Settings.h"
#include "Store.h"
#include "Toolbar.h"
#include "TrayIcon.h"

class MainWindow {
public:
    bool Create(HINSTANCE inst, bool startHidden);
    HWND Hwnd() const { return m_hwnd; }
    Settings& GetSettings() { return m_settings; }
    // Keyboard handling before the message is dispatched; true: handled.
    bool PreTranslate(MSG& msg);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK ForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void OnCommand(int id);
    LRESULT OnNotify(NMHDR* nm);
    void Layout();
    RECT SplitterRect() const;

    // Recording
    void CaptureClipboard();
    void Cleanup();

    // The list
    void Refresh(bool selectFirst = false);
    std::vector<int64_t> SelectedIds() const;
    const ClipItem* FocusedItem() const;
    void UpdatePreview();
    void UpdateStatus();
    void Flash(const std::wstring& text);  // a short message in the status bar

    // Actions
    bool CopyItem(int64_t id);
    bool CopySelected(bool plainText);
    void Activate();  // Enter or double-click: paste, or copy
    void TogglePin();
    void DeleteSelected();
    void ClearUnpinned();
    void SaveAs();
    void ShowInFolder();
    void OpenFolder();
    void SetFilter(int filter);
    void SetPaused(bool paused);

    // Menus and windows
    void ShowFilterMenu();
    void ShowMoreMenu();
    void ShowContextMenu(POINT pt);
    void ShowTrayMenu();
    void ShowWindowFromTray();
    void ShowForPaste();  // the shortcut
    void HideToTray();
    void RegisterShortcut(bool complain);
    void ShowSettings();
    void ShowAbout();
    void ApplyTheme();
    void AskAutostartOnce();

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_search = nullptr;
    Settings m_settings;
    Store m_store;
    std::wstring m_storeNote;
    Toolbar m_toolbar;
    Toolbar m_status;
    ItemList m_list;
    Preview m_preview;
    TrayIcon m_tray;
    HICON m_smallIcon = nullptr;
    HWINEVENTHOOK m_foregroundHook = nullptr;

    std::vector<int64_t> m_viewIds;  // the ids of the rows shown, in order
    std::vector<std::pair<int64_t, int64_t>> m_renamed;  // ids changed by copying again: {old, new}

    HWND m_lastApp = nullptr;      // where Enter pastes: the program used before this window
    HWND m_pasteTarget = nullptr;
    int m_pasteTries = 0;
    DWORD m_lastSequence = 0;      // clipboard change already handled
    int m_captureTries = 0;
    bool m_paused = false;
    bool m_hotkeyOn = false;
    bool m_splitting = false;
    std::wstring m_flash;
    UINT m_taskbarCreated = 0;
    int m_dpi = 96;
    bool m_quitting = false;
};
