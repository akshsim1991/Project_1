// pdf_jni.cpp - the PDFium engine for Android (JNI bridge).
//
// This is the Android counterpart of FeatherPDF/src/PdfEngine.cpp. All
// functions are called from ONE Kotlin worker thread (PdfWorker), because
// PDFium is not thread-safe. The UI thread never calls into this file.
//
// Documents are read lazily through FPDF_LoadCustomDocument with pread() on
// a file descriptor, so a 500 MB PDF is never loaded into memory. Pages are
// rendered straight into Android Bitmap memory (no intermediate copies).
#include <android/bitmap.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "fpdf_doc.h"
#include "fpdf_text.h"
#include "fpdfview.h"

namespace {

// Keep the most recently parsed pages: tiles of a page are requested in
// bursts and content-stream parsing is the expensive part.
constexpr size_t kMaxParsedPages = 4;

struct Document {
    FPDF_DOCUMENT doc = nullptr;
    FPDF_FILEACCESS access{};
    int fd = -1;
    int pageCount = 0;
    std::vector<std::pair<int, FPDF_PAGE>> pages;  // front = most recently used

    ~Document() {
        for (auto& p : pages) FPDF_ClosePage(p.second);
        if (doc) FPDF_CloseDocument(doc);
        if (fd >= 0) close(fd);
    }

    FPDF_PAGE Page(int index) {
        if (index < 0 || index >= pageCount) return nullptr;
        for (size_t i = 0; i < pages.size(); ++i) {
            if (pages[i].first == index) {
                auto entry = pages[i];
                pages.erase(pages.begin() + (long)i);
                pages.insert(pages.begin(), entry);
                return entry.second;
            }
        }
        FPDF_PAGE page = FPDF_LoadPage(doc, index);
        if (!page) return nullptr;
        pages.insert(pages.begin(), {index, page});
        if (pages.size() > kMaxParsedPages) {
            FPDF_ClosePage(pages.back().second);
            pages.pop_back();
        }
        return page;
    }

    void ReleasePages() {
        for (auto& p : pages) FPDF_ClosePage(p.second);
        pages.clear();
    }
};

Document* Doc(jlong handle) { return reinterpret_cast<Document*>(handle); }

int GetBlock(void* param, unsigned long position, unsigned char* buf, unsigned long size) {
    auto* d = static_cast<Document*>(param);
    unsigned long done = 0;
    while (done < size) {
        ssize_t n = pread(d->fd, buf + done, size - done, (off_t)(position + done));
        if (n <= 0) return 0;
        done += (unsigned long)n;
    }
    return 1;
}

jstring ToJString(JNIEnv* env, const unsigned short* s, size_t len) {
    return env->NewString(reinterpret_cast<const jchar*>(s), (jsize)len);
}

// Unrotated display coordinates (points, top-left origin), 0.01 pt precision.
void ToDisplay(FPDF_PAGE page, int sx, int sy, double l, double t, double r, double b, float* out) {
    int x1, y1, x2, y2;
    FPDF_PageToDevice(page, 0, 0, sx, sy, 0, l, t, &x1, &y1);
    FPDF_PageToDevice(page, 0, 0, sx, sy, 0, r, b, &x2, &y2);
    out[0] = std::min(x1, x2) / 100.0f;
    out[1] = std::min(y1, y2) / 100.0f;
    out[2] = std::max(x1, x2) / 100.0f;
    out[3] = std::max(y1, y2) / 100.0f;
}

struct Target {
    int page = -1;
    float y = -1;
    std::u16string uri;
};

void ReadDest(Document* d, FPDF_DEST dest, Target& t) {
    t.page = FPDFDest_GetDestPageIndex(d->doc, dest);
    if (t.page >= d->pageCount) t.page = -1;
    FPDF_BOOL hasX = 0, hasY = 0, hasZ = 0;
    FS_FLOAT x = 0, y = 0, z = 0;
    if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZ, &x, &y, &z) && hasY) t.y = y;
}

