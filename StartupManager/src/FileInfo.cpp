// FileInfo.cpp - program files: command lines, signatures, version info.
#include "FileInfo.h"

#include <algorithm>
#include <vector>

// clang-format off
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <bcrypt.h>
// clang-format on

#include "Util.h"

namespace {
std::wstring SystemRoot() {
    wchar_t buf[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(buf, n) : std::wstring(L"C:\\Windows");
}

bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

std::wstring ImageFromCommandLineImpl(const std::wstring& cmd) {
    std::wstring p = cmd;
    p.erase(0, p.find_first_not_of(L" \t"));
    if (p.empty()) return p;
    if (p[0] == L'"') {
        const size_t end = p.find(L'"', 1);
        p = p.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
    } else {
        const std::wstring low = ToLower(p);
        size_t pos = low.find(L".exe");
        if (pos != std::wstring::npos)
            p.resize(pos + 4);
        else if ((pos = p.find(L' ')) != std::wstring::npos)
            p.resize(pos);
    }
    if (StartsWithNoCase(p, L"\\??\\")) p.erase(0, 4);
    if (StartsWithNoCase(p, L"\\SystemRoot\\")) p = SystemRoot() + p.substr(11);
    if (StartsWithNoCase(p, L"System32\\") || StartsWithNoCase(p, L"SysWOW64\\"))
        p = SystemRoot() + L"\\" + p;
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(p.c_str(), buf, MAX_PATH * 2);
    return n && n <= MAX_PATH * 2 ? std::wstring(buf) : p;
}

// The certificate subject of the first signer of a verified file.
std::wstring SignerOf(HANDLE state) {
    CRYPT_PROVIDER_DATA* data = WTHelperProvDataFromStateData(state);
    CRYPT_PROVIDER_SGNR* signer = data ? WTHelperGetProvSignerFromChain(data, 0, FALSE, 0) : nullptr;
    CRYPT_PROVIDER_CERT* cert = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
    if (!cert || !cert->pCert) return {};
    wchar_t name[256] = {};
    CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
    return name;
}

LONG Verify(WINTRUST_DATA& wd, std::wstring& signer) {
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    wd.cbStruct = sizeof(wd);
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;  // never go online here
    const LONG r = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);
    if (wd.hWVTStateData) signer = SignerOf(wd.hWVTStateData);
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &action, &wd);
    return r;
}

bool NoSignature(LONG r) {
    return r == (LONG)TRUST_E_NOSIGNATURE || r == (LONG)TRUST_E_SUBJECT_FORM_UNKNOWN ||
           r == (LONG)TRUST_E_PROVIDER_UNKNOWN;
}

// Windows' own files are usually signed through catalog files rather than
// an embedded signature: look the file's hash up in the system catalogs.
// The SHA-256 capable "2" functions (Windows 8+) are resolved at run time,
// because some SDK import libraries lack them.
using AcquireContext2Fn = BOOL(WINAPI*)(HCATADMIN*, const GUID*, PCWSTR, PCCERT_STRONG_SIGN_PARA, DWORD);
using CalcHash2Fn = BOOL(WINAPI*)(HCATADMIN, HANDLE, DWORD*, BYTE*, DWORD);

bool CatalogSignature(const std::wstring& file, const wchar_t* hashAlg, Signature& sig) {
    static HMODULE wintrust = LoadLibraryExW(L"wintrust.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto acquire2 = wintrust ? (AcquireContext2Fn)GetProcAddress(wintrust, "CryptCATAdminAcquireContext2") : nullptr;
    static auto calc2 = wintrust ? (CalcHash2Fn)GetProcAddress(wintrust, "CryptCATAdminCalcHashFromFileHandle2") : nullptr;
    HCATADMIN admin = nullptr;
    bool found = false;
    const bool modern = acquire2 && calc2;
    if (!modern && hashAlg) return false;  // SHA-256 needs the modern functions
    if (modern ? !acquire2(&admin, nullptr, hashAlg, nullptr, 0) : !CryptCATAdminAcquireContext(&admin, nullptr, 0))
        return false;
    auto calc = [&](HANDLE f, DWORD* len, BYTE* hash) {
        return modern ? calc2(admin, f, len, hash, 0) != FALSE
                      : CryptCATAdminCalcHashFromFileHandle(f, len, hash, 0) != FALSE;
    };
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD len = 0;
        calc(f, &len, nullptr);
        std::vector<BYTE> hash(len ? len : 64);
        if (len && calc(f, &len, hash.data())) {
            HCATINFO ci = CryptCATAdminEnumCatalogFromHash(admin, hash.data(), len, 0, nullptr);
            if (ci) {
                CATALOG_INFO info{};
                info.cbStruct = sizeof(info);
                if (CryptCATCatalogInfoFromContext(ci, &info, 0)) {
                    std::wstring tag;
                    for (DWORD i = 0; i < len; ++i) {
                        wchar_t hx[3];
                        swprintf_s(hx, L"%02X", hash[i]);
                        tag += hx;
                    }
                    WINTRUST_CATALOG_INFO wci{};
                    wci.cbStruct = sizeof(wci);
                    wci.pcwszCatalogFilePath = info.wszCatalogFile;
                    wci.pcwszMemberFilePath = file.c_str();
                    wci.pcwszMemberTag = tag.c_str();
                    wci.hMemberFile = f;
                    wci.pbCalculatedFileHash = hash.data();
                    wci.cbCalculatedFileHash = len;
                    wci.hCatAdmin = admin;
                    WINTRUST_DATA wd{};
                    wd.dwUnionChoice = WTD_CHOICE_CATALOG;
                    wd.pCatalog = &wci;
                    const LONG r = Verify(wd, sig.signer);
                    sig.state = r == 0 ? SignState::Signed : SignState::Invalid;
                    sig.catalog = true;
                    found = true;
                }
                CryptCATAdminReleaseCatalogContext(admin, ci, 0);
            }
        }
        CloseHandle(f);
    }
    CryptCATAdminReleaseContext(admin, 0);
    return found;
}

