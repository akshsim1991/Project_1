// Ocr.cpp - Windows.Media.Ocr through its raw COM interfaces.
//
// The Windows Runtime is used without C++/WinRT or the SDK's projection
// headers, so the same code builds with MSVC and MinGW: the few interfaces
// needed are declared below exactly as in the Windows SDK (method order is
// the binary contract), and combase.dll is loaded at run time, so the
// program still starts on systems without it.
#include "Ocr.h"

#include <inspectable.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <type_traits>

namespace {
// --- Windows Runtime interfaces (declared as in the Windows SDK) -----------
struct OcrRect {
    float x, y, width, height;
};

struct IOcrWordAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_BoundingRect(OcrRect* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) = 0;
};

template <typename T>
struct IVectorViewAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, T** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(T* item, UINT32* index, boolean* found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, T** items, UINT32* actual) = 0;
};

struct IOcrLineAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Words(IVectorViewAbi<IOcrWordAbi>** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) = 0;
};

struct IOcrResultAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Lines(IVectorViewAbi<IOcrLineAbi>** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TextAngle(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) = 0;
};

// IAsyncOperation<OcrResult>
struct IAsyncOcrAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IOcrResultAbi** results) = 0;
};

struct IAsyncInfoAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* code) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
enum { kAsyncStarted = 0, kAsyncCompleted = 1 };

struct PlaneDescription {
    INT32 startIndex, width, height, stride;
};

struct IBitmapBufferAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetPlaneCount(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaneDescription(INT32 index, PlaneDescription* value) = 0;
};

