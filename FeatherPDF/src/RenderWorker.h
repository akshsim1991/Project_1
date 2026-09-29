// RenderWorker.h - the single background thread that owns PDFium.
//
// PDFium is not thread-safe (not even across documents), so every open
// document - one per tab - lives in this one thread, each in its own
// PdfEngine keyed by document id.
//
// The UI thread never blocks on PDF work. It talks to the worker through:
//   * commands (open/close document, text layer, copy text, search,
//     trim memory), executed in FIFO order;
//   * the "wanted tiles" list, which is REPLACED (not appended) every time
//     the visible view repaints. Scrolling quickly through 1000 pages
//     therefore never builds a backlog: stale requests simply disappear.
//
// Priority inside the worker loop: commands > visible/prefetch tiles >
// incremental search (one page per step). Search therefore never delays
// rendering of what the user is looking at.
#pragma once
#include <deque>
#include <map>
#include <memory>

#include "PdfEngine.h"

class RenderWorker {
public:
    RenderWorker();
    ~RenderWorker();

    bool Start(HWND notifyWindow);
    void Stop();

    void OpenDocument(uint32_t docId, const std::wstring& path, const std::string& password);
    void CloseDocument(uint32_t docId);
    void SetWantedTiles(uint32_t docId, std::vector<TileRequest>&& tiles);
    void RequestTextLayer(uint32_t docId, int page);
    void CopyText(uint32_t docId, uint32_t requestId, TextPos from, TextPos to);
    void StartSearch(uint32_t docId, uint32_t searchId, const std::wstring& query,
                     bool matchCase, int startPage);
    void CancelSearch();
    void TrimMemory();

private:
    struct Command {
        enum Type { Open, Close, TextLayer, Copy, Search, CancelSearch, Trim } type = Open;
        uint32_t docId = 0;
        uint32_t requestId = 0;  // search id / copy request id
        std::wstring text;
        std::string password;
        bool matchCase = false;
        int page = 0;
        TextPos from, to;
    };

    static DWORD WINAPI ThreadProc(LPVOID self);
    void Run();
    void Execute(Command& cmd);
    void SearchStep();
    void Push(Command&& cmd);
    PdfEngine* Engine(uint32_t docId);
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

    // --- worker-thread-only state ------------------------------------------
    std::map<uint32_t, std::unique_ptr<PdfEngine>> m_engines;
    struct {
        uint32_t docId = 0;
        uint32_t id = 0;
        std::wstring query;
        bool matchCase = false;
        int startPage = 0;
        int done = 0;
    } m_search;
};
