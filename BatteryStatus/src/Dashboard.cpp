// Dashboard.cpp - painting the gauge, cards and history graph (GDI+).
#include "Dashboard.h"

#include <algorithm>
#include <ctime>

#include <windowsx.h>

#include "GdiPlusInc.h"
#include "Theme.h"
#include "Util.h"

using namespace Gdiplus;

namespace {
const wchar_t kClass[] = L"BsDashboard";

struct Palette {
    Color page, card, cardBorder, text, dim, grid, accent, good, warn, bad, chargeBand, line, fillTop;
};

Palette GetPalette() {
    const bool dark = CurrentTheme().dark;
    if (dark)
        return {Color(255, 32, 32, 32),   Color(255, 43, 43, 43),    Color(255, 58, 58, 58),
                Color(255, 238, 238, 238), Color(255, 165, 165, 165), Color(255, 62, 62, 62),
                Color(255, 96, 205, 255), Color(255, 108, 203, 95),  Color(255, 252, 185, 65),
                Color(255, 255, 120, 110), Color(60, 108, 203, 95),   Color(255, 96, 205, 255),
                Color(70, 96, 205, 255)};
    return {Color(255, 243, 243, 243), Color(255, 255, 255, 255), Color(255, 225, 225, 225),
            Color(255, 28, 28, 28),    Color(255, 105, 105, 105), Color(255, 232, 232, 232),
            Color(255, 0, 95, 184),    Color(255, 16, 124, 16),   Color(255, 176, 106, 0),
            Color(255, 196, 43, 28),   Color(55, 16, 160, 16),    Color(255, 0, 95, 184),
            Color(50, 0, 95, 184)};
}

Color ToneColor(const Palette& p, int tone) {
    switch (tone) {
        case kGood: return p.good;
        case kWarn: return p.warn;
        case kBad: return p.bad;
        default: return p.text;
    }
}

Color LevelColor(const Palette& p, int pct, int low, bool charging) {
    if (charging) return p.good;
    if (pct <= low) return p.bad;
    if (pct <= 40) return p.warn;
    return p.good;
}

void RoundRect(GraphicsPath& path, RectF r, float rad) {
    const float d = rad * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void Card(Graphics& g, const Palette& p, RectF r, float s) {
    GraphicsPath path;
    RoundRect(path, r, 8 * s);
    SolidBrush b(p.card);
    g.FillPath(&b, &path);
    Pen pen(p.cardBorder, 1);
    g.DrawPath(&pen, &path);
}

struct Fonts {
    FontFamily family;
    Fonts() : family(L"Segoe UI") {}
    const FontFamily* Fam() const { return family.IsAvailable() ? &family : FontFamily::GenericSansSerif(); }
};

void Text(Graphics& g, const std::wstring& t, const Font& f, const Color& c, RectF r, StringAlignment h = StringAlignmentNear,
          StringAlignment v = StringAlignmentNear) {
    StringFormat fmt;
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    g.DrawString(t.c_str(), (INT)t.size(), &f, r, &fmt, &b);
}

void WrappedText(Graphics& g, const std::wstring& t, const Font& f, const Color& c, RectF r) {
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingEllipsisWord);
    SolidBrush b(c);
    g.DrawString(t.c_str(), (INT)t.size(), &f, r, &fmt, &b);
}

std::wstring TimeLabel(int64_t t, int hours) {
    const time_t tt = (time_t)t;
    tm local{};
    localtime_s(&local, &tt);
    wchar_t buf[32];
    wcsftime(buf, 32, hours > 48 ? L"%a %d" : L"%H:%M", &local);
    return buf;
}
}  // namespace

bool Dashboard::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Dashboard::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr,
                             GetModuleHandleW(nullptr), this);
    m_dpi = GetWindowDpi(parent);
    return m_hwnd != nullptr;
}

