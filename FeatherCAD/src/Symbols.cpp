// Symbols.cpp - see Symbols.h.
#include "Symbols.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

#include "Annot.h"

namespace {
// Builds a symbol: its lines, arcs, fills, texts, attributes and ports.
struct Sb {
    BlockData b;
    void Item(Entity e, int lw = 25) {
        e.layer = 0;
        e.lineweight = (int16_t)lw;
        b.items.push_back(std::move(e));
    }
    void L(double x0, double y0, double x1, double y1, int lw = 25) { Item(Entity::Line({x0, y0}, {x1, y1}), lw); }
    void C(double x, double y, double r) { Item(Entity::Circle({x, y}, r)); }
    void A(double x, double y, double r, double a0, double a1) { Item(Entity::Arc({x, y}, r, ToRadians(a0), ToRadians(a1))); }
    void P(std::vector<Vec2> pts, bool closed, double bulge = 0) {
        std::vector<Vertex> v;
        for (Vec2 p : pts) v.push_back({p, bulge});
        if (!closed && !v.empty()) v.back().bulge = 0;
        Item(Entity::Polyline(v, closed));
    }
    void Fill(std::vector<Vec2> pts) {
        HatchData h;
        h.pattern = L"SOLID";
        h.loops.push_back(pts);
        Entity e = MakeHatch(h);
        e.layer = 0;
        b.items.push_back(e);
    }
    void T(double x, double y, const std::wstring& s, double h = 3, int justify = 4) {
        TextData t;
        t.content = s;
        t.height = h;
        t.justify = justify;
        Item(MakeText({x, y}, t), 18);
    }
    void Attr(double x, double y, const wchar_t* tag, const wchar_t* prompt, double h = 2.5, int justify = 7) {
        TextData t;
        t.content = L"";
        t.tag = tag;
        t.prompt = prompt;
        t.height = h;
        t.justify = justify;
        Item(MakeText({x, y}, t), 18);
    }
    void Port(double x, double y, double dx, double dy) { b.ports.push_back({{x, y}, {dx, dy}}); }
    // A filled arrowhead with its tip at (x, y), pointing along (dx, dy).
    void Arrow(double x, double y, double dx, double dy, double size = 1.6) {
        const Vec2 d = Normalized(Vec2{dx, dy}), tip{x, y}, back = tip - d * size, side = Perp(d) * (size * 0.35);
        Fill({tip, back + side, back - side});
    }
    // A two-lead part along x from 0 to 20 (ports at the ends), its label above.
    void Leads(double from, double to) {
        if (from > 0) L(0, 0, from, 0);
        if (to < 20) L(to, 0, 20, 0);
        Port(0, 0, -1, 0);
        Port(20, 0, 1, 0);
    }
};

Vec2 V(double x, double y) { return {x, y}; }

// ---- electrical and electronic (IS 2032 / IEC 60617 style) --------------------
void Electrical(const std::wstring& n, Sb& s) {
    if (n == L"RESISTOR") {
        s.Leads(5, 15);
        s.P({V(5, -2), V(15, -2), V(15, 2), V(5, 2)}, true);
    } else if (n == L"RESISTOR_ZIGZAG") {
        s.Leads(4, 16);
        std::vector<Vec2> z{V(4, 0)};
        for (int k = 0; k < 6; ++k) z.push_back(V(5 + 2 * k, k % 2 ? -2.5 : 2.5));
        z.push_back(V(16, 0));
        s.P(z, false);
    } else if (n == L"VARIABLE_RESISTOR") {
        s.Leads(5, 15);
        s.P({V(5, -2), V(15, -2), V(15, 2), V(5, 2)}, true);
        s.L(6, -4, 14, 4);
        s.Arrow(14, 4, 1, 1);
    } else if (n == L"CAPACITOR") {
        s.Leads(9, 11);
        s.L(9, -4, 9, 4, 50);
        s.L(11, -4, 11, 4, 50);
    } else if (n == L"ELECTROLYTIC_CAPACITOR") {
        s.Leads(9, 11);
        s.L(9, -4, 9, 4, 50);
        s.A(15, 0, 4, 150, 210);
        s.T(7, 3.5, L"+", 2.5);
    } else if (n == L"INDUCTOR") {
        s.Leads(4, 16);
        for (int k = 0; k < 4; ++k) s.A(5.5 + 3 * k, 0, 1.5, 0, 180);
    } else if (n == L"DIODE" || n == L"ZENER_DIODE" || n == L"LED" || n == L"PHOTODIODE") {
        s.Leads(7, 13);
        s.P({V(7, -3), V(7, 3), V(13, 0)}, true);
        s.L(13, -3, 13, 3, 35);
        if (n == L"ZENER_DIODE") {
            s.L(13, 3, 11.8, 3.8);
            s.L(13, -3, 14.2, -3.8);
        }
        if (n == L"LED")
            for (double x : {9.0, 12.0}) {
                s.L(x, 3.5, x + 2.5, 6);
                s.Arrow(x + 2.5, 6, 1, 1, 1.2);
            }
        if (n == L"PHOTODIODE")
            for (double x : {11.0, 14.0}) {
                s.L(x + 2.5, 6.5, x, 4);
                s.Arrow(x, 4, -1, -1, 1.2);
            }
    } else if (n == L"NPN_TRANSISTOR" || n == L"PNP_TRANSISTOR") {
        s.C(10, 0, 6);
        s.L(0, 0, 7, 0);
        s.L(7, -3.5, 7, 3.5, 50);
        s.L(7, 1.5, 12, 4.5);
        s.L(12, 4.5, 12, 10);
        s.L(7, -1.5, 12, -4.5);
        s.L(12, -4.5, 12, -10);
        if (n == L"NPN_TRANSISTOR") s.Arrow(12, -4.5, 5, -3);
        else s.Arrow(7.4, -1.7, -5, 3);
        s.T(4.5, 1.6, L"B", 1.8);
        s.T(13.5, 8, L"C", 1.8);
        s.T(13.5, -8, L"E", 1.8);
        s.b.ports.clear();
        s.Port(0, 0, -1, 0);
        s.Port(12, 10, 0, 1);
        s.Port(12, -10, 0, -1);
    } else if (n == L"CELL" || n == L"BATTERY") {
        const bool two = n == L"BATTERY";
        s.Leads(two ? 7 : 9, two ? 13 : 11);
        auto cell = [&](double x) {
            s.L(x, -5, x, 5);
            s.L(x + 2, -2.5, x + 2, 2.5, 50);
        };
        cell(two ? 7 : 9);
        if (two) {
            cell(11);
            s.L(9, 0, 11, 0);
        }
        s.T(two ? 5.5 : 7.5, 4, L"+", 2.5);
    } else if (n == L"DC_SOURCE" || n == L"AC_SOURCE" || n == L"AMMETER" || n == L"VOLTMETER" || n == L"GALVANOMETER" ||
               n == L"MOTOR" || n == L"LAMP" || n == L"GENERATOR") {
        s.Leads(5, 15);
        s.C(10, 0, 5);
        if (n == L"DC_SOURCE") {
            s.T(8, 0, L"+", 3);
            s.T(12, 0, L"\x2013", 3);
        } else if (n == L"AC_SOURCE") {
            std::vector<Vec2> w;
            for (int k = 0; k <= 16; ++k) w.push_back(V(7 + 6.0 * k / 16, 2 * std::sin(kTau * k / 16)));
            s.P(w, false);
        } else if (n == L"LAMP") {
            const double d = 5 / std::sqrt(2.0);
            s.L(10 - d, -d, 10 + d, d);
            s.L(10 - d, d, 10 + d, -d);
        } else {
            const wchar_t* letter = n == L"AMMETER" ? L"A" : n == L"VOLTMETER" ? L"V" : n == L"GALVANOMETER" ? L"G" : n == L"MOTOR" ? L"M" : L"G";
            s.T(10, 0, letter, 4);
            if (n == L"GENERATOR") s.A(10, 0, 3.6, 200, 340);
        }
    } else if (n == L"SWITCH") {
        s.Leads(6, 14);
        s.C(6.8, 0, 0.8);
        s.C(13.2, 0, 0.8);
        s.L(7.4, 0.5, 13.5, 4.5);
    } else if (n == L"PUSH_BUTTON") {
        s.Leads(6, 14);
        s.C(6.8, 0, 0.8);
        s.C(13.2, 0, 0.8);
        s.L(6, 2.5, 14, 2.5);
        s.L(10, 2.5, 10, 6);
    } else if (n == L"FUSE") {
        s.Leads(5, 15);
        s.P({V(5, -1.5), V(15, -1.5), V(15, 1.5), V(5, 1.5)}, true);
        s.L(5, 0, 15, 0);
    } else if (n == L"TRANSFORMER") {
        // Two windings facing each other, an iron core between.
        for (int k = 0; k < 4; ++k) {
            s.A(6, -4.5 + 3 * k, 1.5, -90, 90);
            s.A(14, -4.5 + 3 * k, 1.5, 90, 270);
        }
        s.L(9.3, -6, 9.3, 6);
        s.L(10.7, -6, 10.7, 6);
        s.L(0, 6, 6, 6);
        s.L(0, -6, 6, -6);
        s.L(14, 6, 20, 6);
        s.L(14, -6, 20, -6);
        s.Port(0, 6, -1, 0);
        s.Port(0, -6, -1, 0);
        s.Port(20, 6, 1, 0);
        s.Port(20, -6, 1, 0);
        s.Attr(10, 8, L"LABEL", L"Label (such as T1)");
        return;
    } else if (n == L"GROUND") {
        s.L(0, 0, 0, -5);
        s.L(-5, -5, 5, -5, 35);
        s.L(-3.3, -6.5, 3.3, -6.5, 35);
        s.L(-1.6, -8, 1.6, -8, 35);
        s.Port(0, 0, 0, 1);
        return;
    } else if (n == L"JUNCTION") {
        std::vector<Vec2> c;
        for (int k = 0; k < 12; ++k) c.push_back(Polar({0, 0}, 0.8, kTau * k / 12));
        s.Fill(c);
        s.Port(0, 0, 0, 0);
        return;
    } else if (n == L"TERMINAL") {
        s.C(0, 0, 1);
        s.Port(0, 0, 0, 0);
        s.Attr(0, 2, L"LABEL", L"Label (such as +12 V)");
        return;
    }
    s.Attr(10, n.find(L"TRANSISTOR") != std::wstring::npos ? 7 : 5, L"LABEL", L"Label (such as R1 10 k)");
}

// ---- logic gates (distinctive shapes) ------------------------------------------------
void Logic(const std::wstring& n, Sb& s) {
    const bool bubble = n == L"NAND" || n == L"NOR" || n == L"XNOR" || n == L"NOT";
    const bool single = n == L"NOT" || n == L"BUFFER";
    double out = 0;
    if (n == L"AND" || n == L"NAND") {
        s.P({V(11, -6), V(5, -6), V(5, 6), V(11, 6)}, false);
        s.A(11, 0, 6, -90, 90);
        out = 17;
        s.L(0, 3, 5, 3);
        s.L(0, -3, 5, -3);
    } else if (n == L"OR" || n == L"NOR" || n == L"XOR" || n == L"XNOR") {
        const double a = ToDegrees(std::atan2(6.0, 8.0));
        s.A(-3, 0, 10, -a, a);  // the back
        const double y0 = -40.0 / 3, r = 6 - y0, front = ToDegrees(std::atan2(-y0, 14.0));
        s.A(5, y0, r, front, 90);
        s.A(5, -y0, r, -90, -front);
        out = 19;
        const bool x = n == L"XOR" || n == L"XNOR";
        if (x) s.A(-5, 0, 10, -a, a);
        const double lead = (x ? -5 : -3) + std::sqrt(100.0 - 9);
        s.L(0, 3, lead, 3);
        s.L(0, -3, lead, -3);
    } else {  // NOT, BUFFER
        s.P({V(5, -6), V(5, 6), V(17, 0)}, true);
        out = 17;
        s.L(0, 0, 5, 0);
    }
    if (bubble) {
        s.C(out + 1, 0, 1);
        out += 2;
    }
    s.L(out, 0, 25, 0);
    if (single) {
        s.Port(0, 0, -1, 0);
    } else {
        s.Port(0, 3, -1, 0);
        s.Port(0, -3, -1, 0);
    }
    s.Port(25, 0, 1, 0);
    s.Attr(11, 7, L"LABEL", L"Label (such as G1)");
}

// ---- flowchart ----------------------------------------------------------------------
void Flow(const std::wstring& n, Sb& s) {
    double w = 20, h = 10;  // half sizes
    if (n == L"TERMINATOR") {
        s.L(-10, 10, 10, 10);
        s.L(-10, -10, 10, -10);
        s.A(10, 0, 10, -90, 90);
        s.A(-10, 0, 10, 90, 270);
    } else if (n == L"PROCESS") {
        s.P({V(-20, -10), V(20, -10), V(20, 10), V(-20, 10)}, true);
    } else if (n == L"DECISION") {
        w = 22, h = 12;
        s.P({V(0, 12), V(-22, 0), V(0, -12), V(22, 0)}, true);
    } else if (n == L"INPUT_OUTPUT") {
        s.P({V(-15, 10), V(-20, -10), V(15, -10), V(20, 10)}, true);
    } else if (n == L"PREDEFINED_PROCESS") {
        s.P({V(-20, -10), V(20, -10), V(20, 10), V(-20, 10)}, true);
        s.L(-16, -10, -16, 10);
        s.L(16, -10, 16, 10);
    } else if (n == L"DOCUMENT") {
        s.L(-20, -7, -20, 10);
        s.L(-20, 10, 20, 10);
        s.L(20, 10, 20, -7);
        s.Item(Entity::Spline({V(20, -7), V(10, -11), V(0, -7.5), V(-10, -3.5), V(-20, -7)}, false));
    } else if (n == L"PREPARATION") {
        s.P({V(-14, 10), V(-20, 0), V(-14, -10), V(14, -10), V(20, 0), V(14, 10)}, true);
    } else if (n == L"CONNECTOR") {
        w = h = 5;
        s.C(0, 0, 5);
    } else if (n == L"DATA_STORAGE") {
        s.L(-15, 10, 20, 10);
        s.L(-15, -10, 20, -10);
        s.A(-15, 0, 10, 90, 270);
        s.A(28, 0, 12.81, 180 - 51.34, 180 + 51.34);
        w = 20;
    }
    s.Port(0, h, 0, 1);
    s.Port(0, -h, 0, -1);
    s.Port(-w, 0, -1, 0);
    s.Port(w, 0, 1, 0);
    s.Attr(0, 0, L"TEXT", n == L"CONNECTOR" ? L"Letter or number" : L"Text inside", 3, 4);
}

// ---- computer networks ------------------------------------------------------------------
void Network(const std::wstring& n, Sb& s) {
    double w = 10, top = 10, bottom = -10;
    if (n == L"PC") {
        s.P({V(-10, 0), V(10, 0), V(10, 14), V(-10, 14)}, true);
        s.P({V(-8, 2), V(8, 2), V(8, 12), V(-8, 12)}, true);
        s.P({V(-2, 0), V(-3, -3), V(3, -3), V(2, 0)}, false);
        s.L(-7, -3, 7, -3);
        top = 14, bottom = -3;
    } else if (n == L"LAPTOP") {
        s.P({V(-8, 0), V(8, 0), V(8, 10), V(-8, 10)}, true);
        s.P({V(-6.5, 1.5), V(6.5, 1.5), V(6.5, 8.5), V(-6.5, 8.5)}, true);
        s.P({V(-8, 0), V(-11, -4), V(11, -4), V(8, 0)}, false);
        w = 11, top = 10, bottom = -4;
    } else if (n == L"SERVER") {
        s.P({V(-6, -12), V(6, -12), V(6, 12), V(-6, 12)}, true);
        for (double y : {6.0, 0.0, -6.0}) s.L(-6, y, 6, y);
        for (double y : {9.0, 3.0, -3.0}) {
            s.L(-4, y, 1, y);
            s.C(3.5, y, 0.7);
        }
        w = 6, top = 12, bottom = -12;
    } else if (n == L"ROUTER") {
        s.Item(Entity::Ellipse({0, 3}, {12, 0}, 0.35));
        s.L(-12, 3, -12, -3);
        s.L(12, 3, 12, -3);
        s.Item(Entity::EllipseArc({0, -3}, {12, 0}, 0.35, kPi, kTau));
        // Four arrows on top: two in, two out.
        s.L(-8, 3, -2.5, 3);
        s.Arrow(-2, 3, 1, 0, 1.4);
        s.L(8, 3, 2.5, 3);
        s.Arrow(2, 3, -1, 0, 1.4);
        s.L(0, 4.3, 0, 6.6);
        s.Arrow(0, 7.2, 0, 1, 1.2);
        s.L(0, 1.7, 0, -0.6);
        s.Arrow(0, -1.2, 0, -1, 1.2);
        w = 12, top = 7.2, bottom = -7.2;
    } else if (n == L"SWITCH") {
        s.P({V(-14, -4), V(14, -4), V(14, 4), V(-14, 4)}, true);
        for (int k = 0; k < 2; ++k) {
            const double y = k ? -1.6 : 1.6, x0 = -8, x1 = 8;
            s.L(x0, y, x1, y);
            if (k) s.Arrow(x0 - 0.6, y, -1, 0, 1.4);
            else s.Arrow(x1 + 0.6, y, 1, 0, 1.4);
        }
        w = 14, top = 4, bottom = -4;
    } else if (n == L"HUB") {
        s.P({V(-14, -4), V(14, -4), V(14, 4), V(-14, 4)}, true);
        s.T(0, 0, L"HUB", 3);
        w = 14, top = 4, bottom = -4;
    } else if (n == L"ACCESS_POINT") {
        s.P({V(-10, -3), V(10, -3), V(10, 3), V(-10, 3)}, true);
        s.L(7, 3, 7, 9);
        for (double r : {2.0, 3.5}) {
            s.A(7, 9, r, 20, 70);
            s.A(7, 9, r, 110, 160);
        }
        top = 9, bottom = -3;
    } else if (n == L"CLOUD") {
        std::vector<Vec2> c;
        for (int k = 0; k < 8; ++k) c.push_back(V(14 * std::cos(kTau * k / 8), 7 * std::sin(kTau * k / 8)));
        s.P(c, true, 0.45);
        s.T(0, 0, L"Internet", 2.5);
        w = 16, top = 9, bottom = -9;
    } else if (n == L"PRINTER") {
        s.P({V(-10, -4), V(10, -4), V(10, 4), V(-10, 4)}, true);
        s.P({V(-6, 4), V(-6, 10), V(6, 10), V(6, 4)}, false);
        s.P({V(-6, -4), V(-6, -8), V(6, -8), V(6, -4)}, false);
        top = 10, bottom = -8;
    } else if (n == L"FIREWALL") {
        s.P({V(-10, -7), V(10, -7), V(10, 7), V(-10, 7)}, true);
        for (int r = 1; r < 4; ++r) s.L(-10, -7 + 3.5 * r, 10, -7 + 3.5 * r);
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) {
                const double x = -10 + 5 * k + (r % 2 ? 2.5 : 5);
                if (x < 10) s.L(x, -7 + 3.5 * r, x, -7 + 3.5 * (r + 1));
            }
        top = 7, bottom = -7;
    } else if (n == L"MODEM") {
        s.P({V(-10, -3), V(10, -3), V(10, 3), V(-10, 3)}, true);
        for (int k = 0; k < 4; ++k) s.C(-6 + 3 * k, 0, 0.7);
        top = 3, bottom = -3;
    } else if (n == L"IOT_DEVICE") {
        s.P({V(-6, -6), V(6, -6), V(6, 6), V(-6, 6)}, true);
        s.C(0, 0, 2.5);
        s.L(4, 6, 4, 10);
        s.A(4, 10, 2, 30, 150);
        w = 6, top = 10, bottom = -6;
    } else if (n == L"MOBILE") {
        s.P({V(-4, -8), V(4, -8), V(4, 8), V(-4, 8)}, true);
        s.P({V(-3, -5.5), V(3, -5.5), V(3, 6), V(-3, 6)}, true);
        s.C(0, -6.8, 0.6);
        w = 4, top = 8, bottom = -8;
    }
    s.Port(0, top, 0, 1);
    s.Port(0, bottom, 0, -1);
    s.Port(-w, (top + bottom) / 2, -1, 0);
    s.Port(w, (top + bottom) / 2, 1, 0);
    s.Attr(0, bottom - 1.5, L"LABEL", L"Label (such as PC1, Router)", 2.5, 1);
}

