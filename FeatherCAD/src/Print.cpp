// Print.cpp - see Print.h.
#include "Print.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <commdlg.h>

namespace {
COLORREF Ref(uint32_t rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

class Pens {
public:
    explicit Pens(HDC dc) : m_dc(dc) {}
    ~Pens() {
        for (auto& kv : m_pens) DeleteObject(kv.second);
        for (auto& kv : m_brushes) DeleteObject(kv.second);
    }
    HPEN Pen(uint32_t rgb, int width) {
        auto key = std::make_pair(rgb, width);
        auto it = m_pens.find(key);
        if (it != m_pens.end()) return it->second;
        LOGBRUSH lb{BS_SOLID, Ref(rgb), 0};
        HPEN p = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, (DWORD)width, &lb, 0, nullptr);
        m_pens[key] = p;
        return p;
    }
    HBRUSH Brush(uint32_t rgb) {
        auto it = m_brushes.find(rgb);
        if (it != m_brushes.end()) return it->second;
        HBRUSH b = CreateSolidBrush(Ref(rgb));
        m_brushes[rgb] = b;
        return b;
    }

private:
    HDC m_dc;
    std::map<std::pair<uint32_t, int>, HPEN> m_pens;
    std::map<uint32_t, HBRUSH> m_brushes;
};
}  // namespace

void DrawPlotPage(HDC dc, const PlotPage& page, double k, double x, double y) {
    auto pt = [&](Vec2 p) { return POINT{(LONG)std::lround(x + p.x * k), (LONG)std::lround(y + (page.height - p.y) * k)}; };
    Pens pens(dc);
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);
    SetPolyFillMode(dc, ALTERNATE);
    SetGraphicsMode(dc, GM_ADVANCED);
    int clip = -1, clipSaved = 0;
    std::vector<POINT> pts;
    for (const PlotItem& it : page.items) {
        if (it.clip != clip) {
            if (clipSaved) RestoreDC(dc, clipSaved);
            clipSaved = 0;
            clip = it.clip;
            if (clip >= 0 && (size_t)clip < page.clips.size()) {
                const Box& b = page.clips[(size_t)clip];
                clipSaved = SaveDC(dc);
                const POINT a = pt({b.minX, b.maxY}), c = pt({b.maxX, b.minY});
                IntersectClipRect(dc, a.x, a.y, c.x + 1, c.y + 1);
            }
        }
        switch (it.kind) {
            case PlotItem::Path: {
                if (it.pts.size() < 2) break;
                const int w = std::max(1, (int)std::lround(it.width * k));
                if (it.pts.size() == 2 && Near(it.pts[0], it.pts[1], 1e-9)) {  // a dot
                    const POINT c = pt(it.pts[0]);
                    const int r = std::max(1, w / 2);
                    SelectObject(dc, GetStockObject(NULL_PEN));
                    SelectObject(dc, pens.Brush(it.rgb));
                    Ellipse(dc, c.x - r, c.y - r, c.x + r + 1, c.y + r + 1);
                    break;
                }
                pts.clear();
                for (Vec2 p : it.pts) pts.push_back(pt(p));
                if (it.closed) pts.push_back(pts.front());
                SelectObject(dc, pens.Pen(it.rgb, w));
                Polyline(dc, pts.data(), (int)pts.size());
                break;
            }
            case PlotItem::Fill: {
                pts.clear();
                std::vector<INT> counts;
                for (const auto& l : it.loops) {
                    for (Vec2 p : l) pts.push_back(pt(p));
                    counts.push_back((INT)l.size());
                }
                SelectObject(dc, GetStockObject(NULL_PEN));
                SelectObject(dc, pens.Brush(it.rgb));
                PolyPolygon(dc, pts.data(), counts.data(), (int)counts.size());
                break;
            }
            case PlotItem::Text: {
                const TextItem& t = it.text;
                const int em = (int)std::lround(FontSizeForHeight(t.height) * k);
                if (em < 1) break;
                const int tenths = (int)std::lround(ToDegrees(t.angle) * 10);
                const std::wstring face = t.font.empty() ? L"Arial" : t.font;
                HFONT f = CreateFontW(-em, 0, tenths, tenths, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face.c_str());
                if (t.widthFactor > 0 && std::fabs(t.widthFactor - 1) > 1e-6) {  // wider or narrower letters
                    HGDIOBJ old = SelectObject(dc, f);
                    TEXTMETRICW tm{};
                    GetTextMetricsW(dc, &tm);
                    SelectObject(dc, old);
                    DeleteObject(f);
                    f = CreateFontW(-em, (int)std::lround(tm.tmAveCharWidth * t.widthFactor), tenths, tenths, FW_NORMAL, 0, 0, 0,
                                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                                    face.c_str());
                }
                HGDIOBJ old = SelectObject(dc, f);
                SetTextColor(dc, Ref(it.rgb));
                SetTextAlign(dc, TA_BASELINE | (t.align == 1 ? TA_CENTER : t.align == 2 ? TA_RIGHT : TA_LEFT));
                const Vec2 n{-std::sin(t.angle), std::cos(t.angle)};
                const POINT at = pt(t.pos - n * (t.height / 2));  // on the baseline
                TextOutW(dc, at.x, at.y, t.text.c_str(), (int)t.text.size());
                SelectObject(dc, old);
                DeleteObject(f);
                break;
            }
        }
    }
    if (clipSaved) RestoreDC(dc, clipSaved);
    RestoreDC(dc, saved);
}

