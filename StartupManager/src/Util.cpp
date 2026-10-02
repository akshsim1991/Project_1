// Util.cpp - small helpers shared by all modules.
#include "Util.h"

#include <algorithm>
#include <cwctype>

#include <shellapi.h>

int GetWindowDpi(HWND hwnd) {
    // GetDpiForWindow exists on Windows 10 1607+; resolved dynamically so
    // the program still starts on older builds.
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

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needleLower) {
    if (needleLower.empty()) return true;
    if (haystack.size() < needleLower.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needleLower.begin(), needleLower.end(),
                          [](wchar_t a, wchar_t b) { return towlower(a) == b; });
    return it != haystack.end();
}

std::wstring GetWindowString(HWND hwnd) {
    int n = GetWindowTextLengthW(hwnd);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(hwnd, s.data(), n + 1);
    s.resize((size_t)n);
    return s;
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

bool RelaunchElevated(HWND owner) {
    const std::wstring exe = ExecutablePath();
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = L"/elevated";
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

void CopyToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(mem)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}
