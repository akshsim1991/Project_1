// Export.h - writing pages as pictures (PNG, JPEG), plain text or Markdown.
// Called on the render worker thread; nothing here touches PDFium.
#pragma once
#include "RenderTypes.h"

// Saves a rendered page; `dpi` is recorded in the file. Written under a
// temporary name and renamed when complete.
bool SavePicture(const PixelBuffer& px, const std::wstring& path, bool jpeg, int dpi);

// Encodes 32-bit BGRx pixels as a JPEG file in memory (quality 1-100).
bool EncodeJpeg(const uint8_t* bits, int w, int h, int stride, int quality, std::string& out);

// Writes `text` as UTF-8 (with a byte order mark when `bom`), CRLF line
// endings, through a temporary file.
bool WriteTextFile(const std::wstring& path, const std::wstring& text, bool bom);

// Plain text of a page as PDFium gives it, tidied: lines end in CRLF,
// trailing spaces removed.
std::wstring TidyText(const std::wstring& text);

// Markdown from the lines of every exported page: larger type becomes
// headings, bullets become lists, wrapped lines are joined into paragraphs.
std::wstring LinesToMarkdown(const std::vector<std::vector<TextLine>>& pages);