// ---- civil plans (millimetres, real size) ---------------------------------------------------
void Civil(const std::wstring& n, Sb& s) {
    if (n == L"DOOR") {  // 900 wide, hinged at the base point, opening inwards (up)
        s.L(0, 0, 0, 900, 35);
        s.A(0, 0, 900, 0, 90);
        s.L(0, 0, -50, 0);
        s.L(900, 0, 950, 0);
    } else if (n == L"DOUBLE_DOOR") {  // 1200, two leaves
        s.L(0, 0, 0, 600, 35);
        s.L(1200, 0, 1200, 600, 35);
        s.A(0, 0, 600, 0, 90);
        s.A(1200, 0, 600, 90, 180);
    } else if (n == L"WINDOW") {  // 1200 in a 230 wall
        s.P({V(0, 0), V(1200, 0), V(1200, 230), V(0, 230)}, true);
        s.L(0, 90, 1200, 90);
        s.L(0, 140, 1200, 140);
    } else if (n == L"VENTILATOR") {  // 600 in a 230 wall, high level
        s.P({V(0, 0), V(600, 0), V(600, 230), V(0, 230)}, true);
        s.L(0, 115, 600, 115);
    } else if (n == L"STAIRS") {  // a straight flight, 1000 wide, 10 treads of 250, arrow up
        s.P({V(0, 0), V(1000, 0), V(1000, 2500), V(0, 2500)}, true);
        for (int k = 1; k < 10; ++k) s.L(0, 250.0 * k, 1000, 250.0 * k);
        s.L(500, 125, 500, 2300);
        s.Arrow(500, 2400, 0, 1, 150);
        s.T(500, -150, L"UP", 120);
    }
    s.Attr(0, -200, L"LABEL", L"Label (such as D1, W1)", 150, 0);
}
}  // namespace

