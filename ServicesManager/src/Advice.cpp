// Advice.cpp - download, cache and look up service advice (see Advice.h).
#include "Advice.h"

#include <map>
#include <memory>

#include <shlobj.h>
#include <winhttp.h>

#include "Util.h"

namespace {
// The main branch is tried first; the development branch keeps the
// feature working before it is merged.
const wchar_t* const kUrls[] = {
    L"https://raw.githubusercontent.com/akshsim1991/Project_1/main/ServicesManager/data/service-advice.tsv",
    L"https://raw.githubusercontent.com/akshsim1991/Project_1/claude/lightweight-pdf-viewer-windows-bgiawo/"
    L"ServicesManager/data/service-advice.tsv",
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::map<std::wstring, AdviceEntry> g_entries;  // lowercase service name
Advice::Source g_state = Advice::Source::None;
std::wstring g_updated, g_cachedOn;
bool g_enabled = true;
bool g_fetchedThisSession = false;

struct Lock {
    Lock() { AcquireSRWLockExclusive(&g_lock); }
    ~Lock() { ReleaseSRWLockExclusive(&g_lock); }
};

std::wstring CachePath() {
    wchar_t* base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) dir = base;
    CoTaskMemFree(base);
    if (dir.empty()) return {};
    dir += L"\\WindowsServicesManager";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\service-advice.tsv";
}

// Downloads `url` (HTTPS) into `body`. Short timeouts: this runs while the
// user waits for the details window to fill in.
bool Download(const wchar_t* url, std::string& body) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[1024];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return false;
    // Automatic proxy (Windows 8.1+), falling back to the default settings.
    HINTERNET session = WinHttpOpen(L"WindowsServicesManager/" APP_VERSION, 4 /*AUTOMATIC_PROXY*/,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(L"WindowsServicesManager/" APP_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 4000, 4000, 5000, 5000);
    bool ok = false;
    if (HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0)) {
        if (HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)) {
            DWORD status = 0, size = sizeof(status);
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr) &&
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                                    &size, nullptr) &&
                status == 200) {
                body.clear();
                for (;;) {
                    DWORD avail = 0, read = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                    const size_t old = body.size();
                    if (old + avail > (2u << 20)) break;  // the list is small; refuse anything huge
                    body.resize(old + avail);
                    if (!WinHttpReadData(req, &body[old], avail, &read)) break;
                    body.resize(old + read);
                }
                ok = !body.empty();
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return ok;
}

// Lines: "name<TAB>verdict<TAB>advice". "#updated<TAB>date" gives the date;
// other lines starting with # are comments.
bool Parse(const std::string& utf8, std::map<std::wstring, AdviceEntry>& out, std::wstring& updated) {
    std::wstring text;
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    text.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), text.data(), n);
    if (!text.empty() && text[0] == 0xFEFF) text.erase(0, 1);
    out.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;
        const size_t t1 = line.find(L'\t');
        if (line[0] == L'#') {
            if (t1 != std::wstring::npos && line.compare(0, t1, L"#updated") == 0) updated = line.substr(t1 + 1);
            continue;
        }
        const size_t t2 = t1 == std::wstring::npos ? std::wstring::npos : line.find(L'\t', t1 + 1);
        if (t2 == std::wstring::npos) continue;
        const std::wstring verdict = ToLower(line.substr(t1 + 1, t2 - t1 - 1));
        AdviceEntry e;
        e.verdict = verdict == L"safe" ? Verdict::Safe
                    : verdict == L"caution" ? Verdict::Caution
                    : verdict == L"keep" ? Verdict::Keep
                                         : Verdict::Unknown;
        e.text = line.substr(t2 + 1);
        out[ToLower(line.substr(0, t1))] = std::move(e);
    }
    return out.size() > 10;  // a real list, not an error page
}

