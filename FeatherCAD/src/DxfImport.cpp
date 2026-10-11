// DxfImport.cpp - see DxfImport.h.
#include "DxfImport.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <set>

namespace {
constexpr int kMaxDepth = 16;          // blocks inside blocks
constexpr size_t kMaxObjects = 2000000;

struct Group {
    int code;
    std::string value;
};

// One DXF entity: its groups in order (and the vertices of an old-style polyline).
struct Raw {
    std::string type;
    std::vector<Group> g;
    std::vector<Raw> sub;

    const Group* Find(int code) const {
        for (const Group& x : g)
            if (x.code == code) return &x;
        return nullptr;
    }
    bool Has(int code) const { return Find(code) != nullptr; }
    double Num(int code, double def = 0) const {
        const Group* x = Find(code);
        double v;
        return x && ParseNumber(x->value, v) ? v : def;
    }
    int Int(int code, int def = 0) const { return (int)std::lround(Num(code, def)); }
    std::string Str(int code) const {
        const Group* x = Find(code);
        return x ? x->value : std::string();
    }
    Vec2 Pt(int code, Vec2 def = {}) const { return {Num(code, def.x), Num(code + 10, def.y)}; }
    std::vector<double> All(int code) const {
        std::vector<double> v;
        for (const Group& x : g) {
            double d;
            if (x.code == code && ParseNumber(x.value, d)) v.push_back(d);
        }
        return v;
    }
};

// x' = a x + c y + tx, y' = b x + d y + ty
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Vec2 operator()(Vec2 p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    Vec2 Vec(Vec2 v) const { return {a * v.x + c * v.y, b * v.x + d * v.y}; }
    Affine operator*(const Affine& o) const {  // this after o
        Affine r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.tx = a * o.tx + c * o.ty + tx;
        r.ty = b * o.tx + d * o.ty + ty;
        return r;
    }
    double Det() const { return a * d - b * c; }
    // Uniform scale and a turn, maybe mirrored (circles stay circles).
    bool Similar(double& scale, double& rot, bool& mirror) const {
        const double s1 = std::hypot(a, b), s2 = std::hypot(c, d);
        if (s1 <= 0 || s2 <= 0) return false;
        if (std::fabs(s1 - s2) > 1e-9 * std::max(s1, s2) || std::fabs(a * c + b * d) > 1e-9 * s1 * s2) return false;
        scale = s1;
        rot = std::atan2(b, a);
        mirror = Det() < 0;
        return true;
    }
    static Affine Move(Vec2 t) {
        Affine m;
        m.tx = t.x;
        m.ty = t.y;
        return m;
    }
    static Affine Turn(double ang) {
        Affine m;
        m.a = std::cos(ang);
        m.b = std::sin(ang);
        m.c = -m.b;
        m.d = m.a;
        return m;
    }
    static Affine Scale(double sx, double sy) {
        Affine m;
        m.a = sx;
        m.d = sy;
        return m;
    }
};

struct Block {
    Vec2 base;
    std::vector<Raw> ents;
};

std::string TrimRight(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}
std::string Trim(std::string s) {
    s = TrimRight(std::move(s));
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
    return s.substr(b);
}

class Importer {
public:
    Importer(DxfResult& out) : m_out(out) {}

