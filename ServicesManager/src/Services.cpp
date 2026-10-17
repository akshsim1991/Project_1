// Services.cpp - Service Control Manager access (see Services.h).
#include "Services.h"

#include <algorithm>
#include <memory>

#include <shlwapi.h>

#include "Inspect.h"
#include "Util.h"

namespace {
// Closes an SC_HANDLE when it goes out of scope.
struct ScHandle {
    SC_HANDLE h = nullptr;
    explicit ScHandle(SC_HANDLE handle) : h(handle) {}
    ~ScHandle() {
        if (h) CloseServiceHandle(h);
    }
    ScHandle(const ScHandle&) = delete;
    ScHandle& operator=(const ScHandle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

// Services Windows needs to run, boot, log on, network or stay secure.
// Stopping or disabling them can make the PC unstable or unbootable.
const wchar_t* const kCritical[] = {
    L"AppInfo", L"AudioEndpointBuilder", L"BFE", L"BrokerInfrastructure", L"CoreMessagingRegistrar",
    L"CryptSvc", L"DcomLaunch", L"Dhcp", L"Dnscache", L"EventLog", L"EventSystem", L"gpsvc",
    L"KeyIso", L"LanmanWorkstation", L"LSM", L"mpssvc", L"netprofm", L"NlaSvc", L"nsi",
    L"PlugPlay", L"Power", L"ProfSvc", L"RpcEptMapper", L"RpcSs", L"SamSs", L"Schedule",
    L"SecurityHealthService", L"SENS", L"SgrmBroker", L"sppsvc", L"StateRepository",
    L"SystemEventsBroker", L"TimeBrokerSvc", L"TrustedInstaller", L"UserManager", L"Wcmsvc",
    L"WdNisSvc", L"WinDefend", L"Winmgmt", L"wscsvc",
};

std::wstring SystemRoot() {
    wchar_t buf[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(buf, n) : std::wstring(L"C:\\Windows");
}

bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

// "C:\Program Files\X\x.exe" -k arg  ->  C:\Program Files\X\x.exe
std::wstring ImageFromCommandLine(const std::wstring& cmd) {
    std::wstring p = cmd;
    p.erase(0, p.find_first_not_of(L" \t"));
    if (p.empty()) return p;
    if (p[0] == L'"') {
        const size_t end = p.find(L'"', 1);
        p = p.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
    } else {
        const std::wstring low = ToLower(p);
        size_t pos = low.find(L".exe");
        if (pos != std::wstring::npos)
            p.resize(pos + 4);
        else if ((pos = p.find(L' ')) != std::wstring::npos)
            p.resize(pos);
    }
    if (StartsWithNoCase(p, L"\\??\\")) p.erase(0, 4);
    if (StartsWithNoCase(p, L"\\SystemRoot\\")) p = SystemRoot() + p.substr(11);
    if (StartsWithNoCase(p, L"System32\\") || StartsWithNoCase(p, L"SysWOW64\\"))
        p = SystemRoot() + L"\\" + p;
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(p.c_str(), buf, MAX_PATH * 2);
    return n && n <= MAX_PATH * 2 ? std::wstring(buf) : p;
}

std::wstring CompanyOf(const std::wstring& file) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) return {};
    struct Translation {
        WORD language, codePage;
    }* tr = nullptr;
    UINT len = 0;
    std::vector<std::wstring> keys;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &len) && tr &&
        len >= sizeof(Translation)) {
        wchar_t key[64];
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\CompanyName", tr->language, tr->codePage);
        keys.push_back(key);
    }
    keys.push_back(L"\\StringFileInfo\\040904b0\\CompanyName");
    keys.push_back(L"\\StringFileInfo\\040904e4\\CompanyName");
    for (const auto& key : keys) {
        wchar_t* value = nullptr;
        if (VerQueryValueW(data.data(), key.c_str(), (void**)&value, &len) && value && len > 1)
            return std::wstring(value, wcsnlen(value, len));
    }
    return {};
}

