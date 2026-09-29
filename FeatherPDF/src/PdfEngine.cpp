// PdfEngine.cpp - PDFium integration.
#include "PdfEngine.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "Util.h"
#include "fpdf_doc.h"
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

void PdfEngine::InitLibrary() {
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
}

void PdfEngine::DestroyLibrary() { FPDF_DestroyLibrary(); }

PdfEngine::PdfEngine() = default;
PdfEngine::~PdfEngine() { Close(); }

void PdfEngine::ReleasePages() {
    for (auto& p : m_pages) FPDF_ClosePage(p.second);
    m_pages.clear();
}

void PdfEngine::Close() {
    ReleasePages();
    if (m_doc) {
        FPDF_CloseDocument(m_doc);
        m_doc = nullptr;
    }
    m_file.reset();
    m_pageCount = 0;
}

OpenError PdfEngine::Open(const std::wstring& path, const std::string& password,
                          std::vector<SizeF>& pageSizes) {
    auto file = std::make_unique<FileSource>();
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

    FPDF_DOCUMENT doc = nullptr;
    unsigned long err = FPDF_ERR_SUCCESS;
    bool ok = Guarded([&] {
        doc = FPDF_LoadCustomDocument(&file->access, password.empty() ? nullptr : password.c_str());
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

    int count = 0;
    std::vector<SizeF> sizes;
    ok = Guarded([&] {
        count = FPDF_GetPageCount(doc);
        if (count <= 0) return;
        sizes.resize((size_t)count);
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
    if (!ok || count <= 0) {
        Guarded([&] { FPDF_CloseDocument(doc); });
        return ok ? OpenError::NoPages : OpenError::NotPdf;
    }

    // Success: replace the previous document.
    Close();
    m_doc = doc;
    m_file = std::move(file);
    m_pageCount = count;
    pageSizes = std::move(sizes);
    return OpenError::None;
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
    if (!Guarded([&] { page = FPDF_LoadPage(m_doc, index); }) || !page) return nullptr;
    m_pages.insert(m_pages.begin(), {index, page});
    if (m_pages.size() > kMaxParsedPages) {
        FPDF_ClosePage(m_pages.back().second);
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
                                std::vector<LinkInfo>& links) {
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
