// Images.h - PNG encoding and decoding with GDI+.
#pragma once
#include <memory>

#include "GdiPlusInc.h"

// Decodes PNG (or any picture GDI+ reads) into a 32-bit bitmap that owns its
// pixels. nullptr if the data is not a picture.
std::unique_ptr<Gdiplus::Bitmap> DecodeImage(const std::string& bytes);

// Encodes as PNG. Opaque pictures are stored without an alpha channel,
// which makes the files smaller.
bool EncodePng(Gdiplus::Bitmap& bmp, std::string& out);

// A hash of the pixels, to recognise the same picture copied again.
uint64_t PixelHash(Gdiplus::Bitmap& bmp);

bool ReadFileBytes(const std::wstring& path, std::string& out);
