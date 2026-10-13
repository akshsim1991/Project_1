// MakeExamples.cpp - writes the solved examples in samples/ (Help >
// Examples): one problem for each syllabus topic, drawn with the program's
// own constructions, each with a STEPS note (how to do it with the
// commands) and an A3 sheet with the BIS title block.
//
// Build and run from FeatherCAD/ (the core sources, no Windows; one command line):
//   g++ -std=c++17 -O1 -Isrc tools/MakeExamples.cpp src/Geometry.cpp src/Entity.cpp src/Spline.cpp
//       src/Curve.cpp src/Modify.cpp src/Grips.cpp src/Select.cpp src/Dimension.cpp src/Style.cpp
//       src/Document.cpp src/DxfImport.cpp src/Snap.cpp src/Engine.cpp src/Commands.cpp
//       src/CommandsModify.cpp src/CommandsDraw.cpp src/CommandsDim.cpp src/Annot.cpp
//       src/CommandsAnnot.cpp src/Sheet.cpp src/Plot.cpp src/Expr.cpp src/Relate.cpp
//       src/CommandsRelate.cpp src/FreeSketch.cpp src/EngGraphics.cpp src/CommandsEG.cpp
//       src/Symbols.cpp src/EngCurves.cpp src/CommandsCurves.cpp -o make_examples
//   ./make_examples samples
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "Annot.h"
#include "EngCurves.h"
#include "EngGraphics.h"
#include "Sheet.h"
#include "Style.h"
#include "Symbols.h"

