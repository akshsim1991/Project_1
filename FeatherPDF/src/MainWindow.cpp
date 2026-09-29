// MainWindow.cpp - top-level window and command handling.
#include "MainWindow.h"

#include <cmath>
#include <memory>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include "FileAssoc.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
const wchar_t kClassName[] = L"FeatherPdfMain";
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
    ReloadTheme();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    // Default size: a portrait-friendly window that fits the work area.
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int sysDpi = GetWindowDpi(nullptr);
    int w = std::min(Dpi(1000, sysDpi), (int)(work.right - work.left) * 9 / 10);
    int h = std::min(Dpi(1100, sysDpi), (int)(work.bottom - work.top) * 9 / 10);

    m_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName, APP_NAME,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                             w, h, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;
    ApplyWindowTheme(m_hwnd);
    CreateChildren();
    CreateAccelerators();
    if (!m_worker.Start(m_hwnd)) return false;

    m_view.SetContinuous(m_settings.continuous);
    if (m_settings.zoomMode == 0) {
        m_view.SetZoom(m_settings.zoom);
    } else {
        m_view.SetZoomMode((ZoomMode)m_settings.zoomMode);
    }

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
    SetFocus(m_view.Hwnd());

    // Open the requested file, or reopen the last document.
    if (!file.empty()) {
        int start = page >= 0 ? page
                    : SamePath(file, m_settings.lastFile) ? m_settings.lastPage
                                                           : 0;
        OpenFile(file, start);
    } else if (!m_settings.lastFile.empty() && FileExists(m_settings.lastFile)) {
        OpenFile(m_settings.lastFile, m_settings.lastPage);
    }
    UpdateUi();
    return true;
}

void MainWindow::CreateChildren() {
    const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;

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

    m_view.Create(m_hwnd, &m_worker);
    m_view.SetSearch(&m_search);
    m_view.onViewChanged = [this] { UpdateUi(); };
    Layout();
}

void MainWindow::CreateAccelerators() {
    ACCEL acc[] = {
        {FCONTROL | FVIRTKEY, 'O', ID_OPEN},
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
    };
    m_accel = CreateAcceleratorTableW(acc, (int)(sizeof(acc) / sizeof(acc[0])));
}

