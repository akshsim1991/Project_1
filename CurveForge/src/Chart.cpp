// Chart.cpp - rendering (GDI+ on screen and to PNG, or SVG text) and
// mouse interaction.
#include "Chart.h"

#include <cmath>
#include <memory>
#include <string>

#include <windowsx.h>

#include "GdiPlusInc.h"
#include "NumFormat.h"
#include "Theme.h"
#include "Util.h"

using namespace Gdiplus;

namespace {
const wchar_t kChartClass[] = L"CfChart";

Color ToColor(COLORREF c, int alpha = 255) { return Color((BYTE)alpha, GetRValue(c), GetGValue(c), GetBValue(c)); }

// One GDI+ measuring surface for SVG text widths.
Graphics& MeasureGraphics() {
    static Bitmap* bmp = new Bitmap(1, 1, PixelFormat32bppARGB);
    static Graphics* g = new Graphics(bmp);
    return *g;
}

double MeasureText(Graphics& g, const std::wstring& s, double px, bool bold) {
    if (s.empty()) return 0;
    FontFamily family(L"Segoe UI");
    Font font(family.IsAvailable() ? &family : FontFamily::GenericSansSerif(), (REAL)px,
              bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    RectF box;
    g.MeasureString(s.c_str(), (INT)s.size(), &font, PointF(0, 0), StringFormat::GenericTypographic(), &box);
    return box.Width;
}

class GdiPainter : public Painter {
public:
    explicit GdiPainter(Graphics& g) : m_g(g) {
        m_g.SetSmoothingMode(SmoothingModeAntiAlias);
        m_g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        m_g.SetPixelOffsetMode(PixelOffsetModeHalf);
    }
    void FillRect(double x, double y, double w, double h, COLORREF c) override {
        SolidBrush b(ToColor(c));
        m_g.FillRectangle(&b, (REAL)x, (REAL)y, (REAL)w, (REAL)h);
    }
    void Rect(double x, double y, double w, double h, COLORREF c, double width) override {
        Pen p(ToColor(c), (REAL)width);
        m_g.DrawRectangle(&p, (REAL)x, (REAL)y, (REAL)w, (REAL)h);
    }
    void Line(double x1, double y1, double x2, double y2, COLORREF c, double width, bool dashed) override {
        Pen p(ToColor(c), (REAL)width);
        if (dashed) p.SetDashStyle(DashStyleDash);
        m_g.DrawLine(&p, (REAL)x1, (REAL)y1, (REAL)x2, (REAL)y2);
    }
    void Polyline(const std::vector<Pt>& pts, COLORREF c, double width, bool dashed) override {
        if (pts.size() < 2) return;
        std::vector<PointF> v;
        v.reserve(pts.size());
        for (const Pt& p : pts) v.emplace_back((REAL)p.x, (REAL)p.y);
        Pen p(ToColor(c), (REAL)width);
        p.SetLineJoin(LineJoinRound);
        if (dashed) p.SetDashStyle(DashStyleDash);
        m_g.DrawLines(&p, v.data(), (INT)v.size());
    }
    void FillPolygon(const std::vector<Pt>& pts, COLORREF c, int alpha) override {
        if (pts.size() < 3) return;
        std::vector<PointF> v;
        for (const Pt& p : pts) v.emplace_back((REAL)p.x, (REAL)p.y);
        SolidBrush b(ToColor(c, alpha));
        m_g.FillPolygon(&b, v.data(), (INT)v.size());
    }
    void Marker(double x, double y, double r, COLORREF fill, COLORREF stroke, double width, bool filled) override {
        if (filled) {
            SolidBrush b(ToColor(fill));
            m_g.FillEllipse(&b, (REAL)(x - r), (REAL)(y - r), (REAL)(2 * r), (REAL)(2 * r));
        }
        Pen p(ToColor(stroke), (REAL)width);
        m_g.DrawEllipse(&p, (REAL)(x - r), (REAL)(y - r), (REAL)(2 * r), (REAL)(2 * r));
    }
    void Text(const std::wstring& s, double x, double y, int align, int valign, COLORREF c, double px, bool bold,
              bool vertical) override {
        if (s.empty()) return;
        FontFamily family(L"Segoe UI");
        Font font(family.IsAvailable() ? &family : FontFamily::GenericSansSerif(), (REAL)px,
                  bold ? FontStyleBold : FontStyleRegular, UnitPixel);
        StringFormat fmt(StringFormat::GenericTypographic());
        fmt.SetAlignment(align == 0 ? StringAlignmentNear : align == 1 ? StringAlignmentCenter : StringAlignmentFar);
        fmt.SetLineAlignment(valign == 0 ? StringAlignmentNear : valign == 1 ? StringAlignmentCenter : StringAlignmentFar);
        fmt.SetFormatFlags(fmt.GetFormatFlags() | StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
        SolidBrush b(ToColor(c));
        if (vertical) {
            GraphicsState st = m_g.Save();
            m_g.TranslateTransform((REAL)x, (REAL)y);
            m_g.RotateTransform(-90);
            m_g.DrawString(s.c_str(), (INT)s.size(), &font, PointF(0, 0), &fmt, &b);
            m_g.Restore(st);
        } else {
            m_g.DrawString(s.c_str(), (INT)s.size(), &font, PointF((REAL)x, (REAL)y), &fmt, &b);
        }
    }
    double TextWidth(const std::wstring& s, double px, bool bold) override { return MeasureText(m_g, s, px, bold); }
    void Clip(double x, double y, double w, double h) override {
        m_g.SetClip(RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h));
    }
    void Unclip() override { m_g.ResetClip(); }

private:
    Graphics& m_g;
};

std::string Utf8(const std::wstring& s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)std::max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

class SvgPainter : public Painter {
public:
    std::string out;
    void FillRect(double x, double y, double w, double h, COLORREF c) override {
        out += "<rect x=\"" + N(x) + "\" y=\"" + N(y) + "\" width=\"" + N(w) + "\" height=\"" + N(h) + "\" fill=\"" +
               C(c) + "\"/>\n";
    }
    void Rect(double x, double y, double w, double h, COLORREF c, double width) override {
        out += "<rect x=\"" + N(x) + "\" y=\"" + N(y) + "\" width=\"" + N(w) + "\" height=\"" + N(h) +
               "\" fill=\"none\" stroke=\"" + C(c) + "\" stroke-width=\"" + N(width) + "\"/>\n";
    }
    void Line(double x1, double y1, double x2, double y2, COLORREF c, double width, bool dashed) override {
        out += "<line x1=\"" + N(x1) + "\" y1=\"" + N(y1) + "\" x2=\"" + N(x2) + "\" y2=\"" + N(y2) + "\" stroke=\"" +
               C(c) + "\" stroke-width=\"" + N(width) + "\"" + Dash(dashed, width) + "/>\n";
    }
    void Polyline(const std::vector<Pt>& pts, COLORREF c, double width, bool dashed) override {
        if (pts.size() < 2) return;
        out += "<polyline fill=\"none\" stroke=\"" + C(c) + "\" stroke-width=\"" + N(width) +
               "\" stroke-linejoin=\"round\"" + Dash(dashed, width) + " points=\"";
        for (const Pt& p : pts) out += N(p.x) + "," + N(p.y) + " ";
        out += "\"/>\n";
    }
    void FillPolygon(const std::vector<Pt>& pts, COLORREF c, int alpha) override {
        if (pts.size() < 3) return;
        out += "<polygon fill=\"" + C(c) + "\" fill-opacity=\"" + N(alpha / 255.0) + "\" points=\"";
        for (const Pt& p : pts) out += N(p.x) + "," + N(p.y) + " ";
        out += "\"/>\n";
    }
    void Marker(double x, double y, double r, COLORREF fill, COLORREF stroke, double width, bool filled) override {
        out += "<circle cx=\"" + N(x) + "\" cy=\"" + N(y) + "\" r=\"" + N(r) + "\" fill=\"" +
               (filled ? C(fill) : std::string("none")) + "\" stroke=\"" + C(stroke) + "\" stroke-width=\"" + N(width) +
               "\"/>\n";
    }
    void Text(const std::wstring& s, double x, double y, int align, int valign, COLORREF c, double px, bool bold,
              bool vertical) override {
        if (s.empty()) return;
        const char* anchor = align == 0 ? "start" : align == 1 ? "middle" : "end";
        // Approximate vertical alignment with a baseline shift (portable across viewers).
        const double shift = valign == 0 ? px * 0.8 : valign == 1 ? px * 0.35 : -px * 0.2;
        std::string attrs = "font-size=\"" + N(px) + "\" fill=\"" + C(c) + "\" text-anchor=\"" + anchor + "\"" +
                            (bold ? " font-weight=\"bold\"" : "");
        if (vertical)
            out += "<text " + attrs + " transform=\"translate(" + N(x + shift) + "," + N(y) + ") rotate(-90)\">";
        else
            out += "<text " + attrs + " x=\"" + N(x) + "\" y=\"" + N(y + shift) + "\">";
        out += Escape(s) + "</text>\n";
    }
    double TextWidth(const std::wstring& s, double px, bool bold) override {
        return MeasureText(MeasureGraphics(), s, px, bold);
    }
    void Clip(double x, double y, double w, double h) override {
        const std::string id = "c" + std::to_string(++m_clips);
        out += "<clipPath id=\"" + id + "\"><rect x=\"" + N(x) + "\" y=\"" + N(y) + "\" width=\"" + N(w) +
               "\" height=\"" + N(h) + "\"/></clipPath>\n<g clip-path=\"url(#" + id + ")\">\n";
        m_open = true;
    }
    void Unclip() override {
        if (m_open) out += "</g>\n";
        m_open = false;
    }

private:
    static std::string N(double v) {
        char b[32];
        snprintf(b, sizeof(b), "%.2f", v);
        std::string s = b;
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s.empty() || s == "-" ? "0" : s;
    }
    static std::string C(COLORREF c) {
        char b[16];
        snprintf(b, sizeof(b), "#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
        return b;
    }
    static std::string Dash(bool dashed, double w) {
        return dashed ? " stroke-dasharray=\"" + N(w * 4) + "," + N(w * 3) + "\"" : "";
    }
    static std::string Escape(const std::wstring& s) {
        std::string u = Utf8(s), o;
        for (char ch : u) {
            switch (ch) {
                case '&': o += "&amp;"; break;
                case '<': o += "&lt;"; break;
                case '>': o += "&gt;"; break;
                case '"': o += "&quot;"; break;
                default: o += ch;
            }
        }
        return o;
    }
    int m_clips = 0;
    bool m_open = false;
};

// Ticks at "nice" multiples (1, 2, 2.5, 5 x 10^k).
std::vector<double> NiceTicks(double lo, double hi, int maxCount, double& step) {
    std::vector<double> t;
    if (!(hi > lo) || maxCount < 1) return t;
    const double raw = (hi - lo) / maxCount;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    step = mag * 10;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (mag * m >= raw) {
            step = mag * m;
            break;
        }
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) {
        t.push_back(std::fabs(v) < step * 1e-9 ? 0.0 : v);
        if (t.size() > 200) break;
    }
    return t;
}

// Ticks for a log10 axis, in axis space.
std::vector<double> LogTicks(double lo, double hi, int maxCount) {
    std::vector<double> t;
    if (!(hi > lo)) return t;
    if (hi - lo >= 2.5) {
        const int every = std::max(1, (int)std::ceil((hi - lo) / std::max(maxCount, 1)));
        for (int d = (int)std::ceil(lo); d <= (int)std::floor(hi); ++d)
            if (d % every == 0) t.push_back(d);
        return t;
    }
    for (int d = (int)std::floor(lo) - 1; d <= (int)std::ceil(hi); ++d)
        for (double m : {1.0, 2.0, 5.0}) {
            const double v = d + std::log10(m);
            if (v >= lo - 1e-12 && v <= hi + 1e-12) t.push_back(v);
        }
    return t;
}
}  // namespace

struct Chart::Palette {
    COLORREF bg, grid, axis, text, dim, point, pointEdge, excluded, fit, band, outlier, hover, legendBg;
    static Palette Get(bool dark) {
        Palette p;
        if (dark) {
            p = {RGB(32, 32, 32),    RGB(58, 58, 58),   RGB(120, 120, 120), RGB(225, 225, 225), RGB(160, 160, 160),
                 RGB(96, 180, 255),  RGB(40, 120, 200), RGB(140, 140, 140), RGB(255, 150, 60),  RGB(255, 150, 60),
                 RGB(255, 99, 99),   RGB(255, 255, 255), RGB(40, 40, 40)};
        } else {
            p = {RGB(255, 255, 255), RGB(232, 232, 232), RGB(140, 140, 140), RGB(40, 40, 40),  RGB(110, 110, 110),
                 RGB(0, 95, 184),    RGB(0, 70, 140),    RGB(150, 150, 150), RGB(214, 90, 0),   RGB(230, 120, 30),
                 RGB(200, 30, 30),   RGB(0, 0, 0),       RGB(255, 255, 255)};
        }
        return p;
    }
};

// ===========================================================================
// Setup
// ===========================================================================
bool Chart::Create(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Chart::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = kChartClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    m_hwnd = CreateWindowExW(0, kChartClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr,
                             GetModuleHandleW(nullptr), this);
    m_dpi = GetWindowDpi(parent);
    return m_hwnd != nullptr;
}

void Chart::SetModel(ChartModel model, bool resetView) {
    m_model = std::move(model);
    if (m_hover >= (int)m_model.x.size()) m_hover = -1;
    if (resetView || !m_viewSet) ResetView();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Chart::SetOptions(const ChartOptions& o) {
    const bool axesChanged = o.logX != m_opt.logX || o.logY != m_opt.logY;
    m_opt = o;
    if (axesChanged) ResetView();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

double Chart::Tx(double v) const { return m_opt.logX ? (v > 0 ? std::log10(v) : NAN) : v; }
double Chart::Ty(double v) const { return m_opt.logY ? (v > 0 ? std::log10(v) : NAN) : v; }
double Chart::Ux(double t) const { return m_opt.logX ? std::pow(10.0, t) : t; }
double Chart::Uy(double t) const { return m_opt.logY ? std::pow(10.0, t) : t; }

bool Chart::HasData() const {
    for (size_t i = 0; i < m_model.x.size(); ++i)
        if (std::isfinite(Tx(m_model.x[i])) && std::isfinite(Ty(m_model.y[i]))) return true;
    return false;
}

Chart::View Chart::AutoView() const {
    View v;
    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (size_t i = 0; i < m_model.x.size(); ++i) {
        const double tx = Tx(m_model.x[i]), ty = Ty(m_model.y[i]);
        if (!std::isfinite(tx) || !std::isfinite(ty)) continue;
        x0 = std::min(x0, tx);
        x1 = std::max(x1, tx);
        y0 = std::min(y0, ty);
        y1 = std::max(y1, ty);
    }
    if (!std::isfinite(x0)) return View{0, 1, 0, 1};
    auto pad = [](double& a, double& b) {
        if (b - a < 1e-300 * std::max(1.0, std::fabs(a)) || b == a) {
            const double d = a == 0 ? 1 : std::fabs(a) * 0.1;
            a -= d;
            b += d;
        } else {
            const double d = (b - a) * 0.06;
            a -= d;
            b += d;
        }
    };
    pad(x0, x1);
    pad(y0, y1);
    v.x0 = x0;
    v.x1 = x1;
    v.y0 = y0;
    v.y1 = y1;
    return v;
}

void Chart::ResetView() {
    m_view = AutoView();
    m_viewSet = HasData();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Chart::Zoom(double factor) {
    const double cx = (m_view.x0 + m_view.x1) / 2, cy = (m_view.y0 + m_view.y1) / 2;
    m_view.x0 = cx + (m_view.x0 - cx) / factor;
    m_view.x1 = cx + (m_view.x1 - cx) / factor;
    m_view.y0 = cy + (m_view.y0 - cy) / factor;
    m_view.y1 = cy + (m_view.y1 - cy) / factor;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Chart::ResidualRange(double& lo, double& hi) const {
    double m = 0;
    if (m_model.fit)
        for (size_t i = 0; i < m_model.x.size(); ++i) {
            if (i < m_model.excluded.size() && m_model.excluded[i]) continue;
            const double r = m_model.y[i] - m_model.fit(m_model.x[i]);
            if (std::isfinite(r)) m = std::max(m, std::fabs(r));
        }
    if (m == 0) m = 1;
    lo = -m * 1.15;
    hi = m * 1.15;
}

// ===========================================================================
// Rendering
// ===========================================================================
Chart::Layout Chart::ComputeLayout(Painter& p, double w, double h, double s) const {
    Layout L{};
    L.scale = s;
    const double font = 12 * s;
    const bool residuals = m_opt.residuals && m_model.fit && HasData();
    // Widest y tick label decides the left margin.
    double labelW = 0;
    const int yCount = std::max(2, (int)(h / (60 * s)));
    std::vector<double> ticks;
    double step = 1;
    if (m_opt.logY) ticks = LogTicks(m_view.y0, m_view.y1, yCount);
    else ticks = NiceTicks(m_view.y0, m_view.y1, yCount, step);
    for (double t : ticks)
        labelW = std::max(labelW, p.TextWidth(m_opt.logY ? FormatNumber(Uy(t), 4) : FormatTick(t, step), font, false));
    if (residuals) {
        double lo, hi, rs = 1;
        ResidualRange(lo, hi);
        for (double t : NiceTicks(lo, hi, 3, rs)) labelW = std::max(labelW, p.TextWidth(FormatTick(t, rs), font, false));
    }
    L.left = 12 * s + 18 * s + labelW + 8 * s;
    L.top = 12 * s;
    const double right = 18 * s;
    const double bottom = 8 * s + font * 1.4 + 18 * s + 8 * s;
    L.width = std::max(10.0, w - L.left - right);
    const double total = std::max(10.0, h - L.top - bottom);
    if (residuals) {
        const double gap = 16 * s;
        L.height = (total - gap) * 0.72;
        L.rTop = L.top + L.height + gap;
        L.rHeight = total - gap - L.height;
    } else {
        L.height = total;
    }
    return L;
}

void Chart::Render(Painter& p, double w, double h, double s, bool exportStyle) const {
    const Palette pal = Palette::Get(!exportStyle && CurrentTheme().dark);
    p.FillRect(0, 0, w, h, pal.bg);
    const double font = 12 * s;
    if (!HasData()) {
        p.Text(m_model.x.empty() ? L"Open a data file (Ctrl+O), paste from Excel (Ctrl+V), or type values into the table."
                                 : L"The chosen columns have no numbers to plot" +
                                       std::wstring(m_opt.logX || m_opt.logY ? L" (log axes need values above 0)." : L"."),
               w / 2, h / 2, 1, 1, pal.dim, 13 * s, false, false);
        return;
    }
    const Layout L = ComputeLayout(p, w, h, s);
    if (!exportStyle) m_lastLayout = L;
    const View& v = m_view;
    auto px = [&](double tx) { return L.left + (tx - v.x0) / (v.x1 - v.x0) * L.width; };
    auto py = [&](double ty) { return L.top + (1 - (ty - v.y0) / (v.y1 - v.y0)) * L.height; };
    double rlo = -1, rhi = 1;
    if (L.rHeight > 0) ResidualRange(rlo, rhi);
    auto ry = [&](double r) { return L.rTop + (1 - (r - rlo) / (rhi - rlo)) * L.rHeight; };
    auto clampY = [&](double y) { return std::min(std::max(y, -10.0 * h), 11.0 * h); };

    // Ticks.
    double xs = 1, ys = 1;
    const std::vector<double> xt = m_opt.logX ? LogTicks(v.x0, v.x1, std::max(2, (int)(L.width / (90 * s))))
                                              : NiceTicks(v.x0, v.x1, std::max(2, (int)(L.width / (90 * s))), xs);
    const std::vector<double> yt = m_opt.logY ? LogTicks(v.y0, v.y1, std::max(2, (int)(L.height / (50 * s))))
                                              : NiceTicks(v.y0, v.y1, std::max(2, (int)(L.height / (50 * s))), ys);
    auto xLabel = [&](double t) { return m_opt.logX ? FormatNumber(Ux(t), 4) : FormatTick(t, xs); };
    auto yLabel = [&](double t) { return m_opt.logY ? FormatNumber(Uy(t), 4) : FormatTick(t, ys); };

    // Grid and frames.
    auto plotFrame = [&](double top, double height) {
        p.FillRect(L.left, top, L.width, height, pal.bg);
        if (m_opt.grid)
            for (double t : xt) p.Line(px(t), top, px(t), top + height, pal.grid, 1 * s, false);
        p.Rect(L.left, top, L.width, height, pal.axis, 1 * s);
    };
    plotFrame(L.top, L.height);
    if (m_opt.grid)
        for (double t : yt) p.Line(L.left, py(t), L.left + L.width, py(t), pal.grid, 1 * s, false);
    for (double t : yt) {
        p.Line(L.left - 4 * s, py(t), L.left, py(t), pal.axis, 1 * s, false);
        p.Text(yLabel(t), L.left - 7 * s, py(t), 2, 1, pal.text, font, false, false);
    }

    // Fitted curve and bands (sampled across the visible x range).
    std::vector<Painter::Pt> curve;
    std::vector<std::vector<Painter::Pt>> curves;
    std::vector<std::vector<Painter::Pt>> confUp, confLo, predUp, predLo;
    const int samples = std::max(200, (int)(L.width / s));
    if (m_model.fit) {
        std::vector<Painter::Pt> cu, cl, pu, pl;
        auto flush = [&] {
            if (curve.size() > 1) curves.push_back(curve);
            if (cu.size() > 1) {
                confUp.push_back(cu);
                confLo.push_back(cl);
            }
            if (pu.size() > 1) {
                predUp.push_back(pu);
                predLo.push_back(pl);
            }
            curve.clear();
            cu.clear();
            cl.clear();
            pu.clear();
            pl.clear();
        };
        const bool wantBands = (m_opt.confidenceBand || m_opt.predictionBand) && m_model.bands;
        for (int i = 0; i <= samples; ++i) {
            const double tx = v.x0 + (v.x1 - v.x0) * i / samples;
            const double x = Ux(tx);
            const double y = m_model.fit(x);
            const double ty = Ty(y);
            if (!std::isfinite(ty)) {
                flush();
                continue;
            }
            const double X = px(tx);
            curve.push_back({X, clampY(py(ty))});
            double conf, pred;
            if (wantBands && m_model.bands(x, conf, pred)) {
                const double a = Ty(y + conf), b = Ty(y - conf), c = Ty(y + pred), d = Ty(y - pred);
                if (std::isfinite(a) && std::isfinite(b)) {
                    cu.push_back({X, clampY(py(a))});
                    cl.push_back({X, clampY(py(b))});
                }
                if (std::isfinite(c) && std::isfinite(d)) {
                    pu.push_back({X, clampY(py(c))});
                    pl.push_back({X, clampY(py(d))});
                }
            }
        }
        flush();
    }

    p.Clip(L.left, L.top, L.width, L.height);
    if (m_opt.confidenceBand)
        for (size_t i = 0; i < confUp.size(); ++i) {
            std::vector<Painter::Pt> poly = confUp[i];
            poly.insert(poly.end(), confLo[i].rbegin(), confLo[i].rend());
            p.FillPolygon(poly, pal.band, 50);
        }
    if (m_opt.predictionBand)
        for (size_t i = 0; i < predUp.size(); ++i) {
            p.Polyline(predUp[i], pal.fit, 1.2 * s, true);
            p.Polyline(predLo[i], pal.fit, 1.2 * s, true);
        }
    for (const auto& c : curves) p.Polyline(c, pal.fit, 2.2 * s, false);

    // Points.
    const double r = 3.6 * s;
    bool anyExcluded = false, anyOutlier = false;
    for (size_t i = 0; i < m_model.x.size(); ++i) {
        const double tx = Tx(m_model.x[i]), ty = Ty(m_model.y[i]);
        if (!std::isfinite(tx) || !std::isfinite(ty)) continue;
        const double X = px(tx), Y = py(ty);
        if (X < L.left - 10 * s || X > L.left + L.width + 10 * s || Y < L.top - 10 * s || Y > L.top + L.height + 10 * s)
            continue;
        const bool ex = i < m_model.excluded.size() && m_model.excluded[i];
        if (ex) {
            anyExcluded = true;
            p.Marker(X, Y, r, pal.bg, pal.excluded, 1.3 * s, true);
            p.Line(X - r * 0.7, Y - r * 0.7, X + r * 0.7, Y + r * 0.7, pal.excluded, 1.2 * s, false);
            p.Line(X - r * 0.7, Y + r * 0.7, X + r * 0.7, Y - r * 0.7, pal.excluded, 1.2 * s, false);
        } else {
            p.Marker(X, Y, r, pal.point, pal.pointEdge, 1 * s, true);
            if (i < m_model.robustWeight.size() && m_model.robustWeight[i] < 0.1) {
                anyOutlier = true;
                p.Marker(X, Y, r + 3 * s, pal.bg, pal.outlier, 1.5 * s, false);
            }
        }
        if (!exportStyle && (int)i == m_hover) p.Marker(X, Y, r + 4 * s, pal.bg, pal.hover, 1.5 * s, false);
    }
    p.Unclip();

    // Residual plot.
    const double axisBottom = L.rHeight > 0 ? L.rTop + L.rHeight : L.top + L.height;
    if (L.rHeight > 0) {
        plotFrame(L.rTop, L.rHeight);
        double rs = 1;
        for (double t : NiceTicks(rlo, rhi, 3, rs)) {
            if (m_opt.grid) p.Line(L.left, ry(t), L.left + L.width, ry(t), pal.grid, 1 * s, false);
            p.Line(L.left - 4 * s, ry(t), L.left, ry(t), pal.axis, 1 * s, false);
            p.Text(FormatTick(t, rs), L.left - 7 * s, ry(t), 2, 1, pal.text, font, false, false);
        }
        p.Clip(L.left, L.rTop, L.width, L.rHeight);
        p.Line(L.left, ry(0), L.left + L.width, ry(0), pal.fit, 1.2 * s, false);
        for (size_t i = 0; i < m_model.x.size(); ++i) {
            const double tx = Tx(m_model.x[i]);
            const double res = m_model.y[i] - m_model.fit(m_model.x[i]);
            if (!std::isfinite(tx) || !std::isfinite(res)) continue;
            const bool ex = i < m_model.excluded.size() && m_model.excluded[i];
            const double X = px(tx), Y = ry(res);
            if (ex) p.Marker(X, Y, r * 0.8, pal.bg, pal.excluded, 1.2 * s, true);
            else p.Marker(X, Y, r * 0.8, pal.point, pal.pointEdge, 1 * s, true);
            if (!exportStyle && (int)i == m_hover) p.Marker(X, Y, r + 3 * s, pal.bg, pal.hover, 1.5 * s, false);
        }
        p.Unclip();
        p.Text(L"Residual", 12 * s, L.rTop + L.rHeight / 2, 1, 0, pal.dim, font, false, true);
    }
    for (double t : xt) {
        p.Line(px(t), axisBottom, px(t), axisBottom + 4 * s, pal.axis, 1 * s, false);
        p.Text(xLabel(t), px(t), axisBottom + 7 * s, 1, 0, pal.text, font, false, false);
    }

    // Axis titles.
    p.Text(m_model.xTitle + (m_opt.logX ? L"  (log scale)" : L""), L.left + L.width / 2, h - 8 * s, 1, 2, pal.text,
           13 * s, true, false);
    p.Text(m_model.yTitle + (m_opt.logY ? L"  (log scale)" : L""), 12 * s, L.top + L.height / 2, 1, 0, pal.text,
           13 * s, true, true);

    // Legend.
    struct Item {
        int kind;  // 0 point, 1 line, 2 band, 3 dashed, 4 excluded, 5 outlier
        std::wstring text;
    };
    std::vector<Item> items;
    items.push_back({0, L"Data"});
    if (anyExcluded) items.push_back({4, L"Left out"});
    if (anyOutlier) items.push_back({5, L"Outlier (ignored by the robust fit)"});
    if (m_model.fit) {
        items.push_back({1, m_model.fitLabel.empty() ? L"Fit" : m_model.fitLabel});
        const std::wstring pct = std::to_wstring(m_model.confidencePercent) + L"%";
        if (m_opt.confidenceBand && !confUp.empty()) items.push_back({2, pct + L" confidence band"});
        if (m_opt.predictionBand && !predUp.empty()) items.push_back({3, pct + L" prediction band"});
    }
    double lw = 0;
    for (const Item& it : items) lw = std::max(lw, p.TextWidth(it.text, font, false));
    const double rowH = font * 1.5, sw = 22 * s;
    const double boxW = lw + sw + 22 * s, boxH = rowH * items.size() + 10 * s;
    // The corner with the fewest points (top right first).
    double bx = L.left + L.width - boxW - 8 * s, by = L.top + 8 * s;
    {
        const double xs2[2] = {L.left + L.width - boxW - 8 * s, L.left + 8 * s};
        const double ys2[2] = {L.top + 8 * s, L.top + L.height - boxH - 8 * s};
        int bestCount = -1;
        for (double cy : ys2)
            for (double cx : xs2) {
                int count = 0;
                for (size_t i = 0; i < m_model.x.size(); ++i) {
                    const double X = px(Tx(m_model.x[i])), Y = py(Ty(m_model.y[i]));
                    if (X >= cx - r && X <= cx + boxW + r && Y >= cy - r && Y <= cy + boxH + r) ++count;
                }
                for (const auto& c : curves)
                    for (size_t k = 0; k < c.size(); k += 4)
                        if (c[k].x >= cx && c[k].x <= cx + boxW && c[k].y >= cy && c[k].y <= cy + boxH) ++count;
                if (bestCount < 0 || count < bestCount) {
                    bestCount = count;
                    bx = cx;
                    by = cy;
                }
            }
    }
    if (boxW < L.width * 0.8 && boxH < L.height * 0.8) {
        p.FillPolygon({{bx, by}, {bx + boxW, by}, {bx + boxW, by + boxH}, {bx, by + boxH}}, pal.legendBg, 225);
        p.Rect(bx, by, boxW, boxH, pal.grid, 1 * s);
        double yy = by + 5 * s + rowH / 2;
        for (const Item& it : items) {
            const double gx = bx + 8 * s, cx = gx + sw / 2;
            switch (it.kind) {
                case 0: p.Marker(cx, yy, r, pal.point, pal.pointEdge, 1 * s, true); break;
                case 1: p.Line(gx, yy, gx + sw, yy, pal.fit, 2.2 * s, false); break;
                case 2: p.FillRect(gx, yy - 5 * s, sw, 10 * s, RGB((GetRValue(pal.band) + 2 * GetRValue(pal.legendBg)) / 3,
                                                               (GetGValue(pal.band) + 2 * GetGValue(pal.legendBg)) / 3,
                                                               (GetBValue(pal.band) + 2 * GetBValue(pal.legendBg)) / 3));
                    break;
                case 3: p.Line(gx, yy, gx + sw, yy, pal.fit, 1.2 * s, true); break;
                case 4:
                    p.Marker(cx, yy, r, pal.legendBg, pal.excluded, 1.3 * s, true);
                    p.Line(cx - r * 0.7, yy - r * 0.7, cx + r * 0.7, yy + r * 0.7, pal.excluded, 1.2 * s, false);
                    p.Line(cx - r * 0.7, yy + r * 0.7, cx + r * 0.7, yy - r * 0.7, pal.excluded, 1.2 * s, false);
                    break;
                case 5:
                    p.Marker(cx, yy, r, pal.point, pal.pointEdge, 1 * s, true);
                    p.Marker(cx, yy, r + 3 * s, pal.legendBg, pal.outlier, 1.5 * s, false);
                    break;
            }
            p.Text(it.text, gx + sw + 8 * s, yy, 0, 1, pal.text, font, false, false);
            yy += rowH;
        }
    }

    // Box being dragged for zooming.
    if (!exportStyle && m_boxZoom && m_dragging) {
        const double x0 = std::min(m_downPt.x, m_curPt.x), y0 = std::min(m_downPt.y, m_curPt.y);
        const double ww = std::abs(m_curPt.x - m_downPt.x), hh = std::abs(m_curPt.y - m_downPt.y);
        p.FillPolygon({{x0, y0}, {x0 + ww, y0}, {x0 + ww, y0 + hh}, {x0, y0 + hh}}, pal.point, 40);
        p.Rect(x0, y0, ww, hh, pal.point, 1 * s);
    }
}

void Chart::Paint(HDC hdc) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        GdiPainter p(g);
        Render(p, rc.right, rc.bottom, m_dpi / 96.0, false);
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ===========================================================================
// Export
// ===========================================================================
namespace {
bool PngEncoder(CLSID& clsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    auto* codecs = (ImageCodecInfo*)buf.data();
    GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    return false;
}
}  // namespace

std::string Chart::PngBytes(int width, int height) const {
    Bitmap bmp(width, height, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        GdiPainter p(g);
        Render(p, width, height, std::min(std::max(width / 900.0, 1.0), 4.0), true);
    }
    CLSID png;
    if (!PngEncoder(png)) return {};
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return {};
    std::string out;
    if (bmp.Save(stream, &png, nullptr) == Ok) {
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        out.resize((size_t)st.cbSize.QuadPart);
        LARGE_INTEGER zero{};
        stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        ULONG read = 0;
        stream->Read(out.data(), (ULONG)out.size(), &read);
        out.resize(read);
    }
    stream->Release();
    return out;
}

bool Chart::SavePng(const std::wstring& path, int width, int height) const {
    const std::string bytes = PngBytes(width, height);
    if (bytes.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
    CloseHandle(f);
    return ok;
}

bool Chart::SaveSvg(const std::wstring& path, int width, int height) const {
    SvgPainter p;
    p.out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
            std::to_string(width) + "\" height=\"" + std::to_string(height) + "\" viewBox=\"0 0 " +
            std::to_string(width) + " " + std::to_string(height) +
            "\" font-family=\"Segoe UI, Arial, sans-serif\">\n";
    Render(p, width, height, std::min(std::max(width / 900.0, 1.0), 4.0), true);
    p.out += "</svg>\n";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, p.out.data(), (DWORD)p.out.size(), &written, nullptr) && written == p.out.size();
    CloseHandle(f);
    return ok;
}

bool Chart::CopyToClipboard(HWND owner, int width, int height) const {
    Bitmap bmp(width, height, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        GdiPainter p(g);
        Render(p, width, height, std::min(std::max(width / 900.0, 1.0), 4.0), true);
    }
    HBITMAP hbm = nullptr;
    if (bmp.GetHBITMAP(Color(255, 255, 255), &hbm) != Ok || !hbm) return false;
    if (!OpenClipboard(owner)) {
        DeleteObject(hbm);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_BITMAP, hbm) != nullptr;
    CloseClipboard();
    if (!ok) DeleteObject(hbm);
    return ok;
}

// ===========================================================================
// Interaction
// ===========================================================================
int Chart::HitPoint(POINT pt) const {
    if (!HasData()) return -1;
    const Layout& L = m_lastLayout;
    const double s = L.scale > 0 ? L.scale : 1;
    const View& v = m_view;
    double rlo = -1, rhi = 1;
    if (L.rHeight > 0) ResidualRange(rlo, rhi);
    int best = -1;
    double bestD = (9 * s) * (9 * s);
    for (size_t i = 0; i < m_model.x.size(); ++i) {
        const double tx = Tx(m_model.x[i]), ty = Ty(m_model.y[i]);
        if (!std::isfinite(tx)) continue;
        const double X = L.left + (tx - v.x0) / (v.x1 - v.x0) * L.width;
        if (std::isfinite(ty) && pt.y < L.top + L.height + 4 * s) {
            const double Y = L.top + (1 - (ty - v.y0) / (v.y1 - v.y0)) * L.height;
            const double d = (X - pt.x) * (X - pt.x) + (Y - pt.y) * (Y - pt.y);
            if (d < bestD) {
                bestD = d;
                best = (int)i;
            }
        } else if (L.rHeight > 0 && m_model.fit && pt.y >= L.rTop - 4 * s) {
            const double res = m_model.y[i] - m_model.fit(m_model.x[i]);
            if (!std::isfinite(res)) continue;
            const double Y = L.rTop + (1 - (res - rlo) / (rhi - rlo)) * L.rHeight;
            const double d = (X - pt.x) * (X - pt.x) + (Y - pt.y) * (Y - pt.y);
            if (d < bestD) {
                bestD = d;
                best = (int)i;
            }
        }
    }
    return best;
}

LRESULT CALLBACK Chart::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Chart* self;
    if (msg == WM_NCCREATE) {
        self = (Chart*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (Chart*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Chart::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            Paint(hdc);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: InvalidateRect(m_hwnd, nullptr, FALSE); return 0;
        case WM_DPICHANGED_AFTERPARENT:
            m_dpi = GetWindowDpi(m_hwnd);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(m_hwnd);
            SetCapture(m_hwnd);
            m_down = true;
            m_dragging = false;
            m_boxZoom = (wp & MK_CONTROL) || (wp & MK_SHIFT);
            m_downPt = m_curPt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            m_downView = m_view;
            return 0;
        case WM_MOUSEMOVE: {
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
            TrackMouseEvent(&tme);
            if (m_down) {
                m_curPt = pt;
                if (!m_dragging && (std::abs(pt.x - m_downPt.x) > 3 || std::abs(pt.y - m_downPt.y) > 3)) m_dragging = true;
                if (m_dragging && !m_boxZoom && m_lastLayout.width > 0) {
                    const double dx = (pt.x - m_downPt.x) / m_lastLayout.width * (m_downView.x1 - m_downView.x0);
                    const double dy = (pt.y - m_downPt.y) / m_lastLayout.height * (m_downView.y1 - m_downView.y0);
                    m_view = m_downView;
                    m_view.x0 -= dx;
                    m_view.x1 -= dx;
                    m_view.y0 += dy;
                    m_view.y1 += dy;
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }
            const int hit = HitPoint(pt);
            if (hit != m_hover) {
                m_hover = hit;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            if (onHover) {
                std::wstring text;
                const Layout& L = m_lastLayout;
                if (hit >= 0) {
                    const double x = m_model.x[(size_t)hit], y = m_model.y[(size_t)hit];
                    text = L"Point " + std::to_wstring(hit + 1) + L":  x = " + FormatNumber(x) + L",  y = " + FormatNumber(y);
                    if (m_model.fit) {
                        const double f = m_model.fit(x);
                        text += L",  fit = " + FormatNumber(f) + L",  residual = " + FormatNumber(y - f);
                    }
                    const bool ex = (size_t)hit < m_model.excluded.size() && m_model.excluded[(size_t)hit];
                    text += ex ? L"   (left out: click to use it again)" : L"   (click to leave it out)";
                } else if (HasData() && pt.x >= L.left && pt.x <= L.left + L.width && pt.y >= L.top &&
                           pt.y <= L.top + L.height) {
                    const double tx = m_view.x0 + (pt.x - L.left) / L.width * (m_view.x1 - m_view.x0);
                    const double ty = m_view.y1 - (pt.y - L.top) / L.height * (m_view.y1 - m_view.y0);
                    text = L"x = " + FormatNumber(Ux(tx), 5) + L",  y = " + FormatNumber(Uy(ty), 5);
                    if (m_model.fit) text += L"   (fit at this x: " + FormatNumber(m_model.fit(Ux(tx)), 5) + L")";
                }
                onHover(text);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (m_hover >= 0) {
                m_hover = -1;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            if (onHover) onHover(L"");
            return 0;
        case WM_LBUTTONUP: {
            if (!m_down) return 0;
            m_down = false;
            ReleaseCapture();
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (m_dragging && m_boxZoom && m_lastLayout.width > 0) {
                const Layout& L = m_lastLayout;
                const double ax = m_view.x0 + (std::min(pt.x, m_downPt.x) - L.left) / L.width * (m_view.x1 - m_view.x0);
                const double bx = m_view.x0 + (std::max(pt.x, m_downPt.x) - L.left) / L.width * (m_view.x1 - m_view.x0);
                const double ay = m_view.y1 - (std::max(pt.y, m_downPt.y) - L.top) / L.height * (m_view.y1 - m_view.y0);
                const double by = m_view.y1 - (std::min(pt.y, m_downPt.y) - L.top) / L.height * (m_view.y1 - m_view.y0);
                if (std::abs(pt.x - m_downPt.x) > 5 && std::abs(pt.y - m_downPt.y) > 5) m_view = {ax, bx, ay, by};
            } else if (!m_dragging) {
                const int hit = HitPoint(pt);
                if (hit >= 0 && onTogglePoint) onTogglePoint(hit);
            }
            m_dragging = false;
            m_boxZoom = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CAPTURECHANGED:
            m_down = m_dragging = m_boxZoom = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDBLCLK:
            if (HitPoint({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) < 0) ResetView();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(m_hwnd, &pt);
            const Layout& L = m_lastLayout;
            if (L.width <= 0) return 0;
            const double f = std::pow(1.2, -(double)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
            const bool onlyX = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) || pt.y > L.top + L.height;
            const bool onlyY = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
            const double cx = m_view.x0 + (pt.x - L.left) / L.width * (m_view.x1 - m_view.x0);
            const double cy = m_view.y1 - (std::min((double)pt.y, L.top + L.height) - L.top) / L.height * (m_view.y1 - m_view.y0);
            if (!onlyY) {
                m_view.x0 = cx + (m_view.x0 - cx) * f;
                m_view.x1 = cx + (m_view.x1 - cx) * f;
            }
            if (!onlyX) {
                m_view.y0 = cy + (m_view.y0 - cy) * f;
                m_view.y1 = cy + (m_view.y1 - cy) * f;
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CONTEXTMENU:
            if (onContextMenu) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                if (pt.x == -1 && pt.y == -1) {
                    RECT rc;
                    GetWindowRect(m_hwnd, &rc);
                    pt = {rc.left + 40, rc.top + 40};
                }
                onContextMenu(pt);
            }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_HOME) {
                ResetView();
                return 0;
            }
            if (wp == VK_ADD || wp == VK_OEM_PLUS) {
                Zoom(1.25);
                return 0;
            }
            if (wp == VK_SUBTRACT || wp == VK_OEM_MINUS) {
                Zoom(0.8);
                return 0;
            }
            break;
        case WM_GETDLGCODE: return DLGC_WANTARROWS;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