namespace {
constexpr double kH = 3.5;  // text height

struct Example {
    Document doc;
    void Add(Entity e) {
        if (e.layer < 0) e.layer = doc.Current().layer;
        doc.Add(e);
    }
    void AddAll(const std::vector<Entity>& ents) {
        for (const Entity& e : ents) Add(e);
    }
    Entity Line(Vec2 a, Vec2 b, int lw = 50) {
        Entity e = Entity::Line(a, b);
        e.lineweight = (int16_t)lw;
        return e;
    }
    void L(Vec2 a, Vec2 b, int lw = 50) { Add(Line(a, b, lw)); }
    void Thin(Vec2 a, Vec2 b) { L(a, b, 18); }
    void Hidden(Vec2 a, Vec2 b) {
        Entity e = Line(a, b, 25);
        e.linetype = (int16_t)FindLineType(L"HIDDEN2");
        Add(e);
    }
    void Text(Vec2 at, const std::wstring& s, double h = kH, int justify = 0) {
        TextData t;
        t.content = s;
        t.height = h;
        t.justify = justify;
        Add(MakeText(at, t));
    }
    void Label(Vec2 p, const std::wstring& s, Vec2 dir) { Add(MakePointLabel(p, s, kH, dir)); }
    void XY(double x0, double x1, double y = 0) {
        Thin({x0, y}, {x1, y});
        Label({x0, y}, L"X", {-1, 0.3});
        Label({x1, y}, L"Y", {1, 0.3});
    }
    // Title above, steps below everything drawn so far.
    void Finish(const std::wstring& title, const std::wstring& steps, const std::wstring& file, const std::wstring& dir) {
        const Box b = doc.Extents();
        Text({b.minX, b.maxY + 12}, title, 5);
        Text({b.minX, b.minY - 10}, L"STEPS\n" + steps, 3);
        doc.SetProperty(L"title", title.substr(0, title.find(L'\n')));
        doc.SetProperty(L"drawnby", L"Feather CAD example");
        SheetList sheets;
        sheets.push_back(NewSheet(doc, L"Sheet1", L"A3", true));
        const std::string text = DrawingToText(doc, sheets);
        std::string path;
        for (wchar_t c : dir + L"/" + file) path.push_back((char)c);
        std::ofstream(path, std::ios::binary) << text;
        std::printf("%s\n", path.c_str());
    }
    Example() { ApplyBisSetup(doc, false); }
};

// Points of a 3D object in the front view (x, z) and top view (x, -y), XY at y = 0.
Vec2 FV(double x, double, double z, Vec2 o) { return o + Vec2{x, z}; }
Vec2 TV(double x, double y, double, Vec2 o) { return o + Vec2{x, -y}; }

void Conic(const std::wstring& dir) {
    Example x;
    std::vector<Entity> ents;
    std::wstring why;
    ConicCurve({0, 0}, 50, 2.0 / 3, kH, ents, why);
    x.AddAll(ents);
    // Tangent and normal at a point 30 above the axis.
    Entity curve;
    for (const Entity& e : ents)
        if (e.type == EntityType::Spline) curve = e;
    TangentNormal(curve, {60, 31}, 30, kH, ents);
    x.AddAll(ents);
    x.Finish(L"ELLIPSE BY THE ECCENTRICITY METHOD\nfocus 50 from the directrix, e = 2/3; tangent and normal at a point",
             L"1. CONIC: type 50 (focus from directrix), then 2/3 (eccentricity); click where the axis crosses the directrix.\n"
             L"2. TANNORM: click the ellipse, then the point: the tangent T-T and normal N-N.\n"
             L"3. By hand: AF = 50 divided at V so VF/VA = 2/3; verticals through points 1, 2, 3... on the axis; "
             L"arcs from F of radius e x (distance from the directrix) cut them.",
             L"Ellipse-by-eccentricity.fcad", dir);
}

void Cycloid(const std::wstring& dir) {
    Example x;
    std::vector<Entity> ents;
    std::wstring why;
    RollingCurve(Roll::Cycloid, {0, 0}, 50, 0, kH, ents, why);
    x.AddAll(ents);
    TangentNormal(ents[0], {100, 45}, 30, kH, ents);
    x.AddAll(ents);
    x.Finish(L"CYCLOID: a circle of 50 diameter rolls on a straight line for one turn",
             L"1. CYCLOID: type 50, click the start point P.\n"
             L"2. By hand: line P-P12 = pi x 50 = 157.08 in 12 parts; centres C1...C12 on the centre line;\n"
             L"   the circle in 12 parts; lines through its points parallel to the base; arcs of radius 25 from C1...C12 cut them.\n"
             L"3. TANNORM at any point: the normal passes through the point where the circle touches the base.",
             L"Cycloid.fcad", dir);
}

void Involute(const std::wstring& dir) {
    Example x;
    std::vector<Entity> ents;
    std::wstring why;
    InvoluteCurve({0, 0}, 0, 40, kH, ents, why);
    x.AddAll(ents);
    x.Finish(L"INVOLUTE OF A CIRCLE of 40 diameter (a string unwound for one turn)",
             L"1. INVOLUTE: type 0 (a circle), then 40; click the centre.\n"
             L"2. By hand: the circle in 12 parts; tangents at each point; on tangent k mark k/12 of pi x 40 = 125.66.\n"
             L"3. A smooth curve (SPLINE) through the ends.",
             L"Involute-of-a-circle.fcad", dir);
}

void Development(const std::wstring& dir) {
    Example x;
    // A square prism, base 40, axis 60, resting on HP with a side of the base parallel to VP,
    // cut by a plane at 30 deg to HP meeting the axis 30 above the base.
    const Vec2 fo{0, 10}, to{0, -40};
    x.XY(-40, 260);
    x.L(FV(-20, 0, 0, fo), FV(20, 0, 0, fo));
    const double t = std::tan(kPi / 6), lo = 30 - 20 * t, hi = 30 + 20 * t;
    x.L(FV(-20, 0, 0, fo), FV(-20, 0, lo, fo));
    x.L(FV(20, 0, 0, fo), FV(20, 0, hi, fo));
    x.L(FV(-20, 0, lo, fo), FV(20, 0, hi, fo));
    for (double xx : {-20.0, 20.0}) x.Thin(FV(xx, 0, xx < 0 ? lo : hi, fo), FV(xx, 0, 60, fo));
    x.Thin(FV(-20, 0, 60, fo), FV(20, 0, 60, fo));
    std::vector<Vertex> sq = {{TV(-20, -20, 0, to), 0}, {TV(20, -20, 0, to), 0}, {TV(20, 20, 0, to), 0}, {TV(-20, 20, 0, to), 0}};
    Entity s = Entity::Polyline(sq, true);
    s.lineweight = 50;
    x.Add(s);
    x.Label(TV(-20, -20, 0, to), L"a", {-1, -1});
    x.Label(TV(20, -20, 0, to), L"b", {1, -1});
    x.Label(TV(20, 20, 0, to), L"c", {1, 1});
    x.Label(TV(-20, 20, 0, to), L"d", {-1, 1});
    x.Label(FV(-20, 0, 0, fo), L"a'(d')", {-1, -1});
    x.Label(FV(20, 0, 0, fo), L"b'(c')", {1, -1});
    SolidSpec spec;
    spec.cut = true;
    std::vector<Entity> ents;
    std::wstring why;
    DevelopSolid(spec, {60, 10}, kH, ents, why);
    x.AddAll(ents);
    x.Finish(L"DEVELOPMENT OF A CUT SQUARE PRISM\nbase 40, axis 60, a side of the base parallel to VP; cut at 30 deg to HP through the axis 30 above the base",
             L"1. Draw the top view (square 40) and the front view (rectangle 40 x 60) with PROJECTOR lines; the cutting plane at 30 deg.\n"
             L"2. DEVELOP: P (prism), 4, 40, 60, E (edge parallel to VP), Y (cut), 30, 30; click the place.\n"
             L"3. By hand: stretch-out 4 x 40 = 160; fold lines A, B, C, D, A; carry the cut heights across from the front view.",
             L"Development-cut-square-prism.fcad", dir);
}

void TrueLength(const std::wstring& dir) {
    Example x;
    x.XY(-30, 150);
    LineViews v;
    std::wstring why;
    ViewsFromTrueLength({0, 20}, {0, -15}, 80, ToRadians(30), ToRadians(45), true, v, why);
    x.AddAll(TrueLengthConstruction(v, kH, true));
    x.Finish(L"TRUE LENGTH OF A LINE\nAB 80 long, at 30 deg to HP and 45 deg to VP; A 20 above HP and 15 in front of VP",
             L"1. TRUELENGTH, D: click a' (20 above XY), then a (15 below it); type 80, 30, 45: both views and the construction.\n"
             L"2. Or: draw the two views, TRUELENGTH, click the front view, then the top view: TL and the angles.\n"
             L"3. By hand: turn the top view level about a (arc), project up to the locus of b': a'b1' is the true length at theta.",
             L"True-length-of-a-line.fcad", dir);
}

void Section(const std::wstring& dir) {
    Example x;
    // A square pyramid, base 40 (a side parallel to VP), axis 60, cut by a plane at 30 deg to HP
    // meeting the axis 25 above the base: the apparent section in the top view, then its true shape.
    const Vec2 fo{0, 10}, to{0, -40};
    x.XY(-40, 200);
    const double h = 60, c = 25, t = std::tan(kPi / 6);
    const Vec2 corners[4] = {{-20, -20}, {20, -20}, {20, 20}, {-20, 20}};
    x.L(FV(-20, 0, 0, fo), FV(20, 0, 0, fo));
    x.L(FV(-20, 0, 0, fo), FV(0, 0, h, fo), 18);
    x.L(FV(20, 0, 0, fo), FV(0, 0, h, fo), 18);
    std::vector<Vec2> secTV, secFV;
    for (Vec2 k : corners) {
        // Edge from the corner to the apex: z = h u, x = kx (1 - u).
        const double u = (c + t * k.x) / (h + t * k.x);
        const double px = k.x * (1 - u), py = k.y * (1 - u), pz = h * u;
        secFV.push_back(FV(px, py, pz, fo));
        secTV.push_back(TV(px, py, pz, to));
        x.Thin(TV(k.x, k.y, 0, to), TV(0, 0, 0, to));
    }
    for (size_t i = 0; i < 4; ++i) x.Thin(TV(corners[i].x, corners[i].y, 0, to), TV(corners[(i + 1) % 4].x, corners[(i + 1) % 4].y, 0, to));
    std::vector<Vertex> sv;
    for (Vec2 p : secTV) sv.push_back({p, 0});
    Entity sec = Entity::Polyline(sv, true);
    sec.lineweight = 50;
    x.Add(sec);
    HatchData hd;
    hd.loops.push_back(secTV);
    x.Add(MakeHatch(hd));
    // The cutting plane in the front view, then the true shape.
    const Vec2 a = FV(-35, 0, c - 35 * t, fo), b = FV(35, 0, c + 35 * t, fo);
    x.AddAll(CuttingPlane({a, b}, a + Vec2{-10, 20}, L"A", kH));
    TrueShapeResult r;
    std::wstring why;
    TrueShape(a, b, secTV, FV(40, 0, 75, fo), true, 0, r, why);
    for (const auto& pr : r.projectors) x.Thin(pr.first, pr.second);
    x.Thin(r.x1, r.y1);
    std::vector<Vertex> tv;
    for (Vec2 p : r.shape) tv.push_back({p, 0});
    Entity ts = Entity::Polyline(tv, true);
    ts.lineweight = 50;
    x.Add(ts);
    HatchData th;
    th.loops.push_back(r.shape);
    x.Add(MakeHatch(th));
    x.Label(r.x1, L"X1", {-1, 0.3});
    x.Label(r.y1, L"Y1", {1, 0.3});
    x.Finish(L"SECTION OF A SQUARE PYRAMID AND ITS TRUE SHAPE\nbase 40, axis 60; cut at 30 deg to HP meeting the axis 25 above the base",
             L"1. Front and top views; the cutting plane line in the front view (CUTPLANE, letter A).\n"
             L"2. Where it cuts each edge in the front view, project down to the same edge in the top view; join and hatch: the apparent section.\n"
             L"3. TRUESHAPE: click the cutting plane line, the apparent section, XY, then where X1Y1 goes: the true shape, hatched.",
             L"Section-and-true-shape-of-a-pyramid.fcad", dir);
}

void Isometric(const std::wstring& dir) {
    Example x;
    // An L block: 60 long, 40 deep, 15 high, with an upright 15 x 40 x 40 at the back left; a 20 hole on its face.
    auto P = [](double X, double Y, double Z) { return Vec2{(X - Y) * std::cos(kPi / 6), (X + Y) * 0.5 + Z}; };
    // Seen from the front left: the tops, the faces at y = 0 and y = 25 and the L-shaped face at x = 0 show.
    const double E[][6] = {
        {0, 0, 0, 60, 0, 0},     {60, 0, 0, 60, 0, 15},   {0, 0, 15, 60, 0, 15},   {60, 0, 15, 60, 25, 15},
        {0, 25, 15, 60, 25, 15}, {0, 0, 0, 0, 40, 0},     {0, 40, 0, 0, 40, 55},   {0, 40, 55, 0, 25, 55},
        {0, 25, 55, 0, 25, 15},  {0, 25, 15, 0, 0, 15},   {0, 0, 15, 0, 0, 0},     {60, 25, 15, 60, 25, 55},
        {0, 25, 55, 60, 25, 55}, {60, 25, 55, 60, 40, 55}, {60, 40, 55, 0, 40, 55},
    };
    for (const auto& e : E) x.L(P(e[0], e[1], e[2]), P(e[3], e[4], e[5]));
    // A 20 hole in the upright's front face (the left/right plane: x along 30 deg, z up).
    x.Add(IsoCircle(P(30, 25, 35), 10, IsoPlane::Right));
    x.Finish(L"ISOMETRIC VIEW OF AN L BLOCK (true lengths)\nbase 60 x 40 x 15, upright 60 x 15 x 40 at the back, a hole of 20 diameter",
             L"1. ISODRAFT on (or the Iso switch on the status bar); Ortho keeps lines on the 30 deg and upright axes.\n"
             L"2. LINE with typed lengths: @60<30, @40<150, @15<90...; F5 changes the plane (left, top, right).\n"
             L"3. ISOCIRCLE on the right plane for the hole. For isometric projection, ISODRAFT > Scale draws lengths x 0.816.",
             L"Isometric-L-block.fcad", dir);
}

void Circuit(const std::wstring& dir) {
    Example x;
    Document& d = x.doc;
    auto place = [&](const wchar_t* name, Vec2 at, double rot, const std::wstring& label) {
        auto def = SymbolBlock(d, name);
        std::vector<std::wstring> v;
        for (const Entity& e : def->items)
            if (const TextData* t = TextOf(e); t && !t->tag.empty()) v.push_back(label);
        Entity ins = MakeInsert(def, at, 1, rot, v);
        ins.layer = d.Current().layer;
        return d.Add(ins);
    };
    auto wire = [&](uint64_t a, int pa, uint64_t b, int pb) {
        Vec2 p, q, da, db;
        PortPoint(d, {a, pa}, p, da);
        PortPoint(d, {b, pb}, q, db);
        Entity w = MakeConnector(d, {a, pa}, p, {b, pb}, q, false, 0);
        w.layer = d.Current().layer;
        w.lineweight = 35;
        d.Add(w);
    };
    const uint64_t bat = place(L"BATTERY", {0, 0}, kPi / 2, L"B1 9 V");
    const uint64_t sw = place(L"SWITCH", {20, 40}, 0, L"S1");
    const uint64_t r = place(L"RESISTOR", {60, 40}, 0, L"R1 470");
    const uint64_t led = place(L"LED", {100, 20}, -kPi / 2, L"D1");
    wire(bat, 1, sw, 0);
    wire(sw, 1, r, 0);
    wire(r, 1, led, 0);
    wire(led, 1, bat, 0);
    // A half adder below: XOR for the sum, AND for the carry.
    const uint64_t g1 = place(L"XOR", {40, -60}, 0, L"SUM");
    const uint64_t g2 = place(L"AND", {40, -90}, 0, L"CARRY");
    const uint64_t a = place(L"TERMINAL", {0, -57}, 0, L"A");
    const uint64_t b = place(L"TERMINAL", {0, -63}, 0, L"B");
    wire(a, 0, g1, 0);
    wire(b, 0, g1, 1);
    wire(a, 0, g2, 0);
    wire(b, 0, g2, 1);
    x.Finish(L"SIMPLE ELECTRONIC CIRCUITS\nan LED with its resistor and switch on a 9 V battery; a half adder",
             L"1. SYMBOL: pick Battery, Switch, Resistor, LED from the Electrical group; type a label; click to place (R rotates 90 deg).\n"
             L"2. WIRE: click the end of one lead, then the end of the next: the wire bends square and stays joined when a symbol moves.\n"
             L"3. Logic gates: XOR and AND from the Logic gates group; terminals for the inputs A and B.",
             L"Electronic-circuit.fcad", dir);
}

void Flowchart(const std::wstring& dir) {
    Example x;
    Document& d = x.doc;
    auto place = [&](const wchar_t* name, Vec2 at, const std::wstring& text) {
        auto def = SymbolBlock(d, name);
        Entity ins = MakeInsert(def, at, 1, 0, {text});
        ins.layer = d.Current().layer;
        return d.Add(ins);
    };
    auto flow = [&](uint64_t a, int pa, uint64_t b, int pb) {
        Vec2 p, q, da, db;
        PortPoint(d, {a, pa}, p, da);
        PortPoint(d, {b, pb}, q, db);
        Entity w = MakeConnector(d, {a, pa}, p, {b, pb}, q, false, 3);
        w.layer = d.Current().layer;
        d.Add(w);
    };
    const uint64_t s = place(L"TERMINATOR", {0, 0}, L"Start");
    const uint64_t in = place(L"INPUT_OUTPUT", {0, -35}, L"Read A, B");
    const uint64_t dec = place(L"DECISION", {0, -75}, L"A > B ?");
    const uint64_t pa = place(L"INPUT_OUTPUT", {-45, -115}, L"Print A");
    const uint64_t pb = place(L"INPUT_OUTPUT", {45, -115}, L"Print B");
    const uint64_t e = place(L"TERMINATOR", {0, -155}, L"Stop");
    flow(s, 1, in, 0);
    flow(in, 1, dec, 0);
    flow(dec, 2, pa, 0);
    flow(dec, 3, pb, 0);
    flow(pa, 1, e, 2);
    flow(pb, 1, e, 3);
    x.Text({-30, -70}, L"Yes", 3);
    x.Text({24, -70}, L"No", 3);
    x.Finish(L"FLOWCHART: the larger of two numbers",
             L"1. SYMBOL, Flowchart group: Start / stop, Input / output, Decision; type the text inside; click to place.\n"
             L"2. WIRE from one shape's side to the next: flow lines get arrows by themselves on flowchart shapes.\n"
             L"3. TEXT for Yes / No beside the decision's exits.",
             L"Flowchart-larger-of-two-numbers.fcad", dir);
}

void Network(const std::wstring& dir) {
    Example x;
    Document& d = x.doc;
    auto place = [&](const wchar_t* name, Vec2 at, const std::wstring& label) {
        auto def = SymbolBlock(d, name);
        Entity ins = MakeInsert(def, at, 1, 0, {label});
        ins.layer = d.Current().layer;
        return d.Add(ins);
    };
    auto link = [&](uint64_t a, int pa, uint64_t b, int pb) {
        Vec2 p, q, da, db;
        PortPoint(d, {a, pa}, p, da);
        PortPoint(d, {b, pb}, q, db);
        Entity w = MakeConnector(d, {a, pa}, p, {b, pb}, q, true, 0);
        w.layer = d.Current().layer;
        d.Add(w);
    };
    const uint64_t cloud = place(L"CLOUD", {0, 70}, L"");
    const uint64_t router = place(L"ROUTER", {0, 40}, L"Router");
    const uint64_t sw = place(L"NETWORK_SWITCH", {0, 0}, L"Switch");
    link(cloud, 1, router, 0);
    link(router, 1, sw, 0);
    const wchar_t* devices[] = {L"PC", L"PC", L"LAPTOP", L"PRINTER", L"SERVER"};
    const wchar_t* names[] = {L"PC1", L"PC2", L"Laptop", L"Printer", L"Server"};
    for (int k = 0; k < 5; ++k) {
        const uint64_t dv = place(devices[k], {-60 + 30.0 * k, -45}, names[k]);
        link(sw, 1, dv, 0);
    }
    const uint64_t ap = place(L"ACCESS_POINT", {60, 0}, L"Wi-Fi");
    link(sw, 3, ap, 2);
    const uint64_t ph = place(L"MOBILE", {90, -30}, L"Phone");
    (void)ph;
    x.Finish(L"STAR TOPOLOGY NETWORK\na router to the Internet, a switch, PCs, a laptop, a printer, a server and Wi-Fi",
             L"1. SYMBOL, Network group: Cloud, Router, Switch, PC, Laptop, Printer, Server, Access point.\n"
             L"2. WIRE between them: network links are drawn straight; they stay joined when a device moves.\n"
             L"3. Wireless devices (the phone) are shown without a link.",
             L"Network-star-topology.fcad", dir);
}
}  // namespace

int main(int argc, char** argv) {
    std::wstring dir = L"samples";
    if (argc > 1) {
        dir.clear();
        for (const char* p = argv[1]; *p; ++p) dir.push_back((wchar_t)*p);
    }
    Conic(dir);
    Cycloid(dir);
    Involute(dir);
    Development(dir);
    TrueLength(dir);
    Section(dir);
    Isometric(dir);
    Circuit(dir);
    Flowchart(dir);
    Network(dir);
    return 0;
}