    bool Parse(const std::string& text, std::wstring& error) {
        if (text.compare(0, 18, "AutoCAD Binary DXF") == 0) {
            error = L"This is a binary DXF file. Save it as an ASCII DXF and open that.";
            return false;
        }
        // The file as (code, value) pairs.
        std::vector<std::string> lines;
        {
            size_t start = 0;
            while (start <= text.size()) {
                size_t nl = text.find('\n', start);
                if (nl == std::string::npos) nl = text.size();
                lines.push_back(text.substr(start, nl - start));
                start = nl + 1;
                if (nl == text.size()) break;
            }
        }
        if (lines.size() > 2 && lines[0].size() >= 3 && (unsigned char)lines[0][0] == 0xEF) lines[0].erase(0, 3);  // BOM
        for (size_t i = 0; i + 1 < lines.size(); i += 2) {
            const std::string code = Trim(lines[i]);
            if (code.empty() && i + 2 >= lines.size()) break;
            double c;
            if (!ParseNumber(code, c)) {
                error = L"This file does not look like a DXF drawing (line " + std::to_wstring(i + 1) + L").";
                return false;
            }
            std::string value = TrimRight(lines[i + 1]);
            if (c != 1 && c != 3) value = Trim(value);  // text keeps its leading spaces
            m_groups.push_back({(int)c, std::move(value)});
        }
        bool sawEntities = false;
        size_t i = 0;
        while (i < m_groups.size()) {
            if (m_groups[i].code == 0 && m_groups[i].value == "SECTION" && i + 1 < m_groups.size()) {
                const std::string name = m_groups[i + 1].value;
                i += 2;
                if (name == "HEADER") Header(i);
                else if (name == "BLOCKS") Blocks(i);
                else if (name == "ENTITIES") {
                    m_entities = Entities(i, {"ENDSEC"});
                    sawEntities = true;
                } else Skip(i);
            } else if (m_groups[i].code == 0 && m_groups[i].value == "EOF") {
                break;
            } else {
                ++i;
            }
        }
        if (!sawEntities) {
            error = L"There are no drawing objects in this file (no ENTITIES section). It may not be a DXF drawing.";
            return false;
        }
        for (const Raw& r : m_entities) Convert(r, Affine{}, 0);
        return true;
    }

private:
    void Skip(size_t& i) {
        while (i < m_groups.size() && !(m_groups[i].code == 0 && m_groups[i].value == "ENDSEC")) ++i;
        if (i < m_groups.size()) ++i;
    }

    void Header(size_t& i) {
        std::string var;
        double dimScale = 1;
        bool zin = false;
        int zinValue = 0;
        while (i < m_groups.size() && !(m_groups[i].code == 0 && m_groups[i].value == "ENDSEC")) {
            const Group& g = m_groups[i++];
            if (g.code == 9) {
                var = g.value;
                continue;
            }
            double v = 0;
            const bool isNum = ParseNumber(g.value, v);
            if (var == "$ACADVER" && g.code == 1) {
                m_utf8 = g.value >= "AC1021";  // AutoCAD 2007 and later write UTF-8
            } else if (var == "$INSUNITS" && isNum) {
                const int u = (int)v;
                const std::pair<int, Units> map[] = {{0, Units::Unitless}, {1, Units::Inches}, {2, Units::Feet},
                                                     {4, Units::Millimetres}, {5, Units::Centimetres},
                                                     {6, Units::Metres}};
                for (const auto& m : map)
                    if (m.first == u) {
                        m_out.units = m.second;
                        m_out.hasUnits = true;
                    }
            } else if (isNum && var == "$DIMTXT" && v > 0) {
                m_out.dimStyle.textHeight = v;
                m_out.hasDimStyle = true;
            } else if (isNum && var == "$DIMASZ" && v >= 0) {
                m_out.dimStyle.arrowSize = v;
            } else if (isNum && var == "$DIMEXO" && v >= 0) {
                m_out.dimStyle.extOffset = v;
            } else if (isNum && var == "$DIMEXE" && v >= 0) {
                m_out.dimStyle.extBeyond = v;
            } else if (isNum && var == "$DIMGAP") {
                m_out.dimStyle.textGap = std::fabs(v);
            } else if (isNum && var == "$DIMDEC") {
                m_out.dimStyle.precision = std::clamp((int)v, 0, 8);
            } else if (isNum && var == "$DIMZIN") {
                zin = true;
                zinValue = (int)v;
            } else if (isNum && var == "$DIMSCALE" && v > 0) {
                dimScale = v;
            }
        }
        if (i < m_groups.size()) ++i;
        if (zin) m_out.dimStyle.trimZeros = (zinValue & 8) != 0;
        if (m_out.hasDimStyle && dimScale != 1) {
            DimStyle& s = m_out.dimStyle;
            s.textHeight *= dimScale;
            s.arrowSize *= dimScale;
            s.extOffset *= dimScale;
            s.extBeyond *= dimScale;
            s.textGap *= dimScale;
        }
    }

