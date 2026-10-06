// PdfEngine.cpp - PDFium integration.
#include "PdfEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <map>
#include <set>
#include <type_traits>

#include "Util.h"
#include "fpdf_annot.h"
#include "fpdf_doc.h"
#include "fpdf_edit.h"
#include "fpdf_formfill.h"
#include "fpdf_ppo.h"
#include "fpdf_save.h"
#include "fpdf_text.h"
#include "fpdfview.h"

// ---------------------------------------------------------------------------
// Crash containment.
//
// PDFium is heavily fuzzed (it ships in Chrome) and is robust against
// malformed files, but as a last line of defence the MSVC build wraps each
// call into PDFium in a structured-exception guard. If PDFium faults on a
// hostile file the operation fails (the page shows as blank / the file is
// reported as damaged) instead of the whole application disappearing.
// MinGW has no __try/__except, so there the call is made directly.
// ---------------------------------------------------------------------------
namespace {

template <typename F>
void Trampoline(void* ctx) { (*static_cast<F*>(ctx))(); }

#if defined(_MSC_VER)
bool GuardedInvoke(void (*fn)(void*), void* ctx) {
    __try {
        fn(ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#else
bool GuardedInvoke(void (*fn)(void*), void* ctx) {
    fn(ctx);
    return true;
}
#endif

// Runs `f` inside the guard; returns false if it faulted.
template <typename F>
bool Guarded(F&& f) {
    using Fn = std::remove_reference_t<F>;
    return GuardedInvoke(&Trampoline<Fn>, (void*)&f);
}

}  // namespace

namespace {
// Converts a rectangle from PDF user space to top-left-origin page points in
// the displayed orientation (0.01 pt precision).
RectF ToDisplay(FPDF_PAGE page, int sx, int sy, double l, double t, double r, double b) {
    int x1, y1, x2, y2;
    FPDF_PageToDevice(page, 0, 0, sx, sy, 0, l, t, &x1, &y1);
    FPDF_PageToDevice(page, 0, 0, sx, sy, 0, r, b, &x2, &y2);
    RectF rc;
    rc.left = std::min(x1, x2) / 100.0f;
    rc.right = std::max(x1, x2) / 100.0f;
    rc.top = std::min(y1, y2) / 100.0f;
    rc.bottom = std::max(y1, y2) / 100.0f;
    return rc;
}

// A string entry of an annotation ("Contents", "T", "M").
std::wstring AnnotString(FPDF_ANNOTATION annot, const char* key) {
    const unsigned long bytes = FPDFAnnot_GetStringValue(annot, key, nullptr, 0);
    if (bytes <= 2 || bytes > (1u << 20)) return {};
    std::vector<FPDF_WCHAR> buf(bytes / 2);
    FPDFAnnot_GetStringValue(annot, key, buf.data(), bytes);
    return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()), bytes / 2 - 1);
}

// Comments, markup, and the drawings, stamps and signatures added by the
// annotation tools (they can be commented on and deleted the same way).
bool IsCommentType(int subtype) {
    switch (subtype) {
        case FPDF_ANNOT_TEXT:
        case FPDF_ANNOT_HIGHLIGHT:
        case FPDF_ANNOT_UNDERLINE:
        case FPDF_ANNOT_SQUIGGLY:
        case FPDF_ANNOT_STRIKEOUT:
        case FPDF_ANNOT_FREETEXT:
        case FPDF_ANNOT_LINE:
        case FPDF_ANNOT_SQUARE:
        case FPDF_ANNOT_CIRCLE:
        case FPDF_ANNOT_POLYGON:
        case FPDF_ANNOT_POLYLINE:
        case FPDF_ANNOT_STAMP:
        case FPDF_ANNOT_INK: return true;
        default: return false;
    }
}

template <typename Get>
std::wstring FieldString(Get get, FPDF_FORMHANDLE form, FPDF_ANNOTATION annot) {
    const unsigned long bytes = get(form, annot, nullptr, 0);
    if (bytes <= 2 || bytes > (1u << 20)) return {};
    std::vector<FPDF_WCHAR> buf(bytes / 2);
    get(form, annot, buf.data(), bytes);
    return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()), bytes / 2 - 1);
}

// The form fields (widget annotations) of a page.
void ReadFields(FPDF_FORMHANDLE form, FPDF_PAGE page, int sx, int sy, std::vector<FormField>& out) {
    constexpr int kMaxFields = 4000;
    const int n = std::min(FPDFPage_GetAnnotCount(page), kMaxFields);
    for (int i = 0; i < n; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;
        FS_RECTF r;
        if (FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_WIDGET && FPDFAnnot_GetRect(annot, &r)) {
            FormField f;
            f.rect = ToDisplay(page, sx, sy, r.left, r.top, r.right, r.bottom);
            f.annot = i;
            f.type = FPDFAnnot_GetFormFieldType(form, annot);
            f.name = FieldString([](auto... a) { return FPDFAnnot_GetFormFieldName(a...); }, form, annot);
            f.value = FieldString([](auto... a) { return FPDFAnnot_GetFormFieldValue(a...); }, form, annot);
            const int flags = FPDFAnnot_GetFormFieldFlags(form, annot);
            f.readOnly = (flags & FPDF_FORMFLAG_READONLY) != 0;
            f.multiline = (flags & FPDF_FORMFLAG_TEXT_MULTILINE) != 0;
            f.password = (flags & FPDF_FORMFLAG_TEXT_PASSWORD) != 0;
            if (f.type == kFieldCheckBox || f.type == kFieldRadio) f.checked = FPDFAnnot_IsChecked(form, annot) != 0;
            if (f.type == kFieldComboBox || f.type == kFieldListBox) {
                const int count = std::min(FPDFAnnot_GetOptionCount(form, annot), 500);
                for (int k = 0; k < count; ++k) {
                    const unsigned long bytes = FPDFAnnot_GetOptionLabel(form, annot, k, nullptr, 0);
                    std::wstring label;
                    if (bytes > 2 && bytes < 8192) {
                        std::vector<FPDF_WCHAR> buf(bytes / 2);
                        FPDFAnnot_GetOptionLabel(form, annot, k, buf.data(), bytes);
                        label.assign(reinterpret_cast<const wchar_t*>(buf.data()), bytes / 2 - 1);
                    }
                    f.options.push_back(label);
                    if (f.selected < 0 && FPDFAnnot_IsOptionSelected(form, annot, k)) f.selected = k;
                }
            }
            if (f.type != kFieldUnknown) out.push_back(std::move(f));
        }
        FPDFPage_CloseAnnot(annot);
    }
}

// Sticky notes and text markup of a page (markup may have no comment).
void ReadComments(FPDF_PAGE page, int sx, int sy, std::vector<CommentInfo>& out) {
    constexpr int kMaxComments = 4000;
    const int n = std::min(FPDFPage_GetAnnotCount(page), kMaxComments);
    for (int i = 0; i < n; ++i) {
        FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
        if (!annot) continue;
        const int subtype = FPDFAnnot_GetSubtype(annot);
        FS_RECTF r;
        if (IsCommentType(subtype) && FPDFAnnot_GetRect(annot, &r)) {
            CommentInfo c;
            c.rect = ToDisplay(page, sx, sy, r.left, r.top, r.right, r.bottom);
            c.annot = i;
            c.subtype = subtype;
            c.text = AnnotString(annot, "Contents");
            c.author = AnnotString(annot, "T");
            c.date = AnnotString(annot, "M");
            if (c.date.empty()) c.date = AnnotString(annot, "CreationDate");
            out.push_back(std::move(c));
        }
        FPDFPage_CloseAnnot(annot);
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// File access.
//
// PDFium reads the file lazily through this callback instead of us loading
// the whole file into memory. Only the parts PDFium actually needs (xref,
// the objects of pages being displayed) are read; the Windows file cache
// does the rest. The file is opened with full sharing so other programs can
// still edit, rename or delete it while it is open here.
// ---------------------------------------------------------------------------
struct PdfEngine::FileSource {
    HANDLE handle = INVALID_HANDLE_VALUE;
    FPDF_FILEACCESS access{};

    ~FileSource() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }

    static int GetBlock(void* param, unsigned long position, unsigned char* buf,
                        unsigned long size) {
        auto* self = static_cast<FileSource*>(param);
        OVERLAPPED ov{};  // synchronous read at an explicit offset
        ov.Offset = position;
        DWORD read = 0;
        if (!ReadFile(self->handle, buf, size, &read, &ov)) return 0;
        return read == size ? 1 : 0;
    }
};

struct PdfEngine::FormEnv {
    FPDF_FORMFILLINFO info{};
    FPDF_FORMHANDLE handle = nullptr;
};

void PdfEngine::InitLibrary() {
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
}

void PdfEngine::DestroyLibrary() { FPDF_DestroyLibrary(); }

PdfEngine::PdfEngine() = default;
PdfEngine::~PdfEngine() { Close(); }

void PdfEngine::ReleasePages() {
    for (auto& p : m_pages) {
        if (m_form) FORM_OnBeforeClosePage(p.second, m_form->handle);
        FPDF_ClosePage(p.second);
    }
    m_pages.clear();
}

void PdfEngine::Close() {
    ReleasePages();
    if (m_form) {
        Guarded([&] { FPDFDOC_ExitFormFillEnvironment(m_form->handle); });
        m_form.reset();
    }
    if (m_doc) {
        FPDF_CloseDocument(m_doc);
        m_doc = nullptr;
    }
    m_file.reset();
    m_pageCount = 0;
}

OpenError PdfEngine::LoadDocument(const std::wstring& path, const std::string& password,
                                  std::unique_ptr<FileSource>& file, FPDF_DOCUMENT& doc) {
    doc = nullptr;
    file = std::make_unique<FileSource>();
    file->handle = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                               nullptr);
    if (file->handle == INVALID_HANDLE_VALUE) {
        switch (GetLastError()) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_NAME:
            case ERROR_BAD_NETPATH:
                return OpenError::NotFound;
            case ERROR_ACCESS_DENIED:
            case ERROR_SHARING_VIOLATION:
            case ERROR_LOCK_VIOLATION:
                return OpenError::AccessDenied;
            default:
                return OpenError::ReadError;
        }
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file->handle, &size)) return OpenError::ReadError;
    if (size.QuadPart == 0) return OpenError::NotPdf;
    // FPDF_FILEACCESS uses a 32-bit length on Windows.
    if (size.QuadPart > 0xFFFFFFFFLL) return OpenError::TooLarge;

    file->access.m_FileLen = (unsigned long)size.QuadPart;
    file->access.m_GetBlock = &FileSource::GetBlock;
    file->access.m_Param = file.get();

    unsigned long err = FPDF_ERR_SUCCESS;
    FPDF_FILEACCESS* access = &file->access;
    bool ok = Guarded([&] {
        doc = FPDF_LoadCustomDocument(access, password.empty() ? nullptr : password.c_str());
        if (!doc) err = FPDF_GetLastError();
    });
    if (!ok) return OpenError::NotPdf;
    if (!doc) {
        switch (err) {
            case FPDF_ERR_FILE: return OpenError::ReadError;
            case FPDF_ERR_FORMAT: return OpenError::NotPdf;
            case FPDF_ERR_PASSWORD: return OpenError::Password;
            case FPDF_ERR_SECURITY: return OpenError::Security;
            default: return OpenError::Unknown;
        }
    }
    return OpenError::None;
}