namespace {
short DmPaper(const PlotPage& p) {
    const double w = std::min(p.width, p.height), h = std::max(p.width, p.height);
    struct P {
        double w, h;
        short id;
    };
    const P list[] = {{210, 297, DMPAPER_A4}, {297, 420, DMPAPER_A3}, {420, 594, 66 /* A2 */}, {215.9, 279.4, DMPAPER_LETTER},
                      {215.9, 355.6, DMPAPER_LEGAL}, {279.4, 431.8, DMPAPER_TABLOID}, {431.8, 558.8, DMPAPER_CSHEET},
                      {558.8, 863.6, DMPAPER_DSHEET}, {863.6, 1117.6, DMPAPER_ESHEET}};
    for (const P& x : list)
        if (std::fabs(x.w - w) < 2 && std::fabs(x.h - h) < 2) return x.id;
    return 0;
}
}  // namespace

bool PrintPages(HWND owner, const std::vector<PlotPage>& pages, bool fit, const std::wstring& title, std::wstring& error) {
    if (pages.empty()) return false;
    // The default printer, set to the first page's paper and orientation.
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDEFAULT;
    if (PrintDlgW(&pd) && pd.hDevMode) {
        if (auto* dm = (DEVMODEW*)GlobalLock(pd.hDevMode)) {
            dm->dmOrientation = pages[0].width > pages[0].height ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            dm->dmFields |= DM_ORIENTATION;
            if (const short paper = DmPaper(pages[0])) {
                dm->dmPaperSize = paper;
                dm->dmFields |= DM_PAPERSIZE;
            }
            GlobalUnlock(pd.hDevMode);
        }
    }
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE;
    const BOOL ok = PrintDlgW(&pd);
    auto cleanup = [&] {
        if (pd.hDC) DeleteDC(pd.hDC);
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
    };
    if (!ok || !pd.hDC) {
        const DWORD e = CommDlgExtendedError();
        if (e) error = L"The printer could not be used (error " + std::to_wstring(e) + L").";
        cleanup();
        return false;
    }
    HDC dc = pd.hDC;
    DOCINFOW di{};
    di.cbSize = sizeof(di);
    di.lpszDocName = title.c_str();
    if (StartDocW(dc, &di) <= 0) {
        error = L"The printer did not start the job.";
        cleanup();
        return false;
    }
    bool good = true;
    for (const PlotPage& page : pages) {
        if (StartPage(dc) <= 0) {
            good = false;
            break;
        }
        const double pxPerMm = GetDeviceCaps(dc, LOGPIXELSX) / 25.4;
        const double resX = GetDeviceCaps(dc, HORZRES), resY = GetDeviceCaps(dc, VERTRES);
        double k = pxPerMm, x, y;
        if (fit) {
            k = std::min(resX / page.width, resY / page.height);
            x = (resX - page.width * k) / 2;
            y = (resY - page.height * k) / 2;
        } else {  // real size, from the paper's corner
            x = -GetDeviceCaps(dc, PHYSICALOFFSETX);
            y = -GetDeviceCaps(dc, PHYSICALOFFSETY);
        }
        DrawPlotPage(dc, page, k, x, y);
        if (EndPage(dc) <= 0) {
            good = false;
            break;
        }
    }
    if (good) EndDoc(dc);
    else AbortDoc(dc);
    if (!good) error = L"Printing stopped.";
    cleanup();
    return good;
}