    void Blocks(size_t& i) {
        while (i < m_groups.size() && !(m_groups[i].code == 0 && m_groups[i].value == "ENDSEC")) {
            if (m_groups[i].code == 0 && m_groups[i].value == "BLOCK") {
                Raw head;
                ++i;
                while (i < m_groups.size() && m_groups[i].code != 0) head.g.push_back(m_groups[i++]);
                Block b;
                b.base = head.Pt(10);
                b.ents = Entities(i, {"ENDBLK", "ENDSEC"});
                if (i < m_groups.size() && m_groups[i].value == "ENDBLK") {
                    ++i;
                    while (i < m_groups.size() && m_groups[i].code != 0) ++i;
                }
                std::string name = head.Str(2);
                if (name.empty()) name = head.Str(3);
                m_blocks[name] = std::move(b);
            } else {
                ++i;
            }
        }
        if (i < m_groups.size()) ++i;
    }

    // Entities up to (not including) one of the `end` markers.
    std::vector<Raw> Entities(size_t& i, const std::set<std::string>& end) {
        std::vector<Raw> list;
        while (i < m_groups.size()) {
            const Group& g = m_groups[i];
            if (g.code != 0) {
                ++i;
                continue;
            }
            if (end.count(g.value)) break;
            if (g.value == "ENDSEC" || g.value == "EOF") break;
            Raw r;
            r.type = g.value;
            ++i;
            while (i < m_groups.size() && m_groups[i].code != 0) r.g.push_back(m_groups[i++]);
            if (r.type == "POLYLINE" || (r.type == "INSERT" && r.Int(66) == 1)) {
                // Followed by VERTEX (or ATTRIB) entities up to SEQEND.
                while (i < m_groups.size() && m_groups[i].code == 0 && m_groups[i].value != "SEQEND" &&
                       !end.count(m_groups[i].value) && m_groups[i].value != "ENDSEC") {
                    Raw v;
                    v.type = m_groups[i].value;
                    ++i;
                    while (i < m_groups.size() && m_groups[i].code != 0) v.g.push_back(m_groups[i++]);
                    if (v.type == "VERTEX") r.sub.push_back(std::move(v));
                }
                if (i < m_groups.size() && m_groups[i].value == "SEQEND") {
                    ++i;
                    while (i < m_groups.size() && m_groups[i].code != 0) ++i;
                }
            }
            list.push_back(std::move(r));
        }
        return list;
    }

    // ---- text -------------------------------------------------------------------

    std::wstring Text(const std::string& s) const {
        std::wstring w;
        if (m_utf8) w = TextFromUtf8(s);
        else
            for (unsigned char c : s) w += (wchar_t)c;
        std::wstring out;
        for (size_t i = 0; i < w.size(); ++i) {
            // \U+2300 and %%c (diameter), %%d (degree), %%p (plus-minus)
            if (w.compare(i, 3, L"\\U+") == 0 && i + 7 <= w.size()) {
                const std::wstring hex = w.substr(i + 3, 4);
                wchar_t* endp = nullptr;
                const unsigned long v = std::wcstoul(hex.c_str(), &endp, 16);
                if (endp && *endp == 0) {
                    out += v == 0x2300 ? L'\x00D8' : (wchar_t)v;  // the diameter sign most fonts have
                    i += 6;
                    continue;
                }
            }
            if (w.compare(i, 2, L"%%") == 0 && i + 2 < w.size()) {
                const wchar_t k = (wchar_t)std::towlower(w[i + 2]);
                if (k == L'c') out += L'\x00D8';
                else if (k == L'd') out += L'\x00B0';
                else if (k == L'p') out += L'\x00B1';
                else if (k == L'%') out += L'%';
                else if (k == L'u' || k == L'o') {}  // underline, overline: dropped
                else {
                    out += w[i];
                    continue;
                }
                i += 2;
                continue;
            }
            out += w[i];
        }
        return out;
    }

