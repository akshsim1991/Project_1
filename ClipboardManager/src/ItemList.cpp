// ItemList.cpp - drawing the history rows.
#include "ItemList.h"

#include <algorithm>

#include <commctrl.h>

#include "Images.h"
#include "Theme.h"
#include "Toolbar.h"
#include "Util.h"

using namespace Gdiplus;

namespace {
constexpr int kRowDip = 58;
constexpr int kThumbWDip = 76;
constexpr size_t kMaxThumbs = 400;

const wchar_t kGlyphText[] = L"\xE8A5";   // document
const wchar_t kGlyphFiles[] = L"\xE8B7";  // folder
const wchar_t kGlyphPin[] = L"\xE718";

COLORREF Mix(COLORREF a, COLORREF b, int percentB) {
    auto ch = [&](int ca, int cb) { return (ca * (100 - percentB) + cb * percentB) / 100; };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

std::vector<std::wstring> Lines(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t e = s.find(L'\n', pos);
        if (e == std::wstring::npos) e = s.size();
        std::wstring l = s.substr(pos, e - pos);
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        if (!l.empty()) out.push_back(l);
        pos = e + 1;
    }
    return out;
}
}  // namespace

std::wstring FormatCount(uint64_t n) {
    std::wstring s = std::to_wstring(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, L",");
    return s;
}

std::wstring ItemTitle(const ClipItem& it, size_t maxChars) {
    if (it.kind == ClipKind::Image)
        return L"Picture, " + std::to_wstring(it.width) + L" \x00D7 " + std::to_wstring(it.height);
    if (it.kind == ClipKind::Files) {
        const auto lines = Lines(it.text);
        std::wstring names;
        for (const std::wstring& p : lines) {
            const size_t slash = p.find_last_of(L'\\');
            std::wstring name = slash == std::wstring::npos || slash + 1 == p.size() ? p : p.substr(slash + 1);
            if (!names.empty()) names += L", ";
            names += name;
            if (names.size() > maxChars) break;
        }
        if (lines.size() > 1) names = std::to_wstring(lines.size()) + L" files: " + names;
        return names.size() > maxChars ? names.substr(0, maxChars) + L"\x2026" : names;
    }
    // The text on one line: runs of spaces, tabs and line breaks collapsed.
    std::wstring out;
    bool space = false;
    for (size_t i = 0; i < it.text.size() && out.size() < maxChars; ++i) {
        const wchar_t c = it.text[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x00A0) {
            space = !out.empty();
            continue;
        }
        if (space) out += L' ';
        space = false;
        out += c;
    }
    if (out.size() >= maxChars) out += L"\x2026";
    if (out.empty()) out = L"(spaces or empty lines only)";
    return out;
}

std::wstring ItemDetails(const ClipItem& it) {
    std::wstring s = FormatWhen(it.id);
    if (!it.source.empty()) s += L"  \x00B7  " + it.source;
    s += L"  \x00B7  ";
    if (it.kind == ClipKind::Image) {
        s += FormatSize(it.bytes);
    } else if (it.kind == ClipKind::Files) {
        const size_t n = Lines(it.text).size();
        s += n == 1 ? L"1 copied file" : FormatCount(n) + L" copied files";
    } else if (it.partial) {
        s += FormatSize(it.bytes) + L" of text";
    } else {
        s += it.length == 1 ? L"1 character" : FormatCount(it.length) + L" characters";
    }
    return s;
}

ItemList::~ItemList() {
    if (m_font) DeleteObject(m_font);
    if (m_smallFont) DeleteObject(m_smallFont);
    if (m_iconFont) DeleteObject(m_iconFont);
    if (m_pinFont) DeleteObject(m_pinFont);
    if (m_rowImages) ImageList_Destroy(m_rowImages);
}

bool ItemList::Create(HWND parent, int id) {
    m_hwnd = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA |
                                 LVS_OWNERDRAWFIXED | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
                             0, 0, 100, 100, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd) return false;
    ListView_SetExtendedListViewStyle(m_hwnd, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH;
    col.cx = 100;
    ListView_InsertColumn(m_hwnd, 0, &col);
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    SetRowHeight();
    OnThemeChanged();
    return true;
}