const std::vector<SymbolInfo>& SymbolLibrary() {
    static const std::vector<SymbolInfo> list = {
        {L"Electrical", L"RESISTOR", L"Resistor"},
        {L"Electrical", L"RESISTOR_ZIGZAG", L"Resistor (zigzag)"},
        {L"Electrical", L"VARIABLE_RESISTOR", L"Variable resistor"},
        {L"Electrical", L"CAPACITOR", L"Capacitor"},
        {L"Electrical", L"ELECTROLYTIC_CAPACITOR", L"Electrolytic capacitor"},
        {L"Electrical", L"INDUCTOR", L"Inductor"},
        {L"Electrical", L"TRANSFORMER", L"Transformer"},
        {L"Electrical", L"DIODE", L"Diode"},
        {L"Electrical", L"ZENER_DIODE", L"Zener diode"},
        {L"Electrical", L"LED", L"LED"},
        {L"Electrical", L"PHOTODIODE", L"Photodiode"},
        {L"Electrical", L"NPN_TRANSISTOR", L"NPN transistor"},
        {L"Electrical", L"PNP_TRANSISTOR", L"PNP transistor"},
        {L"Electrical", L"CELL", L"Cell"},
        {L"Electrical", L"BATTERY", L"Battery"},
        {L"Electrical", L"DC_SOURCE", L"DC source"},
        {L"Electrical", L"AC_SOURCE", L"AC source"},
        {L"Electrical", L"GENERATOR", L"Generator"},
        {L"Electrical", L"SWITCH", L"Switch"},
        {L"Electrical", L"PUSH_BUTTON", L"Push button"},
        {L"Electrical", L"FUSE", L"Fuse"},
        {L"Electrical", L"LAMP", L"Lamp"},
        {L"Electrical", L"MOTOR", L"Motor"},
        {L"Electrical", L"AMMETER", L"Ammeter"},
        {L"Electrical", L"VOLTMETER", L"Voltmeter"},
        {L"Electrical", L"GALVANOMETER", L"Galvanometer"},
        {L"Electrical", L"GROUND", L"Ground (earth)"},
        {L"Electrical", L"JUNCTION", L"Junction dot"},
        {L"Electrical", L"TERMINAL", L"Terminal"},
        {L"Logic gates", L"AND", L"AND gate"},
        {L"Logic gates", L"OR", L"OR gate"},
        {L"Logic gates", L"NOT", L"NOT gate (inverter)"},
        {L"Logic gates", L"NAND", L"NAND gate"},
        {L"Logic gates", L"NOR", L"NOR gate"},
        {L"Logic gates", L"XOR", L"XOR gate"},
        {L"Logic gates", L"XNOR", L"XNOR gate"},
        {L"Logic gates", L"BUFFER", L"Buffer"},
        {L"Flowchart", L"TERMINATOR", L"Start / stop"},
        {L"Flowchart", L"PROCESS", L"Process"},
        {L"Flowchart", L"DECISION", L"Decision"},
        {L"Flowchart", L"INPUT_OUTPUT", L"Input / output"},
        {L"Flowchart", L"PREDEFINED_PROCESS", L"Predefined process"},
        {L"Flowchart", L"DOCUMENT", L"Document"},
        {L"Flowchart", L"PREPARATION", L"Preparation"},
        {L"Flowchart", L"DATA_STORAGE", L"Stored data"},
        {L"Flowchart", L"CONNECTOR", L"Connector (on-page)"},
        {L"Network", L"PC", L"PC (desktop)"},
        {L"Network", L"LAPTOP", L"Laptop"},
        {L"Network", L"SERVER", L"Server"},
        {L"Network", L"ROUTER", L"Router"},
        {L"Network", L"SWITCH", L"Switch"},
        {L"Network", L"HUB", L"Hub"},
        {L"Network", L"ACCESS_POINT", L"Wireless access point"},
        {L"Network", L"MODEM", L"Modem"},
        {L"Network", L"FIREWALL", L"Firewall"},
        {L"Network", L"PRINTER", L"Printer"},
        {L"Network", L"MOBILE", L"Mobile phone"},
        {L"Network", L"IOT_DEVICE", L"IoT device"},
        {L"Network", L"CLOUD", L"Cloud (Internet)"},
        {L"Civil", L"DOOR", L"Door (900)"},
        {L"Civil", L"DOUBLE_DOOR", L"Double door (1200)"},
        {L"Civil", L"WINDOW", L"Window (1200, 230 wall)"},
        {L"Civil", L"VENTILATOR", L"Ventilator (600)"},
        {L"Civil", L"STAIRS", L"Stairs (straight flight)"},
    };
    return list;
}

