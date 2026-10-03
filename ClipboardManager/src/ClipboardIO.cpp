// ClipboardIO.cpp - clipboard formats in and out.
#include "ClipboardIO.h"

#include <cwctype>
#include <map>

#include <shellapi.h>
#include <shlobj.h>

#include "Images.h"

using namespace Gdiplus;

namespace {
// Opens the clipboard, trying a few times: right after a change the
// program that copied may still have it open.
bool OpenWithRetry(HWND owner, int attempts) {
    for (int i = 0; i < attempts; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(15);
    }
    return false;
}

struct ClipboardCloser {
    ~ClipboardCloser() { CloseClipboard(); }
};

UINT PngFormat() {
    static const UINT f = RegisterClipboardFormatW(L"PNG");
    return f;
}

// Clipboard data as bytes.
bool DataBytes(UINT format, std::string& out) {
    HANDLE h = GetClipboardData(format);
    if (!h) return false;
    const size_t size = GlobalSize(h);
    const void* p = GlobalLock(h);
    if (!p) return false;
    out.assign((const char*)p, size);
    GlobalUnlock(h);
    return !out.empty();
}

// The real length of PNG data (clipboard memory is often rounded up).
size_t PngLength(const std::string& d) {
    static const char sig[] = "\x89PNG\r\n\x1a\n";
    if (d.size() < 8 || d.compare(0, 8, sig, 8) != 0) return 0;
    size_t at = 8;
    while (at + 12 <= d.size()) {
        const size_t len = ((size_t)(unsigned char)d[at] << 24) | ((size_t)(unsigned char)d[at + 1] << 16) |
                           ((size_t)(unsigned char)d[at + 2] << 8) | (size_t)(unsigned char)d[at + 3];
        const bool end = d.compare(at + 4, 4, "IEND") == 0;
        at += 12 + len;
        if (end) return at <= d.size() ? at : d.size();
    }
    return d.size();
}

// A packed DIB (CF_DIB) as a 32-bit bitmap.
std::unique_ptr<Bitmap> FromDib(const std::string& dib) {
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return nullptr;
    BITMAPINFOHEADER bih;
    memcpy(&bih, dib.data(), sizeof(bih));
    if (bih.biSize < sizeof(BITMAPINFOHEADER) || bih.biSize > dib.size()) return nullptr;
    const int w = bih.biWidth, h = bih.biHeight < 0 ? -bih.biHeight : bih.biHeight;
    const bool topDown = bih.biHeight < 0;
    if (w <= 0 || h <= 0 || w > 100000 || h > 100000) return nullptr;
    if (bih.biCompression == BI_JPEG || bih.biCompression == BI_PNG) return nullptr;
    const size_t colors = bih.biClrUsed ? bih.biClrUsed : (bih.biBitCount <= 8 ? (size_t)1 << bih.biBitCount : 0);
    size_t masks = 0;
    if (bih.biSize == sizeof(BITMAPINFOHEADER)) {
        if (bih.biCompression == BI_BITFIELDS) masks = 12;
        if (bih.biCompression == 6 /*BI_ALPHABITFIELDS*/) masks = 16;
    }
    const size_t offset = bih.biSize + masks + colors * 4;
    if (offset >= dib.size()) return nullptr;
    const BYTE* bits = (const BYTE*)dib.data() + offset;
    const size_t available = dib.size() - offset;

    auto bmp = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
    if (bmp->GetLastStatus() != Ok) return nullptr;
    Rect r(0, 0, w, h);
    BitmapData out{};
    if (bmp->LockBits(&r, ImageLockModeWrite, PixelFormat32bppARGB, &out) != Ok) return nullptr;
    auto outRow = [&](int y) { return (BYTE*)out.Scan0 + (size_t)y * (size_t)out.Stride; };

    bool done = false;
    // 32-bit pixels are copied directly, keeping transparency.
    DWORD rm = 0x00FF0000, gm = 0x0000FF00, bm = 0x000000FF;
    if (masks) {
        memcpy(&rm, dib.data() + bih.biSize, 4);
        memcpy(&gm, dib.data() + bih.biSize + 4, 4);
        memcpy(&bm, dib.data() + bih.biSize + 8, 4);
    } else if (bih.biSize >= sizeof(BITMAPV4HEADER) && bih.biCompression == BI_BITFIELDS) {
        const auto* v4 = (const BITMAPV4HEADER*)dib.data();
        rm = v4->bV4RedMask;
        gm = v4->bV4GreenMask;
        bm = v4->bV4BlueMask;
    }
    const size_t stride32 = (size_t)w * 4;
    if (bih.biBitCount == 32 && (bih.biCompression == BI_RGB || bih.biCompression == BI_BITFIELDS) &&
        rm == 0x00FF0000 && gm == 0x0000FF00 && bm == 0x000000FF && available >= stride32 * (size_t)h) {
        bool anyAlpha = false;
        for (int y = 0; y < h; ++y) {
            const BYTE* src = bits + (size_t)(topDown ? y : h - 1 - y) * stride32;
            memcpy(outRow(y), src, stride32);
            for (int x = 0; x < w && !anyAlpha; ++x) anyAlpha = src[x * 4 + 3] != 0;
        }
        // Most programs leave the alpha bytes at 0: the picture is opaque.
        if (!anyAlpha)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) outRow(y)[x * 4 + 3] = 255;
        done = true;
    }
    if (!done) {
        // Everything else (palettes, 16 and 24 bits, compressed): let GDI
        // convert it into a 32-bit top-down bitmap.
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* dst = nullptr;
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP section = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &dst, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (section && dst) {
            HGDIOBJ old = SelectObject(mem, section);
            const int lines = SetDIBitsToDevice(mem, 0, 0, (DWORD)w, (DWORD)h, 0, 0, 0, (UINT)h, bits,
                                                (const BITMAPINFO*)dib.data(), DIB_RGB_COLORS);
            GdiFlush();
            SelectObject(mem, old);
            if (lines > 0) {
                for (int y = 0; y < h; ++y) {
                    BYTE* row = outRow(y);
                    memcpy(row, (const BYTE*)dst + (size_t)y * stride32, stride32);
                    for (int x = 0; x < w; ++x) row[x * 4 + 3] = 255;
                }
                done = true;
            }
        }
        if (section) DeleteObject(section);
        DeleteDC(mem);
    }
    bmp->UnlockBits(&out);
    if (!done) return nullptr;
    return bmp;
}