std::wstring AccountText(const std::wstring& a) {
    if (a.empty()) return {};
    if (_wcsicmp(a.c_str(), L"LocalSystem") == 0) return L"Local System";
    if (_wcsicmp(a.c_str(), L"NT AUTHORITY\\LocalService") == 0 ||
        _wcsicmp(a.c_str(), L"NT AUTHORITY\\Local Service") == 0)
        return L"Local Service";
    if (_wcsicmp(a.c_str(), L"NT AUTHORITY\\NetworkService") == 0 ||
        _wcsicmp(a.c_str(), L"NT AUTHORITY\\Network Service") == 0)
        return L"Network Service";
    return a;
}

// QueryServiceConfig2W into a growing buffer.
bool QueryConfig2(SC_HANDLE svc, DWORD level, std::vector<BYTE>& buf) {
    DWORD needed = 0;
    if (buf.size() < 256) buf.resize(256);
    if (QueryServiceConfig2W(svc, level, buf.data(), (DWORD)buf.size(), &needed)) return true;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed > (1u << 20)) return false;
    buf.resize(needed);
    return QueryServiceConfig2W(svc, level, buf.data(), (DWORD)buf.size(), &needed) != FALSE;
}

void ReadConfig(SC_HANDLE scm, ServiceInfo& s, EnumCache& cache) {
    ScHandle svc(OpenServiceW(scm, s.name.c_str(), SERVICE_QUERY_CONFIG));
    if (!svc) return;
    std::vector<BYTE> buf(8192);
    DWORD needed = 0;
    if (!QueryServiceConfigW(svc.h, (QUERY_SERVICE_CONFIGW*)buf.data(), (DWORD)buf.size(), &needed)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed > (1u << 20)) return;
        buf.resize(needed);
        if (!QueryServiceConfigW(svc.h, (QUERY_SERVICE_CONFIGW*)buf.data(), (DWORD)buf.size(), &needed))
            return;
    }
    const auto* cfg = (const QUERY_SERVICE_CONFIGW*)buf.data();
    s.configKnown = true;
    s.startType = cfg->dwStartType;
    s.binaryPath = cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
    s.account = AccountText(cfg->lpServiceStartName ? cfg->lpServiceStartName : L"");
    s.imagePath = ImageFromCommandLine(s.binaryPath);
    for (const wchar_t* d = cfg->lpDependencies; d && *d; d += wcslen(d) + 1) s.dependencies.push_back(d);

    std::vector<BYTE> b2;
    if (QueryConfig2(svc.h, SERVICE_CONFIG_DESCRIPTION, b2)) {
        const auto* d = (const SERVICE_DESCRIPTIONW*)b2.data();
        if (d->lpDescription) s.description = d->lpDescription;
    }
    // Some services store "@file.dll,-123" (a resource string): resolve it.
    if (!s.description.empty() && s.description[0] == L'@') {
        wchar_t text[2048];
        if (SUCCEEDED(SHLoadIndirectString(s.description.c_str(), text, 2048, nullptr)))
            s.description = text;
    }
    if (s.startType == SERVICE_AUTO_START &&
        QueryConfig2(svc.h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, b2))
        s.delayed = ((const SERVICE_DELAYED_AUTO_START_INFO*)b2.data())->fDelayedAutostart != FALSE;
    // SERVICE_TRIGGER_INFO starts with the trigger count (declared here
    // because some SDK headers lack the structure).
    struct TriggerInfoHead {
        DWORD cTriggers;
    };
    if (QueryConfig2(svc.h, 8 /* SERVICE_CONFIG_TRIGGER_INFO */, b2))
        s.triggered = ((const TriggerInfoHead*)b2.data())->cTriggers > 0;

    if (!s.imagePath.empty()) {
        auto it = cache.companies.find(s.imagePath);
        if (it == cache.companies.end()) it = cache.companies.emplace(s.imagePath, CompanyOf(s.imagePath)).first;
        s.company = it->second;
    }
}

Safety Classify(const ServiceInfo& s, const std::wstring& systemRootLower) {
    if (Svc::IsCritical(s.name)) return Safety::Critical;
    if (ContainsNoCase(s.company, L"microsoft")) return Safety::Windows;
    if (s.company.empty() && !s.imagePath.empty() &&
        ToLower(s.imagePath).rfind(systemRootLower, 0) == 0)
        return Safety::Windows;
    if (!s.configKnown) return Safety::Windows;  // unknown: assume Windows, the careful choice
    return Safety::ThirdParty;
}

