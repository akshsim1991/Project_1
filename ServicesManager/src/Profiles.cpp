// Profiles.cpp - profile and snapshot files (see Profiles.h).
#include "Profiles.h"

#include <algorithm>

#include <shlobj.h>

#include "Util.h"

namespace {
bool ParseMode(const std::wstring& token, StartMode& m) {
    const std::wstring t = ToLower(token);
    if (t == L"automatic") m = StartMode::Automatic;
    else if (t == L"automaticdelayed") m = StartMode::AutomaticDelayed;
    else if (t == L"manual") m = StartMode::Manual;
    else if (t == L"disabled") m = StartMode::Disabled;
    else return false;
    return true;
}

std::wstring Trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    const size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

Profile Make(const wchar_t* title, const wchar_t* description,
             std::initializer_list<std::pair<const wchar_t*, StartMode>> items) {
    Profile p;
    p.title = title;
    p.description = description;
    for (const auto& it : items) p.entries.push_back({it.first, it.second});
    return p;
}
}  // namespace

std::wstring Profiles::Folder(const wchar_t* sub) {
    wchar_t* base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) dir = base;
    CoTaskMemFree(base);
    if (dir.empty()) return {};
    dir += L"\\WindowsServicesManager";
    CreateDirectoryW(dir.c_str(), nullptr);
    if (sub && *sub) {
        // "Snapshots\\Automatic" -> create each level.
        std::wstring rel = sub;
        size_t pos = 0;
        for (;;) {
            const size_t next = rel.find(L'\\', pos);
            CreateDirectoryW((dir + L"\\" + rel.substr(0, next)).c_str(), nullptr);
            if (next == std::wstring::npos) break;
            pos = next + 1;
        }
        dir += L"\\" + rel;
    }
    return dir;
}

const wchar_t* Profiles::ModeToken(StartMode m) {
    switch (m) {
        case StartMode::Automatic: return L"Automatic";
        case StartMode::AutomaticDelayed: return L"AutomaticDelayed";
        case StartMode::Manual: return L"Manual";
        default: return L"Disabled";
    }
}

const wchar_t* Profiles::ModeTitle(StartMode m) {
    switch (m) {
        case StartMode::Automatic: return L"Automatic";
        case StartMode::AutomaticDelayed: return L"Automatic (delayed start)";
        case StartMode::Manual: return L"Manual";
        default: return L"Disabled";
    }
}

bool Profiles::ModeOf(const ServiceInfo& s, StartMode& mode) {
    if (!s.configKnown) return false;
    switch (s.startType) {
        case SERVICE_AUTO_START: mode = s.delayed ? StartMode::AutomaticDelayed : StartMode::Automatic; return true;
        case SERVICE_DEMAND_START: mode = StartMode::Manual; return true;
        case SERVICE_DISABLED: mode = StartMode::Disabled; return true;
        default: return false;
    }
}

std::wstring Profiles::Timestamp(bool forFileName) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t buf[64];
    if (forFileName)
        swprintf_s(buf, L"%04u-%02u-%02u %02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else
        swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    return buf;
}

bool Profiles::Save(const std::wstring& path, const Profile& p) {
    std::wstring text = L"; Windows Services Manager start-type profile\r\n";
    text += L"title=" + p.title + L"\r\n";
    if (!p.description.empty()) {
        std::wstring d = p.description;
        std::replace(d.begin(), d.end(), L'\n', L' ');
        std::replace(d.begin(), d.end(), L'\r', L' ');
        text += L"description=" + d + L"\r\n";
    }
    text += L"[services]\r\n";
    for (const ProfileEntry& e : p.entries) text += e.name + L"=" + ModeToken(e.mode) + L"\r\n";
    const std::string utf8 = "\xEF\xBB\xBF" + ToUtf8(text);
    // Written under a temporary name first, so a crash never leaves half a file.
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr) && written == utf8.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool Profiles::Load(const std::wstring& path, Profile& p) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    std::string data;
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (4 << 20)) {
        data.resize((size_t)size.QuadPart);
        DWORD read = 0;
        if (!ReadFile(f, data.data(), (DWORD)data.size(), &read, nullptr)) read = 0;
        data.resize(read);
    }
    CloseHandle(f);
    if (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
    std::wstring text((size_t)MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), text.data(), (int)text.size());
    p = Profile{};
    p.file = path;
    bool services = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring line = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            services = _wcsicmp(line.c_str(), L"[services]") == 0;
            continue;
        }
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (!services) {
            if (_wcsicmp(key.c_str(), L"title") == 0) p.title = value;
            if (_wcsicmp(key.c_str(), L"description") == 0) p.description = value;
            continue;
        }
        ProfileEntry e;
        e.name = key;
        if (!key.empty() && ParseMode(value, e.mode)) p.entries.push_back(e);
    }
    if (p.title.empty()) {
        const size_t slash = path.find_last_of(L"\\/");
        p.title = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
        if (p.title.size() > 4) p.title.resize(p.title.size() - 4);
    }
    return !p.entries.empty();
}

