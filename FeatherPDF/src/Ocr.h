// Ocr.h - text recognition with the engine built into Windows 10 and 11
// (Windows.Media.Ocr), on a thread of its own.
//
// The render worker renders each page as a picture (WM_APP_OCR_IMAGE); the
// main window hands it to Recognise(), and the words found come back as
// WM_APP_OCR_DONE. Recognition uses the languages of the user's Windows
// profile; Windows downloads them with the language's "Optical character
// recognition" feature. No other software is needed.
#pragma once
#include <deque>

#include "RenderTypes.h"

class OcrRunner {
public:
    OcrRunner() = default;
    ~OcrRunner();
    OcrRunner(const OcrRunner&) = delete;
    OcrRunner& operator=(const OcrRunner&) = delete;

    // Recognises the words of `image` (taken over); the result is posted to
    // `notify` as an OcrResult*.
    void Recognise(HWND notify, OcrImage* image);
    // Drops the pages of job `jobId` still waiting.
    void Cancel(uint32_t jobId);
    void Stop();

private:
    static DWORD WINAPI ThreadProc(LPVOID self);
    void Run();

    HANDLE m_thread = nullptr;
    HWND m_notify = nullptr;
    SRWLOCK m_lock = SRWLOCK_INIT;
    CONDITION_VARIABLE m_cv = CONDITION_VARIABLE_INIT;
    std::deque<OcrImage*> m_queue;
    bool m_quit = false;
};