std::vector<std::wstring> SymbolCategories() {
    std::vector<std::wstring> out;
    for (const SymbolInfo& s : SymbolLibrary())
        if (std::find(out.begin(), out.end(), s.category) == out.end()) out.push_back(s.category);
    return out;
}

namespace {
std::wstring Upper(std::wstring s) {
    for (wchar_t& c : s) c = c == L' ' ? L'_' : (wchar_t)std::towupper(c);
    return s;
}
// The block name: the network switch is not the electrical one.
std::wstring BlockName(const SymbolInfo& s) {
    return std::wstring(s.category) == L"Network" && std::wstring(s.name) == L"SWITCH" ? L"NETWORK_SWITCH" : s.name;
}
}  // namespace

const SymbolInfo* FindSymbol(const std::wstring& name) {
    const std::wstring u = Upper(name);
    for (const SymbolInfo& s : SymbolLibrary())
        if (BlockName(s) == u) return &s;
    for (const SymbolInfo& s : SymbolLibrary())
        if (s.name == u || Upper(s.title) == u) return &s;
    return nullptr;
}

bool MakeSymbolBlock(const std::wstring& name, BlockData& out) {
    const SymbolInfo* info = FindSymbol(name);
    if (!info) return false;
    Sb s;
    const std::wstring n = info->name, cat = info->category;
    if (cat == L"Electrical") Electrical(n, s);
    else if (cat == L"Logic gates") Logic(n, s);
    else if (cat == L"Flowchart") Flow(n, s);
    else if (cat == L"Network") Network(n, s);
    else Civil(n, s);
    out = std::move(s.b);
    out.name = BlockName(*info);
    out.base = {0, 0};
    return true;
}