bool SetData(UINT format, const void* data, size_t size) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    if (!mem) return false;
    void* p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        return false;
    }
    if (size) memcpy(p, data, size);
    GlobalUnlock(mem);
    if (!SetClipboardData(format, mem)) {
        GlobalFree(mem);
        return false;
    }
    return true;
}

bool SetUnicodeText(const std::wstring& text) {
    return SetData(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
}

bool BeginWrite(HWND owner) {
    if (!OpenWithRetry(owner, 20)) return false;
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }
    return true;
}
}  // namespace

namespace ClipboardIO {
std::wstring ProgramName(HWND hwnd) {
    DWORD pid = 0;
    if (!hwnd || !GetWindowThreadProcessId(hwnd, &pid) || !pid) return L"";
    if (pid == GetCurrentProcessId()) return APP_NAME;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return L"";
    wchar_t path[MAX_PATH * 2];
    DWORD len = MAX_PATH * 2;
    const bool ok = QueryFullProcessImageNameW(proc, 0, path, &len) != 0;
    CloseHandle(proc);
    if (!ok) return L"";

    static std::map<std::wstring, std::wstring> cache;
    auto found = cache.find(path);
    if (found != cache.end()) return found->second;

    // The description from the version information, else the file name.
    std::wstring name;
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size) {
        std::vector<BYTE> info(size);
        if (GetFileVersionInfoW(path, 0, size, info.data())) {
            struct Lang {
                WORD language, codepage;
            }* langs = nullptr;
            UINT bytes = 0;
            if (VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", (void**)&langs, &bytes) &&
                bytes >= sizeof(Lang)) {
                wchar_t key[64];
                swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].language, langs[0].codepage);
                wchar_t* value = nullptr;
                UINT chars = 0;
                if (VerQueryValueW(info.data(), key, (void**)&value, &chars) && value && chars) name = value;
            }
        }
    }
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    if (name.empty()) {
        std::wstring file = path;
        const size_t slash = file.find_last_of(L'\\');
        if (slash != std::wstring::npos) file = file.substr(slash + 1);
        const size_t dot = file.find_last_of(L'.');
        if (dot != std::wstring::npos) file = file.substr(0, dot);
        name = file;
    }
    cache[path] = name;
    return name;
}

