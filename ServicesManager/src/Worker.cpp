// Worker.cpp - background service queries and actions (see Worker.h).
#include "Worker.h"

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
    m_cancel = true;
    WakeConditionVariable(&m_cv);
    // A service that ignores a stop request can hold the thread for the
    // whole timeout; do not keep the closing window waiting for that.
    WaitForSingleObject(m_thread, 2000);
    CloseHandle(m_thread);
    m_thread = nullptr;
}

void Worker::Push(Job&& job) {
    {
        LockGuard g(m_lock);
        if (job.refresh) {
            for (Job& j : m_jobs)
                if (j.refresh) {  // one queued refresh is enough
                    j.full = j.full || job.full;
                    return;
                }
        }
        m_jobs.push_back(std::move(job));
    }
    WakeConditionVariable(&m_cv);
}

void Worker::Refresh(bool full) {
    Job job;
    job.full = full;
    Push(std::move(job));
}

void Worker::Run(OpRequest&& request) {
    Job job;
    job.refresh = false;
    job.request = std::move(request);
    m_busy = true;
    Push(std::move(job));
}

void Worker::CancelActions() { m_cancel = true; }

DWORD WINAPI Worker::ThreadProc(LPVOID self) {
    static_cast<Worker*>(self)->Loop();
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
        if (job.refresh) {
            DoRefresh(job.full);
        } else {
            DoBatch(job.request);
            DoRefresh();  // show the new states right away
        }
    }
}

void Worker::DoRefresh(bool full) {
    if (full) {
        m_cache.signatures.clear();
        m_cache.bootDelaysRead = false;
    }
    auto* snap = new Snapshot;
    Svc::Enumerate(snap->services, snap->error, m_cache);
    Post(m_notify, WM_APP_SNAPSHOT, snap);
}

void Worker::DoBatch(OpRequest& request) {
    m_cancel = false;
    auto* result = new OpBatchResult;
    const int total = (int)request.names.size();
    for (int i = 0; i < total; ++i) {
        if (m_cancel) {
            result->cancelled = true;
            break;
        }
        auto* progress = new OpProgress;
        progress->kind = request.kind;
        progress->index = i;
        progress->total = total;
        progress->displayName = i < (int)request.displayNames.size() ? request.displayNames[(size_t)i]
                                                                      : request.names[(size_t)i];
        Post(m_notify, WM_APP_OP_PROGRESS, progress);
        result->outcomes.push_back(Svc::Run(request, (size_t)i, m_cancel));
        if (result->outcomes.back().error == ERROR_CANCELLED) result->cancelled = true;
    }
    result->request = std::move(request);
    m_busy = false;
    Post(m_notify, WM_APP_OP_DONE, result);
}
