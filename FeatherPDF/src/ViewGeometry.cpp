// ViewGeometry.cpp - PdfView's Geometry tools and redaction marks:
//   * Ruler: drag to measure a distance; ticks along the line in the chosen
//     unit and the length (at the drawing scale, e.g. 1:100) beside it.
//   * Protractor: drag the first arm from the corner, then click where the
//     second arm ends; the angle and a degree scale are shown.
//   * Rulers along the top and left edges, measured from the page's corner.
//   * Redaction marks: hatched red areas waiting for "Apply redactions".
// Measurements stay on screen (they are not part of the document) until
// cleared, or are drawn onto the page with "Keep on the page".
#include "PdfView.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

#include <windowsx.h>

#include "Util.h"

namespace {
const COLORREF kMeasureColor = RGB(0, 110, 210);
const COLORREF kMarkColor = RGB(210, 30, 30);
constexpr double kPi = 3.14159265358979323846;

float Distance(const PointF& a, const PointF& b) { return std::hypot(b.x - a.x, b.y - a.y); }

// 0.4 / 3.25 / 12.5 / 840: fewer decimals for bigger numbers.
std::wstring Number(double v) {
    const double a = std::fabs(v);
    wchar_t buf[48];
    swprintf_s(buf, a < 10 ? L"%.2f" : a < 100 ? L"%.1f" : L"%.0f", v);
    return buf;
}

// The smallest of 1, 2, 5 x 10^k that is at least `v`.
double NiceStep(double v) {
    if (v <= 0) return 1;
    const double p = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= v) return m * p;
    return 10 * p;
}

double SegmentDistance(POINT p, POINT a, POINT b) {
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

void Label(HDC dc, HFONT font, const std::wstring& text, int x, int y, COLORREF color, int dpi) {
    HGDIOBJ old = SelectObject(dc, font);
    RECT r = {0, 0, 0, 0};
    DrawTextW(dc, text.c_str(), -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    const int pad = Dpi(4, dpi);
    const int w = r.right + 2 * pad, h = r.bottom + pad;
    RECT box = {x - w / 2, y - h / 2, x - w / 2 + w, y - h / 2 + h};
    HBRUSH fill = CreateSolidBrush(RGB(255, 255, 255));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, fill), op = SelectObject(dc, pen);
    RoundRect(dc, box.left, box.top, box.right, box.bottom, Dpi(6, dpi), Dpi(6, dpi));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(fill);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}
}  // namespace