std::shared_ptr<const BlockData> SymbolBlock(Document& doc, const std::wstring& name) {
    const SymbolInfo* info = FindSymbol(name);
    if (!info) return nullptr;
    if (auto have = doc.FindBlock(BlockName(*info))) return have;
    BlockData b;
    if (!MakeSymbolBlock(name, b)) return nullptr;
    return doc.DefineBlock(std::move(b));
}

// ---------------------------------------------------------------------------
// Connectors
// ---------------------------------------------------------------------------

bool PortPoint(const Document& doc, const DimRef& ref, Vec2& at, Vec2& dir) {
    const Entity* e = ref.id ? doc.Find(ref.id) : nullptr;
    if (!e || e->type != EntityType::Insert || !e->annot || !e->annot->block) return false;
    const auto& ports = e->annot->block->ports;
    if (ref.grip < 0 || ref.grip >= (int)ports.size()) return false;
    const BlockPort& p = ports[(size_t)ref.grip];
    at = BlockToInsert(*e, p.at);
    const Vec2 tip = BlockToInsert(*e, p.at + p.dir);
    dir = Length(p.dir) > 1e-12 ? Normalized(tip - at) : Vec2{0, 0};
    return true;
}

DimRef NearestPort(const Document& doc, Vec2 p, double tol, Vec2* at) {
    DimRef best;
    double bestD = tol;
    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        if (e.type != EntityType::Insert || !e.annot || !e.annot->block || e.annot->block->ports.empty() ||
            !Bounds(e).Inflated(tol).Contains(p))
            continue;
        for (size_t i = 0; i < e.annot->block->ports.size(); ++i) {
            Vec2 q, d;
            if (!PortPoint(doc, {kv.first, (int)i}, q, d)) continue;
            if (const double dd = Dist(q, p); dd <= bestD) {
                bestD = dd;
                best = {kv.first, (int)i};
                if (at) *at = q;
            }
        }
    }
    return best;
}

