// OcrSmoke.cpp - checks text recognition end to end on a real Windows PC:
// draws a sentence into a picture, recognises it with OcrRunner (the code
// Feather PDF uses) and expects the words back with sensible positions.
//
// Exit code 0: the words were recognised, or this PC has no recognition
// language installed (reported as a warning); 1: anything else.
#include <cstdio>

#include "Ocr.h"

// PixelBuffer lives in PdfEngine.cpp, which this test does not need.
bool PixelBuffer::Allocate(int w, int h) {
    Free();
    bits = (uint8_t*)VirtualAlloc(nullptr, (size_t)w * h * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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

int wmain() {
    const wchar_t kText[] = L"Feather PDF reads scanned pages 2026";
    const int w = 1700, h = 220;

    // The sentence in black on white, at 2 pixels per point.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* dib = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &dib, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT rc{0, 0, w, h};
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HFONT font = CreateFontW(-64, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, 60, 70, kText, (int)wcslen(kText));
    GdiFlush();

    auto* img = new OcrImage;
    img->jobId = 7;
    img->page = 0;
    img->scale = 2;
    if (!img->pixels.Allocate(w, h)) return 1;
    memcpy(img->pixels.bits, dib, (size_t)w * h * 4);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);

    HWND wnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    OcrRunner ocr;
    ocr.Recognise(wnd, img);
    SetTimer(nullptr, 0, 60000, nullptr);

    int result = 1;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER) {
            wprintf(L"FAIL: no answer within 60 seconds\n");
            break;
        }
        if (msg.message != WM_APP_OCR_DONE) {
            DispatchMessageW(&msg);
            continue;
        }
        auto* res = reinterpret_cast<OcrResult*>(msg.lParam);
        if (!res->ok && res->fatal) {
            wprintf(L"::warning::Text recognition is not available on this PC: %ls\n", res->error.c_str());
            result = 0;
        } else if (!res->ok) {
            wprintf(L"FAIL: %ls\n", res->error.c_str());
        } else {
            std::wstring line;
            for (const OcrWord& word : res->words) {
                wprintf(L"  '%ls' at %.1f,%.1f - %.1f,%.1f pt%ls\n", word.text.c_str(), word.rect.left, word.rect.top,
                        word.rect.right, word.rect.bottom, word.lineEnd ? L" (end of line)" : L"");
                line += (line.empty() ? L"" : L" ") + word.text;
            }
            wprintf(L"Recognised: %ls\n", line.c_str());
            // Positions are in points: the text starts 30 pt from the left
            // and sits roughly between 35 and 70 pt from the top.
            const bool placed = !res->words.empty() && res->words[0].rect.left > 20 && res->words[0].rect.left < 40 &&
                                res->words[0].rect.top > 25 && res->words[0].rect.bottom < 80;
            if (line == kText && placed && res->words.back().lineEnd) {
                wprintf(L"PASS\n");
                result = 0;
            } else {
                wprintf(L"FAIL: expected '%ls' with positions in points\n", kText);
            }
        }
        delete res;
        break;
    }
    ocr.Stop();
    return result;
}