void PdfView::SetMeasureOptions(const MeasureOptions& options) {
    m_measure = options;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::ClearMeasurements() {
    m_measures.clear();
    m_angleArm = false;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

std::wstring PdfView::FormatLength(float points) const {
    return Number(points * m_measure.scale * m_measure.perPoint) + L" " + m_measure.unit;
}

std::wstring PdfView::MeasureText(const Measure& m) const {
    if (m.pts.size() == 2) return FormatLength(Distance(m.pts[0], m.pts[1]));
    if (m.pts.size() < 3) return {};
    const PointF a = m.pts[0], v = m.pts[1], c = m.pts[2];
    double d = std::fabs(std::atan2(c.y - v.y, c.x - v.x) - std::atan2(a.y - v.y, a.x - v.x)) * 180 / kPi;
    if (d > 180) d = 360 - d;
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f\x00B0", d);
    return buf;
}

void PdfView::SnapPoint(PointF& p, const PointF& from) const {
    if (GetKeyState(VK_SHIFT) >= 0) return;
    const double len = Distance(from, p);
    const double step = kPi / 12;  // 15 degrees
    const double angle = std::round(std::atan2(p.y - from.y, p.x - from.x) / step) * step;
    p = {(float)(from.x + len * std::cos(angle)), (float)(from.y + len * std::sin(angle))};
}

void PdfView::DrawMeasure(HDC dc, const Measure& m, bool live) {
    if (m.pts.size() < 2 || m.page < 0 || m.page >= PageCount()) return;
    if (!m_smallFont) m_smallFont = CreateMessageFont(m_dpi, 100);
    std::vector<POINT> c;
    for (const PointF& p : m.pts) c.push_back(ClientPointOf(m.page, p.x, p.y));
    HPEN pen = CreatePen(live ? PS_SOLID : PS_SOLID, Dpi(2, m_dpi), kMeasureColor);
    HPEN thin = CreatePen(PS_SOLID, 1, kMeasureColor);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    if (m.pts.size() == 2) {
        const POINT a = c[0], b = c[1];
        MoveToEx(dc, a.x, a.y, nullptr);
        LineTo(dc, b.x, b.y);
        const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1.0, std::hypot(dx, dy));
        const double nx = -dy / len, ny = dx / len;  // across the line
        const int endTick = Dpi(8, m_dpi);
        for (const POINT& e : {a, b}) {
            MoveToEx(dc, (int)(e.x - nx * endTick), (int)(e.y - ny * endTick), nullptr);
            LineTo(dc, (int)(e.x + nx * endTick), (int)(e.y + ny * endTick));
        }
        // Ruler ticks: a step of the unit at least 6 pixels apart, longer every 5 or 10.
        const double units = Distance(m.pts[0], m.pts[1]) * m_measure.scale * m_measure.perPoint;
        if (units > 0) {
            const double pxPerUnit = len / units;
            const double step = NiceStep(Dpi(6, m_dpi) / pxPerUnit);
            const double lead = step / std::pow(10.0, std::floor(std::log10(step)));
            const int every = lead < 1.5 ? 10 : lead < 3 ? 5 : 2;
            SelectObject(dc, thin);
            const int count = (int)std::min(500.0, std::floor(units / step));
            for (int i = 1; i <= count; ++i) {
                const double t = i * step * pxPerUnit / len;
                const int tick = Dpi(i % every == 0 ? 6 : 3, m_dpi);
                const double x = a.x + dx * t, y = a.y + dy * t;
                MoveToEx(dc, (int)x, (int)y, nullptr);
                LineTo(dc, (int)(x + nx * tick), (int)(y + ny * tick));
            }
        }
        const int off = Dpi(16, m_dpi);
        Label(dc, m_smallFont, MeasureText(m), (int)((a.x + b.x) / 2 - nx * off), (int)((a.y + b.y) / 2 - ny * off),
              kMeasureColor, m_dpi);
    } else {
        const POINT a = c[0], v = c[1], b = c[2];
        MoveToEx(dc, a.x, a.y, nullptr);
        LineTo(dc, v.x, v.y);
        LineTo(dc, b.x, b.y);
        const double a1 = std::atan2(a.y - v.y, a.x - v.x), a2 = std::atan2(b.y - v.y, b.x - v.x);
        double sweep = a2 - a1;
        while (sweep > kPi) sweep -= 2 * kPi;
        while (sweep < -kPi) sweep += 2 * kPi;
        const double arm = std::min(std::hypot(a.x - v.x, a.y - v.y), std::hypot(b.x - v.x, b.y - v.y));
        const double r = std::clamp(arm * 0.5, (double)Dpi(18, m_dpi), (double)Dpi(60, m_dpi));
        // The angle's arc.
        std::vector<POINT> arc;
        for (int i = 0; i <= 32; ++i) {
            const double t = a1 + sweep * i / 32;
            arc.push_back({(LONG)(v.x + r * std::cos(t)), (LONG)(v.y + r * std::sin(t))});
        }
        Polyline(dc, arc.data(), (int)arc.size());
        // A protractor scale from the first arm: a tick every 10 degrees over half a turn.
        SelectObject(dc, thin);
        const double dir = sweep >= 0 ? 1 : -1;
        for (int deg = 0; deg <= 180; deg += 10) {
            const double t = a1 + dir * deg * kPi / 180;
            const double r0 = r + Dpi(4, m_dpi), r1 = r0 + Dpi(deg % 30 == 0 ? 8 : 4, m_dpi);
            MoveToEx(dc, (int)(v.x + r0 * std::cos(t)), (int)(v.y + r0 * std::sin(t)), nullptr);
            LineTo(dc, (int)(v.x + r1 * std::cos(t)), (int)(v.y + r1 * std::sin(t)));
        }
        const double mid = a1 + sweep / 2, lr = r + Dpi(28, m_dpi);
        Label(dc, m_smallFont, MeasureText(m), (int)(v.x + lr * std::cos(mid)), (int)(v.y + lr * std::sin(mid)),
              kMeasureColor, m_dpi);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    DeleteObject(thin);
}

void PdfView::DrawMeasures(HDC dc) {
    for (const Measure& m : m_measures) DrawMeasure(dc, m, false);
    if (m_angleArm && m_drawPoints.size() >= 3) {  // the protractor's second arm follows the mouse
        Measure live{m_drawPage, {m_drawPoints[1], m_drawPoints[0], m_drawPoints[2]}};
        DrawMeasure(dc, live, true);
    }
}

void PdfView::DrawMarks(HDC dc) {
    if (m_marks.empty()) return;
    HBRUSH hatch = CreateHatchBrush(HS_BDIAGONAL, kMarkColor);
    HPEN pen = CreatePen(PS_SOLID, Dpi(2, m_dpi), kMarkColor);
    HGDIOBJ ob = SelectObject(dc, hatch), op = SelectObject(dc, pen);
    SetBkMode(dc, TRANSPARENT);
    for (const PageRect& m : m_marks) {
        if (m.page < 0 || m.page >= PageCount()) continue;
        const RECT r = ClientRectOf(m.page, m.rect);
        if (r.right < 0 || r.bottom < 0 || r.left > ClientW() || r.top > ClientH()) continue;
        Rectangle(dc, r.left, r.top, r.right + 1, r.bottom + 1);
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(hatch);
    DeleteObject(pen);
}

void PdfView::DrawRulers(HDC dc) {
    if (!m_measure.rulers || !HasDocument() || m_last < m_first) return;
    if (!m_smallFont) m_smallFont = CreateMessageFont(m_dpi, 100);
    const int page = std::clamp(CurrentPage(), 0, PageCount() - 1);
    if ((size_t)page >= m_layout.size()) return;
    const PageLayout& L = m_layout[(size_t)page];
    const int band = Dpi(20, m_dpi);
    const double x0 = (double)(L.left - m_scrollX), y0 = (double)(L.top - m_scrollY);
    const double pxPerUnit = m_scale / (m_measure.perPoint * m_measure.scale);
    if (pxPerUnit <= 0) return;
    const double major = NiceStep(Dpi(70, m_dpi) / pxPerUnit);
    double minor = major / 10;
    if (minor * pxPerUnit < Dpi(5, m_dpi)) minor = major / 5;
    if (minor * pxPerUnit < Dpi(5, m_dpi)) minor = major / 2;

    HBRUSH fill = CreateSolidBrush(RGB(243, 243, 243));
    RECT top = {0, 0, ClientW(), band}, left = {0, 0, band, ClientH()};
    FillRect(dc, &top, fill);
    FillRect(dc, &left, fill);
    DeleteObject(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(110, 110, 110));
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ of = SelectObject(dc, m_smallFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(70, 70, 70));
    MoveToEx(dc, 0, band - 1, nullptr);
    LineTo(dc, ClientW(), band - 1);
    MoveToEx(dc, band - 1, 0, nullptr);
    LineTo(dc, band - 1, ClientH());
    auto ticks = [&](bool horizontal) {
        const double origin = horizontal ? x0 : y0;
        const int length = horizontal ? ClientW() : ClientH();
        const long long firstTick = (long long)std::floor((band - origin) / pxPerUnit / minor);
        const long long lastTick = (long long)std::ceil((length - origin) / pxPerUnit / minor);
        const long long perMajor = std::max(1LL, std::llround(major / minor));
        for (long long i = firstTick; i <= lastTick && i - firstTick < 5000; ++i) {
            const int pos = (int)std::lround(origin + i * minor * pxPerUnit);
            if (pos < band || pos >= length) continue;
            const bool isMajor = i % perMajor == 0;
            const int tick = isMajor ? band * 2 / 3 : band / 4;
            if (horizontal) {
                MoveToEx(dc, pos, band - 1, nullptr);
                LineTo(dc, pos, band - 1 - tick);
            } else {
                MoveToEx(dc, band - 1, pos, nullptr);
                LineTo(dc, band - 1 - tick, pos);
            }
            if (isMajor) {
                wchar_t label[32];
                const int decimals = major >= 1 ? 0 : major >= 0.1 ? 1 : 2;
                swprintf_s(label, L"%.*f", decimals, i * minor);
                const std::wstring text = label;
                if (horizontal)
                    TextOutW(dc, pos + Dpi(2, m_dpi), 0, text.c_str(), (int)text.size());
                else
                    TextOutW(dc, Dpi(1, m_dpi), pos + Dpi(1, m_dpi), text.c_str(), (int)text.size());
            }
        }
    };
    ticks(true);
    ticks(false);
    // Where the mouse is.
    if (m_mouse.x >= band && m_mouse.y >= band) {
        HPEN red = CreatePen(PS_SOLID, 1, kMarkColor);
        SelectObject(dc, red);
        MoveToEx(dc, m_mouse.x, 0, nullptr);
        LineTo(dc, m_mouse.x, band);
        MoveToEx(dc, 0, m_mouse.y, nullptr);
        LineTo(dc, band, m_mouse.y);
        SelectObject(dc, pen);
        DeleteObject(red);
    }
    // The unit in the corner.
    RECT corner = {0, 0, band, band};
    HBRUSH cf = CreateSolidBrush(RGB(230, 230, 230));
    FillRect(dc, &corner, cf);
    DeleteObject(cf);
    DrawTextW(dc, m_measure.unit.c_str(), -1, &corner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, op);
    SelectObject(dc, of);
    DeleteObject(pen);
}

int PdfView::HitMeasure(POINT pt) const {
    const double near_ = Dpi(6, m_dpi);
    for (int i = (int)m_measures.size() - 1; i >= 0; --i) {
        const Measure& m = m_measures[(size_t)i];
        std::vector<POINT> c;
        for (const PointF& p : m.pts) c.push_back(ClientPointOf(m.page, p.x, p.y));
        for (size_t k = 1; k < c.size(); ++k)
            if (SegmentDistance(pt, c[k - 1], c[k]) <= near_) return i;
    }
    return -1;
}

int PdfView::HitMark(POINT pt) const {
    for (int i = (int)m_marks.size() - 1; i >= 0; --i) {
        const PageRect& m = m_marks[(size_t)i];
        if (m.page < 0 || m.page >= PageCount()) continue;
        RECT r = ClientRectOf(m.page, m.rect);
        InflateRect(&r, 2, 2);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

void PdfView::KeepMeasurements() {
    if (!onEdit) return;
    for (const Measure& m : m_measures) {
        EditOp op;
        op.kind = EditOp::AddMeasure;
        op.page = m.page;
        op.points = m.pts;
        op.text = MeasureText(m);
        op.color = kMeasureColor;
        onEdit(std::move(op));
    }
    ClearMeasurements();
}

bool PdfView::ShowGeometryMenu(POINT screen, POINT client) {
    const int mi = HitMeasure(client), ki = HitMark(client);
    if (mi < 0 && ki < 0) return false;
    HMENU m = CreatePopupMenu();
    if (mi >= 0) {
        AppendMenuW(m, MF_STRING, ID_MEASURE_KEEP, L"&Keep this measurement on the page");
        AppendMenuW(m, MF_STRING, ID_MEASURE_REMOVE, L"&Remove this measurement");
        AppendMenuW(m, MF_STRING, ID_MEASURE_CLEAR, L"&Clear all measurements");
    }
    if (ki >= 0) {
        if (mi >= 0) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, ID_REDACT_REMOVE_MARK, L"Remove this redaction &mark");
        AppendMenuW(m, MF_STRING, ID_REDACT_APPLY, L"&Apply redactions\x2026");
        AppendMenuW(m, MF_STRING, ID_REDACT_CLEAR, L"Clear all redaction m&arks");
    }
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == ID_MEASURE_KEEP && mi >= 0 && onEdit) {
        const Measure keep = m_measures[(size_t)mi];
        m_measures.erase(m_measures.begin() + mi);
        EditOp op;
        op.kind = EditOp::AddMeasure;
        op.page = keep.page;
        op.points = keep.pts;
        op.text = MeasureText(keep);
        op.color = kMeasureColor;
        onEdit(std::move(op));
    } else if (cmd == ID_MEASURE_REMOVE && mi >= 0) {
        m_measures.erase(m_measures.begin() + mi);
    } else if (cmd == ID_MEASURE_CLEAR) {
        m_measures.clear();
    } else if (cmd == ID_REDACT_REMOVE_MARK && ki >= 0) {
        m_marks.erase(m_marks.begin() + ki);
        if (onMarksChanged) onMarksChanged();
    } else if (cmd == ID_REDACT_CLEAR) {
        ClearRedactionMarks();
    } else if (cmd == ID_REDACT_APPLY) {
        SendMessageW(GetParent(m_hwnd), WM_COMMAND, ID_REDACT_APPLY, 0);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return true;
}

void PdfView::AddRedactionMarks(const std::vector<PageRect>& marks) {
    for (const PageRect& m : marks) {
        PageRect r = m;
        // A little margin so the edges of letters go too.
        r.rect.left -= 0.5f;
        r.rect.top -= 0.5f;
        r.rect.right += 0.5f;
        r.rect.bottom += 0.5f;
        m_marks.push_back(r);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    if (onMarksChanged) onMarksChanged();
}

void PdfView::ClearRedactionMarks() {
    m_marks.clear();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    if (onMarksChanged) onMarksChanged();
}

std::vector<PageRect> PdfView::SelectionRects() {
    std::vector<PageRect> out;
    if (!HasSelection()) return out;
    TextPos start, end;
    SelectionBounds(start, end);
    for (int p = start.page; p <= end.page && p < PageCount(); ++p) {
        const TextLayer* layer = GetTextLayer(p, false);
        if (!layer) continue;
        const int n = (int)layer->chars.size();
        const int s = p == start.page ? std::min(start.index, n) : 0;
        const int e = p == end.page ? std::min(end.index, n) : n;
        bool open = false;
        RectF cur;
        for (int i = s; i < e; ++i) {
            const TextChar& c = layer->chars[(size_t)i];
            if (!c.hasBox) continue;
            const float mid = (c.box.top + c.box.bottom) / 2;
            if (open && mid >= cur.top && mid <= cur.bottom && c.box.left >= cur.left - 2) {
                cur.left = std::min(cur.left, c.box.left);
                cur.right = std::max(cur.right, c.box.right);
                cur.top = std::min(cur.top, c.box.top);
                cur.bottom = std::max(cur.bottom, c.box.bottom);
                continue;
            }
            if (open) out.push_back({p, cur});
            cur = c.box;
            open = true;
        }
        if (open) out.push_back({p, cur});
    }
    return out;
}
