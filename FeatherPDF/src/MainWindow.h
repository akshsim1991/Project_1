// MainWindow.h - top-level window: owns the tab strip, toolbar, search bar,
// one page view per tab, the render worker and settings, and wires user
// commands to them.
#pragma once
#include <memory>

#include "PdfView.h"
#include "RenderWorker.h"
#include "Search.h"
#include "Settings.h"
#include "Sidebar.h"
#include "TabBar.h"
#include "CommandPalette.h"
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
        std::vector<OutlineItem> outline;  // bookmarks
        DocInfo info;
        // editing
        bool dirty = false;           // unsaved changes (shown as "\x2022" on the tab)
        bool canUndo = false, canRedo = false;
        uint32_t saveDocId = 0;       // id the last save was requested under
        bool discardOnQuit = false;   // "Don't save" was chosen while exiting
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
    void CloseTab(int index, bool force = false);
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

    // editing (the work itself happens in the worker's DocEditor)
    bool CanEdit();
    void SendEdit(EditOp&& op);
    void UndoRedo(bool redo);
    void SaveDocument(int tabIndex, bool saveAs, uint32_t flags);
    bool ConfirmCloseTab(int index);  // true: close now
    bool ConfirmQuit();               // true: exit now
    void OnDocEdited(EditResult* res);
    void OnExtracted(ExtractResult* res);
    std::vector<int> SelectedPages();
    void DeletePages();
    void RotatePages(int turns);
    void MovePages(int gap);
    void InsertBlankPage();
    void InsertPagesFromFile();
    void MergeFiles();
    void MergeTabs();
    void ExtractPages();
    void AddMarkup(int type, const std::wstring& comment = {});
    // Edit PDF menu: text and comments
    HMENU CreateEditPdfMenu();
    HMENU CreateMarkupMenu();
    void ShowEditPdfMenu();
    void SetTool(ViewTool tool);
    void ReplaceTextInDocument();
    void NewComment(int page, float x, float y);
    void OpenComment(int page, const CommentInfo& comment);
    void CommentOnSelection();
    void OnCommentList(CommentListResult* res);
    // Annotate menu: drawing, stamps, signatures, pictures, new text
    HMENU CreateAnnotateMenu();
    HMENU CreateMarkupMenuColors();
    void ShowAnnotateMenu();
    void StartTool(ViewTool tool);
    void StartStamp(const std::wstring& text, COLORREF color);
    void StartSignature(bool newOne);
    void SignField(int page, const RectF& rect);
    void InsertImage();
    // Command palette, history, recent files, presentation, About
    void ShowCommandPalette();
    void RunPaletteCommand(int id, const std::wstring& arg);
    void AddRecent(const std::wstring& path);
    HMENU CreateRecentMenu();
    void TogglePresentation();
    void ShowAbout();
    bool InputBox(const std::wstring& title, const std::wstring& label, std::wstring& text);
    void ShowPagesMenu(POINT screen);
    HMENU CreatePagesMenu();
    std::vector<std::wstring> PickPdfFiles(bool multiple, const wchar_t* title);

    // sidebar, printing, properties, clipboard images
    void SetSidebarMode(SidebarMode mode);
    void SyncSidebar();  // show the active tab's outline / thumbnails
    void OnSidebar(WPARAM event, LPARAM value);
    RECT SplitterRect() const;
    void Print();
    void OnPrintProgress(int done, int total);
    void ShowProperties();
    void CopyImageToClipboard(TileResult* image);
    void SetPageColors(int mode);

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HACCEL m_accel = nullptr;
    bool m_toldAboutFonts = false;
    CommandPalette m_palette;
    // presentation: the view settings to restore afterwards
    bool m_presenting = false;
    bool m_presentWasFullscreen = false;
    ViewMode m_presentViewMode = ViewMode::Continuous;
    ZoomMode m_presentZoomMode = ZoomMode::FitWidth;
    double m_presentZoom = 1.0;  // "a similar font was used" is said once
    std::wstring m_replaceFind, m_replaceWith;
    bool m_replaceCase = false;
    TabBar m_tabBar;
    Sidebar m_sidebar;
    bool m_splitDrag = false;
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

    // printing (one job at a time; the printer settings are remembered)
    bool m_printing = false;
    bool m_printCancelled = false;
    int m_printDone = 0, m_printTotal = 0;
    HGLOBAL m_devMode = nullptr, m_devNames = nullptr;

    // exiting with unsaved changes: saves still running before closing
    int m_quitSaves = 0;
    bool m_quitAfterSaves = false;

    // full screen
    bool m_fullscreen = false;
    WINDOWPLACEMENT m_restorePlacement{sizeof(WINDOWPLACEMENT)};
};
