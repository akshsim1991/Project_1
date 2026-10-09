// RenderWorker.cpp - background rendering / text / search thread.
#include "RenderWorker.h"

#include <algorithm>
#include <cwctype>

#include "Export.h"
#include "Util.h"

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

void RenderWorker::RequestTextRuns(uint32_t docId, int page) {
    Command c;
    c.type = Command::TextRuns;
    c.docId = docId;
    c.page = page;
    Push(std::move(c));
}

void RenderWorker::ListComments(uint32_t docId) {
    Command c;
    c.type = Command::Comments;
    c.docId = docId;
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

void RenderWorker::Edit(uint32_t docId, uint32_t newDocId, EditOp&& op) {
    Command c;
    c.type = Command::Edit;
    c.docId = docId;
    c.newDocId = newDocId;
    c.op = std::move(op);
    Push(std::move(c));
}

void RenderWorker::Undo(uint32_t docId, uint32_t newDocId) {
    Command c;
    c.type = Command::Undo;
    c.docId = docId;
    c.newDocId = newDocId;
    Push(std::move(c));
}

void RenderWorker::Redo(uint32_t docId, uint32_t newDocId) {
    Command c;
    c.type = Command::Redo;
    c.docId = docId;
    c.newDocId = newDocId;
    Push(std::move(c));
}

void RenderWorker::Save(uint32_t docId, const std::wstring& path, uint32_t flags) {
    Command c;
    c.type = Command::Save;
    c.docId = docId;
    c.text = path;
    c.flags = flags;
    Push(std::move(c));
}

void RenderWorker::Extract(uint32_t docId, std::vector<int>&& pages, const std::wstring& path,
                           bool separate) {
    Command c;
    c.type = Command::Extract;
    c.docId = docId;
    c.pages = std::move(pages);
    c.text = path;
    c.flags = separate ? 1 : 0;
    Push(std::move(c));
}

void RenderWorker::RenderForOcr(uint32_t docId, uint32_t jobId, int page, float scale, bool skipText) {
    Command c;
    c.type = Command::OcrPage;
    c.docId = docId;
    c.requestId = jobId;
    c.page = page;
    c.scale = scale;
    c.flags = skipText ? 1 : 0;
    Push(std::move(c));
}

void RenderWorker::StartExport(ExportJob&& job) {
    Command c;
    c.type = Command::Export;
    c.docId = job.docId;
    c.exportJob = std::move(job);
    Push(std::move(c));
}

void RenderWorker::CancelExport() {
    Command c;
    c.type = Command::CancelExport;
    Push(std::move(c));
}

template <typename T>
void RenderWorker::Post(UINT msg, T* obj) {
    if (!PostMessageW(m_notify, msg, 0, (LPARAM)obj)) delete obj;
}

PdfEngine* RenderWorker::Engine(uint32_t docId) {
    auto it = m_docs.find(docId);
    return it == m_docs.end() ? nullptr : it->second->Engine();
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
        enum { None, DoCommand, DoTile, DoThumb, DoPrint, DoExport, DoSearch } work = None;
        {
            LockGuard g(m_lock);
            // Sleep (zero CPU) until there is something to do.
            while (!m_quit && m_commands.empty() && m_wantedPos >= m_wanted.size() &&
                   m_thumbPos >= m_thumbs.size() && !m_printActive && !m_exportActive && !m_searchActive) {
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
            } else if (m_exportActive) {
                work = DoExport;
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
        } else if (work == DoExport) {
            ExportStep();
        } else if (work == DoSearch) {
            SearchStep();
        }
    }

    if (m_print.dc) EndPrint(true);
    m_docs.clear();  // closes every document and deletes private copies
    PdfEngine::DestroyLibrary();
}

void RenderWorker::Execute(Command& cmd) {
    switch (cmd.type) {
        case Command::Open: {
            auto* res = new DocLoadResult;
            res->docId = cmd.docId;
            res->path = cmd.text;
            auto doc = std::make_unique<DocEditor>();
            res->error = doc->Open(cmd.text, cmd.password, res->pageSizes);
            SecureZeroMemory(cmd.password.data(), cmd.password.size());
            if (res->error == OpenError::None) {
                doc->Engine()->LoadOutline(res->outline);
                doc->Engine()->GetInfo(res->info);
                m_docs[cmd.docId] = std::move(doc);
            }
            Post(WM_APP_DOC_LOADED, res);
            break;
        }
        case Command::Close:
            if (m_print.dc && m_print.docId == cmd.docId) EndPrint(true);
            m_docs.erase(cmd.docId);
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
            engine->ExtractPageInfo(cmd.page, res->chars, res->links, res->comments, res->fields);
            Post(WM_APP_TEXT_LAYER, res);
            break;
        }
        case Command::TextRuns: {
            PdfEngine* engine = Engine(cmd.docId);
            if (!engine) break;
            auto* res = new TextRunsResult;
            res->docId = cmd.docId;
            res->page = cmd.page;
            engine->GetTextRuns(cmd.page, res->runs);
            Post(WM_APP_TEXT_RUNS, res);
            break;
        }
        case Command::Comments: {
            PdfEngine* engine = Engine(cmd.docId);
            if (!engine) break;
            auto* res = new CommentListResult;
            res->docId = cmd.docId;
            engine->ListComments(res->comments);
            Post(WM_APP_COMMENTS, res);
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
            for (auto& d : m_docs)
                if (d.second->Engine()) d.second->Engine()->ReleasePages();
            break;
        case Command::Edit:
        case Command::Undo:
        case Command::Redo:
        case Command::Save:
            ExecuteEdit(cmd);
            break;
        case Command::Extract:
            ExecuteExtract(cmd);
            break;
        case Command::OcrPage: {
            auto* res = new OcrImage;
            res->docId = cmd.docId;
            res->jobId = cmd.requestId;
            res->page = cmd.page;
            res->scale = cmd.scale;
            PdfEngine* engine = Engine(cmd.docId);
            if (!engine || (cmd.flags && engine->CountLetters(cmd.page) >= 10))
                res->skipped = true;
            else
                engine->RenderPage(cmd.page, cmd.scale, false, res->pixels);  // empty: failed
            Post(WM_APP_OCR_IMAGE, res);
            break;
        }
        case Command::Export:
            if (m_exportActive) EndExport(false, {});  // one at a time
            m_export.job = std::move(cmd.exportJob);
            m_export.pos = 0;
            m_export.text.clear();
            m_export.lines.clear();
            m_export.files.clear();
            if (!Engine(m_export.job.docId) || m_export.job.pages.empty()) {
                EndExport(false, L"There is nothing to export.");
            } else {
                LockGuard g(m_lock);
                m_exportActive = true;
            }
            break;
        case Command::CancelExport:
            if (m_exportActive) EndExport(false, {});
            break;
    }
}

// Moves a document to a new id (see the header): work still queued for
// the old id is dropped, and a running print job or search follows.
void RenderWorker::Rekey(uint32_t oldId, uint32_t newId) {
    if (oldId == newId) return;
    auto it = m_docs.find(oldId);
    if (it == m_docs.end()) return;
    m_docs[newId] = std::move(it->second);
    m_docs.erase(it);
    if (m_print.docId == oldId) m_print.docId = newId;
    if (m_export.job.docId == oldId) m_export.job.docId = newId;
    LockGuard g(m_lock);
    if (m_search.docId == oldId) m_searchActive = false;  // the UI restarts it
}

// Inserted files are copied (tabs: saved) to private files first, so that
// undo/redo can replay the edit later whatever happens to the originals.
bool RenderWorker::PrepareSources(EditOp& op, DocEditor& target, std::wstring& error) {
    for (ImportSource& src : op.sources) {
        const std::wstring copy = MakeTempPdfPath();
        if (src.docId) {
            auto it = m_docs.find(src.docId);
            if (it == m_docs.end() || !it->second->Engine() ||
                !it->second->Engine()->WritePlainCopy(copy)) {
                DeleteFileW(copy.c_str());
                error = L"An open tab could not be added.";
                return false;
            }
            src.password.clear();  // the copy is written without a password
            src.docId = 0;
        } else if (!CopyFileW(src.path.c_str(), copy.c_str(), FALSE)) {
            error = FileNameFromPath(src.path) + L" could not be read.";
            return false;
        }
        target.AdoptTempFile(copy);
        src.path = copy;
    }
    return true;
}

void RenderWorker::ExecuteEdit(Command& cmd) {
    auto it = m_docs.find(cmd.docId);
    if (it == m_docs.end()) return;
    DocEditor& doc = *it->second;
    auto* res = new EditResult;
    res->docId = cmd.docId;
    res->flags = cmd.flags;
    switch (cmd.type) {
        case Command::Edit: {
            res->action = EditAction::Edit;
            const EditOp::Kind kind = cmd.op.kind;
            res->ok = PrepareSources(cmd.op, doc, res->error) &&
                      doc.Apply(std::move(cmd.op), res->error, res->focusPage, res->select);
            if (res->ok && doc.Engine()) {
                const int n = doc.Engine()->LastEditCount();
                if (kind == EditOp::FindReplace)
                    res->note = n == 1 ? L"1 place was changed." : std::to_wstring(n) + L" places were changed.";
                res->fontChanged = doc.Engine()->LastEditChangedFont();
            }
            break;
        }
        case Command::Undo:
            res->action = EditAction::Undo;
            res->ok = doc.Undo(res->error);
            break;
        case Command::Redo:
            res->action = EditAction::Redo;
            res->ok = doc.Redo(res->error, res->focusPage, res->select);
            break;
        default:
            res->action = EditAction::Save;
            res->ok = doc.Save(cmd.text, res->error);
            break;
    }
    if (cmd.type != Command::Save) {
        Rekey(cmd.docId, cmd.newDocId);
        res->docId = cmd.newDocId;
    }
    if (PdfEngine* engine = doc.Engine()) {
        engine->GetPageSizes(res->pageSizes);
        engine->LoadOutline(res->outline);
        engine->GetInfo(res->info);
    }
    res->canUndo = doc.CanUndo();
    res->canRedo = doc.CanRedo();
    res->dirty = doc.Dirty();
    res->path = doc.Path();
    Post(WM_APP_DOC_EDITED, res);
}

void RenderWorker::ExecuteExtract(Command& cmd) {
    PdfEngine* engine = Engine(cmd.docId);
    if (!engine) return;
    auto* res = new ExtractResult;
    // Each file is written under a temporary name and renamed when
    // complete, so a failure never leaves a half-written PDF behind.
    auto write = [&](const std::vector<int>& pages, const std::wstring& target) {
        const std::wstring temp = target + L".tmp";
        if (!engine->WritePagesTo(pages, temp) ||
            !MoveFileExW(temp.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            return false;
        }
        res->files.push_back(target);
        return true;
    };
    if (!cmd.flags) {
        res->ok = write(cmd.pages, cmd.text);
    } else {
        std::wstring stem = cmd.text;
        if (stem.size() > 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".pdf") == 0)
            stem.resize(stem.size() - 4);
        for (int p : cmd.pages) {
            std::wstring target = stem + L"-" + std::to_wstring(p + 1) + L".pdf";
            for (int n = 2; FileExists(target) && n < 1000; ++n)
                target = stem + L"-" + std::to_wstring(p + 1) + L" (" + std::to_wstring(n) + L").pdf";
            if (!write({p}, target)) {
                res->ok = false;
                break;
            }
        }
    }
    if (!res->ok)
        res->error = L"The pages could not be saved. The folder may be read-only, or the disk "
                     L"may be full.";
    Post(WM_APP_EXTRACTED, res);
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

namespace {
// "C:\a\Report.png" and page 3 -> "C:\a\Report-3.png" (or "Report-3 (2).png"
// if that exists already).
std::wstring NumberedPath(const std::wstring& path, int number) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    const bool hasExt = dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
    const std::wstring stem = hasExt ? path.substr(0, dot) : path, ext = hasExt ? path.substr(dot) : L"";
    std::wstring target = stem + L"-" + std::to_wstring(number) + ext;
    for (int n = 2; FileExists(target) && n < 1000; ++n)
        target = stem + L"-" + std::to_wstring(number) + L" (" + std::to_wstring(n) + L")" + ext;
    return target;
}
}  // namespace

// Exports one page per call (see StartExport).
void RenderWorker::ExportStep() {
    ExportJob& job = m_export.job;
    PdfEngine* engine = Engine(job.docId);
    if (!engine) {
        EndExport(false, {});  // the tab was closed
        return;
    }
    const size_t total = job.pages.size();
    if (m_export.pos < total) {
        const int page = job.pages[m_export.pos];
        switch (job.format) {
            case ExportFormat::Png:
            case ExportFormat::Jpeg: {
                PixelBuffer px;
                const std::wstring target = total == 1 ? job.path : NumberedPath(job.path, page + 1);
                if (!engine->RenderPage(page, job.dpi / 72.0f, true, px)) {
                    EndExport(false, L"Page " + std::to_wstring(page + 1) +
                                         L" could not be made into a picture. Try a lower resolution.");
                    return;
                }
                const bool saved = SavePicture(px, target, job.format == ExportFormat::Jpeg, job.dpi);
                px.Free();
                if (!saved) {
                    EndExport(false, L"\x201C" + FileNameFromPath(target) +
                                         L"\x201D could not be saved. The folder may be read-only, or the "
                                         L"disk may be full.");
                    return;
                }
                m_export.files.push_back(target);
                break;
            }
            case ExportFormat::Text: {
                const std::wstring text = TidyText(engine->PageText(page));
                if (!m_export.text.empty() && !text.empty()) m_export.text += L"\n";
                m_export.text += text;
                break;
            }
            case ExportFormat::Markdown:
                m_export.lines.emplace_back();
                engine->PageLines(page, m_export.lines.back());
                break;
        }
        ++m_export.pos;
        const bool pictures = job.format == ExportFormat::Png || job.format == ExportFormat::Jpeg;
        if (m_export.pos < total && (pictures || m_export.pos % 16 == 0)) {
            auto* res = new ExportProgress;
            res->done = (int)m_export.pos;
            res->total = (int)total;
            Post(WM_APP_EXPORT_PROGRESS, res);
        }
    }
    if (m_export.pos < total) return;

    bool noText = false;
    if (job.format == ExportFormat::Text || job.format == ExportFormat::Markdown) {
        const std::wstring text =
            job.format == ExportFormat::Text ? m_export.text : LinesToMarkdown(m_export.lines);
        noText = std::all_of(text.begin(), text.end(), [](wchar_t c) { return iswspace(c) != 0; });
        if (!WriteTextFile(job.path, text, job.format == ExportFormat::Text)) {
            EndExport(false, L"\x201C" + FileNameFromPath(job.path) +
                                 L"\x201D could not be saved. The folder may be read-only, or the disk "
                                 L"may be full.");
            return;
        }
        m_export.files.push_back(job.path);
    }
    auto* res = new ExportProgress;
    res->done = res->total = (int)total;
    res->finished = true;
    res->noText = noText;
    res->files = std::move(m_export.files);
    Post(WM_APP_EXPORT_PROGRESS, res);
    m_export = {};
    LockGuard g(m_lock);
    m_exportActive = false;
}

void RenderWorker::EndExport(bool ok, const std::wstring& error) {
    auto* res = new ExportProgress;
    res->finished = true;
    res->ok = ok;
    res->error = error;  // empty: cancelled, nothing to say
    res->files = std::move(m_export.files);
    Post(WM_APP_EXPORT_PROGRESS, res);
    m_export = {};
    LockGuard g(m_lock);
    m_exportActive = false;
}
