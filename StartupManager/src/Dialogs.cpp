// Dialogs.cpp - the program's dialogs (layouts in res/app.rc).
#include "Dialogs.h"

#include <memory>

#include <commctrl.h>
#include <objbase.h>
#include <commdlg.h>
#include <shellapi.h>

#include "Theme.h"
#include "Util.h"
#include "resource.h"

// ===========================================================================
// Shared actions
// ===========================================================================
void OpenFileLocation(HWND owner, const StartupEntry& e) {
    const std::wstring file = e.kind == EntryKind::StartupFolder && !FileExists(e.program) ? e.filePath : e.program;
    if (!FileExists(file)) {
        MessageBoxW(owner, L"The program file does not exist.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    const std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(owner, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void ShowWhereSet(HWND owner, const StartupEntry& e) {
    switch (e.kind) {
        case EntryKind::Run:
        case EntryKind::RunOnce: {
            // Registry Editor opens at the key it remembers last.
            const std::wstring full = std::wstring(L"Computer\\") +
                                      (e.scope == Scope::User ? L"HKEY_CURRENT_USER\\" : L"HKEY_LOCAL_MACHINE\\") +
                                      (e.scope == Scope::Machine32 ? L"SOFTWARE\\WOW6432Node" + e.keyPath.substr(8)
                                                                   : e.keyPath);
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit", 0,
                                nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
                RegSetValueExW(key, L"LastKey", 0, REG_SZ, (const BYTE*)full.c_str(),
                               (DWORD)((full.size() + 1) * sizeof(wchar_t)));
                RegCloseKey(key);
            }
            ShellExecuteW(owner, L"open", L"regedit.exe", L"/m", nullptr, SW_SHOWNORMAL);
            break;
        }
        case EntryKind::StartupFolder: {
            const std::wstring args = L"/select,\"" + e.filePath + L"\"";
            ShellExecuteW(owner, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case EntryKind::Task:
            ShellExecuteW(owner, L"open", L"taskschd.msc", nullptr, nullptr, SW_SHOWNORMAL);
            break;
    }
}

void CheckOnVirusTotal(HWND owner, const StartupEntry& e) {
    if (!FileExists(e.program)) {
        MessageBoxW(owner, L"The program file does not exist.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const std::wstring hash = FileInfo::Sha256(e.program);
    SetCursor(old);
    if (hash.empty()) {
        MessageBoxW(owner, L"The file could not be read.", APP_NAME, MB_ICONWARNING);
        return;
    }
    // Only the hash is sent, never the file.
    const std::wstring url = L"https://www.virustotal.com/gui/file/" + hash;
    ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SearchEntryOnline(HWND owner, const StartupEntry& e) {
    const size_t slash = e.program.find_last_of(L'\\');
    const std::wstring file = slash == std::wstring::npos ? e.program : e.program.substr(slash + 1);
    const std::wstring query = L"\"" + e.name + L"\" " + file + L" startup";
    std::wstring url = L"https://www.bing.com/search?q=";
    for (wchar_t c : query) {
        if (iswalnum(c) || c == L'-' || c == L'_' || c == L'.') {
            url += c;
            continue;
        }
        char utf8[8];
        const int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), nullptr, nullptr);
        for (int i = 0; i < n; ++i) {
            wchar_t hex[4];
            swprintf_s(hex, L"%%%02X", (unsigned char)utf8[i]);
            url += hex;
        }
    }
    ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ===========================================================================
// Settings
// ===========================================================================
namespace {
struct SettingsState {
    Settings* s;
    bool resetColumns = false;
};

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            const Settings& s = *((SettingsState*)lp)->s;
            HWND theme = GetDlgItem(dlg, IDC_THEME);
            for (const wchar_t* t : {L"Same as Windows", L"Light", L"Dark"}) SendMessageW(theme, CB_ADDSTRING, 0, (LPARAM)t);
            SendMessageW(theme, CB_SETCURSEL, (WPARAM)s.themeMode, 0);
            HWND refresh = GetDlgItem(dlg, IDC_REFRESH);
            int sel = 0;
            for (int i = 0; i < kRefreshChoiceCount; ++i) {
                const int v = kRefreshChoices[i];
                const std::wstring text = v == 0 ? L"Never (press F5 to refresh)"
                                          : v < 60 ? std::to_wstring(v) + L" seconds"
                                                   : L"1 minute";
                SendMessageW(refresh, CB_ADDSTRING, 0, (LPARAM)text.c_str());
                if (v == s.refreshSeconds) sel = i;
            }
            SendMessageW(refresh, CB_SETCURSEL, (WPARAM)sel, 0);
            CheckDlgButton(dlg, IDC_CONFIRM, s.confirmDisable ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_SHOW_MS, s.showMicrosoftTasks ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_SHOW_ONCE, s.showRunOnce ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_ELEVATE, s.alwaysElevate ? BST_CHECKED : BST_UNCHECKED);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* st = (SettingsState*)GetWindowLongPtrW(dlg, DWLP_USER);
            switch (LOWORD(wp)) {
                case IDC_RESET_COLUMNS:
                    st->resetColumns = true;
                    SetDlgItemTextW(dlg, IDC_RESET_COLUMNS, L"Will be reset");
                    EnableWindow(GetDlgItem(dlg, IDC_RESET_COLUMNS), FALSE);
                    return TRUE;
                case IDOK: {
                    Settings& s = *st->s;
                    s.themeMode = (int)SendDlgItemMessageW(dlg, IDC_THEME, CB_GETCURSEL, 0, 0);
                    const int r = (int)SendDlgItemMessageW(dlg, IDC_REFRESH, CB_GETCURSEL, 0, 0);
                    s.refreshSeconds = kRefreshChoices[r >= 0 && r < kRefreshChoiceCount ? r : 2];
                    s.confirmDisable = IsDlgButtonChecked(dlg, IDC_CONFIRM) == BST_CHECKED;
                    s.showMicrosoftTasks = IsDlgButtonChecked(dlg, IDC_SHOW_MS) == BST_CHECKED;
                    s.showRunOnce = IsDlgButtonChecked(dlg, IDC_SHOW_ONCE) == BST_CHECKED;
                    s.alwaysElevate = IsDlgButtonChecked(dlg, IDC_ELEVATE) == BST_CHECKED;
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
        }
    }
    return FALSE;
}
}  // namespace

bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s, bool& resetColumns) {
    SettingsState st{&s};
    const bool ok = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS), owner, SettingsProc, (LPARAM)&st) == IDOK;
    resetColumns = ok && st.resetColumns;
    return ok;
}

// ===========================================================================
// Add
// ===========================================================================
namespace {
struct AddState {
    AddRequest* req;
    bool elevated;
};

INT_PTR CALLBACK AddProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* st = (AddState*)lp;
            CheckRadioButton(dlg, IDC_ADD_ME, IDC_ADD_ALL, IDC_ADD_ME);
            CheckRadioButton(dlg, IDC_ADD_REG, IDC_ADD_LNK, IDC_ADD_REG);
            if (!st->elevated) EnableWindow(GetDlgItem(dlg, IDC_ADD_ALL), FALSE);
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* st = (AddState*)GetWindowLongPtrW(dlg, DWLP_USER);
            switch (LOWORD(wp)) {
                case IDC_ADD_BROWSE: {
                    wchar_t file[MAX_PATH * 2] = L"";
                    OPENFILENAMEW ofn{sizeof(ofn)};
                    ofn.hwndOwner = dlg;
                    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
                    ofn.lpstrFile = file;
                    ofn.nMaxFile = MAX_PATH * 2;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;
                    if (GetOpenFileNameW(&ofn)) {
                        SetDlgItemTextW(dlg, IDC_ADD_PROGRAM, file);
                        if (GetWindowTextLengthW(GetDlgItem(dlg, IDC_ADD_NAME)) == 0) {
                            std::wstring name = FileInfo::Description(file);
                            if (name.empty()) {
                                name = file;
                                name = name.substr(name.find_last_of(L'\\') + 1);
                                if (name.size() > 4) name.resize(name.size() - 4);
                            }
                            SetDlgItemTextW(dlg, IDC_ADD_NAME, name.c_str());
                        }
                    }
                    return TRUE;
                }
                case IDOK: {
                    AddRequest& r = *st->req;
                    r.name = GetWindowString(GetDlgItem(dlg, IDC_ADD_NAME));
                    r.program = GetWindowString(GetDlgItem(dlg, IDC_ADD_PROGRAM));
                    r.args = GetWindowString(GetDlgItem(dlg, IDC_ADD_ARGS));
                    // Accept a pasted "quoted path".
                    if (r.program.size() > 1 && r.program.front() == L'"' && r.program.back() == L'"')
                        r.program = r.program.substr(1, r.program.size() - 2);
                    if (r.name.empty() || r.name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
                        MessageBoxW(dlg, L"Enter a name without \\ / : * ? \" < > |.", APP_NAME, MB_ICONWARNING);
                        return TRUE;
                    }
                    if (!FileExists(r.program)) {
                        MessageBoxW(dlg, L"Choose a program file that exists.", APP_NAME, MB_ICONWARNING);
                        return TRUE;
                    }
                    r.allUsers = IsDlgButtonChecked(dlg, IDC_ADD_ALL) == BST_CHECKED;
                    r.shortcut = IsDlgButtonChecked(dlg, IDC_ADD_LNK) == BST_CHECKED;
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
        }
    }
    return FALSE;
}
}  // namespace