// Warning flags. Signatures are only checked for third-party programs
// (Windows' own are trusted) and only once per file.
void CheckWarnings(ServiceInfo& s, EnumCache& cache) {
    if (!s.configKnown || s.imagePath.empty()) return;
    const bool exists = FileExists(s.imagePath);
    if (!exists) s.warnings |= kWarnMissingFile;
    const std::wstring low = ToLower(s.imagePath);
    if (low.find(L"\\users\\") != std::wstring::npos || low.find(L"\\appdata\\") != std::wstring::npos ||
        low.find(L"\\temp\\") != std::wstring::npos)
        s.warnings |= kWarnUserFolder;
    // C:\Program Files\My App\svc.exe without quotes: Windows may run
    // C:\Program.exe or C:\Program Files\My.exe instead.
    std::wstring cmd = s.binaryPath;
    cmd.erase(0, cmd.find_first_not_of(L" \t"));
    if (!cmd.empty() && cmd[0] != L'"') {
        const size_t exe = ToLower(cmd).find(L".exe");
        if (exe != std::wstring::npos && cmd.substr(0, exe).find(L' ') != std::wstring::npos)
            s.warnings |= kWarnUnquoted;
    }
    if (exists && s.safety == Safety::ThirdParty) {
        auto it = cache.signatures.find(low);
        if (it == cache.signatures.end())
            it = cache.signatures.emplace(low, (int)Inspect::CheckSignature(s.imagePath).state).first;
        if (it->second == (int)SignState::Unsigned) s.warnings |= kWarnUnsigned;
        if (it->second == (int)SignState::Invalid) s.warnings |= kWarnBadSignature;
    }
}
}  // namespace

// ===========================================================================
// Listing
// ===========================================================================
std::wstring Svc::WarningsText(unsigned w) {
    std::wstring out;
    auto add = [&](unsigned flag, const wchar_t* text) {
        if (!(w & flag)) return;
        if (!out.empty()) out += L" \x00B7 ";
        out += text;
    };
    add(kWarnMissingFile, L"File missing");
    add(kWarnBadSignature, L"Bad signature");
    add(kWarnUnsigned, L"Unsigned");
    add(kWarnUserFolder, L"In user folder");
    add(kWarnUnquoted, L"Unquoted path");
    return out;
}

std::wstring Svc::WarningsExplained(unsigned w) {
    std::wstring out;
    auto add = [&](unsigned flag, const wchar_t* text) {
        if (w & flag) out += std::wstring(L"\x26A0 ") + text + L"\r\n";
    };
    add(kWarnMissingFile, L"The program file is missing: the service cannot start. Often left over from "
                          L"uninstalled software.");
    add(kWarnBadSignature, L"The program's digital signature is broken or not trusted: the file may have been "
                           L"changed.");
    add(kWarnUnsigned, L"The program has no digital signature, so its publisher cannot be verified.");
    add(kWarnUserFolder, L"It runs from a user, AppData or Temp folder. Normal services run from Program Files "
                         L"or Windows; malware often hides in these folders.");
    add(kWarnUnquoted, L"Its path contains spaces but no quotes. Windows might run a different program placed "
                       L"earlier in that path (a known security hole). Ask the publisher for an update.");
    return out;
}

