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
typedef struct fpdf_dest_t__* FPDF_DEST;
typedef struct fpdf_action_t__* FPDF_ACTION;
typedef struct fpdf_pageobject_t__* FPDF_PAGEOBJECT;

class PdfEngine {
public:
    PdfEngine();
    ~PdfEngine();
    PdfEngine(const PdfEngine&) = delete;
    PdfEngine& operator=(const PdfEngine&) = delete;

    static void InitLibrary();
    static void DestroyLibrary();

    // Opens `path`. Each open document (one per tab) has its own PdfEngine;
    // on failure the engine stays empty.
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

    // Text layer of one page (every character with its box, for selection),
    // its links (annotations plus URLs detected in the text) and comments.
    void ExtractPageInfo(int page, std::vector<TextChar>& chars, std::vector<LinkInfo>& links,
                         std::vector<CommentInfo>& comments, std::vector<FormField>& fields);

    // The editable lines of text on one page (see TextRun).
    void GetTextRuns(int page, std::vector<TextRun>& runs);
    // Every comment in the document (sticky notes, and markup with a comment).
    void ListComments(std::vector<std::pair<int, CommentInfo>>& out);

    // Bookmarks, flattened depth-first, and document metadata.
    void LoadOutline(std::vector<OutlineItem>& out);
    void GetInfo(DocInfo& info);

    // Prints one page on `dc` (between StartDoc/EndDoc), fitted to the paper.
    bool PrintPage(HDC dc, int page);

    static void ApplyPageColors(PixelBuffer& px, int mode);

    // Plain text between two caret positions (pages joined by line breaks).
    std::wstring ExtractText(TextPos from, TextPos to);

    // A whole page as displayed (upright, /Rotate applied) at `scale`
    // pixels per point; with annotations and form fields when `annots`.
    bool RenderPage(int page, float scale, bool annots, PixelBuffer& out);
    // Visible letters (not spaces) in a page's text layer: 0 for a scan.
    int CountLetters(int page);
    // The text of one page ("\r\n" between lines).
    std::wstring PageText(int page);
    // The lines of one page with their size and weight (Markdown export).
    void PageLines(int page, std::vector<TextLine>& lines);

    // Releases cached parsed pages (used when the window is minimised).
    void ReleasePages();

    // --- editing (driven by DocEditor) ----------------------------------------
    // Applies one change to the document in memory. On failure `error` says
    // why; the caller then rebuilds the document from its history, because
    // a partly applied change can not be rolled back here.
    bool ApplyEdit(const EditOp& op, std::wstring& error);
    // How many places the last edit changed (FindReplace), and whether it
    // had to use a different font because the original lacks some letters.
    int LastEditCount() const { return m_editCount; }
    bool LastEditChangedFont() const { return m_editFontChanged; }
    // Current page sizes (in points, after /Rotate), e.g. after an edit.
    void GetPageSizes(std::vector<SizeF>& out);
    // Writes the whole document (all edits included) to a new file.
    bool WriteTo(const std::wstring& path);
    // Writes the given pages, in order, as a new document.
    bool WritePagesTo(const std::vector<int>& pages, const std::wstring& path);

private:
    struct FileSource;
    static OpenError LoadDocument(const std::wstring& path, const std::string& password,
                                  std::unique_ptr<FileSource>& file, FPDF_DOCUMENT& doc);
    static bool ReadPageSizes(FPDF_DOCUMENT doc, std::vector<SizeF>& sizes);
    bool AddMarkup(const EditOp& op);
    bool EditText(const EditOp& op, std::wstring& error);
    bool ReplaceEverywhere(const EditOp& op, std::wstring& error);
    bool AddNote(const EditOp& op);
    bool ChangeAnnot(const EditOp& op, std::wstring& error);
    bool SetField(const EditOp& op, std::wstring& error);
    bool AddShape(const EditOp& op);
    bool AddStamp(const EditOp& op);
    bool AddImage(const EditOp& op);
    bool AddText(const EditOp& op, std::wstring& error);
    bool StyleText(const EditOp& op, std::wstring& error);
    bool AddWatermark(const EditOp& op, std::wstring& error);
    bool AddPageNumbers(const EditOp& op, std::wstring& error);
    bool AddOcrText(const EditOp& op);
    // Pages loaded for an edit, with the form-filling layer attached.
    FPDF_PAGE LoadEditPage(int index);
    void CloseEditPage(FPDF_PAGE page);
    struct FontCache;
    bool SetRunText(FPDF_PAGE page, int firstIndex, const std::vector<FPDF_PAGEOBJECT>& objs,
                    const std::wstring& text, FontCache& fonts, std::wstring& error);
    FPDF_PAGE GetPage(int index);  // uses the small parsed-page LRU
    void ReadDest(FPDF_DEST dest, LinkTarget& target);
    void ReadAction(FPDF_ACTION action, LinkTarget& target);

    FPDF_DOCUMENT m_doc = nullptr;
    std::unique_ptr<FileSource> m_file;
    int m_pageCount = 0;

    // Parsed pages are expensive to create (content stream parsing) but
    // tiles of the same page are requested in bursts, so the last few
    // parsed pages are kept. Front = most recently used.
    static constexpr size_t kMaxParsedPages = 4;
    std::vector<std::pair<int, FPDF_PAGE>> m_pages;

    // Form filling (AcroForm): PDFium's form layer for this document. It
    // draws field values and highlights, and changes fields.
    struct FormEnv;
    std::unique_ptr<FormEnv> m_form;

    int m_editCount = 0;
    bool m_editFontChanged = false;
};
