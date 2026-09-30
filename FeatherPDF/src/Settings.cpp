// Settings.cpp - registry persistence.
#include "Settings.h"

namespace {
DWORD ReadDword(HKEY key, const wchar_t* name, DWORD def) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return def;
    return v;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD v) {
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

}  // namespace

void Settings::Load() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return;

    DWORD size = sizeof(placement);
    hasPlacement = RegGetValueW(key, nullptr, L"WindowPlacement", RRF_RT_REG_BINARY, nullptr,
                                &placement, &size) == ERROR_SUCCESS &&
                   size == sizeof(placement) && placement.length == sizeof(placement);

    zoomMode = (int)ReadDword(key, L"ZoomMode", 1);
    if (zoomMode < 0 || zoomMode > 2) zoomMode = 1;
    DWORD zoomPermille = ReadDword(key, L"ZoomPermille", 1000);
    zoom = zoomPermille / 1000.0;
    if (zoom < 0.05 || zoom > 16) zoom = 1.0;
    viewMode = (int)ReadDword(key, L"ViewMode", ReadDword(key, L"Continuous", 1) ? 1 : 0);
    if (viewMode < 0 || viewMode > 2) viewMode = 1;
    coverPage = ReadDword(key, L"CoverPage", 1) != 0;
    pageColors = (int)ReadDword(key, L"PageColors", 0);
    if (pageColors < 0 || pageColors > 2) pageColors = 0;
    sidebarMode = (int)ReadDword(key, L"Sidebar", 0);
    if (sidebarMode < 0 || sidebarMode > 2) sidebarMode = 0;
    sidebarWidth = (int)ReadDword(key, L"SidebarWidth", 240);
    if (sidebarWidth < 120 || sidebarWidth > 800) sidebarWidth = 240;
    matchCase = ReadDword(key, L"MatchCase", 0) != 0;
    highlightColor = (int)ReadDword(key, L"HighlightColor", 0);
    if (highlightColor < 0 || highlightColor > 3) highlightColor = 0;
    themeMode = (int)ReadDword(key, L"Theme", 0);
    if (themeMode < 0 || themeMode > 2) themeMode = 0;
    activeTab = (int)ReadDword(key, L"ActiveTab", 0);

    // Session: REG_MULTI_SZ of "page|path" entries.
    DWORD bytes = 0;
    if (RegGetValueW(key, nullptr, L"Session", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes) ==
            ERROR_SUCCESS &&
        bytes >= sizeof(wchar_t)) {
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegGetValueW(key, nullptr, L"Session", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(),
                         &bytes) == ERROR_SUCCESS) {
            for (const wchar_t* p = buf.c_str(); *p; p += wcslen(p) + 1) {
                std::wstring entry = p;
                size_t bar = entry.find(L'|');
                if (bar == std::wstring::npos || bar + 1 >= entry.size()) continue;
                session.push_back({entry.substr(bar + 1), _wtoi(entry.substr(0, bar).c_str())});
            }
        }
    }
    RegCloseKey(key);
}

void Settings::Save() const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return;
    if (hasPlacement)
        RegSetValueExW(key, L"WindowPlacement", 0, REG_BINARY, (const BYTE*)&placement,
                       sizeof(placement));
    WriteDword(key, L"ZoomMode", (DWORD)zoomMode);
    WriteDword(key, L"ZoomPermille", (DWORD)(zoom * 1000 + 0.5));
    WriteDword(key, L"ViewMode", (DWORD)viewMode);
    WriteDword(key, L"CoverPage", coverPage ? 1 : 0);
    WriteDword(key, L"PageColors", (DWORD)pageColors);
    WriteDword(key, L"Sidebar", (DWORD)sidebarMode);
    WriteDword(key, L"SidebarWidth", (DWORD)sidebarWidth);
    WriteDword(key, L"MatchCase", matchCase ? 1 : 0);
    WriteDword(key, L"HighlightColor", (DWORD)highlightColor);
    WriteDword(key, L"Theme", (DWORD)themeMode);
    WriteDword(key, L"ActiveTab", (DWORD)activeTab);
    std::wstring multi;
    for (const OpenFile& f : session) {
        multi += std::to_wstring(f.page) + L"|" + f.path;
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    RegSetValueExW(key, L"Session", 0, REG_MULTI_SZ, (const BYTE*)multi.data(),
                   (DWORD)(multi.size() * sizeof(wchar_t)));
    RegCloseKey(key);
}
