// Worker.h - background thread for reading and changing startup entries,
// so the window never waits on the registry, files or the Task Scheduler.
#pragma once
#include <deque>

#include "Entries.h"

enum class OpKind { Enable, Disable, Delete, Restore, Add };

struct OpRequest {
    OpKind kind = OpKind::Enable;
    std::vector<StartupEntry> entries;  // Enable / Disable / Delete
    std::vector<std::wstring> backups;  // Restore: backup folders
    // Add
    std::wstring name, program, args;
    bool allUsers = false, shortcut = false;
    bool forUndo = false;  // the result should not be recorded for undo again
};

struct OpOutcome {
    std::wstring name;
    DWORD error = 0;
    std::wstring backup;  // Delete: where it was saved; Restore: the backup used
};

struct OpResult {
    OpRequest request;
    std::vector<OpOutcome> outcomes;
};

class Worker {
public:
    ~Worker();
    bool Start(HWND notify);
    void Stop();
    void Refresh();  // skipped if one is already queued
    void Run(OpRequest&& request);

private:
    struct Job {
        bool refresh = true;
        OpRequest request;
    };
    static DWORD WINAPI ThreadProc(LPVOID self);
    void Loop();
    void Push(Job&& job);

    HANDLE m_thread = nullptr;
    HWND m_notify = nullptr;
    SRWLOCK m_lock = SRWLOCK_INIT;
    CONDITION_VARIABLE m_cv = CONDITION_VARIABLE_INIT;
    std::deque<Job> m_jobs;
    bool m_quit = false;
    FileCache m_cache;  // worker thread only
};
