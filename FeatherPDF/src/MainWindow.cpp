// MainWindow.cpp - top-level window, tabs and command handling.
#include "MainWindow.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

#include <commctrl.h>
#include <objbase.h>  // must precede commdlg.h for PrintDlgEx
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include "FileAssoc.h"
#include "Picture.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
constexpr UINT_PTR kSearchTimer = 1;  // incremental search debounce
constexpr UINT kSearchDelayMs = 300;

// Segoe MDL2 Assets / Segoe Fluent Icons code points.
const wchar_t kGlyphOpen[] = L"\xE8E5";
const wchar_t kGlyphPrev[] = L"\xE76B";
const wchar_t kGlyphNext[] = L"\xE76C";
const wchar_t kGlyphZoomOut[] = L"\xE71F";
const wchar_t kGlyphZoomIn[] = L"\xE8A3";
const wchar_t kGlyphSearch[] = L"\xE721";
const wchar_t kGlyphMore[] = L"\xE712";
const wchar_t kGlyphUp[] = L"\xE70E";
const wchar_t kGlyphDown[] = L"\xE70D";
const wchar_t kGlyphClose[] = L"\xE711";
const wchar_t kGlyphSave[] = L"\xE74E";
const wchar_t kGlyphUndo[] = L"\xE7A7";
const wchar_t kGlyphRedo[] = L"\xE7A6";

// Highlight colours offered in the menu (ID_HL_COLOR_FIRST + index).
const COLORREF kHighlightColors[] = {RGB(255, 230, 0), RGB(120, 220, 110), RGB(110, 190, 255),
                                     RGB(255, 140, 200)};
const wchar_t* const kHighlightColorNames[] = {L"&Yellow", L"&Green", L"&Blue", L"&Pink"};

struct StampDef {
    const wchar_t* text;
    COLORREF color;
};
const StampDef kStamps[] = {
    {L"APPROVED", RGB(0, 130, 60)},   {L"REJECTED", RGB(200, 20, 20)},     {L"DRAFT", RGB(0, 70, 200)},
    {L"CONFIDENTIAL", RGB(200, 20, 20)}, {L"REVIEWED", RGB(0, 130, 60)},   {L"FINAL", RGB(0, 130, 60)},
    {L"PAID", RGB(0, 130, 60)},       {L"RECEIVED", RGB(0, 70, 200)},      {L"NOT APPROVED", RGB(200, 20, 20)},
    {L"FOR INFORMATION", RGB(0, 70, 200)},
};
constexpr int kStampCount = (int)(sizeof(kStamps) / sizeof(kStamps[0]));
const COLORREF kDrawColors[] = {RGB(220, 30, 30), RGB(0, 90, 210), RGB(0, 140, 60), RGB(0, 0, 0),
                                RGB(240, 130, 0), RGB(130, 40, 170), RGB(250, 210, 0), RGB(120, 120, 120)};
const wchar_t* const kDrawColorNames[] = {L"&Red", L"&Blue", L"&Green", L"Blac&k", L"&Orange", L"&Purple", L"&Yellow", L"Gr&ey"};
constexpr int kDrawColorCount = 8;
const int kLineWidths[] = {10, 20, 40};  // tenths of a point
const wchar_t* const kLineWidthNames[] = {L"&Thin (1 pt)", L"&Medium (2 pt)", L"T&hick (4 pt)"};
constexpr int kLineWidthCount = 3;
const int kTextSizes[] = {10, 12, 14, 18, 24, 36};
constexpr int kTextSizeCount = 6;
constexpr COLORREF kUnderlineColor = RGB(0, 90, 220);
constexpr COLORREF kStrikeOutColor = RGB(220, 30, 30);

const wchar_t* OpenErrorText(OpenError e) {
    switch (e) {
        case OpenError::NotFound: return L"The file could not be found.";
        case OpenError::AccessDenied:
            return L"Access to the file was denied. You may not have permission to read it.";
        case OpenError::ReadError: return L"The file could not be read.";
        case OpenError::TooLarge: return L"The file is too large to open (over 4 GB).";
        case OpenError::NotPdf: return L"The file is damaged or is not a PDF document.";
        case OpenError::Password: return L"The password is incorrect.";
        case OpenError::Security: return L"This PDF uses an unsupported encryption method.";
        case OpenError::NoPages: return L"The document does not contain any pages.";
        default: return L"The document could not be opened.";
    }
}

std::wstring FullPath(const std::wstring& path) {
    DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (n == 0) return path;
    std::wstring full(n, L'\0');
    n = GetFullPathNameW(path.c_str(), n, full.data(), nullptr);
    full.resize(n);
    return n ? full : path;
}

std::wstring GetText(HWND hwnd) {
    int n = GetWindowTextLengthW(hwnd);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(hwnd, s.data(), n + 1);
    s.resize((size_t)n);
    return s;
}

// --- password prompt -------------------------------------------------------
struct PasswordPrompt {
    std::wstring fileName;
    bool retry = false;
    std::wstring password;
};

INT_PTR CALLBACK PasswordDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* p = (PasswordPrompt*)lp;
            std::wstring text = (p->retry ? L"Incorrect password. " : L"") +
                                std::wstring(L"\x201C") + p->fileName +
                                L"\x201D is protected. Enter the password to open it:";
            SetDlgItemTextW(dlg, IDC_PASSWORD_TEXT, text.c_str());
            ApplyWindowTheme(dlg);
            return TRUE;  // focus goes to the first tab stop (the edit)
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                if (LOWORD(wp) == IDOK) {
                    auto* p = (PasswordPrompt*)GetWindowLongPtrW(dlg, DWLP_USER);
                    p->password = GetText(GetDlgItem(dlg, IDC_PASSWORD_EDIT));
                }
                EndDialog(dlg, LOWORD(wp));
                return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

// ===========================================================================
// Creation
// ===========================================================================
bool MainWindow::Create(HINSTANCE inst, int showCmd, const std::wstring& file, int page) {
    m_inst = inst;
    m_settings.Load();
    CleanOldTempFiles();
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

    // Default size: a portrait-friendly window that fits the work area.
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int sysDpi = GetWindowDpi(nullptr);
    int w = std::min(Dpi(1000, sysDpi), (int)(work.right - work.left) * 9 / 10);
    int h = std::min(Dpi(1100, sysDpi), (int)(work.bottom - work.top) * 9 / 10);

    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, APP_WINDOW_CLASS, APP_NAME,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                             w, h, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    ApplyWindowTheme(m_hwnd);
    if (!m_worker.Start(m_hwnd)) return false;
    CreateChildren();
    CreateAccelerators();

    // Restore the previous window position/size (Windows moves it back on
    // screen if that monitor is gone).
    if (m_settings.hasPlacement) {
        WINDOWPLACEMENT wp = m_settings.placement;
        wp.flags = 0;
        wp.showCmd = (showCmd == SW_SHOWMINNOACTIVE || showCmd == SW_SHOWMINIMIZED ||
                      showCmd == SW_MINIMIZE)
                         ? (UINT)showCmd
                     : wp.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED
                                                      : SW_SHOWNORMAL;
        SetWindowPlacement(m_hwnd, &wp);
    } else {
        ShowWindow(m_hwnd, showCmd);
    }
    UpdateWindow(m_hwnd);  // show the window before any PDF work
    SetFocus(View().Hwnd());

    // Open the requested file, or restore the tabs of the last session.
    if (!file.empty()) {
        int start = page >= 0 ? page : 0;
        if (page < 0) {
            for (const auto& f : m_settings.session)
                if (SamePath(FullPath(file), f.path)) start = f.page;
        }
        OpenFile(file, start);
    } else {
        int restored = 0, activeIndex = -1;
        for (size_t i = 0; i < m_settings.session.size(); ++i) {
            const auto& f = m_settings.session[i];
            if (!FileExists(f.path)) continue;
            OpenFile(f.path, f.page, {}, -1, false);
            if ((int)i == m_settings.activeTab) activeIndex = restored;
            ++restored;
        }
        if (restored) ActivateTab(activeIndex >= 0 ? activeIndex : 0);
    }
    UpdateTabs();
    UpdateUi();
    return true;
}

void MainWindow::CreateChildren() {
    const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;

    m_tabBar.Create(m_hwnd, m_hwnd, 36);
    m_sidebar.Create(m_hwnd, m_hwnd, &m_worker);

    // Main toolbar:  Open | < > [page] / N | - 100% + | ...  Search  More
    m_toolbar.Create(m_hwnd, m_hwnd, 44);
    m_toolbar.AddButton(ID_OPEN, kGlyphOpen, L"Open (Ctrl+O)");
    m_toolbar.AddButton(ID_SAVE, kGlyphSave, L"Save (Ctrl+S)");
    m_toolbar.AddButton(ID_UNDO, kGlyphUndo, L"Undo (Ctrl+Z)");
    m_toolbar.AddButton(ID_REDO, kGlyphRedo, L"Redo (Ctrl+Y)");
    m_toolbar.AddSeparator();
    m_toolbar.AddTextButton(ID_EDIT_PDF_MENU, L"Edit PDF",
                            L"Edit text, find and replace, comments, markup and pages", 72);
    m_toolbar.AddTextButton(ID_ANNOTATE_MENU, L"Annotate",
                            L"Highlight, draw, add text, stamps, signatures and pictures", 78);
    m_toolbar.AddTextButton(ID_ADD_COMMENT, L"Comment", L"Add a comment: click where it should go (Ctrl+M)", 78);
    m_toolbar.AddSeparator();
    m_toolbar.AddButton(ID_PREV_PAGE, kGlyphPrev, L"Previous page (Page Up)");
    m_toolbar.AddButton(ID_NEXT_PAGE, kGlyphNext, L"Next page (Page Down)");
    m_pageEdit = CreateWindowExW(0, L"EDIT", L"", editStyle | ES_NUMBER | ES_CENTER, 0, 0, 0, 0,
                                 m_toolbar.Hwnd(), (HMENU)(INT_PTR)ID_PAGE_EDIT, m_inst, nullptr);
    SendMessageW(m_pageEdit, EM_SETLIMITTEXT, 7, 0);
    m_toolbar.AddChild(m_pageEdit, 56);
    m_toolbar.AddLabel(ID_PAGE_TOTAL, 64, false, nullptr);
    m_toolbar.AddSeparator();
    m_toolbar.AddButton(ID_ZOOM_OUT, kGlyphZoomOut, L"Zoom out (Ctrl+-)");
    m_toolbar.AddLabel(ID_ZOOM_LABEL, 56, true, L"Zoom options");
    m_toolbar.AddButton(ID_ZOOM_IN, kGlyphZoomIn, L"Zoom in (Ctrl++)");
    m_toolbar.AddSpacer();
    m_toolbar.AddButton(ID_SEARCH, kGlyphSearch, L"Search (Ctrl+F)");
    m_toolbar.AddButton(ID_MENU, kGlyphMore, L"More options");

    // Search bar (hidden until Ctrl+F).
    m_searchBar.Create(m_hwnd, m_hwnd, 40);
    m_searchEdit = CreateWindowExW(0, L"EDIT", L"", editStyle, 0, 0, 0, 0, m_searchBar.Hwnd(),
                                   (HMENU)(INT_PTR)ID_SEARCH_EDIT, m_inst, nullptr);
    SendMessageW(m_searchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Find in document");
    m_searchBar.AddChild(m_searchEdit, 260);
    m_searchBar.AddButton(ID_FIND_PREV, kGlyphUp, L"Previous match (Shift+Enter)");
    m_searchBar.AddButton(ID_FIND_NEXT, kGlyphDown, L"Next match (Enter)");
    m_searchBar.AddTextButton(ID_MATCH_CASE, L"Aa", L"Match case");
    m_searchBar.AddLabel(ID_SEARCH_STATUS, 160, false, nullptr);
    m_searchBar.AddSpacer();
    m_searchBar.AddButton(ID_SEARCH_CLOSE, kGlyphClose, L"Close (Esc)");
    m_searchBar.SetChecked(ID_MATCH_CASE, m_settings.matchCase);
    ShowWindow(m_searchBar.Hwnd(), SW_HIDE);

    SetWindowSubclass(m_pageEdit, &MainWindow::EditProc, 1, (DWORD_PTR)this);
    SetWindowSubclass(m_searchEdit, &MainWindow::EditProc, 1, (DWORD_PTR)this);

    ActivateTab(NewTab());  // there is always at least one (possibly empty) tab
    SetSidebarMode((SidebarMode)m_settings.sidebarMode);
}

void MainWindow::CreateAccelerators() {
    ACCEL acc[] = {
        {FCONTROL | FVIRTKEY, 'O', ID_OPEN},
        {FCONTROL | FVIRTKEY, 'T', ID_OPEN},
        {FCONTROL | FVIRTKEY, 'W', ID_CLOSE_TAB},
        {FCONTROL | FVIRTKEY, VK_F4, ID_CLOSE_TAB},
        {FCONTROL | FVIRTKEY, VK_TAB, ID_NEXT_TAB},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, ID_PREV_TAB},
        {FCONTROL | FVIRTKEY, VK_NEXT, ID_NEXT_TAB},
        {FCONTROL | FVIRTKEY, VK_PRIOR, ID_PREV_TAB},
        {FCONTROL | FVIRTKEY, 'F', ID_SEARCH},
        {FVIRTKEY, VK_F3, ID_FIND_NEXT},
        {FSHIFT | FVIRTKEY, VK_F3, ID_FIND_PREV},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_ZOOM_IN},  // Ctrl+= / Ctrl++
        {FCONTROL | FSHIFT | FVIRTKEY, VK_OEM_PLUS, ID_ZOOM_IN},
        {FCONTROL | FVIRTKEY, VK_ADD, ID_ZOOM_IN},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_ZOOM_OUT},
        {FCONTROL | FVIRTKEY, VK_SUBTRACT, ID_ZOOM_OUT},
        {FCONTROL | FVIRTKEY, '0', ID_FIT_PAGE},
        {FCONTROL | FVIRTKEY, VK_NUMPAD0, ID_FIT_PAGE},
        {FCONTROL | FVIRTKEY, '1', ID_ACTUAL_SIZE},
        {FCONTROL | FVIRTKEY, '2', ID_FIT_WIDTH},
        {FCONTROL | FVIRTKEY, 'G', ID_GOTO_PAGE},
        {FVIRTKEY, VK_F11, ID_FULLSCREEN},
        {FCONTROL | FVIRTKEY, 'P', ID_PRINT},
        {FCONTROL | FVIRTKEY, 'D', ID_PROPERTIES},
        {FCONTROL | FVIRTKEY, 'L', ID_ROTATE_LEFT},
        {FCONTROL | FVIRTKEY, 'R', ID_ROTATE_RIGHT},
        {FCONTROL | FVIRTKEY, 'B', ID_SIDEBAR_BOOKMARKS},
        {FCONTROL | FSHIFT | FVIRTKEY, 'B', ID_SIDEBAR_THUMBNAILS},
        {FCONTROL | FVIRTKEY, 'S', ID_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_SAVE_AS},
        {FCONTROL | FVIRTKEY, 'Z', ID_UNDO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'Z', ID_REDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_REDO},
        {FCONTROL | FVIRTKEY, 'E', ID_EDIT_TEXT},
        {FCONTROL | FSHIFT | FVIRTKEY, 'H', ID_REPLACE_TEXT},
        {FCONTROL | FVIRTKEY, 'M', ID_ADD_COMMENT},
        {FCONTROL | FSHIFT | FVIRTKEY, 'M', ID_COMMENT_SELECTION},
        {FCONTROL | FVIRTKEY, 'K', ID_COMMAND_PALETTE},
        {FALT | FVIRTKEY, VK_LEFT, ID_BACK},
        {FALT | FVIRTKEY, VK_RIGHT, ID_FORWARD},
        {FVIRTKEY, VK_F5, ID_PRESENT},
    };
    m_accel = CreateAcceleratorTableW(acc, (int)(sizeof(acc) / sizeof(acc[0])));
}