bool Svc::Enumerate(std::vector<ServiceInfo>& out, DWORD& error, EnumCache& cache) {
    out.clear();
    error = 0;
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE));
    if (!scm) {
        error = GetLastError();
        return false;
    }
    std::vector<BYTE> buf(128 * 1024);
    DWORD resume = 0;
    for (;;) {
        DWORD needed = 0, count = 0;
        const BOOL ok = EnumServicesStatusExW(scm.h, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                                              SERVICE_STATE_ALL, buf.data(), (DWORD)buf.size(),
                                              &needed, &count, &resume, nullptr);
        const DWORD e = ok ? 0 : GetLastError();
        if (!ok && e != ERROR_MORE_DATA) {
            error = e;
            return false;
        }
        const auto* items = (const ENUM_SERVICE_STATUS_PROCESSW*)buf.data();
        for (DWORD i = 0; i < count; ++i) {
            ServiceInfo s;
            s.name = items[i].lpServiceName ? items[i].lpServiceName : L"";
            s.displayName = items[i].lpDisplayName ? items[i].lpDisplayName : s.name;
            const SERVICE_STATUS_PROCESS& st = items[i].ServiceStatusProcess;
            s.state = st.dwCurrentState;
            s.pid = st.dwProcessId;
            s.controls = st.dwControlsAccepted;
            s.type = st.dwServiceType;
            out.push_back(std::move(s));
        }
        if (ok) break;
        if (needed > buf.size()) buf.resize(needed);
    }
    if (!cache.bootDelaysRead) {
        // Read once per run (the boot log only changes when Windows starts).
        cache.bootDelaysRead = true;
        std::map<std::wstring, Inspect::BootDelay> delays;
        if (Inspect::ReadBootDelays(delays))
            for (const auto& d : delays) cache.bootDelays[d.first] = {d.second.worstMs, d.second.count};
    }
    const std::wstring root = ToLower(SystemRoot()) + L"\\";
    for (ServiceInfo& s : out) {
        ReadConfig(scm.h, s, cache);
        s.safety = Classify(s, root);
        CheckWarnings(s, cache);
        auto it = cache.bootDelays.find(ToLower(s.name));
        if (it == cache.bootDelays.end()) it = cache.bootDelays.find(ToLower(s.displayName));
        if (it != cache.bootDelays.end()) {
            s.bootDelayMs = it->second.first;
            s.bootDelayCount = it->second.second;
        }
    }
    return true;
}

std::wstring Svc::StateText(DWORD state) {
    switch (state) {
        case SERVICE_RUNNING: return L"Running";
        case SERVICE_STOPPED: return L"Stopped";
        case SERVICE_START_PENDING: return L"Starting\x2026";
        case SERVICE_STOP_PENDING: return L"Stopping\x2026";
        case SERVICE_PAUSED: return L"Paused";
        case SERVICE_PAUSE_PENDING: return L"Pausing\x2026";
        case SERVICE_CONTINUE_PENDING: return L"Resuming\x2026";
        default: return L"Unknown";
    }
}

bool Svc::IsPending(DWORD state) {
    return state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ||
           state == SERVICE_PAUSE_PENDING || state == SERVICE_CONTINUE_PENDING;
}

std::wstring Svc::StartTypeText(const ServiceInfo& s) {
    if (!s.configKnown) return L"(no access)";
    std::wstring t;
    switch (s.startType) {
        case SERVICE_AUTO_START: t = s.delayed ? L"Automatic (delayed)" : L"Automatic"; break;
        case SERVICE_DEMAND_START: t = L"Manual"; break;
        case SERVICE_DISABLED: return L"Disabled";
        case SERVICE_BOOT_START: return L"Boot";
        case SERVICE_SYSTEM_START: return L"System";
        default: return L"Unknown";
    }
    if (s.triggered) t += L", trigger";
    return t;
}

const wchar_t* Svc::SafetyText(Safety s) {
    switch (s) {
        case Safety::Critical: return L"Critical";
        case Safety::Windows: return L"Windows";
        default: return L"Third-party";
    }
}

bool Svc::IsCritical(const std::wstring& name) {
    for (const wchar_t* c : kCritical)
        if (_wcsicmp(name.c_str(), c) == 0) return true;
    return false;
}

