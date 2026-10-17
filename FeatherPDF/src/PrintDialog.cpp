// PrintDialog.cpp - see PrintDialog.h.
//
// The preview: the sheets are laid out with the paper size and printable
// area the chosen printer reports (an information context, no printing),
// and each page is rendered by the worker, as printed (comments and form
// fields included, true colours), at a modest size and kept in a small
// cache. Painting scales those pictures into the sheet.
#include "PrintDialog.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

#include <commctrl.h>
#include <windowsx.h>
#include <winspool.h>

#include "RenderWorker.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
const wchar_t* const kLayouts[] = {L"1 page per sheet",    L"2 pages per sheet", L"4 pages per sheet",
                                   L"6 pages per sheet",   L"9 pages per sheet",
                                   L"Booklet (fold in the middle)"};
const int kPerSheet[] = {1, 2, 4, 6, 9, 0};
const wchar_t* const kScales[] = {L"Fit to the paper", L"Actual size", L"Shrink large pages only"};
const wchar_t* const kOrientations[] = {L"Automatic (as the pages)", L"Portrait", L"Landscape"};
constexpr size_t kMaxCached = 32;  // preview pictures kept

std::wstring GetText(HWND h) {
    std::wstring s((size_t)GetWindowTextLengthW(h) + 1, L'\0');
    s.resize((size_t)GetWindowTextW(h, s.data(), (int)s.size()));
    return s;
}

std::vector<std::wstring> ListPrinters() {
    std::vector<std::wstring> out;
    DWORD needed = 0, count = 0;
    const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &count);
    if (!needed) return out;
    std::vector<BYTE> buf(needed);
    if (!EnumPrintersW(flags, nullptr, 4, buf.data(), needed, &needed, &count)) return out;
    const auto* info = reinterpret_cast<const PRINTER_INFO_4W*>(buf.data());
    for (DWORD i = 0; i < count; ++i)
        if (info[i].pPrinterName) out.push_back(info[i].pPrinterName);
    return out;
}

std::wstring DefaultPrinter() {
    DWORD n = 0;
    GetDefaultPrinterW(nullptr, &n);
    if (!n) return {};
    std::wstring name(n, L'\0');
    if (!GetDefaultPrinterW(name.data(), &n)) return {};
    name.resize(wcslen(name.c_str()));
    return name;
}

// The printer's settings (DEVMODEW); `prompt`: through its Properties window.
bool PrinterSettings(HWND owner, const std::wstring& printer, std::vector<uint8_t>& devMode, bool prompt) {
    HANDLE h = nullptr;
    if (!OpenPrinterW(const_cast<wchar_t*>(printer.c_str()), &h, nullptr)) return false;
    bool ok = false;
    const LONG size = DocumentPropertiesW(owner, h, const_cast<wchar_t*>(printer.c_str()), nullptr, nullptr, 0);
    if (size > 0) {
        std::vector<uint8_t> out((size_t)size);
        const bool haveIn = devMode.size() >= sizeof(DEVMODEW);
        DWORD mode = DM_OUT_BUFFER;
        if (haveIn) mode |= DM_IN_BUFFER;
        if (prompt) mode |= DM_IN_PROMPT;
        const LONG r = DocumentPropertiesW(owner, h, const_cast<wchar_t*>(printer.c_str()),
                                           reinterpret_cast<DEVMODEW*>(out.data()),
                                           haveIn ? reinterpret_cast<DEVMODEW*>(devMode.data()) : nullptr, mode);
        ok = r == IDOK;
        if (ok) devMode.swap(out);
    }
    ClosePrinter(h);
    return ok;
}

// The settings with the orientation the layout needs; copies are made by
// repeating sheets, so the printer makes one of each.
std::vector<uint8_t> ForPrinting(const std::vector<uint8_t>& devMode, bool landscape) {
    std::vector<uint8_t> dm = devMode;
    if (dm.size() >= sizeof(DEVMODEW)) {
        auto* d = reinterpret_cast<DEVMODEW*>(dm.data());
        d->dmOrientation = landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
        d->dmFields |= DM_ORIENTATION;
        if (d->dmFields & DM_COPIES) d->dmCopies = 1;
        if (d->dmFields & DM_COLLATE) d->dmCollate = DMCOLLATE_FALSE;
    }
    return dm;
}