void ReadAction(Document* d, FPDF_ACTION action, Target& t) {
    switch (FPDFAction_GetType(action)) {
        case PDFACTION_GOTO:
            if (FPDF_DEST dest = FPDFAction_GetDest(d->doc, action)) ReadDest(d, dest, t);
            break;
        case PDFACTION_URI: {
            unsigned long len = FPDFAction_GetURIPath(d->doc, action, nullptr, 0);
            if (len <= 1 || len > 8192) break;
            std::string uri(len, '\0');
            FPDFAction_GetURIPath(d->doc, action, uri.data(), len);
            uri.resize(len - 1);
            // URIs are 7-bit ASCII by the PDF spec.
            t.uri.assign(uri.begin(), uri.end());
            break;
        }
        default:  // launch / remote files are never followed (security)
            break;
    }
}

// Night / dim colours applied to rendered RGBA pixels.
void ApplyColors(uint8_t* pixels, int w, int h, int stride, int mode) {
    if (mode == 0) return;
    uint8_t lut[256];
    for (int c = 0; c < 256; ++c)
        lut[c] = mode == 1 ? (uint8_t)(30 + (255 - c) * 195 / 255) : (uint8_t)(c * 200 / 255);
    for (int y = 0; y < h; ++y) {
        uint8_t* p = pixels + (size_t)y * stride;
        for (int x = 0; x < w; ++x, p += 4) {
            p[0] = lut[p[0]];
            p[1] = lut[p[1]];
            p[2] = lut[p[2]];
        }
    }
}

jobjectArray NewStringArray(JNIEnv* env, jsize n) {
    jclass cls = env->FindClass("java/lang/String");
    return env->NewObjectArray(n, cls, nullptr);
}

}  // namespace

#define JNI_FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_featherpdf_viewer_Native_##name

JNI_FN(void, nInit)(JNIEnv*, jclass) {
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    FPDF_InitLibraryWithConfig(&config);
}

// Returns a document handle, or 0 with err[0] set to the PDFium error code
// (1 unknown, 2 file, 3 format, 4 password, 5 security, 6 no pages).
JNI_FN(jlong, nOpen)(JNIEnv* env, jclass, jint fd, jstring password, jintArray err) {
    auto setErr = [&](jint code) { env->SetIntArrayRegion(err, 0, 1, &code); };
    auto* d = new Document();
    d->fd = dup(fd);  // the Kotlin side may close its descriptor
    struct stat st{};
    if (d->fd < 0 || fstat(d->fd, &st) != 0 || st.st_size <= 0) {
        delete d;
        setErr(2);
        return 0;
    }
    d->access.m_FileLen = (unsigned long)st.st_size;
    d->access.m_GetBlock = GetBlock;
    d->access.m_Param = d;
    const char* pw = password ? env->GetStringUTFChars(password, nullptr) : nullptr;
    d->doc = FPDF_LoadCustomDocument(&d->access, pw);
    if (pw) env->ReleaseStringUTFChars(password, pw);
    if (!d->doc) {
        jint code = (jint)FPDF_GetLastError();
        delete d;
        setErr(code ? code : 1);
        return 0;
    }
    d->pageCount = FPDF_GetPageCount(d->doc);
    if (d->pageCount <= 0) {
        delete d;
        setErr(6);
        return 0;
    }
    return reinterpret_cast<jlong>(d);
}

JNI_FN(void, nClose)(JNIEnv*, jclass, jlong h) { delete Doc(h); }

JNI_FN(void, nTrim)(JNIEnv*, jclass, jlong h) {
    if (Doc(h)) Doc(h)->ReleasePages();
}