bool PdfEngine::ReadPageSizes(FPDF_DOCUMENT doc, std::vector<SizeF>& sizes) {
    return Guarded([&] {
        const int count = FPDF_GetPageCount(doc);
        sizes.assign(count > 0 ? (size_t)count : 0, SizeF{});
        // Page sizes come from the page dictionaries only; page content is
        // NOT parsed here, so this is fast even for thousands of pages.
        for (int i = 0; i < count; ++i) {
            FS_SIZEF s{};
            if (!FPDF_GetPageSizeByIndexF(doc, i, &s)) s = {612, 792};
            // Guard against absurd or broken MediaBoxes.
            if (!(s.width >= 1 && s.width <= 200000)) s.width = 612;
            if (!(s.height >= 1 && s.height <= 200000)) s.height = 792;
            sizes[(size_t)i] = {s.width, s.height};
        }
    });
}

OpenError PdfEngine::Open(const std::wstring& path, const std::string& password,
                          std::vector<SizeF>& pageSizes) {
    std::unique_ptr<FileSource> file;
    FPDF_DOCUMENT doc = nullptr;
    const OpenError err = LoadDocument(path, password, file, doc);
    if (err != OpenError::None) return err;

    std::vector<SizeF> sizes;
    const bool ok = ReadPageSizes(doc, sizes);
    if (!ok || sizes.empty()) {
        Guarded([&] { FPDF_CloseDocument(doc); });
        return ok ? OpenError::NoPages : OpenError::NotPdf;
    }

    // Success: replace the previous document.
    Close();
    m_doc = doc;
    m_file = std::move(file);
    m_pageCount = (int)sizes.size();
    pageSizes = std::move(sizes);

    // Documents with form fields get PDFium's form layer (no JavaScript:
    // this PDFium build has none, and no callbacks are needed to fill fields).
    bool hasForm = false;
    Guarded([&] { hasForm = FPDF_GetFormType(m_doc) == FORMTYPE_ACRO_FORM; });
    if (hasForm) {
        auto form = std::make_unique<FormEnv>();
        form->info.version = 1;
        Guarded([&] {
            form->handle = FPDFDOC_InitFormFillEnvironment(m_doc, &form->info);
            if (form->handle) {
                FPDF_SetFormFieldHighlightColor(form->handle, FPDF_FORMFIELD_UNKNOWN, 0xDDE8FF);
                FPDF_SetFormFieldHighlightAlpha(form->handle, 110);
            }
        });
        if (form->handle) m_form = std::move(form);
    }
    return OpenError::None;
}

FPDF_PAGE PdfEngine::LoadEditPage(int index) {
    FPDF_PAGE page = FPDF_LoadPage(m_doc, index);
    if (page && m_form) FORM_OnAfterLoadPage(page, m_form->handle);
    return page;
}

void PdfEngine::CloseEditPage(FPDF_PAGE page) {
    if (!page) return;
    if (m_form) FORM_OnBeforeClosePage(page, m_form->handle);
    FPDF_ClosePage(page);
}

FPDF_PAGE PdfEngine::GetPage(int index) {
    if (!m_doc || index < 0 || index >= m_pageCount) return nullptr;
    for (size_t i = 0; i < m_pages.size(); ++i) {
        if (m_pages[i].first == index) {
            auto entry = m_pages[i];
            m_pages.erase(m_pages.begin() + (ptrdiff_t)i);
            m_pages.insert(m_pages.begin(), entry);
            return entry.second;
        }
    }
    FPDF_PAGE page = nullptr;
    if (!Guarded([&] { page = LoadEditPage(index); }) || !page) return nullptr;
    m_pages.insert(m_pages.begin(), {index, page});
    if (m_pages.size() > kMaxParsedPages) {
        CloseEditPage(m_pages.back().second);
        m_pages.pop_back();
    }
    return page;
}

// Dark mode maps white paper to near-black and black text to light grey
// (an inversion with softened contrast); dim mode lowers brightness.
void PdfEngine::ApplyPageColors(PixelBuffer& px, int mode) {
    if (mode == kColorsNormal || !px.bits) return;
    uint8_t lut[256];
    for (int c = 0; c < 256; ++c) {
        lut[c] = mode == kColorsDark ? (uint8_t)(30 + (255 - c) * 195 / 255)
                                     : (uint8_t)(c * 200 / 255);
    }
    uint8_t* p = px.bits;
    const size_t n = (size_t)px.width * px.height;
    for (size_t i = 0; i < n; ++i, p += 4) {
        p[0] = lut[p[0]];
        p[1] = lut[p[1]];
        p[2] = lut[p[2]];
    }
}

bool PdfEngine::RenderTile(const TileRequest& req, PixelBuffer& out) {
    if (req.w <= 0 || req.h <= 0) return false;
    if (!out.Allocate(req.w, req.h)) return false;

    FPDF_PAGE page = GetPage(req.page);
    // Render directly into our buffer: no intermediate copy.
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(req.w, req.h, FPDFBitmap_BGRx, out.bits, req.w * 4);
    if (!bmp) {
        out.Free();
        return false;
    }
    bool ok = page != nullptr;
    FPDFBitmap_FillRect(bmp, 0, 0, req.w, req.h, ok ? 0xFFFFFFFF : 0xFFE0E0E0);
    if (ok) {
        // The page is positioned at (-x, -y) so that only this tile's part
        // lands inside the bitmap. PDFium culls objects outside the clip,
        // so rendering a tile costs roughly in proportion to its content.
        // FPDF_RENDER_LIMITEDIMAGECACHE keeps PDFium's decoded-image cache
        // small, trading a little speed for much lower memory on
        // image-heavy documents.
        ok = Guarded([&] {
            FPDF_RenderPageBitmap(bmp, page, -req.x, -req.y, req.pageW, req.pageH, req.rotate & 3,
                                  FPDF_ANNOT | FPDF_RENDER_LIMITEDIMAGECACHE);
            // Form fields: their current values and a light highlight.
            if (m_form)
                FPDF_FFLDraw(m_form->handle, bmp, page, -req.x, -req.y, req.pageW, req.pageH,
                             req.rotate & 3, FPDF_ANNOT | FPDF_RENDER_LIMITEDIMAGECACHE);
        });
        if (!ok) {
            // PDFium faulted: drop the parsed page and show a grey tile.
            FPDFBitmap_FillRect(bmp, 0, 0, req.w, req.h, 0xFFE0E0E0);
            ReleasePages();
        }
    }
    FPDFBitmap_Destroy(bmp);  // does not free our external buffer
    ApplyPageColors(out, req.colorMode);
    return true;
}

void PdfEngine::SearchPage(int pageIndex, const std::wstring& query, bool matchCase,
                           std::vector<SearchHit>& hits) {
    if (query.empty()) return;
    // Use the parsed-page cache only if the page is already there, so that
    // a search sweeping the whole document does not evict the pages being
    // displayed.
    FPDF_PAGE page = nullptr;
    bool owned = false;
    for (auto& p : m_pages) {
        if (p.first == pageIndex) page = p.second;
    }
    if (!page) {
        if (!m_doc) return;
        Guarded([&] { page = FPDF_LoadPage(m_doc, pageIndex); });
        if (!page) return;
        owned = true;
    }

    Guarded([&] {
        FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
        if (!text) return;
        // Rects from PDFium are in PDF user space (origin bottom-left,
        // unrotated). FPDF_PageToDevice maps them into the displayed page
        // orientation; using a 100x "device" keeps 0.01 pt precision.
        const float pw = FPDF_GetPageWidthF(page), ph = FPDF_GetPageHeightF(page);
        const int sx = (int)std::lround(pw * 100), sy = (int)std::lround(ph * 100);
        unsigned long flags = matchCase ? FPDF_MATCHCASE : 0;
        FPDF_SCHHANDLE sch = FPDFText_FindStart(
            text, reinterpret_cast<FPDF_WIDESTRING>(query.c_str()), flags, 0);
        if (sch) {
            constexpr size_t kMaxHitsPerPage = 5000;
            while (hits.size() < kMaxHitsPerPage && FPDFText_FindNext(sch)) {
                int start = FPDFText_GetSchResultIndex(sch);
                int count = FPDFText_GetSchCount(sch);
                int nrects = FPDFText_CountRects(text, start, count);
                SearchHit hit;
                hit.page = pageIndex;
                for (int r = 0; r < nrects; ++r) {
                    double l, t, rr, b;
                    if (!FPDFText_GetRect(text, r, &l, &t, &rr, &b)) continue;
                    hit.rects.push_back(ToDisplay(page, sx, sy, l, t, rr, b));
                }
                if (!hit.rects.empty()) hits.push_back(std::move(hit));
            }
            FPDFText_FindClose(sch);
        }
        FPDFText_ClosePage(text);
    });

    if (owned) Guarded([&] { FPDF_ClosePage(page); });
}