HDC PrinterDC(const std::wstring& printer, const std::vector<uint8_t>& dm, bool infoOnly) {
    const DEVMODEW* d = dm.size() >= sizeof(DEVMODEW) ? reinterpret_cast<const DEVMODEW*>(dm.data()) : nullptr;
    return infoOnly ? CreateICW(L"WINSPOOL", printer.c_str(), nullptr, d)
                    : CreateDCW(L"WINSPOOL", printer.c_str(), nullptr, d);
}

bool ReadPaper(HDC dc, PaperInfo& paper) {
    const double dx = GetDeviceCaps(dc, LOGPIXELSX), dy = GetDeviceCaps(dc, LOGPIXELSY);
    const int pw = GetDeviceCaps(dc, PHYSICALWIDTH), ph = GetDeviceCaps(dc, PHYSICALHEIGHT);
    if (dx <= 0 || dy <= 0 || pw <= 0 || ph <= 0) return false;
    const double ox = GetDeviceCaps(dc, PHYSICALOFFSETX), oy = GetDeviceCaps(dc, PHYSICALOFFSETY);
    const double w = GetDeviceCaps(dc, HORZRES), h = GetDeviceCaps(dc, VERTRES);
    paper.w = (float)(pw * 72 / dx);
    paper.h = (float)(ph * 72 / dy);
    paper.printable = {(float)(ox * 72 / dx), (float)(oy * 72 / dy), (float)((ox + w) * 72 / dx),
                       (float)((oy + h) * 72 / dy)};
    return true;
}

// A paper's common name ("A4") from its size in points.
std::wstring PaperName(float w, float h) {
    const float a = std::min(w, h), b = std::max(w, h);
    struct Known {
        const wchar_t* name;
        float a, b;
    };
    static const Known kKnown[] = {{L"A4", 595.3f, 841.9f}, {L"A3", 841.9f, 1190.6f}, {L"A5", 419.5f, 595.3f},
                                   {L"Letter", 612, 792},   {L"Legal", 612, 1008},    {L"B5", 498.9f, 708.7f}};
    for (const Known& k : kKnown)
        if (std::fabs(a - k.a) < 4 && std::fabs(b - k.b) < 4) return k.name;
    return {};
}

struct Dialog {
    HWND dlg = nullptr;
    RenderWorker* worker = nullptr;
    uint32_t docId = 0;
    const std::vector<SizeF>* sizes = nullptr;
    PrintSettings* settings = nullptr;
    HDC* outDC = nullptr;
    std::vector<PrintSheet>* outSheets = nullptr;

    std::vector<std::wstring> printers;
    std::wstring printer;
    std::vector<uint8_t> devMode;
    std::map<int, PaperInfo> papers;  // by orientation (0 portrait, 1 landscape), for this printer
    PaperInfo paper;
    bool landscape = false;
    PrintOptions options;
    PageChoice pages;
    std::vector<PrintSheet> sheets;  // one set, for the preview
    int sheet = 0;

    struct Picture {
        PixelBuffer px;
        uint64_t used = 0;
    };
    std::map<int, Picture> cache;  // page * 4 + rotate
    std::set<int> requested;
    uint64_t clock = 0;

    ~Dialog() {
        for (auto& p : cache) p.second.px.Free();
    }
};
Dialog* g_open = nullptr;

int Key(int page, int rotate) { return page * 4 + (rotate & 3); }

void RequestPictures(Dialog& d) {
    for (int s = d.sheet; s <= d.sheet + 1 && s < (int)d.sheets.size(); ++s) {
        for (const PrintCell& c : d.sheets[(size_t)s].cells) {
            const int key = Key(c.page, c.rotate);
            if (d.cache.count(key) || d.requested.count(key) || c.page < 0 || c.page >= (int)d.sizes->size())
                continue;
            const SizeF sz = (*d.sizes)[(size_t)c.page];
            // About 1000 pixels on the long side: sharp in the preview, small to keep.
            const float scale = std::clamp(1000.0f / std::max(1.0f, std::max(sz.w, sz.h)), 0.1f, 3.0f);
            d.requested.insert(key);
            d.worker->RenderPreview(d.docId, c.page, scale, c.rotate, 0);
        }
    }
}

