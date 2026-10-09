// ToolDialogs.cpp - dialog procedures for ToolDialogs.h.
#include "ToolDialogs.h"

#include <algorithm>

#include <commctrl.h>

#include "Theme.h"
#include "Util.h"
#include "resource.h"

const COLORREF kWatermarkColors[5] = {RGB(200, 30, 30), RGB(128, 128, 128), RGB(30, 80, 200),
                                      RGB(20, 140, 60), RGB(0, 0, 0)};
const int kWatermarkOpacity[6] = {10, 20, 30, 50, 75, 100};
const float kWatermarkSizes[6] = {0, 36, 48, 72, 96, 144};
const wchar_t* const kNumberFormats[5] = {L"{n}", L"Page {n}", L"Page {n} of {total}", L"{n} / {total}",
                                          L"- {n} -"};
const float kNumberSizes[6] = {8, 9, 10, 11, 12, 14};
const int kExportDpi[3] = {72, 150, 300};
const CompressLevel kCompressLevels[3] = {
    {96, 60, L"Smallest file (96 dpi)", L"For reading on screen and sending by e-mail. Photos lose some detail."},
    {150, 75, L"Balanced (150 dpi) - recommended", L"Good on screen and for ordinary printing, at a fraction of the size."},
    {220, 85, L"High quality (220 dpi)", L"For printing photos well. Saves less space."},
};

namespace {
std::wstring Text(HWND dlg, int id) {
    HWND h = GetDlgItem(dlg, id);
    std::wstring s((size_t)GetWindowTextLengthW(h) + 1, L'\0');
    s.resize((size_t)GetWindowTextW(h, s.data(), (int)s.size()));
    return s;
}

void Fill(HWND dlg, int id, std::initializer_list<const wchar_t*> items, int selected) {
    HWND combo = GetDlgItem(dlg, id);
    for (const wchar_t* item : items) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)item);
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)selected, 0);
}

int Selected(HWND dlg, int id) {
    return std::max(0, (int)SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0));
}

// --- the shared "Pages" group ---------------------------------------------
void InitPages(HWND dlg, const PageChoice& p) {
    CheckRadioButton(dlg, IDC_PAGES_ALL, IDC_PAGES_RANGE,
                     p.mode == PageChoice::All ? IDC_PAGES_ALL
                     : p.mode == PageChoice::Current ? IDC_PAGES_CURRENT
                                                     : IDC_PAGES_RANGE);
    const std::wstring current = L"C&urrent page (" + std::to_wstring(p.current + 1) + L")";
    SetDlgItemTextW(dlg, IDC_PAGES_CURRENT, current.c_str());
    const std::wstring all = L"&All " + std::to_wstring(p.pageCount) + (p.pageCount == 1 ? L" page" : L" pages");
    SetDlgItemTextW(dlg, IDC_PAGES_ALL, all.c_str());
    SetDlgItemTextW(dlg, IDC_PAGES_EDIT, p.range.c_str());
    SendDlgItemMessageW(dlg, IDC_PAGES_EDIT, EM_SETCUEBANNER, TRUE, (LPARAM)L"for example 1-3, 5, 8-");
}

// Typing a range selects "Pages:".
bool PagesCommand(HWND dlg, WPARAM wp) {
    if (LOWORD(wp) == IDC_PAGES_EDIT && HIWORD(wp) == EN_CHANGE &&
        GetWindowTextLengthW(GetDlgItem(dlg, IDC_PAGES_EDIT)) > 0) {
        CheckRadioButton(dlg, IDC_PAGES_ALL, IDC_PAGES_RANGE, IDC_PAGES_RANGE);
        return true;
    }
    return false;
}

