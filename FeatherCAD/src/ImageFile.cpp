// ImageFile.cpp - pictures from files through GDI+ (see ImageFile.h).
#include "ImageFile.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>

#include "GdiPlusInc.h"

namespace {
constexpr int kMaxSide = 4096;  // larger pictures are scaled down (screen and print need no more)

// The picture as 32-bit BGRA, at most kMaxSide pixels a side.
bool Load(const std::wstring& path, int& w, int& h, std::vector<uint8_t>& bgra) {
    std::unique_ptr<Gdiplus::Bitmap> src(Gdiplus::Bitmap::FromFile(path.c_str(), FALSE));
    if (!src || src->GetLastStatus() != Gdiplus::Ok || src->GetWidth() == 0 || src->GetHeight() == 0) return false;
    w = (int)src->GetWidth();
    h = (int)src->GetHeight();
    const double f = std::min(1.0, (double)kMaxSide / std::max(w, h));
    w = std::max(1, (int)(w * f));
    h = std::max(1, (int)(h * f));
    Gdiplus::Bitmap dst(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&dst);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.Clear(Gdiplus::Color(0, 255, 255, 255));
        g.DrawImage(src.get(), 0, 0, w, h);
    }
    Gdiplus::Rect r(0, 0, w, h);
    Gdiplus::BitmapData bd{};
    if (dst.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return false;
    bgra.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) memcpy(&bgra[(size_t)y * w * 4], (const uint8_t*)bd.Scan0 + (ptrdiff_t)y * bd.Stride, (size_t)w * 4);
    dst.UnlockBits(&bd);
    return true;
}
}  // namespace

bool ImageFileSize(const std::wstring& path, int& width, int& height) {
    std::unique_ptr<Gdiplus::Bitmap> b(Gdiplus::Bitmap::FromFile(path.c_str(), FALSE));
    if (!b || b->GetLastStatus() != Gdiplus::Ok) return false;
    width = (int)b->GetWidth();
    height = (int)b->GetHeight();
    return width > 0 && height > 0;
}

bool ReadImageBgra(const std::wstring& path, int& width, int& height, std::vector<uint8_t>& bgra) {
    return Load(path, width, height, bgra);
}

bool ReadImageRgb(const std::wstring& path, int& width, int& height, std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> bgra;
    if (!Load(path, width, height, bgra)) return false;
    rgb.resize((size_t)width * height * 3);
    for (size_t i = 0, n = (size_t)width * height; i < n; ++i) {
        const uint8_t a = bgra[i * 4 + 3];  // transparent parts on white paper
        auto mix = [a](uint8_t c) { return (uint8_t)((c * a + 255 * (255 - a)) / 255); };
        rgb[i * 3] = mix(bgra[i * 4 + 2]);
        rgb[i * 3 + 1] = mix(bgra[i * 4 + 1]);
        rgb[i * 3 + 2] = mix(bgra[i * 4]);
    }
    return true;
}