// Paper size, orientation and what the printer can print on.
void UpdatePaper(Dialog& d) {
    const int key = d.landscape ? 1 : 0;
    auto it = d.papers.find(key);
    if (it == d.papers.end()) {
        PaperInfo p;
        bool ok = false;
        if (!d.printer.empty()) {
            if (HDC ic = PrinterDC(d.printer, ForPrinting(d.devMode, d.landscape), true)) {
                ok = ReadPaper(ic, p);
                DeleteDC(ic);
            }
        }
        if (!ok) {  // no printer: A4 with 5 mm margins
            p = PaperInfo{};
            if (d.landscape) {
                std::swap(p.w, p.h);
                p.printable = {14.2f, 14.2f, p.w - 14.2f, p.h - 14.2f};
            }
        }
        it = d.papers.emplace(key, p).first;
    }
    d.paper = it->second;
}

void Update(Dialog& d) {
    HWND dlg = d.dlg;
    // The pages (a range being typed is used once it makes sense).
    std::vector<int> pages;
    const int count = (int)d.sizes->size();
    if (IsDlgButtonChecked(dlg, IDC_PAGES_CURRENT) == BST_CHECKED) {
        pages.push_back(std::clamp(d.pages.current, 0, count - 1));
    } else if (IsDlgButtonChecked(dlg, IDC_PAGES_RANGE) == BST_CHECKED) {
        std::vector<int> parsed;
        if (ParsePageRanges(GetText(GetDlgItem(dlg, IDC_PAGES_EDIT)), count, parsed)) pages = parsed;
        else pages = d.options.pages;
    } else {
        for (int i = 0; i < count; ++i) pages.push_back(i);
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    d.options.pages = pages;

    const int layout = std::max(0, (int)SendDlgItemMessageW(dlg, IDC_PR_LAYOUT, CB_GETCURSEL, 0, 0));
    d.options.booklet = kPerSheet[std::min(layout, 5)] == 0;
    d.options.perSheet = d.options.booklet ? 2 : kPerSheet[std::min(layout, 5)];
    d.options.scale = std::max(0, (int)SendDlgItemMessageW(dlg, IDC_PR_SCALE, CB_GETCURSEL, 0, 0));
    d.options.orientation = std::max(0, (int)SendDlgItemMessageW(dlg, IDC_PR_ORIENT, CB_GETCURSEL, 0, 0));
    BOOL ok = FALSE;
    const UINT copies = GetDlgItemInt(dlg, IDC_PR_COPIES, &ok, FALSE);
    d.options.copies = ok ? (int)std::clamp<UINT>(copies, 1, 999) : 1;
    d.options.collate = IsDlgButtonChecked(dlg, IDC_PR_COLLATE) == BST_CHECKED;
    EnableWindow(GetDlgItem(dlg, IDC_PR_SCALE), d.options.perSheet == 1 && !d.options.booklet);
    EnableWindow(GetDlgItem(dlg, IDC_PR_COLLATE), d.options.copies > 1 && !d.options.booklet);

    d.landscape = PrintLandscape(d.options, *d.sizes);
    UpdatePaper(d);
    d.sheets = LayoutSheets(d.options, *d.sizes, d.paper, false);
    d.sheet = std::clamp(d.sheet, 0, std::max(0, (int)d.sheets.size() - 1));

    // What it will be printed on.
    std::wstring paper;
    if (d.printer.empty()) {
        paper = L"No printer is installed: the preview shows A4 paper.";
    } else {
        const std::wstring name = PaperName(d.paper.w, d.paper.h);
        wchar_t mm[64];
        swprintf_s(mm, L"%.0f \x00D7 %.0f mm", d.paper.w / 72 * 25.4, d.paper.h / 72 * 25.4);
        paper = L"Paper: " + (name.empty() ? std::wstring() : name + L", ") + mm +
                (d.landscape ? L", landscape" : L", portrait");
    }
    SetDlgItemTextW(dlg, IDC_PR_PAPER, paper.c_str());

    const int sides = (int)d.sheets.size();
    const int sheetsOfPaper = d.options.booklet ? (sides + 1) / 2 : sides;
    const int total = sheetsOfPaper * d.options.copies;
    std::wstring note = std::to_wstring(total) + (total == 1 ? L" sheet of paper" : L" sheets of paper");
    if (d.options.copies > 1) note += L" (" + std::to_wstring(d.options.copies) + L" copies)";
    note += L".\n\n";
    if (d.options.booklet) {
        note += L"Booklet: print on both sides of the paper, flipping on the short edge (choose two-sided "
                L"printing in Properties; printers that can not turn the paper themselves usually offer manual "
                L"two-sided printing there). Then fold the stack in the middle.";
    } else {
        note += L"Comments, highlights and filled-in form fields are printed. Colours are printed as in the "
                L"document (dark and dimmed page colours are only for the screen).";
    }
    SetDlgItemTextW(dlg, IDC_PR_NOTE, note.c_str());

    std::wstring label;
    if (d.sheets.empty()) {
        label = L"Nothing to print";
    } else if (d.options.booklet) {
        label = L"Sheet " + std::to_wstring(d.sheet / 2 + 1) + (d.sheet % 2 ? L", back" : L", front") + L" (" +
                std::to_wstring(d.sheet + 1) + L" of " + std::to_wstring(sides) + L")";
    } else {
        label = L"Sheet " + std::to_wstring(d.sheet + 1) + L" of " + std::to_wstring(sides);
    }
    SetDlgItemTextW(dlg, IDC_PR_SHEET, label.c_str());
    EnableWindow(GetDlgItem(dlg, IDC_PR_PREV), d.sheet > 0);
    EnableWindow(GetDlgItem(dlg, IDC_PR_NEXT), d.sheet + 1 < sides);
    EnableWindow(GetDlgItem(dlg, IDOK), !d.printer.empty() && !d.sheets.empty());

    RequestPictures(d);
    InvalidateRect(GetDlgItem(dlg, IDC_PR_PREVIEW), nullptr, FALSE);
}

void DrawPreview(Dialog& d, const DRAWITEMSTRUCT* di) {
    HDC dc = di->hDC;
    const RECT rc = di->rcItem;
    HBRUSH back = CreateSolidBrush(RGB(96, 96, 100));
    FillRect(dc, &rc, back);
    DeleteObject(back);
    if (d.sheets.empty()) return;
    // The sheet, as large as fits.
    const int margin = 12;
    const double k = std::min((rc.right - rc.left - 2.0 * margin) / d.paper.w,
                              (rc.bottom - rc.top - 2.0 * margin) / d.paper.h);
    if (k <= 0) return;
    const int pw = (int)std::lround(d.paper.w * k), ph = (int)std::lround(d.paper.h * k);
    const int px = rc.left + (rc.right - rc.left - pw) / 2, py = rc.top + (rc.bottom - rc.top - ph) / 2;
    RECT shadow = {px + 4, py + 4, px + pw + 4, py + ph + 4};
    HBRUSH dark = CreateSolidBrush(RGB(60, 60, 64));
    FillRect(dc, &shadow, dark);
    DeleteObject(dark);
    RECT sheet = {px, py, px + pw, py + ph};
    FillRect(dc, &sheet, (HBRUSH)GetStockObject(WHITE_BRUSH));

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, sheet.left, sheet.top, sheet.right, sheet.bottom);
    // The edge of what the printer can print on.
    HPEN dotted = CreatePen(PS_DOT, 1, RGB(200, 200, 200));
    HGDIOBJ oldPen = SelectObject(dc, dotted);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    Rectangle(dc, px + (int)(d.paper.printable.left * k), py + (int)(d.paper.printable.top * k),
              px + (int)(d.paper.printable.right * k), py + (int)(d.paper.printable.bottom * k));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(dotted);

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    for (const PrintCell& c : d.sheets[(size_t)d.sheet].cells) {
        const RECT r = {px + (int)std::lround(c.rect.left * k), py + (int)std::lround(c.rect.top * k),
                        px + (int)std::lround(c.rect.right * k), py + (int)std::lround(c.rect.bottom * k)};
        auto it = d.cache.find(Key(c.page, c.rotate));
        if (it != d.cache.end() && it->second.px.bits) {
            const PixelBuffer& b = it->second.px;
            it->second.used = ++d.clock;
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = b.width;
            bi.bmiHeader.biHeight = -b.height;  // top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, b.width, b.height, b.bits,
                          &bi, DIB_RGB_COLORS, SRCCOPY);
        } else {  // still being drawn
            HBRUSH wait = CreateSolidBrush(RGB(236, 236, 236));
            FillRect(dc, &r, wait);
            DeleteObject(wait);
        }
        // A thin edge, so white pages show where they are.
        HPEN edge = CreatePen(PS_SOLID, 1, RGB(215, 215, 215));
        HGDIOBJ op = SelectObject(dc, edge);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(edge);
    }
    RestoreDC(dc, saved);
}