void PdfEngine::ExtractPageInfo(int pageIndex, std::vector<TextChar>& chars,
                                std::vector<LinkInfo>& links, std::vector<CommentInfo>& comments,
                                std::vector<FormField>& fields) {
    FPDF_PAGE page = GetPage(pageIndex);  // the page is on screen: cache it
    if (!page) return;
    Guarded([&] {
        const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
        const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);

        // Link annotations (internal jumps and URIs).
        constexpr size_t kMaxLinks = 4000;
        int pos = 0;
        FPDF_LINK link = nullptr;
        while (links.size() < kMaxLinks && FPDFLink_Enumerate(page, &pos, &link)) {
            FS_RECTF r;
            if (!FPDFLink_GetAnnotRect(link, &r)) continue;
            LinkInfo li;
            li.rect = ToDisplay(page, sx, sy, r.left, r.top, r.right, r.bottom);
            if (FPDF_DEST dest = FPDFLink_GetDest(m_doc, link))
                ReadDest(dest, li.target);
            else if (FPDF_ACTION action = FPDFLink_GetAction(link))
                ReadAction(action, li.target);
            if (li.target.page >= 0 || !li.target.uri.empty()) links.push_back(std::move(li));
        }

        ReadComments(page, sx, sy, comments);
        if (m_form) ReadFields(m_form->handle, page, sx, sy, fields);

        FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
        if (!text) return;
        const int n = FPDFText_CountChars(text);
        chars.resize(n > 0 ? (size_t)n : 0);
        for (int i = 0; i < n; ++i) {
            TextChar& c = chars[(size_t)i];
            c.cp = FPDFText_GetUnicode(text, i);
            FS_RECTF r;
            if (FPDFText_IsGenerated(text, i) != 1 && FPDFText_GetLooseCharBox(text, i, &r)) {
                c.box = ToDisplay(page, sx, sy, r.left, r.top, r.right, r.bottom);
                c.hasBox = c.box.right > c.box.left && c.box.bottom > c.box.top;
            }
        }

        // Plain-text URLs ("www.example.com") that are not link annotations.
        if (FPDF_PAGELINK web = FPDFLink_LoadWebLinks(text)) {
            const int count = FPDFLink_CountWebLinks(web);
            for (int i = 0; i < count && links.size() < kMaxLinks; ++i) {
                const int len = FPDFLink_GetURL(web, i, nullptr, 0);
                if (len <= 1 || len > 8192) continue;
                std::vector<unsigned short> buf((size_t)len);
                FPDFLink_GetURL(web, i, buf.data(), len);
                LinkInfo li;
                li.target.uri.assign(reinterpret_cast<const wchar_t*>(buf.data()), (size_t)len - 1);
                const int rects = FPDFLink_CountRects(web, i);
                for (int k = 0; k < rects; ++k) {
                    double l, t, rr, b;
                    if (!FPDFLink_GetRect(web, i, k, &l, &t, &rr, &b)) continue;
                    li.rect = ToDisplay(page, sx, sy, l, t, rr, b);
                    links.push_back(li);
                }
            }
            FPDFLink_CloseWebLinks(web);
        }
        FPDFText_ClosePage(text);
    });
}

void PdfEngine::ReadDest(FPDF_DEST dest, LinkTarget& target) {
    target.page = FPDFDest_GetDestPageIndex(m_doc, dest);
    if (target.page >= m_pageCount) target.page = -1;
    FPDF_BOOL hasX = 0, hasY = 0, hasZoom = 0;
    FS_FLOAT x = 0, y = 0, zoom = 0;
    if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y, &zoom) && hasY)
        target.destY = y;
}

void PdfEngine::ReadAction(FPDF_ACTION action, LinkTarget& target) {
    switch (FPDFAction_GetType(action)) {
        case PDFACTION_GOTO:
            if (FPDF_DEST dest = FPDFAction_GetDest(m_doc, action)) ReadDest(dest, target);
            break;
        case PDFACTION_URI: {
            const unsigned long len = FPDFAction_GetURIPath(m_doc, action, nullptr, 0);
            if (len <= 1 || len > 8192) break;
            std::string uri(len, '\0');
            FPDFAction_GetURIPath(m_doc, action, uri.data(), len);
            uri.resize(len - 1);
            target.uri = Utf8ToWide(uri);
            break;
        }
        default:  // launch / remote files are never followed (security)
            break;
    }
}

void PdfEngine::LoadOutline(std::vector<OutlineItem>& out) {
    if (!m_doc) return;
    Guarded([&] {
        constexpr size_t kMaxItems = 20000;
        constexpr int kMaxDepth = 32;
        std::vector<FPDF_BOOKMARK> seen;  // malformed outlines can contain cycles
        // Iterative depth-first walk: (bookmark, level).
        std::vector<std::pair<FPDF_BOOKMARK, int>> stack;
        if (FPDF_BOOKMARK first = FPDFBookmark_GetFirstChild(m_doc, nullptr))
            stack.push_back({first, 0});
        while (!stack.empty() && out.size() < kMaxItems) {
            auto [bm, level] = stack.back();
            stack.pop_back();
            if (std::find(seen.begin(), seen.end(), bm) != seen.end()) continue;
            seen.push_back(bm);

            OutlineItem item;
            item.level = level;
            const unsigned long bytes = FPDFBookmark_GetTitle(bm, nullptr, 0);
            if (bytes > 2 && bytes < 4096) {
                std::vector<unsigned short> buf(bytes / 2);
                FPDFBookmark_GetTitle(bm, buf.data(), bytes);
                item.title.assign(reinterpret_cast<const wchar_t*>(buf.data()), bytes / 2 - 1);
            }
            if (FPDF_DEST dest = FPDFBookmark_GetDest(m_doc, bm))
                ReadDest(dest, item.target);
            else if (FPDF_ACTION action = FPDFBookmark_GetAction(bm))
                ReadAction(action, item.target);
            out.push_back(std::move(item));

            // Push the next sibling first so the child is visited next.
            if (FPDF_BOOKMARK next = FPDFBookmark_GetNextSibling(m_doc, bm))
                stack.push_back({next, level});
            if (level + 1 < kMaxDepth)
                if (FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(m_doc, bm))
                    stack.push_back({child, level + 1});
        }
    });
}

void PdfEngine::GetInfo(DocInfo& info) {
    if (!m_doc) return;
    Guarded([&] {
        auto meta = [&](const char* tag) {
            const unsigned long bytes = FPDF_GetMetaText(m_doc, tag, nullptr, 0);
            if (bytes <= 2 || bytes > 65536) return std::wstring();
            std::vector<unsigned short> buf(bytes / 2);
            FPDF_GetMetaText(m_doc, tag, buf.data(), bytes);
            return std::wstring(reinterpret_cast<const wchar_t*>(buf.data()), bytes / 2 - 1);
        };
        info.title = meta("Title");
        info.author = meta("Author");
        info.subject = meta("Subject");
        info.keywords = meta("Keywords");
        info.creator = meta("Creator");
        info.producer = meta("Producer");
        info.created = meta("CreationDate");
        info.modified = meta("ModDate");
        int version = 0;
        if (FPDF_GetFileVersion(m_doc, &version)) info.version = version;
        info.encrypted = FPDF_GetSecurityHandlerRevision(m_doc) != -1;
    });
}

bool PdfEngine::PrintPage(HDC dc, int index) {
    if (!m_doc || index < 0 || index >= m_pageCount) return false;
    FPDF_PAGE page = nullptr;
    if (!Guarded([&] { page = FPDF_LoadPage(m_doc, index); }) || !page) return false;

    // Fit to the printable area, keeping the aspect ratio, centred, and
    // turned 90 degrees when page and paper orientation differ.
    const float w = FPDF_GetPageWidthF(page), h = FPDF_GetPageHeightF(page);
    const int resX = GetDeviceCaps(dc, HORZRES), resY = GetDeviceCaps(dc, VERTRES);
    const int dpiX = GetDeviceCaps(dc, LOGPIXELSX), dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    const bool rotate = (w > h) != (resX > resY);
    const double pw = (rotate ? h : w) * dpiX / 72.0, ph = (rotate ? w : h) * dpiY / 72.0;
    const double k = std::min(resX / pw, resY / ph);
    const int outW = (int)(pw * k), outH = (int)(ph * k);

    bool ok = StartPage(dc) > 0;
    if (ok) {
        Guarded([&] {
            FPDF_RenderPage(dc, page, (resX - outW) / 2, (resY - outH) / 2, outW, outH,
                            rotate ? 1 : 0, FPDF_ANNOT | FPDF_PRINTING);
        });
        ok = EndPage(dc) > 0;
    }
    Guarded([&] { FPDF_ClosePage(page); });
    return ok;
}

