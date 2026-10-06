// Picture.cpp - image files, the saved signature and the signature dialog.
#include "Picture.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>

#include "GdiPlusInc.h"
#include "Util.h"
#include "resource.h"

using namespace Gdiplus;

namespace {
// Copies a GDI+ bitmap into a Picture (BGRA, straight alpha).
bool FromBitmap(Bitmap& bmp, Picture& out) {
    const int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
    if (w <= 0 || h <= 0) return false;
    Rect r(0, 0, w, h);
    BitmapData d{};
    if (bmp.LockBits(&r, ImageLockModeRead, PixelFormat32bppARGB, &d) != Ok) return false;
    out.width = w;
    out.height = h;
    out.pixels.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        memcpy(out.pixels.data() + (size_t)y * w * 4, (const BYTE*)d.Scan0 + (size_t)y * d.Stride, (size_t)w * 4);
    bmp.UnlockBits(&d);
    return true;
}

std::unique_ptr<Bitmap> ToBitmap(const Picture& pic) {
    auto bmp = std::make_unique<Bitmap>(pic.width, pic.height, PixelFormat32bppARGB);
    Rect r(0, 0, pic.width, pic.height);
    BitmapData d{};
    if (bmp->LockBits(&r, ImageLockModeWrite, PixelFormat32bppARGB, &d) != Ok) return nullptr;
    for (int y = 0; y < pic.height; ++y)
        memcpy((BYTE*)d.Scan0 + (size_t)y * d.Stride, pic.pixels.data() + (size_t)y * pic.width * 4,
               (size_t)pic.width * 4);
    bmp->UnlockBits(&d);
    return bmp;
}

std::wstring SignaturePath(bool create) {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base))) dir = base;
    CoTaskMemFree(base);
    if (dir.empty()) return {};
    dir += L"\\FeatherPDF";
    if (create) CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\signature.png";
}

bool PngEncoder(CLSID& clsid) {
    UINT count = 0, size = 0;
    if (GetImageEncodersSize(&count, &size) != Ok || !size) return false;
    std::vector<BYTE> buf(size);
    auto* codecs = (ImageCodecInfo*)buf.data();
    if (GetImageEncoders(count, size, codecs) != Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    return false;
}

// Only the part with ink, plus a small margin.
Picture Crop(const Picture& p) {
    int x0 = p.width, y0 = p.height, x1 = -1, y1 = -1;
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x)
            if (p.pixels[((size_t)y * p.width + x) * 4 + 3] > 10) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    Picture out;
    if (x1 < 0) return out;
    const int m = 6;
    x0 = std::max(0, x0 - m);
    y0 = std::max(0, y0 - m);
    x1 = std::min(p.width - 1, x1 + m);
    y1 = std::min(p.height - 1, y1 + m);
    out.width = x1 - x0 + 1;
    out.height = y1 - y0 + 1;
    out.pixels.resize((size_t)out.width * out.height * 4);
    for (int y = 0; y < out.height; ++y)
        memcpy(out.pixels.data() + (size_t)y * out.width * 4, p.pixels.data() + ((size_t)(y + y0) * p.width + x0) * 4,
               (size_t)out.width * 4);
    return out;
}
}  // namespace

bool LoadPictureFile(const std::wstring& path, int maxSide, Picture& out) {
    Bitmap src(path.c_str());
    if (src.GetLastStatus() != Ok || src.GetWidth() == 0 || src.GetHeight() == 0) return false;
    int w = (int)src.GetWidth(), h = (int)src.GetHeight();
    const double k = std::min(1.0, (double)maxSide / std::max(w, h));
    w = std::max(1, (int)std::lround(w * k));
    h = std::max(1, (int)std::lround(h * k));
    Bitmap scaled(w, h, PixelFormat32bppARGB);
    {
        Graphics g(&scaled);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(&src, Rect(0, 0, w, h), 0, 0, (INT)src.GetWidth(), (INT)src.GetHeight(), UnitPixel);
    }
    return FromBitmap(scaled, out);
}

bool LoadSavedSignature(Picture& out) {
    const std::wstring path = SignaturePath(false);
    return !path.empty() && FileExists(path) && LoadPictureFile(path, 4000, out);
}

bool SaveSignature(const Picture& sig) {
    const std::wstring path = SignaturePath(true);
    CLSID png;
    if (path.empty() || sig.Empty() || !PngEncoder(png)) return false;
    auto bmp = ToBitmap(sig);
    return bmp && bmp->Save(path.c_str(), &png, nullptr) == Ok;
}

void DeleteSavedSignature() {
    const std::wstring path = SignaturePath(false);
    if (!path.empty()) DeleteFileW(path.c_str());
}

