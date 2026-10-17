// MainWindow.h - top-level window: owns the tab strip, toolbar, search bar,
// one page view per tab, the render worker and settings, and wires user
// commands to them.
#pragma once
#include <map>
#include <memory>

#include "PdfView.h"
#include "RenderWorker.h"
#include "Search.h"
#include "Settings.h"
#include "Sidebar.h"
#include "TabBar.h"
#include "CommandPalette.h"
#include "Ocr.h"
#include "PrintDialog.h"
#include "ToolDialogs.h"
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
        bool unlocking = false;       // re-opened with what should be the owner password
        // Two documents side by side in one tab: the tab in the tab strip
        // holds the left (or top) one; `partner` is the other. The right
        // side is a tab of its own in m_tabs, kept out of the tab strip
        // (`side`), so everything that works on the active tab works on
        // whichever side is active.
        Tab* partner = nullptr;
        bool side = false;
        bool lastRight = false;       // left side: the right one was active when the tab was left
        // Recovery of unsaved changes after a crash (see Recovery.cpp)
        std::wstring recoveryFile;    // the latest copy of its unsaved state
        uint32_t autosavedDocId = 0;  // the state that copy holds
        std::wstring autosavingTo;    // a copy being written
        std::wstring recoveredFrom;   // reopened from a recovery copy: the file it belongs to
        std::wstring recoveryBase;    // ... and that copy (the document's base until saved)
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

    // two documents side by side in one tab
    int IndexOf(const Tab* tab) const;
    int BarIndexOf(int index) const;  // its place in the tab strip
    int TabOfBar(int bar) const;      // the tab (left side) at that place
    Tab* LeftOf(Tab& t) { return t.side ? t.partner : &t; }
    Tab* RightOf(Tab& t) { return t.side ? &t : t.partner; }
    void ShowBesideMenu();
    HMENU CreateBesideMenu();
    void OpenBeside(const std::wstring& path, int page = 0);  // a file beside the active document
    void ShowTabBeside(int index);  // an open tab's document beside the active one
    void FocusSide(int index);      // the other side of the same tab becomes active
    void CloseSide(bool right);
    void SwapSides();
    void SyncPartner(Tab& from);    // scroll and zoom the other side to match
    void UpdatePaneBars();
    RECT PaneSplitterRect() const;
    Tab* m_markInto = nullptr;      // a comparison's marked copy goes into this side (Mark differences)

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
    void SendEdit(EditOp&& op, int tabIndex = -1);  // -1: the active tab
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
    // Copying, cutting and pasting pages (between tabs and windows)
    void CopyPages(bool cut, Tab* dropTab = nullptr, int dropIndex = -1);
    void OnPagesCopied(ExtractResult* result);
    void PastePages(int where);  // ID_PASTE_PAGES_AFTER / BEFORE / START / END
    void PastePagesIntoTabs();
    void PastePagesInto(Tab& tab, int index, const std::wstring& file);
    void DropPages(POINT screen);
    // Cropping pages, pages per sheet and booklets
    void CropPagesDialog();
    void OnCropBox(int page, const RectF& rect);
    void MakePagesPerSheet();
    // Recovery of unsaved changes (Recovery.cpp)
    void InitRecovery();             // offers to restore what a crash left; returns after opening it
    void AutosaveTick();             // every few seconds: copies of changed documents
    void OnAutosaved(AutosaveResult* result);
    void ForgetRecovery(Tab& tab, bool closing);  // saved or made clean (or closed: everything)
    void WriteRecoveryList();
    void EndRecovery();              // a normal exit: nothing to recover
    std::vector<std::wstring> RecoveredOriginals() const;
    // Editing blocks, pictures and text formatting (the bar shown while editing)
    void PasteImage();
    void ReplaceImage();
    void FormatCommand(int id);
    const std::vector<std::wstring>& FontFaces();
    // Recognise text (OCR), watermarks, page numbers, export
    PageChoice NewPageChoice(const PageChoice& last);
    void RecogniseText();
    void RequestOcrPages();
    void OnOcrImage(OcrImage* image);
    void OnOcrDone(OcrResult* result);
    void OcrPageDone();
    void StopOcr(const std::wstring& why);
    void AddWatermark();
    void AddPageNumbers();
    void ExportPages();
    void OnExportProgress(ExportProgress* progress);
    // Compress, compare, password protection, redaction (2.3)
    void CompressDocument();
    HMENU CreateCompareMenu();
    void CompareWith(int tabIndex);  // -1: choose a file
    void OnCompared(CompareResult* result);
    void ProtectDocument();
    void UnlockDocument();
    bool EditPermitted(const EditOp& op);  // tells the user when the author forbids it
    bool Permitted(uint32_t permission, const wchar_t* what);
    HMENU CreateRedactMenu();
    void StartRedactTool();
    void RedactSelection();
    void FindForRedaction();
    void OnRedactFound(RedactFindResult* result);
    void ApplyRedactions();
    // Geometry menu: ruler, protractor, rulers, units and scale
    void ShowGeometryMenu();
    void SetMeasureTool(ViewTool tool);
    void ApplyMeasureOptions();
    void SetCustomScale();
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
    Toolbar m_formatBar;   // B I U S, font, size, colour, alignment: shown while editing text
    bool m_formatVisible = false;
    std::vector<std::wstring> m_fontFaces;  // installed fonts (read once)
    bool m_toldAboutFontPick = false;
    struct ClipJob {          // pages being written for the clipboard
        bool cut = false;
        Tab* tab = nullptr;   // where they come from
        uint32_t docId = 0;   // its state then
        std::vector<int> pages;
        std::wstring name;    // the document's name
        Tab* dropTab = nullptr;  // dragged onto another document: pasted there
        int dropIndex = -1;
    };
    static constexpr UINT_PTR kAutosaveTimer = 7;
    std::wstring m_recoveryId;       // this window's session: "<process>-<start time>"
    HANDLE m_recoveryLock = INVALID_HANDLE_VALUE;  // held while it runs (released if it crashes)
    std::wstring m_recoveryDir;      // %LOCALAPPDATA%\FeatherPDF\Recovery
    uint32_t m_recoverySerial = 1;
    std::map<uint32_t, ClipJob> m_clipJobs;
    uint32_t m_nextClipId = 1;
    Toolbar m_paneBar[2];  // above each side of a split tab: its name, the options, close
    bool m_paneDrag = false;
    bool m_syncing = false;
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

    // text recognition: one job at a time, a page or two ahead
    struct OcrJob {
        bool active = false;
        uint32_t id = 0;
        uint32_t docId = 0;        // the document as it was when the job started
        std::vector<int> pages;
        size_t next = 0;           // next page to render
        int inFlight = 0;          // rendered or being recognised
        int done = 0, skipped = 0, failed = 0;
        bool skipText = true;
        std::vector<OcrPage> found;
    } m_ocrJob;
    OcrRunner m_ocr;
    uint32_t m_nextOcrId = 1;
    OcrOptions m_ocrOptions;
    WatermarkOptions m_watermarkOptions;
    PageNumberOptions m_numberOptions;
    ExportOptions m_exportOptions;
    CropOptions m_cropOptions;
    PrintSettings m_printSettings;
    NupOptions m_nupOptions;
    // export (one at a time)
    bool m_exporting = false;
    int m_exportDone = 0, m_exportTotal = 0;
    ExportFormat m_exportFormat = ExportFormat::Png;
    // compress, compare, redaction
    CompressOptions m_compressOptions;
    RedactFindOptions m_redactFind;
    bool m_comparing = false;
    std::wstring m_compareNames;  // "a.pdf" and "b.pdf", for the result message

    // exiting with unsaved changes: saves still running before closing
    int m_quitSaves = 0;
    bool m_quitAfterSaves = false;

    // full screen
    bool m_fullscreen = false;
    WINDOWPLACEMENT m_restorePlacement{sizeof(WINDOWPLACEMENT)};
};