void MainWindow::Layout() {
    if (m_tabs.empty()) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int y = 0;
    if (!m_fullscreen) {
        const int tabH = m_tabBar.Height();
        MoveWindow(m_tabBar.Hwnd(), 0, 0, rc.right, tabH, TRUE);
        ShowWindow(m_tabBar.Hwnd(), SW_SHOWNA);
        y = tabH;
        const int th = m_toolbar.Height();
        MoveWindow(m_toolbar.Hwnd(), 0, y, rc.right, th, TRUE);
        ShowWindow(m_toolbar.Hwnd(), SW_SHOWNA);
        y += th;
    } else {
        ShowWindow(m_tabBar.Hwnd(), SW_HIDE);
        ShowWindow(m_toolbar.Hwnd(), SW_HIDE);
    }
    if (m_searchVisible) {
        const int sh = m_searchBar.Height();
        MoveWindow(m_searchBar.Hwnd(), 0, y, rc.right, sh, TRUE);
        ShowWindow(m_searchBar.Hwnd(), SW_SHOWNA);
        y += sh;
    } else {
        ShowWindow(m_searchBar.Hwnd(), SW_HIDE);
    }
    // Optional sidebar on the left, then a thin splitter.
    int x = 0;
    const int h = std::max(0, (int)rc.bottom - y);
    if (m_sidebar.Mode() != SidebarMode::None && !m_fullscreen) {
        const int dpi = GetWindowDpi(m_hwnd);
        const int w = std::min(Dpi(m_settings.sidebarWidth, dpi), (int)rc.right / 2);
        MoveWindow(m_sidebar.Hwnd(), 0, y, w, h, TRUE);
        ShowWindow(m_sidebar.Hwnd(), SW_SHOWNA);
        x = w + Dpi(4, dpi);
    } else {
        ShowWindow(m_sidebar.Hwnd(), SW_HIDE);
    }
    InvalidateRect(m_hwnd, nullptr, TRUE);  // repaint the splitter strip
    // Only the active tab's view is visible (and therefore renders).
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        HWND view = m_tabs[i]->view->Hwnd();
        if ((int)i == m_active) {
            MoveWindow(view, x, y, std::max(0, (int)rc.right - x), h, TRUE);
            ShowWindow(view, SW_SHOWNA);
        } else {
            ShowWindow(view, SW_HIDE);
        }
    }
}

// ===========================================================================
// Tabs
// ===========================================================================
int MainWindow::NewTab() {
    auto tab = std::make_unique<Tab>();
    tab->view = std::make_unique<PdfView>();
    tab->view->Create(m_hwnd, &m_worker);
    ShowWindow(tab->view->Hwnd(), SW_HIDE);
    Tab* raw = tab.get();
    tab->view->SetSearch(&raw->search);
    tab->view->onViewChanged = [this, raw] {
        if (m_active >= 0 && raw == &Active()) UpdateUi();
    };
    tab->view->onEdit = [this, raw](EditOp&& op) {
        if (m_active >= 0 && raw == &Active()) SendEdit(std::move(op));
    };
    tab->view->onNewComment = [this, raw](int page, float x, float y) {
        if (m_active >= 0 && raw == &Active()) NewComment(page, x, y);
    };
    tab->view->onOpenComment = [this, raw](int page, const CommentInfo& c) {
        if (m_active >= 0 && raw == &Active()) OpenComment(page, c);
    };
    tab->view->onSignField = [this, raw](int page, const RectF& rect) {
        if (m_active >= 0 && raw == &Active()) SignField(page, rect);
    };
    // New tabs inherit the view mode and zoom of the current tab.
    if (m_active >= 0) {
        PdfView& cur = View();
        tab->view->SetViewMode(cur.GetViewMode());
        tab->view->SetCoverPage(cur.CoverPage());
        tab->view->SetPageColors(cur.GetPageColors());
        if (cur.GetZoomMode() == ZoomMode::Custom)
            tab->view->SetZoom(cur.Zoom());
        else
            tab->view->SetZoomMode(cur.GetZoomMode());
    } else {
        tab->view->SetViewMode((ViewMode)m_settings.viewMode);
        tab->view->SetCoverPage(m_settings.coverPage);
        tab->view->SetPageColors(m_settings.pageColors);
        if (m_settings.zoomMode == 0)
            tab->view->SetZoom(m_settings.zoom);
        else
            tab->view->SetZoomMode((ZoomMode)m_settings.zoomMode);
    }
    m_tabs.push_back(std::move(tab));
    return (int)m_tabs.size() - 1;
}

void MainWindow::ActivateTab(int index) {
    if (index < 0 || index >= (int)m_tabs.size()) return;
    if (index != m_active && m_active >= 0 && m_active < (int)m_tabs.size()) {
        // Leaving a tab: stop its search and give its rendered tiles back
        // (they are re-rendered in a few ms when the tab is shown again).
        Tab& old = Active();
        if (old.search.running) CancelSearch();
        old.view->TrimMemory();
    }
    m_active = index;
    Layout();
    InvalidateRect(View().Hwnd(), nullptr, FALSE);
    if (GetFocus() != m_searchEdit) SetFocus(View().Hwnd());
    if (m_searchVisible && !GetText(m_searchEdit).empty() &&
        GetText(m_searchEdit) != Active().search.query && View().HasDocument())
        StartSearch();
    UpdateTabs();
    UpdateTitle();
    SyncSidebar();
    UpdateUi();
    UpdateSearchStatus();
}

void MainWindow::CloseTab(int index, bool force) {
    if (index < 0 || index >= (int)m_tabs.size()) return;
    if (!force && !ConfirmCloseTab(index)) return;
    Tab& tab = *m_tabs[(size_t)index];
    if (index == m_active && tab.search.running) CancelSearch();
    if (tab.docId) m_worker.CloseDocument(tab.docId);

    if (m_tabs.size() == 1) {  // keep one empty tab rather than no view at all
        tab.docId = tab.pendingDocId = 0;
        tab.dirty = tab.canUndo = tab.canRedo = false;
        tab.path.clear();
        tab.search.Reset();
        tab.search.query.clear();
        tab.view->CloseDocument();
        tab.view->SetMessage({});
        tab.outline.clear();
        tab.info = DocInfo{};
        SyncSidebar();
        UpdateTabs();
        UpdateTitle();
        UpdateUi();
        UpdateSearchStatus();
        return;
    }
    const bool wasActive = index == m_active;
    m_tabs.erase(m_tabs.begin() + index);  // destroys the view window
    if (wasActive) {
        m_active = -1;
        ActivateTab(std::min(index, (int)m_tabs.size() - 1));
    } else {
        if (index < m_active) --m_active;
        UpdateTabs();
    }
}

void MainWindow::UpdateTabs() {
    std::vector<TabBar::TabInfo> infos;
    for (const auto& t : m_tabs) {
        TabBar::TabInfo info;
        if (t->docId)
            info.title = (t->dirty ? L"\x2022 " : L"") + FileNameFromPath(t->path);
        else if (t->pendingDocId)
            info.title = L"Opening\x2026";
        else
            info.title = L"New tab";
        info.tip = t->path;
        infos.push_back(std::move(info));
    }
    m_tabBar.SetTabs(std::move(infos), m_active);
}

int MainWindow::TabByDocId(uint32_t docId) const {
    for (size_t i = 0; i < m_tabs.size(); ++i)
        if (docId && m_tabs[i]->docId == docId) return (int)i;
    return -1;
}

