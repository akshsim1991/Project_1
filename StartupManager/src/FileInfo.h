// FileInfo.h - facts about a program file: which file a command line runs,
// its digital signature (embedded or through a Windows catalog), publisher,
// description, version and SHA-256 hash.
#pragma once
#include "Common.h"

enum class SignState { Unknown, Signed, Unsigned, Invalid };

struct Signature {
    SignState state = SignState::Unknown;
    std::wstring signer;  // certificate subject, e.g. "Microsoft Windows"
    bool catalog = false; // signed through a Windows catalog file
};

namespace FileInfo {
// "C:\Program Files\X\x.exe" -a  ->  C:\Program Files\X\x.exe (environment
// variables expanded, system-relative paths made absolute; a bare file
// name is looked up on the PATH).
std::wstring ProgramFromCommandLine(const std::wstring& command);
std::wstring Arguments(const std::wstring& command);  // what follows the program
Signature CheckSignature(const std::wstring& file);
std::wstring Sha256(const std::wstring& file);
std::wstring Version(const std::wstring& file);
std::wstring Company(const std::wstring& file);
std::wstring Description(const std::wstring& file);  // FileDescription
std::wstring FormatTime(const FILETIME& ft);           // local "2026-10-02 14:05"
}  // namespace FileInfo
