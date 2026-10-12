// Plot.cpp - see Plot.h.
#include "Plot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>

PlotStyle StyleOf(const SheetSetup& s) {
    PlotStyle p;
    p.colourMode = s.colourMode;
    p.lineWeights = s.lineWeights;
    p.pens = s.pens;
    return p;
}

namespace {
// Drawing point -> paper point: (p - from) * k + to.
struct Map {
    Vec2 from, to;
    double k = 1;
    Vec2 operator()(Vec2 p) const { return (p - from) * k + to; }
};

class Plotter {
public:
    Plotter(PlotPage& page, const PlotStyle& style) : m_page(page), m_style(style) {}

    // The visible, printable objects of `doc` through `map`, clipped to `clip`
    // (-1: none); only those touching `cull` (drawing units) when given.
    void Objects(const Document& doc, const Map& map, int clip, const Box* cull, const FieldValues* fields) {
        m_doc = &doc;
        m_map = map;
        m_clip = clip;
        m_fields = fields;
        for (const auto& kv : doc.Entities()) {
            const Entity& e = kv.second;
            if (!Printable(e)) continue;
            if (cull && !Bounds(e).Intersects(*cull)) continue;
            Object(e, nullptr, 0);
        }
    }

private:
    bool Printable(const Entity& e) const {
        if (e.construction || e.type == EntityType::Viewport || !m_doc->Visible(e)) return false;
        const Layer* l = m_doc->FindLayer(e.layer);
        return !l || l->plot;
    }

    void Object(const Entity& e, const Entity* block, int depth) {
        if (depth > 16) return;
        if (e.type == EntityType::Array || e.type == EntityType::Insert) {
            std::vector<Entity> parts;
            if (e.type == EntityType::Array) ExpandArray(e, parts);
            else ExpandBlock(e, parts, 0, false);
            for (const Entity& x : parts)
                if (Printable(x)) Object(x, e.type == EntityType::Insert ? &e : block, depth + 1);
            return;
        }
        Pen(e, block);
        switch (e.type) {
            case EntityType::Dimension: {
                if (!e.dim) return;
                DimGraphics g;
                DimLayout(*e.dim, g);
                Prims(g.lines);
                for (const auto& a : g.arrows) Fill({{a.begin(), a.end()}});
                if (!g.text.empty()) {
                    TextItem t;
                    t.text = g.text;
                    t.pos = g.textPos;
                    t.angle = g.textAngle;
                    t.height = g.textHeight;
                    t.width = g.textWidth;
                    t.align = 1;
                    t.font = L"Arial";
                    Text(t);
                }
                return;
            }
            case EntityType::Text:
            case EntityType::Leader:
            case EntityType::Hatch: {
                AnnotGraphics g;
                AnnotLayout(e, m_fields, g);
                const bool dashed = m_dash;
                if (e.type == EntityType::Hatch) m_dash = false;  // pattern lines are drawn whole
                Prims(g.lines);
                m_dash = dashed;
                if (g.solid) Fill(g.fills);
                else
                    for (const auto& f : g.fills) Fill({f});
                for (const TextItem& t : g.texts) Text(t);
                return;
            }
            case EntityType::Point: {
                PlotItem it = Base(PlotItem::Path);
                it.pts = {m_map(e.p0), m_map(e.p0)};
                it.width = std::max(it.width, 0.3);
                m_page.items.push_back(std::move(it));
                return;
            }
            default: {
                std::vector<Prim> prims;
                Primitives(e, prims);
                Prims(prims, e.closed || e.type == EntityType::Circle);
                return;
            }
        }
    }

    // Colour, width and dashes of the object.
    void Pen(const Entity& e, const Entity* block) {
        const Look k = m_doc->LookOf(e, block);
        uint32_t rgb = k.foreground ? 0 : k.rgb;
        if (m_style.colourMode == 1) {
            rgb = 0;
        } else if (m_style.colourMode == 2) {
            const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
            const int y = std::min(200, (int)std::lround(0.299 * r + 0.587 * g + 0.114 * b));  // never too faint
            rgb = (uint32_t)((y << 16) | (y << 8) | y);
        }
        int lw = k.lineweight;
        if (k.color >= 1 && k.color <= 9 && m_style.pens[(size_t)k.color] >= 0) lw = m_style.pens[(size_t)k.color];
        m_rgb = rgb;
        m_width = m_style.lineWeights ? std::max(0, lw) / 100.0 : 0;
        if (m_width > 0 && m_width < 0.13) m_width = 0.13;
        m_pattern.clear();
        m_dash = false;
        if (k.linetype > 0 && (size_t)k.linetype < LineTypes().size()) {
            for (double d : LineTypes()[(size_t)k.linetype].pattern) m_pattern.push_back(d * k.ltscale);
            m_dash = !m_pattern.empty();
        }
    }