void MainWindow::OnTabBar(TabAction action, int index) {
    switch (action) {
        case TabAction::Select: ActivateTab(index); break;
        case TabAction::Close: CloseTab(index); break;
        case TabAction::New: ShowOpenDialog(); break;
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
    switch (msg) {
        case WM_APP_DOC_LOADED:
            OnDocLoaded((DocLoadResult*)lp);
            return 0;
        case WM_APP_TILE_READY: {
            auto* res = (TileResult*)lp;
            int i = TabByDocId(res->docId);
            if (i >= 0)
                m_tabs[(size_t)i]->view->OnTileReady(res);
            else
                delete res;
            return 0;
        }
        case WM_APP_TEXT_LAYER: {
            auto* res = (TextLayerResult*)lp;
            int i = TabByDocId(res->docId);
            if (i >= 0)
                m_tabs[(size_t)i]->view->OnTextLayer(res);
            else
                delete res;
            return 0;
        }
        case WM_APP_TEXT_RUNS: {
            auto* res = (TextRunsResult*)lp;
            int i = TabByDocId(res->docId);
            if (i >= 0)
                m_tabs[(size_t)i]->view->OnTextRuns(res);
            else
                delete res;
            return 0;
        }
        case WM_APP_COMMENTS:
            OnCommentList((CommentListResult*)lp);
            return 0;
        case WM_APP_TEXT_COPIED: {
            std::unique_ptr<TextCopyResult> res((TextCopyResult*)lp);
            CopyToClipboard(res->text);
            return 0;
        }
        case WM_APP_SEARCH_RESULT:
            OnSearchResult((SearchPageResult*)lp);
            return 0;
        case WM_APP_TABBAR:
            OnTabBar((TabAction)wp, (int)lp);
            return 0;
        case WM_APP_THUMB_READY:
            m_sidebar.Thumbs().OnThumbReady((TileResult*)lp);
            return 0;
        case WM_APP_IMAGE_READY:
            CopyImageToClipboard((TileResult*)lp);
            return 0;
        case WM_APP_PRINT_PROGRESS:
            OnPrintProgress((int)wp, (int)lp);
            return 0;
        case WM_APP_SIDEBAR:
            OnSidebar(wp, lp);
            return 0;
        case WM_APP_DOC_EDITED:
            OnDocEdited((EditResult*)lp);
            return 0;
        case WM_APP_EXTRACTED:
            OnExtracted((ExtractResult*)lp);
            return 0;

        // Splitter between sidebar and page: drag to resize the sidebar.
        case WM_SETCURSOR:
            if ((HWND)wp == m_hwnd && LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);
                RECT sr = SplitterRect();
                if (m_splitDrag || PtInRect(&sr, pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN: {
            RECT sr = SplitterRect();
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (PtInRect(&sr, pt)) {
                m_splitDrag = true;
                SetCapture(m_hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (m_splitDrag) {
                const int dpi = GetWindowDpi(m_hwnd);
                const int x = std::max(Dpi(120, dpi), (int)GET_X_LPARAM(lp));
                m_settings.sidebarWidth = std::max(120, std::min(800, MulDiv(x, 96, dpi)));
                Layout();
            }
            return 0;
        case WM_LBUTTONUP:
            if (m_splitDrag) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            m_splitDrag = false;
            return 0;
        case WM_ERASEBKGND: {  // only the splitter strip is ever visible
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            SetBkColor((HDC)wp, CurrentTheme().barBorder);
            ExtTextOutW((HDC)wp, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
            return 1;
        }

        case WM_COPYDATA:
            OnCopyData((const COPYDATASTRUCT*)lp);
            return TRUE;

        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp), (HWND)lp);
            return 0;

        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) {
                // Nobody is looking: give the rendered pages back to the OS.
                if (!m_tabs.empty()) View().TrimMemory();
                m_sidebar.Thumbs().TrimMemory();
            } else {
                Layout();
            }
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) m_lastFocus = GetFocus();
            break;
        case WM_SETFOCUS: {
            if (m_tabs.empty()) return 0;
            HWND target = View().Hwnd();
            if (m_lastFocus && IsChild(m_hwnd, m_lastFocus) && IsWindowVisible(m_lastFocus))
                target = m_lastFocus;
            SetFocus(target);
            return 0;
        }

        case WM_MOUSEWHEEL:  // wheel over the toolbar etc. scrolls the page
            if (!m_tabs.empty()) return SendMessageW(View().Hwnd(), msg, wp, lp);
            return 0;

        case WM_TIMER:
            if (wp == kSearchTimer) {
                KillTimer(m_hwnd, kSearchTimer);
                if (GetText(m_searchEdit) != Active().search.query) StartSearch();
            }
            return 0;

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {  // every dropped file gets a tab
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                if (!len) continue;
                std::wstring path((size_t)len + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), len + 1);
                path.resize(len);
                OpenFile(path, 0);
            }
            DragFinish(drop);
            SetForegroundWindow(m_hwnd);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lp;
            const int dpi = GetWindowDpi(m_hwnd);
            mmi->ptMinTrackSize = {Dpi(480, dpi), Dpi(320, dpi)};
            return 0;
        }

        case WM_DPICHANGED: {
            m_tabBar.OnDpiChanged();
            m_toolbar.OnDpiChanged();
            m_searchBar.OnDpiChanged();
            m_sidebar.OnDpiChanged();
            for (auto& t : m_tabs) t->view->OnDpiChanged();
            const RECT* r = (const RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }

        case WM_SETTINGCHANGE:
            if (lp && lstrcmpW((const wchar_t*)lp, L"ImmersiveColorSet") == 0 &&
                m_settings.themeMode == (int)ThemeMode::System)
                OnThemeChanged();
            break;

        case WM_CLOSE:
            if (m_quitSaves > 0) return 0;  // still saving; closes when done
            if (!ConfirmQuit()) return 0;
            SaveSettings();
            DestroyWindow(m_hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(m_hwnd, kSearchTimer);
            m_worker.Stop();
            if (m_devMode) GlobalFree(m_devMode);
            if (m_devNames) GlobalFree(m_devNames);
            m_devMode = m_devNames = nullptr;
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
            if (wp == VK_RETURN || wp == VK_ESCAPE) {
                self->OnEditKey(hwnd, wp);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wp == L'\r' || wp == 27) return 0;  // no "ding"
            break;
        case WM_KILLFOCUS:
            if (hwnd == self->m_pageEdit) {
                LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
                self->UpdateUi();  // restore the real page number
                return r;
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, &MainWindow::EditProc, 1);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void MainWindow::OnEditKey(HWND edit, WPARAM key) {
    if (edit == m_pageEdit) {
        if (key == VK_RETURN) GoToPageFromEdit();
        SetFocus(View().Hwnd());  // Esc: abandon edit (text restored on kill focus)
        return;
    }
    if (edit == m_searchEdit) {
        if (key == VK_ESCAPE)
            ShowSearch(false);
        else
            FindNext(GetKeyState(VK_SHIFT) >= 0);
    }
}

void MainWindow::GoToPageFromEdit() {
    const std::wstring text = GetText(m_pageEdit);
    if (text.empty() || !View().HasDocument()) return;
    const long page = wcstol(text.c_str(), nullptr, 10);
    if (page >= 1) {
        View().PushHistory();
        View().GoToPage((int)std::min<long>(page, View().PageCount()) - 1);
    }
}

// ===========================================================================
// Commands
// ===========================================================================
void MainWindow::OnCommand(int id, int code, HWND ctl) {
    PdfView& view = View();
    if (id >= ID_HL_COLOR_FIRST && id <= ID_HL_COLOR_LAST) {
        m_settings.highlightColor = id - ID_HL_COLOR_FIRST;
        if (view.HasSelection()) AddMarkup(kMarkupHighlight);
        return;
    }
    if (id >= ID_STAMP_FIRST && id < ID_STAMP_FIRST + kStampCount) {
        StartStamp(kStamps[id - ID_STAMP_FIRST].text, kStamps[id - ID_STAMP_FIRST].color);
        return;
    }
    if (id >= ID_DRAWCOLOR_FIRST && id < ID_DRAWCOLOR_FIRST + kDrawColorCount) {
        m_settings.drawColor = kDrawColors[id - ID_DRAWCOLOR_FIRST];
        StartTool(view.Tool());  // the active tool takes the new colour
        return;
    }
    if (id >= ID_WIDTH_FIRST && id < ID_WIDTH_FIRST + kLineWidthCount) {
        m_settings.lineWidthTenths = kLineWidths[id - ID_WIDTH_FIRST];
        StartTool(view.Tool());
        return;
    }
    if (id >= ID_TEXTSIZE_FIRST && id < ID_TEXTSIZE_FIRST + kTextSizeCount) {
        m_settings.textSize = kTextSizes[id - ID_TEXTSIZE_FIRST];
        StartTool(view.Tool());
        return;
    }
    if (id >= ID_RECENT_FIRST && id < ID_RECENT_FIRST + 20) {
        const size_t i = (size_t)(id - ID_RECENT_FIRST);
        if (i < m_settings.recent.size()) {
            const std::wstring path = m_settings.recent[i];
            if (FileExists(path)) {
                OpenFile(path, 0);
            } else {
                m_settings.recent.erase(m_settings.recent.begin() + (ptrdiff_t)i);
                MessageBoxW(m_hwnd, (L"\x201C" + path + L"\x201D no longer exists, so it was removed from the list.").c_str(),
                            APP_NAME, MB_ICONINFORMATION);
            }
        }
        return;
    }
    if (id >= ID_ZOOM_PRESET_FIRST && id < ID_ZOOM_PRESET_FIRST + kZoomPresetCount) {
        view.SetZoom(kZoomPresets[id - ID_ZOOM_PRESET_FIRST]);
        return;
    }
    if (ctl && ctl == m_searchEdit) {
        if (code == EN_CHANGE) SetTimer(m_hwnd, kSearchTimer, kSearchDelayMs, nullptr);
        return;
    }
    if (ctl && ctl == m_pageEdit) return;

    const bool fromAccelerator = code == 1;
    switch (id) {
        case ID_OPEN: ShowOpenDialog(); break;
        case ID_SAVE: SaveDocument(m_active, false, 0); break;
        case ID_SAVE_AS: SaveDocument(m_active, true, 0); break;
        case ID_UNDO:
        case ID_REDO:
            // In a text box Ctrl+Z undoes typing, not the document.
            if (HWND focus = GetFocus()) {
                wchar_t cls[16] = L"";
                GetClassNameW(focus, cls, 16);
                if (_wcsicmp(cls, L"Edit") == 0) {
                    if (id == ID_UNDO) SendMessageW(focus, EM_UNDO, 0, 0);
                    break;
                }
            }
            UndoRedo(id == ID_REDO);
            break;
        case ID_DELETE_PAGES: DeletePages(); break;
        case ID_ROTATE_PAGES_CW: RotatePages(1); break;
        case ID_ROTATE_PAGES_CCW: RotatePages(-1); break;
        case ID_INSERT_BLANK: InsertBlankPage(); break;
        case ID_INSERT_FILE: InsertPagesFromFile(); break;
        case ID_MERGE_FILES: MergeFiles(); break;
        case ID_MERGE_TABS: MergeTabs(); break;
        case ID_EXTRACT_PAGES: ExtractPages(); break;
        case ID_EDIT_PDF_MENU: ShowEditPdfMenu(); break;
        case ID_ANNOTATE_MENU: ShowAnnotateMenu(); break;
        case ID_TOOL_ADD_TEXT: StartTool(view.Tool() == ViewTool::AddText ? ViewTool::Select : ViewTool::AddText); break;
        case ID_TOOL_RECT: StartTool(view.Tool() == ViewTool::Rectangle ? ViewTool::Select : ViewTool::Rectangle); break;
        case ID_TOOL_ELLIPSE: StartTool(view.Tool() == ViewTool::Ellipse ? ViewTool::Select : ViewTool::Ellipse); break;
        case ID_TOOL_LINE: StartTool(view.Tool() == ViewTool::Line ? ViewTool::Select : ViewTool::Line); break;
        case ID_TOOL_ARROW: StartTool(view.Tool() == ViewTool::Arrow ? ViewTool::Select : ViewTool::Arrow); break;
        case ID_TOOL_PEN: StartTool(view.Tool() == ViewTool::Pen ? ViewTool::Select : ViewTool::Pen); break;
        case ID_SQUIGGLY: AddMarkup(kMarkupSquiggly); break;
        case ID_STAMP_CUSTOM: {
            std::wstring text;
            if (InputBox(L"Custom stamp", L"The word or words on the stamp (for example \x201C" L"CHECKED BY RAHUL\x201D):", text) &&
                !text.empty()) {
                CharUpperBuffW(text.data(), (DWORD)text.size());
                StartStamp(text, RGB(0, 70, 200));
            }
            break;
        }
        case ID_SIGNATURE_USE: StartSignature(false); break;
        case ID_SIGNATURE_NEW: StartSignature(true); break;
        case ID_SIGNATURE_FORGET:
            DeleteSavedSignature();
            MessageBoxW(m_hwnd, L"The saved signature was removed from this PC.", APP_NAME, MB_ICONINFORMATION);
            break;
        case ID_INSERT_IMAGE: InsertImage(); break;
        case ID_DRAWCOLOR_MORE: {
            static COLORREF custom[16] = {};
            CHOOSECOLORW cc{sizeof(cc)};
            cc.hwndOwner = m_hwnd;
            cc.lpCustColors = custom;
            cc.rgbResult = m_settings.drawColor;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                m_settings.drawColor = cc.rgbResult;
                StartTool(view.Tool());
            }
            break;
        }
        case ID_COMMAND_PALETTE: ShowCommandPalette(); break;
        case ID_BACK: view.Back(); break;
        case ID_FORWARD: view.Forward(); break;
        case ID_RECENT_CLEAR: m_settings.recent.clear(); break;
        case ID_PRESENT: TogglePresentation(); break;
        case ID_EDIT_TEXT:
            SetTool(view.Tool() == ViewTool::EditText ? ViewTool::Select : ViewTool::EditText);
            break;
        case ID_ADD_COMMENT:
            SetTool(view.Tool() == ViewTool::AddComment ? ViewTool::Select : ViewTool::AddComment);
            break;
        case ID_COMMENT_SELECTION: CommentOnSelection(); break;
        case ID_REPLACE_TEXT: ReplaceTextInDocument(); break;
        case ID_SHOW_COMMENTS:
            if (CanEdit()) m_worker.ListComments(Active().docId);
            break;
        case ID_HIGHLIGHT: AddMarkup(kMarkupHighlight); break;
        case ID_UNDERLINE: AddMarkup(kMarkupUnderline); break;
        case ID_STRIKEOUT: AddMarkup(kMarkupStrikeOut); break;
        case ID_CLOSE_TAB: CloseTab(m_active); break;
        case ID_NEXT_TAB: ActivateTab((m_active + 1) % (int)m_tabs.size()); break;
        case ID_PREV_TAB:
            ActivateTab((m_active + (int)m_tabs.size() - 1) % (int)m_tabs.size());
            break;
        case ID_PREV_PAGE: view.PrevPage(); break;
        case ID_NEXT_PAGE: view.NextPage(); break;
        case ID_FIRST_PAGE:
            view.PushHistory();
            view.GoToPage(0);
            break;
        case ID_LAST_PAGE:
            view.PushHistory();
            view.GoToPage(view.PageCount() - 1);
            break;
        case ID_GOTO_PAGE:
            if (m_fullscreen) ToggleFullscreen();
            SetFocus(m_pageEdit);
            SendMessageW(m_pageEdit, EM_SETSEL, 0, -1);
            break;
        case ID_ZOOM_IN: view.ZoomIn(); break;
        case ID_ZOOM_OUT: view.ZoomOut(); break;
        case ID_ZOOM_LABEL: ShowZoomMenu(); break;
        case ID_FIT_WIDTH: view.SetZoomMode(ZoomMode::FitWidth); break;
        case ID_FIT_PAGE: view.SetZoomMode(ZoomMode::FitPage); break;
        case ID_ACTUAL_SIZE: view.SetZoom(1.0); break;
        case ID_CONTINUOUS: view.SetViewMode(ViewMode::Continuous); break;
        case ID_SINGLE_PAGE: view.SetViewMode(ViewMode::Single); break;
        case ID_VIEW_TWO_PAGE: view.SetViewMode(ViewMode::TwoPage); break;
        case ID_COVER_PAGE:
            m_settings.coverPage = !view.CoverPage();
            for (auto& t : m_tabs) t->view->SetCoverPage(m_settings.coverPage);
            break;
        case ID_ROTATE_LEFT:
            view.Rotate(-1);
            SyncSidebar();
            break;
        case ID_ROTATE_RIGHT:
            view.Rotate(1);
            SyncSidebar();
            break;
        case ID_COLORS_NORMAL: SetPageColors(kColorsNormal); break;
        case ID_COLORS_DARK: SetPageColors(kColorsDark); break;
        case ID_COLORS_DIM: SetPageColors(kColorsDim); break;
        case ID_SIDEBAR_BOOKMARKS:
            SetSidebarMode(m_sidebar.Mode() == SidebarMode::Bookmarks ? SidebarMode::None
                                                                      : SidebarMode::Bookmarks);
            break;
        case ID_SIDEBAR_THUMBNAILS:
            SetSidebarMode(m_sidebar.Mode() == SidebarMode::Thumbnails ? SidebarMode::None
                                                                       : SidebarMode::Thumbnails);
            break;
        case ID_PRINT: Print(); break;
        case ID_CANCEL_PRINT:
            if (m_printing) {
                m_printCancelled = true;
                m_worker.CancelPrint();
            }
            break;
        case ID_PROPERTIES: ShowProperties(); break;
        case ID_COPY_PAGE_IMAGE: view.CopyPageImage(); break;
        case ID_COPY_AREA_IMAGE: view.StartAreaCopy(); break;
        case ID_FULLSCREEN: ToggleFullscreen(); break;
        case ID_COPY: view.CopySelection(); break;
        case ID_SELECT_ALL: view.SelectAll(); break;
        case ID_THEME_SYSTEM: SetThemeMode((int)ThemeMode::System); break;
        case ID_THEME_LIGHT: SetThemeMode((int)ThemeMode::Light); break;
        case ID_THEME_DARK: SetThemeMode((int)ThemeMode::Dark); break;
        case ID_SEARCH:
            // Ctrl+F always opens/focuses; the toolbar button toggles.
            if (!fromAccelerator && m_searchVisible)
                ShowSearch(false);
            else
                ShowSearch(true);
            break;
        case ID_FIND_NEXT: FindNext(true); break;
        case ID_FIND_PREV: FindNext(false); break;
        case ID_MATCH_CASE:
            m_settings.matchCase = !m_settings.matchCase;
            m_searchBar.SetChecked(ID_MATCH_CASE, m_settings.matchCase);
            if (!GetText(m_searchEdit).empty()) StartSearch();
            break;
        case ID_SEARCH_CLOSE: ShowSearch(false); break;
        case ID_ESCAPE:
            if (m_presenting)
                TogglePresentation();
            else if (m_searchVisible)
                ShowSearch(false);
            else if (m_fullscreen)
                ToggleFullscreen();
            break;
        case ID_MENU: ShowMoreMenu(); break;
        case ID_REGISTER_DEFAULT:
            if (RegisterFileAssociation()) {
                MessageBoxW(m_hwnd,
                            L"Feather PDF is now registered as a PDF viewer.\n\n"
                            L"In the Settings page that opens next, choose Feather PDF as the "
                            L"default app for .pdf files.",
                            APP_NAME, MB_ICONINFORMATION);
                OpenDefaultAppsSettings();
            } else {
                MessageBoxW(m_hwnd, L"The file association could not be registered.", APP_NAME,
                            MB_ICONWARNING);
            }
            break;
        case ID_ABOUT: ShowAbout(); break;
        case ID_EXIT: PostMessageW(m_hwnd, WM_CLOSE, 0, 0); break;
    }
}

void MainWindow::ShowMoreMenu() {
    PdfView& view = View();
    const bool doc = view.HasDocument();
    const UINT docFlag = doc ? MF_ENABLED : MF_GRAYED;
    auto check = [](bool on) -> UINT { return on ? MF_CHECKED : 0; };

    HMENU layout = CreatePopupMenu();
    AppendMenuW(layout, MF_STRING, ID_SINGLE_PAGE, L"&Single page");
    AppendMenuW(layout, MF_STRING, ID_CONTINUOUS, L"&Continuous");
    AppendMenuW(layout, MF_STRING, ID_VIEW_TWO_PAGE, L"&Two pages");
    CheckMenuRadioItem(layout, ID_CONTINUOUS, ID_VIEW_TWO_PAGE,
                       view.GetViewMode() == ViewMode::Single     ? ID_SINGLE_PAGE
                       : view.GetViewMode() == ViewMode::TwoPage ? ID_VIEW_TWO_PAGE
                                                                 : ID_CONTINUOUS,
                       MF_BYCOMMAND);
    AppendMenuW(layout, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(layout,
                MF_STRING | check(view.CoverPage()) |
                    (view.GetViewMode() == ViewMode::TwoPage ? 0 : MF_GRAYED),
                ID_COVER_PAGE, L"Show &cover page separately");

    HMENU colors = CreatePopupMenu();
    AppendMenuW(colors, MF_STRING, ID_COLORS_NORMAL, L"&Normal");
    AppendMenuW(colors, MF_STRING, ID_COLORS_DARK, L"&Dark (night mode)");
    AppendMenuW(colors, MF_STRING, ID_COLORS_DIM, L"D&immed");
    CheckMenuRadioItem(colors, ID_COLORS_NORMAL, ID_COLORS_DIM,
                       ID_COLORS_NORMAL + view.GetPageColors(), MF_BYCOMMAND);

    HMENU theme = CreatePopupMenu();
    AppendMenuW(theme, MF_STRING, ID_THEME_SYSTEM, L"&System default");
    AppendMenuW(theme, MF_STRING, ID_THEME_LIGHT, L"&Light");
    AppendMenuW(theme, MF_STRING, ID_THEME_DARK, L"&Dark");
    CheckMenuRadioItem(theme, ID_THEME_SYSTEM, ID_THEME_DARK, ID_THEME_SYSTEM + m_settings.themeMode,
                       MF_BYCOMMAND);

    const Tab& tab = Active();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, L"&Open\x2026\tCtrl+O");
    AppendMenuW(m, MF_POPUP | (m_settings.recent.empty() ? MF_GRAYED : 0), (UINT_PTR)CreateRecentMenu(),
                L"Open rece&nt");
    AppendMenuW(m, MF_STRING | (doc && tab.dirty ? 0 : MF_GRAYED), ID_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(m, MF_STRING | docFlag, ID_SAVE_AS, L"Sa&ve as\x2026\tCtrl+Shift+S");
    AppendMenuW(m, MF_STRING, ID_CLOSE_TAB, L"&Close tab\tCtrl+W");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (doc && tab.canUndo ? 0 : MF_GRAYED), ID_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(m, MF_STRING | (doc && tab.canRedo ? 0 : MF_GRAYED), ID_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(m, MF_POPUP | docFlag, (UINT_PTR)CreateEditPdfMenu(), L"E&dit PDF");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    if (m_printing)
        AppendMenuW(m, MF_STRING, ID_CANCEL_PRINT, L"Cancel &printing");
    else
        AppendMenuW(m, MF_STRING | docFlag, ID_PRINT, L"&Print\x2026\tCtrl+P");
    AppendMenuW(m, MF_STRING | docFlag, ID_PROPERTIES, L"Document p&roperties\x2026\tCtrl+D");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (view.HasSelection() ? 0 : MF_GRAYED), ID_COPY, L"Cop&y\tCtrl+C");
    AppendMenuW(m, MF_STRING | docFlag, ID_SELECT_ALL, L"Select &all\tCtrl+A");
    AppendMenuW(m, MF_STRING | docFlag, ID_COPY_PAGE_IMAGE, L"Copy page as &image");
    AppendMenuW(m, MF_STRING | docFlag, ID_COPY_AREA_IMAGE, L"Copy area as i&mage");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | docFlag, ID_FIRST_PAGE, L"&First page\tHome");
    AppendMenuW(m, MF_STRING | docFlag, ID_LAST_PAGE, L"&Last page\tEnd");
    AppendMenuW(m, MF_STRING | docFlag, ID_GOTO_PAGE, L"&Go to page\x2026\tCtrl+G");
    AppendMenuW(m, MF_STRING | (view.CanGoBack() ? 0 : MF_GRAYED), ID_BACK, L"&Back\tAlt+Left");
    AppendMenuW(m, MF_STRING | (view.CanGoForward() ? 0 : MF_GRAYED), ID_FORWARD, L"For&ward\tAlt+Right");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | check(m_sidebar.Mode() == SidebarMode::Bookmarks),
                ID_SIDEBAR_BOOKMARKS, L"&Bookmarks panel\tCtrl+B");
    AppendMenuW(m, MF_STRING | check(m_sidebar.Mode() == SidebarMode::Thumbnails),
                ID_SIDEBAR_THUMBNAILS, L"T&humbnails panel\tCtrl+Shift+B");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)layout, L"Page la&yout");
    AppendMenuW(m, MF_STRING | docFlag, ID_ROTATE_LEFT, L"Rotate l&eft\tCtrl+L");
    AppendMenuW(m, MF_STRING | docFlag, ID_ROTATE_RIGHT, L"Rotate ri&ght\tCtrl+R");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | check(view.GetZoomMode() == ZoomMode::FitWidth), ID_FIT_WIDTH,
                L"Fit &width\tCtrl+2");
    AppendMenuW(m, MF_STRING | check(view.GetZoomMode() == ZoomMode::FitPage), ID_FIT_PAGE,
                L"Fit pa&ge\tCtrl+0");
    AppendMenuW(m, MF_STRING, ID_ACTUAL_SIZE, L"Actual si&ze\tCtrl+1");
    AppendMenuW(m, MF_STRING | check(m_fullscreen), ID_FULLSCREEN, L"F&ull screen\tF11");
    AppendMenuW(m, MF_STRING | docFlag | check(m_presenting), ID_PRESENT, L"&Presentation\tF5");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)colors, L"Page colo&urs");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)theme, L"&Theme");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COMMAND_PALETTE, L"&Command palette\x2026\tCtrl+K");
    AppendMenuW(m, MF_STRING, ID_REGISTER_DEFAULT, L"Set as &default PDF viewer\x2026");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"Abou&t " APP_NAME);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");

    RECT rc = m_toolbar.ItemScreenRect(ID_MENU);
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_TOPALIGN, rc.right, rc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);  // also destroys the submenus
}