std::wstring Svc::ErrorText(DWORD e) {
    switch (e) {
        case ERROR_ACCESS_DENIED:
            return IsElevated()
                       ? L"Windows does not allow this for this service (it is protected)."
                       : L"Administrator rights are needed. Use \x201CRestart as administrator\x201D.";
        case ERROR_SERVICE_DISABLED:
            return L"The service is disabled. Set its start type to Manual or Automatic first.";
        case ERROR_DEPENDENT_SERVICES_RUNNING:
            return L"Other running services depend on it.";
        case ERROR_SERVICE_NOT_ACTIVE: return L"The service is not running.";
        case ERROR_SERVICE_ALREADY_RUNNING: return L"The service is already running.";
        case ERROR_INVALID_SERVICE_CONTROL:
            return L"The service does not support this action.";
        case ERROR_SERVICE_CANNOT_ACCEPT_CTRL:
            return L"The service is busy starting or stopping. Try again in a moment.";
        case ERROR_SERVICE_REQUEST_TIMEOUT:
            return L"The service did not respond in time. It may still finish on its own, or "
                   L"it may be hung (you can kill it).";
        case ERROR_SERVICE_DEPENDENCY_FAIL:
        case ERROR_SERVICE_DEPENDENCY_DELETED:
            return L"A service it depends on could not be started.";
        case ERROR_SERVICE_LOGON_FAILED:
            return L"The service could not sign in with its account (the password may have changed).";
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return L"The service's program file is missing.";
        case ERROR_SERVICE_MARKED_FOR_DELETE:
            return L"The service is being removed. Restart Windows to finish.";
        case ERROR_SERVICE_DOES_NOT_EXIST: return L"The service no longer exists.";
        case ERROR_SERVICE_NEVER_STARTED:
        case ERROR_PROCESS_ABORTED:
            return L"The service started but stopped again straight away.";
        case ERROR_CANCELLED: return L"Cancelled.";
        default: break;
    }
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPWSTR)&text, 0, nullptr);
    std::wstring msg = text ? text : L"Unknown error";
    if (text) LocalFree(text);
    while (!msg.empty() && (msg.back() == L'\n' || msg.back() == L'\r' || msg.back() == L' '))
        msg.pop_back();
    return msg + L" (" + std::to_wstring(e) + L")";
}

// ===========================================================================
// Actions
// ===========================================================================
namespace {
// Waits until the service is in `target`. Fails if it settles in another
// state (e.g. it stopped while starting), on timeout or on cancel.
DWORD WaitFor(SC_HANDLE svc, DWORD target, DWORD timeoutMs, const std::atomic<bool>& cancel) {
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        SERVICE_STATUS_PROCESS st{};
        DWORD needed = 0;
        if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (BYTE*)&st, sizeof(st), &needed))
            return GetLastError();
        if (st.dwCurrentState == target) return 0;
        if (!Svc::IsPending(st.dwCurrentState)) {
            // Settled somewhere else: a start that failed reports why.
            if (target == SERVICE_RUNNING && st.dwCurrentState == SERVICE_STOPPED) {
                if (st.dwWin32ExitCode && st.dwWin32ExitCode != ERROR_SERVICE_SPECIFIC_ERROR)
                    return st.dwWin32ExitCode;
                return ERROR_SERVICE_NEVER_STARTED;
            }
        }
        if (cancel) return ERROR_CANCELLED;
        if (GetTickCount64() - start > timeoutMs) return ERROR_SERVICE_REQUEST_TIMEOUT;
        // Poll at a tenth of the service's own estimate, within 100 ms - 1 s.
        Sleep(std::min<DWORD>(1000, std::max<DWORD>(100, st.dwWaitHint / 10)));
    }
}

DWORD CurrentState(SC_HANDLE svc, DWORD* pid = nullptr) {
    SERVICE_STATUS_PROCESS st{};
    DWORD needed = 0;
    if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, (BYTE*)&st, sizeof(st), &needed)) return 0;
    if (pid) *pid = st.dwProcessId;
    return st.dwCurrentState;
}

std::wstring DisplayNameOf(SC_HANDLE scm, const std::wstring& name) {
    wchar_t buf[512];
    DWORD len = 512;
    return GetServiceDisplayNameW(scm, name.c_str(), buf, &len) ? std::wstring(buf) : name;
}

// Running services that depend on `svc`, in the order they must be stopped.
std::vector<std::wstring> ActiveDependents(SC_HANDLE svc) {
    std::vector<std::wstring> names;
    DWORD needed = 0, count = 0;
    if (EnumDependentServicesW(svc, SERVICE_ACTIVE, nullptr, 0, &needed, &count) ||
        GetLastError() != ERROR_MORE_DATA)
        return names;
    std::vector<BYTE> buf(needed);
    if (!EnumDependentServicesW(svc, SERVICE_ACTIVE, (ENUM_SERVICE_STATUSW*)buf.data(), needed,
                                &needed, &count))
        return names;
    const auto* items = (const ENUM_SERVICE_STATUSW*)buf.data();
    for (DWORD i = 0; i < count; ++i) names.push_back(items[i].lpServiceName);
    return names;
}