void Dashboard::SetData(const Summary& s, const History* history, int graphHours, int lowPercent, int fullPercent,
                        bool lowLine, bool fullLine) {
    m_summary = s;
    m_history = history;
    m_hours = graphHours;
    m_low = lowPercent;
    m_full = fullPercent;
    m_lowLine = lowLine;
    m_fullLine = fullLine;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

LRESULT CALLBACK Dashboard::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Dashboard* self;
    if (msg == WM_NCCREATE) {
        self = (Dashboard*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (Dashboard*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Dashboard::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            Paint(hdc);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_DPICHANGED_AFTERPARENT:
            m_dpi = GetWindowDpi(m_hwnd);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE: {
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
            TrackMouseEvent(&tme);
            const int x = PtInRect(&m_plot, pt) ? pt.x : -1;
            if (x != m_hoverX) {
                m_hoverX = x;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (m_hoverX >= 0) {
                m_hoverX = -1;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void Dashboard::Paint(HDC hdc) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        const Palette p = GetPalette();
        const float s = m_dpi / 96.0f;
        const float W = (float)rc.right, H = (float)rc.bottom;
        const float m = 16 * s;
        SolidBrush page(p.page);
        g.FillRectangle(&page, 0.0f, 0.0f, W, H);

        Fonts fonts;
        Font big(fonts.Fam(), 46 * s, FontStyleBold, UnitPixel);
        Font state(fonts.Fam(), 18 * s, FontStyleBold, UnitPixel);
        Font body(fonts.Fam(), 13.5f * s, FontStyleRegular, UnitPixel);
        Font bodyBold(fonts.Fam(), 13.5f * s, FontStyleBold, UnitPixel);
        Font smallFont(fonts.Fam(), 12 * s, FontStyleRegular, UnitPixel);
        Font title(fonts.Fam(), 14 * s, FontStyleBold, UnitPixel);
        const Summary& sm = m_summary;
        const int pct = std::max(0, sm.percent);

        // ---- Header: battery gauge, percentage, state, estimate, advice ----
        const float gx = m + 4 * s, gy = m + 14 * s, gw = 150 * s, gh = 72 * s;
        {
            GraphicsPath path;
            RoundRect(path, RectF(gx, gy, gw, gh), 10 * s);
            Pen outline(p.text, 3.2f * s);
            const float inset = 7 * s;
            if (sm.hasBattery) {
                const float fillW = (gw - 2 * inset) * pct / 100.0f;
                GraphicsPath fill;
                if (fillW > 2 * s) {
                    RoundRect(fill, RectF(gx + inset, gy + inset, fillW, gh - 2 * inset), 5 * s);
                    SolidBrush fb(LevelColor(p, pct, m_low, sm.charging));
                    g.FillPath(&fb, &fill);
                }
            }
            g.DrawPath(&outline, &path);
            SolidBrush nub(p.text);
            g.FillRectangle(&nub, gx + gw + 3 * s, gy + gh * 0.32f, 7 * s, gh * 0.36f);
            if (sm.charging || (sm.onAc && sm.hasBattery)) {
                const float cx = gx + gw / 2, cy = gy + gh / 2, k = gh / 72.0f * 1.0f;
                PointF bolt[] = {{cx + 6 * k, cy - 30 * k}, {cx - 16 * k, cy + 4 * k}, {cx - 2 * k, cy + 4 * k},
                                 {cx - 8 * k, cy + 30 * k}, {cx + 16 * k, cy - 6 * k}, {cx + 2 * k, cy - 6 * k}};
                SolidBrush bf(Color(255, 255, 196, 40));
                Pen be(Color(255, 40, 40, 40), 2 * s);
                g.FillPolygon(&bf, bolt, 6);
                g.DrawPolygon(&be, bolt, 6);
            }
            if (!sm.hasBattery) Text(g, L"\x2014", state, p.dim, RectF(gx, gy, gw, gh), StringAlignmentCenter, StringAlignmentCenter);
        }
        const float tx = gx + gw + 34 * s, tw = W - tx - m;
        Text(g, sm.hasBattery ? std::to_wstring(pct) + L"%" : L"\x2014", big, p.text, RectF(tx, m - 2 * s, tw, 58 * s));
        Text(g, sm.state, state, sm.charging ? p.good : p.text, RectF(tx, m + 56 * s, tw, 24 * s));
        Text(g, sm.estimate, body, p.dim, RectF(tx, m + 82 * s, tw, 20 * s));
        if (!sm.advice.empty())
            Text(g, sm.advice, bodyBold, ToneColor(p, sm.adviceTone), RectF(tx, m + 104 * s, tw, 20 * s));

        // ---- Cards ----
        const float top = m + 136 * s;
        const float rowH = 23 * s, head = 34 * s;
        const size_t rows = std::max(sm.health.size(), sm.session.size());
        const float cardH = head + rowH * (float)std::max<size_t>(rows, 3) + 10 * s;
        const float cw = (W - 3 * m) / 2;
        auto drawCard = [&](float x, const wchar_t* heading, const std::vector<InfoRow>& list) {
            Card(g, p, RectF(x, top, cw, cardH), s);
            Text(g, heading, title, p.text, RectF(x + 14 * s, top + 10 * s, cw - 28 * s, 20 * s));
            float y = top + head;
            const float labelW = std::min(170 * s, cw * 0.46f);
            for (const InfoRow& r : list) {
                Text(g, r.label, body, p.dim, RectF(x + 14 * s, y, labelW, rowH));
                Text(g, r.value, r.tone == kNormal ? body : bodyBold, ToneColor(p, r.tone),
                     RectF(x + 14 * s + labelW, y, cw - 28 * s - labelW, rowH));
                y += rowH;
            }
            if (list.empty()) Text(g, L"Nothing to show yet.", body, p.dim, RectF(x + 14 * s, y, cw - 28 * s, rowH));
        };
        drawCard(m, L"Battery", sm.health);
        drawCard(m * 2 + cw, L"This session", sm.session);

        // ---- History graph ----
        const float gTop = top + cardH + m;
        const float gH = H - gTop - m;
        m_plot = RECT{};
        if (gH > 90 * s) {
            Card(g, p, RectF(m, gTop, W - 2 * m, gH), s);
            const std::wstring range = m_hours == 1 ? L"last hour" : m_hours == 168 ? L"last 7 days"
                                                                                     : L"last " + std::to_wstring(m_hours) + L" hours";
            Text(g, L"Battery level \x2014 " + range, title, p.text, RectF(m + 14 * s, gTop + 10 * s, W / 2, 20 * s));
            // Legend: charging shading.
            {
                const float lx = W - m - 14 * s - 190 * s, ly = gTop + 13 * s;
                SolidBrush band(p.chargeBand);
                g.FillRectangle(&band, lx, ly, 18 * s, 12 * s);
                Text(g, L"On the charger", smallFont, p.dim, RectF(lx + 24 * s, ly - 2 * s, 160 * s, 16 * s));
            }
            const float pl = m + 52 * s, pr = W - m - 18 * s, pt = gTop + 42 * s, pb = gTop + gH - 30 * s;
            if (pb - pt > 30 * s && pr - pl > 60 * s) {
                m_plot = RECT{(LONG)pl, (LONG)pt, (LONG)pr, (LONG)pb};
                const int64_t now = History::Now();
                m_t1 = now;
                m_t0 = now - (int64_t)m_hours * 3600;
                auto X = [&](int64_t t) { return pl + (float)(t - m_t0) / (float)(m_t1 - m_t0) * (pr - pl); };
                auto Y = [&](double v) { return pb - (float)(v / 100.0) * (pb - pt); };
                Pen grid(p.grid, 1);
                for (int v = 0; v <= 100; v += 25) {
                    g.DrawLine(&grid, pl, Y(v), pr, Y(v));
                    Text(g, std::to_wstring(v) + L"%", smallFont, p.dim, RectF(m + 4 * s, Y(v) - 8 * s, 42 * s, 16 * s),
                         StringAlignmentFar);
                }
                // Time labels.
                const int64_t step = m_hours == 1 ? 600 : m_hours == 6 ? 3600 : m_hours == 24 ? 3 * 3600 : 86400;
                tm local{};
                const time_t t0 = (time_t)m_t0;
                localtime_s(&local, &t0);
                // Align ticks to whole local hours/days.
                int64_t first = m_t0 - (m_t0 % step);
                if (step == 86400) {
                    tm mid = local;
                    mid.tm_hour = mid.tm_min = mid.tm_sec = 0;
                    first = (int64_t)mktime(&mid);
                } else if (step >= 3600) {
                    tm h = local;
                    h.tm_min = h.tm_sec = 0;
                    first = (int64_t)mktime(&h);
                }
                for (int64_t t = first; t <= m_t1; t += step) {
                    if (t < m_t0) continue;
                    g.DrawLine(&grid, X(t), pt, X(t), pb);
                    Text(g, TimeLabel(t, m_hours), smallFont, p.dim, RectF(X(t) - 40 * s, pb + 6 * s, 80 * s, 16 * s),
                         StringAlignmentCenter);
                }
                // Alert levels.
                if (m_lowLine) {
                    Pen lp(p.bad, 1.2f * s);
                    lp.SetDashStyle(DashStyleDash);
                    g.DrawLine(&lp, pl, Y(m_low), pr, Y(m_low));
                }
                if (m_fullLine && m_full < 100) {
                    Pen fp(p.good, 1.2f * s);
                    fp.SetDashStyle(DashStyleDash);
                    g.DrawLine(&fp, pl, Y(m_full), pr, Y(m_full));
                }
                static const std::vector<Sample> kNone;
                const std::vector<Sample>& samples = m_history ? m_history->Samples() : kNone;
                g.SetClip(RectF(pl, pt - 2 * s, pr - pl, pb - pt + 4 * s));
                // Charger periods.
                SolidBrush band(p.chargeBand);
                // One rectangle per stretch on the charger (not one per sample).
                int64_t spanStart = -1, spanEnd = -1;
                auto flushSpan = [&] {
                    if (spanStart >= 0 && spanEnd > spanStart && spanEnd >= m_t0)
                        g.FillRectangle(&band, X(spanStart), pt, std::max(1.0f, X(spanEnd) - X(spanStart)), pb - pt);
                    spanStart = spanEnd = -1;
                };
                for (size_t i = 0; i + 1 < samples.size(); ++i) {
                    const Sample& a = samples[i];
                    const Sample& b = samples[i + 1];
                    if (!a.ac || b.time - a.time > 30 * 60) {
                        flushSpan();
                        continue;
                    }
                    if (spanStart < 0) spanStart = a.time;
                    spanEnd = b.time;
                }
                flushSpan();
                // The level: one point per pixel, broken at gaps (sleep, shut down).
                std::vector<std::vector<PointF>> lines(1);
                int lastPx = -100000;
                int64_t lastT = 0;
                for (const Sample& smp : samples) {
                    if (smp.time < m_t0 - 3600) continue;
                    if (lastT && smp.time - lastT > 30 * 60 && !lines.back().empty()) lines.emplace_back();
                    lastT = smp.time;
                    const PointF pnt(X(smp.time), Y(smp.percent));
                    const int px = (int)pnt.X;
                    if (px == lastPx && !lines.back().empty()) lines.back().back() = pnt;
                    else lines.back().push_back(pnt);
                    lastPx = px;
                }
                Pen line(p.line, 2.2f * s);
                line.SetLineJoin(LineJoinRound);
                SolidBrush area(p.fillTop);
                for (auto& l : lines) {
                    if (l.size() < 2) continue;
                    std::vector<PointF> poly = l;
                    poly.emplace_back(l.back().X, pb);
                    poly.emplace_back(l.front().X, pb);
                    g.FillPolygon(&area, poly.data(), (INT)poly.size());
                    g.DrawLines(&line, l.data(), (INT)l.size());
                }
                g.ResetClip();
                if (samples.size() < 2)
                    Text(g, L"The graph fills in while Battery Status runs (one point a minute).", body, p.dim,
                         RectF(pl, pt, pr - pl, pb - pt), StringAlignmentCenter, StringAlignmentCenter);
                // Hover readout.
                if (m_hoverX >= 0 && samples.size() >= 2) {
                    const int64_t t = m_t0 + (int64_t)((m_hoverX - pl) / (pr - pl) * (m_t1 - m_t0));
                    const Sample* best = nullptr;
                    for (const Sample& smp : samples)
                        if (!best || std::llabs(smp.time - t) < std::llabs(best->time - t)) best = &smp;
                    if (best && std::llabs(best->time - t) < std::max<int64_t>(600, (m_t1 - m_t0) / 100)) {
                        const float hx = X(best->time), hy = Y(best->percent);
                        Pen hl(p.dim, 1);
                        g.DrawLine(&hl, hx, pt, hx, pb);
                        SolidBrush dot(p.line);
                        g.FillEllipse(&dot, hx - 4 * s, hy - 4 * s, 8 * s, 8 * s);
                        std::wstring label = FormatClock(best->time) + L"  \x00B7  " + std::to_wstring(best->percent) +
                                             L"%  \x00B7  " + (best->charging ? L"charging" : best->ac ? L"on charger" : L"on battery");
                        if (best->rate) {
                            wchar_t w[32];
                            swprintf_s(w, L"  \x00B7  %.1f W", std::abs(best->rate) / 1000.0);
                            label += w;
                        }
                        RectF box;
                        StringFormat fmt;
                        g.MeasureString(label.c_str(), (INT)label.size(), &smallFont, PointF(0, 0), &fmt, &box);
                        float bx = hx + 10 * s;
                        if (bx + box.Width + 12 * s > pr) bx = hx - box.Width - 22 * s;
                        const float by = std::max(pt, hy - 34 * s);
                        GraphicsPath tip;
                        RoundRect(tip, RectF(bx, by, box.Width + 12 * s, box.Height + 8 * s), 5 * s);
                        SolidBrush tb(p.card);
                        Pen tpen(p.cardBorder, 1);
                        g.FillPath(&tb, &tip);
                        g.DrawPath(&tpen, &tip);
                        Text(g, label, smallFont, p.text, RectF(bx + 6 * s, by + 4 * s, box.Width + 4 * s, box.Height));
                    }
                }
            }
        } else {
            WrappedText(g, L"Make the window taller to see the history graph.", smallFont, p.dim,
                        RectF(m, gTop, W - 2 * m, 40 * s));
        }
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}