std::vector<Profile> Profiles::BuiltIn() {
    // Conservative presets: only services that are not needed for normal
    // use, or that only matter for a specific feature named in the title.
    // Every profile is previewed before anything changes, and an automatic
    // snapshot makes it easy to go back.
    return {
        Make(L"Privacy: reduce telemetry",
             L"Stops sending diagnostic and usage data to Microsoft and turns off error reporting. "
             L"Normal use is not affected.",
             {{L"DiagTrack", StartMode::Disabled},
              {L"dmwappushservice", StartMode::Disabled},
              {L"WerSvc", StartMode::Disabled},
              {L"wercplsupport", StartMode::Manual},
              {L"InventorySvc", StartMode::Manual}}),
        Make(L"Lighter: fewer background services",
             L"Disables services almost nobody needs: shop demo mode, offline maps, fax, media sharing, "
             L"remote registry, phone/NFC payments and old peer-to-peer features.",
             {{L"RetailDemo", StartMode::Disabled},
              {L"MapsBroker", StartMode::Disabled},
              {L"Fax", StartMode::Disabled},
              {L"WMPNetworkSvc", StartMode::Disabled},
              {L"RemoteRegistry", StartMode::Disabled},
              {L"SEMgrSvc", StartMode::Disabled},
              {L"AJRouter", StartMode::Disabled},
              {L"PcaSvc", StartMode::Manual},
              {L"wisvc", StartMode::Disabled},
              {L"WalletService", StartMode::Disabled}}),
        Make(L"No Xbox (not a gamer)",
             L"Disables the Xbox services. Choose this only if you don't play Xbox or Game Pass games "
             L"and don't use an Xbox controller.",
             {{L"XblAuthManager", StartMode::Disabled},
              {L"XblGameSave", StartMode::Disabled},
              {L"XboxNetApiSvc", StartMode::Disabled},
              {L"XboxGipSvc", StartMode::Disabled}}),
        Make(L"No printer or scanner",
             L"Disables printing, fax and scanning. Note: \x201CPrint to PDF\x201D also stops working.",
             {{L"Spooler", StartMode::Disabled},
              {L"PrintNotify", StartMode::Disabled},
              {L"Fax", StartMode::Disabled},
              {L"stisvc", StartMode::Manual}}),
        Make(L"Security hardening",
             L"Turns off remote access features you don't use at home: remote registry, remote desktop "
             L"and remote management.",
             {{L"RemoteRegistry", StartMode::Disabled},
              {L"TermService", StartMode::Manual},
              {L"WinRM", StartMode::Manual},
              {L"RemoteAccess", StartMode::Disabled}}),
    };
}

std::vector<Profile> Profiles::Custom() {
    std::vector<Profile> out;
    const std::wstring dir = Folder(L"Profiles");
    if (dir.empty()) return out;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*.wsm").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        Profile p;
        if (Load(dir + L"\\" + fd.cFileName, p)) out.push_back(std::move(p));
    } while (FindNextFileW(find, &fd) && out.size() < 50);
    FindClose(find);
    std::sort(out.begin(), out.end(), [](const Profile& a, const Profile& b) { return a.title < b.title; });
    return out;
}

Profile Profiles::FromServices(const std::vector<const ServiceInfo*>& services, const std::wstring& title) {
    Profile p;
    p.title = title;
    for (const ServiceInfo* s : services) {
        StartMode m;
        if (ModeOf(*s, m)) p.entries.push_back({s->name, m});
    }
    return p;
}

std::wstring Profiles::AutoSnapshot(const std::vector<ServiceInfo>& all, const std::wstring& reason) {
    const std::wstring dir = Folder(L"Snapshots\\Automatic");
    if (dir.empty() || all.empty()) return {};
    std::vector<const ServiceInfo*> ptrs;
    for (const auto& s : all) ptrs.push_back(&s);
    Profile p = FromServices(ptrs, L"Automatic snapshot " + Timestamp(false));
    p.description = L"Taken before: " + reason;
    const std::wstring path = dir + L"\\" + Timestamp(true) + L".wsm";
    if (!Save(path, p)) return {};
    // Keep the newest 30 (names sort by time).
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*.wsm").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do files.push_back(fd.cFileName);
        while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i + 30 < files.size(); ++i) DeleteFileW((dir + L"\\" + files[i]).c_str());
    return path;
}
