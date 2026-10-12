// ImageFile.h - pictures from files (PNG, JPEG, BMP, GIF, TIFF) through GDI+.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Its size in pixels. False if it cannot be read.
bool ImageFileSize(const std::wstring& path, int& width, int& height);
// 8-bit RGB, top row first (very large pictures are made smaller).
bool ReadImageRgb(const std::wstring& path, int& width, int& height, std::vector<uint8_t>& rgb);
// 32-bit BGRA with alpha (for the screen), top row first.
bool ReadImageBgra(const std::wstring& path, int& width, int& height, std::vector<uint8_t>& bgra);
