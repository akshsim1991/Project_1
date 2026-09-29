// RenderWorker.cpp - background rendering / search thread.
#include "RenderWorker.h"

namespace {
class LockGuard {
public:
    explicit LockGuard(SRWLOCK& l) : m_l(l) { AcquireSRWLockExclusive(&m_l); }
    ~LockGuard() { ReleaseSRWLockExclusive(&m_l); }

private:
    SRWLOCK& m_l;
};
}  // namespace

RenderWorker::RenderWorker() = default;

RenderWorker::~RenderWorker() { Stop(); }

bool RenderWorker::Start(HWND notifyWindow) {
    m_notify = notifyWindow;
    m_thread = CreateThread(nullptr, 0, &RenderWorker::ThreadProc, this, 0, nullptr);
    return m_thread != nullptr;
}

void RenderWorker::Stop() {
    if (!m_thread) return;
    {
        LockGuard g(m_lock);
        m_quit = true;
    }
    WakeConditionVariable(&m_cv);
    // A single tile render is short; if PDFium is stuck on a pathological
    // page we do not hold up application exit for long.
    WaitForSingleObject(m_thread, 3000);
    CloseHandle(m_thread);
    m_thread = nullptr;
}

void RenderWorker::Push(Command&& cmd) {
    {
        LockGuard g(m_lock);
        if (cmd.type == Command::Open) {
            // A new document makes any queued work obsolete.
            m_wanted.clear();
            m_wantedPos = 0;
            m_searchActive = false;
        }
        m_commands.push_back(std::move(cmd));
    }
    WakeConditionVariable(&m_cv);
}

void RenderWorker::OpenDocument(uint32_t docId, const std::wstring& path,
                                const std::string& password) {
    Command c;
    c.type = Command::Open;
    c.docId = docId;
    c.text = path;
    c.password = password;
    Push(std::move(c));
}

void RenderWorker::SetWantedTiles(uint32_t docId, std::vector<TileRequest>&& tiles) {
    {
        LockGuard g(m_lock);
        // Do not queue the tile that is being rendered right now again.
        if (m_hasInFlight) {
            for (size_t i = 0; i < tiles.size(); ++i) {
                if (tiles[i].SameTile(m_inFlight)) {
                    tiles.erase(tiles.begin() + (ptrdiff_t)i);
                    break;
                }
            }
        }
        m_wanted = std::move(tiles);
        m_wantedPos = 0;
        m_wantedDocId = docId;
    }
    WakeConditionVariable(&m_cv);
}

void RenderWorker::StartSearch(uint32_t docId, uint32_t searchId, const std::wstring& query,
                               bool matchCase, int startPage) {
    Command c;
    c.type = Command::Search;
    c.docId = docId;
    c.searchId = searchId;
    c.text = query;
    c.matchCase = matchCase;
    c.startPage = startPage;
    Push(std::move(c));
}

void RenderWorker::CancelSearch() {
    Command c;
    c.type = Command::CancelSearch;
    Push(std::move(c));
}

void RenderWorker::TrimMemory() {
    Command c;
    c.type = Command::Trim;
    Push(std::move(c));
}

template <typename T>
void RenderWorker::Post(UINT msg, T* obj) {
    if (!PostMessageW(m_notify, msg, 0, (LPARAM)obj)) delete obj;
}

DWORD WINAPI RenderWorker::ThreadProc(LPVOID self) {
    static_cast<RenderWorker*>(self)->Run();
    return 0;
}

void RenderWorker::Run() {
    // Rendering is background work: keep the UI thread snappy on low-core
    // machines.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    PdfEngine::InitLibrary();

    for (;;) {
        Command cmd;
        TileRequest tile;
        uint32_t tileDoc = 0;
        enum { None, DoCommand, DoTile, DoSearch } work = None;
        {
            LockGuard g(m_lock);
            // Sleep (zero CPU) until there is something to do.
            while (!m_quit && m_commands.empty() && m_wantedPos >= m_wanted.size() &&
                   !m_searchActive) {
                SleepConditionVariableSRW(&m_cv, &m_lock, INFINITE, 0);
            }
            if (m_quit) break;
            if (!m_commands.empty()) {
                cmd = std::move(m_commands.front());
                m_commands.pop_front();
                work = DoCommand;
            } else if (m_wantedPos < m_wanted.size()) {
                tile = m_wanted[m_wantedPos++];
                tileDoc = m_wantedDocId;
                m_inFlight = tile;
                m_hasInFlight = true;
                work = DoTile;
            } else {
                work = DoSearch;
            }
        }

        if (work == DoCommand) {
            Execute(cmd);
        } else if (work == DoTile) {
            if (tileDoc == m_docId && m_engine.IsOpen()) {
                auto* res = new TileResult;
                res->docId = tileDoc;
                res->req = tile;
                m_engine.RenderTile(tile, res->pixels);  // empty pixels = failure
                Post(WM_APP_TILE_READY, res);
            }
            LockGuard g(m_lock);
            m_hasInFlight = false;
        } else if (work == DoSearch) {
            SearchStep();
        }
    }

    m_engine.Close();
    PdfEngine::DestroyLibrary();
}

void RenderWorker::Execute(Command& cmd) {
    switch (cmd.type) {
        case Command::Open: {
            auto* res = new DocLoadResult;
            res->docId = cmd.docId;
            res->path = cmd.text;
            res->error = m_engine.Open(cmd.text, cmd.password, res->pageSizes);
            SecureZeroMemory(cmd.password.data(), cmd.password.size());
            if (res->error == OpenError::None) m_docId = cmd.docId;
            Post(WM_APP_DOC_LOADED, res);
            break;
        }
        case Command::Search: {
            if (cmd.docId != m_docId || !m_engine.IsOpen() || cmd.text.empty()) break;
            m_search.id = cmd.searchId;
            m_search.query = cmd.text;
            m_search.matchCase = cmd.matchCase;
            m_search.startPage = cmd.startPage;
            m_search.done = 0;
            LockGuard g(m_lock);
            m_searchActive = true;
            break;
        }
        case Command::CancelSearch: {
            LockGuard g(m_lock);
            m_searchActive = false;
            break;
        }
        case Command::Trim:
            m_engine.ReleasePages();
            break;
    }
}

void RenderWorker::SearchStep() {
    const int count = m_engine.PageCount();
    if (count <= 0) {
        LockGuard g(m_lock);
        m_searchActive = false;
        return;
    }
    // Search starts at the page the user is on and wraps around, so the
    // first hit reported is the one "after" the current position.
    int start = m_search.startPage;
    if (start < 0 || start >= count) start = 0;
    const int page = (start + m_search.done) % count;

    std::vector<SearchHit> hits;
    m_engine.SearchPage(page, m_search.query, m_search.matchCase, hits);
    m_search.done++;
    const bool finished = m_search.done >= count;

    // Only post when there is something to show, to report progress
    // occasionally, or to say we are done: avoids flooding the UI queue.
    if (!hits.empty() || finished || (m_search.done % 64) == 0) {
        auto* res = new SearchPageResult;
        res->docId = m_docId;
        res->searchId = m_search.id;
        res->page = page;
        res->pagesDone = m_search.done;
        res->finished = finished;
        res->hits = std::move(hits);
        Post(WM_APP_SEARCH_RESULT, res);
    }
    if (finished) {
        LockGuard g(m_lock);
        m_searchActive = false;
    }
}