void ChoosePrinter(Dialog& d, const std::wstring& name) {
    d.printer = name;
    d.papers.clear();
    d.devMode.clear();
    if (name.empty()) return;
    if (name == d.settings->printer && d.settings->devMode.size() >= sizeof(DEVMODEW))
        d.devMode = d.settings->devMode;
    else
        PrinterSettings(d.dlg, name, d.devMode, false);
}

bool Finish(Dialog& d) {
    if (!ReadPageChoice(d.dlg, d.pages)) return false;
    Update(d);
    if (d.printer.empty() || d.sheets.empty()) return false;
    const std::vector<uint8_t> dm = ForPrinting(d.devMode, d.landscape);
    HDC dc = PrinterDC(d.printer, dm, false);
    if (!dc) {
        MessageBoxW(d.dlg, (L"The printer \x201C" + d.printer + L"\x201D could not be used. Check that it is on and "
                                                                L"connected, or choose another one.")
                               .c_str(),
                    APP_NAME, MB_ICONWARNING);
        return false;
    }
    PaperInfo paper = d.paper;
    ReadPaper(dc, paper);  // what the printer really uses now
    *d.outSheets = LayoutSheets(d.options, *d.sizes, paper, true);
    *d.outDC = dc;
    d.settings->printer = d.printer;
    d.settings->devMode = d.devMode;
    d.settings->pages = d.pages;
    d.settings->options = d.options;
    return true;
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    Dialog* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            d = reinterpret_cast<Dialog*>(lp);
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            d->dlg = dlg;
            g_open = d;
            // Printers: the one used last time, else the Windows default.
            d->printers = ListPrinters();
            HWND combo = GetDlgItem(dlg, IDC_PR_PRINTER);
            for (const auto& p : d->printers) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)p.c_str());
            std::wstring want = d->settings->printer;
            if (std::find(d->printers.begin(), d->printers.end(), want) == d->printers.end()) want = DefaultPrinter();
            if (std::find(d->printers.begin(), d->printers.end(), want) == d->printers.end())
                want = d->printers.empty() ? std::wstring() : d->printers.front();
            if (d->printers.empty()) {
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"(no printer installed)");
                SendMessageW(combo, CB_SETCURSEL, 0, 0);
                EnableWindow(combo, FALSE);
                EnableWindow(GetDlgItem(dlg, IDC_PR_PROPS), FALSE);
            } else {
                SendMessageW(combo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)want.c_str());
            }
            ChoosePrinter(*d, want);

            InitPageChoice(dlg, d->pages);
            for (const wchar_t* s : kLayouts) SendDlgItemMessageW(dlg, IDC_PR_LAYOUT, CB_ADDSTRING, 0, (LPARAM)s);
            for (const wchar_t* s : kScales) SendDlgItemMessageW(dlg, IDC_PR_SCALE, CB_ADDSTRING, 0, (LPARAM)s);
            for (const wchar_t* s : kOrientations) SendDlgItemMessageW(dlg, IDC_PR_ORIENT, CB_ADDSTRING, 0, (LPARAM)s);
            const PrintOptions& o = d->settings->options;
            int layout = 0;
            for (int i = 0; i < 5; ++i)
                if (kPerSheet[i] == o.perSheet) layout = i;
            if (o.booklet) layout = 5;
            SendDlgItemMessageW(dlg, IDC_PR_LAYOUT, CB_SETCURSEL, (WPARAM)layout, 0);
            SendDlgItemMessageW(dlg, IDC_PR_SCALE, CB_SETCURSEL, (WPARAM)std::clamp(o.scale, 0, 2), 0);
            SendDlgItemMessageW(dlg, IDC_PR_ORIENT, CB_SETCURSEL, (WPARAM)std::clamp(o.orientation, 0, 2), 0);
            SendDlgItemMessageW(dlg, IDC_PR_COPIES_SPIN, UDM_SETRANGE32, 1, 999);
            SetDlgItemInt(dlg, IDC_PR_COPIES, 1, FALSE);
            CheckDlgButton(dlg, IDC_PR_COLLATE, o.collate ? BST_CHECKED : BST_UNCHECKED);
            ApplyWindowTheme(dlg);
            Update(*d);
            return TRUE;
        }
        case WM_DRAWITEM: {
            const auto* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (d && di->CtlID == IDC_PR_PREVIEW) {
                DrawPreview(*d, di);
                return TRUE;
            }
            break;
        }
        case WM_MOUSEWHEEL:
            if (d && !d->sheets.empty()) {
                const int step = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
                const int next = std::clamp(d->sheet + step, 0, (int)d->sheets.size() - 1);
                if (next != d->sheet) {
                    d->sheet = next;
                    Update(*d);
                }
                return TRUE;
            }
            break;
        case WM_COMMAND: {
            if (!d) break;
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (PageChoiceCommand(dlg, wp)) {
                Update(*d);
                return TRUE;
            }
            switch (id) {
                case IDC_PAGES_ALL:
                case IDC_PAGES_CURRENT:
                case IDC_PAGES_RANGE:
                case IDC_PR_COLLATE:
                    if (code == BN_CLICKED) Update(*d);
                    return TRUE;
                case IDC_PAGES_EDIT:
                case IDC_PR_COPIES:
                    if (code == EN_CHANGE) Update(*d);
                    return TRUE;
                case IDC_PR_LAYOUT:
                case IDC_PR_SCALE:
                case IDC_PR_ORIENT:
                    if (code == CBN_SELCHANGE) Update(*d);
                    return TRUE;
                case IDC_PR_PRINTER:
                    if (code == CBN_SELCHANGE) {
                        const int sel = (int)SendDlgItemMessageW(dlg, IDC_PR_PRINTER, CB_GETCURSEL, 0, 0);
                        if (sel >= 0 && sel < (int)d->printers.size()) ChoosePrinter(*d, d->printers[(size_t)sel]);
                        Update(*d);
                    }
                    return TRUE;
                case IDC_PR_PROPS:
                    if (!d->printer.empty() && PrinterSettings(dlg, d->printer, d->devMode, true)) {
                        d->papers.clear();  // the paper may have changed
                        Update(*d);
                    }
                    return TRUE;
                case IDC_PR_PREV:
                case IDC_PR_NEXT:
                    d->sheet += id == IDC_PR_NEXT ? 1 : -1;
                    Update(*d);
                    return TRUE;
                case IDOK:
                    if (Finish(*d)) EndDialog(dlg, IDOK);
                    return TRUE;
                case IDCANCEL:
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            if (g_open == d) g_open = nullptr;
            break;
    }
    return FALSE;
}
}  // namespace

