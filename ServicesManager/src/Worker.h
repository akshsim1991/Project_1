// Worker.h - the background thread that reads the service list and runs
// actions, so the window never freezes while a service takes its time.
//
// Jobs run one at a time in order. After a batch of actions the list is
// read again so the window shows the result. Results are posted to the
// main window (WM_APP_SNAPSHOT / WM_APP_OP_PROGRESS / WM_APP_OP_DONE).
#pragma once
#include <atomic>
#include <deque>

#include "Services.h"

class Worker {
public:
    ~Worker();
    bool Start(HWND notify);
    void Stop();

    // Skipped if a refresh is already queued. `full` also re-checks program
    // signatures and the boot-delay log (F5).
    void Refresh(bool full = false);
    void Run(OpRequest&& request);
    void CancelActions();         // stops the running batch after the current wait
    bool Busy() const { return m_busy; }

private:
    struct Job {
        bool refresh = true;
        bool full = false;
        OpRequest request;
    };
    static DWORD WINAPI ThreadProc(LPVOID self);
    void Loop();
    void DoRefresh(bool full = false);
    void DoBatch(OpRequest& request);
    void Push(Job&& job);

    HANDLE m_thread = nullptr;
    HWND m_notify = nullptr;
    SRWLOCK m_lock = SRWLOCK_INIT;
    CONDITION_VARIABLE m_cv = CONDITION_VARIABLE_INIT;
    std::deque<Job> m_jobs;
    bool m_quit = false;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_busy{false};  // a batch of actions is running
    EnumCache m_cache;                // worker thread only
};
