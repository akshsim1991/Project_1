// Util.cpp - small helpers shared by all modules.
#include "Util.h"

#include <shlobj.h>

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
    // GetDpiForWindow exists on Windows 10 1607+; resolved at run time.
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

std::wstring StripExtension(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    size_t slash = name.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return name;
    return name.substr(0, dot);
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

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

std::wstring FullPath(const std::wstring& path) {
    DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!n) return path;
    std::wstring full(n, L'\0');
    n = GetFullPathNameW(path.c_str(), n, full.data(), nullptr);
    full.resize(n);
    return full.empty() ? path : full;
}

bool ReadWholeFile(const std::wstring& path, std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1LL << 31);
    if (ok) {
        data.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = data.empty() || (ReadFile(f, data.data(), (DWORD)data.size(), &read, nullptr) && read == data.size());
    }
    CloseHandle(f);
    return ok;
}

std::wstring LastErrorText(DWORD code) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::wstring text = msg ? msg : L"";
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    if (text.empty()) text = L"Windows error " + std::to_wstring(code) + L".";
    return text;
}

bool WriteFileSafely(const std::wstring& path, const std::string& data, std::wstring* error) {
    const std::wstring temp = path + L".saving";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        if (error) *error = LastErrorText(GetLastError());
        return false;
    }
    DWORD written = 0;
    bool ok = data.empty() || (WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size());
    DWORD code = ok ? 0 : GetLastError();
    ok = ok && FlushFileBuffers(f);
    if (!ok && !code) code = GetLastError();
    CloseHandle(f);
    if (ok && !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
        code = GetLastError();
    }
    if (!ok) {
        DeleteFileW(temp.c_str());
        if (error) *error = LastErrorText(code);
    }
    return ok;
}

std::wstring AppDataDir(const wchar_t* sub) {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &base))) dir = base;
    if (base) CoTaskMemFree(base);
    if (dir.empty()) {
        wchar_t tmp[MAX_PATH + 1];
        DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
        dir = std::wstring(tmp, n);
        while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
    }
    dir += L"\\FeatherCAD";
    CreateDirectoryW(dir.c_str(), nullptr);
    if (sub && *sub) {
        dir += L"\\";
        dir += sub;
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    return dir;
}

std::wstring FormatFileTime(const FILETIME& ft) {
    FILETIME local;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return L"";
    wchar_t date[64] = {}, time[64] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, 64);
    return std::wstring(date) + L" " + time;
}