std::wstring PdfEngine::ExtractText(TextPos from, TextPos to) {
    std::wstring out;
    if (!m_doc) return out;
    constexpr size_t kMaxChars = 32u << 20;  // 64 MB of UTF-16 is plenty
    for (int p = std::max(0, from.page); p <= to.page && p < m_pageCount; ++p) {
        FPDF_PAGE page = nullptr;
        Guarded([&] { page = FPDF_LoadPage(m_doc, p); });
        if (!page) continue;
        Guarded([&] {
            FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
            if (!text) return;
            const int n = FPDFText_CountChars(text);
            const int s = p == from.page ? std::min(from.index, n) : 0;
            const int e = p == to.page ? std::min(to.index, n) : n;
            if (e > s) {
                std::vector<unsigned short> buf((size_t)(e - s) + 1);
                int written = FPDFText_GetText(text, s, e - s, buf.data());
                if (written > 1) {
                    if (!out.empty() && out.back() != L'\n') out += L"\r\n";
                    out.append(reinterpret_cast<const wchar_t*>(buf.data()), (size_t)written - 1);
                }
            }
            FPDFText_ClosePage(text);
        });
        Guarded([&] { FPDF_ClosePage(page); });
        if (out.size() > kMaxChars) break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
namespace {
// Streams FPDF_SaveAsCopy output into a file.
struct FileWriter : FPDF_FILEWRITE {
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool failed = false;

    static int Write(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
        auto* w = static_cast<FileWriter*>(self);
        const auto* p = static_cast<const uint8_t*>(data);
        while (size > 0 && !w->failed) {
            DWORD written = 0;
            if (!WriteFile(w->handle, p, size, &written, nullptr) || written == 0) {
                w->failed = true;
                break;
            }
            p += written;
            size -= written;
        }
        return w->failed ? 0 : 1;
    }
};

// Saves `doc` to `path` (overwriting it) and flushes it to disk.
bool SaveDocument(FPDF_DOCUMENT doc, const std::wstring& path) {
    FileWriter w;
    w.version = 1;
    w.WriteBlock = &FileWriter::Write;
    w.handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (w.handle == INVALID_HANDLE_VALUE) return false;
    FPDF_BOOL saved = 0;
    const bool ok = Guarded([&] { saved = FPDF_SaveAsCopy(doc, &w, FPDF_NO_INCREMENTAL); });
    // The data must be on disk before the file replaces the original.
    const bool flushed = FlushFileBuffers(w.handle) != 0;
    CloseHandle(w.handle);
    return ok && saved && !w.failed && flushed;
}

// The current time as a PDF date string (UTC), e.g. D:20260301140500Z.
std::wstring PdfDateNow() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"D:%04u%02u%02u%02u%02u%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond);
    return buf;
}
}  // namespace

void PdfEngine::GetPageSizes(std::vector<SizeF>& out) {
    if (m_doc) ReadPageSizes(m_doc, out);
}

bool PdfEngine::WriteTo(const std::wstring& path) { return m_doc && SaveDocument(m_doc, path); }

bool PdfEngine::WritePagesTo(const std::vector<int>& pages, const std::wstring& path) {
    if (!m_doc || pages.empty()) return false;
    for (int p : pages)
        if (p < 0 || p >= m_pageCount) return false;
    FPDF_DOCUMENT out = nullptr;
    bool ok = Guarded([&] {
        out = FPDF_CreateNewDocument();
        if (!out) return;
        if (!FPDF_ImportPagesByIndex(out, m_doc, pages.data(), (unsigned long)pages.size(), 0)) {
            FPDF_CloseDocument(out);
            out = nullptr;
            return;
        }
        FPDF_CopyViewerPreferences(out, m_doc);
    });
    if (!ok || !out) return false;
    ok = SaveDocument(out, path);
    Guarded([&] { FPDF_CloseDocument(out); });
    return ok;
}

bool PdfEngine::ApplyEdit(const EditOp& op, std::wstring& error) {
    if (!m_doc) return false;
    ReleasePages();  // parsed pages may be deleted or change below
    m_editCount = 0;
    m_editFontChanged = false;
    bool ok = false;
    switch (op.kind) {
        case EditOp::DeletePages: {
            if (op.pages.empty() || (int)op.pages.size() >= m_pageCount) {
                error = L"A document must keep at least one page.";
                return false;
            }
            for (int p : op.pages)
                if (p < 0 || p >= m_pageCount) return false;
            ok = Guarded([&] {
                // Highest first, so the remaining indices stay valid.
                for (auto it = op.pages.rbegin(); it != op.pages.rend(); ++it)
                    FPDFPage_Delete(m_doc, *it);
            });
            break;
        }
        case EditOp::MovePages: {
            // FPDF_MovePages may leave the document half-changed on bad
            // input, so everything is validated first.
            const int n = (int)op.pages.size();
            if (n == 0 || op.index < 0 || op.index > m_pageCount - n) return false;
            for (int i = 0; i < n; ++i) {
                if (op.pages[(size_t)i] < 0 || op.pages[(size_t)i] >= m_pageCount) return false;
                if (i > 0 && op.pages[(size_t)i] <= op.pages[(size_t)i - 1]) return false;
            }
            FPDF_BOOL moved = 0;
            ok = Guarded([&] {
                moved = FPDF_MovePages(m_doc, op.pages.data(), (unsigned long)n, op.index);
            }) && moved;
            break;
        }
        case EditOp::RotatePages: {
            ok = Guarded([&] {
                for (int p : op.pages) {
                    FPDF_PAGE page = FPDF_LoadPage(m_doc, p);
                    if (!page) continue;
                    const int current = std::max(0, FPDFPage_GetRotation(page));
                    FPDFPage_SetRotation(page, ((current + op.turns) % 4 + 4) % 4);
                    FPDF_ClosePage(page);
                }
            });
            break;
        }
        case EditOp::InsertBlank: {
            if (op.index < 0 || op.index > m_pageCount) return false;
            FPDF_PAGE page = nullptr;
            ok = Guarded([&] {
                page = FPDFPage_New(m_doc, op.index, op.size.w, op.size.h);
                if (page) FPDF_ClosePage(page);
            }) && page;
            break;
        }
        case EditOp::InsertFiles: {
            if (op.index < 0 || op.index > m_pageCount) return false;
            // Open every source first, so a bad file changes nothing.
            struct Source {
                std::unique_ptr<FileSource> file;
                FPDF_DOCUMENT doc = nullptr;
            };
            std::vector<Source> sources;
            for (const ImportSource& src : op.sources) {
                Source s;
                const OpenError e = LoadDocument(src.path, src.password, s.file, s.doc);
                if (e != OpenError::None) {
                    for (auto& o : sources) Guarded([&] { FPDF_CloseDocument(o.doc); });
                    error = e == OpenError::Password
                                ? L"A file is password-protected. Open it in a tab first, "
                                  L"then use \x201CMerge open tabs\x201D."
                                : L"A file could not be read, or is not a PDF document.";
                    return false;
                }
                sources.push_back(std::move(s));
            }
            ok = true;
            int at = op.index;
            for (auto& s : sources) {
                int count = 0;
                FPDF_BOOL done = 0;
                ok = ok && Guarded([&] {
                    count = FPDF_GetPageCount(s.doc);
                    done = FPDF_ImportPagesByIndex(m_doc, s.doc, nullptr, 0, at);
                }) && done;
                at += count;
                Guarded([&] { FPDF_CloseDocument(s.doc); });
            }
            break;
        }
        case EditOp::Markup:
            ok = AddMarkup(op);
            break;
        case EditOp::EditText:
            ok = EditText(op, error);
            break;
        case EditOp::FindReplace:
            ok = ReplaceEverywhere(op, error);
            break;
        case EditOp::AddNote:
            ok = AddNote(op);
            break;
        case EditOp::EditComment:
        case EditOp::DeleteAnnot:
            ok = ChangeAnnot(op, error);
            break;
        case EditOp::SetField: ok = SetField(op, error); break;
        case EditOp::AddShape: ok = AddShape(op); break;
        case EditOp::AddStamp: ok = AddStamp(op); break;
        case EditOp::AddImage: ok = AddImage(op); break;
        case EditOp::AddText: ok = AddText(op, error); break;
        case EditOp::StyleText: ok = StyleText(op, error); break;
    }
    int count = m_pageCount;
    Guarded([&] { count = FPDF_GetPageCount(m_doc); });
    m_pageCount = std::max(0, count);
    if (!ok && error.empty()) error = L"The change could not be made to this document.";
    return ok;
}

// Adds a highlight / underline / strike-out annotation over a text range,
// one annotation per page. Quad points come straight from PDFium's text
// rectangles (PDF user space), so they are right for rotated pages too.
bool PdfEngine::AddMarkup(const EditOp& op) {
    bool any = false;
    const std::wstring now = PdfDateNow();
    for (int p = std::max(0, op.from.page); p <= op.to.page && p < m_pageCount; ++p) {
        Guarded([&] {
            FPDF_PAGE page = FPDF_LoadPage(m_doc, p);
            if (!page) return;
            FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
            if (text) {
                const int n = FPDFText_CountChars(text);
                const int s = p == op.from.page ? std::min(op.from.index, n) : 0;
                const int e = p == op.to.page ? std::min(op.to.index, n) : n;
                const int rects = e > s ? FPDFText_CountRects(text, s, e - s) : 0;
                FPDF_ANNOTATION annot =
                    rects > 0 ? FPDFPage_CreateAnnot(page, (FPDF_ANNOTATION_SUBTYPE)op.markup) : nullptr;
                if (annot) {
                    FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, GetRValue(op.color),
                                       GetGValue(op.color), GetBValue(op.color), 255);
                    FS_RECTF bounds{1e9f, -1e9f, -1e9f, 1e9f};  // left, top, right, bottom
                    for (int r = 0; r < rects; ++r) {
                        double l, t, rr, b;
                        if (!FPDFText_GetRect(text, r, &l, &t, &rr, &b)) continue;
                        FS_QUADPOINTSF q;
                        q.x1 = (float)l;  q.y1 = (float)t;   // upper left
                        q.x2 = (float)rr; q.y2 = (float)t;   // upper right
                        q.x3 = (float)l;  q.y3 = (float)b;   // lower left
                        q.x4 = (float)rr; q.y4 = (float)b;   // lower right
                        FPDFAnnot_AppendAttachmentPoints(annot, &q);
                        bounds.left = std::min(bounds.left, (float)l);
                        bounds.right = std::max(bounds.right, (float)rr);
                        bounds.top = std::max(bounds.top, (float)t);
                        bounds.bottom = std::min(bounds.bottom, (float)b);
                    }
                    FPDFAnnot_SetRect(annot, &bounds);
                    FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
                    FPDFAnnot_SetStringValue(annot, "M",
                                             reinterpret_cast<FPDF_WIDESTRING>(now.c_str()));
                    if (!op.text.empty() && !any) {  // the comment goes on the first part
                        FPDFAnnot_SetStringValue(annot, "Contents",
                                                 reinterpret_cast<FPDF_WIDESTRING>(op.text.c_str()));
                        FPDFAnnot_SetStringValue(annot, "T",
                                                 reinterpret_cast<FPDF_WIDESTRING>(op.author.c_str()));
                    }
                    FPDFPage_CloseAnnot(annot);
                    any = true;
                }
                FPDFText_ClosePage(text);
            }
            FPDF_ClosePage(page);
        });
    }
    return any;
}


