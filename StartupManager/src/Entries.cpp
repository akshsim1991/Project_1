// Entries.cpp - reading and changing startup entries (see Entries.h).
#include "Entries.h"

#include <algorithm>
#include <map>

#include <objbase.h>
#include <oleauto.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <taskschd.h>

#include "Util.h"

namespace {
const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunOnceKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
const wchar_t kApprovedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
template <typename T>
struct Com {
    T* p = nullptr;
    Com() = default;
    ~Com() {
        if (p) p->Release();
    }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct Bstr {
    BSTR b = nullptr;
    Bstr() = default;
    explicit Bstr(const std::wstring& s) : b(SysAllocString(s.c_str())) {}
    ~Bstr() {
        if (b) SysFreeString(b);
    }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    BSTR* operator&() { return &b; }
    std::wstring str() const { return b ? std::wstring(b, SysStringLen(b)) : std::wstring(); }
};

DWORD FromHr(HRESULT hr) {
    if (SUCCEEDED(hr)) return 0;
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) return HRESULT_CODE(hr);
    return (DWORD)hr;
}

HKEY RootOf(Scope s) { return s == Scope::User ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE; }
REGSAM ViewOf(Scope s) { return s == Scope::Machine32 ? KEY_WOW64_32KEY : KEY_WOW64_64KEY; }

std::wstring KeyDisplay(Scope s, const std::wstring& key) {
    if (s == Scope::User) return L"HKCU\\" + key;
    if (s == Scope::Machine32) return L"HKLM\\Software\\WOW6432Node" + key.substr(8);  // after "Software"
    return L"HKLM\\" + key;
}

// "StartupApproved" sub-key (Run, Run32 or StartupFolder) for an entry.
std::wstring ApprovedSub(const StartupEntry& e) {
    if (e.kind == EntryKind::StartupFolder) return L"StartupFolder";
    return e.scope == Scope::Machine32 ? L"Run32" : L"Run";
}

// Task Manager's format: 12 bytes; an odd first byte means disabled, and
// bytes 4-11 hold the time it was disabled.
void ReadApproved(HKEY root, const std::wstring& sub, const std::wstring& value, bool& enabled, FILETIME& when) {
    BYTE data[16] = {};
    DWORD size = sizeof(data);
    enabled = true;
    if (RegGetValueW(root, (std::wstring(kApprovedKey) + sub).c_str(), value.c_str(),
                     RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, nullptr, data, &size) != ERROR_SUCCESS ||
        size < 1)
        return;
    enabled = (data[0] & 1) == 0;
    if (!enabled && size >= 12) memcpy(&when, data + 4, sizeof(FILETIME));
}

DWORD WriteApproved(HKEY root, const std::wstring& sub, const std::wstring& value, bool enable) {
    HKEY key = nullptr;
    LSTATUS st = RegCreateKeyExW(root, (std::wstring(kApprovedKey) + sub).c_str(), 0, nullptr, 0,
                                 KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (st != ERROR_SUCCESS) return (DWORD)st;
    BYTE data[12] = {};
    data[0] = enable ? 0x02 : 0x03;
    if (!enable) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        memcpy(data + 4, &now, sizeof(now));
    }
    st = RegSetValueExW(key, value.c_str(), 0, REG_BINARY, data, sizeof(data));
    RegCloseKey(key);
    return (DWORD)st;
}

void DeleteApproved(HKEY root, const std::wstring& sub, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, (std::wstring(kApprovedKey) + sub).c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                      &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, value.c_str());
        RegCloseKey(key);
    }
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    wchar_t* p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

std::wstring Quote(const std::wstring& s) { return s.find(L' ') != std::wstring::npos ? L"\"" + s + L"\"" : s; }