JNI_FN(jfloatArray, nPageSizes)(JNIEnv* env, jclass, jlong h) {
    Document* d = Doc(h);
    std::vector<float> sizes((size_t)d->pageCount * 2);
    for (int i = 0; i < d->pageCount; ++i) {
        FS_SIZEF s{};
        if (!FPDF_GetPageSizeByIndexF(d->doc, i, &s)) s = {612, 792};
        if (!(s.width >= 1 && s.width <= 200000)) s.width = 612;
        if (!(s.height >= 1 && s.height <= 200000)) s.height = 792;
        sizes[(size_t)i * 2] = s.width;
        sizes[(size_t)i * 2 + 1] = s.height;
    }
    jfloatArray out = env->NewFloatArray((jsize)sizes.size());
    env->SetFloatArrayRegion(out, 0, (jsize)sizes.size(), sizes.data());
    return out;
}

// Renders the rectangle (x, y, w, h) of the page scaled to pageW x pageH
// pixels (rotated `rotate` quarter turns) into the top-left of `bitmap`.
JNI_FN(jboolean, nRender)(JNIEnv* env, jclass, jlong h, jint pageIndex, jobject bitmap, jint x,
                          jint y, jint w, jint hgt, jint pageW, jint pageH, jint rotate,
                          jint colors) {
    Document* d = Doc(h);
    AndroidBitmapInfo info{};
    void* pixels = nullptr;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 || (uint32_t)w > info.width ||
        (uint32_t)hgt > info.height || w <= 0 || hgt <= 0)
        return JNI_FALSE;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
        return JNI_FALSE;
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, hgt, FPDFBitmap_BGRA, pixels, (int)info.stride);
    bool ok = false;
    if (bmp) {
        FPDF_PAGE page = d->Page(pageIndex);
        FPDFBitmap_FillRect(bmp, 0, 0, w, hgt, page ? 0xFFFFFFFF : 0xFFE0E0E0);
        if (page) {
            // FPDF_REVERSE_BYTE_ORDER: PDFium writes RGBA, Android's layout.
            FPDF_RenderPageBitmap(bmp, page, -x, -y, pageW, pageH, rotate & 3,
                                  FPDF_ANNOT | FPDF_REVERSE_BYTE_ORDER |
                                      FPDF_RENDER_LIMITEDIMAGECACHE);
        }
        FPDFBitmap_Destroy(bmp);
        ApplyColors(static_cast<uint8_t*>(pixels), w, hgt, (int)info.stride, colors);
        ok = true;
    }
    AndroidBitmap_unlockPixels(env, bitmap);
    return ok ? JNI_TRUE : JNI_FALSE;
}

