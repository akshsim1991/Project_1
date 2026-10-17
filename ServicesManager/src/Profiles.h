// Profiles.h - start-type profiles and snapshots.
//
// Both are the same simple text file: a list of "service=StartType" lines.
// A snapshot holds every service (taken by hand, or automatically before
// every start-type change); a profile holds just the services it changes.
// Files live in %LOCALAPPDATA%\WindowsServicesManager\Profiles and
// ...\Snapshots, so they can be copied to another PC.
#pragma once
#include "Services.h"

struct ProfileEntry {
    std::wstring name;
    StartMode mode = StartMode::Manual;
};

struct Profile {
    std::wstring title;
    std::wstring description;
    std::wstring file;  // empty for built-in profiles
    std::vector<ProfileEntry> entries;
};

namespace Profiles {
std::wstring Folder(const wchar_t* sub);  // created if needed
bool Save(const std::wstring& path, const Profile& p);
bool Load(const std::wstring& path, Profile& p);

std::vector<Profile> BuiltIn();
std::vector<Profile> Custom();  // *.wsm in the Profiles folder

// The start type of a service as a profile entry (false for drivers'
// boot/system types or unreadable configuration).
bool ModeOf(const ServiceInfo& s, StartMode& mode);
const wchar_t* ModeToken(StartMode m);  // "Automatic", "AutomaticDelayed", ...
const wchar_t* ModeTitle(StartMode m);  // "Automatic (delayed start)", ...

Profile FromServices(const std::vector<const ServiceInfo*>& services, const std::wstring& title);
// Saves all services to Snapshots\Automatic and keeps the newest 30.
// Returns the file written (empty on failure).
std::wstring AutoSnapshot(const std::vector<ServiceInfo>& all, const std::wstring& reason);
std::wstring Timestamp(bool forFileName);
}  // namespace Profiles