bool ResolveShortcut(const std::wstring& lnk, std::wstring& target, std::wstring& args) {
    Com<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link)))
        return false;
    Com<IPersistFile> file;
    if (FAILED(link->QueryInterface(IID_IPersistFile, (void**)&file)) || FAILED(file->Load(lnk.c_str(), STGM_READ)))
        return false;
    wchar_t buf[MAX_PATH * 2] = {};
    if (FAILED(link->GetPath(buf, MAX_PATH * 2, nullptr, SLGP_RAWPATH))) buf[0] = 0;
    wchar_t expanded[MAX_PATH * 2];
    target = ExpandEnvironmentStringsW(buf, expanded, MAX_PATH * 2) ? expanded : buf;
    wchar_t a[2048] = {};
    if (SUCCEEDED(link->GetArguments(a, 2048))) args = a;
    return !target.empty();
}

std::wstring Timestamp(bool forFileName) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t buf[64];
    if (forFileName)
        swprintf_s(buf, L"%04u-%02u-%02u %02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    else
        swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    return buf;
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

bool WriteFileBytes(const std::wstring& path, const void* data, size_t size) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, data, (DWORD)size, &written, nullptr) && written == size;
    CloseHandle(f);
    return ok;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (16 << 20);
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = ReadFile(f, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size();
    }
    CloseHandle(f);
    return ok;
}

void RemoveFolder(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    RemoveDirectoryW(dir.c_str());
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------
void ReadRunKey(HKEY root, Scope scope, EntryKind kind, const wchar_t* keyName, EntryList& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, keyName, 0, KEY_READ | ViewOf(scope), &key) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; ++i) {
        wchar_t name[16384];
        DWORD nameLen = 16384, type = 0, size = 0;
        LSTATUS st = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, nullptr, &size);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st != ERROR_SUCCESS && st != ERROR_MORE_DATA) break;
        if ((type != REG_SZ && type != REG_EXPAND_SZ) || size > (1u << 16)) continue;
        std::vector<wchar_t> data(size / sizeof(wchar_t) + 2, 0);
        DWORD dataSize = size;
        nameLen = 16384;
        if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type, (BYTE*)data.data(), &dataSize) != ERROR_SUCCESS)
            continue;
        StartupEntry e;
        e.kind = kind;
        e.scope = scope;
        e.name = name;
        e.command = data.data();
        e.valueType = type;
        e.keyPath = keyName;
        e.location = KeyDisplay(scope, keyName);
        if (kind == EntryKind::RunOnce) {
            e.canDisable = false;  // runs once at the next sign-in, then Windows removes it
        } else {
            ReadApproved(root, ApprovedSub(e), e.name, e.enabled, e.disabledOn);
        }
        if (type == REG_EXPAND_SZ) {
            wchar_t expanded[32768];
            if (ExpandEnvironmentStringsW(e.command.c_str(), expanded, 32768)) e.program = FileInfo::ProgramFromCommandLine(expanded);
        } else {
            e.program = FileInfo::ProgramFromCommandLine(e.command);
        }
        out.entries.push_back(std::move(e));
    }
    RegCloseKey(key);
}

