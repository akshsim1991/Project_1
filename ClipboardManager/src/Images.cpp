// Images.cpp - PNG encoding and decoding with GDI+.
#include "Images.h"

#include "Store.h"

using namespace Gdiplus;

namespace {
bool PngEncoder(CLSID& clsid) {
    UINT count = 0, size = 0;
    if (GetImageEncodersSize(&count, &size) != Ok || !size) return false;
    std::vector<BYTE> buf(size);
    auto* codecs = (ImageCodecInfo*)buf.data();
    if (GetImageEncoders(count, size, codecs) != Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    return false;
}

bool IsOpaque(Bitmap& bmp) {
    const UINT w = bmp.GetWidth(), h = bmp.GetHeight();
    Rect r(0, 0, (INT)w, (INT)h);
    BitmapData d{};
    if (bmp.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &d) != Ok) return true;
    bool opaque = true;
    for (UINT y = 0; y < h && opaque; ++y) {
        const BYTE* row = (const BYTE*)d.Scan0 + (size_t)y * (size_t)d.Stride;
        for (UINT x = 0; x < w; ++x)
            if (row[x * 4 + 3] != 255) {
                opaque = false;
                break;
            }
    }
    bmp.UnlockBits(&d);
    return opaque;
}
}  // namespace

std::unique_ptr<Bitmap> DecodeImage(const std::string& bytes) {
    if (bytes.empty()) return nullptr;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!mem) return nullptr;
    void* p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        return nullptr;
    }
    memcpy(p, bytes.data(), bytes.size());
    GlobalUnlock(mem);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
        GlobalFree(mem);
        return nullptr;
    }
    std::unique_ptr<Bitmap> result;
    {
        Bitmap src(stream, FALSE);
        const UINT w = src.GetWidth(), h = src.GetHeight();
        if (src.GetLastStatus() == Ok && w && h) {
            // Copy into a bitmap that owns its pixels: the stream goes away.
            auto copy = std::make_unique<Bitmap>((INT)w, (INT)h, PixelFormat32bppARGB);
            if (copy->GetLastStatus() == Ok) {
                Graphics g(copy.get());
                g.SetCompositingMode(CompositingModeSourceCopy);
                g.SetInterpolationMode(InterpolationModeNearestNeighbor);
                g.SetPixelOffsetMode(PixelOffsetModeHalf);
                g.DrawImage(&src, Rect(0, 0, (INT)w, (INT)h), 0, 0, (INT)w, (INT)h, UnitPixel);
                result = std::move(copy);
            }
        }
    }
    stream->Release();
    return result;
}

bool EncodePng(Bitmap& bmp, std::string& out) {
    CLSID clsid;
    if (!PngEncoder(clsid)) return false;
    std::unique_ptr<Bitmap> rgb;
    Bitmap* src = &bmp;
    if (IsOpaque(bmp)) {
        rgb.reset(bmp.Clone(0, 0, (INT)bmp.GetWidth(), (INT)bmp.GetHeight(), PixelFormat24bppRGB));
        if (rgb && rgb->GetLastStatus() == Ok) src = rgb.get();
    }
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
    bool ok = src->Save(stream, &clsid, nullptr) == Ok;
    if (ok) {
        HGLOBAL mem = nullptr;
        GetHGlobalFromStream(stream, &mem);
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        const void* p = mem ? GlobalLock(mem) : nullptr;
        ok = p != nullptr;
        if (p) {
            out.assign((const char*)p, (size_t)st.cbSize.QuadPart);
            GlobalUnlock(mem);
        }
    }
    stream->Release();
    return ok && !out.empty();
}

uint64_t PixelHash(Bitmap& bmp) {
    const UINT w = bmp.GetWidth(), h = bmp.GetHeight();
    uint64_t hash = Store::Hash(&w, sizeof(w));
    hash = Store::Hash(&h, sizeof(h), hash);
    Rect r(0, 0, (INT)w, (INT)h);
    BitmapData d{};
    if (bmp.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &d) != Ok) return hash;
    for (UINT y = 0; y < h; ++y) hash = Store::Hash((const BYTE*)d.Scan0 + (size_t)y * (size_t)d.Stride, (size_t)w * 4, hash);
    bmp.UnlockBits(&d);
    return hash;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    out.assign((size_t)size.QuadPart, '\0');
    size_t done = 0;
    bool ok = true;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = (DWORD)std::min<size_t>(out.size() - done, 1u << 30);
        if (!ReadFile(f, out.data() + done, want, &got, nullptr) || !got) {
            ok = false;
            break;
        }
        done += got;
    }
    CloseHandle(f);
    out.resize(done);
    return ok;
}