void ItemList::CreateFonts() {
    if (m_font) DeleteObject(m_font);
    if (m_smallFont) DeleteObject(m_smallFont);
    if (m_iconFont) DeleteObject(m_iconFont);
    if (m_pinFont) DeleteObject(m_pinFont);
    m_pinFont = CreateFontW(-Dpi(14, m_dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, IconFontName());
    m_font = CreateMessageFont(m_dpi, 105);
    m_smallFont = CreateMessageFont(m_dpi, 92);
    m_iconFont = CreateFontW(-Dpi(20, m_dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, IconFontName());
    SendMessageW(m_hwnd, WM_SETFONT, (WPARAM)m_font, FALSE);
}

void ItemList::SetRowHeight() {
    // A list view takes its row height from the small image list.
    if (m_rowImages) ImageList_Destroy(m_rowImages);
    m_rowImages = ImageList_Create(1, Dpi(kRowDip, m_dpi), ILC_COLOR32, 1, 0);
    ListView_SetImageList(m_hwnd, m_rowImages, LVSIL_SMALL);
}

void ItemList::OnSize() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    ListView_SetColumnWidth(m_hwnd, 0, std::max<int>(rc.right, 10));
}

void ItemList::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    CreateFonts();
    SetRowHeight();
    ForgetThumbnails();
    OnSize();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ItemList::OnThemeChanged() {
    const Theme& t = CurrentTheme();
    ListView_SetBkColor(m_hwnd, t.listBg);
    ListView_SetTextBkColor(m_hwnd, t.listBg);
    ListView_SetTextColor(m_hwnd, t.listText);
    ApplyScrollbarTheme(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ItemList::ForgetThumbnails() { m_thumbs.clear(); }

void ItemList::SetItems(const Store* store, std::vector<int> view) {
    m_store = store;
    m_view = std::move(view);
    // Drop thumbnails of deleted items.
    for (auto i = m_thumbs.begin(); i != m_thumbs.end();)
        i = store->IndexOf(i->first) < 0 ? m_thumbs.erase(i) : std::next(i);
    ListView_SetItemCountEx(m_hwnd, (int)m_view.size(), LVSICF_NOSCROLL);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

int ItemList::Focused() const {
    int i = ListView_GetNextItem(m_hwnd, -1, LVNI_FOCUSED);
    if (i >= 0 && ListView_GetItemState(m_hwnd, i, LVIS_SELECTED)) return i;
    return ListView_GetNextItem(m_hwnd, -1, LVNI_SELECTED);
}

std::vector<int> ItemList::Selected() const {
    std::vector<int> rows;
    for (int i = ListView_GetNextItem(m_hwnd, -1, LVNI_SELECTED); i >= 0;
         i = ListView_GetNextItem(m_hwnd, i, LVNI_SELECTED))
        rows.push_back(i);
    return rows;
}

void ItemList::Select(const std::vector<int>& rows, int focus) {
    ListView_SetItemState(m_hwnd, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (int r : rows)
        if (r >= 0 && r < Count()) ListView_SetItemState(m_hwnd, r, LVIS_SELECTED, LVIS_SELECTED);
    if (focus >= 0 && focus < Count()) {
        ListView_SetItemState(m_hwnd, focus, LVIS_FOCUSED, LVIS_FOCUSED);
        ListView_SetSelectionMark(m_hwnd, focus);
        ListView_EnsureVisible(m_hwnd, focus, FALSE);
    }
}

void ItemList::SelectAll() { ListView_SetItemState(m_hwnd, -1, LVIS_SELECTED, LVIS_SELECTED); }

Bitmap* ItemList::Thumbnail(const ClipItem& it, int boxW, int boxH) {
    Thumb& t = m_thumbs[it.id];
    t.used = ++m_clock;
    if (t.bmp || t.failed) return t.bmp.get();
    std::string data;
    std::unique_ptr<Bitmap> full;
    if (ReadFileBytes(m_store->PathOf(it), data)) full = DecodeImage(data);
    if (!full) {
        t.failed = true;
        return nullptr;
    }
    const double w = full->GetWidth(), h = full->GetHeight();
    const double scale = std::min({1.0, boxW / w, boxH / h});
    const int tw = std::max(1, (int)(w * scale + 0.5)), th = std::max(1, (int)(h * scale + 0.5));
    t.bmp = std::make_unique<Bitmap>(tw, th, PixelFormat32bppPARGB);
    {
        Graphics g(t.bmp.get());
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(full.get(), Rect(0, 0, tw, th), 0, 0, (INT)w, (INT)h, UnitPixel);
    }
    // Keep memory in check: forget the thumbnails used longest ago.
    if (m_thumbs.size() > kMaxThumbs) {
        std::vector<std::pair<uint64_t, int64_t>> ages;
        for (auto& [id, th2] : m_thumbs) ages.push_back({th2.used, id});
        std::sort(ages.begin(), ages.end());
        for (size_t i = 0; i < ages.size() - kMaxThumbs / 2; ++i) m_thumbs.erase(ages[i].second);
        return m_thumbs.count(it.id) ? m_thumbs[it.id].bmp.get() : nullptr;
    }
    return t.bmp.get();
}

bool ItemList::OnDrawItem(const DRAWITEMSTRUCT* dis) {
    if (dis->hwndItem != m_hwnd) return false;
    const int row = (int)dis->itemID;
    if (!m_store || row < 0 || row >= Count()) return true;
    const ClipItem& it = m_store->Items()[(size_t)m_view[(size_t)row]];
    const Theme& t = CurrentTheme();
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    RECT client;
    GetClientRect(m_hwnd, &client);
    rc.right = client.right;

    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool focusHere = GetFocus() == m_hwnd;
    COLORREF bg = t.listBg;
    if (selected) bg = Mix(t.listBg, t.accent, focusHere ? (t.dark ? 34 : 18) : (t.dark ? 18 : 10));
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    // A thin line between rows.
    RECT line{rc.left + Dpi(10, m_dpi), rc.bottom - 1, rc.right - Dpi(10, m_dpi), rc.bottom};
    brush = CreateSolidBrush(Mix(t.listBg, t.listText, 9));
    FillRect(dc, &line, brush);
    DeleteObject(brush);
    if (selected) {
        RECT bar{rc.left, rc.top + Dpi(8, m_dpi), rc.left + Dpi(3, m_dpi), rc.bottom - Dpi(8, m_dpi)};
        brush = CreateSolidBrush(t.accent);
        FillRect(dc, &bar, brush);
        DeleteObject(brush);
    }

    const int pad = Dpi(8, m_dpi);
    RECT box{rc.left + Dpi(10, m_dpi), rc.top + pad - Dpi(2, m_dpi), rc.left + Dpi(10 + kThumbWDip, m_dpi),
             rc.bottom - pad + Dpi(2, m_dpi)};
    SetBkMode(dc, TRANSPARENT);
    if (it.kind == ClipKind::Image) {
        if (Bitmap* thumb = Thumbnail(it, box.right - box.left, box.bottom - box.top)) {
            const int tw = (int)thumb->GetWidth(), th = (int)thumb->GetHeight();
            const int x = box.left + (box.right - box.left - tw) / 2, y = box.top + (box.bottom - box.top - th) / 2;
            Graphics g(dc);
            // A checkerboard shows transparent parts.
            const int cell = Dpi(4, m_dpi);
            SolidBrush light(Color(255, 255, 255, 255)), darkCell(Color(255, 222, 222, 222));
            g.FillRectangle(&light, x, y, tw, th);
            for (int cy = 0; cy < th; cy += cell)
                for (int cx = ((cy / cell) % 2) * cell; cx < tw; cx += 2 * cell)
                    g.FillRectangle(&darkCell, x + cx, y + cy, std::min(cell, tw - cx), std::min(cell, th - cy));
            g.DrawImage(thumb, x, y, tw, th);
        } else {
            SelectObject(dc, m_smallFont);
            SetTextColor(dc, t.listDim);
            DrawTextW(dc, L"(missing)", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        SelectObject(dc, m_iconFont);
        SetTextColor(dc, Mix(t.listBg, t.accent, 80));
        DrawTextW(dc, it.kind == ClipKind::Files ? kGlyphFiles : kGlyphText, -1, &box,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Text: the title and, below it, when and where.
    const int textLeft = box.right + Dpi(12, m_dpi);
    int textRight = rc.right - Dpi(10, m_dpi);
    if (it.pinned) {
        RECT pin{textRight - Dpi(18, m_dpi), rc.top + pad, textRight, rc.top + pad + Dpi(20, m_dpi)};
        SelectObject(dc, m_pinFont);
        SetTextColor(dc, t.accent);
        DrawTextW(dc, kGlyphPin, -1, &pin, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        textRight = pin.left - Dpi(4, m_dpi);
    }
    const int mid = (rc.top + rc.bottom) / 2;
    RECT title{textLeft, rc.top + pad, textRight, mid + Dpi(2, m_dpi)};
    SelectObject(dc, m_font);
    SetTextColor(dc, t.listText);
    const std::wstring titleText = ItemTitle(it);
    DrawTextW(dc, titleText.c_str(), (int)titleText.size(), &title,
              DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT details{textLeft, mid + Dpi(4, m_dpi), textRight, rc.bottom - pad};
    SelectObject(dc, m_smallFont);
    SetTextColor(dc, t.listDim);
    const std::wstring detailText = ItemDetails(it);
    DrawTextW(dc, detailText.c_str(), (int)detailText.size(), &details,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if ((dis->itemState & ODS_FOCUS) && focusHere && !(SendMessageW(m_hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT f = rc;
        InflateRect(&f, -1, -1);
        DrawFocusRect(dc, &f);
    }
    return true;
}