struct ISoftwareBitmapAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_BitmapPixelFormat(int* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BitmapAlphaMode(int* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PixelWidth(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PixelHeight(INT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsReadOnly(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DpiX(double value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DpiX(double* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DpiY(double value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DpiY(double* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE LockBuffer(int mode, IBitmapBufferAbi** value) = 0;
};
enum { kPixelBgra8 = 87, kAlphaPremultiplied = 0, kBufferWrite = 2 };

struct ISoftwareBitmapFactoryAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(int format, INT32 width, INT32 height,
                                             ISoftwareBitmapAbi** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWithAlpha(int format, INT32 width, INT32 height, int alpha,
                                                      ISoftwareBitmapAbi** value) = 0;
};

struct IOcrEngineAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RecognizeAsync(ISoftwareBitmapAbi* bitmap, IAsyncOcrAbi** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RecognizerLanguage(IInspectable** value) = 0;
};

struct IOcrEngineStaticsAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MaxImageDimension(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AvailableRecognizerLanguages(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsLanguageSupported(IInspectable* language, boolean* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromLanguage(IInspectable* language, IOcrEngineAbi** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromUserProfileLanguages(IOcrEngineAbi** result) = 0;
};

struct IMemoryBufferAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateReference(IInspectable** reference) = 0;
};

struct IMemoryBufferByteAccessAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

struct IClosableAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

const GUID kIidOcrEngineStatics = {0x5bffa85a, 0x3384, 0x3540, {0x99, 0x40, 0x69, 0x91, 0x20, 0xd4, 0x28, 0xa8}};
const GUID kIidSoftwareBitmapFactory = {0xc99feb69, 0x2d62, 0x4d47, {0xa6, 0xb3, 0x4f, 0xdb, 0x6a, 0x07, 0xfd, 0xf8}};
const GUID kIidMemoryBuffer = {0xfbc4dd2a, 0x245b, 0x11e4, {0xaf, 0x98, 0x68, 0x94, 0x23, 0x26, 0x0c, 0xf8}};
const GUID kIidMemoryBufferByteAccess = {0x5b0d3235, 0x4dba, 0x4d44, {0x86, 0x5e, 0x8f, 0x1d, 0x0e, 0x4f, 0xd0, 0x4d}};
const GUID kIidClosable = {0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e}};
const GUID kIidAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// A minimal owning COM pointer.
template <typename T>
class Ref {
public:
    Ref() = default;
    ~Ref() { Reset(); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    T** Put() {
        Reset();
        return &m_p;
    }
    void** PutVoid() { return reinterpret_cast<void**>(Put()); }
    T* Get() const { return m_p; }
    T* operator->() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }
    void Reset() {
        if (m_p) m_p->Release();
        m_p = nullptr;
    }

private:
    T* m_p = nullptr;
};

// combase.dll, loaded at run time.
struct Combase {
    HMODULE dll = nullptr;
    HRESULT(WINAPI* roInitialize)(int) = nullptr;
    void(WINAPI* roUninitialize)() = nullptr;
    HRESULT(WINAPI* getFactory)(HSTRING, REFIID, void**) = nullptr;
    HRESULT(WINAPI* createString)(LPCWSTR, UINT32, HSTRING*) = nullptr;
    HRESULT(WINAPI* deleteString)(HSTRING) = nullptr;
    LPCWSTR(WINAPI* rawBuffer)(HSTRING, UINT32*) = nullptr;

    bool Load() {
        dll = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) return false;
        auto get = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
                reinterpret_cast<void*>(GetProcAddress(dll, name)));
            return fn != nullptr;
        };
        return get(roInitialize, "RoInitialize") && get(roUninitialize, "RoUninitialize") &&
               get(getFactory, "RoGetActivationFactory") && get(createString, "WindowsCreateString") &&
               get(deleteString, "WindowsDeleteString") && get(rawBuffer, "WindowsGetStringRawBuffer");
    }
    ~Combase() {
        if (dll) FreeLibrary(dll);
    }
    template <typename T>
    HRESULT Factory(const wchar_t* cls, const GUID& iid, Ref<T>& out) {
        HSTRING name = nullptr;
        HRESULT hr = createString(cls, (UINT32)wcslen(cls), &name);
        if (FAILED(hr)) return hr;
        hr = getFactory(name, iid, out.PutVoid());
        deleteString(name);
        return hr;
    }
    std::wstring Take(HSTRING s) {
        std::wstring out;
        if (!s) return out;
        UINT32 len = 0;
        if (LPCWSTR p = rawBuffer(s, &len)) out.assign(p, len);
        deleteString(s);
        return out;
    }
};

// Box-filters `src` down by `factor` (> 1), so the page fits the largest
// picture the engine accepts.
void Shrink(const PixelBuffer& src, double factor, PixelBuffer& dst) {
    const int w = std::max(1, (int)(src.width / factor)), h = std::max(1, (int)(src.height / factor));
    if (!dst.Allocate(w, h)) return;
    for (int y = 0; y < h; ++y) {
        const int y0 = (int)(y * factor), y1 = std::max(y0 + 1, std::min(src.height, (int)((y + 1) * factor)));
        for (int x = 0; x < w; ++x) {
            const int x0 = (int)(x * factor), x1 = std::max(x0 + 1, std::min(src.width, (int)((x + 1) * factor)));
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t* p = src.bits + ((size_t)yy * src.width + x0) * 4;
                for (int xx = x0; xx < x1; ++xx, p += 4, ++n) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }
            uint8_t* d = dst.bits + ((size_t)y * w + x) * 4;
            d[0] = (uint8_t)(sum[0] / n);
            d[1] = (uint8_t)(sum[1] / n);
            d[2] = (uint8_t)(sum[2] / n);
            d[3] = 0xFF;
        }
    }
}

// The recognition engine of this thread.
class Engine {
public:
    // Empty on success, otherwise why text can not be recognised here.
    std::wstring Init() {
        if (!m_rt.Load())
            return L"Text recognition needs Windows 10 or later.";
        m_rt.roInitialize(1);  // RO_INIT_MULTITHREADED
        m_initialised = true;
        Ref<IOcrEngineStaticsAbi> statics;
        if (FAILED(m_rt.Factory(L"Windows.Media.Ocr.OcrEngine", kIidOcrEngineStatics, statics)) ||
            FAILED(m_rt.Factory(L"Windows.Graphics.Imaging.SoftwareBitmap", kIidSoftwareBitmapFactory, m_bitmaps)))
            return L"Text recognition is not available on this PC. It needs Windows 10 or later.";
        UINT32 maxDim = 0;
        if (SUCCEEDED(statics->get_MaxImageDimension(&maxDim)) && maxDim >= 500) m_maxDim = (int)maxDim;
        if (FAILED(statics->TryCreateFromUserProfileLanguages(m_engine.Put())) || !m_engine)
            return L"Windows has no text recognition installed for your languages.\n\n"
                   L"To add it: open Settings > Time & language > Language & region, choose "
                   L"\x201CLanguage options\x201D next to your language, and install "
                   L"\x201COptical character recognition\x201D. Then try again.";
        return {};
    }

    ~Engine() {
        m_engine.Reset();
        m_bitmaps.Reset();
        if (m_initialised) m_rt.roUninitialize();
    }

    // Recognises the words of `img`; false if the engine failed on it.
    bool Recognise(const OcrImage& img, std::vector<OcrWord>& words, const volatile bool& quit) {
        const PixelBuffer* px = &img.pixels;
        PixelBuffer shrunk;
        double factor = 1;
        const int biggest = std::max(px->width, px->height);
        if (biggest > m_maxDim) {
            factor = (double)biggest / m_maxDim + 1e-6;
            Shrink(*px, factor, shrunk);
            if (!shrunk.bits) return false;
            px = &shrunk;
        }
        Ref<ISoftwareBitmapAbi> bitmap;
        if (FAILED(m_bitmaps->CreateWithAlpha(kPixelBgra8, px->width, px->height, kAlphaPremultiplied,
                                              bitmap.Put())) ||
            !Fill(bitmap.Get(), *px)) {
            shrunk.Free();
            return false;
        }
        shrunk.Free();

        Ref<IAsyncOcrAbi> op;
        Ref<IAsyncInfoAbi> info;
        if (FAILED(m_engine->RecognizeAsync(bitmap.Get(), op.Put())) ||
            FAILED(op->QueryInterface(kIidAsyncInfo, info.PutVoid())))
            return false;
        int status = kAsyncStarted;
        while (SUCCEEDED(info->get_Status(&status)) && status == kAsyncStarted) {
            if (quit) {
                info->Cancel();
                return false;
            }
            Sleep(10);
        }
        Ref<IOcrResultAbi> result;
        if (status != kAsyncCompleted || FAILED(op->GetResults(result.Put())) || !result) return false;
        info->Close();

        // Engine pixels -> page points.
        const double toPoints = factor / std::max(0.01f, img.scale);
        Ref<IVectorViewAbi<IOcrLineAbi>> lines;
        UINT32 lineCount = 0;
        if (FAILED(result->get_Lines(lines.Put())) || !lines || FAILED(lines->get_Size(&lineCount))) return true;
        for (UINT32 i = 0; i < lineCount; ++i) {
            Ref<IOcrLineAbi> line;
            Ref<IVectorViewAbi<IOcrWordAbi>> list;
            UINT32 wordCount = 0;
            if (FAILED(lines->GetAt(i, line.Put())) || FAILED(line->get_Words(list.Put())) || !list ||
                FAILED(list->get_Size(&wordCount)))
                continue;
            const size_t first = words.size();
            for (UINT32 j = 0; j < wordCount; ++j) {
                Ref<IOcrWordAbi> word;
                OcrRect r{};
                HSTRING text = nullptr;
                if (FAILED(list->GetAt(j, word.Put())) || FAILED(word->get_BoundingRect(&r)) ||
                    FAILED(word->get_Text(&text)))
                    continue;
                OcrWord w;
                w.text = m_rt.Take(text);
                w.rect.left = (float)(r.x * toPoints);
                w.rect.top = (float)(r.y * toPoints);
                w.rect.right = (float)((r.x + r.width) * toPoints);
                w.rect.bottom = (float)((r.y + r.height) * toPoints);
                if (!w.text.empty()) words.push_back(std::move(w));
            }
            if (words.size() > first) words.back().lineEnd = true;
        }
        return true;
    }

private:
    // Copies the pixels into the bitmap's own memory.
    bool Fill(ISoftwareBitmapAbi* bitmap, const PixelBuffer& px) {
        Ref<IBitmapBufferAbi> buffer;
        if (FAILED(bitmap->LockBuffer(kBufferWrite, buffer.Put()))) return false;
        bool ok = false;
        PlaneDescription plane{};
        Ref<IMemoryBufferAbi> memory;
        Ref<IInspectable> reference;
        Ref<IMemoryBufferByteAccessAbi> bytes;
        BYTE* data = nullptr;
        UINT32 capacity = 0;
        if (SUCCEEDED(buffer->GetPlaneDescription(0, &plane)) &&
            SUCCEEDED(buffer->QueryInterface(kIidMemoryBuffer, memory.PutVoid())) &&
            SUCCEEDED(memory->CreateReference(reference.Put())) &&
            SUCCEEDED(reference->QueryInterface(kIidMemoryBufferByteAccess, bytes.PutVoid())) &&
            SUCCEEDED(bytes->GetBuffer(&data, &capacity)) && data && plane.stride >= px.width * 4 &&
            (size_t)plane.startIndex + (size_t)plane.stride * px.height <= capacity) {
            for (int y = 0; y < px.height; ++y)
                memcpy(data + plane.startIndex + (size_t)y * plane.stride, px.bits + (size_t)y * px.width * 4,
                       (size_t)px.width * 4);
            ok = true;
        }
        bytes.Reset();
        Ref<IClosableAbi> close;
        if (reference && SUCCEEDED(reference->QueryInterface(kIidClosable, close.PutVoid()))) close->Close();
        if (SUCCEEDED(buffer->QueryInterface(kIidClosable, close.PutVoid()))) close->Close();
        return ok;
    }

    Combase m_rt;
    bool m_initialised = false;
    Ref<ISoftwareBitmapFactoryAbi> m_bitmaps;
    Ref<IOcrEngineAbi> m_engine;
    int m_maxDim = 2600;
};

class LockGuard {
public:
    explicit LockGuard(SRWLOCK& l) : m_l(l) { AcquireSRWLockExclusive(&m_l); }
    ~LockGuard() { ReleaseSRWLockExclusive(&m_l); }

private:
    SRWLOCK& m_l;
};
}  // namespace

OcrRunner::~OcrRunner() { Stop(); }

void OcrRunner::Recognise(HWND notify, OcrImage* image) {
    {
        LockGuard g(m_lock);
        m_notify = notify;
        m_queue.push_back(image);
    }
    if (!m_thread) {
        m_thread = CreateThread(nullptr, 0, &OcrRunner::ThreadProc, this, 0, nullptr);
        if (!m_thread) {
            LockGuard g(m_lock);
            for (OcrImage* img : m_queue) {
                auto* res = new OcrResult;
                res->jobId = img->jobId;
                res->page = img->page;
                res->ok = false;
                res->fatal = true;
                res->error = L"Text recognition could not be started.";
                if (!PostMessageW(notify, WM_APP_OCR_DONE, 0, (LPARAM)res)) delete res;
                delete img;
            }
            m_queue.clear();
            return;
        }
    }
    WakeConditionVariable(&m_cv);
}

void OcrRunner::Cancel(uint32_t jobId) {
    LockGuard g(m_lock);
    for (auto it = m_queue.begin(); it != m_queue.end();) {
        if ((*it)->jobId == jobId) {
            delete *it;
            it = m_queue.erase(it);
        } else {
            ++it;
        }
    }
}

void OcrRunner::Stop() {
    if (!m_thread) return;
    {
        LockGuard g(m_lock);
        m_quit = true;
    }
    WakeConditionVariable(&m_cv);
    WaitForSingleObject(m_thread, 3000);
    CloseHandle(m_thread);
    m_thread = nullptr;
    for (OcrImage* img : m_queue) delete img;
    m_queue.clear();
}

DWORD WINAPI OcrRunner::ThreadProc(LPVOID self) {
    static_cast<OcrRunner*>(self)->Run();
    return 0;
}

void OcrRunner::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    Engine engine;
    const std::wstring unavailable = engine.Init();
    for (;;) {
        OcrImage* img = nullptr;
        HWND notify = nullptr;
        {
            LockGuard g(m_lock);
            while (!m_quit && m_queue.empty()) SleepConditionVariableSRW(&m_cv, &m_lock, INFINITE, 0);
            if (m_quit) break;
            img = m_queue.front();
            m_queue.pop_front();
            notify = m_notify;
        }
        auto* res = new OcrResult;
        res->jobId = img->jobId;
        res->page = img->page;
        if (!unavailable.empty()) {
            res->ok = false;
            res->fatal = true;
            res->error = unavailable;
        } else if (!engine.Recognise(*img, res->words, m_quit)) {
            res->ok = false;
            res->error = L"Text could not be recognised on page " + std::to_wstring(img->page + 1) + L".";
        }
        delete img;
        if (!PostMessageW(notify, WM_APP_OCR_DONE, 0, (LPARAM)res)) delete res;
    }
}