// ===========================================================================
// Editing text
//
// A PDF page has no paragraphs, only text objects: pieces of text drawn at
// a position in a font. Producers split lines differently (a whole line, a
// word, sometimes a single letter per object), so consecutive objects on
// one baseline in the same font are joined into a "run" that the reader
// edits as one line.
//
// The new text keeps the original font when that font is known to contain
// every letter needed (embedded fonts are often subsets holding only the
// letters the document uses). Otherwise a similar font is used: one of the
// standard PDF fonts (Helvetica, Times, Courier) for Western text, or an
// installed Windows font that has the letters, embedded in the file.
// ===========================================================================
namespace {

std::wstring ObjectText(FPDF_PAGEOBJECT obj, FPDF_TEXTPAGE tp) {
    const unsigned long bytes = FPDFTextObj_GetText(obj, tp, nullptr, 0);
    if (bytes <= 2 || bytes > (1u << 22)) return {};
    std::vector<FPDF_WCHAR> buf(bytes / 2);
    FPDFTextObj_GetText(obj, tp, buf.data(), bytes);
    std::wstring t(reinterpret_cast<const wchar_t*>(buf.data()), bytes / 2 - 1);
    for (wchar_t& c : t)
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == 0) c = L' ';
    return t;
}

// Text without white space, to compare texts joined in different ways.
std::wstring Squash(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s)
        if (!iswspace(c)) out += c;
    return out;
}

bool IsBlank(const std::wstring& s) {
    for (wchar_t c : s)
        if (!iswspace(c)) return false;
    return true;
}

struct FontStyle {
    bool bold = false, italic = false, serif = false, mono = false;
};

FontStyle StyleOf(FPDF_FONT font) {
    FontStyle st;
    if (!font) return st;
    const int flags = FPDFFont_GetFlags(font);
    if (flags > 0) {
        st.mono = (flags & 1) != 0;
        st.serif = (flags & 2) != 0;
        st.italic = (flags & 64) != 0;
    }
    if (FPDFFont_GetWeight(font) >= 600) st.bold = true;
    char name[256] = "";
    if (FPDFFont_GetBaseFontName(font, name, sizeof(name)) > 0) {
        std::string n = name;
        for (char& c : n) c = (char)tolower((unsigned char)c);
        auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
        if (has("bold") || has("black") || has("heavy") || has("semibold") || has("demi")) st.bold = true;
        if (has("italic") || has("oblique")) st.italic = true;
        if (has("courier") || has("mono") || has("consol") || has("typewriter")) st.mono = true;
        if (has("sans") || has("arial") || has("helvetica") || has("calibri") || has("verdana") ||
            has("segoe") || has("tahoma"))
            st.serif = false;
        else if (has("times") || has("serif") || has("roman") || has("georgia") || has("cambria") ||
                 has("garamond") || has("palatino") || has("minion") || has("book"))
            st.serif = true;
    }
    return st;
}

// Letters each font of the page is known to contain (it draws them).
using Coverage = std::map<FPDF_FONT, std::set<wchar_t>>;

Coverage FontCoverage(FPDF_PAGE page, FPDF_TEXTPAGE tp) {
    Coverage cov;
    const int n = FPDFPage_CountObjects(page);
    for (int i = 0; i < n; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) continue;
        FPDF_FONT font = FPDFTextObj_GetFont(obj);
        if (!font) continue;
        auto& set = cov[font];
        for (wchar_t c : ObjectText(obj, tp)) set.insert(c);
    }
    return cov;
}

bool Covered(const Coverage& cov, FPDF_FONT font, const std::wstring& text) {
    // A font that is not embedded is the reader's complete font: any letter
    // it can encode is shown (checked by reading the text back).
    if (FPDFFont_GetIsEmbedded(font) == 0) return true;
    auto it = cov.find(font);
    if (it == cov.end()) return false;
    for (wchar_t c : text)
        if (c != L' ' && !it->second.count(c)) return false;
    return true;
}

// Can every character be written in a standard PDF font (WinAnsi)?
bool IsWinAnsi(const std::wstring& text) {
    if (text.empty()) return true;
    BOOL usedDefault = FALSE;
    const int n = WideCharToMultiByte(1252, WC_NO_BEST_FIT_CHARS, text.c_str(), (int)text.size(),
                                      nullptr, 0, nullptr, &usedDefault);
    return n > 0 && !usedDefault;
}

// The file data of an installed TrueType font that can show `text`.
bool SystemFontData(const wchar_t* face, const FontStyle& st, const std::wstring& text,
                    std::string& data) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    HFONT font = CreateFontW(-64, 0, 0, 0, st.bold ? FW_BOLD : FW_NORMAL, st.italic, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ old = SelectObject(dc, font);
    bool ok = false;
    wchar_t actual[LF_FACESIZE] = L"";
    GetTextFaceW(dc, LF_FACESIZE, actual);
    // Only a single-font file can be embedded (not a .ttc collection).
    const DWORD kTtcf = 0x66637474;  // 'ttcf'
    if (_wcsicmp(actual, face) == 0 && GetFontData(dc, kTtcf, 0, nullptr, 0) == GDI_ERROR) {
        std::vector<WORD> glyphs(text.size() + 1);
        ok = GetGlyphIndicesW(dc, text.c_str(), (int)text.size(), glyphs.data(),
                              GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR;
        for (size_t i = 0; ok && i < text.size(); ++i)
            if (glyphs[i] == 0xFFFF && !iswspace(text[i])) ok = false;
        const DWORD size = ok ? GetFontData(dc, 0, 0, nullptr, 0) : GDI_ERROR;
        ok = ok && size != GDI_ERROR && size > 0 && size < (64u << 20);
        if (ok) {
            data.resize(size);
            ok = GetFontData(dc, 0, 0, data.data(), size) == size;
        }
    }
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteDC(dc);
    return ok;
}
}  // namespace

// Fonts loaded for one edit, closed afterwards (text objects keep their own
// reference).
struct PdfEngine::FontCache {
    std::map<std::string, FPDF_FONT> fonts;
    ~FontCache() {
        for (auto& f : fonts)
            if (f.second) FPDFFont_Close(f.second);
    }
    FPDF_FONT Get(FPDF_DOCUMENT doc, const FontStyle& st, const std::wstring& text, std::wstring& error) {
        if (IsWinAnsi(text)) {
            const char* family = st.mono ? "Courier" : st.serif ? "Times" : "Helvetica";
            std::string name = family;
            if (st.mono || !st.serif) {
                name += st.bold && st.italic ? "-BoldOblique" : st.bold ? "-Bold" : st.italic ? "-Oblique" : "";
            } else {
                name += st.bold && st.italic ? "-BoldItalic" : st.bold ? "-Bold" : st.italic ? "-Italic" : "-Roman";
            }
            auto it = fonts.find(name);
            if (it != fonts.end()) return it->second;
            FPDF_FONT f = FPDFText_LoadStandardFont(doc, name.c_str());
            fonts[name] = f;
            if (f) return f;
        }
        // Letters beyond Western European: an installed font that has them.
        const wchar_t* first = st.mono ? L"Courier New" : st.serif ? L"Times New Roman" : L"Arial";
        const wchar_t* faces[] = {first, L"Arial", L"Segoe UI", L"Nirmala UI", L"Leelawadee UI",
                                  L"Ebrima", L"Gadugi", L"Segoe UI Historic", L"Segoe UI Symbol",
                                  L"Sylfaen", L"Arial Unicode MS"};
        for (const wchar_t* face : faces) {
            const std::string key = WideToUtf8(face) + (st.bold ? "|b" : "") + (st.italic ? "|i" : "");
            auto it = fonts.find(key);
            if (it != fonts.end()) {
                if (it->second) return it->second;
                continue;
            }
            std::string data;
            FPDF_FONT f = nullptr;
            if (SystemFontData(face, st, text, data))
                f = FPDFText_LoadFont(doc, reinterpret_cast<const uint8_t*>(data.data()),
                                      (uint32_t)data.size(), FPDF_FONT_TRUETYPE, TRUE);
            if (f) {
                fonts[key] = f;
                return f;
            }
        }
        error = L"No font installed on this PC has all the letters of the new text.";
        return nullptr;
    }
};

