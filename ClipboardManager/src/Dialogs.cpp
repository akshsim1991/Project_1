// Dialogs.cpp - the Settings dialog.
#include "Dialogs.h"

#include <shellapi.h>

#include "resource.h"

namespace {
struct Ctx {
    Settings s;
    bool autostart;
    std::wstring folder;
};

int ReadInt(HWND dlg, int id, int lo, int hi, int fallback) {
    BOOL ok = FALSE;
    const UINT v = GetDlgItemInt(dlg, id, &ok, FALSE);
    if (!ok) return fallback;
    return (int)v < lo ? lo : (int)v > hi ? hi : (int)v;
}

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (Ctx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            c = (Ctx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            const Settings& s = c->s;
            CheckDlgButton(dlg, IDC_IMAGES, s.recordImages);
            CheckDlgButton(dlg, IDC_FILES, s.recordFiles);
            SetDlgItemInt(dlg, IDC_DELETE_DAYS, (UINT)s.deleteAfterDays, FALSE);
            SetDlgItemTextW(dlg, IDC_FOLDER, (L"Stored in " + c->folder).c_str());
            for (int i = 0; i < kHotkeyCount; ++i)
                SendDlgItemMessageW(dlg, IDC_HOTKEY, CB_ADDSTRING, 0, (LPARAM)HotkeyName(i));
            SendDlgItemMessageW(dlg, IDC_HOTKEY, CB_SETCURSEL, (WPARAM)s.hotkey, 0);
            CheckDlgButton(dlg, IDC_PASTE_ENTER, s.pasteOnEnter);
            for (const wchar_t* t : {L"Same as Windows", L"Light", L"Dark"})
                SendDlgItemMessageW(dlg, IDC_THEME, CB_ADDSTRING, 0, (LPARAM)t);
            SendDlgItemMessageW(dlg, IDC_THEME, CB_SETCURSEL, (WPARAM)s.themeMode, 0);
            CheckDlgButton(dlg, IDC_AUTOSTART, c->autostart);
            CheckDlgButton(dlg, IDC_STARTTRAY, s.startInTray);
            CheckDlgButton(dlg, IDC_CLOSETRAY, s.closeToTray);
            EnableWindow(GetDlgItem(dlg, IDC_STARTTRAY), c->autostart);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_AUTOSTART:
                    EnableWindow(GetDlgItem(dlg, IDC_STARTTRAY), IsDlgButtonChecked(dlg, IDC_AUTOSTART));
                    return TRUE;
                case IDC_OPEN_FOLDER:
                    ShellExecuteW(dlg, L"open", c->folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return TRUE;
                case IDOK: {
                    Settings& s = c->s;
                    const int days = ReadInt(dlg, IDC_DELETE_DAYS, 0, 3650, s.deleteAfterDays);
                    if (days > 0 && days != s.deleteAfterDays &&
                        MessageBoxW(dlg,
                                    (L"Unpinned items older than " + std::to_wstring(days) +
                                     (days == 1 ? L" day" : L" days") +
                                     L" will be deleted, now and from then on.\n\nPinned items are always kept.")
                                        .c_str(),
                                    L"Settings", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
                        SetFocus(GetDlgItem(dlg, IDC_DELETE_DAYS));
                        return TRUE;
                    }
                    s.deleteAfterDays = days;
                    s.recordImages = IsDlgButtonChecked(dlg, IDC_IMAGES) == BST_CHECKED;
                    s.recordFiles = IsDlgButtonChecked(dlg, IDC_FILES) == BST_CHECKED;
                    s.hotkey = (int)SendDlgItemMessageW(dlg, IDC_HOTKEY, CB_GETCURSEL, 0, 0);
                    s.pasteOnEnter = IsDlgButtonChecked(dlg, IDC_PASTE_ENTER) == BST_CHECKED;
                    s.themeMode = (int)SendDlgItemMessageW(dlg, IDC_THEME, CB_GETCURSEL, 0, 0);
                    c->autostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
                    s.startInTray = IsDlgButtonChecked(dlg, IDC_STARTTRAY) == BST_CHECKED;
                    s.closeToTray = IsDlgButtonChecked(dlg, IDC_CLOSETRAY) == BST_CHECKED;
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& autostart, const std::wstring& folder) {
    Ctx c{s, autostart, folder};
    if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS), owner, Proc, (LPARAM)&c) != IDOK) return false;
    s = c.s;
    autostart = c.autostart;
    return true;
}