    PlotItem Base(PlotItem::Kind kind) const {
        PlotItem it;
        it.kind = kind;
        it.rgb = m_rgb;
        it.width = m_width;
        it.clip = m_clip;
        return it;
    }

    // Curve pieces joined into paths (arcs as short straight pieces).
    void Prims(const std::vector<Prim>& prims, bool closed = false) {
        std::vector<Vec2> path;
        auto flush = [&](bool close) {
            if (path.size() >= 2) Path(path, close);
            path.clear();
        };
        for (const Prim& p : prims) {
            const Vec2 a = m_map(p.a);
            if (!path.empty() && !Near(path.back(), a, 1e-6)) flush(false);
            if (path.empty()) path.push_back(a);
            if (p.kind == Prim::Segment) {
                path.push_back(m_map(p.b));
                continue;
            }
            // Within 0.01 mm of the true arc on paper.
            const double rp = p.r * m_map.k;
            const double step = rp > 0.01 ? 2 * std::acos(std::max(0.0, 1 - 0.01 / rp)) : kPi / 2;
            const int n = (int)std::clamp(std::ceil(p.sweep / std::max(step, 1e-4)), 2.0, 4000.0);
            for (int i = 1; i <= n; ++i) path.push_back(m_map(i == n ? p.b : Polar(p.c, p.r, p.start + p.sweep * i / n)));
        }
        if (closed && path.size() > 2 && Near(path.front(), path.back(), 1e-6)) {
            path.pop_back();
            flush(true);
        } else {
            flush(false);
        }
    }

    // A path on paper, cut into the line type's dashes.
    void Path(const std::vector<Vec2>& pts, bool closed) {
        if (!m_dash) {
            PlotItem it = Base(PlotItem::Path);
            it.pts = pts;
            it.closed = closed;
            m_page.items.push_back(std::move(it));
            return;
        }
        std::vector<Vec2> all = pts;
        if (closed) all.push_back(pts.front());
        double total = 0;
        for (double d : m_pattern) total += std::fabs(d);
        if (total < 0.5) {  // too fine to show: solid
            PlotItem it = Base(PlotItem::Path);
            it.pts = pts;
            it.closed = closed;
            m_page.items.push_back(std::move(it));
            return;
        }
        size_t index = 0;
        double left = std::fabs(m_pattern[0]);
        bool on = m_pattern[0] >= 0;
        PlotItem cur = Base(PlotItem::Path);
        auto emit = [&]() {
            if (cur.pts.size() >= 2) m_page.items.push_back(cur);
            cur.pts.clear();
        };
        for (size_t i = 0; i + 1 < all.size(); ++i) {
            const Vec2 a = all[i], b = all[i + 1];
            const double len = Dist(a, b);
            double at = 0;
            while (at < len - 1e-12) {
                const double step = std::min(left, len - at);
                const Vec2 p = Lerp(a, b, at / len), q = Lerp(a, b, (at + step) / len);
                if (on) {
                    if (cur.pts.empty()) cur.pts.push_back(p);
                    cur.pts.push_back(q);
                }
                at += step;
                left -= step;
                if (left <= 1e-9) {
                    if (on) emit();
                    index = (index + 1) % m_pattern.size();
                    const double d = m_pattern[index];
                    if (d == 0) {  // a dot
                        PlotItem dot = Base(PlotItem::Path);
                        dot.pts = {q, q};
                        m_page.items.push_back(dot);
                        index = (index + 1) % m_pattern.size();
                    }
                    left = std::fabs(m_pattern[index]);
                    on = m_pattern[index] > 0;
                    if (left <= 1e-9) left = 1e-6;
                }
            }
        }
        if (on) emit();
    }

    void Fill(const std::vector<std::vector<Vec2>>& loops) {
        PlotItem it = Base(PlotItem::Fill);
        for (const auto& l : loops) {
            if (l.size() < 3) continue;
            std::vector<Vec2> m;
            for (Vec2 p : l) m.push_back(m_map(p));
            it.loops.push_back(std::move(m));
        }
        if (!it.loops.empty()) m_page.items.push_back(std::move(it));
    }

    void Text(TextItem t) {
        if (t.text.empty()) return;
        PlotItem it = Base(PlotItem::Text);
        t.pos = m_map(t.pos);
        t.height *= m_map.k;
        t.width *= m_map.k;
        it.text = std::move(t);
        m_page.items.push_back(std::move(it));
    }