namespace {
// Groups the page's text objects into runs (see the section comment).
void CollectRuns(FPDF_PAGE page, FPDF_TEXTPAGE tp, int sx, int sy, std::vector<TextRun>& runs) {
    struct Prev {
        FPDF_FONT font = nullptr;
        float tf = 0;
        FS_MATRIX m{};
        float right = 0;
    } prev;
    bool open = false;
    TextRun cur;
    auto flush = [&] {
        if (open && !IsBlank(cur.text)) runs.push_back(cur);
        open = false;
    };
    const int n = FPDFPage_CountObjects(page);
    for (int i = 0; i < n && runs.size() < 20000; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (!obj || FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_TEXT) {
            flush();
            continue;
        }
        const std::wstring text = ObjectText(obj, tp);
        float l, b, r, t;
        FS_MATRIX m{1, 0, 0, 1, 0, 0};
        float tf = 0;
        if (text.empty() || !FPDFPageObj_GetBounds(obj, &l, &b, &r, &t)) {
            flush();
            continue;
        }
        FPDFPageObj_GetMatrix(obj, &m);
        FPDFTextObj_GetFontSize(obj, &tf);
        FPDF_FONT font = FPDFTextObj_GetFont(obj);
        float size = tf * std::hypot(m.c, m.d);
        if (size <= 0.5f || size > 2000) size = std::max(1.0f, t - b);
        const bool horizontal = std::fabs(m.b) < 1e-4f && std::fabs(m.c) < 1e-4f;
        const bool join = open && horizontal && font == prev.font && std::fabs(tf - prev.tf) < 0.01f &&
                          std::fabs(m.a - prev.m.a) < 1e-3f && std::fabs(m.d - prev.m.d) < 1e-3f &&
                          std::fabs(m.f - prev.m.f) < size * 0.2f && l >= prev.right - size * 0.6f &&
                          l - prev.right <= size * 1.2f;
        const RectF box = ToDisplay(page, sx, sy, l, t, r, b);
        if (join) {
            // A gap wider than a thin space is a word break.
            if (l - prev.right > size * 0.15f && !cur.text.empty() && cur.text.back() != L' ' &&
                text.front() != L' ')
                cur.text += L' ';
            cur.text += text;
            ++cur.count;
            cur.rect.left = std::min(cur.rect.left, box.left);
            cur.rect.top = std::min(cur.rect.top, box.top);
            cur.rect.right = std::max(cur.rect.right, box.right);
            cur.rect.bottom = std::max(cur.rect.bottom, box.bottom);
        } else {
            flush();
            cur = TextRun();
            cur.first = i;
            cur.count = 1;
            cur.text = text;
            cur.rect = box;
            cur.size = size;
            const FontStyle st = StyleOf(font);
            cur.bold = st.bold;
            cur.italic = st.italic;
            cur.serif = st.serif;
            cur.mono = st.mono;
            open = horizontal;
            if (!horizontal) {  // rotated text: one object per run
                open = true;
                flush();
                continue;
            }
        }
        prev = {font, tf, m, r};
    }
    flush();
}

// Replaces every occurrence of `find` in `text`; returns how many.
int ReplaceAll(std::wstring& text, const std::wstring& find, const std::wstring& with, bool matchCase) {
    if (find.empty()) return 0;
    std::wstring hay = text, needle = find;
    if (!matchCase) {
        CharLowerBuffW(hay.data(), (DWORD)hay.size());
        CharLowerBuffW(needle.data(), (DWORD)needle.size());
    }
    int count = 0;
    std::wstring out;
    size_t pos = 0;
    for (;;) {
        const size_t at = hay.find(needle, pos);
        if (at == std::wstring::npos) break;
        out += text.substr(pos, at - pos) + with;
        pos = at + needle.size();
        ++count;
    }
    if (count) text = out + text.substr(pos);
    return count;
}
}  // namespace

void PdfEngine::GetTextRuns(int pageIndex, std::vector<TextRun>& runs) {
    FPDF_PAGE page = GetPage(pageIndex);
    if (!page) return;
    Guarded([&] {
        const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
        const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);
        FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
        if (!tp) return;
        CollectRuns(page, tp, sx, sy, runs);
        FPDFText_ClosePage(tp);
    });
}

// Puts `text` in place of the run made of `objs` (page objects from index
// `firstIndex` on). See the section comment for the choice of font.
bool PdfEngine::SetRunText(FPDF_PAGE page, int firstIndex, const std::vector<FPDF_PAGEOBJECT>& objs,
                           const std::wstring& text, FontCache& fonts, std::wstring& error) {
    if (objs.empty()) return false;
    std::wstring clean = text;
    for (wchar_t& c : clean)
        if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    auto removeFrom = [&](size_t from) {
        for (size_t k = from; k < objs.size(); ++k)
            if (FPDFPage_RemoveObject(page, objs[k])) FPDFPageObj_Destroy(objs[k]);
    };
    if (IsBlank(clean)) {  // the text was deleted
        removeFrom(0);
        return true;
    }
    FPDF_PAGEOBJECT first = objs.front();
    FPDF_FONT font = FPDFTextObj_GetFont(first);

    // 1. The original font, if it is known to have every letter and the text
    //    reads back the same (the font can encode it).
    Coverage cov;
    if (FPDF_TEXTPAGE tp = FPDFText_LoadPage(page)) {
        cov = FontCoverage(page, tp);
        FPDFText_ClosePage(tp);
    }
    if (font && Covered(cov, font, clean) &&
        FPDFText_SetText(first, reinterpret_cast<FPDF_WIDESTRING>(clean.c_str()))) {
        std::wstring back;
        if (FPDF_TEXTPAGE tp = FPDFText_LoadPage(page)) {
            back = ObjectText(first, tp);
            FPDFText_ClosePage(tp);
        }
        if (Squash(back) == Squash(clean)) {
            removeFrom(1);
            return true;
        }
    }

    // 2. A similar font that has the letters, at the same place and size.
    FPDF_FONT other = fonts.Get(m_doc, StyleOf(font), clean, error);
    if (!other) return false;
    float tf = 0;
    FS_MATRIX m{1, 0, 0, 1, 0, 0};
    FPDFTextObj_GetFontSize(first, &tf);
    FPDFPageObj_GetMatrix(first, &m);
    FPDF_PAGEOBJECT obj = FPDFPageObj_CreateTextObj(m_doc, other, tf > 0 ? tf : 12);
    if (!obj) return false;
    if (!FPDFText_SetText(obj, reinterpret_cast<FPDF_WIDESTRING>(clean.c_str()))) {
        FPDFPageObj_Destroy(obj);
        return false;
    }
    FPDFPageObj_SetMatrix(obj, &m);
    unsigned int r, g, b, a;
    if (FPDFPageObj_GetFillColor(first, &r, &g, &b, &a)) FPDFPageObj_SetFillColor(obj, r, g, b, a);
    if (FPDFPageObj_GetStrokeColor(first, &r, &g, &b, &a)) FPDFPageObj_SetStrokeColor(obj, r, g, b, a);
    const FPDF_TEXT_RENDERMODE mode = FPDFTextObj_GetTextRenderMode(first);
    if (mode != FPDF_TEXTRENDERMODE_UNKNOWN) FPDFTextObj_SetTextRenderMode(obj, mode);
    if (!FPDFPage_InsertObjectAtIndex(page, obj, (size_t)firstIndex)) {
        FPDFPageObj_Destroy(obj);
        return false;
    }
    removeFrom(0);
    m_editFontChanged = true;
    return true;
}

bool PdfEngine::EditText(const EditOp& op, std::wstring& error) {
    if (op.page < 0 || op.page >= m_pageCount || op.index < 0 || op.count <= 0) return false;
    bool ok = false;
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        std::vector<FPDF_PAGEOBJECT> objs;
        const int n = FPDFPage_CountObjects(page);
        for (int i = op.index; i < op.index + op.count && i < n; ++i) {
            FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
            if (obj && FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_TEXT) objs.push_back(obj);
        }
        // The objects must still hold the text that was edited.
        std::wstring old;
        if (FPDF_TEXTPAGE tp = FPDFText_LoadPage(page)) {
            for (FPDF_PAGEOBJECT obj : objs) old += ObjectText(obj, tp);
            FPDFText_ClosePage(tp);
        }
        if ((int)objs.size() != op.count || Squash(old) != Squash(op.find)) {
            error = L"The text could not be found on the page any more.";
        } else {
            FontCache fonts;
            ok = SetRunText(page, op.index, objs, op.text, fonts, error) && FPDFPage_GenerateContent(page);
        }
        FPDF_ClosePage(page);
    });
    return ok;
}

bool PdfEngine::ReplaceEverywhere(const EditOp& op, std::wstring& error) {
    if (op.find.empty()) return false;
    bool ok = true;
    FontCache fonts;
    for (int p = 0; p < m_pageCount && ok; ++p) {
        ok = Guarded([&] {
            FPDF_PAGE page = FPDF_LoadPage(m_doc, p);
            if (!page) return;
            const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
            const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);
            std::vector<TextRun> runs;
            if (FPDF_TEXTPAGE tp = FPDFText_LoadPage(page)) {
                CollectRuns(page, tp, sx, sy, runs);
                FPDFText_ClosePage(tp);
            }
            bool changed = false;
            // Last run first, so the object indices of earlier runs stay valid.
            for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
                std::wstring text = it->text;
                const int count = ReplaceAll(text, op.find, op.text, op.matchCase);
                if (!count) continue;
                std::vector<FPDF_PAGEOBJECT> objs;
                for (int i = it->first; i < it->first + it->count; ++i)
                    objs.push_back(FPDFPage_GetObject(page, i));
                std::wstring err;
                if (SetRunText(page, it->first, objs, text, fonts, err)) {
                    m_editCount += count;
                    changed = true;
                } else if (error.empty()) {
                    error = err;
                }
            }
            if (changed) FPDFPage_GenerateContent(page);
            FPDF_ClosePage(page);
        });
    }
    if (ok && m_editCount == 0) {
        if (error.empty())
            error = L"\x201C" + op.find + L"\x201D was not found in text that can be edited. "
                    L"Scanned pages are pictures and have no editable text.";
        return false;
    }
    error.clear();
    return ok;
}


// ===========================================================================
// Forms, drawings, stamps, pictures and new text
// ===========================================================================
namespace {
// Display points (top-left origin, unrotated) -> PDF user space.
FS_POINTF ToPage(FPDF_PAGE page, float x, float y) {
    const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
    const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);
    double px = 0, py = 0;
    FPDF_DeviceToPage(page, 0, 0, sx, sy, 0, (int)std::lround(x * 100), (int)std::lround(y * 100), &px, &py);
    return {(float)px, (float)py};
}

FS_RECTF ToPageRect(FPDF_PAGE page, const RectF& r) {
    const FS_POINTF a = ToPage(page, r.left, r.top), b = ToPage(page, r.right, r.bottom);
    return {std::min(a.x, b.x), std::max(a.y, b.y), std::max(a.x, b.x), std::min(a.y, b.y)};
}

// The direction of "right" and "up" on the screen, in page space (so new
// text and pictures are upright on rotated pages too).
void ScreenAxes(FPDF_PAGE page, float x, float y, FS_POINTF& right, FS_POINTF& up) {
    const FS_POINTF o = ToPage(page, x, y), rx = ToPage(page, x + 100, y), uy = ToPage(page, x, y - 100);
    right = {(rx.x - o.x) / 100, (rx.y - o.y) / 100};
    up = {(uy.x - o.x) / 100, (uy.y - o.y) / 100};
    auto norm = [](FS_POINTF& v) {
        const float len = std::hypot(v.x, v.y);
        if (len > 0) v = {v.x / len, v.y / len};
    };
    norm(right);
    norm(up);
}

