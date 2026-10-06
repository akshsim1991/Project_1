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
    int rotate = 0;            // extra view rotation, quarter turns clockwise
    int colorMode = 0;         // PageColors: 0 normal, 1 dark (inverted), 2 dimmed

    bool SameTile(const TileRequest& o) const {
        return page == o.page && scaleKey == o.scaleKey && tx == o.tx && ty == o.ty &&
               rotate == o.rotate && colorMode == o.colorMode;
    }
};

// Page colour modes applied to rendered pixels (not to printing or copies).
enum PageColors { kColorsNormal = 0, kColorsDark = 1, kColorsDim = 2 };

struct TileResult {
    uint32_t docId = 0;
    TileRequest req;
    PixelBuffer pixels;  // empty if allocation failed
    ~TileResult() { pixels.Free(); }
};

// Where a link or bookmark goes. `destY` is in PDF user space (points
// from the bottom of the page) or negative when the target has no position.
struct LinkTarget {
    int page = -1;          // internal destination, or -1
    float destY = -1;
    std::wstring uri;       // external destination (http/https/mailto only)
};

struct LinkInfo {
    RectF rect;             // page points, top-left origin, unrotated
    LinkTarget target;
};

// One entry of the document outline (bookmarks), flattened depth-first.
struct OutlineItem {
    std::wstring title;
    int level = 0;
    LinkTarget target;
};

struct DocInfo {
    std::wstring title, author, subject, keywords, creator, producer, created, modified;
    int version = 0;        // e.g. 17 for PDF 1.7
    bool encrypted = false;
};

