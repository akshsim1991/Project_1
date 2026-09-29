// Util.cpp - small helpers shared by all modules.
#include "Util.h"

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

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
