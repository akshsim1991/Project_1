// RenderTypes.h - data exchanged between the UI thread and the render worker.
//
// Ownership rule: every result object is allocated by the worker, posted to
// the main window with PostMessage, and deleted by the UI thread. Pixel
// buffers inside a result are moved into the page cache; whatever is left
// in a result when it is destroyed is freed by its destructor.
#pragma once
#include "Common.h"

// A 32-bit BGRx pixel buffer, top-down, stride = width * 4.
//
// Buffers are allocated with VirtualAlloc rather than the C heap so that
// freeing a tile returns its memory to the OS immediately instead of
// leaving it in a fragmented heap. This keeps the working set honest: when
// the cache evicts, RAM usage really goes down.
struct PixelBuffer {
    uint8_t* bits = nullptr;
    int width = 0;
    int height = 0;

    size_t Bytes() const { return (size_t)width * (size_t)height * 4; }
    bool Allocate(int w, int h);
    void Free();
};

enum class OpenError {
    None,
    NotFound,
    AccessDenied,
    ReadError,
    TooLarge,
    NotPdf,      // corrupted or not a PDF at all
    Password,    // password required or wrong password
    Security,    // unsupported encryption scheme
    NoPages,
    Unknown,
};

// Describes one tile the UI would like rendered. Tiles are rectangles of
// a page rendered at a specific scale; the UI computes all geometry so that
// UI and worker always agree on pixel sizes.
struct TileRequest {
    int page = 0;
    int scaleKey = 0;          // scale * 1000, see PdfView
    int tx = 0, ty = 0;        // tile column / row
    int x = 0, y = 0, w = 0, h = 0;  // tile rect inside the page bitmap
    int pageW = 0, pageH = 0;  // full page size in pixels at this scale

    bool SameTile(const TileRequest& o) const {
        return page == o.page && scaleKey == o.scaleKey && tx == o.tx && ty == o.ty;
    }
};

struct TileResult {
    uint32_t docId = 0;
    TileRequest req;
    PixelBuffer pixels;  // empty if allocation failed
    ~TileResult() { pixels.Free(); }
};

struct DocLoadResult {
    uint32_t docId = 0;
    std::wstring path;
    OpenError error = OpenError::None;
    std::vector<SizeF> pageSizes;
};

struct SearchHit {
    int page = 0;
    std::vector<RectF> rects;  // one match may span several lines
};

struct SearchPageResult {
    uint32_t docId = 0;
    uint32_t searchId = 0;
    int page = 0;
    int pagesDone = 0;
    bool finished = false;
    std::vector<SearchHit> hits;
};