bool ShowAddDialog(HINSTANCE inst, HWND owner, bool elevated, AddRequest& out) {
    AddState st{&out, elevated};
    return DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_ADD), owner, AddProc, (LPARAM)&st) == IDOK;
}

// ===========================================================================
// Restore deleted
// ===========================================================================
namespace {
struct RestoreState {
    std::vector<Entries::Backup> backups;
    std::vector<std::wstring> chosen;
};

INT_PTR CALLBACK RestoreProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* st = (RestoreState*)lp;
            HWND list = GetDlgItem(dlg, IDC_RES_LIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
            const int dpi = GetWindowDpi(dlg);
            const struct {
                const wchar_t* title;
                int width;
            } cols[] = {{L"Name", 210}, {L"Type", 200}, {L"Deleted on", 120}};
            for (int i = 0; i < 3; ++i) {
                LVCOLUMNW c{};
                c.mask = LVCF_TEXT | LVCF_WIDTH;
                c.pszText = const_cast<wchar_t*>(cols[i].title);
                c.cx = Dpi(cols[i].width, dpi);
                ListView_InsertColumn(list, i, &c);
            }
            for (size_t i = 0; i < st->backups.size(); ++i) {
                LVITEMW it{};
                it.mask = LVIF_TEXT;
                it.iItem = (int)i;
                it.pszText = st->backups[i].name.data();
                const int row = ListView_InsertItem(list, &it);
                ListView_SetItemText(list, row, 1, st->backups[i].kindText.data());
                ListView_SetItemText(list, row, 2, st->backups[i].deletedOn.data());
            }
            if (st->backups.empty())
                SetDlgItemTextW(dlg, IDC_RES_INFO, L"There are no deleted entries to restore.");
            ApplyWindowTheme(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* st = (RestoreState*)GetWindowLongPtrW(dlg, DWLP_USER);
            switch (LOWORD(wp)) {
                case IDC_RES_OPEN:
                    ShellExecuteW(dlg, L"open", Entries::BackupRoot().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return TRUE;
                case IDOK: {
                    HWND list = GetDlgItem(dlg, IDC_RES_LIST);
                    for (size_t i = 0; i < st->backups.size(); ++i)
                        if (ListView_GetCheckState(list, (int)i)) st->chosen.push_back(st->backups[i].folder);
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    st->chosen.clear();
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
            }
            break;
        }
    }
    return FALSE;
}
}  // namespace

std::vector<std::wstring> ShowRestoreDialog(HINSTANCE inst, HWND owner) {
    RestoreState st;
    st.backups = Entries::ListBackups();
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_RESTORE), owner, RestoreProc, (LPARAM)&st);
    return st.chosen;
}