void ReadStartupFolder(REFKNOWNFOLDERID id, Scope scope, EntryList& out) {
    const std::wstring dir = KnownFolder(id);
    if (dir.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_wcsicmp(fd.cFileName, L"desktop.ini") == 0) continue;
        StartupEntry e;
        e.kind = EntryKind::StartupFolder;
        e.scope = scope;
        e.filePath = dir + L"\\" + fd.cFileName;
        e.location = dir;
        std::wstring name = fd.cFileName;
        const size_t dot = name.rfind(L'.');
        const std::wstring ext = dot == std::wstring::npos ? L"" : ToLower(name.substr(dot));
        e.name = ext == L".lnk" ? name.substr(0, dot) : name;
        std::wstring target, args;
        if (ext == L".lnk" && ResolveShortcut(e.filePath, target, args)) {
            e.program = target;
            e.command = Quote(target) + (args.empty() ? L"" : L" " + args);
        } else {
            e.program = e.filePath;
            e.command = Quote(e.filePath);
        }
        ReadApproved(RootOf(scope), L"StartupFolder", fd.cFileName, e.enabled, e.disabledOn);
        out.entries.push_back(std::move(e));
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

void ReadTaskFolder(ITaskFolder* folder, EntryList& out, int depth) {
    Com<IRegisteredTaskCollection> tasks;
    if (SUCCEEDED(folder->GetTasks(TASK_ENUM_HIDDEN, &tasks))) {
        LONG count = 0;
        tasks->get_Count(&count);
        for (LONG i = 1; i <= count; ++i) {
            VARIANT index;
            VariantInit(&index);
            index.vt = VT_I4;
            index.lVal = i;
            Com<IRegisteredTask> task;
            if (FAILED(tasks->get_Item(index, &task))) continue;
            Com<ITaskDefinition> def;
            if (FAILED(task->get_Definition(&def))) continue;
            // Only tasks that start at sign-in or at boot count as "startup".
            std::wstring trigger;
            Com<ITriggerCollection> triggers;
            if (SUCCEEDED(def->get_Triggers(&triggers))) {
                LONG n = 0;
                triggers->get_Count(&n);
                for (LONG t = 1; t <= n && trigger.empty(); ++t) {
                    Com<ITrigger> tr;
                    TASK_TRIGGER_TYPE2 type;
                    if (SUCCEEDED(triggers->get_Item(t, &tr)) && SUCCEEDED(tr->get_Type(&type))) {
                        if (type == TASK_TRIGGER_LOGON) trigger = L"At sign-in";
                        if (type == TASK_TRIGGER_BOOT) trigger = L"At startup";
                    }
                }
            }
            if (trigger.empty()) continue;
            StartupEntry e;
            e.kind = EntryKind::Task;
            e.scope = Scope::Machine;
            e.trigger = trigger;
            Bstr name, path;
            task->get_Name(&name);
            task->get_Path(&path);
            e.name = name.str();
            e.taskPath = path.str();
            e.location = L"Task Scheduler: " + e.taskPath;
            VARIANT_BOOL enabled = VARIANT_TRUE;
            task->get_Enabled(&enabled);
            e.enabled = enabled != VARIANT_FALSE;
            Com<IActionCollection> actions;
            if (SUCCEEDED(def->get_Actions(&actions))) {
                LONG n = 0;
                actions->get_Count(&n);
                for (LONG a = 1; a <= n && e.command.empty(); ++a) {
                    Com<IAction> action;
                    TASK_ACTION_TYPE type;
                    if (FAILED(actions->get_Item(a, &action)) || FAILED(action->get_Type(&type))) continue;
                    if (type == TASK_ACTION_EXEC) {
                        Com<IExecAction> exec;
                        if (SUCCEEDED(action->QueryInterface(IID_IExecAction, (void**)&exec))) {
                            Bstr p, args;
                            exec->get_Path(&p);
                            exec->get_Arguments(&args);
                            wchar_t expanded[32768];
                            std::wstring prog = p.str();
                            if (ExpandEnvironmentStringsW(prog.c_str(), expanded, 32768)) prog = expanded;
                            e.command = Quote(prog) + (args.str().empty() ? L"" : L" " + args.str());
                            e.program = FileInfo::ProgramFromCommandLine(Quote(prog));
                        }
                    } else if (type == TASK_ACTION_COM_HANDLER) {
                        e.command = L"(runs a Windows component)";
                    }
                }
            }
            out.entries.push_back(std::move(e));
        }
    }
    if (depth > 12) return;
    Com<ITaskFolderCollection> folders;
    if (FAILED(folder->GetFolders(0, &folders))) return;
    LONG count = 0;
    folders->get_Count(&count);
    for (LONG i = 1; i <= count; ++i) {
        VARIANT index;
        VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;
        Com<ITaskFolder> sub;
        if (SUCCEEDED(folders->get_Item(index, &sub))) ReadTaskFolder(sub.p, out, depth + 1);
    }
}

bool ConnectTasks(Com<ITaskService>& service) {
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                (void**)&service)))
        return false;
    VARIANT empty;
    VariantInit(&empty);
    return SUCCEEDED(service->Connect(empty, empty, empty, empty));
}

void ReadTasks(EntryList& out) {
    Com<ITaskService> service;
    if (!ConnectTasks(service)) {
        out.errors.push_back(L"Scheduled tasks could not be read.");
        return;
    }
    Bstr rootPath(L"\\");
    Com<ITaskFolder> root;
    if (FAILED(service->GetFolder(rootPath.b, &root))) {
        out.errors.push_back(L"Scheduled tasks could not be read.");
        return;
    }
    ReadTaskFolder(root.p, out, 0);
}

