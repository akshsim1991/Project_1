// RenderWorker.h - the single background thread that owns PDFium.
//
// The UI thread never blocks on PDF work. It talks to the worker through:
//   * commands (open document, start/cancel search, trim memory), executed
//     in FIFO order;
//   * the "wanted tiles" list, which is REPLACED (not appended) every time
//     the view repaints. Scrolling quickly through 1000 pages therefore
//     never builds a backlog: stale requests simply disappear.
//
// Priority inside the worker loop: commands > visible/prefetch tiles >
// incremental search (one page per step). Search therefore never delays
// rendering of what the user is looking at.
#pragma once
#include <deque>

#include "PdfEngine.h"

class RenderWorker {
public:
    RenderWorker();
    ~RenderWorker();

    bool Start(HWND notifyWindow);
    void Stop();

    void OpenDocument(uint32_t docId, const std::wstring& path, const std::string& password);
    void SetWantedTiles(uint32_t docId, std::vector<TileRequest>&& tiles);
    void StartSearch(uint32_t docId, uint32_t searchId, const std::wstring& query,
                     bool matchCase, int startPage);
    void CancelSearch();
    void TrimMemory();

private:
    struct Command {
        enum Type { Open, Search, CancelSearch, Trim } type = Open;
        uint32_t docId = 0;
        uint32_t searchId = 0;
        std::wstring text;
        std::string password;
        bool matchCase = false;
        int startPage = 0;
    };

    static DWORD WINAPI ThreadProc(LPVOID self);
    void Run();
    void Execute(Command& cmd);
    void SearchStep();
    void Push(Command&& cmd);
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
    PdfEngine m_engine;
    uint32_t m_docId = 0;  // id of the document currently open in m_engine
    struct {
        uint32_t id = 0;
        std::wstring query;
        bool matchCase = false;
        int startPage = 0;
        int done = 0;
    } m_search;
};