    // Formatting codes of multi-line text (\A1; {\fArial|b0;...} \P) taken out.
    static std::wstring PlainText(const std::wstring& s) {
        std::wstring out;
        for (size_t i = 0; i < s.size(); ++i) {
            const wchar_t c = s[i];
            if (c == L'{' || c == L'}') continue;
            if (c == L'\\' && i + 1 < s.size()) {
                const wchar_t k = s[i + 1];
                if (k == L'P' || k == L'~') {
                    out += L' ';
                    ++i;
                } else if (k == L'\\' || k == L'{' || k == L'}') {
                    out += k;
                    ++i;
                } else if (k == L'S') {  // stacked: 1^2 -> 1/2
                    const size_t semi = s.find(L';', i);
                    std::wstring part = s.substr(i + 2, semi == std::wstring::npos ? std::wstring::npos : semi - i - 2);
                    for (wchar_t& x : part)
                        if (x == L'^' || x == L'#') x = L'/';
                    out += part;
                    i = semi == std::wstring::npos ? s.size() : semi;
                } else if (std::wcschr(L"ACFfHQTWpLlOoKk", k)) {
                    if (k == L'L' || k == L'l' || k == L'O' || k == L'o' || k == L'K' || k == L'k') {
                        ++i;  // on/off switches have no argument
                    } else {
                        const size_t semi = s.find(L';', i);
                        i = semi == std::wstring::npos ? s.size() : semi;
                    }
                } else {
                    out += c;
                }
                continue;
            }
            out += c;
        }
        return out;
    }

    // ---- adding objects ----------------------------------------------------------

    void Skipped(const std::string& type) { ++m_out.skipped[type]; }

    void Add(Entity e) {
        if (m_out.entities.size() < kMaxObjects) m_out.entities.push_back(std::move(e));
    }

    // The ellipse c + u cos t + v sin t (t from t0 to t1), u and v conjugate.
    void AddConjugate(Vec2 c, Vec2 u, Vec2 v, double t0, double t1, bool full) {
        const double uu = Dot(u, u), vv = Dot(v, v), uv = Dot(u, v);
        double ts = 0.5 * std::atan2(2 * uv, uu - vv);
        Vec2 A = u * std::cos(ts) + v * std::sin(ts);
        Vec2 B = v * std::cos(ts) - u * std::sin(ts);
        if (Length(A) < Length(B)) {
            ts += kPi / 2;
            const Vec2 t = A;
            A = B;
            B = -t;
        }
        if (Length(A) <= 1e-12) return;
        const double ratio = std::min(1.0, Length(B) / Length(A));
        if (ratio <= 1e-12) {
            Add(Entity::Line(c - A, c + A));
            return;
        }
        if (full) {
            Add(Entity::Ellipse(c, A, ratio));
            return;
        }
        if (Cross(A, B) >= 0) Add(Entity::EllipseArc(c, A, ratio, t0 - ts, t1 - ts));
        else Add(Entity::EllipseArc(c, A, ratio, -(t1 - ts), -(t0 - ts)));
    }

    void AddCircle(const Affine& M, Vec2 c, double r) {
        if (!(r > 0)) return;
        double s, rot;
        bool mirror;
        if (M.Similar(s, rot, mirror)) Add(Entity::Circle(M(c), r * s));
        else AddConjugate(M(c), M.Vec({r, 0}), M.Vec({0, r}), 0, kTau, true);
    }