void SetAnnotDate(FPDF_ANNOTATION annot, const std::wstring& author, const std::wstring& now) {
    FPDFAnnot_SetStringValue(annot, "M", reinterpret_cast<FPDF_WIDESTRING>(now.c_str()));
    FPDFAnnot_SetStringValue(annot, "CreationDate", reinterpret_cast<FPDF_WIDESTRING>(now.c_str()));
    if (!author.empty()) FPDFAnnot_SetStringValue(annot, "T", reinterpret_cast<FPDF_WIDESTRING>(author.c_str()));
}

// A BGRA picture as a PDF image object filling the display rectangle `rc`
// (upright as seen on screen, also on rotated pages).
FPDF_PAGEOBJECT MakeImage(FPDF_DOCUMENT doc, FPDF_PAGE page, const EditOp& op, const RectF& rc) {
    if (op.imageW <= 0 || op.imageH <= 0 || op.pixels.size() < (size_t)op.imageW * op.imageH * 4) return nullptr;
    FPDF_BITMAP bmp = FPDFBitmap_Create(op.imageW, op.imageH, 1);
    if (!bmp) return nullptr;
    uint8_t* dst = static_cast<uint8_t*>(FPDFBitmap_GetBuffer(bmp));
    const int stride = FPDFBitmap_GetStride(bmp);
    for (int y = 0; y < op.imageH; ++y)
        memcpy(dst + (size_t)y * stride, op.pixels.data() + (size_t)y * op.imageW * 4, (size_t)op.imageW * 4);
    FPDF_PAGEOBJECT obj = FPDFPageObj_NewImageObj(doc);
    if (obj && !FPDFImageObj_SetBitmap(nullptr, 0, obj, bmp)) {
        FPDFPageObj_Destroy(obj);
        obj = nullptr;
    }
    FPDFBitmap_Destroy(bmp);
    if (!obj) return nullptr;
    // The image's unit square: (0,0) bottom-left, (1,0) bottom-right, (0,1) top-left.
    const FS_POINTF bl = ToPage(page, rc.left, rc.bottom), br = ToPage(page, rc.right, rc.bottom),
                    tl = ToPage(page, rc.left, rc.top);
    FS_MATRIX m{br.x - bl.x, br.y - bl.y, tl.x - bl.x, tl.y - bl.y, bl.x, bl.y};
    FPDFPageObj_SetMatrix(obj, &m);
    return obj;
}
}  // namespace

bool PdfEngine::SetField(const EditOp& op, std::wstring& error) {
    if (!m_form || op.page < 0 || op.page >= m_pageCount) return false;
    FPDF_FORMHANDLE form = m_form->handle;
    bool ok = false;
    Guarded([&] {
        FPDF_PAGE page = LoadEditPage(op.page);
        if (!page) return;
        FPDF_ANNOTATION annot = op.index >= 0 && op.index < FPDFPage_GetAnnotCount(page)
                                    ? FPDFPage_GetAnnot(page, op.index)
                                    : nullptr;
        if (annot && FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_WIDGET) {
            const int type = FPDFAnnot_GetFormFieldType(form, annot);
            if (type == kFieldText || (type == kFieldComboBox && op.option < 0)) {
                if (FORM_SetFocusedAnnot(form, annot)) {
                    FORM_SelectAllText(form, page);
                    FORM_ReplaceSelection(form, page, reinterpret_cast<FPDF_WIDESTRING>(op.text.c_str()));
                    ok = FORM_ForceToKillFocus(form) != 0;
                }
            } else if (type == kFieldCheckBox || type == kFieldRadio) {
                if ((FPDFAnnot_IsChecked(form, annot) != 0) == op.checked) {
                    ok = true;
                } else {
                    // A click in the middle of the box toggles it, as in any viewer.
                    FS_RECTF r;
                    FPDFAnnot_GetRect(annot, &r);
                    const double cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
                    FORM_OnMouseMove(form, page, 0, cx, cy);
                    FORM_OnLButtonDown(form, page, 0, cx, cy);
                    FORM_OnLButtonUp(form, page, 0, cx, cy);
                    FORM_ForceToKillFocus(form);
                    ok = (FPDFAnnot_IsChecked(form, annot) != 0) == op.checked;
                }
            } else if ((type == kFieldComboBox || type == kFieldListBox) && op.option >= 0) {
                if (FORM_SetFocusedAnnot(form, annot)) {
                    ok = FORM_SetIndexSelected(form, page, op.option, TRUE) != 0;
                    FORM_ForceToKillFocus(form);
                }
            }
        }
        if (annot) FPDFPage_CloseAnnot(annot);
        CloseEditPage(page);
    });
    if (!ok) error = L"The form field could not be changed.";
    return ok;
}

bool PdfEngine::AddShape(const EditOp& op) {
    if (op.page < 0 || op.page >= m_pageCount || op.points.size() < 2) return false;
    bool ok = false;
    const std::wstring now = PdfDateNow();
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        std::vector<FS_POINTF> pts;
        for (const PointF& p : op.points) pts.push_back(ToPage(page, p.x, p.y));
        const bool box = op.shape == kShapeRect || op.shape == kShapeEllipse;
        FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(
            page, op.shape == kShapeRect ? FPDF_ANNOT_SQUARE : op.shape == kShapeEllipse ? FPDF_ANNOT_CIRCLE : FPDF_ANNOT_INK);
        if (annot) {
            const float w = std::max(0.5f, op.width);
            FS_RECTF bounds{1e9f, -1e9f, -1e9f, 1e9f};
            auto grow = [&](const FS_POINTF& p) {
                bounds.left = std::min(bounds.left, p.x);
                bounds.right = std::max(bounds.right, p.x);
                bounds.top = std::max(bounds.top, p.y);
                bounds.bottom = std::min(bounds.bottom, p.y);
            };
            if (box) {
                grow(pts.front());
                grow(pts.back());
            } else {
                std::vector<FS_POINTF> stroke = op.shape == kShapePen ? pts : std::vector<FS_POINTF>{pts.front(), pts.back()};
                for (const FS_POINTF& p : stroke) grow(p);
                FPDFAnnot_AddInkStroke(annot, stroke.data(), stroke.size());
                if (op.shape == kShapeArrow) {
                    // The head: two short strokes back from the tip.
                    const FS_POINTF a = pts.front(), b = pts.back();
                    const float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
                    if (len > 0.1f) {
                        const float head = std::max(8.0f, w * 4), ux = dx / len, uy = dy / len;
                        const float c = 0.866f, s2 = 0.5f;  // 30 degrees
                        FS_POINTF l{b.x - head * (ux * c - uy * s2), b.y - head * (uy * c + ux * s2)};
                        FS_POINTF r{b.x - head * (ux * c + uy * s2), b.y - head * (uy * c - ux * s2)};
                        FS_POINTF headStroke[] = {l, b, r};
                        FPDFAnnot_AddInkStroke(annot, headStroke, 3);
                        grow(l);
                        grow(r);
                    }
                }
            }
            bounds.left -= w;
            bounds.right += w;
            bounds.top += w;
            bounds.bottom -= w;
            FPDFAnnot_SetRect(annot, &bounds);
            FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, GetRValue(op.color), GetGValue(op.color),
                               GetBValue(op.color), 255);
            FPDFAnnot_SetBorder(annot, 0, 0, w);
            FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
            SetAnnotDate(annot, op.author, now);
            FPDFPage_CloseAnnot(annot);
            ok = true;
        }
        FPDF_ClosePage(page);
    });
    return ok;
}

bool PdfEngine::AddStamp(const EditOp& op) {
    if (op.page < 0 || op.page >= m_pageCount || op.text.empty()) return false;
    bool ok = false;
    const std::wstring now = PdfDateNow();
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        const FS_RECTF r = ToPageRect(page, op.rect);
        FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, FPDF_ANNOT_STAMP);
        if (annot) {
            FPDFAnnot_SetRect(annot, &r);
            const unsigned cr = GetRValue(op.color), cg = GetGValue(op.color), cb = GetBValue(op.color);
            const float w = r.right - r.left, h = r.top - r.bottom;
            const float line = std::max(1.5f, h * 0.06f);
            // A rounded frame...
            FPDF_PAGEOBJECT frame = FPDFPageObj_CreateNewRect(r.left + line, r.bottom + line, w - 2 * line, h - 2 * line);
            FPDFPageObj_SetStrokeColor(frame, cr, cg, cb, 255);
            FPDFPageObj_SetStrokeWidth(frame, line);
            FPDFPath_SetDrawMode(frame, FPDF_FILLMODE_NONE, TRUE);
            FPDFAnnot_AppendObject(annot, frame);
            // ...and the word, as large as fits, centred.
            float size = h * 0.55f;
            FPDF_PAGEOBJECT text = FPDFPageObj_NewTextObj(m_doc, "Helvetica-Bold", size);
            if (text) {
                FPDFText_SetText(text, reinterpret_cast<FPDF_WIDESTRING>(op.text.c_str()));
                float l, b, rr, t;
                if (FPDFPageObj_GetBounds(text, &l, &b, &rr, &t) && rr - l > w - 4 * line) {
                    size *= (w - 4 * line) / (rr - l);
                    FPDFTextObj_SetFontSize(text, size);
                    FPDFPageObj_GetBounds(text, &l, &b, &rr, &t);
                }
                const float tw = rr - l;
                FPDFPageObj_Transform(text, 1, 0, 0, 1, r.left + (w - tw) / 2 - l, r.bottom + (h - size * 0.72f) / 2);
                FPDFPageObj_SetFillColor(text, cr, cg, cb, 255);
                FPDFAnnot_AppendObject(annot, text);
            }
            FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, cr, cg, cb, 255);
            FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
            FPDFAnnot_SetStringValue(annot, "Contents", reinterpret_cast<FPDF_WIDESTRING>(op.text.c_str()));
            SetAnnotDate(annot, op.author, now);
            FPDFPage_CloseAnnot(annot);
            ok = true;
        }
        FPDF_ClosePage(page);
    });
    return ok;
}