// Splits "\\Folder\\Sub\\Name" into its folder and name.
void SplitTaskPath(const std::wstring& path, std::wstring& folder, std::wstring& name) {
    const size_t slash = path.rfind(L'\\');
    folder = slash == 0 || slash == std::wstring::npos ? L"\\" : path.substr(0, slash);
    name = slash == std::wstring::npos ? path : path.substr(slash + 1);
}

DWORD WithTask(const std::wstring& taskPath, HRESULT (*fn)(ITaskFolder*, const std::wstring&, void*), void* ctx) {
    Com<ITaskService> service;
    if (!ConnectTasks(service)) return ERROR_SERVICE_NOT_ACTIVE;
    std::wstring folderPath, name;
    SplitTaskPath(taskPath, folderPath, name);
    Bstr fp(folderPath);
    Com<ITaskFolder> folder;
    const HRESULT hr = service->GetFolder(fp.b, &folder);
    if (FAILED(hr)) return FromHr(hr);
    return FromHr(fn(folder.p, name, ctx));
}

bool IsMicrosoft(const StartupEntry& e) {
    if (e.kind == EntryKind::Task && _wcsnicmp(e.taskPath.c_str(), L"\\Microsoft\\", 11) == 0) return true;
    if (ContainsNoCase(e.publisher, L"microsoft")) return true;
    if (e.publisher.empty() && !e.program.empty()) {
        wchar_t win[MAX_PATH];
        const UINT n = GetWindowsDirectoryW(win, MAX_PATH);
        if (n && _wcsnicmp(e.program.c_str(), win, n) == 0) return true;
    }
    return false;
}
}  // namespace

// ===========================================================================
// Public
// ===========================================================================
std::wstring StartupEntry::Id() const {
    const std::wstring where = kind == EntryKind::StartupFolder ? filePath : kind == EntryKind::Task ? taskPath : keyPath;
    return ToLower(std::to_wstring((int)kind) + L"|" + std::to_wstring((int)scope) + L"|" + where + L"|" + name);
}

