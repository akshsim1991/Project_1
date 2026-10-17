// Inspect.cpp - signatures, hashes, process statistics, recovery settings,
// configuration changes and event-log history (see Inspect.h).
#include "Inspect.h"

#include <algorithm>
#include <memory>

// clang-format off
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <bcrypt.h>
#include <psapi.h>
#include <ntsecapi.h>
#include <winevt.h>
// clang-format on

#include "Util.h"

namespace {
struct ScHandle {
    SC_HANDLE h = nullptr;
    explicit ScHandle(SC_HANDLE handle) : h(handle) {}
    ~ScHandle() {
        if (h) CloseServiceHandle(h);
    }
    ScHandle(const ScHandle&) = delete;
    ScHandle& operator=(const ScHandle&) = delete;
};

struct EvtHandle {
    EVT_HANDLE h = nullptr;
    explicit EvtHandle(EVT_HANDLE handle) : h(handle) {}
    ~EvtHandle() {
        if (h) EvtClose(h);
    }
    EvtHandle(const EvtHandle&) = delete;
    EvtHandle& operator=(const EvtHandle&) = delete;
};

SC_HANDLE OpenService(const std::wstring& name, DWORD access, SC_HANDLE* scmOut) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return nullptr;
    SC_HANDLE svc = OpenServiceW(scm, name.c_str(), access);
    if (!svc) {
        const DWORD e = GetLastError();
        CloseServiceHandle(scm);
        SetLastError(e);
        return nullptr;
    }
    *scmOut = scm;
    return svc;
}

// The certificate subject of the first signer of a verified file.
std::wstring SignerOf(HANDLE state) {
    CRYPT_PROVIDER_DATA* data = WTHelperProvDataFromStateData(state);
    CRYPT_PROVIDER_SGNR* signer = data ? WTHelperGetProvSignerFromChain(data, 0, FALSE, 0) : nullptr;
    CRYPT_PROVIDER_CERT* cert = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
    if (!cert || !cert->pCert) return {};
    wchar_t name[256] = {};
    CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
    return name;
}

LONG Verify(WINTRUST_DATA& wd, std::wstring& signer) {
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;  // never go online here
    const LONG r = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);
    if (wd.hWVTStateData) signer = SignerOf(wd.hWVTStateData);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);
    return r;
}

bool NoSignature(LONG r) {
    return r == (LONG)TRUST_E_NOSIGNATURE || r == (LONG)TRUST_E_SUBJECT_FORM_UNKNOWN ||
           r == (LONG)TRUST_E_PROVIDER_UNKNOWN;
}

// Windows' own files are usually signed through catalog files rather than
// an embedded signature: look the file's hash up in the system catalogs.
// The SHA-256 capable "2" functions (Windows 8+) are resolved at run time,
// because some SDK import libraries lack them.
using AcquireContext2Fn = BOOL(WINAPI*)(HCATADMIN*, const GUID*, PCWSTR, PCCERT_STRONG_SIGN_PARA, DWORD);
using CalcHash2Fn = BOOL(WINAPI*)(HCATADMIN, HANDLE, DWORD*, BYTE*, DWORD);