    void AddArc(const Affine& M, Vec2 c, double r, double a0, double a1) {
        if (!(r > 0)) return;
        double s, rot;
        bool mirror;
        if (M.Similar(s, rot, mirror)) {
            if (!mirror) Add(Entity::Arc(M(c), r * s, a0 + rot, a1 + rot));
            else Add(Entity::Arc(M(c), r * s, rot - a1, rot - a0));
            return;
        }
        double sweep = SweepCCW(a0, a1);
        if (sweep <= 0) sweep = kTau;
        AddConjugate(M(c), M.Vec({r, 0}), M.Vec({0, r}), a0, a0 + sweep, false);
    }

    void AddPolyline(const Affine& M, std::vector<Vertex> verts, bool closed) {
        if (verts.size() < 2) {
            if (verts.size() == 1) Add(Entity::Point(M(verts[0].p)));
            return;
        }
        double s, rot;
        bool mirror;
        if (M.Similar(s, rot, mirror)) {
            for (Vertex& v : verts) {
                v.p = M(v.p);
                if (mirror) v.bulge = -v.bulge;
            }
            Add(Entity::Polyline(std::move(verts), closed));
            return;
        }
        // Stretched unevenly: arcs become short straight pieces.
        std::vector<Vertex> out;
        const size_t n = closed ? verts.size() : verts.size() - 1;
        for (size_t i = 0; i < n; ++i) {
            const Vertex& v = verts[i];
            const Vertex& w = verts[(i + 1) % verts.size()];
            out.push_back({M(v.p), 0});
            if (v.bulge != 0) {
                const Prim a = ArcFromBulge(v.p, w.p, v.bulge);
                for (int k = 1; k < 24; ++k) {
                    const double t = (double)k / 24;
                    const double ang = v.bulge > 0 ? a.start + a.sweep * t : a.start + a.sweep * (1 - t);
                    out.push_back({M(Polar(a.c, a.r, ang)), 0});
                }
            }
        }
        if (!closed) out.push_back({M(verts.back().p), 0});
        Add(Entity::Polyline(std::move(out), closed));
    }

    static Affine Ocs(const Raw& r) {
        double nx = r.Num(210, 0), ny = r.Num(220, 0), nz = r.Num(230, 1);
        const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len <= 0) return Affine{};
        nx /= len, ny /= len, nz /= len;
        if (std::fabs(nx) < 1e-12 && std::fabs(ny) < 1e-12 && nz > 0) return Affine{};
        // The "arbitrary axis algorithm" of the DXF reference.
        double ax, ay, az;
        if (std::fabs(nx) < 1.0 / 64 && std::fabs(ny) < 1.0 / 64) {  // (0,1,0) x N
            ax = nz, ay = 0, az = -nx;
        } else {  // (0,0,1) x N
            ax = -ny, ay = nx, az = 0;
        }
        double l = std::sqrt(ax * ax + ay * ay + az * az);
        ax /= l, ay /= l, az /= l;
        double bx = ny * az - nz * ay, by = nz * ax - nx * az, bz = nx * ay - ny * ax;  // N x Ax
        l = std::sqrt(bx * bx + by * by + bz * bz);
        bx /= l, by /= l;
        (void)bz;
        Affine m;
        m.a = ax;
        m.b = ay;
        m.c = bx;
        m.d = by;
        return m;
    }