const FileCache::Facts& FileCache::Get(const std::wstring& file) {
    const std::wstring key = ToLower(file);
    for (auto& it : items)
        if (it.first == key) return it.second;
    Facts f;
    f.company = FileInfo::Company(file);
    f.description = FileInfo::Description(file);
    // Signatures are checked for programs that are not Microsoft's own.
    wchar_t win[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(win, MAX_PATH);
    const bool windowsFile = n && _wcsnicmp(file.c_str(), win, n) == 0;
    if (!ContainsNoCase(f.company, L"microsoft") && !windowsFile && FileExists(file))
        f.sign = FileInfo::CheckSignature(file).state;
    items.emplace_back(key, std::move(f));
    return items.back().second;
}

void Entries::Enumerate(EntryList& out, FileCache& cache) {
    out = EntryList{};
    ReadRunKey(HKEY_CURRENT_USER, Scope::User, EntryKind::Run, kRunKey, out);
    ReadRunKey(HKEY_CURRENT_USER, Scope::User, EntryKind::RunOnce, kRunOnceKey, out);
    ReadRunKey(HKEY_LOCAL_MACHINE, Scope::Machine, EntryKind::Run, kRunKey, out);
    ReadRunKey(HKEY_LOCAL_MACHINE, Scope::Machine, EntryKind::RunOnce, kRunOnceKey, out);
    ReadRunKey(HKEY_LOCAL_MACHINE, Scope::Machine32, EntryKind::Run, kRunKey, out);
    ReadRunKey(HKEY_LOCAL_MACHINE, Scope::Machine32, EntryKind::RunOnce, kRunOnceKey, out);
    ReadStartupFolder(FOLDERID_Startup, Scope::User, out);
    ReadStartupFolder(FOLDERID_CommonStartup, Scope::Machine, out);
    ReadTasks(out);

    for (StartupEntry& e : out.entries) {
        if (!e.program.empty() && FileExists(e.program)) {
            const FileCache::Facts& f = cache.Get(e.program);
            e.publisher = f.company;
            e.description = f.description;
            e.signature = f.sign;
        }
        e.microsoft = IsMicrosoft(e);
        const std::wstring low = ToLower(e.program);
        if (!e.program.empty() && !FileExists(e.program)) e.warnings |= kWarnMissingFile;
        if (!e.microsoft && e.signature == SignState::Unsigned) e.warnings |= kWarnUnsigned;
        if (!e.microsoft && e.signature == SignState::Invalid) e.warnings |= kWarnBadSignature;
        if (low.find(L"\\temp\\") != std::wstring::npos) e.warnings |= kWarnTempFolder;
        const size_t slash = low.find_last_of(L'\\');
        const std::wstring exe = slash == std::wstring::npos ? low : low.substr(slash + 1);
        if (!e.microsoft || e.kind != EntryKind::Task) {
            for (const wchar_t* host : {L"powershell.exe", L"pwsh.exe", L"wscript.exe", L"cscript.exe", L"mshta.exe",
                                        L"cmd.exe"})
                if (exe == host) e.warnings |= kWarnScript;
        }
    }
}

const wchar_t* Entries::KindText(EntryKind k) {
    switch (k) {
        case EntryKind::Run: return L"Registry";
        case EntryKind::RunOnce: return L"Registry (run once)";
        case EntryKind::StartupFolder: return L"Startup folder";
        default: return L"Scheduled task";
    }
}

const wchar_t* Entries::ScopeText(Scope s) {
    switch (s) {
        case Scope::User: return L"Current user";
        case Scope::Machine32: return L"All users (32-bit)";
        default: return L"All users";
    }
}

std::wstring Entries::WarningsText(unsigned w) {
    std::wstring out;
    auto add = [&](unsigned flag, const wchar_t* text) {
        if (!(w & flag)) return;
        if (!out.empty()) out += L" \x00B7 ";
        out += text;
    };
    add(kWarnMissingFile, L"File missing");
    add(kWarnBadSignature, L"Bad signature");
    add(kWarnUnsigned, L"Unsigned");
    add(kWarnTempFolder, L"Temp folder");
    add(kWarnScript, L"Script");
    return out;
}

std::wstring Entries::WarningsExplained(unsigned w) {
    std::wstring out;
    auto add = [&](unsigned flag, const wchar_t* text) {
        if (w & flag) out += std::wstring(L"\x26A0 ") + text + L"\r\n";
    };
    add(kWarnMissingFile, L"The program file is missing, so this entry does nothing except slow sign-in a little. "
                          L"It is usually left over from uninstalled software and can be deleted.");
    add(kWarnBadSignature, L"The program's digital signature is broken or not trusted: the file may have been changed.");
    add(kWarnUnsigned, L"The program has no digital signature, so its publisher can not be verified.");
    add(kWarnTempFolder, L"It runs from a Temp folder. Real programs are installed elsewhere; malware often uses Temp.");
    add(kWarnScript, L"It starts a script (PowerShell, VBScript, HTML application or batch). Legitimate tools "
                     L"do this sometimes, but so does malware: check what the command runs.");
    return out;
}

std::wstring Entries::ErrorText(DWORD e) {
    switch (e) {
        case ERROR_ACCESS_DENIED:
            return IsElevated() ? L"Windows does not allow changing this entry."
                                : L"Administrator rights are needed for entries that apply to all users. Use "
                                  L"\x201CRestart as administrator\x201D.";
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return L"The entry no longer exists. Press F5 to refresh.";
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return L"An entry with that name already exists.";
        case ERROR_NOT_SUPPORTED:
            return L"\x201CRun once\x201D entries can not be disabled, only deleted.";
        default: break;
    }
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPWSTR)&text, 0, nullptr);
    std::wstring msg = text ? text : L"Unknown error";
    if (text) LocalFree(text);
    while (!msg.empty() && (msg.back() == L'\n' || msg.back() == L'\r' || msg.back() == L' ')) msg.pop_back();
    wchar_t code[24];
    swprintf_s(code, e > 0xFFFF ? L" (0x%08X)" : L" (%u)", e);
    return msg + code;
}

