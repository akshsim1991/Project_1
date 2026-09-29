// PdfEngine.h - thin wrapper around PDFium (the "PDF document manager" and
// "rendering engine" layers).
//
// THREADING: PDFium is not thread-safe. Every PdfEngine method, including
// library initialisation, must be called from the single render worker
// thread. The UI thread never touches PDFium directly.
#pragma once
#include <memory>

#include "RenderTypes.h"

typedef struct fpdf_document_t__* FPDF_DOCUMENT;
typedef struct fpdf_page_t__* FPDF_PAGE;

class PdfEngine {
public:
    PdfEngine();
    ~PdfEngine();
    PdfEngine(const PdfEngine&) = delete;
    PdfEngine& operator=(const PdfEngine&) = delete;

    static void InitLibrary();
    static void DestroyLibrary();

    // Opens `path`. On success the previously open document (if any) is
    // closed and replaced; on failure the previous document stays open so
    // the user keeps reading it.
    OpenError Open(const std::wstring& path, const std::string& password,
                   std::vector<SizeF>& pageSizes);
    void Close();
    bool IsOpen() const { return m_doc != nullptr; }
    int PageCount() const { return m_pageCount; }

    // Renders one tile into `out` (allocated here). Returns false if the
    // buffer could not be allocated. A page that fails to load is rendered
    // as a light grey tile instead of failing, so the UI does not retry it
    // forever.
    bool RenderTile(const TileRequest& req, PixelBuffer& out);

    // Finds all occurrences of `query` on one page. Rectangles are returned
    // in page points relative to the top-left of the displayed page.
    void SearchPage(int page, const std::wstring& query, bool matchCase,
                    std::vector<SearchHit>& hits);

    // Releases cached parsed pages (used when the window is minimised).
    void ReleasePages();

private:
    struct FileSource;
    FPDF_PAGE GetPage(int index);  // uses the small parsed-page LRU

    FPDF_DOCUMENT m_doc = nullptr;
    std::unique_ptr<FileSource> m_file;
    int m_pageCount = 0;

    // Parsed pages are expensive to create (content stream parsing) but
    // tiles of the same page are requested in bursts, so the last few
    // parsed pages are kept. Front = most recently used.
    static constexpr size_t kMaxParsedPages = 4;
    std::vector<std::pair<int, FPDF_PAGE>> m_pages;
};