// ===========================================================================
// Signature dialog
//
// The pad keeps the signature in a transparent bitmap at twice the pad's
// size (smooth when scaled onto the page). Drawing, typing or a picture
// each replace what was there.
// ===========================================================================
namespace {
const wchar_t kPadClass[] = L"FeatherSignPad";

struct SignState {
    std::unique_ptr<Bitmap> ink;
    bool drawing = false;
    Gdiplus::PointF last;
    bool fromPicture = false;
    bool blue = false;
    Picture result;
    HWND pad = nullptr;
    HWND dlg = nullptr;

    Color InkColor() const { return blue ? Color(255, 20, 50, 150) : Color(255, 15, 15, 20); }

    void Reset() {
        RECT rc;
        GetClientRect(pad, &rc);
        ink = std::make_unique<Bitmap>(std::max(1L, rc.right * 2), std::max(1L, rc.bottom * 2), PixelFormat32bppARGB);
        Graphics(ink.get()).Clear(Color(0, 0, 0, 0));
        fromPicture = false;
        InvalidateRect(pad, nullptr, FALSE);
    }

    bool Empty() {
        if (!ink) return true;
        Picture p;
        FromBitmap(*ink, p);
        for (size_t i = 3; i < p.pixels.size(); i += 4)
            if (p.pixels[i] > 10) return false;
        return true;
    }

    void Recolor() {  // drawn or typed ink follows the colour choice
        if (!ink || fromPicture) return;
        Picture p;
        if (!FromBitmap(*ink, p)) return;
        const Color c = InkColor();
        for (size_t i = 0; i + 3 < p.pixels.size(); i += 4) {
            p.pixels[i] = c.GetB();
            p.pixels[i + 1] = c.GetG();
            p.pixels[i + 2] = c.GetR();
        }
        ink = ToBitmap(p);
        InvalidateRect(pad, nullptr, FALSE);
    }

    void Type(const std::wstring& name) {
        Reset();
        if (name.empty()) return;
        Graphics g(ink.get());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        const wchar_t* faces[] = {L"Segoe Script", L"Ink Free", L"Lucida Handwriting", L"Brush Script MT", L"Segoe UI"};
        std::unique_ptr<FontFamily> family;
        for (const wchar_t* f : faces) {
            family = std::make_unique<FontFamily>(f);
            if (family->GetLastStatus() == Ok) break;
        }
        const float w = (float)ink->GetWidth(), h = (float)ink->GetHeight();
        float size = h * 0.42f;
        Gdiplus::RectF box;
        for (int i = 0; i < 12; ++i) {
            Font font(family.get(), size, FontStyleItalic, UnitPixel);
            g.MeasureString(name.c_str(), (INT)name.size(), &font, Gdiplus::PointF(0, 0), &box);
            if (box.Width <= w * 0.9f) break;
            size *= 0.85f;
        }
        Font font(family.get(), size, FontStyleItalic, UnitPixel);
        g.MeasureString(name.c_str(), (INT)name.size(), &font, Gdiplus::PointF(0, 0), &box);
        SolidBrush brush(InkColor());
        g.DrawString(name.c_str(), (INT)name.size(), &font, Gdiplus::PointF((w - box.Width) / 2, (h - box.Height) / 2), &brush);
        InvalidateRect(pad, nullptr, FALSE);
    }

    void UsePicture(HWND owner) {
        wchar_t file[MAX_PATH * 2] = L"";
        OPENFILENAMEW ofn{sizeof(ofn)};
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"Pictures (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH * 2;
        ofn.lpstrTitle = L"Choose a picture of your signature";
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetOpenFileNameW(&ofn)) return;
        Picture pic;
        if (!LoadPictureFile(file, 2000, pic)) {
            MessageBoxW(owner, L"That picture could not be read.", APP_NAME, MB_ICONWARNING);
            return;
        }
        // A scanned signature: the white paper becomes transparent.
        for (size_t i = 0; i + 3 < pic.pixels.size(); i += 4) {
            const int lum = (pic.pixels[i] * 11 + pic.pixels[i + 1] * 59 + pic.pixels[i + 2] * 30) / 100;
            if (lum > 225) pic.pixels[i + 3] = 0;
            else if (lum > 170) pic.pixels[i + 3] = (uint8_t)(pic.pixels[i + 3] * (225 - lum) / 55);
        }
        Reset();
        auto src = ToBitmap(pic);
        const float w = (float)ink->GetWidth(), h = (float)ink->GetHeight();
        const float k = std::min(w * 0.95f / pic.width, h * 0.9f / pic.height);
        const float dw = pic.width * k, dh = pic.height * k;
        Graphics g(ink.get());
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(src.get(), Gdiplus::RectF((w - dw) / 2, (h - dh) / 2, dw, dh));
        fromPicture = true;
        SetDlgItemTextW(dlg, IDC_SIG_TYPE, L"");
        InvalidateRect(pad, nullptr, FALSE);
    }
};

