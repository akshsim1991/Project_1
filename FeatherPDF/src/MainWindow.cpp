// MainWindow.cpp - top-level window, tabs and command handling.
#include "MainWindow.h"

#include <cmath>

#include <commctrl.h>
#include <objbase.h>  // must precede commdlg.h for PrintDlgEx
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include "FileAssoc.h"
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

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) ==
           CSTR_EQUAL;
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

void MainWindow::CloseTab(int index) {
    if (index < 0 || index >= (int)m_tabs.size()) return;
    Tab& tab = *m_tabs[(size_t)index];
    if (index == m_active && tab.search.running) CancelSearch();
    if (tab.docId) m_worker.CloseDocument(tab.docId);

    if (m_tabs.size() == 1) {  // keep one empty tab rather than no view at all
        tab.docId = tab.pendingDocId = 0;
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
            info.title = FileNameFromPath(t->path);
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
    if (page >= 1) View().GoToPage((int)std::min<long>(page, View().PageCount()) - 1);
}

// ===========================================================================
// Commands
// ===========================================================================
void MainWindow::OnCommand(int id, int code, HWND ctl) {
    PdfView& view = View();
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
        case ID_CLOSE_TAB: CloseTab(m_active); break;
        case ID_NEXT_TAB: ActivateTab((m_active + 1) % (int)m_tabs.size()); break;
        case ID_PREV_TAB:
            ActivateTab((m_active + (int)m_tabs.size() - 1) % (int)m_tabs.size());
            break;
        case ID_PREV_PAGE: view.PrevPage(); break;
        case ID_NEXT_PAGE: view.NextPage(); break;
        case ID_FIRST_PAGE: view.GoToPage(0); break;
        case ID_LAST_PAGE: view.GoToPage(view.PageCount() - 1); break;
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
            if (m_searchVisible)
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
        case ID_ABOUT:
            MessageBoxW(m_hwnd,
                        APP_NAME L" " APP_VERSION L"\n\n"
                        APP_COPYRIGHT L".\n"
                        L"Developed for faster experience.\n\n"
                        L"PDF rendering: PDFium (BSD-3-Clause / Apache-2.0),\n"
                        L"Copyright The PDFium Authors.",
                        L"About " APP_NAME, MB_ICONINFORMATION);
            break;
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

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, L"&Open\x2026\tCtrl+O");
    AppendMenuW(m, MF_STRING, ID_CLOSE_TAB, L"&Close tab\tCtrl+W");
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
    AppendMenuW(m, MF_POPUP, (UINT_PTR)colors, L"Page colo&urs");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)theme, L"&Theme");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
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
    if (doc && m_sidebar.Mode() == SidebarMode::Thumbnails)
        m_sidebar.Thumbs().SetCurrentPage(view.CurrentPage());
}

void MainWindow::UpdateTitle() {
    const Tab& tab = Active();
    std::wstring title = tab.docId ? FileNameFromPath(tab.path) + L" - " APP_NAME
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
    tab.view->SetDocument(tab.docId, std::move(res->pageSizes), tab.pendingPage);
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
        tab.view->GoToPage((int)value);
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
// "D:20240131154500+01'00'" -> "2024-01-31 15:45"
std::wstring FormatPdfDate(const std::wstring& d) {
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
    line(L"Created:", FormatPdfDate(info.created));
    line(L"Modified:", FormatPdfDate(info.modified));
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
