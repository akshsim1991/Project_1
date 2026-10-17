// Dialogs.cpp - the Settings dialog (layout in res/app.rc).
#include "Dialogs.h"

#include <commctrl.h>

#include "Profiles.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
struct State {
    Settings* settings;
    bool resetColumns = false;
};

void AddItem(HWND combo, const wchar_t* text) {
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            const Settings& s = *((State*)lp)->settings;
            HWND theme = GetDlgItem(dlg, IDC_THEME);
            AddItem(theme, L"Same as Windows");
            AddItem(theme, L"Light");
            AddItem(theme, L"Dark");
            SendMessageW(theme, CB_SETCURSEL, (WPARAM)s.themeMode, 0);

            HWND refresh = GetDlgItem(dlg, IDC_REFRESH);
            int sel = 0;
            for (int i = 0; i < kRefreshChoiceCount; ++i) {
                const int v = kRefreshChoices[i];
                const std::wstring text = v == 0    ? L"Never (press F5 to refresh)"
                                          : v < 60  ? std::to_wstring(v) + L" seconds"
                                                    : std::to_wstring(v / 60) + L" minute";
                AddItem(refresh, text.c_str());
                if (v == s.refreshSeconds) sel = i;
            }
            SendMessageW(refresh, CB_SETCURSEL, (WPARAM)sel, 0);

            SetDlgItemInt(dlg, IDC_TIMEOUT, (UINT)s.timeoutSeconds, FALSE);
            SendDlgItemMessageW(dlg, IDC_TIMEOUT, EM_SETLIMITTEXT, 3, 0);
            CheckDlgButton(dlg, IDC_CONFIRM, s.confirmActions ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_PROTECT, s.protectCritical ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_ELEVATE, s.alwaysElevate ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_ONLINE, s.onlineAdvice ? BST_CHECKED : BST_UNCHECKED);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* st = (State*)GetWindowLongPtrW(dlg, DWLP_USER);
            switch (LOWORD(wp)) {
                case IDC_RESET_COLUMNS:
                    st->resetColumns = true;
                    SetDlgItemTextW(dlg, IDC_RESET_COLUMNS, L"Columns will be reset");
                    EnableWindow(GetDlgItem(dlg, IDC_RESET_COLUMNS), FALSE);
                    return TRUE;
                case IDOK: {
                    Settings& s = *st->settings;
                    BOOL ok = FALSE;
                    const UINT timeout = GetDlgItemInt(dlg, IDC_TIMEOUT, &ok, FALSE);
                    if (!ok || timeout < 5 || timeout > 600) {
                        MessageBoxW(dlg, L"Enter a waiting time between 5 and 600 seconds.", APP_NAME,
                                    MB_ICONWARNING);
                        SetFocus(GetDlgItem(dlg, IDC_TIMEOUT));
                        return TRUE;
                    }
                    const bool protect = IsDlgButtonChecked(dlg, IDC_PROTECT) == BST_CHECKED;
                    if (!protect && s.protectCritical &&
                        MessageBoxW(dlg,
                                    L"Without protection you can stop, kill or disable services "
                                    L"that Windows needs. Doing so can make the PC unstable or "
                                    L"stop it from starting.\n\nTurn protection off?",
                                    APP_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                        return TRUE;
                    s.themeMode = (int)SendDlgItemMessageW(dlg, IDC_THEME, CB_GETCURSEL, 0, 0);
                    const int r = (int)SendDlgItemMessageW(dlg, IDC_REFRESH, CB_GETCURSEL, 0, 0);
                    s.refreshSeconds = kRefreshChoices[r >= 0 && r < kRefreshChoiceCount ? r : 2];
                    s.timeoutSeconds = (int)timeout;
                    s.confirmActions = IsDlgButtonChecked(dlg, IDC_CONFIRM) == BST_CHECKED;
                    s.protectCritical = protect;
                    s.alwaysElevate = IsDlgButtonChecked(dlg, IDC_ELEVATE) == BST_CHECKED;
                    s.onlineAdvice = IsDlgButtonChecked(dlg, IDC_ONLINE) == BST_CHECKED;
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
            }
            break;
        }
    }
    return FALSE;
}
}  // namespace

bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& resetColumns) {
    State st{&s};
    const bool ok = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsProc,
                                    (LPARAM)&st) == IDOK;
    resetColumns = ok && st.resetColumns;
    return ok;
}

// ===========================================================================
// Review changes
// ===========================================================================
namespace {
struct ChangesState {
    std::wstring title, intro;
    std::vector<PlannedChange>* changes;
};

INT_PTR CALLBACK ChangesProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* st = (ChangesState*)lp;
            SetWindowTextW(dlg, st->title.c_str());
            SetDlgItemTextW(dlg, IDC_CHG_INTRO, st->intro.c_str());
            HWND list = GetDlgItem(dlg, IDC_CHG_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
            const int dpi = GetWindowDpi(dlg);
            const struct {
                const wchar_t* title;
                int width;
            } cols[] = {{L"Service", 200}, {L"Now", 120}, {L"Change to", 120}, {L"Note", 150}};
            for (int i = 0; i < 4; ++i) {
                LVCOLUMNW c{};
                c.mask = LVCF_TEXT | LVCF_WIDTH;
                c.pszText = const_cast<wchar_t*>(cols[i].title);
                c.cx = Dpi(cols[i].width, dpi);
                ListView_InsertColumn(list, i, &c);
            }
            for (size_t i = 0; i < st->changes->size(); ++i) {
                const PlannedChange& ch = (*st->changes)[i];
                LVITEMW it{};
                it.mask = LVIF_TEXT | LVIF_PARAM;
                it.iItem = (int)i;
                it.lParam = (LPARAM)i;
                std::wstring name = ch.displayName + L" (" + ch.name + L")";
                it.pszText = name.data();
                const int row = ListView_InsertItem(list, &it);
                ListView_SetItemText(list, row, 1, const_cast<wchar_t*>(Profiles::ModeTitle(ch.from)));
                ListView_SetItemText(list, row, 2, const_cast<wchar_t*>(Profiles::ModeTitle(ch.to)));
                const wchar_t* note = ch.blocked ? L"Critical: protected, skipped"
                                      : ch.critical ? L"\x26A0 Critical to Windows"
                                                    : L"";
                ListView_SetItemText(list, row, 3, const_cast<wchar_t*>(note));
                ListView_SetCheckState(list, row, ch.apply && !ch.blocked);
            }
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_NOTIFY: {
            // Protected changes can not be ticked.
            auto* hdr = (NMHDR*)lp;
            if (hdr->idFrom == IDC_CHG_LIST && hdr->code == LVN_ITEMCHANGING) {
                auto* nm = (NMLISTVIEW*)lp;
                auto* st = (ChangesState*)GetWindowLongPtrW(dlg, DWLP_USER);
                if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2)) &&
                    nm->iItem >= 0 && (size_t)nm->iItem < st->changes->size() && (*st->changes)[(size_t)nm->iItem].blocked) {
                    SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                auto* st = (ChangesState*)GetWindowLongPtrW(dlg, DWLP_USER);
                HWND list = GetDlgItem(dlg, IDC_CHG_LIST);
                int ticked = 0;
                for (size_t i = 0; i < st->changes->size(); ++i) {
                    PlannedChange& ch = (*st->changes)[i];
                    ch.apply = !ch.blocked && ListView_GetCheckState(list, (int)i);
                    if (ch.apply) ++ticked;
                }
                if (!ticked) {
                    MessageBoxW(dlg, L"Nothing is ticked, so there is nothing to change.", APP_NAME, MB_ICONINFORMATION);
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
}  // namespace

bool ShowChangesDialog(HINSTANCE inst, HWND owner, const std::wstring& title, const std::wstring& intro,
                       std::vector<PlannedChange>& changes) {
    ChangesState st{title, intro, &changes};
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_CHANGES), owner, ChangesProc, (LPARAM)&st) == IDOK;
}
