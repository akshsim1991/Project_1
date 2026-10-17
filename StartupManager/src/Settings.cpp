// Settings.cpp - registry persistence.
#include "Settings.h"

const int kRefreshChoices[] = {0, 5, 10, 30, 60};
const int kRefreshChoiceCount = (int)(sizeof(kRefreshChoices) / sizeof(kRefreshChoices[0]));

namespace {
const int kDefaultWidths[kColumnCount] = {220, 80, 190, 120, 110, 340, 300, 150, 300, 220, 90};
// Warnings next to Status, so problems are visible without scrolling.
const int kDefaultOrder[kColumnCount] = {kColName,    kColStatus,   kColWarnings, kColPublisher,   kColType, kColScope,
                                         kColCommand, kColLocation, kColProgram,  kColDescription, kColTrigger};
const unsigned kDefaultVisible =
    ~((1u << kColProgram) | (1u << kColDescription) | (1u << kColTrigger)) & ((1u << kColumnCount) - 1);

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

Settings::Settings() {
    for (int i = 0; i < kColumnCount; ++i) {
        columnWidth[i] = kDefaultWidths[i];
        columnOrder[i] = kDefaultOrder[i];
    }
    visibleColumns = kDefaultVisible;
}

void Settings::Load() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD size = sizeof(placement);
    hasPlacement = RegGetValueW(key, nullptr, L"WindowPlacement", RRF_RT_REG_BINARY, nullptr, &placement, &size) ==
                       ERROR_SUCCESS &&
                   size == sizeof(placement) && placement.length == sizeof(placement);
    themeMode = Clamp((int)ReadDword(key, L"Theme", 0), 0, 2);
    refreshSeconds = Clamp((int)ReadDword(key, L"RefreshSeconds", 10), 0, 3600);
    confirmDisable = ReadDword(key, L"ConfirmDisable", 0) != 0;
    showMicrosoftTasks = ReadDword(key, L"ShowMicrosoftTasks", 0) != 0;
    showRunOnce = ReadDword(key, L"ShowRunOnce", 1) != 0;
    alwaysElevate = ReadDword(key, L"AlwaysElevate", 0) != 0;
    filter = Clamp((int)ReadDword(key, L"Filter", 0), 0, kFilterCount - 1);
    sortColumn = Clamp((int)ReadDword(key, L"SortColumn", 0), 0, kColumnCount - 1);
    sortDescending = ReadDword(key, L"SortDescending", 0) != 0;
    visibleColumns = (ReadDword(key, L"VisibleColumns", kDefaultVisible) & ((1u << kColumnCount) - 1)) | 1u;
    int widths[kColumnCount], order[kColumnCount];
    size = sizeof(widths);
    if (RegGetValueW(key, nullptr, L"ColumnWidths", RRF_RT_REG_BINARY, nullptr, widths, &size) == ERROR_SUCCESS &&
        size == sizeof(widths))
        for (int i = 0; i < kColumnCount; ++i) columnWidth[i] = Clamp(widths[i], 30, 2000);
    size = sizeof(order);
    if (RegGetValueW(key, nullptr, L"ColumnOrder", RRF_RT_REG_BINARY, nullptr, order, &size) == ERROR_SUCCESS &&
        size == sizeof(order)) {
        unsigned seen = 0;
        bool ok = true;
        for (int i = 0; i < kColumnCount; ++i) {
            if (order[i] < 0 || order[i] >= kColumnCount || (seen & (1u << order[i]))) ok = false;
            else seen |= 1u << order[i];
        }
        if (ok)
            for (int i = 0; i < kColumnCount; ++i) columnOrder[i] = order[i];
    }
    RegCloseKey(key);
}

void Settings::Save() const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    if (hasPlacement)
        RegSetValueExW(key, L"WindowPlacement", 0, REG_BINARY, (const BYTE*)&placement, sizeof(placement));
    WriteDword(key, L"Theme", (DWORD)themeMode);
    WriteDword(key, L"RefreshSeconds", (DWORD)refreshSeconds);
    WriteDword(key, L"ConfirmDisable", confirmDisable ? 1 : 0);
    WriteDword(key, L"ShowMicrosoftTasks", showMicrosoftTasks ? 1 : 0);
    WriteDword(key, L"ShowRunOnce", showRunOnce ? 1 : 0);
    WriteDword(key, L"AlwaysElevate", alwaysElevate ? 1 : 0);
    WriteDword(key, L"Filter", (DWORD)filter);
    WriteDword(key, L"SortColumn", (DWORD)sortColumn);
    WriteDword(key, L"SortDescending", sortDescending ? 1 : 0);
    WriteDword(key, L"VisibleColumns", visibleColumns);
    RegSetValueExW(key, L"ColumnWidths", 0, REG_BINARY, (const BYTE*)columnWidth, sizeof(columnWidth));
    RegSetValueExW(key, L"ColumnOrder", 0, REG_BINARY, (const BYTE*)columnOrder, sizeof(columnOrder));
    RegCloseKey(key);
}
