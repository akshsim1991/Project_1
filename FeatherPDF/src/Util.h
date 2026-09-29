// Util.h - small helpers: string conversion, DPI helpers, paths.
#pragma once
#include "Common.h"

std::string WideToUtf8(const std::wstring& s);
std::wstring Utf8ToWide(const std::string& s);

// Scale a value given in 96-DPI "device independent pixels" to `dpi`.
inline int Dpi(int value, int dpi) { return MulDiv(value, dpi, 96); }

// DPI of the monitor a window is on (falls back to the system DPI on very
// old Windows 10 builds that lack GetDpiForWindow).
int GetWindowDpi(HWND hwnd);

// The system message font (Segoe UI) scaled for the given DPI.
HFONT CreateMessageFont(int dpi, int sizeAdjustPercent = 100);

// "C:\dir\file.pdf" -> "file.pdf"
std::wstring FileNameFromPath(const std::wstring& path);
// "C:\dir\file.pdf" -> "C:\dir"
std::wstring DirectoryFromPath(const std::wstring& path);
// Full path of the running executable.
std::wstring ExecutablePath();
bool FileExists(const std::wstring& path);

// Opens a link from a PDF in the default browser / mail client. Only http,
// https and mailto are allowed: other schemes (file:, custom protocols)
// could launch programs. Returns false if the link was refused.
bool OpenExternalLink(HWND owner, const std::wstring& uri);
