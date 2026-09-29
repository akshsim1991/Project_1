// MainWindow.h - top-level window: owns the tab strip, toolbar, search bar,
// one page view per tab, the render worker and settings, and wires user
// commands to them.
#pragma once
#include <memory>

#include "PdfView.h"
#include "RenderWorker.h"
#include "Search.h"
#include "Settings.h"
#include "TabBar.h"
#include "Toolbar.h"

// COPYDATASTRUCT::dwData used when a second instance forwards a file.
constexpr ULONG_PTR kCopyDataOpenFile = 0x46505046;  // "FPPF"

class MainWindow {
public:
    bool Create(HINSTANCE inst, int showCmd, const std::wstring& file, int page);
    HWND Hwnd() const { return m_hwnd; }
    HACCEL Accelerators() const { return m_accel; }

private:
    // One open document (or an empty "new tab").
    struct Tab {
        std::unique_ptr<PdfView> view;
        SearchState search;
        std::wstring path;         // file shown (or being opened)
        uint32_t docId = 0;        // 0 = no document loaded
        uint32_t pendingDocId = 0; // open in progress
        int pendingPage = 0;
        int passwordAttempts = 0;
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    void CreateChildren();
    void CreateAccelerators();
    void Layout();
    void OnCommand(int id, int code, HWND ctl);
    void UpdateUi();
    void UpdateTitle();

    // tabs
    Tab& Active() { return *m_tabs[(size_t)m_active]; }
    PdfView& View() { return *Active().view; }
    int NewTab();
    void ActivateTab(int index);
    void CloseTab(int index);
    void UpdateTabs();
    int TabByDocId(uint32_t docId) const;
    void OnTabBar(TabAction action, int index);

    // documents
    void OpenFile(const std::wstring& path, int page, const std::string& password = {},
                  int tabIndex = -1, bool activate = true);
    void ShowOpenDialog();
    void OnDocLoaded(DocLoadResult* res);
    void OnCopyData(const COPYDATASTRUCT* cds);

    // search
    void ShowSearch(bool show);
    void StartSearch();
    void CancelSearch();
    void FindNext(bool forward);
    void OnSearchResult(SearchPageResult* res);
    void UpdateSearchStatus();

    // misc
    void OnEditKey(HWND edit, WPARAM key);
    void GoToPageFromEdit();
    void ShowMoreMenu();
    void ShowZoomMenu();
    void ToggleFullscreen();
    void SetThemeMode(int mode);
    void OnThemeChanged();
    void CopyToClipboard(const std::wstring& text);
    void SaveSettings();

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HACCEL m_accel = nullptr;
    TabBar m_tabBar;
    Toolbar m_toolbar;
    Toolbar m_searchBar;
    HWND m_pageEdit = nullptr;
    HWND m_searchEdit = nullptr;
    HWND m_lastFocus = nullptr;
    RenderWorker m_worker;
    Settings m_settings;
    bool m_searchVisible = false;

    std::vector<std::unique_ptr<Tab>> m_tabs;
    int m_active = -1;
    uint32_t m_nextDocId = 1;
    uint32_t m_nextSearchId = 1;

    // full screen
    bool m_fullscreen = false;
    WINDOWPLACEMENT m_restorePlacement{sizeof(WINDOWPLACEMENT)};
};