// ===========================================================================
// Details
// ===========================================================================
namespace {
struct DetailsState {
    const std::vector<StartupEntry>* entries;
    bool loaded = false;
    Signature signature;
    std::wstring version;
};

struct DetailsJob {
    HWND dlg;
    std::wstring file;
};

struct DetailsFacts {
    Signature signature;
    std::wstring version;
};

DWORD WINAPI DetailsThread(LPVOID p) {
    std::unique_ptr<DetailsJob> job((DetailsJob*)p);
    auto* facts = new DetailsFacts;
    facts->signature = FileInfo::CheckSignature(job->file);
    facts->version = FileInfo::Version(job->file);
    if (!IsWindow(job->dlg) || !PostMessageW(job->dlg, WM_APP_DETAILS_READY, 0, (LPARAM)facts)) delete facts;
    return 0;
}

bool IsZero(const FILETIME& f) { return f.dwLowDateTime == 0 && f.dwHighDateTime == 0; }

std::wstring EntryReport(const StartupEntry& e, const DetailsState* st) {
    std::wstring t = e.name + L"\r\n";
    if (!e.description.empty() && e.description != e.name) t += e.description + L"\r\n";
    t += L"\r\nSTATUS\r\n";
    t += e.enabled ? L"Enabled" : L"Disabled";
    if (!e.enabled && !IsZero(e.disabledOn)) t += L" (since " + FileInfo::FormatTime(e.disabledOn) + L")";
    t += L"\r\nRuns: " + (e.kind == EntryKind::Task ? e.trigger
                          : e.kind == EntryKind::RunOnce ? std::wstring(L"once, at the next sign-in, then Windows removes it")
                                                         : std::wstring(L"at sign-in"));
    t += L"\r\n\r\nWHERE IT IS SET\r\nType: " + std::wstring(Entries::KindText(e.kind)) + L"\r\nFor: " +
         Entries::ScopeText(e.scope) + L"\r\n";
    switch (e.kind) {
        case EntryKind::Run:
        case EntryKind::RunOnce: t += L"Registry key: " + e.location + L"\r\nValue name: " + e.name + L"\r\n"; break;
        case EntryKind::StartupFolder: t += L"File: " + e.filePath + L"\r\n"; break;
        case EntryKind::Task: t += L"Task: " + e.taskPath + L"\r\n"; break;
    }
    t += L"\r\nWHAT IT RUNS\r\nCommand: " + e.command + L"\r\n";
    if (!e.program.empty())
        t += L"Program: " + e.program + (FileExists(e.program) ? L"" : L"   (MISSING)") + L"\r\n";
    if (!e.publisher.empty()) t += L"Publisher: " + e.publisher + L"\r\n";
    if (st && (e.program.empty() || !FileExists(e.program))) {
        // Nothing to check.
    } else if (st) {
        if (!st->loaded) {
            t += L"Checking the digital signature\x2026\r\n";
        } else {
            if (!st->version.empty()) t += L"Version: " + st->version + L"\r\n";
            std::wstring sig;
            switch (st->signature.state) {
                case SignState::Signed:
                    sig = L"Valid, signed by " + (st->signature.signer.empty() ? L"(unknown)" : st->signature.signer);
                    if (st->signature.catalog) sig += L" (Windows catalog)";
                    break;
                case SignState::Unsigned: sig = L"Not signed: the publisher can not be verified"; break;
                case SignState::Invalid: sig = L"INVALID or not trusted"; break;
                default: sig = L"Could not be checked"; break;
            }
            t += L"Digital signature: " + sig + L"\r\n";
        }
    }
    t += L"\r\nWARNINGS\r\n" + (e.warnings ? Entries::WarningsExplained(e.warnings) : std::wstring(L"None.\r\n"));
    return t;
}

void RefreshDetails(HWND dlg) {
    auto* st = (DetailsState*)GetWindowLongPtrW(dlg, DWLP_USER);
    std::wstring text;
    if (st->entries->size() == 1) {
        text = EntryReport((*st->entries)[0], st);
    } else {
        for (const auto& e : *st->entries) text += EntryReport(e, nullptr) + L"\r\n\x2500\x2500\x2500\r\n\r\n";
    }
    SetDlgItemTextW(dlg, IDC_DET_TEXT, text.c_str());
}

INT_PTR CALLBACK DetailsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            auto* st = (DetailsState*)lp;
            const bool single = st->entries->size() == 1;
            const std::wstring title = single ? (*st->entries)[0].name + L" \x2014 Details"
                                              : L"About the " + std::to_wstring(st->entries->size()) + L" selected entries";
            SetWindowTextW(dlg, title.c_str());
            for (int id : {IDC_DET_FILE, IDC_DET_WHERE, IDC_DET_VT, IDC_DET_SEARCH}) EnableWindow(GetDlgItem(dlg, id), single);
            if (single) {
                const StartupEntry& e = (*st->entries)[0];
                EnableWindow(GetDlgItem(dlg, IDC_DET_VT), FileExists(e.program));
                auto* job = new DetailsJob{dlg, e.program};
                HANDLE t = FileExists(e.program) ? CreateThread(nullptr, 0, DetailsThread, job, 0, nullptr) : nullptr;
                if (t) CloseHandle(t);
                else {
                    delete job;
                    st->loaded = true;
                }
            }
            RefreshDetails(dlg);
            ApplyWindowTheme(dlg);
            SetFocus(GetDlgItem(dlg, IDOK));
            return FALSE;
        }
        case WM_APP_DETAILS_READY: {
            std::unique_ptr<DetailsFacts> facts((DetailsFacts*)lp);
            auto* st = (DetailsState*)GetWindowLongPtrW(dlg, DWLP_USER);
            st->signature = facts->signature;
            st->version = facts->version;
            st->loaded = true;
            RefreshDetails(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            auto* st = (DetailsState*)GetWindowLongPtrW(dlg, DWLP_USER);
            const StartupEntry& e = (*st->entries)[0];
            switch (LOWORD(wp)) {
                case IDC_DET_FILE: OpenFileLocation(dlg, e); return TRUE;
                case IDC_DET_WHERE: ShowWhereSet(dlg, e); return TRUE;
                case IDC_DET_VT: CheckOnVirusTotal(dlg, e); return TRUE;
                case IDC_DET_SEARCH: SearchEntryOnline(dlg, e); return TRUE;
                case IDC_DET_COPY: CopyToClipboard(dlg, GetWindowString(GetDlgItem(dlg, IDC_DET_TEXT))); return TRUE;
                case IDOK:
                case IDCANCEL: EndDialog(dlg, IDOK); return TRUE;
            }
            break;
        }
    }
    return FALSE;
}
}  // namespace

void ShowDetailsDialog(HINSTANCE inst, HWND owner, const std::vector<StartupEntry>& entries) {
    if (entries.empty()) return;
    DetailsState st{&entries};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_DETAILS), owner, DetailsProc, (LPARAM)&st);
}