ReadResult Read(HWND owner, bool images, bool files, Capture& out) {
    out = Capture();
    if (!OpenWithRetry(owner, 3)) return ReadResult::Busy;
    ClipboardCloser closer;

    // Password managers and other programs mark content that should not be
    // kept in clipboard histories.
    static const UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    static const UINT canInclude = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    static const UINT ignore = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
    if (IsClipboardFormatAvailable(exclude) || IsClipboardFormatAvailable(ignore)) {
        out.excluded = true;
        return ReadResult::Ok;
    }
    if (IsClipboardFormatAvailable(canInclude)) {
        std::string v;
        DWORD allowed = 1;
        if (DataBytes(canInclude, v) && v.size() >= sizeof(DWORD)) memcpy(&allowed, v.data(), sizeof(DWORD));
        if (!allowed) {
            out.excluded = true;
            return ReadResult::Ok;
        }
    }

    // The program that copied: the clipboard owner, else (some programs
    // copy without one) the program in front.
    HWND from = GetClipboardOwner();
    out.source = ProgramName(from ? from : GetForegroundWindow());
    if (out.source == APP_NAME) out.source.clear();

    // Copied files (Explorer).
    if (files && IsClipboardFormatAvailable(CF_HDROP)) {
        if (HDROP drop = (HDROP)GetClipboardData(CF_HDROP)) {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::wstring list;
            for (UINT i = 0; i < count; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring p(len + 1, L'\0');
                DragQueryFileW(drop, i, p.data(), len + 1);
                p.resize(len);
                if (p.empty()) continue;
                if (!list.empty()) list += L'\n';
                list += p;
            }
            if (!list.empty()) {
                out.kind = Capture::Files;
                out.text = std::move(list);
                return ReadResult::Ok;
            }
        }
    }

    // Text. (Programs such as Excel also offer a picture of the cells; the
    // text is what people want back.)
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            const size_t max = GlobalSize(h) / sizeof(wchar_t);
            if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                size_t n = 0;
                while (n < max && p[n]) ++n;
                out.text.assign(p, n);
                GlobalUnlock(h);
            }
        }
        if (!out.text.empty()) {
            out.kind = Capture::Text;
            return ReadResult::Ok;
        }
    }

    // Pictures: PNG keeps transparency; otherwise the bitmap.
    if (images) {
        std::string data;
        if (IsClipboardFormatAvailable(PngFormat()) && DataBytes(PngFormat(), data)) {
            const size_t len = PngLength(data);
            if (len) {
                data.resize(len);
                if (auto bmp = DecodeImage(data)) {
                    out.kind = Capture::Image;
                    out.width = (int)bmp->GetWidth();
                    out.height = (int)bmp->GetHeight();
                    out.pixelHash = PixelHash(*bmp);
                    out.png = std::move(data);
                    return ReadResult::Ok;
                }
            }
        }
        if (IsClipboardFormatAvailable(CF_DIB) && DataBytes(CF_DIB, data)) {
            if (auto bmp = FromDib(data)) {
                std::string png;
                if (EncodePng(*bmp, png)) {
                    out.kind = Capture::Image;
                    out.width = (int)bmp->GetWidth();
                    out.height = (int)bmp->GetHeight();
                    out.pixelHash = PixelHash(*bmp);
                    out.png = std::move(png);
                    return ReadResult::Ok;
                }
            }
        }
    }
    return ReadResult::Nothing;
}

bool WriteText(HWND owner, const std::wstring& text) {
    if (!BeginWrite(owner)) return false;
    const bool ok = SetUnicodeText(text);
    CloseClipboard();
    return ok;
}

bool WritePng(HWND owner, const std::string& png) {
    auto bmp = DecodeImage(png);
    if (!bmp) return false;
    const int w = (int)bmp->GetWidth(), h = (int)bmp->GetHeight();

    // A 24-bit bitmap on white: transparent parts look right everywhere.
    Bitmap flat(w, h, PixelFormat24bppRGB);
    {
        Graphics g(&flat);
        g.Clear(Color(255, 255, 255, 255));
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(bmp.get(), Rect(0, 0, w, h), 0, 0, w, h, UnitPixel);
    }
    const size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    std::string dib(sizeof(BITMAPINFOHEADER) + stride * (size_t)h, '\0');
    auto* bih = (BITMAPINFOHEADER*)dib.data();
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = w;
    bih->biHeight = h;  // bottom-up, the most compatible
    bih->biPlanes = 1;
    bih->biBitCount = 24;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = (DWORD)(stride * (size_t)h);
    Rect r(0, 0, w, h);
    BitmapData d{};
    if (flat.LockBits(&r, ImageLockModeRead, PixelFormat24bppRGB, &d) != Ok) return false;
    BYTE* bits = (BYTE*)dib.data() + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < h; ++y)
        memcpy(bits + (size_t)(h - 1 - y) * stride, (const BYTE*)d.Scan0 + (size_t)y * (size_t)d.Stride, (size_t)w * 3);
    flat.UnlockBits(&d);

    if (!BeginWrite(owner)) return false;
    bool ok = SetData(CF_DIB, dib.data(), dib.size());
    SetData(PngFormat(), png.data(), png.size());
    CloseClipboard();
    return ok;
}

bool WriteFiles(HWND owner, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return false;
    std::wstring list, text;
    for (const std::wstring& p : paths) {
        list += p;
        list += L'\0';
        if (!text.empty()) text += L"\r\n";
        text += p;
    }
    list += L'\0';
    std::string drop(sizeof(DROPFILES) + list.size() * sizeof(wchar_t), '\0');
    auto* df = (DROPFILES*)drop.data();
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    memcpy(drop.data() + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));

    static const UINT effect = RegisterClipboardFormatW(L"Preferred DropEffect");
    const DWORD copy = 1;  // DROPEFFECT_COPY
    if (!BeginWrite(owner)) return false;
    bool ok = SetData(CF_HDROP, drop.data(), drop.size());
    SetData(effect, &copy, sizeof(copy));
    SetUnicodeText(text);
    CloseClipboard();
    return ok;
}

void SendPaste() {
    std::vector<INPUT> in;
    auto key = [&](WORD vk, bool up) {
        INPUT i{};
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = vk;
        i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        in.push_back(i);
    };
    // Keys still held from the shortcut would turn Ctrl+V into something
    // else. Ctrl goes down first, so releasing Alt does not open a menu.
    key(VK_CONTROL, false);
    for (WORD vk : {VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(vk) & 0x8000) key(vk, true);
    key('V', false);
    key('V', true);
    key(VK_CONTROL, true);
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}
}  // namespace ClipboardIO