void MainWindow::ShowZoomMenu() {
    static const int kMenuPresets[] = {3, 5, 7, 9, 10, 12, 14, 15};  // 33%..300%
    PdfView& view = View();
    HMENU m = CreatePopupMenu();
    const int current = (int)std::lround(view.Zoom() * 100);
    for (int idx : kMenuPresets) {
        const int pct = (int)std::lround(kZoomPresets[idx] * 100);
        std::wstring label = std::to_wstring(pct) + L"%";
        UINT flags = MF_STRING;
        if (view.GetZoomMode() == ZoomMode::Custom && pct == current) flags |= MF_CHECKED;
        AppendMenuW(m, flags, ID_ZOOM_PRESET_FIRST + idx, label.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (view.GetZoomMode() == ZoomMode::FitWidth ? MF_CHECKED : 0),
                ID_FIT_WIDTH, L"Fit width\tCtrl+2");
    AppendMenuW(m, MF_STRING | (view.GetZoomMode() == ZoomMode::FitPage ? MF_CHECKED : 0),
                ID_FIT_PAGE, L"Fit page\tCtrl+0");
    AppendMenuW(m, MF_STRING, ID_ACTUAL_SIZE, L"Actual size\tCtrl+1");
    RECT rc = m_toolbar.ItemScreenRect(ID_ZOOM_LABEL);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::UpdateUi() {
    if (m_tabs.empty()) return;
    PdfView& view = View();
    const bool doc = view.HasDocument();
    const int count = view.PageCount();
    const int page = doc ? view.CurrentPage() + 1 : 0;

    if (GetFocus() != m_pageEdit) {
        const std::wstring text = doc ? std::to_wstring(page) : L"";
        if (GetText(m_pageEdit) != text) SetWindowTextW(m_pageEdit, text.c_str());
    }
    m_toolbar.SetText(ID_PAGE_TOTAL, doc ? L"/ " + std::to_wstring(count) : L"");
    m_toolbar.SetText(ID_ZOOM_LABEL, std::to_wstring((int)std::lround(view.Zoom() * 100)) + L"%");
    m_toolbar.SetEnabled(ID_PREV_PAGE, doc && page > 1);
    m_toolbar.SetEnabled(ID_NEXT_PAGE, doc && page < count);
    m_toolbar.SetEnabled(ID_PAGE_EDIT, doc);
    m_toolbar.SetEnabled(ID_ZOOM_IN, doc);
    m_toolbar.SetEnabled(ID_ZOOM_OUT, doc);
    m_toolbar.SetEnabled(ID_ZOOM_LABEL, doc);
    m_toolbar.SetEnabled(ID_SEARCH, doc);
    const Tab& tab = Active();
    m_toolbar.SetEnabled(ID_SAVE, doc && tab.dirty);
    m_toolbar.SetEnabled(ID_EDIT_PDF_MENU, doc);
    m_toolbar.SetEnabled(ID_ADD_COMMENT, doc);
    m_toolbar.SetChecked(ID_EDIT_PDF_MENU, doc && view.Tool() == ViewTool::EditText);
    m_toolbar.SetChecked(ID_ADD_COMMENT, doc && view.Tool() == ViewTool::AddComment);
    m_toolbar.SetEnabled(ID_ANNOTATE_MENU, doc);
    m_toolbar.SetChecked(ID_ANNOTATE_MENU, doc && view.Tool() >= ViewTool::AddText);
    m_toolbar.SetEnabled(ID_UNDO, doc && tab.canUndo);
    m_toolbar.SetEnabled(ID_REDO, doc && tab.canRedo);
    if (doc && m_sidebar.Mode() == SidebarMode::Thumbnails)
        m_sidebar.Thumbs().SetCurrentPage(view.CurrentPage());
}

void MainWindow::UpdateTitle() {
    const Tab& tab = Active();
    std::wstring title = tab.docId ? (tab.dirty ? L"\x2022 " : L"") + FileNameFromPath(tab.path) +
                                         L" - " APP_NAME
                                   : std::wstring(APP_NAME);
    if (m_printing)
        title += L"  (printing " + std::to_wstring(m_printDone) + L" of " +
                 std::to_wstring(m_printTotal) + L")";
    SetWindowTextW(m_hwnd, title.c_str());
}

// ===========================================================================
// Documents
// ===========================================================================
void MainWindow::ShowOpenDialog() {
    // Multiple selection: every chosen file opens in its own tab.
    std::vector<wchar_t> buf(65536, L'\0');
    const std::wstring dir = DirectoryFromPath(Active().path);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY |
                OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&ofn)) return;

    // Single file: "C:\dir\a.pdf\0". Several: "C:\dir\0a.pdf\0b.pdf\0\0".
    const wchar_t* p = buf.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (!*p) {
        OpenFile(first, 0);
        return;
    }
    for (; *p; p += wcslen(p) + 1) OpenFile(first + L"\\" + p, 0);
}

void MainWindow::OpenFile(const std::wstring& path, int page, const std::string& password,
                          int tabIndex, bool activate) {
    const std::wstring fullPath = FullPath(path);
    if (!FileExists(fullPath)) {
        std::wstring msg = FileNameFromPath(fullPath) + L"\n\n" + OpenErrorText(OpenError::NotFound);
        MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }

    // Already open (or opening)? Just switch to that tab.
    if (tabIndex < 0 && password.empty()) {
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            const Tab& t = *m_tabs[i];
            if ((t.docId || t.pendingDocId) && SamePath(t.path, fullPath)) {
                if (activate) ActivateTab((int)i);
                if (t.docId && page > 0) t.view->GoToPage(page);
                return;
            }
        }
    }

    // Reuse the current tab if it is empty, otherwise open a new one.
    if (tabIndex < 0) {
        const Tab& cur = Active();
        tabIndex = (!cur.docId && !cur.pendingDocId) ? m_active : NewTab();
    }
    Tab& tab = *m_tabs[(size_t)tabIndex];
    if (password.empty()) tab.passwordAttempts = 0;
    tab.pendingDocId = m_nextDocId++;
    tab.pendingPage = std::max(0, page);
    if (!tab.docId) tab.path = fullPath;
    if (!tab.view->HasDocument())
        tab.view->SetMessage(L"Opening " + FileNameFromPath(fullPath) + L"\x2026");
    m_worker.OpenDocument(tab.pendingDocId, fullPath, password);
    if (activate)
        ActivateTab(tabIndex);
    else
        UpdateTabs();
}

void MainWindow::OnDocLoaded(DocLoadResult* result) {
    std::unique_ptr<DocLoadResult> res(result);
    int index = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i]->pendingDocId == res->docId) index = (int)i;
    if (index < 0) {  // the tab was closed while loading
        if (res->error == OpenError::None) m_worker.CloseDocument(res->docId);
        return;
    }
    Tab& tab = *m_tabs[(size_t)index];
    const std::wstring name = FileNameFromPath(res->path);

    if (res->error == OpenError::Password) {
        ActivateTab(index);  // show which document is asking
        PasswordPrompt prompt;
        prompt.fileName = name;
        prompt.retry = tab.passwordAttempts > 0;
        if (DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_PASSWORD), m_hwnd, PasswordDlgProc,
                            (LPARAM)&prompt) == IDOK) {
            ++tab.passwordAttempts;
            std::string pw = WideToUtf8(prompt.password);
            SecureZeroMemory(prompt.password.data(), prompt.password.size() * sizeof(wchar_t));
            OpenFile(res->path, tab.pendingPage, pw, index);
            SecureZeroMemory(pw.data(), pw.size());
        } else {
            tab.pendingDocId = 0;
            if (!tab.docId) CloseTab(index);
        }
        return;
    }

    if (res->error != OpenError::None) {
        tab.pendingDocId = 0;
        std::wstring msg = name + L"\n\n" + OpenErrorText(res->error);
        if (!tab.docId) CloseTab(index);  // nothing to show in that tab
        MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }

    if (tab.docId) m_worker.CloseDocument(tab.docId);
    tab.docId = res->docId;
    tab.pendingDocId = 0;
    tab.passwordAttempts = 0;
    tab.path = res->path;
    tab.search.Reset();
    tab.search.query.clear();
    tab.outline = std::move(res->outline);
    tab.info = std::move(res->info);
    tab.dirty = tab.canUndo = tab.canRedo = false;
    tab.view->SetDocument(tab.docId, std::move(res->pageSizes), tab.pendingPage);
    AddRecent(tab.path);
    UpdateTabs();
    if (index == m_active) {
        SyncSidebar();
        UpdateTitle();
        UpdateUi();
        if (m_searchVisible && !GetText(m_searchEdit).empty()) StartSearch();
        UpdateSearchStatus();
        if (GetFocus() != m_searchEdit) SetFocus(View().Hwnd());
    }
}