std::wstring ToHex(const BYTE* p, size_t n) {
    std::wstring s;
    for (size_t i = 0; i < n; ++i) {
        wchar_t hx[3];
        swprintf_s(hx, L"%02x", p[i]);
        s += hx;
    }
    return s;
}


// String value from the program's version resource (first language listed).
std::wstring VersionString(const std::wstring& file, const wchar_t* name) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) return {};
    struct Translation {
        WORD language, codePage;
    }* tr = nullptr;
    UINT len = 0;
    std::vector<std::wstring> keys;
    wchar_t key[96];
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &len) && tr &&
        len >= sizeof(Translation)) {
        swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", tr->language, tr->codePage, name);
        keys.push_back(key);
    }
    swprintf_s(key, L"\\StringFileInfo\\040904b0\\%s", name);
    keys.push_back(key);
    swprintf_s(key, L"\\StringFileInfo\\040904e4\\%s", name);
    keys.push_back(key);
    for (const auto& k : keys) {
        wchar_t* value = nullptr;
        if (VerQueryValueW(data.data(), k.c_str(), (void**)&value, &len) && value && len > 1)
            return std::wstring(value, wcsnlen(value, len));
    }
    return {};
}
}  // namespace

std::wstring FileInfo::ProgramFromCommandLine(const std::wstring& command) {
    std::wstring p = ImageFromCommandLineImpl(command);
    // A bare name ("ctfmon.exe") runs from the PATH.
    if (!p.empty() && p.find(L'\\') == std::wstring::npos) {
        wchar_t found[MAX_PATH];
        if (SearchPathW(nullptr, p.c_str(), L".exe", MAX_PATH, found, nullptr)) p = found;
    }
    return p;
}

std::wstring FileInfo::Arguments(const std::wstring& command) {
    std::wstring c = command;
    c.erase(0, c.find_first_not_of(L" \t"));
    size_t end;
    if (!c.empty() && c[0] == L'"') {
        end = c.find(L'"', 1);
        end = end == std::wstring::npos ? c.size() : end + 1;
    } else {
        const size_t exe = ToLower(c).find(L".exe");
        end = exe != std::wstring::npos ? exe + 4 : c.find(L' ');
        if (end == std::wstring::npos) end = c.size();
    }
    std::wstring args = c.substr(end);
    args.erase(0, args.find_first_not_of(L" \t"));
    return args;
}

Signature FileInfo::CheckSignature(const std::wstring& file) {
    Signature sig;
    if (file.empty() || !FileExists(file)) return sig;
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof(fi);
    fi.pcwszFilePath = file.c_str();
    WINTRUST_DATA wd{};
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    const LONG r = Verify(wd, sig.signer);
    if (r == 0) {
        sig.state = SignState::Signed;
        return sig;
    }
    if (!NoSignature(r)) {
        sig.state = SignState::Invalid;  // tampered, expired or untrusted
        return sig;
    }
    sig.signer.clear();
    if (CatalogSignature(file, BCRYPT_SHA256_ALGORITHM, sig) || CatalogSignature(file, nullptr, sig))
        return sig;
    sig.state = SignState::Unsigned;
    return sig;
}

std::wstring FileInfo::Sha256(const std::wstring& file) {
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::wstring out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<BYTE> buf(1 << 16);
        DWORD read = 0;
        bool ok = true;
        while (ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr) && read)
            if (BCryptHashData(hash, buf.data(), read, 0) != 0) {
                ok = false;
                break;
            }
        BYTE digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) out = ToHex(digest, sizeof(digest));
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return out;
}

std::wstring FileInfo::Version(const std::wstring& file) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (!size) return {};
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT len = 0;
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", (void**)&fixed, &len) || !fixed)
        return {};
    wchar_t buf[64];
    swprintf_s(buf, L"%u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
               HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
    return buf;
}


std::wstring FileInfo::Company(const std::wstring& file) { return VersionString(file, L"CompanyName"); }

std::wstring FileInfo::Description(const std::wstring& file) { return VersionString(file, L"FileDescription"); }

std::wstring FileInfo::FormatTime(const FILETIME& ft) {
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return {};
    wchar_t buf[64];
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour,
               local.wMinute);
    return buf;
}

