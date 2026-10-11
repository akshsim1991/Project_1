// CoreTests.cpp - unit tests for the geometry, entities, undo history and
// file formats (no window needed). Prints each failure and returns the
// number of failures, so CI fails when anything breaks.
//   cmake -DFEATHERCAD_TESTS=ON ...   then run CoreTests.exe
#include <cstdio>
#include <string>

#include "Document.h"
#include "Engine.h"
#include "Snap.h"

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAILED (line %d): %s\n", line, what);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)
bool Close(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
bool Close(Vec2 a, Vec2 b, double tol = 1e-9) { return Dist(a, b) <= tol; }

void TestAngles() {
    CHECK(Close(NormalizeAngle(-kPi / 2), 3 * kPi / 2));
    CHECK(Close(NormalizeAngle(5 * kPi), kPi));
    CHECK(Close(SweepCCW(ToRadians(350), ToRadians(10)), ToRadians(20)));
    CHECK(AngleInSweep(0, ToRadians(350), ToRadians(20)));
    CHECK(!AngleInSweep(kPi, ToRadians(350), ToRadians(20)));
}

void TestIntersections() {
    std::vector<Vec2> pts;
    Intersect(Prim::Seg({0, 0}, {10, 10}), Prim::Seg({0, 10}, {10, 0}), pts);
    CHECK(pts.size() == 1 && Close(pts[0], {5, 5}));

    pts.clear();  // parallel
    Intersect(Prim::Seg({0, 0}, {10, 0}), Prim::Seg({0, 1}, {10, 1}), pts);
    CHECK(pts.empty());

    pts.clear();  // segment through a circle
    Intersect(Prim::Seg({-20, 0}, {20, 0}), Prim::Circle({0, 0}, 10), pts);
    CHECK(pts.size() == 2);

    pts.clear();  // tangent line touches once
    Intersect(Prim::Seg({-20, 10}, {20, 10}), Prim::Circle({0, 0}, 10), pts);
    CHECK(pts.size() == 1 && Close(pts[0], {0, 10}, 1e-6));

    pts.clear();  // upper half arc only meets the line once
    Intersect(Prim::Seg({-20, 5}, {20, 5}), Prim::ArcCCW({0, 0}, 10, 0, kPi), pts);
    CHECK(pts.size() == 2);
    pts.clear();
    Intersect(Prim::Seg({-20, -5}, {20, -5}), Prim::ArcCCW({0, 0}, 10, 0, kPi), pts);
    CHECK(pts.empty());

    pts.clear();  // two circles
    Intersect(Prim::Circle({0, 0}, 5), Prim::Circle({8, 0}, 5), pts);
    CHECK(pts.size() == 2 && Close(pts[0].x, 4) && Close(std::fabs(pts[0].y), 3));
}

void TestArcs() {
    // A half circle bulge (bulge 1) from (0,0) to (10,0), counter-clockwise:
    // it goes below the chord.
    Prim p = ArcFromBulge({0, 0}, {10, 0}, 1);
    CHECK(p.kind == Prim::Arc);
    CHECK(Close(p.c, {5, 0}) && Close(p.r, 5));
    CHECK(Close(p.Mid(), {5, -5}));
    Prim q = ArcFromBulge({0, 0}, {10, 0}, -1);
    CHECK(Close(q.Mid(), {5, 5}));

    // Tangent bulge: leaving (0,0) along +X towards (10,10) is a quarter circle.
    const double b = TangentBulge({0, 0}, {1, 0}, {10, 10});
    CHECK(Close(b, std::tan(kPi / 8)));
    Prim t = ArcFromBulge({0, 0}, {10, 10}, b);
    CHECK(Close(t.c, {0, 10}) && Close(t.sweep, kPi / 2));

    Vec2 c;
    double r;
    CHECK(CircleFrom3Points({10, 0}, {0, 10}, {-10, 0}, c, r) && Close(c, {0, 0}) && Close(r, 10));
    CHECK(!CircleFrom3Points({0, 0}, {1, 1}, {2, 2}, c, r));

    Box bb = Prim::ArcCCW({0, 0}, 10, ToRadians(45), ToRadians(90)).Bounds();
    CHECK(Close(bb.maxY, 10) && Close(bb.minY, 10 * std::sin(ToRadians(45))));

    std::vector<Vec2> tp;
    TangentPoints(Prim::Circle({0, 0}, 5), {10, 0}, tp);
    CHECK(tp.size() == 2 && Close(Dot(tp[0] - Vec2{10, 0}, tp[0]), 0, 1e-9));
}

void TestEntities() {
    Entity rect = Entity::Rectangle({0, 0}, {20, 10});
    CHECK(SegmentCount(rect) == 4);
    CHECK(Close(LengthOf(rect), 60));
    CHECK(InsideBox(rect, Box({-1, -1}, {21, 11})));
    CHECK(!InsideBox(rect, Box({-1, -1}, {10, 11})));
    CHECK(TouchesBox(rect, Box({5, -1}, {6, 1})));
    CHECK(!TouchesBox(rect, Box({5, 2}, {6, 3})));  // inside, touching no edge

    Entity circle = Entity::Circle({0, 0}, 10);
    CHECK(Close(DistanceTo(circle, {0, 0}), 10));
    CHECK(TouchesBox(circle, Box({9, -1}, {11, 1})));
    CHECK(!TouchesBox(circle, Box({-1, -1}, {1, 1})));

    Entity line = Entity::Line({0, 0}, {10, 0});
    Rotate(line, {0, 0}, kPi / 2);
    CHECK(Close(line.p1, {0, 10}));
    Translate(line, {5, 5});
    CHECK(Close(line.p0, {5, 5}));

    Entity arc = Entity::Arc({0, 0}, 10, 0, kPi / 2);
    Rotate(arc, {0, 0}, kPi);
    CHECK(Close(arc.startAngle, kPi) && Close(arc.endAngle, 3 * kPi / 2));
    std::vector<Vec2> q;
    Quadrants(arc, q);
    CHECK(q.size() == 2);
}

void TestUndo() {
    Document doc;
    doc.BeginChange(L"Line");
    uint64_t a = doc.Add(Entity::Line({0, 0}, {1, 0}));
    uint64_t b = doc.Add(Entity::Line({1, 0}, {1, 1}));
    doc.EndChange();
    CHECK(doc.Count() == 2 && doc.UndoDepth() == 1);
    CHECK(doc.Modified());
    doc.MarkSaved();
    CHECK(!doc.Modified());

    doc.BeginChange(L"Move");
    Entity moved = *doc.Find(a);
    Translate(moved, {5, 0});
    doc.Replace(moved);
    Translate(moved, {5, 0});
    doc.Replace(moved);
    doc.Erase(b);
    doc.EndChange();
    CHECK(doc.Count() == 1 && Close(doc.Find(a)->p0, {10, 0}));
    CHECK(doc.Modified());

    CHECK(doc.Undo());
    CHECK(doc.Count() == 2 && Close(doc.Find(a)->p0, {0, 0}) && doc.Find(b));
    CHECK(!doc.Modified());  // back at the saved state
    CHECK(doc.Redo());
    CHECK(doc.Count() == 1 && Close(doc.Find(a)->p0, {10, 0}));
    CHECK(doc.Undo() && doc.Undo());
    CHECK(doc.Count() == 0 && !doc.CanUndo() && doc.CanRedo());

    // Add then erase in one group leaves no step; cancel takes changes back.
    doc.BeginChange(L"Nothing");
    uint64_t c = doc.Add(Entity::Circle({0, 0}, 1));
    doc.Erase(c);
    doc.EndChange();
    CHECK(!doc.CanUndo() && doc.CanRedo());
    doc.BeginChange(L"Cancelled");
    doc.Add(Entity::Circle({0, 0}, 1));
    doc.CancelChange();
    CHECK(doc.Count() == 0);

    // Undo keeps the drawing order (ids).
    Document d2;
    uint64_t x = d2.Add(Entity::Line({0, 0}, {1, 0}));
    d2.Add(Entity::Line({0, 0}, {2, 0}));
    d2.Erase(x);
    d2.Undo();
    CHECK(d2.Entities().begin()->first == x);

    // Replace everything (restore a version) is one step.
    d2.ReplaceAll({Entity::Circle({1, 1}, 2)}, L"Restore");
    CHECK(d2.Count() == 1 && d2.Entities().begin()->second.type == EntityType::Circle);
    d2.Undo();
    CHECK(d2.Count() == 2);
}

void TestFiles() {
    Document doc;
    doc.Add(Entity::Line({0.1, 0.2}, {1e6 / 3, -7.25}));
    doc.Add(Entity::Circle({5, 5}, 2.5));
    doc.Add(Entity::Arc({1, 2}, 3, ToRadians(30), ToRadians(300)));
    doc.Add(Entity::Polyline({{{0, 0}, 0.5}, {{10, 0}, 0}, {{10, 10}, -1}}, true));
    DrawingSettings s;
    s.units = Units::Inches;
    s.precision = 3;
    doc.SetSettings(s);
    const std::string text = doc.ToText();

    Document back;
    std::wstring err;
    CHECK(back.FromText(text, err));
    CHECK(back.Count() == 4);
    CHECK(back.Settings().units == Units::Inches && back.Settings().precision == 3);
    CHECK(back.ToText() == text);  // exact round trip
    const Entity& l = back.Entities().begin()->second;
    CHECK(l.p1.x == 1e6 / 3);

    Document bad;
    CHECK(!bad.FromText("hello\n", err));
    CHECK(!bad.FromText("FeatherCAD 1\nline 1 2 x 4\nend\n", err));
    CHECK(!bad.FromText("FeatherCAD 1\nline 1 2 3 4\n", err));  // cut off
    CHECK(bad.FromText("FeatherCAD 9\nwidget 1 2\nline 1 2 3 4 color=red\nend\n", err) && bad.Count() == 1);

    const std::string dxf = doc.ToDxf();
    CHECK(dxf.find("AC1009") != std::string::npos);
    CHECK(dxf.find("POLYLINE") != std::string::npos && dxf.find("SEQEND") != std::string::npos);
    CHECK(dxf.rfind("EOF") != std::string::npos);
}

void TestSnaps() {
    Document doc;
    doc.Add(Entity::Line({0, 0}, {10, 0}));
    doc.Add(Entity::Line({5, -5}, {5, 5}));
    doc.Add(Entity::Circle({20, 0}, 5));
    SnapQuery q;
    q.modes = kSnapAll & ~kSnapNearest;
    q.aperture = 1;

    q.cursor = {9.6, 0.3};
    SnapResult r = FindSnap(doc, q);
    CHECK(r.found && r.mode == kSnapEndpoint && Close(r.point, {10, 0}));

    q.cursor = {5.3, 0.2};
    r = FindSnap(doc, q);
    CHECK(r.found && r.mode == kSnapIntersection && Close(r.point, {5, 0}));

    q.cursor = {20.4, 5.2};
    r = FindSnap(doc, q);
    CHECK(r.found && r.mode == kSnapQuadrant && Close(r.point, {20, 5}));

    q.cursor = {24.0, 3.3};  // on the circle, away from special points
    r = FindSnap(doc, q);
    CHECK(!r.found || r.mode != kSnapEndpoint);

    q.modes = kSnapCentre;
    q.cursor = {24.0, 3.0};  // hovering the circle finds its centre
    r = FindSnap(doc, q);
    CHECK(r.found && Close(r.point, {20, 0}));

    q.modes = kSnapPerpendicular;
    q.hasBase = true;
    q.base = {2, 8};
    q.cursor = {2.3, 0.4};
    r = FindSnap(doc, q);
    CHECK(r.found && Close(r.point, {2, 0}));

    q.modes = kSnapTangent;
    q.base = {30, 0};
    q.cursor = {22.0, 4.5};
    r = FindSnap(doc, q);
    CHECK(r.found && Close(Dot(r.point - Vec2{30, 0}, r.point - Vec2{20, 0}), 0, 1e-6));

    q.modes = kSnapNearest;
    q.hasBase = false;
    q.cursor = {3, 0.5};
    r = FindSnap(doc, q);
    CHECK(r.found && Close(r.point, {3, 0}));
}

// A host without a window: records messages and zoom requests.
struct TestHost : EngineHost {
    std::vector<std::wstring> lines;
    Vec2 cursor{1, 0};
    int extents = 0;
    std::wstring hostCommand;
    void Message(const std::wstring& l) override { lines.push_back(l); }
    Vec2 TrackedCursor() override { return cursor; }
    bool HostCommand(const std::wstring& n) override { hostCommand = n; return true; }
    void ZoomExtents() override { ++extents; }
    void ZoomWindow(const Box&) override {}
    void ZoomPrevious() override {}
    void ZoomFactor(double) override {}
    void DraftingChanged() override {}
};

void Type(Engine& e, std::initializer_list<const wchar_t*> inputs) {
    for (const wchar_t* in : inputs) e.Text(in);
}

const Entity& Last(const Document& d) { return d.Entities().rbegin()->second; }

void TestEngine() {
    Document doc;
    TestHost host;
    Engine eng(doc, host);

    // LINE with absolute, relative, polar and direct distance input, then Close.
    host.cursor = {200, 50};  // the cursor is to the right of (100,50)
    Type(eng, {L"l", L"0,0", L"100,0", L"@0,50", L"25"});
    CHECK(eng.Active() && doc.Count() == 3);
    CHECK(Close(Last(doc).p1, {125, 50}));
    Type(eng, {L"U"});  // undo the last segment
    CHECK(doc.Count() == 2);
    Type(eng, {L"@50<180", L"c"});
    CHECK(!eng.Active() && doc.Count() == 4);
    CHECK(Close(Last(doc).p1, {0, 0}));
    CHECK(doc.UndoName() == L"Line");
    Type(eng, {L"u"});  // UNDO command takes back the whole LINE
    CHECK(doc.Count() == 0);
    Type(eng, {L"redo"});
    CHECK(doc.Count() == 4);

    // Enter repeats the last command.
    eng.Enter();
    CHECK(eng.ActiveName() == L"REDO" || !eng.Active());

    // CIRCLE by radius and by remembered default, diameter, 2P and 3P.
    Type(eng, {L"circle", L"10,10", L"5"});
    CHECK(Last(doc).type == EntityType::Circle && Close(Last(doc).radius, 5));
    Type(eng, {L"c", L"20,20", L""});
    CHECK(Close(Last(doc).radius, 5) && Close(Last(doc).p0, {20, 20}));
    Type(eng, {L"c", L"0,0", L"d", L"30"});
    CHECK(Close(Last(doc).radius, 15));
    Type(eng, {L"c", L"2p", L"0,0", L"10,0"});
    CHECK(Close(Last(doc).p0, {5, 0}) && Close(Last(doc).radius, 5));
    Type(eng, {L"c", L"3p", L"10,0", L"0,10", L"-10,0"});
    CHECK(Close(Last(doc).p0, {0, 0}) && Close(Last(doc).radius, 10));
    const size_t before = doc.Count();
    Type(eng, {L"c", L"3p", L"0,0", L"1,1", L"2,2"});  // collinear: refused, still asking
    CHECK(doc.Count() == before && eng.Active());
    eng.Cancel();

    // ARC: three points (counter-clockwise through the middle point).
    Type(eng, {L"a", L"10,0", L"0,10", L"-10,0"});
    CHECK(Last(doc).type == EntityType::Arc && Close(Last(doc).startAngle, 0) && Close(Last(doc).endAngle, kPi));
    // Clockwise order gives the same arc from the other end.
    Type(eng, {L"a", L"-10,0", L"0,10", L"10,0"});
    CHECK(Close(Last(doc).startAngle, 0) && Close(Last(doc).endAngle, kPi));
    // Centre, start, included angle.
    Type(eng, {L"arc", L"c", L"0,0", L"10,0", L"a", L"90"});
    CHECK(Close(Last(doc).endAngle, kPi / 2) && Close(Last(doc).radius, 10));
    // Start, centre, end.
    Type(eng, {L"arc", L"0,10", L"c", L"0,0", L"-10,0"});
    CHECK(Close(Last(doc).startAngle, kPi / 2) && Close(Last(doc).endAngle, kPi));

    // RECTANG by corners and by dimensions.
    Type(eng, {L"rec", L"0,0", L"@40,20"});
    CHECK(Last(doc).type == EntityType::Polyline && Last(doc).closed && Close(LengthOf(Last(doc)), 120));
    Type(eng, {L"rec", L"100,100", L"d", L"30", L"10", L"0,0"});  // picked down-left
    CHECK(Close(Last(doc).verts[2].p, {70, 90}));

    // PLINE with a tangent arc and close.
    Type(eng, {L"pl", L"0,0", L"10,0", L"a", L"10,10", L"l", L"0,10", L"c"});
    const Entity& pl = Last(doc);
    CHECK(pl.type == EntityType::Polyline && pl.closed && pl.verts.size() == 4);
    CHECK(Close(pl.verts[1].bulge, 1));  // half circle continuing the +X direction
    CHECK(Close(LengthOf(pl), 10 + 5 * kPi + 10 + 10));

    // MOVE / COPY / ROTATE / ERASE on a pre-selection.
    doc.Clear();
    uint64_t id = doc.Add(Entity::Line({0, 0}, {10, 0}));
    eng.AddToSelection({id});
    Type(eng, {L"m", L"0,0", L"5,5"});
    CHECK(Close(doc.Find(id)->p0, {5, 5}) && eng.Selection().empty());
    eng.AddToSelection({id});
    Type(eng, {L"co", L"0,0", L"0,10", L"0,20", L""});
    CHECK(doc.Count() == 3);
    eng.AddToSelection({id});
    Type(eng, {L"ro", L"5,5", L"90"});
    CHECK(Close(doc.Find(id)->p1, {5, 15}));
    eng.AddToSelection({id});
    Type(eng, {L"ro", L"5,5", L"r", L"90", L"180"});  // reference 90 -> new 180
    CHECK(Close(doc.Find(id)->p1, {-5, 5}));
    // Without a pre-selection the command asks for objects; ALL picks everything.
    Type(eng, {L"erase", L"all", L""});
    CHECK(doc.Count() == 0);
    Type(eng, {L"u"});
    CHECK(doc.Count() == 3);
    // Move with a typed displacement.
    eng.SelectAll();
    Type(eng, {L"move", L"d", L"100,0"});
    CHECK(Close(doc.Find(id)->p1, {95, 5}));

    // Errors are reported, not crashes.
    host.lines.clear();
    Type(eng, {L"nosuchcommand"});
    CHECK(!host.lines.empty() && !eng.Active());
    Type(eng, {L"line", L"hello"});
    CHECK(eng.Active());
    eng.Cancel();
    CHECK(!eng.Active());

    // Settings commands and host commands.
    Type(eng, {L"grid", L"5"});
    CHECK(eng.Draft().gridSpacing == 5 && eng.Draft().gridOn);
    Type(eng, {L"ortho", L"on"});
    CHECK(eng.Draft().orthoOn);
    Type(eng, {L"save"});
    CHECK(host.hostCommand == L"SAVE");
    Type(eng, {L"z", L"e"});
    CHECK(host.extents == 1);

    // One-shot snap keywords are taken at point prompts.
    Type(eng, {L"line", L"end"});
    CHECK(eng.SnapOverride() == kSnapEndpoint);
    eng.Cancel();

    // Formatting.
    CHECK(FormatDecimal(-0.001, 2) == L"0.00");
    CHECK(FormatDegrees(ToRadians(12.5), 4) == L"12.5");
    CHECK(FormatDegrees(ToRadians(90), 2) == L"90");
}
}  // namespace

int main() {
    TestAngles();
    TestIntersections();
    TestArcs();
    TestEntities();
    TestUndo();
    TestFiles();
    TestSnaps();
    TestEngine();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