    PlotPage& m_page;
    const PlotStyle& m_style;
    const Document* m_doc = nullptr;
    Map m_map;
    int m_clip = -1;
    const FieldValues* m_fields = nullptr;
    uint32_t m_rgb = 0;
    double m_width = 0;
    std::vector<double> m_pattern;
    bool m_dash = false;
};
}  // namespace

void PlotSheet(const Sheet& sheet, const Document& model, const FieldValues& fields, PlotPage& out, const PlotStyle* given) {
    out = PlotPage{};
    out.name = sheet.setup.name;
    out.width = sheet.setup.width;
    out.height = sheet.setup.height;
    const PlotStyle style = given ? *given : StyleOf(sheet.setup);
    Plotter p(out, style);
    // What each viewport shows, cut off at its edges.
    for (const auto& kv : sheet.doc.Entities()) {
        const Entity& vp = kv.second;
        if (vp.type != EntityType::Viewport || !vp.annot || !sheet.doc.Visible(vp)) continue;
        Map m;
        m.from = vp.annot->viewCentre;
        m.to = Lerp(vp.p0, vp.p1, 0.5);
        m.k = vp.annot->viewScale;
        out.clips.push_back(Box(vp.p0, vp.p1));
        const Box cull = ViewportModelBox(vp);
        p.Objects(model, m, (int)out.clips.size() - 1, &cull, &fields);
    }
    p.Objects(sheet.doc, Map{}, -1, nullptr, &fields);
}

void PlotDrawing(const Document& model, const Box& area, double paperW, double paperH, double margin, double scale,
                 const PlotStyle& style, const FieldValues& fields, PlotPage& out) {
    out = PlotPage{};
    out.name = L"Drawing";
    out.width = paperW;
    out.height = paperH;
    if (area.Empty()) return;
    const double w = std::max(1.0, paperW - 2 * margin), h = std::max(1.0, paperH - 2 * margin);
    double k = scale;
    if (!(k > 0)) {
        const double aw = std::max(area.Width(), 1e-9), ah = std::max(area.Height(), 1e-9);
        k = std::min(w / aw, h / ah);
    }
    Map m;
    m.from = area.Center();
    m.to = {paperW / 2, paperH / 2};
    m.k = k;
    out.clips.push_back(Box(Vec2{margin, margin}, Vec2{paperW - margin, paperH - margin}));
    Plotter p(out, style);
    const Box cull = Box(m.from - Vec2{w, h} / (2 * k), m.from + Vec2{w, h} / (2 * k));
    p.Objects(model, m, 0, &cull, &fields);
}

// ---------------------------------------------------------------------------
// PDF
// ---------------------------------------------------------------------------

