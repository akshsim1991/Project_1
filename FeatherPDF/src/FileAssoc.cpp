// FileAssoc.cpp - per-user file association registration.
#include "FileAssoc.h"

#include "Common.h"

#include <shellapi.h>
#include <shlobj.h>

#include "Util.h"

namespace {
const wchar_t kProgId[] = L"FeatherPDF.Document";
const wchar_t kExeName[] = L"FeatherPDF.exe";
const wchar_t kCapabilities[] = L"Software\\FeatherPDF\\Capabilities";

bool SetValue(const std::wstring& subkey, const wchar_t* name, const std::wstring& value) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(),
                            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}
}  // namespace

bool RegisterFileAssociation() {
    const std::wstring exe = ExecutablePath();
    if (exe.empty()) return false;
    const std::wstring command = L"\"" + exe + L"\" \"%1\"";
    const std::wstring icon = L"\"" + exe + L"\",0";
    const std::wstring classes = L"Software\\Classes\\";

    bool ok = true;
    // ProgID describing how to open a PDF with this app.
    ok &= SetValue(classes + kProgId, nullptr, L"PDF Document");
    ok &= SetValue(classes + kProgId + L"\\DefaultIcon", nullptr, icon);
    ok &= SetValue(classes + kProgId + L"\\shell\\open\\command", nullptr, command);
    // Offer the ProgID for .pdf ("Open with" list).
    ok &= SetValue(classes + L".pdf\\OpenWithProgids", kProgId, L"");
    // Application registration (friendly name, supported types).
    const std::wstring app = classes + L"Applications\\" + kExeName;
    ok &= SetValue(app, L"FriendlyAppName", APP_NAME);
    ok &= SetValue(app + L"\\SupportedTypes", L".pdf", L"");
    ok &= SetValue(app + L"\\shell\\open\\command", nullptr, command);
    // Capabilities so the app is listed in Settings > Default apps.
    ok &= SetValue(kCapabilities, L"ApplicationName", APP_NAME);
    ok &= SetValue(kCapabilities, L"ApplicationDescription",
                   L"A fast, lightweight PDF viewer.");
    ok &= SetValue(std::wstring(kCapabilities) + L"\\FileAssociations", L".pdf", kProgId);
    ok &= SetValue(L"Software\\RegisteredApplications", L"FeatherPDF", kCapabilities);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

void UnregisterFileAssociation() {
    const std::wstring classes = L"Software\\Classes\\";
    RegDeleteTreeW(HKEY_CURRENT_USER, (classes + kProgId).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (classes + L"Applications\\" + kExeName).c_str());
    RegDeleteKeyValueW(HKEY_CURRENT_USER, (classes + L".pdf\\OpenWithProgids").c_str(), kProgId);
    RegDeleteTreeW(HKEY_CURRENT_USER, kCapabilities);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"FeatherPDF");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void OpenDefaultAppsSettings() {
    // Windows 11 (and late Windows 10) jump straight to this app's page.
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=FeatherPDF",
                  nullptr, nullptr, SW_SHOWNORMAL);
}
