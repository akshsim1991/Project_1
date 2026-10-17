// Details.h - the service details window (double-click a service) and the
// summary for several selected services.
//
// Details is a property sheet with five tabs:
//   General        everything about the service, with "can it be disabled?"
//                  advice, process memory/CPU, signature and warnings
//   Configuration  display name, description, start type, log-on account
//   Recovery       what Windows does when the service fails
//   Dependencies   services it needs and services that need it (a tree)
//   History        recent Service Control Manager events about it
#pragma once
#include <functional>

#include "Inspect.h"
#include "Services.h"

struct DetailsContext {
    ServiceInfo service;
    std::vector<ServiceInfo> all;  // the whole list: dependencies, shared processes
    bool elevated = false;
    bool protectCritical = true;
    HWND main = nullptr;           // gets ID_REFRESH after a change
    // Called after the start type was changed here (for undo and snapshots).
    std::function<void(const ServiceInfo& before)> onStartTypeChanged;

    // Filled in by a background thread for the General tab.
    bool factsLoaded = false;
    Signature signature;
    Inspect::ProcessStats stats;
    std::wstring version;
    Inspect::RecoveryConfig recovery;
};

void ShowServiceDetails(HINSTANCE inst, HWND owner, DetailsContext& ctx);
void ShowServicesSummary(HINSTANCE inst, HWND owner, const std::vector<ServiceInfo>& services);

// Opens https://www.virustotal.com for the file's SHA-256 (only the hash
// is sent, never the file).
void OpenVirusTotal(HWND owner, const std::wstring& file);
void SearchServiceOnline(HWND owner, const ServiceInfo& s);