bool CatalogSignature(const std::wstring& file, const wchar_t* hashAlg, Signature& sig) {
    static HMODULE wintrust = LoadLibraryExW(L"wintrust.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto acquire2 = wintrust ? (AcquireContext2Fn)GetProcAddress(wintrust, "CryptCATAdminAcquireContext2") : nullptr;
    static auto calc2 = wintrust ? (CalcHash2Fn)GetProcAddress(wintrust, "CryptCATAdminCalcHashFromFileHandle2") : nullptr;
    HCATADMIN admin = nullptr;
    bool found = false;
    const bool modern = acquire2 && calc2;
    if (!modern && hashAlg) return false;  // SHA-256 needs the modern functions
    if (modern ? !acquire2(&admin, nullptr, hashAlg, nullptr, 0) : !CryptCATAdminAcquireContext(&admin, nullptr, 0))
        return false;
    auto calc = [&](HANDLE f, DWORD* len, BYTE* hash) {
        return modern ? calc2(admin, f, len, hash, 0) != FALSE
                      : CryptCATAdminCalcHashFromFileHandle(f, len, hash, 0) != FALSE;
    };
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD len = 0;
        calc(f, &len, nullptr);
        std::vector<BYTE> hash(len ? len : 64);
        if (len && calc(f, &len, hash.data())) {
            HCATINFO ci = CryptCATAdminEnumCatalogFromHash(admin, hash.data(), len, 0, nullptr);
            if (ci) {
                CATALOG_INFO info{};
                info.cbStruct = sizeof(info);
                if (CryptCATCatalogInfoFromContext(ci, &info, 0)) {
                    std::wstring tag;
                    for (DWORD i = 0; i < len; ++i) {
                        wchar_t hx[3];
                        swprintf_s(hx, L"%02X", hash[i]);
                        tag += hx;
                    }
                    WINTRUST_CATALOG_INFO wci{};
                    wci.cbStruct = sizeof(wci);
                    wci.pcwszCatalogFilePath = info.wszCatalogFile;
                    wci.pcwszMemberFilePath = file.c_str();
                    wci.pcwszMemberTag = tag.c_str();
                    wci.hMemberFile = f;
                    wci.pbCalculatedFileHash = hash.data();
                    wci.cbCalculatedFileHash = len;
                    wci.hCatAdmin = admin;
                    WINTRUST_DATA wd{};
                    wd.dwUnionChoice = WTD_CHOICE_CATALOG;
                    wd.pCatalog = &wci;
                    const LONG r = Verify(wd, sig.signer);
                    sig.state = r == 0 ? SignState::Signed : SignState::Invalid;
                    sig.catalog = true;
                    found = true;
                }
                CryptCATAdminReleaseCatalogContext(admin, ci, 0);
            }
        }
        CloseHandle(f);
    }
    CryptCATAdminReleaseContext(admin, 0);
    return found;
}

std::wstring ToHex(const BYTE* p, size_t n) {
    std::wstring s;
    for (size_t i = 0; i < n; ++i) {
        wchar_t hx[3];
        swprintf_s(hx, L"%02x", p[i]);
        s += hx;
    }
    return s;
}

bool EnablePrivilege(const wchar_t* name) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok = LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid) &&
                    AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) &&
                    GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

// Services.msc grants "Log on as a service" when an account is chosen;
// so do we, otherwise the service fails to start with a logon error.
DWORD GrantServiceLogon(std::wstring account) {
    if (account.rfind(L".\\", 0) == 0) {
        wchar_t pc[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
        if (GetComputerNameW(pc, &n)) account = std::wstring(pc) + account.substr(1);
    }
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sid);
    wchar_t domain[256];
    DWORD domainSize = 256;
    SID_NAME_USE use;
    if (!LookupAccountNameW(nullptr, account.c_str(), sid, &sidSize, domain, &domainSize, &use))
        return GetLastError();
    LSA_OBJECT_ATTRIBUTES attrs{};
    LSA_HANDLE policy = nullptr;
    if (LsaOpenPolicy(nullptr, &attrs, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES, &policy) != 0)
        return ERROR_ACCESS_DENIED;
    wchar_t rightName[] = L"SeServiceLogonRight";
    LSA_UNICODE_STRING right;
    right.Buffer = rightName;
    right.Length = (USHORT)(wcslen(rightName) * sizeof(wchar_t));
    right.MaximumLength = right.Length + sizeof(wchar_t);
    const NTSTATUS st = LsaAddAccountRights(policy, (PSID)sid, &right, 1);
    LsaClose(policy);
    return st == 0 ? 0 : LsaNtStatusToWinError(st);
}