void MainWindow::OnCopyData(const COPYDATASTRUCT* cds) {
    // Another instance forwarded "page\npath" (Explorer double-click while
    // Feather PDF is running): open it as a tab here.
    if (!cds || cds->dwData != kCopyDataOpenFile || !cds->lpData || cds->cbData < sizeof(wchar_t))
        return;
    std::wstring data((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
    data.resize(wcsnlen(data.c_str(), data.size()));
    const size_t nl = data.find(L'\n');
    if (nl == std::wstring::npos) return;
    const int page = _wtoi(data.substr(0, nl).c_str());
    const std::wstring path = data.substr(nl + 1);
    if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    SetForegroundWindow(m_hwnd);
    if (!path.empty()) OpenFile(path, page);
}

void MainWindow::CopyToClipboard(const std::wstring& text) {
    if (text.empty() || !OpenClipboard(m_hwnd)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

// ===========================================================================
// Search (always in the active tab; switching tabs stops a running search)
// ===========================================================================
void MainWindow::ShowSearch(bool show) {
    if (show && !View().HasDocument()) return;
    if (show) {
        if (!m_searchVisible) {
            m_searchVisible = true;
            Layout();
        }
        SetFocus(m_searchEdit);
        SendMessageW(m_searchEdit, EM_SETSEL, 0, -1);
        return;
    }
    if (!m_searchVisible) return;
    m_searchVisible = false;
    KillTimer(m_hwnd, kSearchTimer);
    CancelSearch();
    Active().search.Reset();
    Active().search.query.clear();
    Layout();
    InvalidateRect(View().Hwnd(), nullptr, FALSE);  // remove highlights
    SetFocus(View().Hwnd());
}

void MainWindow::CancelSearch() {
    m_worker.CancelSearch();
    Active().search.running = false;
}

void MainWindow::StartSearch() {
    KillTimer(m_hwnd, kSearchTimer);
    Tab& tab = Active();
    const std::wstring query = GetText(m_searchEdit);
    m_worker.CancelSearch();
    tab.search.Reset();
    tab.search.query = query;
    tab.search.matchCase = m_settings.matchCase;
    tab.search.id = m_nextSearchId++;
    if (!query.empty() && tab.docId) {
        tab.search.running = true;
        tab.search.pageCount = tab.view->PageCount();
        m_worker.StartSearch(tab.docId, tab.search.id, query, tab.search.matchCase,
                             tab.view->CurrentPage());
    }
    UpdateSearchStatus();
    InvalidateRect(tab.view->Hwnd(), nullptr, FALSE);
}

void MainWindow::FindNext(bool forward) {
    if (!m_searchVisible) {
        ShowSearch(true);
        return;
    }
    SearchState& search = Active().search;
    // Enter after editing the query starts a new search.
    if (GetText(m_searchEdit) != search.query || search.matchCase != m_settings.matchCase) {
        StartSearch();
        return;
    }
    if (search.matches.empty()) return;
    if (forward)
        search.Next();
    else
        search.Prev();
    if (const SearchHit* hit = search.Current()) View().ScrollToHit(*hit);
    UpdateSearchStatus();
    InvalidateRect(View().Hwnd(), nullptr, FALSE);
}

void MainWindow::OnSearchResult(SearchPageResult* result) {
    std::unique_ptr<SearchPageResult> res(result);
    Tab& tab = Active();
    if (res->searchId != tab.search.id || res->docId != tab.docId) return;  // stale
    tab.search.pagesDone = res->pagesDone;
    if (res->finished) tab.search.running = false;
    const bool hadHits = !res->hits.empty();
    if (tab.search.AddHits(std::move(res->hits))) {
        if (const SearchHit* hit = tab.search.Current()) tab.view->ScrollToHit(*hit);
    }
    if (hadHits) InvalidateRect(tab.view->Hwnd(), nullptr, FALSE);
    UpdateSearchStatus();
}

void MainWindow::UpdateSearchStatus() {
    const SearchState& search = Active().search;
    m_searchBar.SetText(ID_SEARCH_STATUS, search.StatusText());
    const bool any = !search.matches.empty();
    m_searchBar.SetEnabled(ID_FIND_NEXT, any);
    m_searchBar.SetEnabled(ID_FIND_PREV, any);
}

// ===========================================================================
// Misc
// ===========================================================================
void MainWindow::ToggleFullscreen() {
    LONG_PTR style = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
    if (!m_fullscreen) {
        m_restorePlacement.length = sizeof(m_restorePlacement);
        GetWindowPlacement(m_hwnd, &m_restorePlacement);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        m_fullscreen = true;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        m_fullscreen = false;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_hwnd, &m_restorePlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    Layout();
    SetFocus(View().Hwnd());
}

void MainWindow::SetThemeMode(int mode) {
    if (mode == m_settings.themeMode) return;
    m_settings.themeMode = mode;
    OnThemeChanged();
}

void MainWindow::OnThemeChanged() {
    ReloadTheme((ThemeMode)m_settings.themeMode);
    ApplyWindowTheme(m_hwnd);
    m_tabBar.OnThemeChanged();
    m_sidebar.OnThemeChanged();
    m_toolbar.OnThemeChanged();
    m_searchBar.OnThemeChanged();
    for (auto& t : m_tabs) t->view->OnThemeChanged();
    // Nudge the frame so DWM repaints the title bar colour.
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}

void MainWindow::SaveSettings() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (m_fullscreen) {
        wp = m_restorePlacement;
    } else {
        GetWindowPlacement(m_hwnd, &wp);
    }
    if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE) {
        wp.showCmd = (wp.flags & WPF_RESTORETOMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    }
    m_settings.placement = wp;
    m_settings.hasPlacement = true;
    PdfView& view = View();
    m_settings.zoomMode = (int)view.GetZoomMode();
    m_settings.zoom = view.Zoom();
    m_settings.viewMode = (int)view.GetViewMode();
    m_settings.coverPage = view.CoverPage();
    m_settings.pageColors = view.GetPageColors();
    m_settings.sidebarMode = (int)m_sidebar.Mode();

    // Remember every open tab and where each was being read.
    m_settings.session.clear();
    m_settings.activeTab = 0;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const Tab& t = *m_tabs[i];
        if (!t.docId) continue;
        if ((int)i == m_active) m_settings.activeTab = (int)m_settings.session.size();
        m_settings.session.push_back({t.path, t.view->CurrentPage()});
    }
    m_settings.Save();
}

// ===========================================================================
// Sidebar (bookmarks / thumbnails)
// ===========================================================================
void MainWindow::SetSidebarMode(SidebarMode mode) {
    m_sidebar.SetMode(mode);
    SyncSidebar();
    Layout();
}

void MainWindow::SyncSidebar() {
    if (m_tabs.empty()) return;
    Tab& tab = Active();
    switch (m_sidebar.Mode()) {
        case SidebarMode::Bookmarks:
            m_sidebar.SetOutline(tab.docId ? &tab.outline : nullptr);
            break;
        case SidebarMode::Thumbnails:
            if (tab.docId) {
                m_sidebar.Thumbs().SetDocument(tab.docId, tab.view->PageSizes(),
                                               tab.view->Rotation(), tab.view->GetPageColors());
                m_sidebar.Thumbs().SetCurrentPage(tab.view->CurrentPage());
            } else {
                m_sidebar.Thumbs().Clear();
            }
            break;
        case SidebarMode::None:
            break;
    }
}

void MainWindow::OnSidebar(WPARAM event, LPARAM value) {
    Tab& tab = Active();
    if (!tab.docId) return;
    if (event == kSidebarPageClicked) {
        tab.view->PushHistory();
        tab.view->GoToPage((int)value);
    } else if (event == kSidebarPagesMoved) {
        MovePages((int)value);
    } else if (event == kSidebarPagesMenu) {
        ShowPagesMenu({GET_X_LPARAM(value), GET_Y_LPARAM(value)});
    } else if (event == kSidebarDeletePages) {
        DeletePages();
    } else if (event == kSidebarOutlineClicked && value >= 0 &&
               (size_t)value < tab.outline.size()) {
        const LinkTarget& target = tab.outline[(size_t)value].target;
        if (target.page >= 0)
            tab.view->GoToTarget(target);
        else if (!target.uri.empty())
            OpenExternalLink(m_hwnd, target.uri);
    }
}

RECT MainWindow::SplitterRect() const {
    RECT r{};
    if (m_sidebar.Mode() == SidebarMode::None || m_fullscreen) return r;
    RECT sb;
    GetWindowRect(m_sidebar.Hwnd(), &sb);
    MapWindowPoints(nullptr, m_hwnd, (POINT*)&sb, 2);
    const int dpi = GetWindowDpi(m_hwnd);
    return {sb.right, sb.top, sb.right + Dpi(4, dpi), sb.bottom};
}

// ===========================================================================
// Page colours (all tabs)
// ===========================================================================
void MainWindow::SetPageColors(int mode) {
    m_settings.pageColors = mode;
    for (auto& t : m_tabs) t->view->SetPageColors(mode);
    SyncSidebar();
}

// ===========================================================================
// Printing
// ===========================================================================
void MainWindow::Print() {
    Tab& tab = Active();
    if (!tab.docId) return;
    if (m_printing) {
        MessageBoxW(m_hwnd, L"A document is already being printed.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    const int count = tab.view->PageCount();
    PRINTPAGERANGE ranges[32] = {};
    ranges[0] = {1, (DWORD)count};
    PRINTDLGEXW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = m_hwnd;
    pd.hDevMode = m_devMode;  // remembers the last printer and its settings
    pd.hDevNames = m_devNames;
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION;
    pd.nPageRanges = 1;
    pd.nMaxPageRanges = 32;
    pd.lpPageRanges = ranges;
    pd.nMinPage = 1;
    pd.nMaxPage = (DWORD)count;
    pd.nCopies = 1;
    pd.nStartPage = START_PAGE_GENERAL;
    const HRESULT hr = PrintDlgExW(&pd);
    m_devMode = pd.hDevMode;
    m_devNames = pd.hDevNames;
    if (FAILED(hr) || pd.dwResultAction != PD_RESULT_PRINT || !pd.hDC) {
        if (pd.hDC) DeleteDC(pd.hDC);
        return;
    }

    // Pages to print, in order.
    std::vector<int> pages;
    if (pd.Flags & PD_CURRENTPAGE) {
        pages.push_back(tab.view->CurrentPage());
    } else if (pd.Flags & PD_PAGENUMS) {
        for (DWORD i = 0; i < pd.nPageRanges; ++i) {
            const int from = std::max(1, (int)std::min(ranges[i].nFromPage, ranges[i].nToPage));
            const int to = std::min(count, (int)std::max(ranges[i].nFromPage, ranges[i].nToPage));
            for (int p = from; p <= to; ++p) pages.push_back(p - 1);
        }
    } else {
        for (int p = 0; p < count; ++p) pages.push_back(p);
    }
    // Copies the driver can not do itself (nCopies is 1 when it can).
    const int copies = std::max(1, (int)pd.nCopies);
    if (copies > 1) {
        std::vector<int> expanded;
        if (pd.Flags & PD_COLLATE) {
            for (int c = 0; c < copies; ++c) expanded.insert(expanded.end(), pages.begin(), pages.end());
        } else {
            for (int p : pages) expanded.insert(expanded.end(), (size_t)copies, p);
        }
        pages.swap(expanded);
    }
    if (pages.empty()) {
        DeleteDC(pd.hDC);
        return;
    }

    PrintJob job;
    job.docId = tab.docId;
    job.dc = pd.hDC;  // owned by the worker from here on
    job.docName = FileNameFromPath(tab.path);
    job.pages = std::move(pages);
    m_printing = true;
    m_printCancelled = false;
    m_printDone = 0;
    m_printTotal = (int)job.pages.size();
    m_worker.StartPrint(std::move(job));
    UpdateTitle();
}

void MainWindow::OnPrintProgress(int done, int total) {
    if (total < 0) {  // failed or cancelled
        const bool wasCancelled = m_printCancelled;
        m_printing = false;
        UpdateTitle();
        if (!wasCancelled)
            MessageBoxW(m_hwnd, L"The document could not be printed.", APP_NAME, MB_ICONWARNING);
        return;
    }
    m_printDone = done;
    m_printTotal = total;
    if (done >= total) m_printing = false;
    UpdateTitle();
}

// ===========================================================================
// Document properties
// ===========================================================================
namespace {
// "D:20240131154500+01'00'" -> "2024-01-31 15:45" (as written in the file)
std::wstring FormatInfoDate(const std::wstring& d) {
    std::wstring s = d.rfind(L"D:", 0) == 0 ? d.substr(2) : d;
    if (s.size() < 8) return d;
    for (size_t i = 0; i < 8; ++i)
        if (!iswdigit(s[i])) return d;
    std::wstring out = s.substr(0, 4) + L"-" + s.substr(4, 2) + L"-" + s.substr(6, 2);
    if (s.size() >= 12 && iswdigit(s[8]) && iswdigit(s[11]))
        out += L" " + s.substr(8, 2) + L":" + s.substr(10, 2);
    return out;
}

std::wstring FormatBytes(ULONGLONG bytes) {
    wchar_t buf[64];
    if (bytes >= (1ull << 20))
        swprintf_s(buf, L"%.1f MB (%llu bytes)", bytes / 1048576.0, bytes);
    else if (bytes >= 1024)
        swprintf_s(buf, L"%.1f KB (%llu bytes)", bytes / 1024.0, bytes);
    else
        swprintf_s(buf, L"%llu bytes", bytes);
    return buf;
}
}  // namespace

void MainWindow::ShowProperties() {
    const Tab& tab = Active();
    if (!tab.docId) return;
    const DocInfo& info = tab.info;
    std::wstring text;
    auto line = [&](const wchar_t* label, const std::wstring& value) {
        if (!value.empty()) text += std::wstring(label) + L"\t" + value + L"\n";
    };
    line(L"File:", FileNameFromPath(tab.path));
    line(L"Location:", DirectoryFromPath(tab.path));
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(tab.path.c_str(), GetFileExInfoStandard, &fad))
        line(L"File size:",
             FormatBytes(((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow));
    text += L"\n";
    line(L"Title:", info.title);
    line(L"Author:", info.author);
    line(L"Subject:", info.subject);
    line(L"Keywords:", info.keywords);
    line(L"Created:", FormatInfoDate(info.created));
    line(L"Modified:", FormatInfoDate(info.modified));
    line(L"Creator:", info.creator);
    line(L"Producer:", info.producer);
    text += L"\n";
    if (info.version)
        line(L"Version:", L"PDF " + std::to_wstring(info.version / 10) + L"." + std::to_wstring(info.version % 10));
    line(L"Pages:", std::to_wstring(tab.view->PageCount()));
    const int page = tab.view->CurrentPage();
    if (page >= 0 && page < tab.view->PageCount()) {
        const SizeF& sz = tab.view->PageSizes()[(size_t)page];
        wchar_t buf[128];
        swprintf_s(buf, L"%.0f \x00D7 %.0f mm  (%.2f \x00D7 %.2f in)", sz.w / 72 * 25.4,
                   sz.h / 72 * 25.4, sz.w / 72, sz.h / 72);
        line(L"Page size:", buf);
    }
    line(L"Encrypted:", info.encrypted ? L"Yes" : L"No");
    MessageBoxW(m_hwnd, text.c_str(), L"Document properties", MB_OK | MB_ICONINFORMATION);
}

// ===========================================================================
// Page image -> clipboard (24-bit DIB, understood by every application)
// ===========================================================================
void MainWindow::CopyImageToClipboard(TileResult* image) {
    std::unique_ptr<TileResult> res(image);
    const PixelBuffer& px = res->pixels;
    if (!px.bits) {
        MessageBoxW(m_hwnd, L"The image is too large to copy.", APP_NAME, MB_ICONWARNING);
        return;
    }
    const size_t stride = ((size_t)px.width * 3 + 3) & ~(size_t)3;
    const size_t bytes = sizeof(BITMAPINFOHEADER) + stride * (size_t)px.height;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return;
    auto* dst = (uint8_t*)GlobalLock(mem);
    if (!dst) {
        GlobalFree(mem);
        return;
    }
    auto* bih = (BITMAPINFOHEADER*)dst;
    *bih = BITMAPINFOHEADER{};
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = px.width;
    bih->biHeight = px.height;  // bottom-up
    bih->biPlanes = 1;
    bih->biBitCount = 24;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = (DWORD)(stride * px.height);
    uint8_t* bits = dst + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < px.height; ++y) {
        const uint8_t* src = px.bits + (size_t)(px.height - 1 - y) * px.width * 4;
        uint8_t* row = bits + (size_t)y * stride;
        for (int x = 0; x < px.width; ++x) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    GlobalUnlock(mem);
    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_DIB, mem)) GlobalFree(mem);
        CloseClipboard();
    } else {
        GlobalFree(mem);
    }
}

// ===========================================================================
// Editing
//
// The UI only describes changes (EditOp) and shows results; DocEditor on the
// render worker applies them, keeps the undo history and saves safely.
// Every edit, undo and redo gives the document a new id, so tiles, text
// and search results that are still queued for the old state are dropped
// (TabByDocId no longer finds them) instead of flashing wrong content.
// ===========================================================================
bool MainWindow::CanEdit() {
    const Tab& tab = Active();
    return tab.docId && !tab.pendingDocId && tab.view->HasDocument();
}

void MainWindow::SendEdit(EditOp&& op) {
    if (!CanEdit()) return;
    Tab& tab = Active();
    if (tab.search.running) CancelSearch();
    const uint32_t oldId = tab.docId;
    tab.docId = m_nextDocId++;
    tab.view->BeginEdit(tab.docId);
    m_worker.Edit(oldId, tab.docId, std::move(op));
    // Optimistic state until the worker answers.
    tab.dirty = tab.canUndo = true;
    tab.canRedo = false;
    UpdateTabs();
    UpdateTitle();
    UpdateUi();
}

void MainWindow::UndoRedo(bool redo) {
    if (!CanEdit()) return;
    Tab& tab = Active();
    if (redo ? !tab.canRedo : !tab.canUndo) return;
    if (tab.search.running) CancelSearch();
    const uint32_t oldId = tab.docId;
    tab.docId = m_nextDocId++;
    tab.view->BeginEdit(tab.docId);
    if (redo)
        m_worker.Redo(oldId, tab.docId);
    else
        m_worker.Undo(oldId, tab.docId);
    tab.canUndo = tab.canRedo = false;  // until the worker answers
    UpdateUi();
}

void MainWindow::SaveDocument(int index, bool saveAs, uint32_t flags) {
    if (index < 0 || index >= (int)m_tabs.size()) return;
    Tab& tab = *m_tabs[(size_t)index];
    if (!tab.docId || tab.pendingDocId) return;
    std::wstring target = tab.path;
    if (saveAs) {
        std::vector<wchar_t> buf(32768, L'\0');
        const std::wstring name = FileNameFromPath(tab.path);
        wcsncpy_s(buf.data(), buf.size(), name.c_str(), _TRUNCATE);
        const std::wstring dir = DirectoryFromPath(tab.path);
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0";
        ofn.lpstrFile = buf.data();
        ofn.nMaxFile = (DWORD)buf.size();
        ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
        ofn.lpstrDefExt = L"pdf";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
        if (!GetSaveFileNameW(&ofn)) return;
        target = FullPath(buf.data());
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if ((int)i != index && m_tabs[i]->docId && SamePath(m_tabs[i]->path, target)) {
                MessageBoxW(m_hwnd,
                            L"That file is open in another tab. Close it first, or choose "
                            L"another name.",
                            APP_NAME, MB_ICONWARNING);
                return;
            }
        }
    }
    tab.saveDocId = tab.docId;
    m_worker.Save(tab.docId, target, flags);
}

bool MainWindow::ConfirmCloseTab(int index) {
    Tab& tab = *m_tabs[(size_t)index];
    if (!tab.docId || !tab.dirty) return true;
    ActivateTab(index);
    const std::wstring msg =
        L"Do you want to save the changes to \x201C" + FileNameFromPath(tab.path) + L"\x201D?";
    switch (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING)) {
        case IDYES:
            SaveDocument(index, false, kAfterSaveCloseTab);  // closes when saved
            return false;
        case IDNO: return true;
        default: return false;
    }
}

bool MainWindow::ConfirmQuit() {
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        Tab& tab = *m_tabs[i];
        if (!tab.docId || !tab.dirty || tab.discardOnQuit) continue;
        ActivateTab((int)i);
        const std::wstring msg = L"Do you want to save the changes to \x201C" +
                                 FileNameFromPath(tab.path) + L"\x201D before closing?";
        const int answer = MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING);
        if (answer == IDYES) {
            ++m_quitSaves;
            SaveDocument((int)i, false, kAfterSaveQuit);
        } else if (answer == IDNO) {
            tab.discardOnQuit = true;
        } else {
            for (auto& t : m_tabs) t->discardOnQuit = false;
            m_quitAfterSaves = false;  // saves already started still finish
            return false;
        }
    }
    if (m_quitSaves > 0) {
        m_quitAfterSaves = true;  // WM_CLOSE is posted again when they finish
        return false;
    }
    return true;
}