struct DocLoadResult {
    uint32_t docId = 0;
    std::wstring path;
    OpenError error = OpenError::None;
    std::vector<SizeF> pageSizes;
    std::vector<OutlineItem> outline;
    DocInfo info;
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

// One character of a page's text layer (used for text selection).
struct TextChar {
    RectF box;           // page points, top-left origin; valid if hasBox
    uint32_t cp = 0;     // Unicode code point
    bool hasBox = false; // false for characters PDFium generated (\r\n, spaces)
};

// A comment: a sticky note, or a text markup (highlight, underline,
// strike-out) that may carry a comment.
struct CommentInfo {
    RectF rect;               // page points, top-left origin, unrotated
    int annot = 0;            // index among the page's annotations
    int subtype = 0;          // kAnnotNote or a MarkupType
    std::wstring text;        // the comment (may be empty for markup)
    std::wstring author;
    std::wstring date;        // PDF date string ("D:2026...")
};

// Form field types (the values of PDFium's FPDF_FORMFIELD_*).
enum FieldType {
    kFieldUnknown = 0, kFieldPushButton = 1, kFieldCheckBox = 2, kFieldRadio = 3,
    kFieldComboBox = 4, kFieldListBox = 5, kFieldText = 6, kFieldSignature = 7,
};

// A fillable form field on a page (one widget annotation).
struct FormField {
    RectF rect;               // page points, top-left origin, unrotated
    int annot = 0;            // index among the page's annotations
    int type = kFieldUnknown;
    std::wstring name, value;
    bool checked = false;
    bool readOnly = false, multiline = false, password = false;
    std::vector<std::wstring> options;  // combo and list boxes
    int selected = -1;
};

struct TextLayerResult {
    uint32_t docId = 0;
    int page = 0;
    std::vector<TextChar> chars;
    std::vector<LinkInfo> links;  // link annotations and URLs found in the text
    std::vector<CommentInfo> comments;
    std::vector<FormField> fields;
};

// A line of editable text: consecutive text objects of the page's content
// on one baseline, in the same font and size. Editing replaces them all.
struct TextRun {
    RectF rect;               // page points, top-left origin, unrotated
    std::wstring text;
    int first = 0, count = 0; // page object indices
    float size = 0;           // font size as shown, in points
    bool bold = false, italic = false, serif = false, mono = false;
};

struct TextRunsResult {
    uint32_t docId = 0;
    int page = 0;
    std::vector<TextRun> runs;
};

struct CommentListResult {
    uint32_t docId = 0;
    std::vector<std::pair<int, CommentInfo>> comments;  // {page, comment}
};

// A caret position in the document: before character `index` of `page`.
struct TextPos {
    int page = 0;
    int index = 0;
    bool operator<(const TextPos& o) const {
        return page != o.page ? page < o.page : index < o.index;
    }
    bool operator==(const TextPos& o) const { return page == o.page && index == o.index; }
};

struct TextCopyResult {
    uint32_t docId = 0;
    uint32_t requestId = 0;
    std::wstring text;
};

// A print job handed to the worker. The printer DC belongs to the worker
// from then on (it calls EndDoc and DeleteDC).
struct PrintJob {
    uint32_t docId = 0;
    HDC dc = nullptr;
    std::wstring docName;
    std::vector<int> pages;  // in printing order (copies already expanded)
};

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

// Text markup annotation types (values are PDFium's FPDF_ANNOT_* subtypes).
enum MarkupType { kMarkupHighlight = 9, kMarkupUnderline = 10, kMarkupStrikeOut = 12 };
// Other annotation types (PDFium's FPDF_ANNOT_* values).
enum {
    kAnnotNote = 1, kAnnotFreeText = 3, kAnnotLine = 4, kAnnotSquare = 5, kAnnotCircle = 6,
    kAnnotPolygon = 7, kAnnotPolyline = 8, kMarkupSquiggly = 11, kAnnotStamp = 13, kAnnotInk = 15,
};

// Drawing tools (EditOp::AddShape).
enum ShapeKind { kShapeRect = 0, kShapeEllipse = 1, kShapeLine = 2, kShapeArrow = 3, kShapePen = 4 };

struct PointF {
    float x = 0, y = 0;
};

// A PDF whose pages are inserted. The worker replaces `path` with a private
// copy before applying the edit, so undo/redo can replay it later even if
// the original file changes or the source tab is closed.
struct ImportSource {
    std::wstring path;
    uint32_t docId = 0;    // an open tab (its current, possibly unsaved, state)
    std::string password;  // filled in by the worker for tab sources
};

// One undoable change to a document. Edits are recorded in order; undo
// re-opens the last saved file and replays all but the last one.
struct EditOp {
    enum Kind {
        DeletePages, MovePages, RotatePages, InsertBlank, InsertFiles, Markup,
        EditText,     // replace the text of a TextRun: page, index = first, count, text
        FindReplace,  // every occurrence of `find` in the document by `text`
        AddNote,      // a sticky note at (x, y) on `page` with `text`
        EditComment,  // the comment of annotation `index` on `page` becomes `text`
        DeleteAnnot,  // annotation `index` on `page` (and its pop-up)
        SetField,     // form field (annotation `index` on `page`): text / checked / option
        AddShape,     // rectangle, ellipse, line, arrow or pen stroke: shape, points, color, width
        AddStamp,     // a stamp ("APPROVED") in `rect`: text, color
        AddImage,     // a picture in `rect` (signature: as a removable stamp annotation)
        AddText,      // new text at (x, y): text, fontSize, color, bold/italic/serif/mono
        StyleText,    // text run (page, index, count): fontSize (0: keep), color (CLR_INVALID: keep)
    } kind = DeletePages;
    std::vector<int> pages;  // Delete/Move/Rotate: ascending page indices
    int index = 0;           // Move: new index of the first moved page;
                             // InsertBlank/InsertFiles: insert before this page
    int turns = 0;           // Rotate: quarter turns clockwise
    SizeF size;              // InsertBlank: page size in points
    std::vector<ImportSource> sources;  // InsertFiles, in order
    int markup = kMarkupHighlight;      // Markup: MarkupType
    COLORREF color = 0;
    TextPos from, to;        // Markup: text range (may span pages)
    int page = 0;            // EditText / AddNote / EditComment / DeleteAnnot
    int count = 0;           // EditText: number of page objects replaced
    float x = 0, y = 0;      // AddNote: page points, top-left origin, unrotated
    std::wstring text;       // new text / comment (Markup: optional comment)
    std::wstring find;       // FindReplace: what to find; EditText: the old text
    bool matchCase = false;  // FindReplace
    std::wstring author;     // comments
    // forms
    bool checked = false;    // SetField: check boxes and radio buttons
    int option = -1;         // SetField: combo / list box choice
    // drawing, stamps, pictures and new text
    int shape = kShapeRect;
    std::vector<PointF> points;  // AddShape: page points, top-left origin, unrotated
    float width = 2;             // AddShape: line width in points
    RectF rect;                  // AddStamp / AddImage
    std::vector<uint8_t> pixels; // AddImage: BGRA, top-down
    int imageW = 0, imageH = 0;
    bool asAnnot = false;        // AddImage: a stamp annotation (signatures)
    float fontSize = 0;          // AddText / StyleText: font size in points
    bool bold = false, italic = false, serif = false, mono = false;  // AddText
};

enum class EditAction { Edit, Undo, Redo, Save };

// Posted after an edit, undo, redo or save. Edits give the document a new
// id (so results still queued for the old state are ignored); `docId` is
// that new id.
struct EditResult {
    uint32_t docId = 0;
    EditAction action = EditAction::Edit;
    bool ok = true;
    std::wstring error;       // shown to the user when !ok
    std::vector<SizeF> pageSizes;
    std::vector<OutlineItem> outline;
    DocInfo info;
    bool canUndo = false, canRedo = false, dirty = false;
    std::wstring path;        // file the document is saved in
    int focusPage = -1;       // page to show after the edit (-1: stay)
    std::vector<int> select;  // pages to select in the thumbnails
    uint32_t flags = 0;       // Save: echoed from the request (kAfterSave*)
    std::wstring note;        // shown after a successful edit (e.g. how many replaced)
    bool fontChanged = false; // edited text needed a font other than the original
};

enum : uint32_t { kAfterSaveCloseTab = 1, kAfterSaveQuit = 2 };

struct ExtractResult {
    bool ok = true;
    std::wstring error;
    std::vector<std::wstring> files;  // written files
};
