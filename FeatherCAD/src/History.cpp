// History.cpp - see History.h.
#include "History.h"

#include <algorithm>
#include <cwctype>

#include "Util.h"

namespace {
const wchar_t kSourceFile[] = L"source.txt";

std::wstring Root() { return AppDataDir(L"History"); }

uint64_t Fnv1a(const std::wstring& s) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : s) {
        h ^= (uint64_t)(unsigned)towlower(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<VersionInfo> ListVersions(const std::wstring& folder) {
    std::vector<VersionInfo> out;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((folder + L"\\*.fcad").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        VersionInfo v;
        v.file = folder + L"\\" + fd.cFileName;
        v.time = fd.ftLastWriteTime;
        v.autosave = wcsstr(fd.cFileName, L"-auto") != nullptr;
        v.bytes = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        out.push_back(v);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    // Names start with the time, so name order is time order.
    std::sort(out.begin(), out.end(), [](const VersionInfo& a, const VersionInfo& b) { return a.file > b.file; });
    return out;
}

void DeleteFolder(const std::wstring& folder) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((folder + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    RemoveDirectoryW(folder.c_str());
}
}  // namespace

void History::SetDrawing(const std::wstring& path, const std::wstring& title) {
    wchar_t name[64];
    if (path.empty()) {
        static LONG counter = 0;
        swprintf(name, 64, L"untitled-%lu-%llx-%ld", GetCurrentProcessId(), (unsigned long long)GetTickCount64(),
                 InterlockedIncrement(&counter));
    } else {
        swprintf(name, 64, L"%016llx", (unsigned long long)Fnv1a(FullPath(path)));
    }
    m_folder = Root() + L"\\" + name;
    m_source = path;
    m_title = title;
    m_described = false;  // the folder is made with the first version
}

bool History::AddVersion(const std::string& text, bool autosave, int keep) {
    if (m_folder.empty()) return false;
    if (!m_described) {
        CreateDirectoryW(m_folder.c_str(), nullptr);
        // First line: the drawing's path (empty if unsaved); second: its name.
        m_described = WriteFileSafely(m_folder + L"\\" + kSourceFile, WideToUtf8(m_source + L"\n" + m_title + L"\n"));
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[80];
    swprintf(name, 80, L"\\%04u%02u%02u-%02u%02u%02u-%03u-%ls.fcad", st.wYear, st.wMonth, st.wDay, st.wHour,
             st.wMinute, st.wSecond, st.wMilliseconds, autosave ? L"auto" : L"save");
    if (!WriteFileSafely(m_folder + name, text)) return false;
    std::vector<VersionInfo> all = ListVersions(m_folder);
    for (size_t i = (size_t)std::max(keep, 1); i < all.size(); ++i) DeleteFileW(all[i].file.c_str());
    return true;
}

std::vector<VersionInfo> History::Versions() const {
    return m_folder.empty() ? std::vector<VersionInfo>{} : ListVersions(m_folder);
}

std::vector<HistoryDrawing> History::AllDrawings() {
    std::vector<HistoryDrawing> out;
    const std::wstring root = Root();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        HistoryDrawing d;
        d.folder = root + L"\\" + fd.cFileName;
        std::vector<VersionInfo> versions = ListVersions(d.folder);
        if (versions.empty()) continue;
        d.latest = versions.front();
        std::string src;
        if (ReadWholeFile(d.folder + L"\\" + kSourceFile, src)) {
            std::wstring s = Utf8ToWide(src);
            size_t nl = s.find(L'\n');
            d.source = s.substr(0, nl);
            if (nl != std::wstring::npos) {
                d.title = s.substr(nl + 1);
                while (!d.title.empty() && (d.title.back() == L'\n' || d.title.back() == L'\r')) d.title.pop_back();
            }
        }
        if (d.title.empty()) d.title = d.source.empty() ? L"Untitled drawing" : FileNameFromPath(d.source);
        out.push_back(std::move(d));
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(out.begin(), out.end(), [](const HistoryDrawing& a, const HistoryDrawing& b) {
        return CompareFileTime(&a.latest.time, &b.latest.time) > 0;
    });
    return out;
}

void History::CleanOld(int days) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER limit;
    limit.LowPart = now.dwLowDateTime;
    limit.HighPart = now.dwHighDateTime;
    limit.QuadPart -= (ULONGLONG)days * 24 * 3600 * 10000000ull;
    const std::wstring root = Root();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> stale;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        const std::wstring folder = root + L"\\" + fd.cFileName;
        const std::vector<VersionInfo> versions = ListVersions(folder);
        ULARGE_INTEGER t{};
        if (!versions.empty()) {
            t.LowPart = versions.front().time.dwLowDateTime;
            t.HighPart = versions.front().time.dwHighDateTime;
        }
        if (versions.empty() || t.QuadPart < limit.QuadPart) stale.push_back(folder);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    for (const std::wstring& folder : stale) DeleteFolder(folder);
}
