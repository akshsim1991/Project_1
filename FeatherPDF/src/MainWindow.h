// MainWindow.h - top-level window: owns the toolbar, search bar, page view,
// render worker and settings, and wires user commands to them.
#pragma once
#include "PdfView.h"
#include "RenderWorker.h"
#include "Search.h"
#include "Settings.h"
#include "Toolbar.h"

class MainWindow {
public:
    bool Create(HINSTANCE inst, int showCmd, const std::wstring& file, int page);
    HWND Hwnd() const { return m_hwnd; }
    HACCEL Accelerators() const { return m_accel; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    void CreateChildren();
    void CreateAccelerators();
    void Layout();
    void OnCommand(int id, int code, HWND ctl);
    void UpdateUi();
    void UpdateTitle();

    // documents
    void OpenFile(const std::wstring& path, int page, const std::string& password = {});
    void ShowOpenDialog();
    void OnDocLoaded(DocLoadResult* res);

    // search
    void ShowSearch(bool show);
    void StartSearch();
    void FindNext(bool forward);
    void OnSearchResult(SearchPageResult* res);
    void UpdateSearchStatus();

    // misc
    void OnEditKey(HWND edit, WPARAM key);
    void GoToPageFromEdit();
    void ShowMoreMenu();
    void ShowZoomMenu();
    void ToggleFullscreen();
    void OnThemeChanged();
    void SaveSettings();

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HACCEL m_accel = nullptr;
    Toolbar m_toolbar;
    Toolbar m_searchBar;
    HWND m_pageEdit = nullptr;
    HWND m_searchEdit = nullptr;
    HWND m_lastFocus = nullptr;
    PdfView m_view;
    RenderWorker m_worker;
    Settings m_settings;
    SearchState m_search;
    bool m_searchVisible = false;

    // document state
    std::wstring m_docPath;
    uint32_t m_docId = 0;
    uint32_t m_nextDocId = 1;
    uint32_t m_pendingDocId = 0;
    int m_pendingPage = 0;
    int m_passwordAttempts = 0;

    // full screen
    bool m_fullscreen = false;
    WINDOWPLACEMENT m_restorePlacement{sizeof(WINDOWPLACEMENT)};
};
