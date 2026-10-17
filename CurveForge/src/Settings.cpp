// Settings.cpp - registry persistence.
#include "Settings.h"

#include <algorithm>

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
    wchar_t buf[2048];
    DWORD size = sizeof(buf);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

void WriteString(HKEY key, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

void Settings::Load() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD size = sizeof(placement);
    hasPlacement = RegGetValueW(key, nullptr, L"WindowPlacement", RRF_RT_REG_BINARY, nullptr, &placement, &size) ==
                       ERROR_SUCCESS &&
                   size == sizeof(placement) && placement.length == sizeof(placement);
    themeMode = Clamp((int)ReadDword(key, L"Theme", 0), 0, 2);
    digits = Clamp((int)ReadDword(key, L"Digits", 6), 3, 12);
    confidence = (int)ReadDword(key, L"Confidence", 95);
    if (confidence != 90 && confidence != 95 && confidence != 99) confidence = 95;
    autoFit = ReadDword(key, L"AutoFit", 1) != 0;
    confidenceBand = ReadDword(key, L"ConfidenceBand", 1) != 0;
    predictionBand = ReadDword(key, L"PredictionBand", 0) != 0;
    residuals = ReadDword(key, L"Residuals", 1) != 0;
    gridLines = ReadDword(key, L"GridLines", 1) != 0;
    tableWidthDip = Clamp((int)ReadDword(key, L"TableWidth", 380), 120, 2000);
    resultsHeightDip = Clamp((int)ReadDword(key, L"ResultsHeight", 240), 60, 2000);
    lastFolder = ReadString(key, L"LastFolder");
    recent.clear();
    for (int i = 0; i < 10; ++i) {
        const std::wstring v = ReadString(key, (L"Recent" + std::to_wstring(i)).c_str());
        if (!v.empty()) recent.push_back(v);
    }
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
    WriteDword(key, L"Digits", (DWORD)digits);
    WriteDword(key, L"Confidence", (DWORD)confidence);
    WriteDword(key, L"AutoFit", autoFit);
    WriteDword(key, L"ConfidenceBand", confidenceBand);
    WriteDword(key, L"PredictionBand", predictionBand);
    WriteDword(key, L"Residuals", residuals);
    WriteDword(key, L"GridLines", gridLines);
    WriteDword(key, L"TableWidth", (DWORD)tableWidthDip);
    WriteDword(key, L"ResultsHeight", (DWORD)resultsHeightDip);
    WriteString(key, L"LastFolder", lastFolder);
    for (int i = 0; i < 10; ++i) {
        const std::wstring name = L"Recent" + std::to_wstring(i);
        if (i < (int)recent.size()) WriteString(key, name.c_str(), recent[(size_t)i]);
        else RegDeleteValueW(key, name.c_str());
    }
    RegCloseKey(key);
}

void Settings::AddRecent(const std::wstring& path) {
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const std::wstring& p) { return _wcsicmp(p.c_str(), path.c_str()) == 0; }),
                 recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > 10) recent.resize(10);
}
