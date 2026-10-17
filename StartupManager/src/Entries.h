// Entries.h - everything that starts automatically with Windows, and the
// changes this program can make to it.
//
// Sources:
//   * registry Run and RunOnce keys: current user, all users, and the
//     32-bit view of all users (WOW6432Node);
//   * the Startup folders: the user's and the one for all users;
//   * scheduled tasks that run at sign-in or at boot.
//
// Enabling and disabling Run-key and Startup-folder entries uses the same
// "StartupApproved" registry values as Task Manager and Settings > Apps >
// Startup, so all three always agree. Tasks are enabled and disabled in
// the Task Scheduler itself.
//
// Deleting never loses anything: the entry (and the shortcut file or task
// definition) is saved to a backup folder first and can be restored.
//
// All of this blocks (registry, files, the Task Scheduler), so it runs on
// the Worker thread.
#pragma once
#include "FileInfo.h"

enum class EntryKind { Run, RunOnce, StartupFolder, Task };
enum class Scope { User, Machine, Machine32 };  // Machine32: 32-bit registry view

// Things worth a closer look.
enum : unsigned {
    kWarnMissingFile = 1,  // the program file does not exist (a broken entry)
    kWarnUnsigned = 2,     // non-Microsoft program without a digital signature
    kWarnBadSignature = 4, // signature broken or not trusted
    kWarnTempFolder = 8,   // runs from a Temp folder
    kWarnScript = 16,      // starts a script host (PowerShell, VBScript, mshta, cmd)
};

struct StartupEntry {
    EntryKind kind = EntryKind::Run;
    Scope scope = Scope::User;
    std::wstring name;         // registry value / file name / task name
    std::wstring command;      // full command line
    std::wstring program;      // the program file it runs
    std::wstring location;     // where it is, for people
    std::wstring keyPath;      // Run/RunOnce: registry key (under HKCU/HKLM)
    std::wstring filePath;     // StartupFolder: the shortcut or file
    std::wstring taskPath;     // Task: "\\Folder\\Name"
    std::wstring trigger;      // Task: "At sign-in" / "At startup"
    DWORD valueType = REG_SZ;  // Run/RunOnce: REG_SZ or REG_EXPAND_SZ
    bool enabled = true;
    bool canDisable = true;    // RunOnce entries can only be deleted
    FILETIME disabledOn{};     // when it was disabled (Task Manager records it)
    std::wstring publisher;    // company from the program's version info
    std::wstring description;  // FileDescription of the program
    bool microsoft = false;    // part of Windows / published by Microsoft
    SignState signature = SignState::Unknown;
    unsigned warnings = 0;

    std::wstring Id() const;   // stable identity across refreshes
};

struct EntryList {
    std::vector<StartupEntry> entries;
    std::vector<std::wstring> errors;  // sources that could not be read
};

// Program file -> facts, kept between refreshes (signatures are slow).
struct FileCache {
    struct Facts {
        std::wstring company, description;
        SignState sign = SignState::Unknown;
    };
    std::vector<std::pair<std::wstring, Facts>> items;  // small: a linear list is fine
    const Facts& Get(const std::wstring& file);
};

namespace Entries {
// Requires COM on the calling thread (the Worker initialises it).
void Enumerate(EntryList& out, FileCache& cache);

const wchar_t* KindText(EntryKind k);   // "Registry", "Startup folder", ...
const wchar_t* ScopeText(Scope s);      // "Current user", "All users"
std::wstring WarningsText(unsigned w);  // short, for the list
std::wstring WarningsExplained(unsigned w);
std::wstring ErrorText(DWORD error);

DWORD SetEnabled(const StartupEntry& e, bool enable);
// Saves a backup, then removes the entry. `backup` receives the backup folder.
DWORD Delete(const StartupEntry& e, std::wstring& backup);
// Restores a deleted entry from its backup folder (and removes the backup).
DWORD Restore(const std::wstring& backup);

struct Backup {
    std::wstring folder, name, kindText, deletedOn;
};
std::vector<Backup> ListBackups();  // newest first
std::wstring BackupRoot();

// Adds a new entry: a Run value, or a shortcut in the Startup folder.
DWORD Add(const std::wstring& name, const std::wstring& program, const std::wstring& args, bool allUsers,
          bool shortcut);
}  // namespace Entries