std::wstring FormatEventMessage(EVT_HANDLE publisher, EVT_HANDLE ev) {
    if (!publisher) return {};
    DWORD used = 0;
    if (!EvtFormatMessage(publisher, ev, 0, 0, nullptr, EvtFormatMessageEvent, 0, nullptr, &used) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return {};
    std::wstring s(used, L'\0');
    if (!EvtFormatMessage(publisher, ev, 0, 0, nullptr, EvtFormatMessageEvent, used, s.data(), &used))
        return {};
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

std::wstring RenderXml(EVT_HANDLE ev) {
    DWORD used = 0, count = 0;
    EvtRender(nullptr, ev, EvtRenderEventXml, 0, nullptr, &used, &count);
    if (!used) return {};
    std::vector<wchar_t> buf(used / sizeof(wchar_t) + 1);
    if (!EvtRender(nullptr, ev, EvtRenderEventXml, (DWORD)(buf.size() * sizeof(wchar_t)), buf.data(), &used,
                   &count))
        return {};
    return buf.data();
}

// <Data Name='key'>value</Data> from rendered event XML.
std::wstring XmlData(const std::wstring& xml, const wchar_t* key) {
    for (const wchar_t* q : {L"'", L"\""}) {
        const std::wstring open = std::wstring(L"Name=") + q + key + q + L">";
        const size_t p = xml.find(open);
        if (p == std::wstring::npos) continue;
        const size_t start = p + open.size();
        const size_t end = xml.find(L"</Data>", start);
        return xml.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    }
    return {};
}
}  // namespace

// ===========================================================================
// Program file
// ===========================================================================
Signature Inspect::CheckSignature(const std::wstring& file) {
    Signature sig;
    if (file.empty() || !FileExists(file)) return sig;
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = file.c_str();
    WINTRUST_DATA wd{};
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    const LONG r = Verify(wd, sig.signer);
    if (r == 0) {
        sig.state = SignState::Signed;
        return sig;
    }
    if (!NoSignature(r)) {
        sig.state = SignState::Invalid;  // tampered, expired or untrusted
        return sig;
    }
    sig.signer.clear();
    if (CatalogSignature(file, BCRYPT_SHA256_ALGORITHM, sig) || CatalogSignature(file, nullptr, sig))
        return sig;
    sig.state = SignState::Unsigned;
    return sig;
}

std::wstring Inspect::Sha256(const std::wstring& file) {
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::wstring out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<BYTE> buf(1 << 16);
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr) && read)
            if (BCryptHashData(hash, buf.data(), read, 0) != 0) {
                ok = false;
                break;
            }
        BYTE digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) out = ToHex(digest, sizeof(digest));
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return out;
}

std::wstring Inspect::FileVersion(const std::wstring& file) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (!size) return {};
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT len = 0;
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", (void**)&fixed, &len) || !fixed)
        return {};
    wchar_t buf[64];
    swprintf_s(buf, L"%u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
               HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
    return buf;
}

// ===========================================================================
// Process
// ===========================================================================
Inspect::ProcessStats Inspect::QueryProcess(DWORD pid) {
    ProcessStats st;
    if (!pid) return st;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return st;
    FILETIME created, exited, kernel, user, now;
    if (GetProcessTimes(p, &created, &exited, &kernel, &user)) {
        auto u64 = [](const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };
        GetSystemTimeAsFileTime(&now);
        st.cpuMs = (u64(kernel) + u64(user)) / 10000;
        st.uptimeSec = u64(now) > u64(created) ? (u64(now) - u64(created)) / 10000000 : 0;
        st.valid = true;
    }
    PROCESS_MEMORY_COUNTERS_EX mem{};
    mem.cb = sizeof(mem);
    if (GetProcessMemoryInfo(p, (PROCESS_MEMORY_COUNTERS*)&mem, sizeof(mem))) {
        st.workingSet = mem.WorkingSetSize;
        st.privateBytes = mem.PrivateUsage;
    }
    CloseHandle(p);
    return st;
}

// ===========================================================================
// Recovery
// ===========================================================================
std::wstring Inspect::RecoveryText(Recovery r) {
    switch (r) {
        case Recovery::Restart: return L"Restart the service";
        case Recovery::RunCommand: return L"Run a program";
        case Recovery::Reboot: return L"Restart the computer";
        default: return L"Take no action";
    }
}

