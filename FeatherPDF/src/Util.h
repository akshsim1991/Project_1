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

// Same file? (case-insensitive, as NTFS names are)
bool SamePath(const std::wstring& a, const std::wstring& b);

// A new, unique file name in %TEMP%\FeatherPDF for private copies of
// documents (the undo history's saved states and inserted files).
std::wstring MakeTempPdfPath();
// Deletes private copies left behind by a crash (older than two days).
void CleanOldTempFiles();

// Parses "1-3, 5, 8-" (1-based, open-ended ranges allowed) into 0-based
// page indices, in the order given. Returns false on a syntax error or a
// page outside 1..pageCount.
bool ParsePageRanges(const std::wstring& text, int pageCount, std::vector<int>& pages);
// The inverse, for 0-based ascending pages: {0,1,2,4} -> "1-3, 5".
std::wstring FormatPageRanges(const std::vector<int>& pages);

// "D:20261005142209Z" -> "05/10/2026 19:52" in the user's format and time
// zone; empty if it is not a PDF date.
std::wstring FormatPdfDate(const std::wstring& pdfDate);
// The Windows user name (the author of new comments).
std::wstring CurrentUserName();
