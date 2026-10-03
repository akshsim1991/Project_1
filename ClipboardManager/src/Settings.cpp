// Settings.cpp - registry persistence, shortcut choices, start with Windows.
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
    hotkey = Clamp((int)ReadDword(key, L"Hotkey", kHotkeyCtrlShiftV), 0, kHotkeyCount - 1);
    pasteOnEnter = ReadDword(key, L"PasteOnEnter", 1) != 0;
    recordImages = ReadDword(key, L"RecordImages", 1) != 0;
    recordFiles = ReadDword(key, L"RecordFiles", 1) != 0;
    startInTray = ReadDword(key, L"StartInTray", 1) != 0;
    closeToTray = ReadDword(key, L"CloseToTray", 1) != 0;
    deleteAfterDays = Clamp((int)ReadDword(key, L"DeleteAfterDays", 0), 0, 3650);
    filter = Clamp((int)ReadDword(key, L"Filter", kFilterAll), 0, kFilterCount - 1);
    listWidthDip = Clamp((int)ReadDword(key, L"ListWidth", 430), 220, 2000);
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
    WriteDword(key, L"Hotkey", (DWORD)hotkey);
    WriteDword(key, L"PasteOnEnter", pasteOnEnter);
    WriteDword(key, L"RecordImages", recordImages);
    WriteDword(key, L"RecordFiles", recordFiles);
    WriteDword(key, L"StartInTray", startInTray);
    WriteDword(key, L"CloseToTray", closeToTray);
    WriteDword(key, L"DeleteAfterDays", (DWORD)deleteAfterDays);
    WriteDword(key, L"Filter", (DWORD)filter);
    WriteDword(key, L"ListWidth", (DWORD)listWidthDip);
    WriteDword(key, L"AskedAutostart", askedAutostart);
    WriteDword(key, L"ToldAboutTray", toldAboutTray);
    RegCloseKey(key);
}

const wchar_t* HotkeyName(int choice) {
    switch (choice) {
        case kHotkeyCtrlShiftV: return L"Ctrl+Shift+V";
        case kHotkeyCtrlAltV: return L"Ctrl+Alt+V";
        case kHotkeyWinShiftV: return L"Win+Shift+V";
        default: return L"None";
    }
}

bool HotkeyKeys(int choice, UINT& modifiers, UINT& vk) {
    vk = 'V';
    switch (choice) {
        case kHotkeyCtrlShiftV: modifiers = MOD_CONTROL | MOD_SHIFT; return true;
        case kHotkeyCtrlAltV: modifiers = MOD_CONTROL | MOD_ALT; return true;
        case kHotkeyWinShiftV: modifiers = MOD_WIN | MOD_SHIFT; return true;
        default: return false;
    }
}

bool AutostartEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    const bool on = RegQueryValueExW(key, APP_RUN_VALUE, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
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
    } else {
        r = RegDeleteValueW(key, APP_RUN_VALUE);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}