void MainWindow::OnDocEdited(EditResult* result) {
    std::unique_ptr<EditResult> res(result);
    int index = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const Tab& t = *m_tabs[i];
        if (t.docId == res->docId ||
            (res->action == EditAction::Save && t.saveDocId == res->docId))
            index = (int)i;
    }
    if (res->action == EditAction::Save && (res->flags & kAfterSaveQuit)) {
        --m_quitSaves;
        if (!res->ok) m_quitAfterSaves = false;
    }
    if (index < 0) return;  // tab closed, or a newer edit is already on its way
    Tab& tab = *m_tabs[(size_t)index];
    const bool current = tab.docId == res->docId;  // no newer edit sent since

    if (current) {
        tab.dirty = res->dirty;
        tab.canUndo = res->canUndo;
        tab.canRedo = res->canRedo;
        tab.path = res->path;
        tab.info = std::move(res->info);
        if (res->action != EditAction::Save) {
            tab.outline = std::move(res->outline);
            tab.view->EndEdit(std::move(res->pageSizes), res->focusPage);
            if (index == m_active) {
                if (m_sidebar.Mode() == SidebarMode::Thumbnails) {
                    m_sidebar.Thumbs().Reload(tab.docId, tab.view->PageSizes());
                    m_sidebar.Thumbs().SetSelection(res->select);
                    m_sidebar.Thumbs().SetCurrentPage(tab.view->CurrentPage());
                } else {
                    SyncSidebar();
                }
                // Results found before the edit point at old positions.
                if (m_searchVisible && !GetText(m_searchEdit).empty()) {
                    StartSearch();
                } else {
                    tab.search.Reset();
                    UpdateSearchStatus();
                }
            } else {
                tab.search.Reset();
            }
        }
    }
    UpdateTabs();
    if (index == m_active) {
        UpdateTitle();
        UpdateUi();
    }

    if (!res->ok) {
        const std::wstring what = res->action == EditAction::Save
                                      ? L"\x201C" + FileNameFromPath(tab.path) + L"\x201D could not be saved.\n\n"
                                      : std::wstring();
        MessageBoxW(m_hwnd, (what + res->error).c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }
    if (res->action == EditAction::Edit && current && index == m_active) {
        if (!res->note.empty()) MessageBoxW(m_hwnd, res->note.c_str(), APP_NAME, MB_ICONINFORMATION);
        if (res->fontChanged && !m_toldAboutFonts) {
            m_toldAboutFonts = true;
            MessageBoxW(m_hwnd,
                        L"The font this text was written in does not contain all the letters "
                        L"you typed (PDF files often hold only the letters they use), so a "
                        L"similar font was used for the changed text.\n\nUndo (Ctrl+Z) takes the "
                        L"change back.",
                        APP_NAME, MB_ICONINFORMATION);
        }
    }
    if (res->action == EditAction::Save) {
        if (res->flags & kAfterSaveCloseTab) CloseTab(index, true);
        if ((res->flags & kAfterSaveQuit) && m_quitAfterSaves && m_quitSaves == 0)
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
}

std::vector<int> MainWindow::SelectedPages() {
    Tab& tab = Active();
    if (m_sidebar.Mode() == SidebarMode::Thumbnails) {
        std::vector<int> sel = m_sidebar.Thumbs().Selection();
        if (!sel.empty() && sel.back() < tab.view->PageCount()) return sel;
    }
    return {tab.view->CurrentPage()};
}

void MainWindow::DeletePages() {
    if (!CanEdit()) return;
    EditOp op;
    op.kind = EditOp::DeletePages;
    op.pages = SelectedPages();
    if ((int)op.pages.size() >= View().PageCount()) {
        MessageBoxW(m_hwnd, L"A document must keep at least one page.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    SendEdit(std::move(op));
}

void MainWindow::RotatePages(int turns) {
    if (!CanEdit()) return;
    EditOp op;
    op.kind = EditOp::RotatePages;
    op.pages = SelectedPages();
    op.turns = turns;
    SendEdit(std::move(op));
}

void MainWindow::MovePages(int gap) {
    if (!CanEdit()) return;
    EditOp op;
    op.kind = EditOp::MovePages;
    op.pages = m_sidebar.Thumbs().Selection();
    if (op.pages.empty()) return;
    // FPDF_MovePages wants the index of the first moved page afterwards.
    int before = 0;
    for (int p : op.pages)
        if (p < gap) ++before;
    op.index = gap - before;
    SendEdit(std::move(op));
}

void MainWindow::InsertBlankPage() {
    if (!CanEdit()) return;
    const int after = SelectedPages().back();
    EditOp op;
    op.kind = EditOp::InsertBlank;
    op.index = after + 1;
    op.size = View().PageSizes()[(size_t)after];  // same size as its neighbour
    SendEdit(std::move(op));
}

std::vector<std::wstring> MainWindow::PickPdfFiles(bool multiple, const wchar_t* title) {
    std::vector<wchar_t> buf(65536, L'\0');
    const std::wstring dir = DirectoryFromPath(Active().path);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY |
                (multiple ? OFN_ALLOWMULTISELECT : 0);
    if (!GetOpenFileNameW(&ofn)) return {};
    // Single file: "C:\dir\a.pdf\0". Several: "C:\dir\0a.pdf\0b.pdf\0\0".
    std::vector<std::wstring> files;
    const wchar_t* p = buf.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (!*p) return {first};
    for (; *p; p += wcslen(p) + 1) files.push_back(first + L"\\" + p);
    return files;
}

void MainWindow::InsertPagesFromFile() {
    if (!CanEdit()) return;
    const int after = SelectedPages().back();
    const auto files = PickPdfFiles(false, L"Insert pages from");
    if (files.empty()) return;
    EditOp op;
    op.kind = EditOp::InsertFiles;
    op.index = after + 1;
    op.sources.push_back({files[0]});
    SendEdit(std::move(op));
}

void MainWindow::MergeFiles() {
    if (!CanEdit()) return;
    const auto files = PickPdfFiles(true, L"Add PDFs to the end of this document");
    if (files.empty()) return;
    EditOp op;
    op.kind = EditOp::InsertFiles;
    op.index = View().PageCount();
    for (const auto& f : files) op.sources.push_back({f});
    SendEdit(std::move(op));
}

void MainWindow::MergeTabs() {
    if (!CanEdit()) return;
    EditOp op;
    op.kind = EditOp::InsertFiles;
    op.index = View().PageCount();
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const Tab& t = *m_tabs[i];
        if ((int)i != m_active && t.docId && !t.pendingDocId) {
            ImportSource src;
            src.docId = t.docId;  // its current state, unsaved changes included
            op.sources.push_back(src);
        }
    }
    if (op.sources.empty()) {
        MessageBoxW(m_hwnd, L"Open the other PDFs in tabs first. Their pages are then added to "
                            L"the end of this document, in tab order.",
                    APP_NAME, MB_ICONINFORMATION);
        return;
    }
    SendEdit(std::move(op));
}

namespace {
struct ExtractPrompt {
    std::wstring range;
    int pageCount = 0;
    bool separate = false;
    std::vector<int> pages;
};

INT_PTR CALLBACK ExtractDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* p = (ExtractPrompt*)lp;
            const std::wstring text = L"Pages to save as a new PDF (for example 1-3, 5, 8-). "
                                      L"The document has " + std::to_wstring(p->pageCount) +
                                      L" pages.";
            SetDlgItemTextW(dlg, IDC_EXTRACT_TEXT, text.c_str());
            SetDlgItemTextW(dlg, IDC_EXTRACT_RANGE, p->range.c_str());
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                auto* p = (ExtractPrompt*)GetWindowLongPtrW(dlg, DWLP_USER);
                p->range = GetText(GetDlgItem(dlg, IDC_EXTRACT_RANGE));
                if (!ParsePageRanges(p->range, p->pageCount, p->pages)) {
                    MessageBoxW(dlg, L"Enter page numbers or ranges, such as 1-3, 5, 8-.", APP_NAME,
                                MB_ICONWARNING);
                    SetFocus(GetDlgItem(dlg, IDC_EXTRACT_RANGE));
                    return TRUE;
                }
                p->separate = IsDlgButtonChecked(dlg, IDC_EXTRACT_SEPARATE) == BST_CHECKED;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

void MainWindow::ExtractPages() {
    if (!CanEdit()) return;
    Tab& tab = Active();
    ExtractPrompt prompt;
    prompt.pageCount = tab.view->PageCount();
    prompt.range = FormatPageRanges(SelectedPages());
    if (DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_EXTRACT), m_hwnd, ExtractDlgProc,
                        (LPARAM)&prompt) != IDOK)
        return;

    std::wstring stem = FileNameFromPath(tab.path);
    if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".pdf") == 0)
        stem.resize(stem.size() - 4);
    std::wstring suggested = prompt.separate ? stem + L".pdf"
                                             : stem + L" (pages " + prompt.range + L").pdf";
    for (wchar_t& c : suggested)
        if (wcschr(L"\\/:*?\"<>|", c)) c = L'-';
    std::vector<wchar_t> buf(32768, L'\0');
    wcsncpy_s(buf.data(), buf.size(), suggested.c_str(), _TRUNCATE);
    const std::wstring dir = DirectoryFromPath(tab.path);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrTitle = prompt.separate ? L"Save pages as (each page gets its number added)"
                                     : L"Save pages as";
    ofn.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY |
                (prompt.separate ? 0 : OFN_OVERWRITEPROMPT);
    if (!GetSaveFileNameW(&ofn)) return;
    const std::wstring target = FullPath(buf.data());
    for (const auto& t : m_tabs) {
        if (t->docId && SamePath(t->path, target)) {
            MessageBoxW(m_hwnd, L"That file is open in a tab. Choose another name.", APP_NAME,
                        MB_ICONWARNING);
            return;
        }
    }
    m_worker.Extract(tab.docId, std::move(prompt.pages), target, prompt.separate);
}

void MainWindow::OnExtracted(ExtractResult* result) {
    std::unique_ptr<ExtractResult> res(result);
    if (!res->ok) {
        MessageBoxW(m_hwnd, res->error.c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }
    if (res->files.size() == 1) {
        const std::wstring msg = L"Saved \x201C" + FileNameFromPath(res->files[0]) +
                                 L"\x201D.\n\nOpen it in a new tab?";
        if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
            OpenFile(res->files[0], 0);
    } else if (!res->files.empty()) {
        const std::wstring msg = L"Saved " + std::to_wstring(res->files.size()) + L" files in\n" +
                                 DirectoryFromPath(res->files[0]) + L".";
        MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONINFORMATION);
    }
}

void MainWindow::AddMarkup(int type, const std::wstring& comment) {
    if (!CanEdit()) return;
    EditOp op;
    op.kind = EditOp::Markup;
    if (!View().GetSelection(op.from, op.to)) return;
    op.markup = type;
    op.text = comment;
    if (!comment.empty()) op.author = CurrentUserName();
    op.color = type == kMarkupHighlight   ? kHighlightColors[m_settings.highlightColor]
               : type == kMarkupUnderline ? kUnderlineColor
                                          : kStrikeOutColor;
    SendEdit(std::move(op));
}

HMENU MainWindow::CreatePagesMenu() {
    const bool several = SelectedPages().size() > 1;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_DELETE_PAGES, several ? L"&Delete pages\tDel" : L"&Delete page\tDel");
    AppendMenuW(m, MF_STRING, ID_ROTATE_PAGES_CW, L"Rotate &clockwise");
    AppendMenuW(m, MF_STRING, ID_ROTATE_PAGES_CCW, L"Rotate c&ounterclockwise");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_INSERT_BLANK, L"Insert &blank page after");
    AppendMenuW(m, MF_STRING, ID_INSERT_FILE, L"&Insert pages from file\x2026");
    AppendMenuW(m, MF_STRING, ID_MERGE_FILES, L"&Merge PDFs into this document\x2026");
    AppendMenuW(m, MF_STRING | (m_tabs.size() > 1 ? 0 : MF_GRAYED), ID_MERGE_TABS,
                L"Merge open &tabs into this document");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXTRACT_PAGES, L"E&xtract or split pages\x2026");
    return m;
}

void MainWindow::ShowPagesMenu(POINT screen) {
    if (!CanEdit()) return;
    HMENU m = CreatePagesMenu();
    TrackPopupMenu(m, TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

// ===========================================================================
// Edit PDF: text and comments
// ===========================================================================
namespace {
struct CommentDialog {
    std::wstring info, text;
    bool canDelete = false;
};

// "\n" <-> "\r\n" for the multi-line text box.
std::wstring ToEditLines(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') {
            out += L"\r\n";
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;
        } else if (s[i] == L'\n') {
            out += L"\r\n";
        } else {
            out += s[i];
        }
    }
    return out;
}

std::wstring FromEditLines(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s)
        if (c != L'\r') out += c;
    return out;
}

std::wstring DialogText(HWND dlg, int id) {
    HWND h = GetDlgItem(dlg, id);
    std::wstring t((size_t)GetWindowTextLengthW(h) + 1, L'\0');
    t.resize((size_t)GetWindowTextW(h, t.data(), (int)t.size()));
    return t;
}

INT_PTR CALLBACK CommentDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (CommentDialog*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            d = (CommentDialog*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)d);
            SetDlgItemTextW(dlg, IDC_COMMENT_INFO, d->info.c_str());
            SetDlgItemTextW(dlg, IDC_COMMENT_EDIT, ToEditLines(d->text).c_str());
            if (!d->canDelete) ShowWindow(GetDlgItem(dlg, IDC_COMMENT_DELETE), SW_HIDE);
            HWND edit = GetDlgItem(dlg, IDC_COMMENT_EDIT);
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, (WPARAM)-1, -1);
            return FALSE;  // the focus was set
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    d->text = FromEditLines(DialogText(dlg, IDC_COMMENT_EDIT));
                    EndDialog(dlg, IDOK);
                    return TRUE;
                case IDC_COMMENT_DELETE: EndDialog(dlg, IDC_COMMENT_DELETE); return TRUE;
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}

struct ReplaceDialog {
    std::wstring find, with;
    bool matchCase = false;
};

INT_PTR CALLBACK ReplaceDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (ReplaceDialog*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            d = (ReplaceDialog*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)d);
            SetDlgItemTextW(dlg, IDC_REPLACE_FIND, d->find.c_str());
            SetDlgItemTextW(dlg, IDC_REPLACE_WITH, d->with.c_str());
            CheckDlgButton(dlg, IDC_REPLACE_CASE, d->matchCase ? BST_CHECKED : BST_UNCHECKED);
            SetFocus(GetDlgItem(dlg, IDC_REPLACE_FIND));
            SendDlgItemMessageW(dlg, IDC_REPLACE_FIND, EM_SETSEL, 0, -1);
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                d->find = DialogText(dlg, IDC_REPLACE_FIND);
                d->with = DialogText(dlg, IDC_REPLACE_WITH);
                d->matchCase = IsDlgButtonChecked(dlg, IDC_REPLACE_CASE) == BST_CHECKED;
                if (d->find.empty()) {
                    SetFocus(GetDlgItem(dlg, IDC_REPLACE_FIND));
                    return TRUE;
                }
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

std::wstring CommentKind(int subtype) {
    switch (subtype) {
        case kAnnotNote: return L"Note";
        case kMarkupSquiggly: return L"Squiggly underline";
        case kAnnotSquare: return L"Rectangle";
        case kAnnotCircle: return L"Ellipse";
        case kAnnotInk: return L"Drawing";
        case kAnnotLine: return L"Line";
        case kAnnotStamp: return L"Stamp";
        case kAnnotFreeText: return L"Text box";
        case kAnnotPolygon:
        case kAnnotPolyline: return L"Shape";
        case kMarkupHighlight: return L"Highlight";
        case kMarkupUnderline: return L"Underline";
        case kMarkupStrikeOut: return L"Strikethrough";
        default: return L"Marking";
    }
}

struct CommentsDialog {
    const std::vector<std::pair<int, CommentInfo>>* items = nullptr;
    int chosen = -1;  // index into items
};