bool ShowPrintDialog(HINSTANCE inst, HWND owner, RenderWorker* worker, uint32_t docId,
                     const std::vector<SizeF>& pageSizes, PrintSettings& settings, HDC& dc,
                     std::vector<PrintSheet>& sheets) {
    if (pageSizes.empty()) return false;
    auto d = std::make_unique<Dialog>();
    d->worker = worker;
    d->docId = docId;
    d->sizes = &pageSizes;
    d->settings = &settings;
    d->pages = settings.pages;
    d->options = settings.options;
    d->outDC = &dc;
    d->outSheets = &sheets;
    dc = nullptr;
    sheets.clear();
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_PRINT), owner, DialogProc, (LPARAM)d.get());
    g_open = nullptr;
    if (r != IDOK && dc) {
        DeleteDC(dc);
        dc = nullptr;
    }
    return r == IDOK && dc;
}

void OnPrintPreviewPage(TileResult* page) {
    std::unique_ptr<TileResult> res(page);
    Dialog* d = g_open;
    if (!d || res->docId != d->docId) return;
    const int key = Key(res->req.page, res->req.rotate);
    d->requested.erase(key);
    if (!res->pixels.bits) return;
    Dialog::Picture& pic = d->cache[key];
    pic.px.Free();
    pic.px = res->pixels;
    res->pixels = PixelBuffer{};  // now owned by the cache
    pic.used = ++d->clock;
    while (d->cache.size() > kMaxCached) {
        auto oldest = d->cache.begin();
        for (auto it = d->cache.begin(); it != d->cache.end(); ++it)
            if (it->second.used < oldest->second.used) oldest = it;
        oldest->second.px.Free();
        d->cache.erase(oldest);
    }
    InvalidateRect(GetDlgItem(d->dlg, IDC_PR_PREVIEW), nullptr, FALSE);
}
