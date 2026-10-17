// Dialogs.cpp - Settings dialog and sounds.
#include "Dialogs.h"

#include <commdlg.h>
#include <mmsystem.h>

#include "Util.h"
#include "resource.h"

namespace {
const int kRefreshChoices[] = {1, 2, 5, 10, 30};

struct Ctx {
    Settings s;
    bool autostart;
};

int ReadInt(HWND dlg, int id, int lo, int hi, int fallback) {
    BOOL ok = FALSE;
    const UINT v = GetDlgItemInt(dlg, id, &ok, FALSE);
    if (!ok) return fallback;
    return (int)v < lo ? lo : (int)v > hi ? hi : (int)v;
}

void UpdateEnabled(HWND dlg) {
    EnableWindow(GetDlgItem(dlg, IDC_LOW_PCT), IsDlgButtonChecked(dlg, IDC_LOW_ON));
    EnableWindow(GetDlgItem(dlg, IDC_CRIT_PCT), IsDlgButtonChecked(dlg, IDC_CRIT_ON));
    EnableWindow(GetDlgItem(dlg, IDC_FULL_PCT), IsDlgButtonChecked(dlg, IDC_FULL_ON));
    EnableWindow(GetDlgItem(dlg, IDC_STARTTRAY), IsDlgButtonChecked(dlg, IDC_AUTOSTART));
    const int sound = (int)SendDlgItemMessageW(dlg, IDC_SOUND, CB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(dlg, IDC_SOUND_BROWSE), sound == kSoundFile);
    EnableWindow(GetDlgItem(dlg, IDC_SOUND_TEST), sound != kSoundNone);
}

bool PickSound(HWND owner, std::wstring& file) {
    wchar_t buf[MAX_PATH * 2];
    wcsncpy_s(buf, file.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Sounds (*.wav)\0*.wav\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    ofn.lpstrInitialDir = L"C:\\Windows\\Media";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    file = buf;
    return true;
}

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = (Ctx*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG: {
            c = (Ctx*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)c);
            const Settings& s = c->s;
            CheckDlgButton(dlg, IDC_LOW_ON, s.lowAlert);
            SetDlgItemInt(dlg, IDC_LOW_PCT, (UINT)s.lowPercent, FALSE);
            CheckDlgButton(dlg, IDC_CRIT_ON, s.criticalAlert);
            SetDlgItemInt(dlg, IDC_CRIT_PCT, (UINT)s.criticalPercent, FALSE);
            CheckDlgButton(dlg, IDC_FULL_ON, s.fullAlert);
            SetDlgItemInt(dlg, IDC_FULL_PCT, (UINT)s.fullPercent, FALSE);
            CheckDlgButton(dlg, IDC_PLUG_ON, s.plugAlert);
            SetDlgItemInt(dlg, IDC_REPEAT, (UINT)s.repeatMinutes, FALSE);
            CheckDlgButton(dlg, IDC_MSGBOX, s.messageBox);
            for (const wchar_t* t : {L"No sound", L"Windows notification", L"Windows alarm", L"A sound file\x2026"})
                SendDlgItemMessageW(dlg, IDC_SOUND, CB_ADDSTRING, 0, (LPARAM)t);
            SendDlgItemMessageW(dlg, IDC_SOUND, CB_SETCURSEL, (WPARAM)s.sound, 0);
            SetDlgItemTextW(dlg, IDC_SOUND_FILE, s.sound == kSoundFile ? s.soundFile.c_str() : L"");
            for (const wchar_t* t : {L"Same as Windows", L"Light", L"Dark"})
                SendDlgItemMessageW(dlg, IDC_THEME, CB_ADDSTRING, 0, (LPARAM)t);
            SendDlgItemMessageW(dlg, IDC_THEME, CB_SETCURSEL, (WPARAM)s.themeMode, 0);
            int sel = 2;
            for (int i = 0; i < (int)(sizeof(kRefreshChoices) / sizeof(kRefreshChoices[0])); ++i) {
                const std::wstring t = std::to_wstring(kRefreshChoices[i]) + (kRefreshChoices[i] == 1 ? L" second" : L" seconds");
                SendDlgItemMessageW(dlg, IDC_REFRESH, CB_ADDSTRING, 0, (LPARAM)t.c_str());
                if (kRefreshChoices[i] == s.refreshSeconds) sel = i;
            }
            SendDlgItemMessageW(dlg, IDC_REFRESH, CB_SETCURSEL, (WPARAM)sel, 0);
            for (const wchar_t* t : {L"A battery picture", L"The percentage"})
                SendDlgItemMessageW(dlg, IDC_TRAYSTYLE, CB_ADDSTRING, 0, (LPARAM)t);
            SendDlgItemMessageW(dlg, IDC_TRAYSTYLE, CB_SETCURSEL, (WPARAM)s.trayStyle, 0);
            CheckDlgButton(dlg, IDC_AUTOSTART, c->autostart);
            CheckDlgButton(dlg, IDC_STARTTRAY, s.startInTray);
            CheckDlgButton(dlg, IDC_CLOSETRAY, s.closeToTray);
            SetDlgItemInt(dlg, IDC_KEEPDAYS, (UINT)s.keepDays, FALSE);
            UpdateEnabled(dlg);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_LOW_ON:
                case IDC_CRIT_ON:
                case IDC_FULL_ON:
                case IDC_AUTOSTART: UpdateEnabled(dlg); return TRUE;
                case IDC_SOUND:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        const int sound = (int)SendDlgItemMessageW(dlg, IDC_SOUND, CB_GETCURSEL, 0, 0);
                        if (sound == kSoundFile && c->s.soundFile.empty() && !PickSound(dlg, c->s.soundFile))
                            SendDlgItemMessageW(dlg, IDC_SOUND, CB_SETCURSEL, (WPARAM)kSoundNotification, 0);
                        SetDlgItemTextW(dlg, IDC_SOUND_FILE,
                                        SendDlgItemMessageW(dlg, IDC_SOUND, CB_GETCURSEL, 0, 0) == kSoundFile
                                            ? c->s.soundFile.c_str()
                                            : L"");
                        UpdateEnabled(dlg);
                    }
                    return TRUE;
                case IDC_SOUND_BROWSE:
                    if (PickSound(dlg, c->s.soundFile)) SetDlgItemTextW(dlg, IDC_SOUND_FILE, c->s.soundFile.c_str());
                    return TRUE;
                case IDC_SOUND_TEST:
                    PlayAlertSound((int)SendDlgItemMessageW(dlg, IDC_SOUND, CB_GETCURSEL, 0, 0), c->s.soundFile, false);
                    return TRUE;
                case IDOK: {
                    Settings& s = c->s;
                    s.lowAlert = IsDlgButtonChecked(dlg, IDC_LOW_ON) == BST_CHECKED;
                    s.lowPercent = ReadInt(dlg, IDC_LOW_PCT, 1, 99, s.lowPercent);
                    s.criticalAlert = IsDlgButtonChecked(dlg, IDC_CRIT_ON) == BST_CHECKED;
                    s.criticalPercent = ReadInt(dlg, IDC_CRIT_PCT, 1, 99, s.criticalPercent);
                    s.fullAlert = IsDlgButtonChecked(dlg, IDC_FULL_ON) == BST_CHECKED;
                    s.fullPercent = ReadInt(dlg, IDC_FULL_PCT, 50, 100, s.fullPercent);
                    if (s.criticalAlert && s.lowAlert && s.criticalPercent >= s.lowPercent) {
                        MessageBoxW(dlg, L"The critical level must be below the low level.", APP_NAME, MB_ICONINFORMATION);
                        SetFocus(GetDlgItem(dlg, IDC_CRIT_PCT));
                        return TRUE;
                    }
                    s.plugAlert = IsDlgButtonChecked(dlg, IDC_PLUG_ON) == BST_CHECKED;
                    s.repeatMinutes = ReadInt(dlg, IDC_REPEAT, 0, 120, s.repeatMinutes);
                    s.messageBox = IsDlgButtonChecked(dlg, IDC_MSGBOX) == BST_CHECKED;
                    s.sound = (int)SendDlgItemMessageW(dlg, IDC_SOUND, CB_GETCURSEL, 0, 0);
                    s.themeMode = (int)SendDlgItemMessageW(dlg, IDC_THEME, CB_GETCURSEL, 0, 0);
                    s.refreshSeconds = kRefreshChoices[SendDlgItemMessageW(dlg, IDC_REFRESH, CB_GETCURSEL, 0, 0)];
                    s.trayStyle = (int)SendDlgItemMessageW(dlg, IDC_TRAYSTYLE, CB_GETCURSEL, 0, 0);
                    c->autostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
                    s.startInTray = IsDlgButtonChecked(dlg, IDC_STARTTRAY) == BST_CHECKED;
                    s.closeToTray = IsDlgButtonChecked(dlg, IDC_CLOSETRAY) == BST_CHECKED;
                    s.keepDays = ReadInt(dlg, IDC_KEEPDAYS, 1, 365, s.keepDays);
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

bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& autostart) {
    Ctx c{s, autostart};
    if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS), owner, Proc, (LPARAM)&c) != IDOK) return false;
    s = c.s;
    autostart = c.autostart;
    return true;
}

void PlayAlertSound(int mode, const std::wstring& file, bool urgent) {
    switch (mode) {
        case kSoundNotification:
            if (!PlaySoundW(urgent ? L"SystemExclamation" : L"Notification.Default", nullptr,
                            SND_ALIAS | SND_ASYNC | SND_NODEFAULT))
                MessageBeep(urgent ? MB_ICONWARNING : MB_ICONINFORMATION);
            break;
        case kSoundAlarm:
            if (!PlaySoundW(L"Notification.Looping.Alarm", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT) &&
                !PlaySoundW(L"SystemHand", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT))
                MessageBeep(MB_ICONERROR);
            break;
        case kSoundFile:
            if (file.empty() || !PlaySoundW(file.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
                MessageBeep(MB_ICONWARNING);
            break;
        default: break;
    }
}