bool LoadCache() {
    const std::wstring path = CachePath();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    std::string data;
    FILETIME written{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (2 << 20)) {
        data.resize((size_t)size.QuadPart);
        DWORD read = 0;
        if (!ReadFile(f, data.data(), (DWORD)data.size(), &read, nullptr)) data.clear();
        data.resize(read);
    }
    GetFileTime(f, nullptr, nullptr, &written);
    CloseHandle(f);
    std::map<std::wstring, AdviceEntry> entries;
    std::wstring updated;
    if (!Parse(data, entries, updated)) return false;
    SYSTEMTIME utc, local;
    wchar_t when[32] = L"";
    if (FileTimeToSystemTime(&written, &utc) && SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        swprintf_s(when, L"%04u-%02u-%02u", local.wYear, local.wMonth, local.wDay);
    Lock l;
    g_entries = std::move(entries);
    g_updated = updated;
    g_cachedOn = when;
    return true;
}

struct FetchArgs {
    HWND notify;
    UINT msg;
};

DWORD WINAPI FetchThread(LPVOID p) {
    std::unique_ptr<FetchArgs> args((FetchArgs*)p);
    std::string body;
    bool online = false;
    for (const wchar_t* url : kUrls) {
        std::map<std::wstring, AdviceEntry> entries;
        std::wstring updated;
        if (Download(url, body) && Parse(body, entries, updated)) {
            // Save for offline use: write a new file, then swap it in.
            const std::wstring path = CachePath();
            if (!path.empty()) {
                const std::wstring tmp = path + L".new";
                HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    const bool ok = WriteFile(f, body.data(), (DWORD)body.size(), &written, nullptr) &&
                                    written == body.size();
                    CloseHandle(f);
                    if (ok) MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
                    else DeleteFileW(tmp.c_str());
                }
            }
            Lock l;
            g_entries = std::move(entries);
            g_updated = updated;
            g_cachedOn.clear();
            g_state = Advice::Source::Online;
            online = true;
            break;
        }
    }
    if (!online) {
        const bool cached = LoadCache();
        Lock l;
        g_state = cached ? Advice::Source::Cached : Advice::Source::Offline;
    }
    if (args->notify && IsWindow(args->notify)) PostMessageW(args->notify, args->msg, 0, 0);
    return 0;
}
}  // namespace

void Advice::SetEnabled(bool enabled) {
    Lock l;
    g_enabled = enabled;
    if (!enabled) g_state = Source::Disabled;
    else if (g_state == Source::Disabled) g_state = Source::None;
}

void Advice::Fetch(HWND notify, UINT msg, bool force) {
    {
        Lock l;
        if (!g_enabled) {
            g_state = Source::Disabled;
            return;
        }
        if (g_state == Source::Loading) return;
        if (g_fetchedThisSession && !force) return;
        g_fetchedThisSession = true;
        g_state = Source::Loading;
    }
    auto* args = new FetchArgs{notify, msg};
    HANDLE t = CreateThread(nullptr, 0, FetchThread, args, 0, nullptr);
    if (t) {
        CloseHandle(t);
    } else {
        delete args;
        Lock l;
        g_state = Source::Offline;
    }
}

Advice::Source Advice::State() {
    Lock l;
    return g_state;
}

std::wstring Advice::Updated() {
    Lock l;
    return g_updated;
}

std::wstring Advice::CachedOn() {
    Lock l;
    return g_cachedOn;
}

bool Advice::Lookup(const std::wstring& serviceName, AdviceEntry& out) {
    std::wstring key = ToLower(serviceName);
    Lock l;
    auto it = g_entries.find(key);
    if (it == g_entries.end()) {
        // Per-user services carry a suffix: "cdpusersvc_1a2b3c" -> "cdpusersvc".
        const size_t us = key.rfind(L'_');
        if (us != std::wstring::npos && us > 0) it = g_entries.find(key.substr(0, us));
    }
    if (it == g_entries.end()) return false;
    out = it->second;
    return true;
}

const wchar_t* Advice::VerdictTitle(Verdict v) {
    switch (v) {
        case Verdict::Safe: return L"Usually safe to disable";
        case Verdict::Caution: return L"Disable only if you don't need it";
        case Verdict::Keep: return L"Do not disable";
        default: return L"No advice";
    }
}

std::wstring Advice::SourceNote() {
    return State() == Source::Cached
               ? L"(No internet connection: showing the advice saved on " + CachedOn() + L".)"
               : std::wstring();
}

std::wstring Advice::Describe(const std::wstring& serviceName, bool sourceNote) {
    switch (State()) {
        case Source::Disabled:
            return L"Online advice is turned off in Settings.";
        case Source::None:
        case Source::Loading:
            return L"Checking online advice\x2026";
        case Source::Offline:
            return L"Online advice is not available: no internet connection.";
        default: break;
    }
    AdviceEntry e;
    std::wstring text;
    if (Lookup(serviceName, e))
        text = std::wstring(VerdictTitle(e.verdict)) + L". " + e.text;
    else
        text = L"This service is not in the advice list. If you don't know what it does, leave it as it is, "
               L"or use \x201CSearch online\x201D.";
    if (sourceNote && State() == Source::Cached) text += L"\r\n" + SourceNote();
    return text;
}
