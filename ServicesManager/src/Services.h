// Services.h - everything that talks to the Windows Service Control Manager:
// listing services with their configuration, the actions (start, stop,
// restart, pause, resume, kill, change start type) and friendly error text.
//
// These functions block (an action waits until the service reaches its new
// state), so they run on the Worker thread, never on the UI thread.
#pragma once
#include <atomic>
#include <map>

#include "Common.h"

// How careful the user should be with a service.
enum class Safety {
    Critical,    // Windows may become unstable or stop booting without it
    Windows,     // part of Windows: features stop working without it
    ThirdParty,  // installed by other software
};

enum class StartMode { Automatic, AutomaticDelayed, Manual, Disabled };

struct ServiceInfo {
    std::wstring name;         // key name, e.g. "Spooler"
    std::wstring displayName;  // e.g. "Print Spooler"
    std::wstring description;
    std::wstring binaryPath;   // command line as registered
    std::wstring imagePath;    // just the program file, environment expanded
    std::wstring account;      // "Local System", ".\\user", ...
    std::wstring company;      // CompanyName from the program's version info
    DWORD state = 0;           // SERVICE_RUNNING, SERVICE_STOPPED, ...
    DWORD pid = 0;
    DWORD startType = SERVICE_DEMAND_START;
    DWORD controls = 0;        // SERVICE_ACCEPT_* flags
    DWORD type = 0;            // SERVICE_WIN32_OWN_PROCESS / SHARE_PROCESS ...
    bool delayed = false;      // automatic (delayed start)
    bool triggered = false;    // has trigger-start conditions
    bool configKnown = false;  // false if the configuration was not readable
    Safety safety = Safety::Windows;
    std::vector<std::wstring> dependencies;  // services (and "+group"s) it needs
    DWORD bootDelayMs = 0;     // worst start-up delay Windows recorded for it
    int bootDelayCount = 0;    // how many recent start-ups it slowed down
    unsigned warnings = 0;     // kWarn* flags
};

// Things worth a closer look (malware and broken installs often show them).
enum : unsigned {
    kWarnMissingFile = 1,  // the program file does not exist
    kWarnUserFolder = 2,   // runs from a user, AppData or Temp folder
    kWarnUnsigned = 4,     // third-party program without a digital signature
    kWarnBadSignature = 8, // signature broken or not trusted
    kWarnUnquoted = 16,    // path with spaces but no quotes (a known security hole)
};

struct Snapshot {
    std::vector<ServiceInfo> services;
    DWORD error = 0;  // non-zero if the list could not be read
};

// Facts about program files, kept between refreshes (reading version
// information and checking signatures are the slowest part of a refresh).
struct EnumCache {
    std::map<std::wstring, std::wstring> companies;  // file -> CompanyName
    std::map<std::wstring, int> signatures;          // file -> SignState
    std::map<std::wstring, std::pair<DWORD, int>> bootDelays;  // lowercase name -> (worst ms, count)
    bool bootDelaysRead = false;
};

namespace Svc {
bool Enumerate(std::vector<ServiceInfo>& out, DWORD& error, EnumCache& cache);
std::wstring WarningsText(unsigned warnings);           // short, for the list
std::wstring WarningsExplained(unsigned warnings);      // one line per warning

std::wstring StateText(DWORD state);
std::wstring StartTypeText(const ServiceInfo& s);
const wchar_t* SafetyText(Safety s);
bool IsCritical(const std::wstring& name);
bool IsPending(DWORD state);

// Friendly explanation of a Win32/service error code.
std::wstring ErrorText(DWORD error);
}  // namespace Svc

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
enum class OpKind { Start, Stop, Restart, Pause, Resume, Kill, SetStartMode };

struct OpRequest {
    OpKind kind = OpKind::Start;
    std::vector<std::wstring> names;         // services to act on, in order
    std::vector<std::wstring> displayNames;  // for progress and results
    StartMode mode = StartMode::Manual;      // SetStartMode (all services)...
    std::vector<StartMode> modes;            // ...or one per service (profiles, restore)
    bool withDependents = false;  // Stop/Restart: stop running dependents first
    bool enableFirst = false;     // Start: set disabled services to Manual first
    DWORD timeoutMs = 30000;      // per service
};

struct OpOutcome {
    std::wstring name, displayName;
    DWORD error = 0;                      // 0 = success
    std::wstring note;                    // e.g. "was already running"
    std::vector<std::wstring> dependents; // ERROR_DEPENDENT_SERVICES_RUNNING
};

struct OpProgress {
    OpKind kind = OpKind::Start;
    int index = 0, total = 0;
    std::wstring displayName;
};

struct OpBatchResult {
    OpRequest request;
    std::vector<OpOutcome> outcomes;
    bool cancelled = false;
};

namespace Svc {
// Performs one action on one service and waits for its result.
OpOutcome Run(const OpRequest& req, size_t index, const std::atomic<bool>& cancel);
const wchar_t* OpVerb(OpKind kind);      // "Start", "Stop", ...
const wchar_t* OpProgressVerb(OpKind kind);  // "Starting", ...
}  // namespace Svc
