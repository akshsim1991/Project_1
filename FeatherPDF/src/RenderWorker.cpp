// RenderWorker.cpp - background rendering / text / search thread.
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

void RenderWorker::CloseDocument(uint32_t docId) {
    {
        LockGuard g(m_lock);
        if (m_wantedDocId == docId) {
            m_wanted.clear();
            m_wantedPos = 0;
        }
    }
    Command c;
    c.type = Command::Close;
    c.docId = docId;
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

void RenderWorker::SetWantedThumbs(uint32_t docId, std::vector<TileRequest>&& thumbs) {
    {
        LockGuard g(m_lock);
        m_thumbs = std::move(thumbs);
        m_thumbPos = 0;
        m_thumbDocId = docId;
    }
    WakeConditionVariable(&m_cv);
}

void RenderWorker::RenderImage(uint32_t docId, const TileRequest& req) {
    Command c;
    c.type = Command::Image;
    c.docId = docId;
    c.tile = req;
    Push(std::move(c));
}

void RenderWorker::StartPrint(PrintJob&& job) {
    Command c;
    c.type = Command::Print;
    c.docId = job.docId;
    c.job = std::move(job);
    Push(std::move(c));
}

void RenderWorker::CancelPrint() {
    Command c;
    c.type = Command::CancelPrint;
    Push(std::move(c));
}

void RenderWorker::RequestTextLayer(uint32_t docId, int page) {
    Command c;
    c.type = Command::TextLayer;
    c.docId = docId;
    c.page = page;
    Push(std::move(c));
}

void RenderWorker::CopyText(uint32_t docId, uint32_t requestId, TextPos from, TextPos to) {
    Command c;
    c.type = Command::Copy;
    c.docId = docId;
    c.requestId = requestId;
    c.from = from;
    c.to = to;
    Push(std::move(c));
}

void RenderWorker::StartSearch(uint32_t docId, uint32_t searchId, const std::wstring& query,
                               bool matchCase, int startPage) {
    Command c;
    c.type = Command::Search;
    c.docId = docId;
    c.requestId = searchId;
    c.text = query;
    c.matchCase = matchCase;
    c.page = startPage;
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

PdfEngine* RenderWorker::Engine(uint32_t docId) {
    auto it = m_engines.find(docId);
    return it == m_engines.end() ? nullptr : it->second.get();
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
        enum { None, DoCommand, DoTile, DoThumb, DoPrint, DoSearch } work = None;
        {
            LockGuard g(m_lock);
            // Sleep (zero CPU) until there is something to do.
            while (!m_quit && m_commands.empty() && m_wantedPos >= m_wanted.size() &&
                   m_thumbPos >= m_thumbs.size() && !m_printActive && !m_searchActive) {
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
            } else if (m_thumbPos < m_thumbs.size()) {
                tile = m_thumbs[m_thumbPos++];
                tileDoc = m_thumbDocId;
                work = DoThumb;
            } else if (m_printActive) {
                work = DoPrint;
            } else {
                work = DoSearch;
            }
        }

        if (work == DoCommand) {
            Execute(cmd);
        } else if (work == DoTile) {
            if (PdfEngine* engine = Engine(tileDoc)) {
                auto* res = new TileResult;
                res->docId = tileDoc;
                res->req = tile;
                engine->RenderTile(tile, res->pixels);  // empty pixels = failure
                Post(WM_APP_TILE_READY, res);
            }
            LockGuard g(m_lock);
            m_hasInFlight = false;
        } else if (work == DoThumb) {
            if (PdfEngine* engine = Engine(tileDoc)) {
                auto* res = new TileResult;
                res->docId = tileDoc;
                res->req = tile;
                engine->RenderTile(tile, res->pixels);
                Post(WM_APP_THUMB_READY, res);
            }
        } else if (work == DoPrint) {
            PrintStep();
        } else if (work == DoSearch) {
            SearchStep();
        }
    }

    if (m_print.dc) EndPrint(true);
    m_engines.clear();  // closes every document
    PdfEngine::DestroyLibrary();
}

void RenderWorker::Execute(Command& cmd) {
    switch (cmd.type) {
        case Command::Open: {
            auto* res = new DocLoadResult;
            res->docId = cmd.docId;
            res->path = cmd.text;
            auto engine = std::make_unique<PdfEngine>();
            res->error = engine->Open(cmd.text, cmd.password, res->pageSizes);
            SecureZeroMemory(cmd.password.data(), cmd.password.size());
            if (res->error == OpenError::None) {
                engine->LoadOutline(res->outline);
                engine->GetInfo(res->info);
                m_engines[cmd.docId] = std::move(engine);
            }
            Post(WM_APP_DOC_LOADED, res);
            break;
        }
        case Command::Close:
            if (m_print.dc && m_print.docId == cmd.docId) EndPrint(true);
            m_engines.erase(cmd.docId);
            if (m_search.docId == cmd.docId) {
                LockGuard g(m_lock);
                m_searchActive = false;
            }
            break;
        case Command::TextLayer: {
            PdfEngine* engine = Engine(cmd.docId);
            if (!engine) break;
            auto* res = new TextLayerResult;
            res->docId = cmd.docId;
            res->page = cmd.page;
            engine->ExtractPageInfo(cmd.page, res->chars, res->links);
            Post(WM_APP_TEXT_LAYER, res);
            break;
        }
        case Command::Copy: {
            PdfEngine* engine = Engine(cmd.docId);
            if (!engine) break;
            auto* res = new TextCopyResult;
            res->docId = cmd.docId;
            res->requestId = cmd.requestId;
            res->text = engine->ExtractText(cmd.from, cmd.to);
            Post(WM_APP_TEXT_COPIED, res);
            break;
        }
        case Command::Search: {
            if (!Engine(cmd.docId) || cmd.text.empty()) break;
            m_search.docId = cmd.docId;
            m_search.id = cmd.requestId;
            m_search.query = cmd.text;
            m_search.matchCase = cmd.matchCase;
            m_search.startPage = cmd.page;
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
        case Command::Image: {
            PdfEngine* engine = Engine(cmd.docId);
            if (!engine) break;
            auto* res = new TileResult;
            res->docId = cmd.docId;
            res->req = cmd.tile;
            engine->RenderTile(cmd.tile, res->pixels);
            Post(WM_APP_IMAGE_READY, res);
            break;
        }
        case Command::Print: {
            if (m_print.dc) EndPrint(true);  // only one job at a time
            m_print = std::move(cmd.job);
            m_printPos = 0;
            DOCINFOW di{sizeof(di)};
            di.lpszDocName = m_print.docName.c_str();
            if (!m_print.dc || !Engine(m_print.docId) || StartDocW(m_print.dc, &di) <= 0) {
                if (m_print.dc) DeleteDC(m_print.dc);
                m_print = PrintJob{};
                PostMessageW(m_notify, WM_APP_PRINT_PROGRESS, 0, -1);
                break;
            }
            LockGuard g(m_lock);
            m_printActive = true;
            break;
        }
        case Command::CancelPrint:
            if (m_print.dc) EndPrint(true);
            break;
        case Command::Trim:
            for (auto& e : m_engines) e.second->ReleasePages();
            break;
    }
}

void RenderWorker::SearchStep() {
    PdfEngine* engine = Engine(m_search.docId);
    const int count = engine ? engine->PageCount() : 0;
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
    engine->SearchPage(page, m_search.query, m_search.matchCase, hits);
    m_search.done++;
    const bool finished = m_search.done >= count;

    // Only post when there is something to show, to report progress
    // occasionally, or to say we are done: avoids flooding the UI queue.
    if (!hits.empty() || finished || (m_search.done % 64) == 0) {
        auto* res = new SearchPageResult;
        res->docId = m_search.docId;
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

// Prints one page per call so rendering stays responsive during long jobs.
void RenderWorker::PrintStep() {
    PdfEngine* engine = Engine(m_print.docId);
    if (!engine || !m_print.dc) {
        EndPrint(true);
        return;
    }
    if (m_printPos < m_print.pages.size()) {
        if (!engine->PrintPage(m_print.dc, m_print.pages[m_printPos])) {
            EndPrint(true);
            return;
        }
        ++m_printPos;
        PostMessageW(m_notify, WM_APP_PRINT_PROGRESS, (WPARAM)m_printPos,
                     (LPARAM)m_print.pages.size());
    }
    if (m_printPos >= m_print.pages.size()) EndPrint(false);
}

void RenderWorker::EndPrint(bool abort) {
    if (m_print.dc) {
        if (abort)
            AbortDoc(m_print.dc);
        else
            EndDoc(m_print.dc);
        DeleteDC(m_print.dc);
    }
    if (abort) PostMessageW(m_notify, WM_APP_PRINT_PROGRESS, 0, -1);
    m_print = PrintJob{};
    m_printPos = 0;
    LockGuard g(m_lock);
    m_printActive = false;
}