void MainWindow::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int y = 0;
    if (!m_fullscreen) {
        const int th = m_toolbar.Height();
        MoveWindow(m_toolbar.Hwnd(), 0, 0, rc.right, th, TRUE);
        ShowWindow(m_toolbar.Hwnd(), SW_SHOWNA);
        y = th;
    } else {
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
    MoveWindow(m_view.Hwnd(), 0, y, rc.right, std::max(0, (int)rc.bottom - y), TRUE);
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
        case WM_APP_TILE_READY:
            m_view.OnTileReady((TileResult*)lp);
            return 0;
        case WM_APP_SEARCH_RESULT:
            OnSearchResult((SearchPageResult*)lp);
            return 0;

        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp), (HWND)lp);
            return 0;

        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) {
                // Nobody is looking: give the rendered pages back to the OS.
                m_view.TrimMemory();
            } else if (m_view.Hwnd()) {
                Layout();
            }
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) m_lastFocus = GetFocus();
            break;
        case WM_SETFOCUS: {
            HWND target = m_view.Hwnd();
            if (m_lastFocus && IsChild(m_hwnd, m_lastFocus) && IsWindowVisible(m_lastFocus))
                target = m_lastFocus;
            SetFocus(target);
            return 0;
        }

        case WM_MOUSEWHEEL:  // wheel over the toolbar etc. scrolls the page
            return SendMessageW(m_view.Hwnd(), msg, wp, lp);

        case WM_TIMER:
            if (wp == kSearchTimer) {
                KillTimer(m_hwnd, kSearchTimer);
                if (GetText(m_searchEdit) != m_search.query) StartSearch();
            }
            return 0;

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            UINT len = DragQueryFileW(drop, 0, nullptr, 0);
            if (len) {
                std::wstring path((size_t)len + 1, L'\0');
                DragQueryFileW(drop, 0, path.data(), len + 1);
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
            m_toolbar.OnDpiChanged();
            m_searchBar.OnDpiChanged();
            m_view.OnDpiChanged();
            const RECT* r = (const RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }

        case WM_SETTINGCHANGE:
            if (lp && lstrcmpW((const wchar_t*)lp, L"ImmersiveColorSet") == 0) OnThemeChanged();
            break;

        case WM_CLOSE:
            SaveSettings();
            DestroyWindow(m_hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(m_hwnd, kSearchTimer);
            m_worker.Stop();
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
        SetFocus(m_view.Hwnd());  // Esc: abandon edit (text restored on kill focus)
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
    if (text.empty() || !m_view.HasDocument()) return;
    const long page = wcstol(text.c_str(), nullptr, 10);
    if (page >= 1) m_view.GoToPage((int)std::min<long>(page, m_view.PageCount()) - 1);
}

// ===========================================================================
// Commands
// ===========================================================================
void MainWindow::OnCommand(int id, int code, HWND ctl) {
    if (id >= ID_ZOOM_PRESET_FIRST && id < ID_ZOOM_PRESET_FIRST + kZoomPresetCount) {
        m_view.SetZoom(kZoomPresets[id - ID_ZOOM_PRESET_FIRST]);
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
        case ID_PREV_PAGE: m_view.PrevPage(); break;
        case ID_NEXT_PAGE: m_view.NextPage(); break;
        case ID_FIRST_PAGE: m_view.GoToPage(0); break;
        case ID_LAST_PAGE: m_view.GoToPage(m_view.PageCount() - 1); break;
        case ID_GOTO_PAGE:
            if (m_fullscreen) ToggleFullscreen();
            SetFocus(m_pageEdit);
            SendMessageW(m_pageEdit, EM_SETSEL, 0, -1);
            break;
        case ID_ZOOM_IN: m_view.ZoomIn(); break;
        case ID_ZOOM_OUT: m_view.ZoomOut(); break;
        case ID_ZOOM_LABEL: ShowZoomMenu(); break;
        case ID_FIT_WIDTH: m_view.SetZoomMode(ZoomMode::FitWidth); break;
        case ID_FIT_PAGE: m_view.SetZoomMode(ZoomMode::FitPage); break;
        case ID_ACTUAL_SIZE: m_view.SetZoom(1.0); break;
        case ID_CONTINUOUS: m_view.SetContinuous(true); break;
        case ID_SINGLE_PAGE: m_view.SetContinuous(false); break;
        case ID_FULLSCREEN: ToggleFullscreen(); break;
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
                        APP_NAME L" " APP_VERSION
                        L"\n\nA fast, lightweight PDF viewer for Windows.\n\n"
                        L"PDF rendering: PDFium (BSD-3-Clause / Apache-2.0),\n"
                        L"Copyright The PDFium Authors.",
                        L"About " APP_NAME, MB_ICONINFORMATION);
            break;
        case ID_EXIT: PostMessageW(m_hwnd, WM_CLOSE, 0, 0); break;
    }
}