std::vector<Vec2> RouteConnector(Vec2 a, Vec2 da, Vec2 b, Vec2 db, bool straight) {
    if (straight || Near(a, b, 1e-9)) return {a, b};
    const double stub = std::min(3.0, Dist(a, b) / 4);
    const Vec2 a1 = a + da * stub, b1 = b + db * stub;
    auto horiz = [](Vec2 d, Vec2 from, Vec2 to) {
        if (Length(d) > 1e-12) return std::fabs(d.x) >= std::fabs(d.y);
        return std::fabs(to.x - from.x) >= std::fabs(to.y - from.y);
    };
    const bool ha = horiz(da, a1, b1), hb = horiz(db, b1, a1);
    std::vector<Vec2> p{a, a1};
    if (ha && hb) {
        const double xm = (a1.x + b1.x) / 2;
        p.push_back({xm, a1.y});
        p.push_back({xm, b1.y});
    } else if (!ha && !hb) {
        const double ym = (a1.y + b1.y) / 2;
        p.push_back({a1.x, ym});
        p.push_back({b1.x, ym});
    } else if (ha) {
        p.push_back({b1.x, a1.y});
    } else {
        p.push_back({a1.x, b1.y});
    }
    p.push_back(b1);
    p.push_back(b);
    // Without repeated or straight-through points.
    std::vector<Vec2> out;
    for (Vec2 q : p) {
        if (!out.empty() && Near(out.back(), q, 1e-9)) continue;
        if (out.size() >= 2 && std::fabs(Cross(out.back() - out[out.size() - 2], q - out.back())) < 1e-9 &&
            Dot(out.back() - out[out.size() - 2], q - out.back()) >= 0)
            out.back() = q;
        else
            out.push_back(q);
    }
    return out;
}