// ---------------------------------------------------------------------------
// Enable / disable
// ---------------------------------------------------------------------------
DWORD Entries::SetEnabled(const StartupEntry& e, bool enable) {
    switch (e.kind) {
        case EntryKind::RunOnce: return ERROR_NOT_SUPPORTED;
        case EntryKind::Run:
            return WriteApproved(RootOf(e.scope), ApprovedSub(e), e.name, enable);
        case EntryKind::StartupFolder: {
            const size_t slash = e.filePath.find_last_of(L'\\');
            return WriteApproved(RootOf(e.scope), L"StartupFolder", e.filePath.substr(slash + 1), enable);
        }
        case EntryKind::Task: {
            struct Ctx {
                bool enable;
            } ctx{enable};
            return WithTask(e.taskPath, [](ITaskFolder* folder, const std::wstring& name, void* p) -> HRESULT {
                Bstr n(name);
                Com<IRegisteredTask> task;
                HRESULT hr = folder->GetTask(n.b, &task);
                if (FAILED(hr)) return hr;
                return task->put_Enabled(((Ctx*)p)->enable ? VARIANT_TRUE : VARIANT_FALSE);
            }, &ctx);
        }
    }
    return ERROR_INVALID_PARAMETER;
}

// ---------------------------------------------------------------------------
// Backups, delete, restore
// ---------------------------------------------------------------------------
std::wstring Entries::BackupRoot() {
    std::wstring dir = KnownFolder(FOLDERID_LocalAppData);
    if (dir.empty()) return {};
    dir += L"\\StartupManager";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\Deleted";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

DWORD Entries::Delete(const StartupEntry& e, std::wstring& backup) {
    backup.clear();
    const std::wstring root = BackupRoot();
    if (root.empty()) return ERROR_PATH_NOT_FOUND;
    static LONG counter = 0;
    std::wstring dir = root + L"\\" + Timestamp(true) + L"-" + std::to_wstring(InterlockedIncrement(&counter));
    if (!CreateDirectoryW(dir.c_str(), nullptr)) return GetLastError();

    // 1. Save what is needed to put it back.
    std::wstring info;
    auto line = [&](const wchar_t* k, const std::wstring& v) { info += std::wstring(k) + L"=" + v + L"\r\n"; };
    line(L"kind", std::to_wstring((int)e.kind));
    line(L"scope", std::to_wstring((int)e.scope));
    line(L"name", e.name);
    line(L"command", e.command);
    line(L"keyPath", e.keyPath);
    line(L"filePath", e.filePath);
    line(L"taskPath", e.taskPath);
    line(L"valueType", std::to_wstring(e.valueType));
    line(L"enabled", e.enabled ? L"1" : L"0");
    line(L"deletedOn", Timestamp(false));
    DWORD err = 0;
    if (e.kind == EntryKind::Run || e.kind == EntryKind::RunOnce) {
        // Save the raw (unexpanded) value exactly as it was.
        HKEY key = nullptr;
        if (RegOpenKeyExW(RootOf(e.scope), e.keyPath.c_str(), 0, KEY_READ | ViewOf(e.scope), &key) == ERROR_SUCCESS) {
            wchar_t raw[32768];
            DWORD size = sizeof(raw), type = 0;
            if (RegQueryValueExW(key, e.name.c_str(), nullptr, &type, (BYTE*)raw, &size) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ)) {
                raw[std::min<size_t>(size / sizeof(wchar_t), 32767)] = 0;
                line(L"raw", raw);
            }
            RegCloseKey(key);
        }
    } else if (e.kind == EntryKind::Task) {
        std::wstring xml;
        struct Ctx {
            std::wstring* xml;
        } ctx{&xml};
        err = WithTask(e.taskPath, [](ITaskFolder* folder, const std::wstring& name, void* p) -> HRESULT {
            Bstr n(name);
            Com<IRegisteredTask> task;
            HRESULT hr = folder->GetTask(n.b, &task);
            if (FAILED(hr)) return hr;
            Bstr x;
            hr = task->get_Xml(&x);
            *((Ctx*)p)->xml = x.str();
            return hr;
        }, &ctx);
        // schtasks /XML expects UTF-16 with a byte-order mark.
        std::wstring bom = L"\xFEFF" + xml;
        if (!err && !WriteFileBytes(dir + L"\\task.xml", bom.data(), bom.size() * sizeof(wchar_t))) err = GetLastError();
    }
    const std::string utf8 = "\xEF\xBB\xBF" + ToUtf8(info);
    if (!err && !WriteFileBytes(dir + L"\\entry.txt", utf8.data(), utf8.size())) err = GetLastError();
    if (err) {
        RemoveFolder(dir);
        return err;
    }

    // 2. Remove it.
    switch (e.kind) {
        case EntryKind::Run:
        case EntryKind::RunOnce: {
            HKEY key = nullptr;
            err = (DWORD)RegOpenKeyExW(RootOf(e.scope), e.keyPath.c_str(), 0, KEY_SET_VALUE | ViewOf(e.scope), &key);
            if (!err) {
                err = (DWORD)RegDeleteValueW(key, e.name.c_str());
                RegCloseKey(key);
            }
            if (!err && e.kind == EntryKind::Run) DeleteApproved(RootOf(e.scope), ApprovedSub(e), e.name);
            break;
        }
        case EntryKind::StartupFolder: {
            const size_t slash = e.filePath.find_last_of(L'\\');
            const std::wstring file = e.filePath.substr(slash + 1);
            if (!MoveFileExW(e.filePath.c_str(), (dir + L"\\" + file).c_str(), MOVEFILE_COPY_ALLOWED))
                err = GetLastError();
            else
                DeleteApproved(RootOf(e.scope), L"StartupFolder", file);
            break;
        }
        case EntryKind::Task:
            err = WithTask(e.taskPath, [](ITaskFolder* folder, const std::wstring& name, void*) -> HRESULT {
                Bstr n(name);
                return folder->DeleteTask(n.b, 0);
            }, nullptr);
            break;
    }
    if (err) {
        RemoveFolder(dir);
        return err;
    }
    backup = dir;
    return 0;
}

