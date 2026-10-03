// Util.h - small helpers: DPI, fonts, strings, paths, elevation.
#pragma once
#include "Common.h"

// Scale a value given in 96-DPI "device independent pixels" to `dpi`.
inline int Dpi(int value, int dpi) { return MulDiv(value, dpi, 96); }

// DPI of the monitor a window is on.
int GetWindowDpi(HWND hwnd);

// The system message font (Segoe UI) scaled for the given DPI.
HFONT CreateMessageFont(int dpi, int sizeAdjustPercent = 100);

std::wstring DirectoryFromPath(const std::wstring& path);
std::wstring ExecutablePath();
bool FileExists(const std::wstring& path);
std::wstring ToLower(std::wstring s);
// Case-insensitive "contains".
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needleLower);
std::wstring GetWindowString(HWND hwnd);

// True when the process runs with administrator rights (elevated token).
bool IsElevated();
// Starts this program again as administrator (UAC prompt). Returns false
// if the user declined.
bool RelaunchElevated(HWND owner);

// Puts text on the clipboard.
void CopyToClipboard(HWND owner, const std::wstring& text);
