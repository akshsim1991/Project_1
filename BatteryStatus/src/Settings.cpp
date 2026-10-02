// Settings.cpp - registry persistence and the start-with-Windows entry.
#include "Settings.h"

#include "Util.h"

namespace {
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

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
    refreshSeconds = Clamp((int)ReadDword(key, L"RefreshSeconds", 5), 1, 60);
    trayStyle = Clamp((int)ReadDword(key, L"TrayStyle", 0), 0, 1);
    startInTray = ReadDword(key, L"StartInTray", 1) != 0;
    closeToTray = ReadDword(key, L"CloseToTray", 1) != 0;
    keepDays = Clamp((int)ReadDword(key, L"KeepDays", 30), 1, 365);
    graphHours = (int)ReadDword(key, L"GraphHours", 24);
    if (graphHours != 1 && graphHours != 6 && graphHours != 24 && graphHours != 168) graphHours = 24;
    lowAlert = ReadDword(key, L"LowAlert", 1) != 0;
    lowPercent = Clamp((int)ReadDword(key, L"LowPercent", 20), 1, 99);
    criticalAlert = ReadDword(key, L"CriticalAlert", 1) != 0;
    criticalPercent = Clamp((int)ReadDword(key, L"CriticalPercent", 10), 1, 99);
    fullAlert = ReadDword(key, L"FullAlert", 1) != 0;
    fullPercent = Clamp((int)ReadDword(key, L"FullPercent", 99), 50, 100);
    plugAlert = ReadDword(key, L"PlugAlert", 0) != 0;
    repeatMinutes = Clamp((int)ReadDword(key, L"RepeatMinutes", 5), 0, 120);
    messageBox = ReadDword(key, L"MessageBox", 0) != 0;
    sound = Clamp((int)ReadDword(key, L"Sound", kSoundNotification), 0, 3);
    soundFile = ReadString(key, L"SoundFile");
    askedAutostart = ReadDword(key, L"AskedAutostart", 0) != 0;
    toldAboutTray = ReadDword(key, L"ToldAboutTray", 0) != 0;
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
    WriteDword(key, L"RefreshSeconds", (DWORD)refreshSeconds);
    WriteDword(key, L"TrayStyle", (DWORD)trayStyle);
    WriteDword(key, L"StartInTray", startInTray);
    WriteDword(key, L"CloseToTray", closeToTray);
    WriteDword(key, L"KeepDays", (DWORD)keepDays);
    WriteDword(key, L"GraphHours", (DWORD)graphHours);
    WriteDword(key, L"LowAlert", lowAlert);
    WriteDword(key, L"LowPercent", (DWORD)lowPercent);
    WriteDword(key, L"CriticalAlert", criticalAlert);
    WriteDword(key, L"CriticalPercent", (DWORD)criticalPercent);
    WriteDword(key, L"FullAlert", fullAlert);
    WriteDword(key, L"FullPercent", (DWORD)fullPercent);
    WriteDword(key, L"PlugAlert", plugAlert);
    WriteDword(key, L"RepeatMinutes", (DWORD)repeatMinutes);
    WriteDword(key, L"MessageBox", messageBox);
    WriteDword(key, L"Sound", (DWORD)sound);
    RegSetValueExW(key, L"SoundFile", 0, REG_SZ, (const BYTE*)soundFile.c_str(),
                   (DWORD)((soundFile.size() + 1) * sizeof(wchar_t)));
    WriteDword(key, L"AskedAutostart", askedAutostart);
    WriteDword(key, L"ToldAboutTray", toldAboutTray);
    RegCloseKey(key);
}

bool AutostartEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    const bool on = RegQueryValueExW(key, APP_RUN_VALUE, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return on;
}

bool OldVersionAutostart() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    const bool on = RegQueryValueExW(key, APP_OLD_RUN_VALUE, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return on;
}

bool SetAutostart(bool on) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    LSTATUS r;
    if (on) {
        const std::wstring cmd = L"\"" + ExecutablePath() + L"\" /tray";
        r = RegSetValueExW(key, APP_RUN_VALUE, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                           (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        // Version 1.2 would start too, showing two battery icons.
        RegDeleteValueW(key, APP_OLD_RUN_VALUE);
    } else {
        r = RegDeleteValueW(key, APP_RUN_VALUE);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}
