// Inspect.h - deeper information about one service or its program file:
// digital signature, file hash, process memory/CPU, recovery settings,
// event-log history and boot delays, plus writing configuration changes.
//
// Most of these block for a moment (file and event-log access), so the UI
// calls them from background threads.
#pragma once
#include <map>

#include "Common.h"

// --- program file -------------------------------------------------------------
enum class SignState { Unknown, Signed, Unsigned, Invalid };

struct Signature {
    SignState state = SignState::Unknown;
    std::wstring signer;  // certificate subject, e.g. "Microsoft Windows"
    bool catalog = false; // signed through a Windows catalog file
};

namespace Inspect {
Signature CheckSignature(const std::wstring& file);
std::wstring Sha256(const std::wstring& file);  // lowercase hex, empty on error
std::wstring FileVersion(const std::wstring& file);

// --- process ------------------------------------------------------------------
struct ProcessStats {
    bool valid = false;
    unsigned long long workingSet = 0, privateBytes = 0;  // bytes
    unsigned long long cpuMs = 0;                          // kernel + user
    unsigned long long uptimeSec = 0;
};
ProcessStats QueryProcess(DWORD pid);

// --- recovery (failure actions) -------------------------------------------------
enum class Recovery { None = 0, Restart = 1, Reboot = 2, RunCommand = 3 };  // SC_ACTION_TYPE values

struct RecoveryConfig {
    bool valid = false;
    Recovery action[3] = {Recovery::None, Recovery::None, Recovery::None};  // 1st, 2nd, later
    DWORD delayMs[3] = {60000, 60000, 60000};
    DWORD resetSeconds = 86400;
    std::wstring command;
    bool nonCrashFailures = false;  // also act when the service stops with an error
};
RecoveryConfig ReadRecovery(const std::wstring& service);
DWORD WriteRecovery(const std::wstring& service, const RecoveryConfig& rc);
std::wstring RecoveryText(Recovery r);

// --- configuration ------------------------------------------------------------
struct ConfigChange {
    std::wstring displayName;          // empty: unchanged
    bool setDescription = false;
    std::wstring description;
    bool setStartType = false;
    DWORD startType = SERVICE_DEMAND_START;
    bool delayed = false;
    bool setAccount = false;
    std::wstring account;              // "LocalSystem", "NT AUTHORITY\\LocalService", ".\\user"
    std::wstring password;
};
DWORD WriteConfig(const std::wstring& service, const ConfigChange& c);

// --- history -----------------------------------------------------------------
struct EventEntry {
    FILETIME time{};
    int level = 4;  // 1 critical, 2 error, 3 warning, 4 information
    DWORD id = 0;
    std::wstring message;
};
// Recent Service Control Manager events that mention the service (newest
// first). Returns false if the event log could not be read.
bool RecentEvents(const std::wstring& name, const std::wstring& displayName, size_t max,
                  std::vector<EventEntry>& out, DWORD& error);

// Boot delays recorded by Windows ("this service caused a delay in the
// system start up", Diagnostics-Performance event 103): service name
// (lowercase) -> worst delay in milliseconds and how often it happened.
struct BootDelay {
    DWORD worstMs = 0;
    int count = 0;
};
bool ReadBootDelays(std::map<std::wstring, BootDelay>& out);

std::wstring FormatTime(const FILETIME& ft);   // local "2026-10-02 14:05"
std::wstring FormatBytes(unsigned long long b);
std::wstring FormatDuration(unsigned long long seconds);
}  // namespace Inspect