Inspect::RecoveryConfig Inspect::ReadRecovery(const std::wstring& service) {
    RecoveryConfig rc;
    SC_HANDLE scm = nullptr;
    SC_HANDLE svc = OpenService(service, SERVICE_QUERY_CONFIG, &scm);
    if (!svc) return rc;
    std::vector<BYTE> buf(8192);
    DWORD needed = 0;
    if (QueryServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, buf.data(), (DWORD)buf.size(), &needed) ||
        (GetLastError() == ERROR_INSUFFICIENT_BUFFER && needed < (1u << 20) &&
         (buf.resize(needed), QueryServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, buf.data(),
                                                   (DWORD)buf.size(), &needed)))) {
        const auto* fa = (const SERVICE_FAILURE_ACTIONSW*)buf.data();
        rc.valid = true;
        rc.resetSeconds = fa->dwResetPeriod;
        rc.command = fa->lpCommand ? fa->lpCommand : L"";
        for (int i = 0; i < 3; ++i) {
            if (fa->cActions == 0) break;
            // Windows repeats the last action for later failures.
            const SC_ACTION& a = fa->lpsaActions[std::min<DWORD>((DWORD)i, fa->cActions - 1)];
            rc.action[i] = (Recovery)std::min<int>((int)a.Type, 3);
            rc.delayMs[i] = a.Delay;
        }
    }
    SERVICE_FAILURE_ACTIONS_FLAG flag{};
    if (QueryServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, (BYTE*)&flag, sizeof(flag), &needed))
        rc.nonCrashFailures = flag.fFailureActionsOnNonCrashFailures != FALSE;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return rc;
}

DWORD Inspect::WriteRecovery(const std::wstring& service, const RecoveryConfig& rc) {
    bool restart = false, reboot = false;
    for (Recovery a : rc.action) {
        if (a == Recovery::Restart) restart = true;
        if (a == Recovery::Reboot) reboot = true;
    }
    // Restarting the computer as a recovery action needs the shutdown privilege.
    if (reboot) EnablePrivilege(SE_SHUTDOWN_NAME);
    SC_HANDLE scm = nullptr;
    SC_HANDLE svc = OpenService(service, SERVICE_CHANGE_CONFIG | (restart ? SERVICE_START : 0), &scm);
    if (!svc) return GetLastError();
    SC_ACTION acts[3];
    for (int i = 0; i < 3; ++i) {
        acts[i].Type = (SC_ACTION_TYPE)rc.action[i];
        acts[i].Delay = rc.action[i] == Recovery::None ? 0 : rc.delayMs[i];
    }
    SERVICE_FAILURE_ACTIONSW fa{};
    fa.dwResetPeriod = rc.resetSeconds;
    std::wstring command = rc.command;
    fa.lpCommand = command.data();  // empty string clears it
    fa.cActions = 3;
    fa.lpsaActions = acts;
    DWORD err = 0;
    if (!ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa)) err = GetLastError();
    SERVICE_FAILURE_ACTIONS_FLAG flag{rc.nonCrashFailures};
    if (!err && !ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag)) err = GetLastError();
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return err;
}

// ===========================================================================
// Configuration
// ===========================================================================
DWORD Inspect::WriteConfig(const std::wstring& service, const ConfigChange& c) {
    if (c.setAccount && _wcsicmp(c.account.c_str(), L"LocalSystem") != 0 &&
        c.account.rfind(L"NT AUTHORITY\\", 0) != 0) {
        if (DWORD e = GrantServiceLogon(c.account)) return e;
    }
    SC_HANDLE scm = nullptr;
    SC_HANDLE svc = OpenService(service, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG, &scm);
    if (!svc) return GetLastError();
    DWORD err = 0;
    const bool needMain = c.setStartType || c.setAccount || !c.displayName.empty();
    if (needMain &&
        !ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, c.setStartType ? c.startType : SERVICE_NO_CHANGE,
                              SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr,
                              c.setAccount ? c.account.c_str() : nullptr,
                              c.setAccount ? c.password.c_str() : nullptr,
                              c.displayName.empty() ? nullptr : c.displayName.c_str()))
        err = GetLastError();
    if (!err && c.setStartType) {
        SERVICE_DELAYED_AUTO_START_INFO d{c.startType == SERVICE_AUTO_START && c.delayed};
        if (!ChangeServiceConfig2W(svc, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &d) && d.fDelayedAutostart)
            err = GetLastError();
    }
    if (!err && c.setDescription) {
        std::wstring text = c.description;
        SERVICE_DESCRIPTIONW d{text.data()};
        if (!ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &d)) err = GetLastError();
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return err;
}