// Reads the group; false (after telling the user) if the range is wrong.
bool ReadPages(HWND dlg, PageChoice& p) {
    p.range = Text(dlg, IDC_PAGES_EDIT);
    p.pages.clear();
    if (IsDlgButtonChecked(dlg, IDC_PAGES_CURRENT) == BST_CHECKED) {
        p.mode = PageChoice::Current;
        p.pages.push_back(p.current);
    } else if (IsDlgButtonChecked(dlg, IDC_PAGES_RANGE) == BST_CHECKED) {
        p.mode = PageChoice::Range;
        if (!ParsePageRanges(p.range, p.pageCount, p.pages)) {
            const std::wstring msg = L"Enter page numbers or ranges between 1 and " +
                                     std::to_wstring(p.pageCount) + L", such as 1-3, 5, 8-.";
            MessageBoxW(dlg, msg.c_str(), APP_NAME, MB_ICONWARNING);
            SetFocus(GetDlgItem(dlg, IDC_PAGES_EDIT));
            return false;
        }
    } else {
        p.mode = PageChoice::All;
        for (int i = 0; i < p.pageCount; ++i) p.pages.push_back(i);
    }
    std::sort(p.pages.begin(), p.pages.end());
    p.pages.erase(std::unique(p.pages.begin(), p.pages.end()), p.pages.end());
    return !p.pages.empty();
}

// Runs a dialog whose procedure gets `data` as its DWLP_USER value.
template <typename T>
bool Run(HINSTANCE inst, HWND owner, int id, DLGPROC proc, T& data) {
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(id), owner, proc, (LPARAM)&data) == IDOK;
}

template <typename T>
T* Data(HWND dlg) {
    return reinterpret_cast<T*>(GetWindowLongPtrW(dlg, DWLP_USER));
}

