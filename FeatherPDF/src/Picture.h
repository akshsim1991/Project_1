// Picture.h - pictures for signatures and "Insert picture" (GDI+, part of
// Windows): loading image files, the saved signature, and the signature
// dialog with its drawing pad.
#pragma once
#include "Common.h"

struct Picture {
    std::vector<uint8_t> pixels;  // BGRA, top-down, not premultiplied
    int width = 0, height = 0;
    bool Empty() const { return width <= 0 || height <= 0; }
};

// PNG, JPEG, BMP, GIF or TIFF, scaled down to at most `maxSide` pixels.
bool LoadPictureFile(const std::wstring& path, int maxSide, Picture& out);

// The signature remembered between sessions (%APPDATA%\FeatherPDF).
bool LoadSavedSignature(Picture& out);
bool SaveSignature(const Picture& sig);
void DeleteSavedSignature();

// Draw, type or pick a signature. Returns false if cancelled.
bool ShowSignatureDialog(HINSTANCE inst, HWND owner, Picture& out);
