// RenderWorker.h - the single background thread that owns PDFium.
//
// PDFium is not thread-safe (not even across documents), so every open
// document - one per tab - lives in this one thread, each in its own
// DocEditor (PdfEngine + edit history) keyed by document id.
//
// Edits, undo and redo move the document to a new id chosen by the UI.
// Tiles, text layers and search results already queued for the old id are
// then ignored by the UI instead of being shown with the wrong content.
//
// The UI thread never blocks on PDF work. It talks to the worker through:
//   * commands (open/close document, text layer, copy text, search,
//     trim memory), executed in FIFO order;
//   * the "wanted tiles" list, which is REPLACED (not appended) every time
//     the visible view repaints. Scrolling quickly through 1000 pages
//     therefore never builds a backlog: stale requests simply disappear.
//
// Priority inside the worker loop: commands > visible/prefetch tiles >
// thumbnails > printing (one page per step) > incremental search (one page
// per step). Printing and search therefore never delay rendering of what
// the user is looking at.
#pragma once
#include <deque>
#include <map>
#include <memory>

#include "DocEditor.h"

class RenderWorker {
public:
    RenderWorker();
    ~RenderWorker();

    bool Start(HWND notifyWindow);
    void Stop();

    void OpenDocument(uint32_t docId, const std::wstring& path, const std::string& password);
    void CloseDocument(uint32_t docId);
    void SetWantedTiles(uint32_t docId, std::vector<TileRequest>&& tiles);
    void SetWantedThumbs(uint32_t docId, std::vector<TileRequest>&& thumbs);
    void RenderImage(uint32_t docId, const TileRequest& req);  // -> WM_APP_IMAGE_READY
    void StartPrint(PrintJob&& job);                          // -> WM_APP_PRINT_PROGRESS
    void CancelPrint();
    void RequestTextLayer(uint32_t docId, int page);
    void RequestTextRuns(uint32_t docId, int page);  // -> WM_APP_TEXT_RUNS
    void ListComments(uint32_t docId);               // -> WM_APP_COMMENTS
    void CopyText(uint32_t docId, uint32_t requestId, TextPos from, TextPos to);
    void StartSearch(uint32_t docId, uint32_t searchId, const std::wstring& query,
                     bool matchCase, int startPage);
    void CancelSearch();
    void TrimMemory();

    // Editing (results arrive as WM_APP_DOC_EDITED / WM_APP_EXTRACTED).
    void Edit(uint32_t docId, uint32_t newDocId, EditOp&& op);
    void Undo(uint32_t docId, uint32_t newDocId);
    void Redo(uint32_t docId, uint32_t newDocId);
    void Save(uint32_t docId, const std::wstring& path, uint32_t flags);
    // Writes `pages` to `path`, or with `separate` one file per page named
    // after `path` ("name-3.pdf").
    void Extract(uint32_t docId, std::vector<int>&& pages, const std::wstring& path, bool separate);

    // Text recognition: renders `page` at `scale` pixels per point for the
    // recognition thread (-> WM_APP_OCR_IMAGE). With `skipText`, a page that
    // already has text is reported as skipped instead.
    void RenderForOcr(uint32_t docId, uint32_t jobId, int page, float scale, bool skipText);
    // Exports pages as pictures or text, one page per step so the view
    // stays responsive (-> WM_APP_EXPORT_PROGRESS).
    void StartExport(ExportJob&& job);
    void CancelExport();

private:
    struct Command {
        enum Type {
            Open, Close, TextLayer, Copy, Search, CancelSearch, Trim, Image, Print, CancelPrint,
            Edit, Undo, Redo, Save, Extract, TextRuns, Comments, OcrPage, Export, CancelExport
        } type = Open;
        uint32_t docId = 0;
        uint32_t newDocId = 0;   // Edit / Undo / Redo
        uint32_t flags = 0;      // Save: kAfterSave*; Extract: separate files
        uint32_t requestId = 0;  // search id / copy request id
        std::wstring text;
        std::string password;
        bool matchCase = false;
        int page = 0;
        TextPos from, to;
        TileRequest tile;
        PrintJob job;
        EditOp op;
        std::vector<int> pages;  // Extract
        float scale = 1;         // OcrPage
        ExportJob exportJob;
    };

    static DWORD WINAPI ThreadProc(LPVOID self);
    void Run();
    void Execute(Command& cmd);
    void SearchStep();
    void PrintStep();
    void EndPrint(bool abort);
    void ExportStep();
    void EndExport(bool ok, const std::wstring& error);
    void Push(Command&& cmd);
    PdfEngine* Engine(uint32_t docId);
    void ExecuteEdit(Command& cmd);
    void ExecuteExtract(Command& cmd);
    bool PrepareSources(EditOp& op, DocEditor& target, std::wstring& error);
    void Rekey(uint32_t oldId, uint32_t newId);
    template <typename T>
    void Post(UINT msg, T* obj);

    HANDLE m_thread = nullptr;
    HWND m_notify = nullptr;

    // --- shared state, guarded by m_lock -----------------------------------
    SRWLOCK m_lock = SRWLOCK_INIT;
    CONDITION_VARIABLE m_cv = CONDITION_VARIABLE_INIT;
    bool m_quit = false;
    std::deque<Command> m_commands;
    std::vector<TileRequest> m_wanted;
    size_t m_wantedPos = 0;
    uint32_t m_wantedDocId = 0;
    TileRequest m_inFlight;
    bool m_hasInFlight = false;
    bool m_searchActive = false;
    std::vector<TileRequest> m_thumbs;
    size_t m_thumbPos = 0;
    uint32_t m_thumbDocId = 0;
    bool m_printActive = false;
    bool m_exportActive = false;

    // --- worker-thread-only state ------------------------------------------
    std::map<uint32_t, std::unique_ptr<DocEditor>> m_docs;
    struct {
        uint32_t docId = 0;
        uint32_t id = 0;
        std::wstring query;
        bool matchCase = false;
        int startPage = 0;
        int done = 0;
    } m_search;
    PrintJob m_print;
    size_t m_printPos = 0;
    struct {
        ExportJob job;
        size_t pos = 0;
        std::wstring text;                         // plain text so far
        std::vector<std::vector<TextLine>> lines;  // Markdown: lines of each page
        std::vector<std::wstring> files;
    } m_export;
};