// Text layer + links of one page, returned as a PageText object.
JNI_FN(jobject, nPageText)(JNIEnv* env, jclass, jlong h, jint pageIndex) {
    Document* d = Doc(h);
    std::vector<jint> cps;
    std::vector<float> boxes;
    std::vector<float> linkRects, linkYs;
    std::vector<jint> linkPages;
    std::vector<std::u16string> linkUris;

    FPDF_PAGE page = d->Page(pageIndex);
    if (page) {
        const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
        const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);
        float rc[4];
        auto addLink = [&](const float* r, const Target& t) {
            linkRects.insert(linkRects.end(), r, r + 4);
            linkPages.push_back(t.page);
            linkYs.push_back(t.y);
            linkUris.push_back(t.uri);
        };
        int pos = 0;
        FPDF_LINK link = nullptr;
        while (linkPages.size() < 4000 && FPDFLink_Enumerate(page, &pos, &link)) {
            FS_RECTF r;
            if (!FPDFLink_GetAnnotRect(link, &r)) continue;
            Target t;
            if (FPDF_DEST dest = FPDFLink_GetDest(d->doc, link))
                ReadDest(d, dest, t);
            else if (FPDF_ACTION action = FPDFLink_GetAction(link))
                ReadAction(d, action, t);
            if (t.page < 0 && t.uri.empty()) continue;
            ToDisplay(page, sx, sy, r.left, r.top, r.right, r.bottom, rc);
            addLink(rc, t);
        }
        if (FPDF_TEXTPAGE text = FPDFText_LoadPage(page)) {
            const int n = FPDFText_CountChars(text);
            cps.resize(n > 0 ? (size_t)n : 0);
            boxes.assign(cps.size() * 4, NAN);
            for (int i = 0; i < n; ++i) {
                cps[(size_t)i] = (jint)FPDFText_GetUnicode(text, i);
                FS_RECTF r;
                if (FPDFText_IsGenerated(text, i) != 1 && FPDFText_GetLooseCharBox(text, i, &r)) {
                    ToDisplay(page, sx, sy, r.left, r.top, r.right, r.bottom, rc);
                    if (rc[2] > rc[0] && rc[3] > rc[1])
                        std::copy(rc, rc + 4, boxes.begin() + (long)i * 4);
                }
            }
            if (FPDF_PAGELINK web = FPDFLink_LoadWebLinks(text)) {
                const int count = FPDFLink_CountWebLinks(web);
                for (int i = 0; i < count && linkPages.size() < 4000; ++i) {
                    const int len = FPDFLink_GetURL(web, i, nullptr, 0);
                    if (len <= 1 || len > 8192) continue;
                    std::vector<unsigned short> buf((size_t)len);
                    FPDFLink_GetURL(web, i, buf.data(), len);
                    Target t;
                    t.uri.assign(buf.begin(), buf.end() - 1);
                    const int rects = FPDFLink_CountRects(web, i);
                    for (int k = 0; k < rects; ++k) {
                        double l, tp, r, b;
                        if (!FPDFLink_GetRect(web, i, k, &l, &tp, &r, &b)) continue;
                        ToDisplay(page, sx, sy, l, tp, r, b, rc);
                        addLink(rc, t);
                    }
                }
                FPDFLink_CloseWebLinks(web);
            }
            FPDFText_ClosePage(text);
        }
    }

    jintArray jcps = env->NewIntArray((jsize)cps.size());
    env->SetIntArrayRegion(jcps, 0, (jsize)cps.size(), cps.data());
    jfloatArray jboxes = env->NewFloatArray((jsize)boxes.size());
    env->SetFloatArrayRegion(jboxes, 0, (jsize)boxes.size(), boxes.data());
    jfloatArray jlr = env->NewFloatArray((jsize)linkRects.size());
    env->SetFloatArrayRegion(jlr, 0, (jsize)linkRects.size(), linkRects.data());
    jintArray jlp = env->NewIntArray((jsize)linkPages.size());
    env->SetIntArrayRegion(jlp, 0, (jsize)linkPages.size(), linkPages.data());
    jfloatArray jly = env->NewFloatArray((jsize)linkYs.size());
    env->SetFloatArrayRegion(jly, 0, (jsize)linkYs.size(), linkYs.data());
    jobjectArray jlu = NewStringArray(env, (jsize)linkUris.size());
    for (size_t i = 0; i < linkUris.size(); ++i) {
        if (linkUris[i].empty()) continue;
        jstring s = ToJString(env, reinterpret_cast<const unsigned short*>(linkUris[i].data()),
                              linkUris[i].size());
        env->SetObjectArrayElement(jlu, (jsize)i, s);
        env->DeleteLocalRef(s);
    }
    jclass cls = env->FindClass("com/featherpdf/viewer/PageText");
    jmethodID ctor = env->GetMethodID(cls, "<init>", "([I[F[F[I[F[Ljava/lang/String;)V");
    return env->NewObject(cls, ctor, jcps, jboxes, jlr, jlp, jly, jlu);
}

