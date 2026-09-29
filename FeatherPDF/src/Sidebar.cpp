// Sidebar.cpp - bookmarks tree / thumbnails panel (see Sidebar.h).
#include "Sidebar.h"

#include <algorithm>

#include <commctrl.h>
#include <uxtheme.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClassName[] = L"FeatherSidebar";
}

Sidebar::~Sidebar() {
    if (m_font) DeleteObject(m_font);
}

bool Sidebar::Create(HWND parent, HWND target, RenderWorker* worker) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &Sidebar::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_target = target;
    m_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                             nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;

    m_tree = CreateWindowExW(0, WC_TREEVIEWW, L"",
                             WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT |
                                 TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_NOHSCROLL |
                                 TVS_INFOTIP,
                             0, 0, 0, 0, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    TreeView_SetExtendedStyle(m_tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    m_font = CreateMessageFont(GetWindowDpi(m_hwnd));
    SendMessageW(m_tree, WM_SETFONT, (WPARAM)m_font, FALSE);
    ApplyTreeTheme();

    m_thumbs.Create(m_hwnd, target, worker);
    return true;
}

void Sidebar::ApplyTreeTheme() {
    const Theme& th = CurrentTheme();
    SetWindowTheme(m_tree, th.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    TreeView_SetBkColor(m_tree, th.barBg);
    TreeView_SetTextColor(m_tree, th.barText);
}

void Sidebar::SetMode(SidebarMode mode) {
    m_mode = mode;
    ShowWindow(m_tree, mode == SidebarMode::Bookmarks ? SW_SHOW : SW_HIDE);
    ShowWindow(m_thumbs.Hwnd(), mode == SidebarMode::Thumbnails ? SW_SHOW : SW_HIDE);
    if (mode != SidebarMode::Thumbnails) m_thumbs.TrimMemory();
    Layout();
}

void Sidebar::SetOutline(const std::vector<OutlineItem>* outline) {
    m_outline = outline;
    m_filling = true;
    SendMessageW(m_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(m_tree);
    if (!outline || outline->empty()) {
        TVINSERTSTRUCTW ins{};
        ins.hParent = TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = const_cast<wchar_t*>(outline ? L"This document has no bookmarks"
                                                        : L"No document");
        ins.item.lParam = -1;
        TreeView_InsertItem(m_tree, &ins);
    } else {
        // Items arrive flattened depth-first with a level; keep a stack of
        // the last item inserted at each level to find parents.
        std::vector<HTREEITEM> parents;
        const bool expandTop = outline->size() <= 300;
        for (size_t i = 0; i < outline->size(); ++i) {
            const OutlineItem& item = (*outline)[i];
            const size_t level = std::min((size_t)item.level, parents.size());
            parents.resize(level);
            TVINSERTSTRUCTW ins{};
            ins.hParent = level ? parents.back() : TVI_ROOT;
            ins.hInsertAfter = TVI_LAST;
            ins.item.mask = TVIF_TEXT | TVIF_PARAM;
            std::wstring title = item.title.empty() ? L"(untitled)" : item.title;
            ins.item.pszText = title.data();
            ins.item.lParam = (LPARAM)i;
            parents.push_back(TreeView_InsertItem(m_tree, &ins));
        }
        if (expandTop) {  // small outlines: show the top-level chapters open
            for (HTREEITEM h = TreeView_GetRoot(m_tree); h; h = TreeView_GetNextSibling(m_tree, h))
                TreeView_Expand(m_tree, h, TVE_EXPAND);
        }
    }
    SendMessageW(m_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_tree, nullptr, TRUE);
    m_filling = false;
}

void Sidebar::OnDpiChanged() {
    if (m_font) DeleteObject(m_font);
    m_font = CreateMessageFont(GetWindowDpi(m_hwnd));
    SendMessageW(m_tree, WM_SETFONT, (WPARAM)m_font, TRUE);
    m_thumbs.OnDpiChanged();
    Layout();
}

void Sidebar::OnThemeChanged() {
    ApplyTreeTheme();
    ApplyScrollbarTheme(m_thumbs.Hwnd());
    InvalidateRect(m_hwnd, nullptr, TRUE);
    InvalidateRect(m_thumbs.Hwnd(), nullptr, FALSE);
}

void Sidebar::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    MoveWindow(m_tree, 0, 0, rc.right, rc.bottom, TRUE);
    MoveWindow(m_thumbs.Hwnd(), 0, 0, rc.right, rc.bottom, TRUE);
}

LRESULT CALLBACK Sidebar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Sidebar* self;
    if (msg == WM_NCCREATE) {
        self = (Sidebar*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (Sidebar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Sidebar::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            SetBkColor((HDC)wp, CurrentTheme().barBg);
            ExtTextOutW((HDC)wp, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
            return 1;
        }
        case WM_NOTIFY: {
            auto* hdr = (NMHDR*)lp;
            if (hdr->hwndFrom != m_tree || m_filling) break;
            HTREEITEM item = nullptr;
            if (hdr->code == TVN_SELCHANGEDW) {
                auto* nm = (NMTREEVIEWW*)lp;
                if (nm->action == TVC_BYMOUSE || nm->action == TVC_BYKEYBOARD) item = nm->itemNew.hItem;
            } else if (hdr->code == NM_CLICK) {
                // Clicking the already selected bookmark jumps there again.
                TVHITTESTINFO hit{};
                GetCursorPos(&hit.pt);
                ScreenToClient(m_tree, &hit.pt);
                if (TreeView_HitTest(m_tree, &hit) && (hit.flags & TVHT_ONITEM) &&
                    hit.hItem == TreeView_GetSelection(m_tree))
                    item = hit.hItem;
            }
            if (item) {
                TVITEMW tvi{};
                tvi.mask = TVIF_PARAM;
                tvi.hItem = item;
                if (TreeView_GetItem(m_tree, &tvi) && tvi.lParam >= 0)
                    PostMessageW(m_target, WM_APP_SIDEBAR, kSidebarOutlineClicked, tvi.lParam);
            }
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
