// Util.h - small helpers: string conversion, DPI, paths and files.
#pragma once
#include "Common.h"

std::string WideToUtf8(const std::wstring& s);
std::wstring Utf8ToWide(const std::string& s);

// Scale a value given in 96-DPI "device independent pixels" to `dpi`.
inline int Dpi(int value, int dpi) { return MulDiv(value, dpi, 96); }
int GetWindowDpi(HWND hwnd);
// The system message font (Segoe UI) scaled for the given DPI.
HFONT CreateMessageFont(int dpi, int sizeAdjustPercent = 100);

// "C:\dir\file.fcad" -> "file.fcad"
std::wstring FileNameFromPath(const std::wstring& path);
// "C:\dir\file.fcad" -> "C:\dir"
std::wstring DirectoryFromPath(const std::wstring& path);
// "file.fcad" -> "file"
std::wstring StripExtension(const std::wstring& name);
std::wstring ExecutablePath();
bool FileExists(const std::wstring& path);
bool SamePath(const std::wstring& a, const std::wstring& b);
std::wstring FullPath(const std::wstring& path);

bool ReadWholeFile(const std::wstring& path, std::string& data);
// Writes to a temporary file next to `path` and then replaces `path`, so a
// crash or full disk never leaves a half-written drawing behind.
bool WriteFileSafely(const std::wstring& path, const std::string& data, std::wstring* error = nullptr);
// %LOCALAPPDATA%\FeatherCAD\<sub> (created if needed).
std::wstring AppDataDir(const wchar_t* sub);
// Text of the last Windows error ("Access is denied.").
std::wstring LastErrorText(DWORD code);
// "11/10/2026 14:05" in the user's format.
std::wstring FormatFileTime(const FILETIME& ft);
