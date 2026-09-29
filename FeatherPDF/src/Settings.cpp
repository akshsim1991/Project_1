// Settings.cpp - registry persistence.
#include "Settings.h"

namespace {
DWORD ReadDword(HKEY key, const wchar_t* name, DWORD def) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return def;
    return v;
}

std::wstring ReadString(HKEY key, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t))
        return {};
    std::wstring s(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, s.data(), &size) != ERROR_SUCCESS)
        return {};
    s.resize(wcslen(s.c_str()));
    return s;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD v) {
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& s) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)s.c_str(),
                   (DWORD)((s.size() + 1) * sizeof(wchar_t)));
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
    continuous = ReadDword(key, L"Continuous", 1) != 0;
    matchCase = ReadDword(key, L"MatchCase", 0) != 0;
    lastFile = ReadString(key, L"LastFile");
    lastPage = (int)ReadDword(key, L"LastPage", 0);
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
    WriteDword(key, L"Continuous", continuous ? 1 : 0);
    WriteDword(key, L"MatchCase", matchCase ? 1 : 0);
    WriteString(key, L"LastFile", lastFile);
    WriteDword(key, L"LastPage", (DWORD)lastPage);
    RegCloseKey(key);
}