INT_PTR CALLBACK OcrProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* o = reinterpret_cast<OcrOptions*>(lp);
            InitPages(dlg, o->pages);
            CheckDlgButton(dlg, IDC_OCR_SKIP, o->skipText ? BST_CHECKED : BST_UNCHECKED);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* o = Data<OcrOptions>(dlg);
            if (PagesCommand(dlg, wp)) return TRUE;
            if (LOWORD(wp) == IDOK) {
                if (!ReadPages(dlg, o->pages)) return TRUE;
                o->skipText = IsDlgButtonChecked(dlg, IDC_OCR_SKIP) == BST_CHECKED;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

INT_PTR CALLBACK WatermarkProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* o = reinterpret_cast<WatermarkOptions*>(lp);
            SetDlgItemTextW(dlg, IDC_WM_TEXT, o->text.c_str());
            SendDlgItemMessageW(dlg, IDC_WM_TEXT, EM_LIMITTEXT, 200, 0);
            Fill(dlg, IDC_WM_COLOR, {L"Red", L"Grey", L"Blue", L"Green", L"Black"}, o->color);
            Fill(dlg, IDC_WM_OPACITY, {L"10%", L"20%", L"30%", L"50%", L"75%", L"100%"}, o->opacity);
            Fill(dlg, IDC_WM_SIZE, {L"Fit the page", L"36 pt", L"48 pt", L"72 pt", L"96 pt", L"144 pt"}, o->size);
            Fill(dlg, IDC_WM_LAYOUT, {L"Diagonal", L"Across"}, o->diagonal ? 0 : 1);
            CheckDlgButton(dlg, IDC_WM_BEHIND, o->behind ? BST_CHECKED : BST_UNCHECKED);
            InitPages(dlg, o->pages);
            ApplyWindowTheme(dlg);
            SendDlgItemMessageW(dlg, IDC_WM_TEXT, EM_SETSEL, 0, -1);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* o = Data<WatermarkOptions>(dlg);
            if (PagesCommand(dlg, wp)) return TRUE;
            if (LOWORD(wp) == IDOK) {
                const std::wstring text = Text(dlg, IDC_WM_TEXT);
                if (text.find_first_not_of(L" \t") == std::wstring::npos) {
                    MessageBoxW(dlg, L"Type the text of the watermark.", APP_NAME, MB_ICONWARNING);
                    SetFocus(GetDlgItem(dlg, IDC_WM_TEXT));
                    return TRUE;
                }
                if (!ReadPages(dlg, o->pages)) return TRUE;
                o->text = text;
                o->color = Selected(dlg, IDC_WM_COLOR);
                o->opacity = Selected(dlg, IDC_WM_OPACITY);
                o->size = Selected(dlg, IDC_WM_SIZE);
                o->diagonal = Selected(dlg, IDC_WM_LAYOUT) == 0;
                o->behind = IsDlgButtonChecked(dlg, IDC_WM_BEHIND) == BST_CHECKED;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

INT_PTR CALLBACK PageNumberProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* o = reinterpret_cast<PageNumberOptions*>(lp);
            Fill(dlg, IDC_PN_FORMAT, {L"1", L"Page 1", L"Page 1 of 9", L"1 / 9", L"- 1 -"}, o->format);
            Fill(dlg, IDC_PN_POSITION,
                 {L"Bottom centre", L"Bottom right", L"Bottom left", L"Top centre", L"Top right", L"Top left"},
                 o->position);
            Fill(dlg, IDC_PN_SIZE, {L"8 pt", L"9 pt", L"10 pt", L"11 pt", L"12 pt", L"14 pt"}, o->size);
            SetDlgItemInt(dlg, IDC_PN_START, (UINT)o->start, FALSE);
            SendDlgItemMessageW(dlg, IDC_PN_START, EM_LIMITTEXT, 6, 0);
            InitPages(dlg, o->pages);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* o = Data<PageNumberOptions>(dlg);
            if (PagesCommand(dlg, wp)) return TRUE;
            if (LOWORD(wp) == IDOK) {
                BOOL ok = FALSE;
                const UINT start = GetDlgItemInt(dlg, IDC_PN_START, &ok, FALSE);
                if (!ok || start > 99999) {
                    MessageBoxW(dlg, L"Enter the number of the first page, such as 1.", APP_NAME, MB_ICONWARNING);
                    SetFocus(GetDlgItem(dlg, IDC_PN_START));
                    return TRUE;
                }
                if (!ReadPages(dlg, o->pages)) return TRUE;
                o->start = (int)start;
                o->format = Selected(dlg, IDC_PN_FORMAT);
                o->position = Selected(dlg, IDC_PN_POSITION);
                o->size = Selected(dlg, IDC_PN_SIZE);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

void UpdateExportNote(HWND dlg) {
    const int format = Selected(dlg, IDC_EXPORT_FORMAT);
    const bool pictures = format <= 1;
    EnableWindow(GetDlgItem(dlg, IDC_EXPORT_DPI), pictures);
    EnableWindow(GetDlgItem(dlg, IDC_EXPORT_DPI_LABEL), pictures);
    const wchar_t* note =
        pictures ? L"Each page becomes a picture named after the file you choose, with the page number added "
                   L"(\x201CReport-1.png\x201D)."
        : format == 2 ? L"The text of the pages in reading order. Scanned pages have text only after "
                        L"Recognise text (OCR)."
                      : L"Text with its headings, lists and paragraphs, for notes apps, wikis and AI tools. "
                        L"Scanned pages need Recognise text (OCR) first.";
    SetDlgItemTextW(dlg, IDC_EXPORT_NOTE, note);
}

INT_PTR CALLBACK ExportProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* o = reinterpret_cast<ExportOptions*>(lp);
            Fill(dlg, IDC_EXPORT_FORMAT,
                 {L"PNG pictures (best for text and drawings)", L"JPEG pictures (smaller, for photos)",
                  L"Plain text (.txt)", L"Markdown (.md)"},
                 (int)o->format);
            Fill(dlg, IDC_EXPORT_DPI, {L"72 dpi (screen, small files)", L"150 dpi (good quality)",
                                       L"300 dpi (print quality)"},
                 o->dpi);
            InitPages(dlg, o->pages);
            UpdateExportNote(dlg);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* o = Data<ExportOptions>(dlg);
            if (PagesCommand(dlg, wp)) return TRUE;
            if (LOWORD(wp) == IDC_EXPORT_FORMAT && HIWORD(wp) == CBN_SELCHANGE) {
                UpdateExportNote(dlg);
                return TRUE;
            }
            if (LOWORD(wp) == IDOK) {
                if (!ReadPages(dlg, o->pages)) return TRUE;
                o->format = (ExportFormat)Selected(dlg, IDC_EXPORT_FORMAT);
                o->dpi = Selected(dlg, IDC_EXPORT_DPI);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}
INT_PTR CALLBACK CompressProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* o = reinterpret_cast<CompressOptions*>(lp);
            Fill(dlg, IDC_COMPRESS_LEVEL, {kCompressLevels[0].name, kCompressLevels[1].name, kCompressLevels[2].name},
                 std::clamp(o->level, 0, 2));
            SetDlgItemTextW(dlg, IDC_COMPRESS_NOTE, kCompressLevels[std::clamp(o->level, 0, 2)].note);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* o = Data<CompressOptions>(dlg);
            if (LOWORD(wp) == IDC_COMPRESS_LEVEL && HIWORD(wp) == CBN_SELCHANGE) {
                SetDlgItemTextW(dlg, IDC_COMPRESS_NOTE, kCompressLevels[std::clamp(Selected(dlg, IDC_COMPRESS_LEVEL), 0, 2)].note);
                return TRUE;
            }
            if (LOWORD(wp) == IDOK) {
                o->level = std::clamp(Selected(dlg, IDC_COMPRESS_LEVEL), 0, 2);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

void UpdateProtect(HWND dlg) {
    const bool open = IsDlgButtonChecked(dlg, IDC_PW_OPEN) == BST_CHECKED;
    const bool limit = IsDlgButtonChecked(dlg, IDC_PW_LIMIT) == BST_CHECKED;
    for (int id : {IDC_PW_USER, IDC_PW_USER2}) EnableWindow(GetDlgItem(dlg, id), open);
    for (int id : {IDC_PW_PRINT, IDC_PW_COPY, IDC_PW_CHANGE, IDC_PW_OWNER, IDC_PW_OWNER2})
        EnableWindow(GetDlgItem(dlg, id), limit);
}

INT_PTR CALLBACK ProtectProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* o = reinterpret_cast<ProtectOptions*>(lp);
            SetDlgItemTextW(dlg, IDC_PW_STATE, o->state.c_str());
            CheckDlgButton(dlg, IDC_PW_OPEN, o->openPassword ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_PW_LIMIT, o->limit ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_PW_PRINT, o->allowPrint ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_PW_COPY, o->allowCopy ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_PW_CHANGE, o->allowChange ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(GetDlgItem(dlg, IDC_PW_REMOVE), o->canRemove);
            for (int id : {IDC_PW_USER, IDC_PW_USER2, IDC_PW_OWNER, IDC_PW_OWNER2})
                SendDlgItemMessageW(dlg, id, EM_LIMITTEXT, 100, 0);
            UpdateProtect(dlg);
            ApplyWindowTheme(dlg);
            SetFocus(GetDlgItem(dlg, IDC_PW_USER));
            return FALSE;
        }
        case WM_COMMAND: {
            auto* o = Data<ProtectOptions>(dlg);
            const int id = LOWORD(wp);
            if (id == IDC_PW_OPEN || id == IDC_PW_LIMIT) {
                UpdateProtect(dlg);
                return TRUE;
            }
            if (id == IDC_PW_REMOVE) {
                o->remove = true;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (id == IDOK) {
                auto fail = [&](const wchar_t* text, int focus) {
                    MessageBoxW(dlg, text, APP_NAME, MB_ICONWARNING);
                    SetFocus(GetDlgItem(dlg, focus));
                    return TRUE;
                };
                o->openPassword = IsDlgButtonChecked(dlg, IDC_PW_OPEN) == BST_CHECKED;
                o->limit = IsDlgButtonChecked(dlg, IDC_PW_LIMIT) == BST_CHECKED;
                o->userPassword = o->openPassword ? Text(dlg, IDC_PW_USER) : std::wstring();
                o->ownerPassword = o->limit ? Text(dlg, IDC_PW_OWNER) : std::wstring();
                if (!o->openPassword && !o->limit)
                    return fail(L"Choose a password to open the document, limits, or both.", IDC_PW_OPEN);
                if (o->openPassword && o->userPassword.empty()) return fail(L"Type the password.", IDC_PW_USER);
                if (o->openPassword && o->userPassword != Text(dlg, IDC_PW_USER2))
                    return fail(L"The two passwords are not the same. Type them again.", IDC_PW_USER2);
                if (o->limit && o->ownerPassword.empty())
                    return fail(L"Type an owner password: it is needed to lift the limits later.", IDC_PW_OWNER);
                if (o->limit && o->ownerPassword != Text(dlg, IDC_PW_OWNER2))
                    return fail(L"The two owner passwords are not the same. Type them again.", IDC_PW_OWNER2);
                if (o->limit && o->ownerPassword == o->userPassword)
                    return fail(L"The owner password must be different from the password to open it.", IDC_PW_OWNER);
                o->allowPrint = IsDlgButtonChecked(dlg, IDC_PW_PRINT) == BST_CHECKED;
                o->allowCopy = IsDlgButtonChecked(dlg, IDC_PW_COPY) == BST_CHECKED;
                o->allowChange = IsDlgButtonChecked(dlg, IDC_PW_CHANGE) == BST_CHECKED;
                o->remove = false;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

INT_PTR CALLBACK RedactFindProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* o = reinterpret_cast<RedactFindOptions*>(lp);
            SetDlgItemTextW(dlg, IDC_RF_TEXT, o->text.c_str());
            CheckDlgButton(dlg, IDC_RF_CASE, o->matchCase ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_RF_EMAIL, o->emails ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_RF_PHONE, o->phones ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_RF_NUMBERS, o->numbers ? BST_CHECKED : BST_UNCHECKED);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* o = Data<RedactFindOptions>(dlg);
            if (LOWORD(wp) == IDOK) {
                o->text = Text(dlg, IDC_RF_TEXT);
                o->matchCase = IsDlgButtonChecked(dlg, IDC_RF_CASE) == BST_CHECKED;
                o->emails = IsDlgButtonChecked(dlg, IDC_RF_EMAIL) == BST_CHECKED;
                o->phones = IsDlgButtonChecked(dlg, IDC_RF_PHONE) == BST_CHECKED;
                o->numbers = IsDlgButtonChecked(dlg, IDC_RF_NUMBERS) == BST_CHECKED;
                if (o->text.find_first_not_of(L" \t") == std::wstring::npos && !o->emails && !o->phones && !o->numbers) {
                    MessageBoxW(dlg, L"Type the text to find, or choose what else to mark.", APP_NAME, MB_ICONWARNING);
                    SetFocus(GetDlgItem(dlg, IDC_RF_TEXT));
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
    }
    return FALSE;
}
}  // namespace

bool ShowCompressDialog(HINSTANCE inst, HWND owner, CompressOptions& o) {
    return Run(inst, owner, IDD_COMPRESS, CompressProc, o);
}

bool ShowProtectDialog(HINSTANCE inst, HWND owner, ProtectOptions& o) {
    return Run(inst, owner, IDD_PROTECT, ProtectProc, o);
}

bool ShowRedactFindDialog(HINSTANCE inst, HWND owner, RedactFindOptions& o) {
    return Run(inst, owner, IDD_REDACT_FIND, RedactFindProc, o);
}

bool ShowOcrDialog(HINSTANCE inst, HWND owner, OcrOptions& o) {
    return Run(inst, owner, IDD_OCR, OcrProc, o);
}

bool ShowWatermarkDialog(HINSTANCE inst, HWND owner, WatermarkOptions& o) {
    return Run(inst, owner, IDD_WATERMARK, WatermarkProc, o);
}

bool ShowPageNumberDialog(HINSTANCE inst, HWND owner, PageNumberOptions& o) {
    return Run(inst, owner, IDD_PAGENUMBERS, PageNumberProc, o);
}

bool ShowExportDialog(HINSTANCE inst, HWND owner, ExportOptions& o) {
    return Run(inst, owner, IDD_EXPORT, ExportProc, o);
}
