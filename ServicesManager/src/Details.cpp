// Details.cpp - service details property sheet and multi-service summary.
#include "Details.h"

#include <algorithm>
#include <memory>
#include <set>

#include <commctrl.h>
#include <prsht.h>
#include <shellapi.h>

#include "Advice.h"
#include "Profiles.h"
#include "Theme.h"
#include "Util.h"
#include "resource.h"

namespace {
DetailsContext* Ctx(HWND page) { return (DetailsContext*)GetWindowLongPtrW(page, DWLP_USER); }

void SetResult(HWND dlg, LONG_PTR r) { SetWindowLongPtrW(dlg, DWLP_MSGRESULT, r); }

const ServiceInfo* Find(const std::vector<ServiceInfo>& all, const std::wstring& name) {
    for (const auto& s : all)
        if (_wcsicmp(s.name.c_str(), name.c_str()) == 0) return &s;
    return nullptr;
}

// Services that list `name` among their dependencies (direct only).
std::vector<const ServiceInfo*> DirectDependents(const std::vector<ServiceInfo>& all, const std::wstring& name) {
    std::vector<const ServiceInfo*> out;
    for (const auto& s : all)
        for (const auto& d : s.dependencies)
            if (_wcsicmp(d.c_str(), name.c_str()) == 0) {
                out.push_back(&s);
                break;
            }
    return out;
}

std::wstring DepName(const std::vector<ServiceInfo>& all, const std::wstring& dep) {
    if (!dep.empty() && dep[0] == SC_GROUP_IDENTIFIERW) return L"Group \x201C" + dep.substr(1) + L"\x201D";
    const ServiceInfo* s = Find(all, dep);
    return s ? s->displayName : dep;
}

std::wstring Join(const std::vector<std::wstring>& items, const wchar_t* sep = L", ") {
    std::wstring out;
    for (const auto& i : items) out += (out.empty() ? L"" : sep) + i;
    return out;
}

std::wstring RecoverySummary(const Inspect::RecoveryConfig& rc) {
    if (!rc.valid) return L"Not available.\r\n";
    std::wstring t;
    const wchar_t* labels[3] = {L"First failure", L"Second failure", L"Later failures"};
    for (int i = 0; i < 3; ++i) {
        t += std::wstring(labels[i]) + L": " + Inspect::RecoveryText(rc.action[i]);
        if (rc.action[i] != Inspect::Recovery::None && rc.delayMs[i])
            t += L" after " + Inspect::FormatDuration(rc.delayMs[i] / 1000);
        t += L"\r\n";
    }
    return t;
}

// ===========================================================================
// General
// ===========================================================================
std::wstring GeneralText(const DetailsContext& c) {
    const ServiceInfo& s = c.service;
    std::wstring t = s.displayName + L"\r\n";
    if (!s.description.empty()) t += s.description + L"\r\n";

    t += L"\r\nCAN IT BE DISABLED?\r\n" + Advice::Describe(s.name) + L"\r\n";
    if (s.safety == Safety::Critical)
        t += L"This app treats it as critical to Windows, so it is protected from being stopped or "
             L"disabled.\r\n";

    t += L"\r\nSTATUS\r\nStatus: " + Svc::StateText(s.state);
    if (s.pid) t += L"     Process ID: " + std::to_wstring(s.pid);
    t += L"\r\n";
    if (s.pid) {
        if (!c.factsLoaded)
            t += L"Reading process details\x2026\r\n";
        else if (c.stats.valid)
            t += L"Running for: " + Inspect::FormatDuration(c.stats.uptimeSec) +
                 L"     Memory: " + Inspect::FormatBytes(c.stats.workingSet) + L" (private " +
                 Inspect::FormatBytes(c.stats.privateBytes) + L")     CPU time: " +
                 Inspect::FormatDuration(c.stats.cpuMs / 1000) + L"\r\n";
        else
            t += L"Process details are not available (protected process, or administrator rights needed).\r\n";
        std::vector<std::wstring> shared;
        for (const auto& o : c.all)
            if (o.pid == s.pid && o.name != s.name) shared.push_back(o.displayName);
        if (!shared.empty())
            t += L"Shares its process with " + std::to_wstring(shared.size()) + L" other service(s): " +
                 Join(shared) + L"\r\n";
    }

    t += L"\r\nSTART-UP\r\nStart type: " + Svc::StartTypeText(s) + L"\r\n";
    if (s.bootDelayMs)
        t += L"Windows recorded that it slowed down start-up by up to " +
             std::to_wstring(s.bootDelayMs / 1000) + L"." + std::to_wstring(s.bootDelayMs % 1000 / 100) +
             L" s (" + std::to_wstring(s.bootDelayCount) + L" time(s)).\r\n";

    t += L"\r\nPROGRAM\r\nCommand line: " + (s.binaryPath.empty() ? std::wstring(L"(unknown)") : s.binaryPath) +
         L"\r\n";
    if (!s.imagePath.empty())
        t += L"File: " + s.imagePath + (FileExists(s.imagePath) ? L"" : L"   (MISSING)") + L"\r\n";
    if (c.factsLoaded) {
        if (!c.version.empty()) t += L"Version: " + c.version + L"\r\n";
        if (!s.company.empty()) t += L"Company: " + s.company + L"\r\n";
        std::wstring sig;
        switch (c.signature.state) {
            case SignState::Signed:
                sig = L"Valid, signed by " + (c.signature.signer.empty() ? L"(unknown)" : c.signature.signer);
                if (c.signature.catalog) sig += L" (Windows catalog)";
                break;
            case SignState::Unsigned: sig = L"Not signed: the publisher cannot be verified"; break;
            case SignState::Invalid:
                sig = L"INVALID or not trusted" +
                      (c.signature.signer.empty() ? std::wstring() : L" (claims " + c.signature.signer + L")");
                break;
            default: sig = L"Could not be checked"; break;
        }
        t += L"Digital signature: " + sig + L"\r\n";
    } else {
        t += L"Checking the digital signature\x2026\r\n";
    }
    t += L"Log on as: " + (s.account.empty() ? std::wstring(L"(unknown)") : s.account) + L"\r\n";
    t += L"Service name: " + s.name + L"     Runs in: " +
         ((s.type & SERVICE_WIN32_SHARE_PROCESS) ? L"a shared process" : L"its own process") + L"\r\n";

    t += L"\r\nWARNINGS\r\n" + (s.warnings ? Svc::WarningsExplained(s.warnings) : std::wstring(L"None.\r\n"));

    t += L"\r\nRECOVERY (what Windows does if it fails)\r\n" + RecoverySummary(c.recovery);

    std::vector<std::wstring> needs, neededBy;
    for (const auto& d : s.dependencies) needs.push_back(DepName(c.all, d));
    for (const ServiceInfo* d : DirectDependents(c.all, s.name)) neededBy.push_back(d->displayName);
    t += L"\r\nDEPENDENCIES\r\nNeeds: " + (needs.empty() ? L"nothing" : Join(needs)) +
         L"\r\nNeeded by: " + (neededBy.empty() ? L"nothing" : Join(neededBy)) + L"\r\n";
    return t;
}

struct Facts {
    Signature signature;
    Inspect::ProcessStats stats;
    std::wstring version;
};

struct FactsJob {
    HWND page;
    std::wstring file;
    DWORD pid;
};

DWORD WINAPI FactsThread(LPVOID p) {
    std::unique_ptr<FactsJob> job((FactsJob*)p);
    auto* facts = new Facts;
    facts->signature = Inspect::CheckSignature(job->file);
    facts->version = Inspect::FileVersion(job->file);
    facts->stats = Inspect::QueryProcess(job->pid);
    if (!IsWindow(job->page) || !PostMessageW(job->page, WM_APP_DETAILS_READY, 0, (LPARAM)facts)) delete facts;
    return 0;
}

void RefreshGeneral(HWND dlg) {
    HWND edit = GetDlgItem(dlg, IDC_GEN_TEXT);
    // Keep the reader's scroll position when the text is updated.
    const int first = (int)SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SetWindowTextW(edit, GeneralText(*Ctx(dlg)).c_str());
    SendMessageW(edit, EM_SETSEL, 0, 0);  // no selection
    SendMessageW(edit, WM_VSCROLL, SB_TOP, 0);       // to the top...
    SendMessageW(edit, EM_LINESCROLL, 0, first);     // ...then where the reader was
}

INT_PTR CALLBACK GeneralProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* ctx = (DetailsContext*)((PROPSHEETPAGEW*)lp)->lParam;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)ctx);
            ctx->recovery = Inspect::ReadRecovery(ctx->service.name);
            RefreshGeneral(dlg);
            EnableWindow(GetDlgItem(dlg, IDC_GEN_OPEN), FileExists(ctx->service.imagePath));
            EnableWindow(GetDlgItem(dlg, IDC_GEN_VT), FileExists(ctx->service.imagePath));
            auto* job = new FactsJob{dlg, ctx->service.imagePath, ctx->service.pid};
            HANDLE t = CreateThread(nullptr, 0, FactsThread, job, 0, nullptr);
            if (t) CloseHandle(t);
            else delete job;
            Advice::Fetch(dlg, WM_APP_ADVICE_READY);
            return TRUE;
        }
        case WM_APP_DETAILS_READY: {
            std::unique_ptr<Facts> facts((Facts*)lp);
            DetailsContext* ctx = Ctx(dlg);
            ctx->signature = facts->signature;
            ctx->stats = facts->stats;
            ctx->version = facts->version;
            ctx->factsLoaded = true;
            RefreshGeneral(dlg);
            return TRUE;
        }
        case WM_APP_ADVICE_READY:
            RefreshGeneral(dlg);
            return TRUE;
        case WM_COMMAND: {
            DetailsContext* ctx = Ctx(dlg);
            switch (LOWORD(wp)) {
                case IDC_GEN_OPEN: {
                    const std::wstring args = L"/select,\"" + ctx->service.imagePath + L"\"";
                    ShellExecuteW(dlg, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                    return TRUE;
                }
                case IDC_GEN_VT: OpenVirusTotal(dlg, ctx->service.imagePath); return TRUE;
                case IDC_GEN_SEARCH: SearchServiceOnline(dlg, ctx->service); return TRUE;
                case IDC_GEN_COPY: CopyToClipboard(dlg, GetWindowString(GetDlgItem(dlg, IDC_GEN_TEXT))); return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// ===========================================================================
// Configuration
// ===========================================================================
struct ConfigState {
    bool loading = true;
    int startIndex = -1;  // original selection (-1: boot/system type, not editable)
    int accountRadio = IDC_CFG_SYSTEM;
    std::wstring user;
};
ConfigState g_config;  // one details window at a time (it is modal)

int StartIndex(const ServiceInfo& s) {
    StartMode m;
    if (!Profiles::ModeOf(s, m)) return -1;
    return (int)m;  // StartMode order matches the combo
}

void UpdateAccountFields(HWND dlg) {
    const bool user = IsDlgButtonChecked(dlg, IDC_CFG_ACCOUNT) == BST_CHECKED && Ctx(dlg)->elevated;
    for (int id : {IDC_CFG_USER, IDC_CFG_PASS, IDC_CFG_PASS2}) EnableWindow(GetDlgItem(dlg, id), user);
}

INT_PTR CALLBACK ConfigProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* ctx = (DetailsContext*)((PROPSHEETPAGEW*)lp)->lParam;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)ctx);
            const ServiceInfo& s = ctx->service;
            g_config = ConfigState{};
            SetDlgItemTextW(dlg, IDC_CFG_NAME, s.displayName.c_str());
            std::wstring desc = s.description;
            SetDlgItemTextW(dlg, IDC_CFG_DESC, desc.c_str());
            HWND combo = GetDlgItem(dlg, IDC_CFG_START);
            for (StartMode m : {StartMode::Automatic, StartMode::AutomaticDelayed, StartMode::Manual, StartMode::Disabled})
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)Profiles::ModeTitle(m));
            g_config.startIndex = StartIndex(s);
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)g_config.startIndex, 0);
            if (g_config.startIndex < 0) EnableWindow(combo, FALSE);
            int radio = IDC_CFG_ACCOUNT;
            if (s.account == L"Local System") radio = IDC_CFG_SYSTEM;
            else if (s.account == L"Local Service") radio = IDC_CFG_LOCALSVC;
            else if (s.account == L"Network Service") radio = IDC_CFG_NETSVC;
            else g_config.user = s.account;
            g_config.accountRadio = radio;
            CheckRadioButton(dlg, IDC_CFG_SYSTEM, IDC_CFG_ACCOUNT, radio);
            SetDlgItemTextW(dlg, IDC_CFG_USER, g_config.user.c_str());
            std::wstring note = L"Account and name changes take effect the next time the service starts.";
            if (!ctx->elevated) {
                note = L"Restart as administrator to change these settings.";
                for (int id : {IDC_CFG_NAME, IDC_CFG_DESC, IDC_CFG_START, IDC_CFG_SYSTEM, IDC_CFG_LOCALSVC,
                               IDC_CFG_NETSVC, IDC_CFG_ACCOUNT})
                    EnableWindow(GetDlgItem(dlg, id), FALSE);
            } else if (s.safety == Safety::Critical) {
                note += L"\r\nThis service is critical to Windows: change it only if you know exactly why.";
            }
            SetDlgItemTextW(dlg, IDC_CFG_NOTE, note.c_str());
            UpdateAccountFields(dlg);
            g_config.loading = false;
            return TRUE;
        }
        case WM_COMMAND:
            if (g_config.loading) break;
            if (LOWORD(wp) >= IDC_CFG_SYSTEM && LOWORD(wp) <= IDC_CFG_ACCOUNT) UpdateAccountFields(dlg);
            if (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED)
                PropSheet_Changed(GetParent(dlg), dlg);
            break;
        case WM_NOTIFY:
            if (((NMHDR*)lp)->code == PSN_APPLY) {
                DetailsContext* ctx = Ctx(dlg);
                const ServiceInfo& s = ctx->service;
                if (!ctx->elevated) {
                    SetResult(dlg, PSNRET_NOERROR);
                    return TRUE;
                }
                Inspect::ConfigChange c;
                const std::wstring name = GetWindowString(GetDlgItem(dlg, IDC_CFG_NAME));
                if (!name.empty() && name != s.displayName) c.displayName = name;
                const std::wstring desc = GetWindowString(GetDlgItem(dlg, IDC_CFG_DESC));
                if (desc != s.description) {
                    c.setDescription = true;
                    c.description = desc;
                }
                const int start = (int)SendDlgItemMessageW(dlg, IDC_CFG_START, CB_GETCURSEL, 0, 0);
                if (g_config.startIndex >= 0 && start >= 0 && start != g_config.startIndex) {
                    const StartMode m = (StartMode)start;
                    if (s.safety == Safety::Critical && ctx->protectCritical &&
                        (m == StartMode::Manual || m == StartMode::Disabled)) {
                        MessageBoxW(dlg,
                                    L"This service is critical to Windows, so it can not be set to Manual or "
                                    L"Disabled while \x201CProtect critical services\x201D is on in Settings.",
                                    APP_NAME, MB_ICONWARNING);
                        SetResult(dlg, PSNRET_INVALID_NOCHANGEPAGE);
                        return TRUE;
                    }
                    c.setStartType = true;
                    c.startType = m == StartMode::Disabled ? SERVICE_DISABLED
                                  : m == StartMode::Manual ? SERVICE_DEMAND_START
                                                           : SERVICE_AUTO_START;
                    c.delayed = m == StartMode::AutomaticDelayed;
                }
                int radio = IDC_CFG_ACCOUNT;
                for (int id : {IDC_CFG_SYSTEM, IDC_CFG_LOCALSVC, IDC_CFG_NETSVC})
                    if (IsDlgButtonChecked(dlg, id) == BST_CHECKED) radio = id;
                std::wstring user = GetWindowString(GetDlgItem(dlg, IDC_CFG_USER));
                const std::wstring pass = GetWindowString(GetDlgItem(dlg, IDC_CFG_PASS));
                const std::wstring pass2 = GetWindowString(GetDlgItem(dlg, IDC_CFG_PASS2));
                if (radio != g_config.accountRadio || (radio == IDC_CFG_ACCOUNT && (user != g_config.user || !pass.empty()))) {
                    c.setAccount = true;
                    switch (radio) {
                        case IDC_CFG_SYSTEM: c.account = L"LocalSystem"; break;
                        case IDC_CFG_LOCALSVC: c.account = L"NT AUTHORITY\\LocalService"; break;
                        case IDC_CFG_NETSVC: c.account = L"NT AUTHORITY\\NetworkService"; break;
                        default:
                            if (user.empty()) {
                                MessageBoxW(dlg, L"Enter the account name, for example .\\username or DOMAIN\\user.",
                                            APP_NAME, MB_ICONWARNING);
                                SetResult(dlg, PSNRET_INVALID_NOCHANGEPAGE);
                                return TRUE;
                            }
                            if (pass != pass2) {
                                MessageBoxW(dlg, L"The two passwords do not match.", APP_NAME, MB_ICONWARNING);
                                SetResult(dlg, PSNRET_INVALID_NOCHANGEPAGE);
                                return TRUE;
                            }
                            if (user.find(L'\\') == std::wstring::npos && user.find(L'@') == std::wstring::npos)
                                user = L".\\" + user;  // a local account
                            c.account = user;
                            c.password = pass;
                    }
                }
                if (c.displayName.empty() && !c.setDescription && !c.setStartType && !c.setAccount) {
                    SetResult(dlg, PSNRET_NOERROR);
                    return TRUE;
                }
                if (const DWORD e = Inspect::WriteConfig(s.name, c)) {
                    MessageBoxW(dlg, (L"The configuration could not be changed.\n\n" + Svc::ErrorText(e)).c_str(),
                                APP_NAME, MB_ICONWARNING);
                    SetResult(dlg, PSNRET_INVALID_NOCHANGEPAGE);
                    return TRUE;
                }
                if (c.setStartType && ctx->onStartTypeChanged) ctx->onStartTypeChanged(s);
                if (c.setAccount && s.state != SERVICE_STOPPED)
                    MessageBoxW(dlg, L"The new account is used the next time the service starts. Restart the "
                                     L"service to use it now.",
                                APP_NAME, MB_ICONINFORMATION);
                // The applied values become the new baseline.
                ctx->service.displayName = name.empty() ? s.displayName : name;
                ctx->service.description = desc;
                if (c.setStartType) g_config.startIndex = start;
                g_config.accountRadio = radio;
                g_config.user = radio == IDC_CFG_ACCOUNT ? user : L"";
                SetDlgItemTextW(dlg, IDC_CFG_PASS, L"");
                SetDlgItemTextW(dlg, IDC_CFG_PASS2, L"");
                PostMessageW(ctx->main, WM_COMMAND, ID_REFRESH, 0);
                SetResult(dlg, PSNRET_NOERROR);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// ===========================================================================
// Recovery
// ===========================================================================
// Combo order -> action.
const Inspect::Recovery kRecoveryOrder[] = {Inspect::Recovery::None, Inspect::Recovery::Restart,
                                           Inspect::Recovery::RunCommand, Inspect::Recovery::Reboot};
bool g_recoveryLoading = true;
bool g_recoveryDirty = false;  // only edited settings are written

INT_PTR CALLBACK RecoveryProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* ctx = (DetailsContext*)((PROPSHEETPAGEW*)lp)->lParam;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)ctx);
            g_recoveryLoading = true;
            g_recoveryDirty = false;
            if (!ctx->recovery.valid) ctx->recovery = Inspect::ReadRecovery(ctx->service.name);
            const Inspect::RecoveryConfig& rc = ctx->recovery;
            const int combos[3] = {IDC_REC_1, IDC_REC_2, IDC_REC_3};
            DWORD delay = 60000;
            for (int i = 0; i < 3; ++i) {
                HWND c = GetDlgItem(dlg, combos[i]);
                int sel = 0;
                for (int k = 0; k < 4; ++k) {
                    SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)Inspect::RecoveryText(kRecoveryOrder[k]).c_str());
                    if (kRecoveryOrder[k] == rc.action[i]) sel = k;
                }
                SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
                if (rc.action[i] != Inspect::Recovery::None && delay == 60000) delay = rc.delayMs[i];
            }
            if (rc.resetSeconds != INFINITE) SetDlgItemInt(dlg, IDC_REC_RESET, rc.resetSeconds / 86400, FALSE);
            SetDlgItemInt(dlg, IDC_REC_DELAY, delay / 60000, FALSE);
            CheckDlgButton(dlg, IDC_REC_NONCRASH, rc.nonCrashFailures ? BST_CHECKED : BST_UNCHECKED);
            SetDlgItemTextW(dlg, IDC_REC_CMD, rc.command.c_str());
            std::wstring note =
                L"\x201CRestart the computer\x201D restarts Windows without asking: use it only on servers.\r\n"
                L"A typical setting is: restart the service, restart the service, take no action.";
            if (!ctx->elevated) {
                note = L"Restart as administrator to change these settings.";
                for (int id : {IDC_REC_1, IDC_REC_2, IDC_REC_3, IDC_REC_RESET, IDC_REC_DELAY, IDC_REC_NONCRASH,
                               IDC_REC_CMD})
                    EnableWindow(GetDlgItem(dlg, id), FALSE);
            }
            SetDlgItemTextW(dlg, IDC_REC_NOTE, note.c_str());
            g_recoveryLoading = false;
            return TRUE;
        }
        case WM_COMMAND:
            if (!g_recoveryLoading &&
                (HIWORD(wp) == EN_CHANGE || HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == BN_CLICKED)) {
                g_recoveryDirty = true;
                PropSheet_Changed(GetParent(dlg), dlg);
            }
            break;
        case WM_NOTIFY:
            if (((NMHDR*)lp)->code == PSN_APPLY) {
                DetailsContext* ctx = Ctx(dlg);
                if (!ctx->elevated || !g_recoveryDirty) {
                    SetResult(dlg, PSNRET_NOERROR);
                    return TRUE;
                }
                Inspect::RecoveryConfig rc;
                rc.valid = true;
                const int combos[3] = {IDC_REC_1, IDC_REC_2, IDC_REC_3};
                BOOL ok = FALSE;
                const UINT minutes = GetDlgItemInt(dlg, IDC_REC_DELAY, &ok, FALSE);
                const DWORD delay = (ok ? std::min<UINT>(minutes, 1440) : 1) * 60000;
                bool command = false;
                for (int i = 0; i < 3; ++i) {
                    const int sel = (int)SendDlgItemMessageW(dlg, combos[i], CB_GETCURSEL, 0, 0);
                    rc.action[i] = kRecoveryOrder[sel >= 0 && sel < 4 ? sel : 0];
                    rc.delayMs[i] = delay;
                    if (rc.action[i] == Inspect::Recovery::RunCommand) command = true;
                }
                const UINT days = GetDlgItemInt(dlg, IDC_REC_RESET, &ok, FALSE);
                rc.resetSeconds = ok ? std::min<UINT>(days, 3650) * 86400 : INFINITE;
                rc.nonCrashFailures = IsDlgButtonChecked(dlg, IDC_REC_NONCRASH) == BST_CHECKED;
                rc.command = GetWindowString(GetDlgItem(dlg, IDC_REC_CMD));
                if (command && rc.command.empty()) {
                    MessageBoxW(dlg, L"Enter the program to run for \x201CRun a program\x201D.", APP_NAME,
                                MB_ICONWARNING);
                    SetResult(dlg, PSNRET_INVALID_NOCHANGEPAGE);
                    return TRUE;
                }
                if (const DWORD e = Inspect::WriteRecovery(ctx->service.name, rc)) {
                    MessageBoxW(dlg, (L"The recovery settings could not be changed.\n\n" + Svc::ErrorText(e)).c_str(),
                                APP_NAME, MB_ICONWARNING);
                    SetResult(dlg, PSNRET_INVALID_NOCHANGEPAGE);
                    return TRUE;
                }
                ctx->recovery = rc;
                g_recoveryDirty = false;
                SetResult(dlg, PSNRET_NOERROR);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// ===========================================================================
// Dependencies
// ===========================================================================
HTREEITEM AddNode(HWND tree, HTREEITEM parent, const std::wstring& text) {
    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT;
    ins.item.pszText = const_cast<wchar_t*>(text.c_str());
    return TreeView_InsertItem(tree, &ins);
}

std::wstring NodeText(const ServiceInfo& s) {
    return s.displayName + L"  (" + s.name + L")  \x2014  " + Svc::StateText(s.state);
}

void AddNeeds(HWND tree, HTREEITEM parent, const std::vector<ServiceInfo>& all, const ServiceInfo& s,
              std::set<std::wstring>& path, int depth) {
    for (const auto& d : s.dependencies) {
        const ServiceInfo* dep = (!d.empty() && d[0] == SC_GROUP_IDENTIFIERW) ? nullptr : Find(all, d);
        HTREEITEM node = AddNode(tree, parent, dep ? NodeText(*dep) : DepName(all, d));
        if (dep && depth < 10 && !path.count(ToLower(dep->name))) {
            path.insert(ToLower(dep->name));
            AddNeeds(tree, node, all, *dep, path, depth + 1);
            path.erase(ToLower(dep->name));
        }
    }
}

void AddNeededBy(HWND tree, HTREEITEM parent, const std::vector<ServiceInfo>& all, const ServiceInfo& s,
                 std::set<std::wstring>& path, int depth) {
    for (const ServiceInfo* d : DirectDependents(all, s.name)) {
        HTREEITEM node = AddNode(tree, parent, NodeText(*d));
        if (depth < 10 && !path.count(ToLower(d->name))) {
            path.insert(ToLower(d->name));
            AddNeededBy(tree, node, all, *d, path, depth + 1);
            path.erase(ToLower(d->name));
        }
    }
}

void ExpandAll(HWND tree, HTREEITEM item) {
    for (; item; item = TreeView_GetNextSibling(tree, item)) {
        TreeView_Expand(tree, item, TVE_EXPAND);
        ExpandAll(tree, TreeView_GetChild(tree, item));
    }
}

INT_PTR CALLBACK DepsProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    if (msg != WM_INITDIALOG) return FALSE;
    auto* ctx = (DetailsContext*)((PROPSHEETPAGEW*)lp)->lParam;
    SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)ctx);
    HWND tree = GetDlgItem(dlg, IDC_DEP_TREE);
    const ServiceInfo& s = ctx->service;
    std::set<std::wstring> path{ToLower(s.name)};
    HTREEITEM needs = AddNode(tree, TVI_ROOT, L"Needs these services to run");
    AddNeeds(tree, needs, ctx->all, s, path, 0);
    if (!TreeView_GetChild(tree, needs)) AddNode(tree, needs, L"(none)");
    HTREEITEM neededBy = AddNode(tree, TVI_ROOT, L"These services need it (they stop if it stops)");
    AddNeededBy(tree, neededBy, ctx->all, s, path, 0);
    if (!TreeView_GetChild(tree, neededBy)) AddNode(tree, neededBy, L"(none)");
    ExpandAll(tree, TreeView_GetRoot(tree));
    return TRUE;
}

