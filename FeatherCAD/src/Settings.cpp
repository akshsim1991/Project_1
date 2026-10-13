// Settings.cpp - registry persistence.
#include "Settings.h"

#include <algorithm>

#include "Personal.h"
#include "Util.h"

namespace {
DWORD ReadDword(HKEY key, const wchar_t* name, DWORD def) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return def;
    return v;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD v) {
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

std::wstring ReadString(HKEY key, const wchar_t* name) {
    wchar_t buf[256];
    DWORD size = sizeof(buf);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return L"";
    return buf;
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& s) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)s.c_str(), (DWORD)((s.size() + 1) * sizeof(wchar_t)));
}

// Doubles are stored as text ("2.5"): exact and readable in regedit.
double ReadDouble(HKEY key, const wchar_t* name, double def, double lo, double hi) {
    double v;
    if (!ParseNumber(WideToUtf8(ReadString(key, name)), v) || v < lo || v > hi) return def;
    return v;
}

void WriteDouble(HKEY key, const wchar_t* name, double v) { WriteString(key, name, Utf8ToWide(FormatNumber(v))); }

std::vector<std::wstring> ReadList(HKEY key, const wchar_t* name) {
    std::vector<std::wstring> out;
    DWORD bytes = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) || bytes > (1u << 20))
        return out;
    std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &bytes) != ERROR_SUCCESS)
        return out;
    for (const wchar_t* p = buf.c_str(); *p && out.size() < 10; p += wcslen(p) + 1) out.push_back(p);
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
}  // namespace

void Settings::Load() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD size = sizeof(placement);
    hasPlacement = RegGetValueW(key, nullptr, L"WindowPlacement", RRF_RT_REG_BINARY, nullptr, &placement, &size) ==
                       ERROR_SUCCESS &&
                   size == sizeof(placement) && placement.length == sizeof(placement);
    themeMode = std::clamp((int)ReadDword(key, L"Theme", 0), 0, 2);
    recent = ReadList(key, L"Recent");
    Drafting& d = drafting;
    d.gridOn = ReadDword(key, L"GridOn", 1) != 0;
    d.gridSpacing = ReadDouble(key, L"GridSpacing", 10, 1e-6, 1e9);
    d.snapOn = ReadDword(key, L"SnapOn", 0) != 0;
    d.snapSpacing = ReadDouble(key, L"SnapSpacing", 10, 1e-6, 1e9);
    d.orthoOn = ReadDword(key, L"OrthoOn", 0) != 0;
    d.osnapOn = ReadDword(key, L"OsnapOn", 1) != 0;
    d.osnapModes = ReadDword(key, L"OsnapModes", kSnapDefault) & kSnapAll;
    d.polarOn = ReadDword(key, L"PolarOn", 1) != 0;
    d.polarAngle = ReadDouble(key, L"PolarAngle", 15, 0.01, 180);
    d.otrackOn = ReadDword(key, L"OtrackOn", 1) != 0;
    d.dynOn = ReadDword(key, L"DynOn", 1) != 0;
    d.lwDisplay = ReadDword(key, L"ShowLineWeights", 1) != 0;
    d.isoPlane = std::clamp((int)ReadDword(key, L"IsoPlane", 1), 0, 2);
    d.isoScale = ReadDword(key, L"IsoScale", 0) != 0;
    propertiesOn = ReadDword(key, L"PropertiesOn", 1) != 0;
    if (d.orthoOn) d.polarOn = false;
    newUnits = std::clamp((int)ReadDword(key, L"NewUnits", 1), 0, 5);
    newPrecision = std::clamp((int)ReadDword(key, L"NewPrecision", 2), 0, 8);
    autosaveMinutes = std::clamp((int)ReadDword(key, L"AutosaveMinutes", 5), 0, 120);
    keepVersions = std::clamp((int)ReadDword(key, L"KeepVersions", 30), 5, 500);
    const std::wstring lang = ReadString(key, L"Language");
    if (!lang.empty()) language = lang;
    for (const wchar_t* k : kMyDetailKeys) details[k] = ReadString(key, (std::wstring(L"My_") + k).c_str());
    RegCloseKey(key);
}

void Settings::Save() const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;
    if (hasPlacement)
        RegSetValueExW(key, L"WindowPlacement", 0, REG_BINARY, (const BYTE*)&placement, sizeof(placement));
    WriteDword(key, L"Theme", (DWORD)themeMode);
    WriteList(key, L"Recent", recent);
    const Drafting& d = drafting;
    WriteDword(key, L"GridOn", d.gridOn);
    WriteDouble(key, L"GridSpacing", d.gridSpacing);
    WriteDword(key, L"SnapOn", d.snapOn);
    WriteDouble(key, L"SnapSpacing", d.snapSpacing);
    WriteDword(key, L"OrthoOn", d.orthoOn);
    WriteDword(key, L"OsnapOn", d.osnapOn);
    WriteDword(key, L"OsnapModes", d.osnapModes);
    WriteDword(key, L"PolarOn", d.polarOn);
    WriteDouble(key, L"PolarAngle", d.polarAngle);
    WriteDword(key, L"OtrackOn", d.otrackOn);
    WriteDword(key, L"DynOn", d.dynOn);
    WriteDword(key, L"ShowLineWeights", d.lwDisplay);
    WriteDword(key, L"IsoPlane", (DWORD)d.isoPlane);
    WriteDword(key, L"IsoScale", d.isoScale);
    WriteDword(key, L"PropertiesOn", propertiesOn);
    WriteDword(key, L"NewUnits", (DWORD)newUnits);
    WriteDword(key, L"NewPrecision", (DWORD)newPrecision);
    WriteDword(key, L"AutosaveMinutes", (DWORD)autosaveMinutes);
    WriteDword(key, L"KeepVersions", (DWORD)keepVersions);
    WriteString(key, L"Language", language);
    for (const wchar_t* k : kMyDetailKeys) {
        auto it = details.find(k);
        WriteString(key, (std::wstring(L"My_") + k).c_str(), it == details.end() ? L"" : it->second);
    }
    RegCloseKey(key);
}

void Settings::AddRecent(const std::wstring& path) {
    recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const std::wstring& p) { return SamePath(p, path); }),
                 recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > 10) recent.resize(10);
}

bool PreviousRunCrashed() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    const bool running = ReadDword(key, L"Running", 0) != 0;
    RegCloseKey(key);
    return running;
}

void MarkRunning(bool running) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;
    WriteDword(key, L"Running", running ? 1 : 0);
    RegCloseKey(key);
}