// Search one page. Returns [hitIndex, left, top, right, bottom] per rect.
JNI_FN(jfloatArray, nSearch)(JNIEnv* env, jclass, jlong h, jint pageIndex, jstring query,
                              jboolean matchCase) {
    Document* d = Doc(h);
    std::vector<float> out;
    const jsize qlen = env->GetStringLength(query);
    std::vector<unsigned short> q((size_t)qlen + 1, 0);
    env->GetStringRegion(query, 0, qlen, reinterpret_cast<jchar*>(q.data()));

    // Do not disturb the parsed-page cache used for rendering.
    FPDF_PAGE page = nullptr;
    bool owned = false;
    for (auto& p : d->pages)
        if (p.first == pageIndex) page = p.second;
    if (!page) {
        page = FPDF_LoadPage(d->doc, pageIndex);
        owned = true;
    }
    if (page && qlen > 0) {
        if (FPDF_TEXTPAGE text = FPDFText_LoadPage(page)) {
            const int sx = (int)std::lround(FPDF_GetPageWidthF(page) * 100);
            const int sy = (int)std::lround(FPDF_GetPageHeightF(page) * 100);
            FPDF_SCHHANDLE sch = FPDFText_FindStart(text, q.data(), matchCase ? FPDF_MATCHCASE : 0, 0);
            int hit = 0;
            while (sch && hit < 5000 && FPDFText_FindNext(sch)) {
                const int start = FPDFText_GetSchResultIndex(sch);
                const int count = FPDFText_GetSchCount(sch);
                const int nrects = FPDFText_CountRects(text, start, count);
                for (int r = 0; r < nrects; ++r) {
                    double l, t, rr, b;
                    if (!FPDFText_GetRect(text, r, &l, &t, &rr, &b)) continue;
                    float rc[4];
                    ToDisplay(page, sx, sy, l, t, rr, b, rc);
                    out.push_back((float)hit);
                    out.insert(out.end(), rc, rc + 4);
                }
                ++hit;
            }
            if (sch) FPDFText_FindClose(sch);
            FPDFText_ClosePage(text);
        }
    }
    if (owned && page) FPDF_ClosePage(page);
    jfloatArray arr = env->NewFloatArray((jsize)out.size());
    env->SetFloatArrayRegion(arr, 0, (jsize)out.size(), out.data());
    return arr;
}

// Plain text between two caret positions (pages joined by line breaks).
JNI_FN(jstring, nExtractText)(JNIEnv* env, jclass, jlong h, jint p0, jint i0, jint p1, jint i1) {
    Document* d = Doc(h);
    std::vector<unsigned short> out;
    for (int p = std::max(0, (int)p0); p <= p1 && p < d->pageCount && out.size() < (32u << 20); ++p) {
        FPDF_PAGE page = FPDF_LoadPage(d->doc, p);
        if (!page) continue;
        if (FPDF_TEXTPAGE text = FPDFText_LoadPage(page)) {
            const int n = FPDFText_CountChars(text);
            const int s = p == p0 ? std::min((int)i0, n) : 0;
            const int e = p == p1 ? std::min((int)i1, n) : n;
            if (e > s) {
                std::vector<unsigned short> buf((size_t)(e - s) + 1);
                const int written = FPDFText_GetText(text, s, e - s, buf.data());
                if (written > 1) {
                    if (!out.empty() && out.back() != '\n') {
                        out.push_back('\n');
                    }
                    out.insert(out.end(), buf.begin(), buf.begin() + (written - 1));
                }
            }
            FPDFText_ClosePage(text);
        }
        FPDF_ClosePage(page);
    }
    return ToJString(env, out.data(), out.size());
}