// ===========================================================================
// History
// ===========================================================================
struct EventsJob {
    HWND page;
    std::wstring name, displayName;
};

struct EventsResult {
    bool ok = false;
    DWORD error = 0;
    std::vector<Inspect::EventEntry> events;
};

DWORD WINAPI EventsThread(LPVOID p) {
    std::unique_ptr<EventsJob> job((EventsJob*)p);
    auto* res = new EventsResult;
    res->ok = Inspect::RecentEvents(job->name, job->displayName, 40, res->events, res->error);
    if (!IsWindow(job->page) || !PostMessageW(job->page, WM_APP_EVENTS_READY, 0, (LPARAM)res)) delete res;
    return 0;
}

INT_PTR CALLBACK EventsProc(HWND dlg, UINT msg, WPARAM, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* ctx = (DetailsContext*)((PROPSHEETPAGEW*)lp)->lParam;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)ctx);
            SetDlgItemTextW(dlg, IDC_EVT_INFO, L"Reading the System event log\x2026");
            auto* job = new EventsJob{dlg, ctx->service.name, ctx->service.displayName};
            HANDLE t = CreateThread(nullptr, 0, EventsThread, job, 0, nullptr);
            if (t) CloseHandle(t);
            else delete job;
            return TRUE;
        }
        case WM_APP_EVENTS_READY: {
            std::unique_ptr<EventsResult> res((EventsResult*)lp);
            if (!res->ok) {
                SetDlgItemTextW(dlg, IDC_EVT_INFO, L"The event log could not be read.");
                SetDlgItemTextW(dlg, IDC_EVT_TEXT,
                                res->error ? Svc::ErrorText(res->error).c_str()
                                           : L"The Windows event log is not available on this system.");
                return TRUE;
            }
            const std::wstring info =
                res->events.empty()
                    ? L"No recent events mention this service."
                    : L"The " + std::to_wstring(res->events.size()) +
                          L" most recent events about this service (newest first):";
            SetDlgItemTextW(dlg, IDC_EVT_INFO, info.c_str());
            std::wstring text;
            for (const auto& e : res->events) {
                const wchar_t* level = e.level <= 2 ? L"ERROR" : e.level == 3 ? L"WARNING" : L"Info";
                std::wstring m = e.message;
                std::replace(m.begin(), m.end(), L'\r', L' ');
                std::replace(m.begin(), m.end(), L'\n', L' ');
                text += Inspect::FormatTime(e.time) + L"   " + level + L"   (event " + std::to_wstring(e.id) +
                        L")\r\n" + m + L"\r\n\r\n";
            }
            SetDlgItemTextW(dlg, IDC_EVT_TEXT, text.c_str());
            return TRUE;
        }
    }
    return FALSE;
}