bool IsConnector(const Entity& e) { return e.type == EntityType::Leader && e.annot && e.annot->connector; }

namespace {
// An end: on its port, or where it was.
void EndOf(const Document& doc, DimRef& ref, Vec2 was, Vec2& at, Vec2& dir) {
    at = was;
    dir = {0, 0};
    if (ref.id && !PortPoint(doc, ref, at, dir)) {
        ref = DimRef{};
        at = was;
    }
}
}  // namespace

Entity MakeConnector(const Document& doc, const DimRef& from, Vec2 a, const DimRef& to, Vec2 b, bool straight, double arrow) {
    Entity e = MakeLeader({b, a}, TextData{}, arrow);
    Annot& an = EditAnnot(e);
    an.connector = true;
    an.from = from;
    an.to = to;
    an.straight = straight;
    RerouteConnector(doc, e);
    return e;
}

bool RerouteConnector(const Document& doc, Entity& c) {
    if (!IsConnector(c) || c.annot->pts.size() < 2) return false;
    Annot& an = EditAnnot(c);
    Vec2 a, da, b, db;
    EndOf(doc, an.from, an.pts.back(), a, da);
    EndOf(doc, an.to, an.pts.front(), b, db);
    std::vector<Vec2> route = RouteConnector(a, da, b, db, an.straight);
    std::reverse(route.begin(), route.end());  // the arrow end first
    if (route == an.pts) return false;
    an.pts = route;
    c.p0 = route[0];
    return true;
}