// Bookmarks, flattened depth-first, as an OutlineData object.
JNI_FN(jobject, nOutline)(JNIEnv* env, jclass, jlong h) {
    Document* d = Doc(h);
    std::vector<std::u16string> titles, uris;
    std::vector<jint> levels, pages;
    std::vector<float> ys;
    std::vector<FPDF_BOOKMARK> seen;
    std::vector<std::pair<FPDF_BOOKMARK, int>> stack;
    if (FPDF_BOOKMARK first = FPDFBookmark_GetFirstChild(d->doc, nullptr)) stack.push_back({first, 0});
    while (!stack.empty() && titles.size() < 20000) {
        auto [bm, level] = stack.back();
        stack.pop_back();
        if (std::find(seen.begin(), seen.end(), bm) != seen.end()) continue;  // cycles
        seen.push_back(bm);
        std::u16string title;
        const unsigned long bytes = FPDFBookmark_GetTitle(bm, nullptr, 0);
        if (bytes > 2 && bytes < 4096) {
            std::vector<unsigned short> buf(bytes / 2);
            FPDFBookmark_GetTitle(bm, buf.data(), bytes);
            title.assign(buf.begin(), buf.end() - 1);
        }
        Target t;
        if (FPDF_DEST dest = FPDFBookmark_GetDest(d->doc, bm))
            ReadDest(d, dest, t);
        else if (FPDF_ACTION action = FPDFBookmark_GetAction(bm))
            ReadAction(d, action, t);
        titles.push_back(title);
        levels.push_back(level);
        pages.push_back(t.page);
        ys.push_back(t.y);
        uris.push_back(t.uri);
        if (FPDF_BOOKMARK next = FPDFBookmark_GetNextSibling(d->doc, bm)) stack.push_back({next, level});
        if (level < 31)
            if (FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(d->doc, bm))
                stack.push_back({child, level + 1});
    }
    const jsize n = (jsize)titles.size();
    jobjectArray jt = NewStringArray(env, n), ju = NewStringArray(env, n);
    for (jsize i = 0; i < n; ++i) {
        jstring s = ToJString(env, reinterpret_cast<const unsigned short*>(titles[(size_t)i].data()),
                              titles[(size_t)i].size());
        env->SetObjectArrayElement(jt, i, s);
        env->DeleteLocalRef(s);
        if (!uris[(size_t)i].empty()) {
            jstring u = ToJString(env, reinterpret_cast<const unsigned short*>(uris[(size_t)i].data()),
                                  uris[(size_t)i].size());
            env->SetObjectArrayElement(ju, i, u);
            env->DeleteLocalRef(u);
        }
    }
    jintArray jl = env->NewIntArray(n), jp = env->NewIntArray(n);
    env->SetIntArrayRegion(jl, 0, n, levels.data());
    env->SetIntArrayRegion(jp, 0, n, pages.data());
    jfloatArray jy = env->NewFloatArray(n);
    env->SetFloatArrayRegion(jy, 0, n, ys.data());
    jclass cls = env->FindClass("com/featherpdf/viewer/OutlineData");
    jmethodID ctor = env->GetMethodID(cls, "<init>", "([Ljava/lang/String;[I[I[F[Ljava/lang/String;)V");
    return env->NewObject(cls, ctor, jt, jl, jp, jy, ju);
}

// [title, author, subject, keywords, creator, producer, created, modified,
//  version (e.g. "17"), encrypted ("1"/"0")]
JNI_FN(jobjectArray, nMetadata)(JNIEnv* env, jclass, jlong h) {
    Document* d = Doc(h);
    static const char* kTags[] = {"Title", "Author", "Subject", "Keywords",
                                  "Creator", "Producer", "CreationDate", "ModDate"};
    jobjectArray out = NewStringArray(env, 10);
    for (int i = 0; i < 8; ++i) {
        const unsigned long bytes = FPDF_GetMetaText(d->doc, kTags[i], nullptr, 0);
        std::vector<unsigned short> buf(bytes > 2 && bytes < 65536 ? bytes / 2 : 1, 0);
        if (bytes > 2 && bytes < 65536) FPDF_GetMetaText(d->doc, kTags[i], buf.data(), bytes);
        jstring s = ToJString(env, buf.data(), buf.size() - 1);
        env->SetObjectArrayElement(out, i, s);
        env->DeleteLocalRef(s);
    }
    int version = 0;
    FPDF_GetFileVersion(d->doc, &version);
    std::string v = std::to_string(version);
    std::string enc = FPDF_GetSecurityHandlerRevision(d->doc) != -1 ? "1" : "0";
    env->SetObjectArrayElement(out, 8, env->NewStringUTF(v.c_str()));
    env->SetObjectArrayElement(out, 9, env->NewStringUTF(enc.c_str()));
    return out;
}