    // Points along a NURBS curve (control points, knots, weights).
    static std::vector<Vec2> Nurbs(const std::vector<Vec2>& P, std::vector<double> U, std::vector<double> W, int p) {
        const size_t n = P.size();
        std::vector<Vec2> pts;
        if (n < 2) return pts;
        p = std::clamp(p, 1, (int)n - 1);
        if (U.size() != n + (size_t)p + 1) {  // clamped and uniform
            U.clear();
            for (int k = 0; k <= p; ++k) U.push_back(0);
            for (size_t k = 1; k + p < n; ++k) U.push_back((double)k);
            for (int k = 0; k <= p; ++k) U.push_back((double)(n - p));
        }
        if (W.size() != n) W.assign(n, 1.0);
        const double lo = U[(size_t)p], hi = U[n];
        if (!(hi > lo)) return pts;
        const int samples = (int)std::clamp<size_t>(n * 12, 32, 4000);
        for (int s = 0; s <= samples; ++s) {
            const double t = lo + (hi - lo) * s / samples;
            size_t k = (size_t)p;
            while (k + 1 < n && t >= U[k + 1]) ++k;
            // de Boor in homogeneous coordinates
            std::vector<double> X(p + 1), Y(p + 1), Wt(p + 1);
            for (int j = 0; j <= p; ++j) {
                const size_t idx = k - (size_t)p + (size_t)j;
                Wt[j] = W[idx];
                X[j] = P[idx].x * W[idx];
                Y[j] = P[idx].y * W[idx];
            }
            for (int r = 1; r <= p; ++r)
                for (int j = p; j >= r; --j) {
                    const size_t i = k - (size_t)p + (size_t)j;
                    const double den = U[i + (size_t)p - (size_t)r + 1] - U[i];
                    const double alpha = den > 0 ? (t - U[i]) / den : 0;
                    X[j] = (1 - alpha) * X[j - 1] + alpha * X[j];
                    Y[j] = (1 - alpha) * Y[j - 1] + alpha * Y[j];
                    Wt[j] = (1 - alpha) * Wt[j - 1] + alpha * Wt[j];
                }
            const double w = Wt[p] != 0 ? Wt[p] : 1;
            pts.push_back({X[p] / w, Y[p] / w});
        }
        return pts;
    }

    bool AddDimension(const Raw& r, const Affine& M) {
        double s, rot;
        bool mirror;
        if (!M.Similar(s, rot, mirror)) return false;
        DimData d;
        d.style = m_out.dimStyle;
        const int type = r.Int(70) & 7;
        const Vec2 p10 = r.Pt(10), p11 = r.Pt(11), p13 = r.Pt(13), p14 = r.Pt(14), p15 = r.Pt(15), p16 = r.Pt(16);
        switch (type) {
            case 0:
                d.kind = DimData::Linear;
                d.pts[0] = p13;
                d.pts[1] = p14;
                d.loc = p10;
                d.angle = ToRadians(r.Num(50));
                break;
            case 1:
                d.kind = DimData::Aligned;
                d.pts[0] = p13;
                d.pts[1] = p14;
                d.loc = p10;
                if (Near(p13, p14, 1e-12)) return false;
                break;
            case 2:
                d.kind = DimData::Angular;
                d.pts = {p13, p14, p10, p15};
                d.loc = p16;
                break;
            case 3:
                d.kind = DimData::Diameter;
                d.pts[0] = Lerp(p10, p15, 0.5);
                d.pts[1] = p15;
                d.loc = r.Has(11) ? p11 : p10;
                break;
            case 4:
                d.kind = DimData::Radius;
                d.pts[0] = p10;
                d.pts[1] = p15;
                d.loc = r.Has(11) ? p11 : p15;
                break;
            case 5:
                d.kind = DimData::Angular;
                d.rays = true;
                d.pts = {p15, p13, p15, p14};
                d.loc = p10;
                break;
            default:
                return false;  // ordinate: brought in as its picture
        }
        for (Vec2& p : d.pts) p = M(p);
        d.loc = M(d.loc);
        d.angle = AngleOf(M.Vec({std::cos(d.angle), std::sin(d.angle)}));
        if (d.kind == DimData::Radius || d.kind == DimData::Diameter) {
            if (Dist(d.pts[0], d.pts[1]) <= 1e-12) return false;
        }
        std::wstring t = PlainText(Text(r.Str(1)));
        if (t == L"<>") t.clear();
        d.text = t;
        Add(MakeDimension(std::move(d)));
        return true;
    }