// ===========================================================================
// Summary of several services
// ===========================================================================
struct SummaryState {
    const std::vector<ServiceInfo>* services;
};

std::wstring SummaryText(const std::vector<ServiceInfo>& services) {
    std::wstring t;
    const std::wstring note = Advice::SourceNote();
    if (!note.empty()) t += note + L"\r\n\r\n";
    for (const auto& s : services) {
        t += s.displayName + L"  (" + s.name + L")\r\n";
        t += L"    " + Svc::StateText(s.state) + L"  \x00B7  " + Svc::StartTypeText(s) + L"  \x00B7  " +
             Svc::SafetyText(s.safety) + L"\r\n";
        if (!s.description.empty()) t += L"    " + s.description + L"\r\n";
        t += L"    Can it be disabled? " + Advice::Describe(s.name, false) + L"\r\n";
        if (s.warnings) t += L"    Warnings: " + Svc::WarningsText(s.warnings) + L"\r\n";
        t += L"\r\n";
    }
    return t;
}

INT_PTR CALLBACK SummaryProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            const auto* st = (SummaryState*)lp;
            const std::wstring title = L"About the " + std::to_wstring(st->services->size()) + L" selected services";
            SetWindowTextW(dlg, title.c_str());
            SetDlgItemTextW(dlg, IDC_SUM_TEXT, SummaryText(*st->services).c_str());
            ApplyWindowTheme(dlg);
            Advice::Fetch(dlg, WM_APP_ADVICE_READY);
            return TRUE;
        }
        case WM_APP_ADVICE_READY: {
            const auto* st = (SummaryState*)GetWindowLongPtrW(dlg, DWLP_USER);
            SetDlgItemTextW(dlg, IDC_SUM_TEXT, SummaryText(*st->services).c_str());
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SUM_COPY: CopyToClipboard(dlg, GetWindowString(GetDlgItem(dlg, IDC_SUM_TEXT))); return TRUE;
                case IDOK:
                case IDCANCEL: EndDialog(dlg, IDOK); return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

// ===========================================================================
// Public
// ===========================================================================
void ShowServiceDetails(HINSTANCE inst, HWND owner, DetailsContext& ctx) {
    struct Page {
        int id;
        DLGPROC proc;
    };
    const Page pages[] = {{IDD_PAGE_GENERAL, GeneralProc}, {IDD_PAGE_CONFIG, ConfigProc},
                          {IDD_PAGE_RECOVERY, RecoveryProc}, {IDD_PAGE_DEPS, DepsProc},
                          {IDD_PAGE_EVENTS, EventsProc}};
    PROPSHEETPAGEW psp[5]{};
    for (int i = 0; i < 5; ++i) {
        psp[i].dwSize = sizeof(PROPSHEETPAGEW);
        psp[i].dwFlags = PSP_DEFAULT;
        psp[i].hInstance = inst;
        psp[i].pszTemplate = MAKEINTRESOURCEW(pages[i].id);
        psp[i].pfnDlgProc = pages[i].proc;
        psp[i].lParam = (LPARAM)&ctx;
    }
    const std::wstring caption = ctx.service.displayName + L" \x2014 Properties";
    PROPSHEETHEADERW h{};
    h.dwSize = sizeof(h);
    h.dwFlags = PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP;
    h.hwndParent = owner;
    h.hInstance = inst;
    h.pszCaption = caption.c_str();
    h.nPages = 5;
    h.ppsp = psp;
    PropertySheetW(&h);
}

void ShowServicesSummary(HINSTANCE inst, HWND owner, const std::vector<ServiceInfo>& services) {
    SummaryState st{&services};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SUMMARY), owner, SummaryProc, (LPARAM)&st);
}

void OpenVirusTotal(HWND owner, const std::wstring& file) {
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const std::wstring hash = Inspect::Sha256(file);
    SetCursor(old);
    if (hash.empty()) {
        MessageBoxW(owner, L"The file could not be read.", APP_NAME, MB_ICONWARNING);
        return;
    }
    const std::wstring url = L"https://www.virustotal.com/gui/file/" + hash;
    ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SearchServiceOnline(HWND owner, const ServiceInfo& s) {
    const std::wstring query = L"\"" + s.name + L"\" " + s.displayName + L" Windows service";
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