// ===========================================================================
// Event log
// ===========================================================================
bool Inspect::RecentEvents(const std::wstring& name, const std::wstring& displayName, size_t max,
                           std::vector<EventEntry>& out, DWORD& error) {
    out.clear();
    error = 0;
    EvtHandle query(EvtQuery(nullptr, L"System", L"*[System[Provider[@Name='Service Control Manager']]]",
                             EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!query.h) {
        error = GetLastError();
        return false;
    }
    EvtHandle publisher(EvtOpenPublisherMetadata(nullptr, L"Service Control Manager", nullptr, 0, 0));
    EvtHandle context(EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem));
    const std::wstring nameLower = ToLower(name), displayLower = ToLower(displayName);
    size_t scanned = 0;
    EVT_HANDLE batch[64];
    DWORD got = 0;
    // Stop after a few thousand events: older history is not worth the wait.
    while (out.size() < max && scanned < 4000 && EvtNext(query.h, 64, batch, 3000, 0, &got)) {
        for (DWORD i = 0; i < got; ++i) {
            ++scanned;
            if (out.size() < max) {
                std::wstring msg = FormatEventMessage(publisher.h, batch[i]);
                if (!msg.empty() && (ContainsNoCase(msg, displayLower) || ContainsNoCase(msg, nameLower))) {
                    EventEntry e;
                    e.message = msg;
                    DWORD used = 0, count = 0;
                    BYTE values[2048];
                    if (context.h && EvtRender(context.h, batch[i], EvtRenderEventValues, sizeof(values), values,
                                               &used, &count)) {
                        const auto* v = (const EVT_VARIANT*)values;
                        if (count > EvtSystemTimeCreated && v[EvtSystemTimeCreated].Type == EvtVarTypeFileTime) {
                            const ULONGLONG t = v[EvtSystemTimeCreated].FileTimeVal;
                            e.time.dwLowDateTime = (DWORD)t;
                            e.time.dwHighDateTime = (DWORD)(t >> 32);
                        }
                        if (count > EvtSystemLevel && v[EvtSystemLevel].Type == EvtVarTypeByte)
                            e.level = v[EvtSystemLevel].ByteVal;
                        if (count > EvtSystemEventID && v[EvtSystemEventID].Type == EvtVarTypeUInt16)
                            e.id = v[EvtSystemEventID].UInt16Val;
                    }
                    out.push_back(std::move(e));
                }
            }
            EvtClose(batch[i]);
        }
    }
    return true;
}

bool Inspect::ReadBootDelays(std::map<std::wstring, BootDelay>& out) {
    out.clear();
    EvtHandle query(EvtQuery(nullptr, L"Microsoft-Windows-Diagnostics-Performance/Operational",
                             L"*[System[(EventID=103)]]", EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!query.h) return false;
    EVT_HANDLE batch[32];
    DWORD got = 0;
    int scanned = 0;
    while (scanned < 2000 && EvtNext(query.h, 32, batch, 3000, 0, &got)) {
        for (DWORD i = 0; i < got; ++i) {
            ++scanned;
            const std::wstring xml = RenderXml(batch[i]);
            const DWORD ms = (DWORD)_wtoi(XmlData(xml, L"DegradationTime").c_str());
            for (const wchar_t* key : {L"Name", L"FriendlyName"}) {
                std::wstring n = ToLower(XmlData(xml, key));
                if (n.empty()) continue;
                BootDelay& d = out[n];
                d.worstMs = std::max(d.worstMs, ms);
                ++d.count;
            }
            EvtClose(batch[i]);
        }
    }
    return true;
}

// ===========================================================================
// Formatting
// ===========================================================================
std::wstring Inspect::FormatTime(const FILETIME& ft) {
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return {};
    wchar_t buf[64];
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour,
               local.wMinute);
    return buf;
}

std::wstring Inspect::FormatBytes(unsigned long long b) {
    wchar_t buf[64];
    if (b >= (1ull << 30))
        swprintf_s(buf, L"%.2f GB", b / 1073741824.0);
    else if (b >= (1ull << 20))
        swprintf_s(buf, L"%.1f MB", b / 1048576.0);
    else
        swprintf_s(buf, L"%.0f KB", b / 1024.0);
    return buf;
}

std::wstring Inspect::FormatDuration(unsigned long long s) {
    const unsigned long long d = s / 86400, h = s / 3600 % 24, m = s / 60 % 60;
    if (d) return std::to_wstring(d) + L" d " + std::to_wstring(h) + L" h";
    if (h) return std::to_wstring(h) + L" h " + std::to_wstring(m) + L" min";
    if (m) return std::to_wstring(m) + L" min";
    return std::to_wstring(s) + L" s";
}
