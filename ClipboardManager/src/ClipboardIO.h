// ClipboardIO.h - reading what was copied, and putting items back on the
// clipboard (text, pictures and copied files).
#pragma once
#include "Common.h"

struct Capture {
    enum Kind { None, Text, Image, Files } kind = None;
    std::wstring text;      // text, or one file path per line
    std::string png;        // pictures, as PNG
    uint64_t pixelHash = 0; // pictures: to recognise the same picture again
    int width = 0, height = 0;
    std::wstring source;    // the program it was copied from
    bool excluded = false;  // a password manager asked not to be recorded
};

namespace ClipboardIO {
enum class ReadResult { Ok, Busy, Nothing };

// Reads the clipboard. Busy: another program has it open (try again soon).
ReadResult Read(HWND owner, bool images, bool files, Capture& out);

bool WriteText(HWND owner, const std::wstring& text);
// A picture stored as PNG: put on the clipboard both as a bitmap (for every
// program) and as PNG (keeps transparency in programs that read it).
bool WritePng(HWND owner, const std::string& png);
// Copied files: Explorer can paste them, and text editors get the paths.
bool WriteFiles(HWND owner, const std::vector<std::wstring>& paths);

// Presses Ctrl+V in the window that has the keyboard.
void SendPaste();

// The name of the program that owns a window ("Notepad", "Google Chrome").
std::wstring ProgramName(HWND hwnd);
}  // namespace ClipboardIO
