// Settings.cpp - registry persistence.
#include "Settings.h"

namespace {
DWORD ReadDword(HKEY key, const wchar_t* name, DWORD def) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return def;
    return v;
}

std::vector<std::wstring> ReadList(HKEY key, const wchar_t* name) {
    std::vector<std::wstring> out;
    DWORD bytes = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) || bytes > (1u << 20))
        return out;
    std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &bytes) != ERROR_SUCCESS)
        return out;
    for (const wchar_t* p = buf.c_str(); *p && out.size() < 50; p += wcslen(p) + 1) out.push_back(p);
    return out;
}

void WriteList(HKEY key, const wchar_t* name, const std::vector<std::wstring>& items) {
    std::wstring multi;
    for (const std::wstring& item : items) {
        multi += item;
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    RegSetValueExW(key, name, 0, REG_MULTI_SZ, (const BYTE*)multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
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
    drawColor = (COLORREF)ReadDword(key, L"DrawColor", RGB(220, 30, 30)) & 0xFFFFFF;
    lineWidthTenths = (int)ReadDword(key, L"LineWidth", 20);
    if (lineWidthTenths < 5 || lineWidthTenths > 200) lineWidthTenths = 20;
    textSize = (int)ReadDword(key, L"TextSize", 12);
    if (textSize < 4 || textSize > 200) textSize = 12;
    measureUnit = (int)ReadDword(key, L"MeasureUnit", 0);
    if (measureUnit < 0 || measureUnit > 5) measureUnit = 0;
    measureScale = (int)ReadDword(key, L"MeasureScale", 1);
    if (measureScale < 1 || measureScale > 1000000) measureScale = 1;
    showRulers = ReadDword(key, L"ShowRulers", 0) != 0;
    recent = ReadList(key, L"Recent");

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
    WriteDword(key, L"DrawColor", (DWORD)drawColor);
    WriteDword(key, L"LineWidth", (DWORD)lineWidthTenths);
    WriteDword(key, L"TextSize", (DWORD)textSize);
    WriteDword(key, L"MeasureUnit", (DWORD)measureUnit);
    WriteDword(key, L"MeasureScale", (DWORD)measureScale);
    WriteDword(key, L"ShowRulers", showRulers ? 1 : 0);
    WriteList(key, L"Recent", recent);
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