INT_PTR CALLBACK CommentsDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (CommentsDialog*)GetWindowLongPtrW(dlg, DWLP_USER);
    HWND list = GetDlgItem(dlg, IDC_COMMENTS_LIST);
    auto selected = [&] { return (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); };
    switch (msg) {
        case WM_INITDIALOG: {
            d = (CommentsDialog*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)d);
            const size_t n = d->items->size();
            SetDlgItemTextW(dlg, IDC_COMMENTS_INFO,
                            (std::to_wstring(n) + (n == 1 ? L" comment" : L" comments") +
                             L" in this document. Double-click one to go to it.")
                                .c_str());
            SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            RECT rc;
            GetClientRect(list, &rc);
            const int w = rc.right - GetSystemMetrics(SM_CXVSCROLL);
            const wchar_t* names[] = {L"Page", L"Type", L"Author", L"Comment"};
            const int widths[] = {w * 10 / 100, w * 15 / 100, w * 20 / 100, w * 55 / 100};
            for (int c = 0; c < 4; ++c) {
                LVCOLUMNW col{};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.pszText = const_cast<wchar_t*>(names[c]);
                col.cx = widths[c];
                SendMessageW(list, LVM_INSERTCOLUMNW, c, (LPARAM)&col);
            }
            for (size_t i = 0; i < n; ++i) {
                const auto& [page, c] = (*d->items)[i];
                std::wstring cells[] = {std::to_wstring(page + 1), CommentKind(c.subtype), c.author, c.text};
                for (wchar_t& ch : cells[3])
                    if (ch == L'\r' || ch == L'\n') ch = L' ';
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = (int)i;
                item.pszText = cells[0].data();
                SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
                for (int c2 = 1; c2 < 4; ++c2) {
                    item.iSubItem = c2;
                    item.pszText = cells[c2].data();
                    SendMessageW(list, LVM_SETITEMTEXTW, i, (LPARAM)&item);
                }
            }
            LVITEMW sel{};
            sel.stateMask = sel.state = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(list, LVM_SETITEMSTATE, 0, (LPARAM)&sel);
            SetFocus(list);
            return FALSE;
        }
        case WM_NOTIFY: {
            const auto* nm = (const NMHDR*)lp;
            if (nm->idFrom == IDC_COMMENTS_LIST && nm->code == NM_DBLCLK && selected() >= 0) {
                d->chosen = selected();
                EndDialog(dlg, IDC_COMMENTS_GOTO);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_COMMENTS_GOTO:
                case IDC_COMMENTS_EDIT:
                case IDC_COMMENTS_DELETE:
                    if (selected() < 0) return TRUE;
                    d->chosen = selected();
                    EndDialog(dlg, LOWORD(wp));
                    return TRUE;
                case IDOK:  // Enter in the list
                    if (selected() >= 0) {
                        d->chosen = selected();
                        EndDialog(dlg, IDC_COMMENTS_GOTO);
                    }
                    return TRUE;
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

HMENU MainWindow::CreateMarkupMenu() {
    const UINT selFlag = View().HasSelection() ? 0 : MF_GRAYED;
    HMENU markup = CreatePopupMenu();
    AppendMenuW(markup, MF_STRING | selFlag, ID_HIGHLIGHT, L"&Highlight\tCtrl+H");
    AppendMenuW(markup, MF_STRING | selFlag, ID_UNDERLINE, L"&Underline\tCtrl+U");
    AppendMenuW(markup, MF_STRING | selFlag, ID_STRIKEOUT, L"&Strikethrough\tCtrl+Shift+K");
    AppendMenuW(markup, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i <= ID_HL_COLOR_LAST - ID_HL_COLOR_FIRST; ++i)
        AppendMenuW(markup, MF_STRING, ID_HL_COLOR_FIRST + i, kHighlightColorNames[i]);
    CheckMenuRadioItem(markup, ID_HL_COLOR_FIRST, ID_HL_COLOR_LAST,
                       ID_HL_COLOR_FIRST + m_settings.highlightColor, MF_BYCOMMAND);
    return markup;
}

HMENU MainWindow::CreateEditPdfMenu() {
    PdfView& view = View();
    const bool doc = view.HasDocument();
    const UINT docFlag = doc ? 0 : MF_GRAYED;
    const UINT selFlag = view.HasSelection() ? 0 : MF_GRAYED;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | docFlag | (view.Tool() == ViewTool::EditText ? MF_CHECKED : 0), ID_EDIT_TEXT,
                L"Edit &text\tCtrl+E");
    AppendMenuW(m, MF_STRING | docFlag, ID_REPLACE_TEXT, L"Find and &replace text\x2026\tCtrl+Shift+H");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | docFlag | (view.Tool() == ViewTool::AddComment ? MF_CHECKED : 0),
                ID_ADD_COMMENT, L"Add &comment\tCtrl+M");
    AppendMenuW(m, MF_STRING | selFlag, ID_COMMENT_SELECTION, L"Comment on &selected text\x2026\tCtrl+Shift+M");
    AppendMenuW(m, MF_STRING | docFlag, ID_SHOW_COMMENTS, L"&All comments\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_POPUP | docFlag, (UINT_PTR)CreateAnnotateMenu(), L"A&nnotate");
    AppendMenuW(m, MF_POPUP | docFlag, (UINT_PTR)CreatePagesMenu(), L"Edit &pages");
    return m;
}

void MainWindow::ShowEditPdfMenu() {
    HMENU m = CreateEditPdfMenu();
    const RECT rc = m_toolbar.ItemScreenRect(ID_EDIT_PDF_MENU);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::SetTool(ViewTool tool) {
    if (!CanEdit()) return;
    if (m_fullscreen && tool != ViewTool::Select) ToggleFullscreen();
    View().SetTool(tool);
    UpdateUi();
}

void MainWindow::ReplaceTextInDocument() {
    if (!CanEdit()) return;
    ReplaceDialog d{m_replaceFind, m_replaceWith, m_replaceCase};
    if (DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_REPLACE), m_hwnd, ReplaceDlgProc, (LPARAM)&d) != IDOK)
        return;
    m_replaceFind = d.find;
    m_replaceWith = d.with;
    m_replaceCase = d.matchCase;
    EditOp op;
    op.kind = EditOp::FindReplace;
    op.find = d.find;
    op.text = d.with;
    op.matchCase = d.matchCase;
    SendEdit(std::move(op));
}

void MainWindow::NewComment(int page, float x, float y) {
    if (!CanEdit()) return;
    CommentDialog d;
    const std::wstring author = CurrentUserName();
    d.info = L"New comment on page " + std::to_wstring(page + 1) + (author.empty() ? L"" : L" by " + author);
    if (DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_COMMENT), m_hwnd, CommentDlgProc, (LPARAM)&d) != IDOK)
        return;
    bool blank = true;
    for (wchar_t c : d.text)
        if (!iswspace(c)) blank = false;
    if (blank) return;
    EditOp op;
    op.kind = EditOp::AddNote;
    op.page = page;
    op.x = x;
    op.y = y;
    op.text = d.text;
    op.author = author;
    SendEdit(std::move(op));
}

void MainWindow::OpenComment(int page, const CommentInfo& c) {
    if (!CanEdit()) return;
    CommentDialog d;
    d.text = c.text;
    d.canDelete = true;
    d.info = CommentKind(c.subtype) + L" on page " + std::to_wstring(page + 1);
    if (!c.author.empty()) d.info += L" by " + c.author;
    const std::wstring when = FormatPdfDate(c.date);
    if (!when.empty()) d.info += L", " + when;
    const INT_PTR r = DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_COMMENT), m_hwnd, CommentDlgProc, (LPARAM)&d);
    EditOp op;
    op.page = page;
    op.index = c.annot;
    if (r == IDC_COMMENT_DELETE) {
        op.kind = EditOp::DeleteAnnot;
    } else if (r == IDOK && d.text != c.text) {
        op.kind = EditOp::EditComment;
        op.text = d.text;
        op.author = CurrentUserName();
    } else {
        return;
    }
    SendEdit(std::move(op));
}

void MainWindow::CommentOnSelection() {
    if (!CanEdit()) return;
    if (!View().HasSelection()) {
        MessageBoxW(m_hwnd,
                    L"Select the text to comment on first (drag over it), or use \x201C"
                    L"Add comment\x201D to put a note anywhere on the page.",
                    APP_NAME, MB_ICONINFORMATION);
        return;
    }
    TextPos from, to;
    View().GetSelection(from, to);
    CommentDialog d;
    d.info = L"Comment on the selected text (it is highlighted)";
    if (DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_COMMENT), m_hwnd, CommentDlgProc, (LPARAM)&d) != IDOK)
        return;
    AddMarkup(kMarkupHighlight, d.text);
}

void MainWindow::OnCommentList(CommentListResult* result) {
    std::unique_ptr<CommentListResult> res(result);
    const int index = TabByDocId(res->docId);
    if (index < 0 || index != m_active) return;
    if (res->comments.empty()) {
        MessageBoxW(m_hwnd,
                    L"This document has no comments yet.\n\nTo add one, click \x201C" L"Comment\x201D "
                    L"on the toolbar (Ctrl+M) and click on the page.",
                    APP_NAME, MB_ICONINFORMATION);
        return;
    }
    CommentsDialog d;
    d.items = &res->comments;
    const INT_PTR r = DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_COMMENTS), m_hwnd, CommentsDlgProc, (LPARAM)&d);
    if (d.chosen < 0 || d.chosen >= (int)res->comments.size() || TabByDocId(res->docId) != m_active) return;
    const auto& [page, comment] = res->comments[(size_t)d.chosen];
    SearchHit spot;  // shows the comment itself, not just its page
    spot.page = page;
    spot.rects.push_back(comment.rect);
    if (r == IDC_COMMENTS_GOTO) {
        View().PushHistory();
        View().ScrollToHit(spot);
    } else if (r == IDC_COMMENTS_EDIT) {
        View().PushHistory();
        View().ScrollToHit(spot);
        OpenComment(page, comment);
    } else if (r == IDC_COMMENTS_DELETE) {
        EditOp op;
        op.kind = EditOp::DeleteAnnot;
        op.page = page;
        op.index = comment.annot;
        SendEdit(std::move(op));
    }
}