namespace {
constexpr double kPt = 72 / 25.4;  // points per millimetre

std::string Num(double v) {
    // Three decimals, no exponent, no locale.
    long long n = std::llround(v * 1000);
    const bool neg = n < 0;
    if (neg) n = -n;
    std::string s = std::to_string(n / 1000);
    const long long f = n % 1000;
    if (f) {
        char buf[8];
        std::snprintf(buf, sizeof buf, ".%03lld", f);
        std::string frac = buf;
        while (frac.back() == '0') frac.pop_back();
        s += frac;
    }
    return neg && (n != 0) ? "-" + s : s;
}

std::string Colour(uint32_t rgb, const char* op) {
    return Num(((rgb >> 16) & 255) / 255.0) + " " + Num(((rgb >> 8) & 255) / 255.0) + " " + Num((rgb & 255) / 255.0) + " " +
           op + "\n";
}

// Text in the PDF's standard fonts (Windows character set); other characters become ?.
std::string PdfString(const std::wstring& s) {
    std::string out = "(";
    for (wchar_t c : s) {
        unsigned code;
        if (c < 128) code = (unsigned)c;
        else if (c >= 0xA0 && c <= 0xFF) code = (unsigned)c;
        else if (c == 0x2300) code = 0xD8;  // diameter: Ø
        else if (c == 0x2022) code = 0x95;
        else if (c == 0x2013) code = 0x96;
        else if (c == 0x2014) code = 0x97;
        else if (c == 0x2026) code = 0x85;
        else if (c == 0x20AC) code = 0x80;
        else code = '?';
        if (code == '(' || code == ')' || code == '\\') out += '\\';
        if (code < 32) code = ' ';
        out += (char)code;
    }
    return out + ")";
}

int FontOf(const std::wstring& font, double& capHeight) {
    std::wstring f = font;
    for (wchar_t& c : f) c = (wchar_t)towlower(c);
    const bool serif = f.find(L"serif") != std::wstring::npos && f.find(L"sans") == std::wstring::npos;
    if (serif || f.find(L"times") != std::wstring::npos || f.find(L"georgia") != std::wstring::npos ||
        f.find(L"cambria") != std::wstring::npos) {
        capHeight = 0.662;
        return 2;
    }
    if (f.find(L"courier") != std::wstring::npos || f.find(L"consolas") != std::wstring::npos || f.find(L"mono") != std::wstring::npos) {
        capHeight = 0.571;
        return 3;
    }
    capHeight = 0.718;
    return 1;
}

std::string PageContent(const PlotPage& page) {
    std::string c = "1 J 1 j\n";
    c += Num(kPt) + " 0 0 " + Num(kPt) + " 0 0 cm\n";  // millimetres from here on
    int clip = -1;
    bool open = false;
    for (const PlotItem& it : page.items) {
        if (it.clip != clip) {
            if (open) c += "Q\n";
            open = false;
            clip = it.clip;
            if (clip >= 0 && (size_t)clip < page.clips.size()) {
                const Box& b = page.clips[(size_t)clip];
                c += "q " + Num(b.minX) + " " + Num(b.minY) + " " + Num(b.Width()) + " " + Num(b.Height()) + " re W n\n";
                open = true;
            }
        }
        switch (it.kind) {
            case PlotItem::Path: {
                if (it.pts.size() < 2) break;
                c += Colour(it.rgb, "RG");
                c += Num(it.width) + " w\n";
                c += Num(it.pts[0].x) + " " + Num(it.pts[0].y) + " m\n";
                for (size_t i = 1; i < it.pts.size(); ++i) c += Num(it.pts[i].x) + " " + Num(it.pts[i].y) + " l\n";
                c += it.closed ? "h S\n" : "S\n";
                break;
            }
            case PlotItem::Fill:
                c += Colour(it.rgb, "rg");
                for (const auto& l : it.loops) {
                    c += Num(l[0].x) + " " + Num(l[0].y) + " m\n";
                    for (size_t i = 1; i < l.size(); ++i) c += Num(l[i].x) + " " + Num(l[i].y) + " l\n";
                    c += "h\n";
                }
                c += "f*\n";
                break;
            case PlotItem::Text: {
                const TextItem& t = it.text;
                double cap;
                const int font = FontOf(t.font, cap);
                const double size = t.height / cap;
                const Vec2 u{std::cos(t.angle), std::sin(t.angle)}, n = Perp(u);
                const double shift = t.align == 0 ? 0 : t.align == 1 ? -t.width / 2 : -t.width;
                const Vec2 at = t.pos + u * shift - n * (t.height / 2);  // on the baseline
                c += Colour(it.rgb, "rg");
                c += "BT /F" + std::to_string(font) + " " + Num(size) + " Tf " + Num(100 * (t.widthFactor > 0 ? t.widthFactor : 1)) +
                     " Tz " + Num(u.x) + " " + Num(u.y) + " " + Num(-u.y) + " " + Num(u.x) + " " + Num(at.x) + " " + Num(at.y) +
                     " Tm " + PdfString(t.text) + " Tj ET\n";
                break;
            }
        }
    }
    if (open) c += "Q\n";
    return c;
}
}  // namespace

std::string WritePdf(const std::vector<PlotPage>& pages, const std::wstring& title) {
    std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets;
    auto object = [&](const std::string& body) {
        offsets.push_back(out.size());
        out += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    const size_t n = pages.size();
    // 1 catalog, 2 page tree, 3-5 fonts, 6 information, then a page and its content each.
    object("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (size_t i = 0; i < n; ++i) kids += std::to_string(7 + 2 * i) + " 0 R ";
    object("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(n) + " >>");
    for (const char* f : {"Helvetica", "Times-Roman", "Courier"})
        object(std::string("<< /Type /Font /Subtype /Type1 /BaseFont /") + f + " /Encoding /WinAnsiEncoding >>");
    object("<< /Title " + PdfString(title) + " /Producer (Feather CAD) /Creator (Feather CAD) >>");
    for (size_t i = 0; i < n; ++i) {
        const PlotPage& p = pages[i];
        object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + Num(p.width * kPt) + " " + Num(p.height * kPt) +
               "] /Resources << /Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R >> >> /Contents " + std::to_string(8 + 2 * i) +
               " 0 R >>");
        const std::string content = PageContent(p);
        object("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
    }
    const size_t xref = out.size();
    out += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        out += buf;
    }
    out += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R /Info 6 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return out;
}