void MainWindow::ShowMoreMenu() {
    const bool doc = m_view.HasDocument();
    const UINT docFlag = doc ? MF_ENABLED : MF_GRAYED;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, L"&Open\x2026\tCtrl+O");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | docFlag, ID_FIRST_PAGE, L"&First page\tHome");
    AppendMenuW(m, MF_STRING | docFlag, ID_LAST_PAGE, L"&Last page\tEnd");
    AppendMenuW(m, MF_STRING | docFlag, ID_GOTO_PAGE, L"&Go to page\x2026\tCtrl+G");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_CONTINUOUS, L"&Continuous scrolling");
    AppendMenuW(m, MF_STRING, ID_SINGLE_PAGE, L"&Single page");
    CheckMenuRadioItem(m, ID_CONTINUOUS, ID_SINGLE_PAGE,
                       m_view.Continuous() ? ID_CONTINUOUS : ID_SINGLE_PAGE, MF_BYCOMMAND);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_view.GetZoomMode() == ZoomMode::FitWidth ? MF_CHECKED : 0),
                ID_FIT_WIDTH, L"Fit &width\tCtrl+2");
    AppendMenuW(m, MF_STRING | (m_view.GetZoomMode() == ZoomMode::FitPage ? MF_CHECKED : 0),
                ID_FIT_PAGE, L"Fit &page\tCtrl+0");
    AppendMenuW(m, MF_STRING, ID_ACTUAL_SIZE, L"&Actual size\tCtrl+1");
    AppendMenuW(m, MF_STRING | (m_fullscreen ? MF_CHECKED : 0), ID_FULLSCREEN,
                L"F&ull screen\tF11");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_REGISTER_DEFAULT, L"Set as &default PDF viewer\x2026");
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"A&bout " APP_NAME);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"E&xit");

    RECT rc = m_toolbar.ItemScreenRect(ID_MENU);
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_TOPALIGN, rc.right, rc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::ShowZoomMenu() {
    static const int kMenuPresets[] = {3, 5, 7, 9, 10, 12, 14, 15};  // 33%..300%
    HMENU m = CreatePopupMenu();
    const int current = (int)std::lround(m_view.Zoom() * 100);
    for (int idx : kMenuPresets) {
        const int pct = (int)std::lround(kZoomPresets[idx] * 100);
        std::wstring label = std::to_wstring(pct) + L"%";
        UINT flags = MF_STRING;
        if (m_view.GetZoomMode() == ZoomMode::Custom && pct == current) flags |= MF_CHECKED;
        AppendMenuW(m, flags, ID_ZOOM_PRESET_FIRST + idx, label.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (m_view.GetZoomMode() == ZoomMode::FitWidth ? MF_CHECKED : 0),
                ID_FIT_WIDTH, L"Fit width\tCtrl+2");
    AppendMenuW(m, MF_STRING | (m_view.GetZoomMode() == ZoomMode::FitPage ? MF_CHECKED : 0),
                ID_FIT_PAGE, L"Fit page\tCtrl+0");
    AppendMenuW(m, MF_STRING, ID_ACTUAL_SIZE, L"Actual size\tCtrl+1");
    RECT rc = m_toolbar.ItemScreenRect(ID_ZOOM_LABEL);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(m);
}

void MainWindow::UpdateUi() {
    const bool doc = m_view.HasDocument();
    const int count = m_view.PageCount();
    const int page = doc ? m_view.CurrentPage() + 1 : 0;

    if (GetFocus() != m_pageEdit) {
        const std::wstring text = doc ? std::to_wstring(page) : L"";
        if (GetText(m_pageEdit) != text) SetWindowTextW(m_pageEdit, text.c_str());
    }
    m_toolbar.SetText(ID_PAGE_TOTAL, doc ? L"/ " + std::to_wstring(count) : L"");
    m_toolbar.SetText(ID_ZOOM_LABEL, std::to_wstring((int)std::lround(m_view.Zoom() * 100)) + L"%");
    m_toolbar.SetEnabled(ID_PREV_PAGE, doc && page > 1);
    m_toolbar.SetEnabled(ID_NEXT_PAGE, doc && page < count);
    m_toolbar.SetEnabled(ID_PAGE_EDIT, doc);
    m_toolbar.SetEnabled(ID_ZOOM_IN, doc);
    m_toolbar.SetEnabled(ID_ZOOM_OUT, doc);
    m_toolbar.SetEnabled(ID_ZOOM_LABEL, doc);
    m_toolbar.SetEnabled(ID_SEARCH, doc);
}

void MainWindow::UpdateTitle() {
    std::wstring title = m_docPath.empty() ? std::wstring(APP_NAME)
                                           : FileNameFromPath(m_docPath) + L" - " APP_NAME;
    SetWindowTextW(m_hwnd, title.c_str());
}

// ===========================================================================
// Documents
// ===========================================================================
void MainWindow::ShowOpenDialog() {
    wchar_t file[32768] = L"";
    const std::wstring dir = DirectoryFromPath(m_docPath);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) OpenFile(file, 0);
}

void MainWindow::OpenFile(const std::wstring& path, int page, const std::string& password) {
    // Normalise relative paths from the command line.
    wchar_t full[32768];
    DWORD n = GetFullPathNameW(path.c_str(), (DWORD)(sizeof(full) / sizeof(full[0])), full, nullptr);
    std::wstring fullPath = (n > 0 && n < sizeof(full) / sizeof(full[0])) ? full : path;

    if (!FileExists(fullPath)) {
        std::wstring msg = FileNameFromPath(fullPath) + L"\n\n" + OpenErrorText(OpenError::NotFound);
        MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }
    if (password.empty()) m_passwordAttempts = 0;
    m_pendingDocId = m_nextDocId++;
    m_pendingPage = page;
    if (!m_view.HasDocument()) m_view.SetMessage(L"Opening " + FileNameFromPath(fullPath) + L"\x2026");
    m_worker.OpenDocument(m_pendingDocId, fullPath, password);
}