    void Insert(const Raw& r, const Affine& M, int depth) {
        auto it = m_blocks.find(r.Str(2));
        if (it == m_blocks.end() || depth >= kMaxDepth) {
            Skipped("INSERT");
            return;
        }
        const Block& b = it->second;
        const Affine ocs = M * Ocs(r);
        const Vec2 at = r.Pt(10);
        const double sx = r.Num(41, 1), sy = r.Num(42, 1), ang = ToRadians(r.Num(50));
        const int cols = std::clamp(r.Int(70, 1), 1, 10000), rows = std::clamp(r.Int(71, 1), 1, 10000);
        const double cs = r.Num(44), rs = r.Num(45);
        if ((size_t)cols * rows * std::max<size_t>(1, b.ents.size()) > kMaxObjects) {
            Skipped("INSERT");
            return;
        }
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < cols; ++col) {
                const Vec2 offset = RotateAround({col * cs, row * rs}, {0, 0}, ang);
                const Affine T = ocs * Affine::Move(at + offset) * Affine::Turn(ang) * Affine::Scale(sx == 0 ? 1 : sx, sy == 0 ? 1 : sy) *
                                 Affine::Move(-b.base);
                for (const Raw& e : b.ents) Convert(e, T, depth + 1);
            }
    }

    void Convert(const Raw& r, const Affine& M, int depth) {
        if (r.Int(67) == 1) return;  // paper space
        const std::string& t = r.type;
        if (t == "LINE") {
            const Vec2 a = M(r.Pt(10)), b = M(r.Pt(11));
            if (!Near(a, b, 1e-12)) Add(Entity::Line(a, b));
        } else if (t == "POINT") {
            Add(Entity::Point(M(r.Pt(10))));
        } else if (t == "CIRCLE") {
            AddCircle(M * Ocs(r), r.Pt(10), r.Num(40));
        } else if (t == "ARC") {
            AddArc(M * Ocs(r), r.Pt(10), r.Num(40), ToRadians(r.Num(50)), ToRadians(r.Num(51)));
        } else if (t == "LWPOLYLINE") {
            std::vector<Vertex> verts;
            for (const Group& g : r.g) {
                double v;
                if (!ParseNumber(g.value, v)) continue;
                if (g.code == 10) verts.push_back({{v, 0}, 0});
                else if (g.code == 20 && !verts.empty()) verts.back().p.y = v;
                else if (g.code == 42 && !verts.empty()) verts.back().bulge = v;
            }
            AddPolyline(M * Ocs(r), std::move(verts), (r.Int(70) & 1) != 0);
        } else if (t == "POLYLINE") {
            const int flags = r.Int(70);
            if (flags & (16 | 64)) {
                Skipped("POLYLINE (mesh)");
                return;
            }
            std::vector<Vertex> verts;
            for (const Raw& v : r.sub) {
                if (v.Int(70) & 16) continue;  // spline frame control point
                verts.push_back({v.Pt(10), v.Num(42)});
            }
            AddPolyline((flags & 8) ? M : M * Ocs(r), std::move(verts), (flags & 1) != 0);
        } else if (t == "ELLIPSE") {
            const Vec2 c = r.Pt(10), major = r.Pt(11);
            const double ratio = r.Num(40, 1);
            double t0 = r.Num(41, 0), t1 = r.Num(42, kTau);
            if (Length(major) <= 1e-12 || !(ratio > 0)) return;
            Vec2 minor = Perp(major) * ratio;
            if (r.Num(230, 1) < 0) minor = -minor;  // seen from below: it runs clockwise
            const bool full = std::fabs(SweepCCW(t0, t1)) < 1e-9 || std::fabs(t1 - t0 - kTau) < 1e-9;
            if (!full && t1 < t0) t1 += kTau * std::ceil((t0 - t1) / kTau);
            AddConjugate(M(c), M.Vec(major), M.Vec(minor), t0, t1, full);
        } else if (t == "SPLINE") {
            const int flags = r.Int(70);
            const bool closed = (flags & 1) != 0;
            std::vector<Vec2> fit, ctrl;
            {
                const std::vector<double> fx = r.All(11), fy = r.All(21), cx = r.All(10), cy = r.All(20);
                for (size_t i = 0; i < fx.size() && i < fy.size(); ++i) fit.push_back(M({fx[i], fy[i]}));
                for (size_t i = 0; i < cx.size() && i < cy.size(); ++i) ctrl.push_back(M({cx[i], cy[i]}));
            }
            if (fit.size() >= 2) {
                if (closed && fit.size() > 2 && Near(fit.front(), fit.back(), 1e-9)) fit.pop_back();
                Add(Entity::Spline(fit, closed && fit.size() >= 3));
            } else if (ctrl.size() >= 2) {
                std::vector<Vec2> pts = Nurbs(ctrl, r.All(40), r.All(41), r.Int(71, 3));
                if (pts.size() < 2) return;
                const bool ends = Near(pts.front(), pts.back(), 1e-9 * (1 + Length(pts.front())));
                if (ends && pts.size() > 3) pts.pop_back();
                std::vector<Vertex> verts;
                for (Vec2 p : pts)
                    if (verts.empty() || !Near(verts.back().p, p, 1e-12)) verts.push_back({p, 0});
                Add(Entity::Polyline(std::move(verts), ends || closed));
            }
        } else if (t == "SOLID" || t == "TRACE") {
            const Affine T = M * Ocs(r);
            const Vec2 a = r.Pt(10), b = r.Pt(11), c = r.Pt(12), d = r.Has(13) ? r.Pt(13) : r.Pt(12);
            std::vector<Vertex> v = {{T(a), 0}, {T(b), 0}, {T(d), 0}};
            if (!Near(c, d, 1e-12)) v.push_back({T(c), 0});
            AddPolyline(Affine{}, std::move(v), true);
        } else if (t == "LEADER") {
            std::vector<Vertex> v;
            const std::vector<double> xs = r.All(10), ys = r.All(20);
            for (size_t i = 0; i < xs.size() && i < ys.size(); ++i) v.push_back({{xs[i], ys[i]}, 0});
            AddPolyline(M, std::move(v), false);
        } else if (t == "INSERT") {
            Insert(r, M, depth);
        } else if (t == "DIMENSION") {
            if (AddDimension(r, M)) return;
            // Not a kind Feather CAD has: bring in what it looks like.
            auto it = m_blocks.find(r.Str(2));
            if (it == m_blocks.end() || depth >= kMaxDepth) {
                Skipped("DIMENSION");
                return;
            }
            for (const Raw& e : it->second.ents) Convert(e, M, depth + 1);
        } else if (t == "VIEWPORT" || t == "SEQEND" || t == "ATTDEF") {
            // nothing to draw
        } else {
            Skipped(t);
        }
    }

    DxfResult& m_out;
    std::vector<Group> m_groups;
    std::vector<Raw> m_entities;
    std::map<std::string, Block> m_blocks;
    bool m_utf8 = false;
};
}  // namespace

std::wstring DxfResult::Notes() const {
    if (skipped.empty()) return L"";
    std::wstring s = L"Not brought in: ";
    bool first = true;
    for (const auto& kv : skipped) {
        if (!first) s += L", ";
        first = false;
        s += std::to_wstring(kv.second) + L" " + TextFromUtf8(kv.first);
    }
    s += L".";
    if (skipped.count("TEXT") || skipped.count("MTEXT") || skipped.count("HATCH"))
        s += L" Text and hatching come in a later version.";
    return s;
}

bool ReadDxf(const std::string& text, DxfResult& out, std::wstring& error) {
    out = DxfResult{};
    Importer imp(out);
    return imp.Parse(text, error);
}