namespace {
bool ReadBackup(const std::wstring& dir, std::map<std::wstring, std::wstring>& kv) {
    std::string data;
    if (!ReadFileBytes(dir + L"\\entry.txt", data)) return false;
    if (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
    const std::wstring text = FromUtf8(data);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const size_t eq = line.find(L'=');
        if (eq != std::wstring::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv.count(L"kind") && kv.count(L"name");
}
}  // namespace

DWORD Entries::Restore(const std::wstring& dir) {
    std::map<std::wstring, std::wstring> kv;
    if (!ReadBackup(dir, kv)) return ERROR_FILE_NOT_FOUND;
    StartupEntry e;
    e.kind = (EntryKind)_wtoi(kv[L"kind"].c_str());
    e.scope = (Scope)_wtoi(kv[L"scope"].c_str());
    e.name = kv[L"name"];
    e.keyPath = kv[L"keyPath"];
    e.filePath = kv[L"filePath"];
    e.taskPath = kv[L"taskPath"];
    e.enabled = kv[L"enabled"] != L"0";
    DWORD err = 0;
    switch (e.kind) {
        case EntryKind::Run:
        case EntryKind::RunOnce: {
            HKEY key = nullptr;
            err = (DWORD)RegCreateKeyExW(RootOf(e.scope), e.keyPath.c_str(), 0, nullptr, 0,
                                         KEY_SET_VALUE | KEY_QUERY_VALUE | ViewOf(e.scope), nullptr, &key, nullptr);
            if (err) break;
            if (RegQueryValueExW(key, e.name.c_str(), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                err = ERROR_ALREADY_EXISTS;
            } else {
                const std::wstring value = kv.count(L"raw") ? kv[L"raw"] : kv[L"command"];
                const DWORD type = (DWORD)_wtoi(kv[L"valueType"].c_str()) == REG_EXPAND_SZ ? REG_EXPAND_SZ : REG_SZ;
                err = (DWORD)RegSetValueExW(key, e.name.c_str(), 0, type, (const BYTE*)value.c_str(),
                                            (DWORD)((value.size() + 1) * sizeof(wchar_t)));
            }
            RegCloseKey(key);
            if (!err && e.kind == EntryKind::Run && !e.enabled) WriteApproved(RootOf(e.scope), ApprovedSub(e), e.name, false);
            break;
        }
        case EntryKind::StartupFolder: {
            const size_t slash = e.filePath.find_last_of(L'\\');
            const std::wstring file = e.filePath.substr(slash + 1);
            if (FileExists(e.filePath)) {
                err = ERROR_ALREADY_EXISTS;
            } else if (!MoveFileExW((dir + L"\\" + file).c_str(), e.filePath.c_str(), MOVEFILE_COPY_ALLOWED)) {
                err = GetLastError();
            } else if (!e.enabled) {
                WriteApproved(RootOf(e.scope), L"StartupFolder", file, false);
            }
            break;
        }
        case EntryKind::Task: {
            // schtasks registers the saved definition exactly as it was,
            // including the account it runs as and its enabled state.
            const std::wstring xml = dir + L"\\task.xml";
            std::wstring cmd = L"schtasks.exe /Create /TN \"" + e.taskPath + L"\" /XML \"" + xml + L"\" /F";
            STARTUPINFOW si{sizeof(si)};
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                err = GetLastError();
                break;
            }
            WaitForSingleObject(pi.hProcess, 30000);
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code != 0) err = IsElevated() ? ERROR_GEN_FAILURE : ERROR_ACCESS_DENIED;
            break;
        }
    }
    if (!err) RemoveFolder(dir);
    return err;
}

std::vector<Entries::Backup> Entries::ListBackups() {
    std::vector<Backup> out;
    const std::wstring root = BackupRoot();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        std::map<std::wstring, std::wstring> kv;
        const std::wstring dir = root + L"\\" + fd.cFileName;
        if (!ReadBackup(dir, kv)) continue;
        Backup b;
        b.folder = dir;
        b.name = kv[L"name"];
        b.kindText = std::wstring(KindText((EntryKind)_wtoi(kv[L"kind"].c_str()))) + L", " +
                     ScopeText((Scope)_wtoi(kv[L"scope"].c_str()));
        b.deletedOn = kv[L"deletedOn"];
        out.push_back(std::move(b));
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(out.begin(), out.end(), [](const Backup& a, const Backup& b) { return a.folder > b.folder; });
    return out;
}

// ---------------------------------------------------------------------------
// Add
// ---------------------------------------------------------------------------
DWORD Entries::Add(const std::wstring& name, const std::wstring& program, const std::wstring& args, bool allUsers,
                   bool shortcut) {
    if (name.empty() || program.empty()) return ERROR_INVALID_PARAMETER;
    if (!shortcut) {
        HKEY key = nullptr;
        DWORD err = (DWORD)RegCreateKeyExW(allUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                           KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
        if (err) return err;
        if (RegQueryValueExW(key, name.c_str(), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            RegCloseKey(key);
            return ERROR_ALREADY_EXISTS;
        }
        const std::wstring command = L"\"" + program + L"\"" + (args.empty() ? L"" : L" " + args);
        err = (DWORD)RegSetValueExW(key, name.c_str(), 0, REG_SZ, (const BYTE*)command.c_str(),
                                    (DWORD)((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        return err;
    }
    const std::wstring dir = KnownFolder(allUsers ? FOLDERID_CommonStartup : FOLDERID_Startup);
    if (dir.empty()) return ERROR_PATH_NOT_FOUND;
    const std::wstring path = dir + L"\\" + name + L".lnk";
    if (FileExists(path)) return ERROR_ALREADY_EXISTS;
    Com<IShellLinkW> link;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link);
    if (FAILED(hr)) return FromHr(hr);
    link->SetPath(program.c_str());
    link->SetArguments(args.c_str());
    link->SetWorkingDirectory(DirectoryFromPath(program).c_str());
    Com<IPersistFile> file;
    hr = link->QueryInterface(IID_IPersistFile, (void**)&file);
    if (SUCCEEDED(hr)) hr = file->Save(path.c_str(), TRUE);
    return FromHr(hr);
}