DWORD StopOne(SC_HANDLE scm, const std::wstring& name, DWORD timeoutMs,
              const std::atomic<bool>& cancel) {
    ScHandle svc(OpenServiceW(scm, name.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS));
    if (!svc) return GetLastError();
    if (CurrentState(svc.h) == SERVICE_STOPPED) return 0;
    SERVICE_STATUS st{};
    if (!ControlService(svc.h, SERVICE_CONTROL_STOP, &st)) {
        const DWORD e = GetLastError();
        if (e != ERROR_SERVICE_NOT_ACTIVE) return e;
    }
    return WaitFor(svc.h, SERVICE_STOPPED, timeoutMs, cancel);
}

DWORD StartOne(SC_HANDLE scm, const std::wstring& name, DWORD timeoutMs,
               const std::atomic<bool>& cancel, std::wstring* note) {
    ScHandle svc(OpenServiceW(scm, name.c_str(), SERVICE_START | SERVICE_QUERY_STATUS));
    if (!svc) return GetLastError();
    const DWORD state = CurrentState(svc.h);
    if (state == SERVICE_RUNNING) {
        if (note) *note = L"was already running";
        return 0;
    }
    if (!StartServiceW(svc.h, 0, nullptr)) {
        const DWORD e = GetLastError();
        if (e != ERROR_SERVICE_ALREADY_RUNNING) return e;
    }
    return WaitFor(svc.h, SERVICE_RUNNING, timeoutMs, cancel);
}

DWORD SetMode(SC_HANDLE scm, const std::wstring& name, StartMode mode) {
    ScHandle svc(OpenServiceW(scm, name.c_str(), SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG));
    if (!svc) return GetLastError();
    const DWORD type = mode == StartMode::Disabled ? SERVICE_DISABLED
                       : mode == StartMode::Manual ? SERVICE_DEMAND_START
                                                   : SERVICE_AUTO_START;
    if (!ChangeServiceConfigW(svc.h, SERVICE_NO_CHANGE, type, SERVICE_NO_CHANGE, nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr, nullptr))
        return GetLastError();
    SERVICE_DELAYED_AUTO_START_INFO delayed{mode == StartMode::AutomaticDelayed};
    if (!ChangeServiceConfig2W(svc.h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed) &&
        mode == StartMode::AutomaticDelayed)
        return GetLastError();
    return 0;
}
}  // namespace

const wchar_t* Svc::OpVerb(OpKind kind) {
    switch (kind) {
        case OpKind::Start: return L"Start";
        case OpKind::Stop: return L"Stop";
        case OpKind::Restart: return L"Restart";
        case OpKind::Pause: return L"Pause";
        case OpKind::Resume: return L"Resume";
        case OpKind::Kill: return L"Kill";
        default: return L"Change start type";
    }
}

const wchar_t* Svc::OpProgressVerb(OpKind kind) {
    switch (kind) {
        case OpKind::Start: return L"Starting";
        case OpKind::Stop: return L"Stopping";
        case OpKind::Restart: return L"Restarting";
        case OpKind::Pause: return L"Pausing";
        case OpKind::Resume: return L"Resuming";
        case OpKind::Kill: return L"Killing";
        default: return L"Changing start type of";
    }
}