LRESULT CALLBACK PadProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (SignState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_LBUTTONDOWN:
            if (!st || !st->ink) break;
            if (st->fromPicture) st->Reset();
            SetDlgItemTextW(st->dlg, IDC_SIG_TYPE, L"");
            st->drawing = true;
            st->last = Gdiplus::PointF((float)GET_X_LPARAM(lp) * 2, (float)GET_Y_LPARAM(lp) * 2);
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (st && st->drawing) {
                const Gdiplus::PointF p((float)GET_X_LPARAM(lp) * 2, (float)GET_Y_LPARAM(lp) * 2);
                Graphics g(st->ink.get());
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                Pen pen(st->InkColor(), 5.0f);
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                g.DrawLine(&pen, st->last, p);
                st->last = p;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (st && st->drawing) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            if (st) st->drawing = false;
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            Bitmap back(std::max(1L, rc.right), std::max(1L, rc.bottom), PixelFormat32bppARGB);
            {
                Graphics g(&back);
                g.Clear(Color(255, 255, 255, 255));
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                Pen line(Color(255, 200, 205, 215), 1.0f);
                const float by = rc.bottom * 0.75f;
                g.DrawLine(&line, 16.0f, by, rc.right - 16.0f, by);
                if (st && st->ink) {
                    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                    g.DrawImage(st->ink.get(), Gdiplus::RectF(0, 0, (REAL)rc.right, (REAL)rc.bottom));
                }
                if (st && !st->drawing && st->Empty()) {
                    FontFamily ff(L"Segoe UI");
                    Font f(&ff, 13, FontStyleRegular, UnitPixel);
                    SolidBrush hint(Color(255, 150, 150, 160));
                    g.DrawString(L"Sign here with the mouse or a pen", -1, &f, Gdiplus::PointF(18, by + 6), &hint);
                }
            }
            Graphics(dc).DrawImage(&back, 0, 0);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

INT_PTR CALLBACK SignatureDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (SignState*)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
        case WM_INITDIALOG:
            st = (SignState*)lp;
            SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)st);
            st->dlg = dlg;
            st->pad = GetDlgItem(dlg, IDC_SIG_PAD);
            SetWindowLongPtrW(st->pad, GWLP_USERDATA, (LONG_PTR)st);
            st->Reset();
            CheckRadioButton(dlg, IDC_SIG_BLACK, IDC_SIG_BLUE, IDC_SIG_BLUE);
            st->blue = true;
            CheckDlgButton(dlg, IDC_SIG_REMEMBER, BST_CHECKED);
            SendDlgItemMessageW(dlg, IDC_SIG_TYPE, EM_SETCUEBANNER, TRUE, (LPARAM)L"Or type your name here");
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SIG_TYPE:
                    if (HIWORD(wp) == EN_CHANGE) {
                        wchar_t name[256] = L"";
                        GetDlgItemTextW(dlg, IDC_SIG_TYPE, name, 256);
                        st->Type(name);
                    }
                    return TRUE;
                case IDC_SIG_PICTURE: st->UsePicture(dlg); return TRUE;
                case IDC_SIG_CLEAR:
                    SetDlgItemTextW(dlg, IDC_SIG_TYPE, L"");
                    st->Reset();
                    return TRUE;
                case IDC_SIG_BLACK:
                case IDC_SIG_BLUE:
                    st->blue = LOWORD(wp) == IDC_SIG_BLUE;
                    st->Recolor();
                    {
                        wchar_t name[256] = L"";
                        GetDlgItemTextW(dlg, IDC_SIG_TYPE, name, 256);
                        if (name[0]) st->Type(name);
                    }
                    return TRUE;
                case IDOK: {
                    Picture full;
                    if (!st->ink || !FromBitmap(*st->ink, full) || (st->result = Crop(full)).Empty()) {
                        MessageBoxW(dlg, L"Draw, type or choose a signature first.", APP_NAME, MB_ICONINFORMATION);
                        return TRUE;
                    }
                    if (IsDlgButtonChecked(dlg, IDC_SIG_REMEMBER) == BST_CHECKED) SaveSignature(st->result);
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}
}  // namespace

bool ShowSignatureDialog(HINSTANCE inst, HWND owner, Picture& out) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = PadProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = kPadClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    SignState st;
    if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SIGNATURE), owner, SignatureDlgProc, (LPARAM)&st) != IDOK)
        return false;
    out = std::move(st.result);
    return !out.Empty();
}
