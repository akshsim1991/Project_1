// Util.cpp - small helpers shared by all modules.
#include "Util.h"

#include <cwctype>

#include <shellapi.h>

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

int GetWindowDpi(HWND hwnd) {
    // GetDpiForWindow exists on Windows 10 1607+. Resolve it dynamically so
    // the executable still starts on older builds.
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi) return (int)dpi;
    }
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    return dpi > 0 ? dpi : 96;
}

HFONT CreateMessageFont(int dpi, int sizeAdjustPercent) {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    using Fn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi");
    if (fn) {
        fn(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, (UINT)dpi);
    } else {
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, dpi, GetWindowDpi(nullptr));
    }
    ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, sizeAdjustPercent, 100);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

std::wstring FileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring DirectoryFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

std::wstring ExecutablePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

bool OpenExternalLink(HWND owner, const std::wstring& uri) {
    auto starts = [&](const wchar_t* prefix) {
        const size_t n = wcslen(prefix);
        return uri.size() > n && _wcsnicmp(uri.c_str(), prefix, n) == 0;
    };
    if (!(starts(L"http://") || starts(L"https://") || starts(L"mailto:"))) return false;
    ShellExecuteW(owner, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return true;
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) ==
           CSTR_EQUAL;
}

namespace {
std::wstring TempDir() {
    wchar_t buf[MAX_PATH + 1];
    const DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    std::wstring dir = (n && n <= MAX_PATH) ? std::wstring(buf, n) : std::wstring(L".\\");
    dir += L"FeatherPDF";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
}  // namespace

std::wstring MakeTempPdfPath() {
    static LONG counter = 0;
    wchar_t name[64];
    swprintf_s(name, L"\\%lu-%llx-%ld.pdf", GetCurrentProcessId(), GetTickCount64(),
               InterlockedIncrement(&counter));
    return TempDir() + name;
}

void CleanOldTempFiles() {
    const std::wstring dir = TempDir();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*.pdf").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG nowT = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    const ULONGLONG twoDays = 2ull * 24 * 3600 * 10000000;
    do {
        const ULONGLONG t = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) |
                            fd.ftLastWriteTime.dwLowDateTime;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && nowT > t + twoDays)
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

bool ParsePageRanges(const std::wstring& text, int pageCount, std::vector<int>& pages) {
    pages.clear();
    size_t i = 0;
    auto skipSpace = [&] {
        while (i < text.size() && iswspace(text[i])) ++i;
    };
    auto number = [&](int& out) {
        skipSpace();
        if (i >= text.size() || !iswdigit(text[i])) return false;
        long v = 0;
        while (i < text.size() && iswdigit(text[i])) {
            v = v * 10 + (text[i++] - L'0');
            if (v > 1000000) return false;
        }
        out = (int)v;
        return true;
    };
    for (;;) {
        skipSpace();
        if (i >= text.size()) break;
        int from, to;
        if (!number(from)) return false;
        to = from;
        skipSpace();
        if (i < text.size() && (text[i] == L'-' || text[i] == L'\x2013')) {
            ++i;
            skipSpace();
            if (i >= text.size() || text[i] == L',' || text[i] == L';')
                to = pageCount;  // "8-": to the end
            else if (!number(to))
                return false;
        }
        if (from < 1 || to < 1 || from > pageCount || to > pageCount) return false;
        const int step = from <= to ? 1 : -1;
        for (int p = from;; p += step) {
            pages.push_back(p - 1);
            if (p == to) break;
        }
        skipSpace();
        if (i < text.size()) {
            if (text[i] != L',' && text[i] != L';') return false;
            ++i;
        }
    }
    return !pages.empty();
}

std::wstring FormatPageRanges(const std::vector<int>& pages) {
    std::wstring out;
    for (size_t i = 0; i < pages.size();) {
        size_t j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) ++j;
        if (!out.empty()) out += L", ";
        out += std::to_wstring(pages[i] + 1);
        if (j > i) out += L"-" + std::to_wstring(pages[j] + 1);
        i = j + 1;
    }
    return out;
}

std::wstring FormatPdfDate(const std::wstring& date) {
    // D:YYYYMMDDHHmmSS followed by Z, +HH'mm' or nothing.
    std::wstring d = date;
    if (d.rfind(L"D:", 0) == 0) d = d.substr(2);
    if (d.size() < 8) return {};
    for (size_t i = 0; i < 8; ++i)
        if (!iswdigit(d[i])) return {};
    auto num = [&](size_t at, size_t len) {
        return at + len <= d.size() ? _wtoi(d.substr(at, len).c_str()) : 0;
    };
    SYSTEMTIME st{};
    st.wYear = (WORD)num(0, 4);
    st.wMonth = (WORD)num(4, 2);
    st.wDay = (WORD)num(6, 2);
    st.wHour = (WORD)num(8, 2);
    st.wMinute = (WORD)num(10, 2);
    if (st.wMonth < 1 || st.wMonth > 12 || st.wDay < 1 || st.wDay > 31) return {};
    // Times written in UTC ("Z") are shown in local time.
    if (d.size() > 14 && d[14] == L'Z') {
        SYSTEMTIME local;
        if (SystemTimeToTzSpecificLocalTime(nullptr, &st, &local)) st = local;
    }
    wchar_t dateText[64] = L"", timeText[32] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, dateText, 64, nullptr);
    if (d.size() >= 12) {
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, timeText, 32);
        return std::wstring(dateText) + L" " + timeText;
    }
    return dateText;
}

std::wstring CurrentUserName() {
    wchar_t name[256] = L"";
    DWORD len = 256;
    if (!GetUserNameW(name, &len)) return L"";
    return name;
}