OpOutcome Svc::Run(const OpRequest& req, size_t index, const std::atomic<bool>& cancel) {
    OpOutcome out;
    out.name = req.names[index];
    out.displayName = index < req.displayNames.size() ? req.displayNames[index] : out.name;
    ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm) {
        out.error = GetLastError();
        return out;
    }
    const std::wstring& name = out.name;
    const DWORD timeout = req.timeoutMs;

    switch (req.kind) {
        case OpKind::Start:
            if (req.enableFirst) {
                ScHandle q(OpenServiceW(scm.h, name.c_str(), SERVICE_QUERY_CONFIG));
                std::vector<BYTE> buf(8192);
                DWORD needed = 0;
                if (q && QueryServiceConfigW(q.h, (QUERY_SERVICE_CONFIGW*)buf.data(),
                                             (DWORD)buf.size(), &needed) &&
                    ((QUERY_SERVICE_CONFIGW*)buf.data())->dwStartType == SERVICE_DISABLED) {
                    if ((out.error = SetMode(scm.h, name, StartMode::Manual)) != 0) break;
                    out.note = L"start type set to Manual";
                }
            }
            out.error = StartOne(scm.h, name, timeout, cancel, &out.note);
            break;

        case OpKind::Stop:
        case OpKind::Restart: {
            std::vector<std::wstring> stopped;  // dependents stopped first
            {
                ScHandle svc(OpenServiceW(scm.h, name.c_str(),
                                          SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS));
                if (!svc) {
                    out.error = GetLastError();
                    break;
                }
                const std::vector<std::wstring> deps = ActiveDependents(svc.h);
                if (!deps.empty() && !req.withDependents) {
                    out.error = ERROR_DEPENDENT_SERVICES_RUNNING;
                    for (const auto& d : deps) out.dependents.push_back(DisplayNameOf(scm.h, d));
                    break;
                }
                for (const auto& d : deps) {
                    if ((out.error = StopOne(scm.h, d, timeout, cancel)) != 0) {
                        out.note = L"while stopping " + DisplayNameOf(scm.h, d);
                        break;
                    }
                    stopped.push_back(d);
                }
                if (out.error) break;
            }
            if ((out.error = StopOne(scm.h, name, timeout, cancel)) != 0) break;
            if (req.kind == OpKind::Restart) {
                if ((out.error = StartOne(scm.h, name, timeout, cancel, nullptr)) != 0) break;
                // Bring back the dependents that were running, last stopped first.
                for (auto it = stopped.rbegin(); it != stopped.rend(); ++it)
                    StartOne(scm.h, *it, timeout, cancel, nullptr);
            }
            if (!stopped.empty())
                out.note = std::to_wstring(stopped.size()) +
                           (req.kind == OpKind::Restart ? L" dependent service(s) restarted too"
                                                        : L" dependent service(s) stopped too");
            break;
        }

        case OpKind::Pause:
        case OpKind::Resume: {
            ScHandle svc(OpenServiceW(scm.h, name.c_str(), SERVICE_PAUSE_CONTINUE | SERVICE_QUERY_STATUS));
            if (!svc) {
                out.error = GetLastError();
                break;
            }
            const bool pause = req.kind == OpKind::Pause;
            const DWORD state = CurrentState(svc.h);
            if (state == (pause ? SERVICE_PAUSED : SERVICE_RUNNING)) {
                out.note = pause ? L"was already paused" : L"was already running";
                break;
            }
            SERVICE_STATUS st{};
            if (!ControlService(svc.h, pause ? SERVICE_CONTROL_PAUSE : SERVICE_CONTROL_CONTINUE, &st)) {
                out.error = GetLastError();
                break;
            }
            out.error = WaitFor(svc.h, pause ? SERVICE_PAUSED : SERVICE_RUNNING, timeout, cancel);
            break;
        }

        case OpKind::Kill: {
            ScHandle svc(OpenServiceW(scm.h, name.c_str(), SERVICE_QUERY_STATUS));
            if (!svc) {
                out.error = GetLastError();
                break;
            }
            DWORD pid = 0;
            CurrentState(svc.h, &pid);
            if (pid == 0) {
                out.error = ERROR_SERVICE_NOT_ACTIVE;
                break;
            }
            HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
            if (!proc) {
                out.error = GetLastError();
                break;
            }
            if (!TerminateProcess(proc, 1))
                out.error = GetLastError();
            else
                WaitForSingleObject(proc, std::min<DWORD>(timeout, 10000));
            CloseHandle(proc);
            if (!out.error) out.note = L"process " + std::to_wstring(pid) + L" ended";
            break;
        }

        case OpKind::SetStartMode:
            out.error = SetMode(scm.h, name, index < req.modes.size() ? req.modes[index] : req.mode);
            break;
    }
    return out;
}