bool PdfEngine::AddImage(const EditOp& op) {
    if (op.page < 0 || op.page >= m_pageCount) return false;
    bool ok = false;
    const std::wstring now = PdfDateNow();
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        const FS_RECTF r = ToPageRect(page, op.rect);
        if (FPDF_PAGEOBJECT img = MakeImage(m_doc, page, op, op.rect)) {
            if (op.asAnnot) {
                // Signatures: a stamp annotation, so it can be deleted again.
                if (FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, FPDF_ANNOT_STAMP)) {
                    FPDFAnnot_SetRect(annot, &r);
                    ok = FPDFAnnot_AppendObject(annot, img) != 0;
                    if (!ok) FPDFPageObj_Destroy(img);
                    FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
                    if (!op.text.empty())
                        FPDFAnnot_SetStringValue(annot, "Contents", reinterpret_cast<FPDF_WIDESTRING>(op.text.c_str()));
                    SetAnnotDate(annot, op.author, now);
                    FPDFPage_CloseAnnot(annot);
                } else {
                    FPDFPageObj_Destroy(img);
                }
            } else {
                FPDFPage_InsertObject(page, img);
                ok = FPDFPage_GenerateContent(page) != 0;
            }
        }
        FPDF_ClosePage(page);
    });
    return ok;
}

bool PdfEngine::AddText(const EditOp& op, std::wstring& error) {
    if (op.page < 0 || op.page >= m_pageCount || IsBlank(op.text)) return false;
    bool ok = false;
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        FontStyle st;
        st.bold = op.bold;
        st.italic = op.italic;
        st.serif = op.serif;
        st.mono = op.mono;
        FontCache fonts;
        const float size = op.fontSize > 0 ? op.fontSize : 12;
        FS_POINTF right, up;
        ScreenAxes(page, op.x, op.y, right, up);
        // One text object per line, each a line height below the previous.
        size_t start = 0;
        int line = 0;
        ok = true;
        while (ok && start <= op.text.size()) {
            size_t end = op.text.find(L'\n', start);
            if (end == std::wstring::npos) end = op.text.size();
            std::wstring text = op.text.substr(start, end - start);
            if (!text.empty() && text.back() == L'\r') text.pop_back();
            if (!IsBlank(text)) {
                FPDF_FONT font = fonts.Get(m_doc, st, text, error);
                FPDF_PAGEOBJECT obj = font ? FPDFPageObj_CreateTextObj(m_doc, font, size) : nullptr;
                if (!obj || !FPDFText_SetText(obj, reinterpret_cast<FPDF_WIDESTRING>(text.c_str()))) {
                    if (obj) FPDFPageObj_Destroy(obj);
                    ok = false;
                    break;
                }
                // Baseline: the top of the box plus the ascent, then line by line.
                const FS_POINTF o = ToPage(page, op.x, op.y + size * (0.85f + 1.2f * line));
                FS_MATRIX m{right.x, right.y, up.x, up.y, o.x, o.y};
                FPDFPageObj_SetMatrix(obj, &m);
                FPDFPageObj_SetFillColor(obj, GetRValue(op.color), GetGValue(op.color), GetBValue(op.color), 255);
                FPDFPage_InsertObject(page, obj);
            }
            ++line;
            start = end + 1;
        }
        if (ok) ok = FPDFPage_GenerateContent(page) != 0;
        FPDF_ClosePage(page);
    });
    return ok;
}

bool PdfEngine::StyleText(const EditOp& op, std::wstring& error) {
    if (op.page < 0 || op.page >= m_pageCount || op.count <= 0) return false;
    bool ok = false;
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        std::vector<FPDF_PAGEOBJECT> objs;
        const int n = FPDFPage_CountObjects(page);
        for (int i = op.index; i < op.index + op.count && i < n; ++i) {
            FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
            if (obj && FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_TEXT) objs.push_back(obj);
        }
        if ((int)objs.size() != op.count) {
            error = L"The text could not be found on the page any more.";
        } else {
            // The size given is the size as shown: scale every object alike.
            float factor = 1;
            if (op.fontSize > 0) {
                float tf = 0;
                FS_MATRIX m{1, 0, 0, 1, 0, 0};
                FPDFTextObj_GetFontSize(objs.front(), &tf);
                FPDFPageObj_GetMatrix(objs.front(), &m);
                const float shown = tf * std::hypot(m.c, m.d);
                if (shown > 0) factor = op.fontSize / shown;
            }
            // Keep the left end of the line where it was.
            float left0 = 0, b0, r0, t0;
            FPDFPageObj_GetBounds(objs.front(), &left0, &b0, &r0, &t0);
            for (FPDF_PAGEOBJECT obj : objs) {
                if (factor != 1) {
                    float tf = 0;
                    FPDFTextObj_GetFontSize(obj, &tf);
                    FPDFTextObj_SetFontSize(obj, tf * factor);
                    // Objects after the first move so the gaps scale too.
                    FS_MATRIX m{1, 0, 0, 1, 0, 0};
                    FPDFPageObj_GetMatrix(obj, &m);
                    m.e = left0 + (m.e - left0) * factor;
                    FPDFPageObj_SetMatrix(obj, &m);
                }
                if (op.color != CLR_INVALID)
                    FPDFPageObj_SetFillColor(obj, GetRValue(op.color), GetGValue(op.color), GetBValue(op.color), 255);
            }
            ok = FPDFPage_GenerateContent(page) != 0;
        }
        FPDF_ClosePage(page);
    });
    return ok;
}

// ===========================================================================
// Comments
// ===========================================================================
bool PdfEngine::AddNote(const EditOp& op) {
    if (op.page < 0 || op.page >= m_pageCount) return false;
    bool ok = false;
    const std::wstring now = PdfDateNow();
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
        const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);
        double px = 0, py = 0;
        if (FPDF_DeviceToPage(page, 0, 0, sx, sy, 0, (int)std::lround(op.x * 100),
                              (int)std::lround(op.y * 100), &px, &py)) {
            if (FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, FPDF_ANNOT_TEXT)) {
                // A 20-point note icon whose top-left corner is where it was placed.
                FS_RECTF r{(float)px, (float)py, (float)px + 20, (float)py - 20};
                FPDFAnnot_SetRect(annot, &r);
                FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, 255, 200, 40, 255);
                FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT | FPDF_ANNOT_FLAG_NOZOOM |
                                              FPDF_ANNOT_FLAG_NOROTATE);
                auto set = [&](const char* key, const std::wstring& v) {
                    FPDFAnnot_SetStringValue(annot, key, reinterpret_cast<FPDF_WIDESTRING>(v.c_str()));
                };
                set("Contents", op.text);
                set("T", op.author);
                set("M", now);
                set("CreationDate", now);
                FPDFPage_CloseAnnot(annot);
                ok = true;
            }
        }
        FPDF_ClosePage(page);
    });
    return ok;
}

bool PdfEngine::ChangeAnnot(const EditOp& op, std::wstring& error) {
    if (op.page < 0 || op.page >= m_pageCount) return false;
    bool ok = false;
    const std::wstring now = PdfDateNow();
    Guarded([&] {
        FPDF_PAGE page = FPDF_LoadPage(m_doc, op.page);
        if (!page) return;
        FPDF_ANNOTATION annot = op.index >= 0 && op.index < FPDFPage_GetAnnotCount(page)
                                    ? FPDFPage_GetAnnot(page, op.index)
                                    : nullptr;
        if (!annot || !IsCommentType(FPDFAnnot_GetSubtype(annot))) {
            error = L"The comment could not be found any more.";
        } else if (op.kind == EditOp::EditComment) {
            ok = FPDFAnnot_SetStringValue(annot, "Contents",
                                          reinterpret_cast<FPDF_WIDESTRING>(op.text.c_str())) &&
                 FPDFAnnot_SetStringValue(annot, "M", reinterpret_cast<FPDF_WIDESTRING>(now.c_str()));
            if (ok && !op.author.empty() && AnnotString(annot, "T").empty())
                FPDFAnnot_SetStringValue(annot, "T", reinterpret_cast<FPDF_WIDESTRING>(op.author.c_str()));
        } else {
            // Its pop-up window (if any) goes too; the higher index first.
            int popup = -1;
            if (FPDF_ANNOTATION p = FPDFAnnot_GetLinkedAnnot(annot, "Popup")) {
                popup = FPDFPage_GetAnnotIndex(page, p);
                FPDFPage_CloseAnnot(p);
            }
            FPDFPage_CloseAnnot(annot);
            annot = nullptr;
            if (popup > op.index) FPDFPage_RemoveAnnot(page, popup);
            ok = FPDFPage_RemoveAnnot(page, op.index) != 0;
            if (popup >= 0 && popup < op.index) FPDFPage_RemoveAnnot(page, popup);
        }
        if (annot) FPDFPage_CloseAnnot(annot);
        FPDF_ClosePage(page);
    });
    return ok;
}

void PdfEngine::ListComments(std::vector<std::pair<int, CommentInfo>>& out) {
    if (!m_doc) return;
    for (int p = 0; p < m_pageCount && out.size() < 20000; ++p) {
        Guarded([&] {
            FPDF_PAGE page = FPDF_LoadPage(m_doc, p);
            if (!page) return;
            if (FPDFPage_GetAnnotCount(page) > 0) {
                const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
                const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);
                std::vector<CommentInfo> comments;
                ReadComments(page, sx, sy, comments);
                for (CommentInfo& c : comments)
                    if (c.subtype == kAnnotNote || !c.text.empty()) out.push_back({p, std::move(c)});
            }
            FPDF_ClosePage(page);
        });
    }
}

// ---------------------------------------------------------------------------
// PixelBuffer
// ---------------------------------------------------------------------------
bool PixelBuffer::Allocate(int w, int h) {
    Free();
    if (w <= 0 || h <= 0) return false;
    size_t bytes = (size_t)w * (size_t)h * 4;
    bits = (uint8_t*)VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!bits) return false;
    width = w;
    height = h;
    return true;
}

void PixelBuffer::Free() {
    if (bits) VirtualFree(bits, 0, MEM_RELEASE);
    bits = nullptr;
    width = height = 0;
}