void MainWindow::OnDocLoaded(DocLoadResult* result) {
    std::unique_ptr<DocLoadResult> res(result);
    if (res->docId != m_pendingDocId) return;  // superseded by a newer open
    const std::wstring name = FileNameFromPath(res->path);

    if (res->error == OpenError::Password) {
        PasswordPrompt prompt;
        prompt.fileName = name;
        prompt.retry = m_passwordAttempts > 0;
        if (DialogBoxParamW(m_inst, MAKEINTRESOURCEW(IDD_PASSWORD), m_hwnd, PasswordDlgProc,
                            (LPARAM)&prompt) == IDOK) {
            ++m_passwordAttempts;
            std::string pw = WideToUtf8(prompt.password);
            SecureZeroMemory(prompt.password.data(), prompt.password.size() * sizeof(wchar_t));
            OpenFile(res->path, m_pendingPage, pw);
            SecureZeroMemory(pw.data(), pw.size());
        } else {
            m_passwordAttempts = 0;
            if (!m_view.HasDocument()) m_view.SetMessage({});
        }
        return;
    }

    if (res->error != OpenError::None) {
        // The previous document (if any) stays open.
        std::wstring msg = name + L"\n\n" + OpenErrorText(res->error);
        if (!m_view.HasDocument()) m_view.SetMessage({});
        MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }

    m_passwordAttempts = 0;
    m_docId = res->docId;
    m_docPath = res->path;
    m_settings.lastFile = m_docPath;

    // A new document invalidates any search in progress.
    m_worker.CancelSearch();
    m_search.Reset();
    m_search.query.clear();

    m_view.SetDocument(m_docId, std::move(res->pageSizes), m_pendingPage);
    UpdateTitle();
    UpdateUi();
    if (m_searchVisible && !GetText(m_searchEdit).empty()) StartSearch();
    UpdateSearchStatus();
    if (GetFocus() != m_searchEdit) SetFocus(m_view.Hwnd());
}

// ===========================================================================
// Search
// ===========================================================================
void MainWindow::ShowSearch(bool show) {
    if (show && !m_view.HasDocument()) return;
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
    m_worker.CancelSearch();
    m_search.Reset();
    m_search.query.clear();
    Layout();
    InvalidateRect(m_view.Hwnd(), nullptr, FALSE);  // remove highlights
    SetFocus(m_view.Hwnd());
}

void MainWindow::StartSearch() {
    KillTimer(m_hwnd, kSearchTimer);
    const std::wstring query = GetText(m_searchEdit);
    m_worker.CancelSearch();
    m_search.Reset();
    m_search.query = query;
    m_search.matchCase = m_settings.matchCase;
    ++m_search.id;
    if (!query.empty() && m_view.HasDocument()) {
        m_search.running = true;
        m_search.pageCount = m_view.PageCount();
        m_worker.StartSearch(m_docId, m_search.id, query, m_search.matchCase, m_view.CurrentPage());
    }
    UpdateSearchStatus();
    InvalidateRect(m_view.Hwnd(), nullptr, FALSE);
}

void MainWindow::FindNext(bool forward) {
    if (!m_searchVisible) {
        ShowSearch(true);
        return;
    }
    // Enter after editing the query starts a new search.
    if (GetText(m_searchEdit) != m_search.query || m_search.matchCase != m_settings.matchCase) {
        StartSearch();
        return;
    }
    if (m_search.matches.empty()) return;
    if (forward)
        m_search.Next();
    else
        m_search.Prev();
    if (const SearchHit* hit = m_search.Current()) m_view.ScrollToHit(*hit);
    UpdateSearchStatus();
    InvalidateRect(m_view.Hwnd(), nullptr, FALSE);
}

void MainWindow::OnSearchResult(SearchPageResult* result) {
    std::unique_ptr<SearchPageResult> res(result);
    if (res->searchId != m_search.id || res->docId != m_docId) return;  // stale
    m_search.pagesDone = res->pagesDone;
    if (res->finished) m_search.running = false;
    const bool hadHits = !res->hits.empty();
    if (m_search.AddHits(std::move(res->hits))) {
        if (const SearchHit* hit = m_search.Current()) m_view.ScrollToHit(*hit);
    }
    if (hadHits) InvalidateRect(m_view.Hwnd(), nullptr, FALSE);
    UpdateSearchStatus();
}

void MainWindow::UpdateSearchStatus() {
    m_searchBar.SetText(ID_SEARCH_STATUS, m_search.StatusText());
    const bool any = !m_search.matches.empty();
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
    SetFocus(m_view.Hwnd());
}

void MainWindow::OnThemeChanged() {
    ReloadTheme();
    ApplyWindowTheme(m_hwnd);
    m_toolbar.OnThemeChanged();
    m_searchBar.OnThemeChanged();
    m_view.OnThemeChanged();
    // Nudge the frame so DWM repaints the title bar colour.
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
    m_settings.zoomMode = (int)m_view.GetZoomMode();
    m_settings.zoom = m_view.Zoom();
    m_settings.continuous = m_view.Continuous();
    if (!m_docPath.empty()) {
        m_settings.lastFile = m_docPath;
        m_settings.lastPage = m_view.CurrentPage();
    }
    m_settings.Save();
}