// ===========================================================================
// Annotate: markup, drawing, new text, stamps, signatures and pictures
// ===========================================================================
HMENU MainWindow::CreateAnnotateMenu() {
    PdfView& view = View();
    const bool doc = view.HasDocument();
    const UINT docFlag = doc ? 0 : MF_GRAYED;
    const UINT selFlag = view.HasSelection() ? 0 : MF_GRAYED;
    const ViewTool tool = view.Tool();
    auto toolItem = [&](HMENU m, int id, ViewTool t, const wchar_t* text) {
        AppendMenuW(m, MF_STRING | docFlag | (tool == t ? MF_CHECKED : 0), id, text);
    };
    HMENU stamps = CreatePopupMenu();
    for (int i = 0; i < kStampCount; ++i) AppendMenuW(stamps, MF_STRING, ID_STAMP_FIRST + i, kStamps[i].text);
    AppendMenuW(stamps, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(stamps, MF_STRING, ID_STAMP_CUSTOM, L"&Custom stamp\x2026");

    Picture saved;
    const bool hasSaved = LoadSavedSignature(saved);
    HMENU sign = CreatePopupMenu();
    if (hasSaved) AppendMenuW(sign, MF_STRING, ID_SIGNATURE_USE, L"Use &my signature");
    AppendMenuW(sign, MF_STRING, ID_SIGNATURE_NEW, hasSaved ? L"&New signature\x2026" : L"&Create signature\x2026");
    if (hasSaved) AppendMenuW(sign, MF_STRING, ID_SIGNATURE_FORGET, L"&Forget my signature");

    HMENU colors = CreatePopupMenu();
    for (int i = 0; i < kDrawColorCount; ++i)
        AppendMenuW(colors, MF_STRING | (kDrawColors[i] == m_settings.drawColor ? MF_CHECKED : 0),
                    ID_DRAWCOLOR_FIRST + i, kDrawColorNames[i]);
    AppendMenuW(colors, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(colors, MF_STRING, ID_DRAWCOLOR_MORE, L"&More colours\x2026");
    HMENU widths = CreatePopupMenu();
    for (int i = 0; i < kLineWidthCount; ++i)
        AppendMenuW(widths, MF_STRING | (kLineWidths[i] == m_settings.lineWidthTenths ? MF_CHECKED : 0),
                    ID_WIDTH_FIRST + i, kLineWidthNames[i]);
    HMENU sizes = CreatePopupMenu();
    for (int i = 0; i < kTextSizeCount; ++i)
        AppendMenuW(sizes, MF_STRING | (kTextSizes[i] == m_settings.textSize ? MF_CHECKED : 0), ID_TEXTSIZE_FIRST + i,
                    (std::to_wstring(kTextSizes[i]) + L" pt").c_str());

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | selFlag, ID_HIGHLIGHT, L"&Highlight\tCtrl+H");
    AppendMenuW(m, MF_STRING | selFlag, ID_UNDERLINE, L"&Underline\tCtrl+U");
    AppendMenuW(m, MF_STRING | selFlag, ID_STRIKEOUT, L"&Strikethrough\tCtrl+Shift+K");
    AppendMenuW(m, MF_STRING | selFlag, ID_SQUIGGLY, L"S&quiggly underline");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    toolItem(m, ID_TOOL_ADD_TEXT, ViewTool::AddText, L"Add &text");
    toolItem(m, ID_TOOL_RECT, ViewTool::Rectangle, L"&Rectangle");
    toolItem(m, ID_TOOL_ELLIPSE, ViewTool::Ellipse, L"&Ellipse");
    toolItem(m, ID_TOOL_LINE, ViewTool::Line, L"&Line");
    toolItem(m, ID_TOOL_ARROW, ViewTool::Arrow, L"&Arrow");
    toolItem(m, ID_TOOL_PEN, ViewTool::Pen, L"&Pen (freehand)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_POPUP | docFlag, (UINT_PTR)stamps, L"Sta&mp");
    AppendMenuW(m, MF_POPUP | docFlag, (UINT_PTR)sign, L"Si&gnature");
    AppendMenuW(m, MF_STRING | docFlag, ID_INSERT_IMAGE, L"Insert p&icture\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)colors, L"&Colour");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)widths, L"Line &width");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sizes, L"Te&xt size");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)CreateMarkupMenuColors(), L"Highlight c&olour");
    return m;
}

HMENU MainWindow::CreateMarkupMenuColors() {
    HMENU m = CreatePopupMenu();
    for (int i = 0; i <= ID_HL_COLOR_LAST - ID_HL_COLOR_FIRST; ++i)
        AppendMenuW(m, MF_STRING, ID_HL_COLOR_FIRST + i, kHighlightColorNames[i]);
    CheckMenuRadioItem(m, ID_HL_COLOR_FIRST, ID_HL_COLOR_LAST, ID_HL_COLOR_FIRST + m_settings.highlightColor,
                       MF_BYCOMMAND);
    return m;
}

void MainWindow::ShowAnnotateMenu() {
    HMENU m = CreateAnnotateMenu();
    const RECT rc = m_toolbar.ItemScreenRect(ID_ANNOTATE_MENU);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

// Starts a tool with the current colour, line width and text size.
void MainWindow::StartTool(ViewTool tool) {
    if (!CanEdit()) return;
    PdfView& view = View();
    ToolOptions o;
    if (tool == ViewTool::Stamp || tool == ViewTool::Signature || tool == ViewTool::Image) return;  // own setup
    o.color = m_settings.drawColor;
    o.width = m_settings.lineWidthTenths / 10.0f;
    o.textSize = (float)m_settings.textSize;
    o.author = CurrentUserName();
    view.SetToolOptions(o);
    SetTool(tool);
}

void MainWindow::StartStamp(const std::wstring& text, COLORREF color) {
    if (!CanEdit()) return;
    ToolOptions o;
    o.stamp = text;
    o.stampColor = color;
    o.author = CurrentUserName();
    View().SetToolOptions(o);
    SetTool(ViewTool::Stamp);
}

void MainWindow::StartSignature(bool newOne) {
    if (!CanEdit()) return;
    Picture sig;
    if (newOne || !LoadSavedSignature(sig)) {
        if (!ShowSignatureDialog(m_inst, m_hwnd, sig)) return;
    }
    ToolOptions o;
    o.pixels = std::move(sig.pixels);
    o.imageW = sig.width;
    o.imageH = sig.height;
    o.author = CurrentUserName();
    View().SetToolOptions(o);
    SetTool(ViewTool::Signature);
}

// A signature field was clicked: the signature goes inside it.
void MainWindow::SignField(int page, const RectF& field) {
    if (!CanEdit()) return;
    Picture sig;
    if (!LoadSavedSignature(sig) && !ShowSignatureDialog(m_inst, m_hwnd, sig)) return;
    const float fw = field.right - field.left, fh = field.bottom - field.top;
    const float aspect = (float)sig.width / std::max(1, sig.height);
    float w = fw, h = fw / aspect;
    if (h > fh) {
        h = fh;
        w = fh * aspect;
    }
    EditOp op;
    op.kind = EditOp::AddImage;
    op.page = page;
    op.rect = {field.left + (fw - w) / 2, field.top + (fh - h) / 2, field.left + (fw + w) / 2, field.top + (fh + h) / 2};
    op.asAnnot = true;
    op.text = L"Signature";
    op.author = CurrentUserName();
    op.pixels = std::move(sig.pixels);
    op.imageW = sig.width;
    op.imageH = sig.height;
    SendEdit(std::move(op));
}

void MainWindow::InsertImage() {
    if (!CanEdit()) return;
    std::vector<wchar_t> file(32768, L'\0');
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Pictures (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0";
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = (DWORD)file.size();
    ofn.lpstrTitle = L"Insert picture";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;
    Picture pic;
    // Large photos are scaled down: 2400 pixels is sharp at full-page size.
    if (!LoadPictureFile(file.data(), 2400, pic)) {
        MessageBoxW(m_hwnd, L"That picture could not be read.", APP_NAME, MB_ICONWARNING);
        return;
    }
    ToolOptions o;
    o.pixels = std::move(pic.pixels);
    o.imageW = pic.width;
    o.imageH = pic.height;
    View().SetToolOptions(o);
    SetTool(ViewTool::Image);
}

// ===========================================================================
// Command palette (Ctrl+K)
// ===========================================================================
void MainWindow::ShowCommandPalette() {
    if (m_fullscreen && !m_presenting) ToggleFullscreen();
    PdfView& view = View();
    const bool doc = view.HasDocument();
    const bool sel = view.HasSelection();
    const Tab& tab = Active();
    std::vector<PaletteCommand> c = {
        {ID_OPEN, L"Open a PDF", L"Ctrl+O", L"file new tab browse"},
        {ID_SAVE, L"Save", L"Ctrl+S", L"", doc && tab.dirty},
        {ID_SAVE_AS, L"Save as", L"Ctrl+Shift+S", L"copy", doc},
        {ID_PRINT, L"Print", L"Ctrl+P", L"printer paper", doc},
        {ID_CLOSE_TAB, L"Close tab", L"Ctrl+W", L""},
        {ID_UNDO, L"Undo", L"Ctrl+Z", L"", doc && tab.canUndo},
        {ID_REDO, L"Redo", L"Ctrl+Y", L"", doc && tab.canRedo},
        {ID_EDIT_TEXT, L"Edit text", L"Ctrl+E", L"change words typo correct modify", doc},
        {ID_REPLACE_TEXT, L"Find and replace text", L"Ctrl+Shift+H", L"substitute change everywhere", doc},
        {ID_ADD_COMMENT, L"Add a comment", L"Ctrl+M", L"note sticky review", doc},
        {ID_COMMENT_SELECTION, L"Comment on the selected text", L"Ctrl+Shift+M", L"note", doc && sel},
        {ID_SHOW_COMMENTS, L"List all comments", L"", L"notes review annotations", doc},
        {ID_HIGHLIGHT, L"Highlight the selected text", L"Ctrl+H", L"marker", doc && sel},
        {ID_UNDERLINE, L"Underline the selected text", L"Ctrl+U", L"", doc && sel},
        {ID_STRIKEOUT, L"Strike through the selected text", L"Ctrl+Shift+K", L"strikethrough cross out", doc && sel},
        {ID_SQUIGGLY, L"Squiggly underline", L"", L"wavy", doc && sel},
        {ID_TOOL_ADD_TEXT, L"Add text", L"", L"type write typewriter text box", doc},
        {ID_TOOL_RECT, L"Draw a rectangle", L"", L"box square shape", doc},
        {ID_TOOL_ELLIPSE, L"Draw an ellipse", L"", L"circle oval shape", doc},
        {ID_TOOL_LINE, L"Draw a line", L"", L"shape", doc},
        {ID_TOOL_ARROW, L"Draw an arrow", L"", L"pointer shape", doc},
        {ID_TOOL_PEN, L"Draw with the pen", L"", L"freehand ink pencil sketch", doc},
        {ID_STAMP_FIRST, L"Stamp: APPROVED", L"", L"stamp", doc},
        {ID_STAMP_FIRST + 1, L"Stamp: REJECTED", L"", L"stamp", doc},
        {ID_STAMP_FIRST + 3, L"Stamp: CONFIDENTIAL", L"", L"stamp", doc},
        {ID_STAMP_CUSTOM, L"Custom stamp", L"", L"stamp", doc},
        {ID_SIGNATURE_USE, L"Sign the document", L"", L"signature autograph", doc},
        {ID_SIGNATURE_NEW, L"Create a new signature", L"", L"sign autograph", doc},
        {ID_INSERT_IMAGE, L"Insert a picture", L"", L"image photo logo png jpg", doc},
        {ID_DELETE_PAGES, L"Delete the current or selected pages", L"", L"remove", doc},
        {ID_ROTATE_PAGES_CW, L"Rotate pages clockwise (saved)", L"", L"turn", doc},
        {ID_ROTATE_PAGES_CCW, L"Rotate pages counterclockwise (saved)", L"", L"turn", doc},
        {ID_INSERT_BLANK, L"Insert a blank page", L"", L"add empty", doc},
        {ID_INSERT_FILE, L"Insert pages from a file", L"", L"add merge", doc},
        {ID_MERGE_FILES, L"Merge PDFs into this document", L"", L"combine join", doc},
        {ID_MERGE_TABS, L"Merge open tabs into this document", L"", L"combine join", doc && m_tabs.size() > 1},
        {ID_EXTRACT_PAGES, L"Extract or split pages", L"", L"split save pages separate", doc},
        {ID_GOTO_PAGE, L"Go to page", L"Ctrl+G", L"jump", doc},
        {ID_FIRST_PAGE, L"First page", L"Home", L"start beginning", doc},
        {ID_LAST_PAGE, L"Last page", L"End", L"end", doc},
        {ID_BACK, L"Back", L"Alt+Left", L"previous history", doc && view.CanGoBack()},
        {ID_FORWARD, L"Forward", L"Alt+Right", L"next history", doc && view.CanGoForward()},
        {ID_SEARCH, L"Search in the document", L"Ctrl+F", L"find", doc},
        {ID_FIT_WIDTH, L"Fit width", L"Ctrl+2", L"zoom", doc},
        {ID_FIT_PAGE, L"Fit page", L"Ctrl+0", L"zoom whole", doc},
        {ID_ACTUAL_SIZE, L"Actual size (100%)", L"Ctrl+1", L"zoom", doc},
        {ID_ZOOM_IN, L"Zoom in", L"Ctrl++", L"bigger", doc},
        {ID_ZOOM_OUT, L"Zoom out", L"Ctrl+-", L"smaller", doc},
        {ID_SINGLE_PAGE, L"Single page view", L"", L"layout", doc},
        {ID_CONTINUOUS, L"Continuous view", L"", L"layout scroll", doc},
        {ID_VIEW_TWO_PAGE, L"Two pages side by side", L"", L"layout book spread", doc},
        {ID_ROTATE_LEFT, L"Rotate the view left", L"Ctrl+L", L"turn", doc},
        {ID_ROTATE_RIGHT, L"Rotate the view right", L"Ctrl+R", L"turn", doc},
        {ID_PRESENT, L"Presentation", L"F5", L"slideshow present full screen", doc},
        {ID_FULLSCREEN, L"Full screen", L"F11", L"", true},
        {ID_COLORS_NORMAL, L"Page colours: normal", L"", L"light", doc},
        {ID_COLORS_DARK, L"Page colours: dark (night mode)", L"", L"night black invert", doc},
        {ID_COLORS_DIM, L"Page colours: dimmed", L"", L"grey", doc},
        {ID_THEME_LIGHT, L"Light theme", L"", L"appearance"},
        {ID_THEME_DARK, L"Dark theme", L"", L"appearance night"},
        {ID_THEME_SYSTEM, L"Theme like Windows", L"", L"appearance system"},
        {ID_SIDEBAR_BOOKMARKS, L"Bookmarks panel", L"Ctrl+B", L"outline contents", doc},
        {ID_SIDEBAR_THUMBNAILS, L"Thumbnails panel", L"Ctrl+Shift+B", L"pages preview", doc},
        {ID_COPY, L"Copy the selected text", L"Ctrl+C", L"", doc && sel},
        {ID_SELECT_ALL, L"Select all text", L"Ctrl+A", L"", doc},
        {ID_COPY_PAGE_IMAGE, L"Copy the page as a picture", L"", L"image clipboard", doc},
        {ID_COPY_AREA_IMAGE, L"Copy an area as a picture", L"", L"image clipboard snapshot", doc},
        {ID_PROPERTIES, L"Document properties", L"Ctrl+D", L"info author title metadata", doc},
        {ID_REGISTER_DEFAULT, L"Set as the default PDF viewer", L"", L"associate"},
        {ID_ABOUT, L"About Feather PDF", L"", L"version licenses"},
        {ID_EXIT, L"Exit", L"", L"quit close"},
    };
    for (size_t i = 0; i < m_settings.recent.size() && i < 10; ++i)
        c.push_back({ID_RECENT_FIRST + (int)i, L"Open recent: " + FileNameFromPath(m_settings.recent[i]), L"",
                     m_settings.recent[i]});
    m_palette.Show(m_hwnd, std::move(c), [this](int id, const std::wstring& arg) { RunPaletteCommand(id, arg); });
}

void MainWindow::RunPaletteCommand(int id, const std::wstring& arg) {
    PdfView& view = View();
    if (id == kPaletteGoTo) {
        const long page = wcstol(arg.c_str(), nullptr, 10);
        if (view.HasDocument() && page >= 1) {
            view.PushHistory();
            view.GoToPage((int)std::min<long>(page, view.PageCount()) - 1);
        }
    } else if (id == kPaletteZoom) {
        const long pct = wcstol(arg.c_str(), nullptr, 10);
        if (view.HasDocument() && pct > 0) view.SetZoom(std::clamp(pct / 100.0, PdfView::kMinZoom, PdfView::kMaxZoom));
    } else if (id == kPaletteFind) {
        if (!view.HasDocument()) return;
        ShowSearch(true);
        SetWindowTextW(m_searchEdit, arg.c_str());
        StartSearch();
    } else if (id > 0) {
        OnCommand(id, 0, nullptr);
    }
    if (!m_palette.IsOpen() && GetFocus() == nullptr) SetFocus(view.Hwnd());
}

// ===========================================================================
// Recent files, presentation
// ===========================================================================
void MainWindow::AddRecent(const std::wstring& path) {
    if (path.empty()) return;
    auto& list = m_settings.recent;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const std::wstring& p) { return SamePath(p, path); }),
               list.end());
    list.insert(list.begin(), path);
    if (list.size() > 20) list.resize(20);
}

HMENU MainWindow::CreateRecentMenu() {
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < m_settings.recent.size() && i < 20; ++i) {
        std::wstring name;  // "&" in a file name is not a menu accelerator
        for (wchar_t ch : FileNameFromPath(m_settings.recent[i])) {
            if (ch == L'&') name += L'&';
            name += ch;
        }
        const std::wstring label = (i < 9 ? L"&" : L"") + std::to_wstring(i + 1) + L"  " + name;
        AppendMenuW(m, MF_STRING, ID_RECENT_FIRST + (int)i, label.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_RECENT_CLEAR, L"&Clear the list");
    return m;
}

void MainWindow::TogglePresentation() {
    PdfView& view = View();
    if (!m_presenting) {
        if (!view.HasDocument()) return;
        m_presenting = true;
        m_presentWasFullscreen = m_fullscreen;
        m_presentViewMode = view.GetViewMode();
        m_presentZoomMode = view.GetZoomMode();
        m_presentZoom = view.Zoom();
        if (m_searchVisible) ShowSearch(false);
        if (!m_fullscreen) ToggleFullscreen();
        view.SetPresenting(true);
        view.SetViewMode(ViewMode::Single);
        view.SetZoomMode(ZoomMode::FitPage);
    } else {
        m_presenting = false;
        view.SetPresenting(false);
        view.SetViewMode(m_presentViewMode);
        if (m_presentZoomMode == ZoomMode::Custom)
            view.SetZoom(m_presentZoom);
        else
            view.SetZoomMode(m_presentZoomMode);
        if (m_fullscreen && !m_presentWasFullscreen) ToggleFullscreen();
    }
    SetFocus(view.Hwnd());
    UpdateUi();
}

// ===========================================================================
// About, licences, input box
// ===========================================================================
namespace {
std::wstring NoticesText() {
    HRSRC res = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_NOTICES), RT_RCDATA);
    HGLOBAL data = res ? LoadResource(GetModuleHandleW(nullptr), res) : nullptr;
    const char* bytes = data ? (const char*)LockResource(data) : nullptr;
    const DWORD size = res ? SizeofResource(GetModuleHandleW(nullptr), res) : 0;
    const std::wstring text = bytes ? Utf8ToWide(std::string(bytes, size)) : L"";
    // Shown as plain text: the Markdown table and marks are tidied up.
    std::wstring out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.rfind(L"|---", 0) == 0 || line.rfind(L"| ---", 0) == 0) continue;
        while (!line.empty() && line.front() == L'#') line.erase(line.begin());
        std::wstring clean;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == L'`' || (line[i] == L'*' && i + 1 < line.size() && line[i + 1] == L'*')) {
                if (line[i] == L'*') ++i;
                continue;
            }
            clean += line[i];
        }
        if (!clean.empty() && clean.front() == L'|') {  // a table row
            std::wstring row;
            size_t a = 1;
            while (a < clean.size()) {
                size_t b = clean.find(L'|', a);
                if (b == std::wstring::npos) b = clean.size();
                std::wstring cell = clean.substr(a, b - a);
                while (!cell.empty() && cell.front() == L' ') cell.erase(cell.begin());
                while (!cell.empty() && cell.back() == L' ') cell.pop_back();
                if (!cell.empty()) row += (row.empty() ? L"" : L"  \x2014  ") + cell;
                a = b + 1;
            }
            clean = L"\x2022 " + row;
        }
        while (!clean.empty() && clean.front() == L' ') clean.erase(clean.begin());
        out += clean + L"\r\n";
    }
    return out;
}

INT_PTR CALLBACK LicensesDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemTextW(dlg, IDC_LICENSES_TEXT, NoticesText().c_str());
            SetFocus(GetDlgItem(dlg, IDCANCEL));
            return FALSE;  // the focus was set (the text is not all selected)
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK) EndDialog(dlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

struct AboutFonts {
    HFONT title = nullptr;
};

INT_PTR CALLBACK AboutDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* fonts = (AboutFonts*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            fonts = (AboutFonts*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)fonts);
            fonts->title = CreateMessageFont(GetWindowDpi(dlg), 160);
            LOGFONTW lf{};
            GetObjectW(fonts->title, sizeof(lf), &lf);
            lf.lfWeight = FW_SEMIBOLD;
            DeleteObject(fonts->title);
            fonts->title = CreateFontIndirectW(&lf);
            SendDlgItemMessageW(dlg, IDC_ABOUT_NAME, WM_SETFONT, (WPARAM)fonts->title, TRUE);
            SetDlgItemTextW(dlg, IDC_ABOUT_NAME, APP_NAME);
            std::wstring version = APP_VERSION;
            while (version.size() > 3 && version.compare(version.size() - 2, 2, L".0") == 0) version.resize(version.size() - 2);
            SetDlgItemTextW(dlg, IDC_ABOUT_VERSION, (L"Version " + version).c_str());
            SetDlgItemTextW(dlg, IDC_ABOUT_TEXT,
                            L"Feather PDF is a fast, lightweight PDF reader and editor for Windows. It opens "
                            L"documents in tabs to read, search, print and present them, and lets you edit them: "
                            L"change and replace text, fill in forms, sign, and add comments, highlights, "
                            L"drawings, stamps, pictures and text, as well as reorganise, merge, split and "
                            L"extract pages, with undo and crash-safe saving. Everything happens on your PC, "
                            L"with no account, cloud service or tracking.\r\n\r\n"
                            L"Created by: Akshaya Simha\r\n\r\n"
                            L"\x00A9 2026 Akshaya Simha. All rights reserved.\r\n"
                            L"This application incorporates third-party software and materials. See Third-Party "
                            L"Licenses and Attributions for applicable notices and license terms.\r\n\r\n"
                            L"Built with care for a faster, simpler document experience.");
            SetFocus(GetDlgItem(dlg, IDOK));
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_ABOUT_LICENSES) {
                DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_LICENSES), dlg, LicensesDlgProc, 0);
                return TRUE;
            }
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            if (fonts && fonts->title) DeleteObject(fonts->title);
            break;
    }
    return FALSE;
}

struct InputDialog {
    std::wstring title, label, text;
};

INT_PTR CALLBACK InputDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (InputDialog*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            d = (InputDialog*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)d);
            SetWindowTextW(dlg, d->title.c_str());
            SetDlgItemTextW(dlg, IDC_INPUT_LABEL, d->label.c_str());
            SetDlgItemTextW(dlg, IDC_INPUT_EDIT, d->text.c_str());
            SetFocus(GetDlgItem(dlg, IDC_INPUT_EDIT));
            SendDlgItemMessageW(dlg, IDC_INPUT_EDIT, EM_SETSEL, 0, -1);
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                d->text = DialogText(dlg, IDC_INPUT_EDIT);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

void MainWindow::ShowAbout() {
    AboutFonts fonts;
    DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_ABOUT), m_hwnd, AboutDlgProc, (LPARAM)&fonts);
}

bool MainWindow::InputBox(const std::wstring& title, const std::wstring& label, std::wstring& text) {
    InputDialog d{title, label, text};
    if (DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_INPUT), m_hwnd, InputDlgProc, (LPARAM)&d) != IDOK) return false;
    text = d.text;
    return true;
}
