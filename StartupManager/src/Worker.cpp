// Worker.cpp - background reads and changes (see Worker.h).
#include "Worker.h"

#include <objbase.h>

namespace {
class LockGuard {
public:
    explicit LockGuard(SRWLOCK& l) : m_l(l) { AcquireSRWLockExclusive(&m_l); }
    ~LockGuard() { ReleaseSRWLockExclusive(&m_l); }

private:
    SRWLOCK& m_l;
};

template <typename T>
void Post(HWND hwnd, UINT msg, T* obj) {
    if (!PostMessageW(hwnd, msg, 0, (LPARAM)obj)) delete obj;
}
}  // namespace

Worker::~Worker() { Stop(); }

bool Worker::Start(HWND notify) {
    m_notify = notify;
    m_thread = CreateThread(nullptr, 0, &Worker::ThreadProc, this, 0, nullptr);
    return m_thread != nullptr;
}

void Worker::Stop() {
    if (!m_thread) return;
    {
        LockGuard g(m_lock);
        m_quit = true;
        m_jobs.clear();
    }
    WakeConditionVariable(&m_cv);
    WaitForSingleObject(m_thread, 3000);
    CloseHandle(m_thread);
    m_thread = nullptr;
}

void Worker::Push(Job&& job) {
    {
        LockGuard g(m_lock);
        if (job.refresh)
            for (const Job& j : m_jobs)
                if (j.refresh) return;
        m_jobs.push_back(std::move(job));
    }
    WakeConditionVariable(&m_cv);
}

void Worker::Refresh() { Push(Job{}); }

void Worker::Run(OpRequest&& request) {
    Job job;
    job.refresh = false;
    job.request = std::move(request);
    Push(std::move(job));
}

DWORD WINAPI Worker::ThreadProc(LPVOID self) {
    // The Task Scheduler and shell links are COM objects.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    static_cast<Worker*>(self)->Loop();
    CoUninitialize();
    return 0;
}

void Worker::Loop() {
    for (;;) {
        Job job;
        {
            LockGuard g(m_lock);
            while (!m_quit && m_jobs.empty()) SleepConditionVariableSRW(&m_cv, &m_lock, INFINITE, 0);
            if (m_quit) return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        if (!job.refresh) {
            auto* res = new OpResult;
            OpRequest& r = job.request;
            switch (r.kind) {
                case OpKind::Enable:
                case OpKind::Disable:
                    for (const StartupEntry& e : r.entries)
                        res->outcomes.push_back({e.name, Entries::SetEnabled(e, r.kind == OpKind::Enable), {}});
                    break;
                case OpKind::Delete:
                    for (const StartupEntry& e : r.entries) {
                        OpOutcome o{e.name, 0, {}};
                        o.error = Entries::Delete(e, o.backup);
                        res->outcomes.push_back(std::move(o));
                    }
                    break;
                case OpKind::Restore: {
                    // Read the names first: a restored backup is removed.
                    const auto known = Entries::ListBackups();
                    for (const std::wstring& b : r.backups) {
                        OpOutcome o{b, 0, b};
                        for (const auto& k : known)
                            if (k.folder == b) o.name = k.name;
                        o.error = Entries::Restore(b);
                        res->outcomes.push_back(std::move(o));
                    }
                    break;
                }
                case OpKind::Add:
                    res->outcomes.push_back({r.name, Entries::Add(r.name, r.program, r.args, r.allUsers, r.shortcut), {}});
                    break;
            }
            res->request = std::move(r);
            Post(m_notify, WM_APP_OP_DONE, res);
        }
        // Read everything again (also after a change, to show its result).
        auto* list = new EntryList;
        Entries::Enumerate(*list, m_cache);
        Post(m_notify, WM_APP_ENTRIES, list);
    }
}
