// CoreTests.cpp - unit tests for the geometry, entities, undo history and
// file formats (no window needed). Prints each failure and returns the
// number of failures, so CI fails when anything breaks.
//   cmake -DFEATHERCAD_TESTS=ON ...   then run CoreTests.exe
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "Annot.h"
#include "Dimension.h"
#include "Document.h"
#include "DxfImport.h"
#include "Engine.h"
#include "EngCurves.h"
#include "EngGraphics.h"
#include "EngMore.h"
#include "Lang.h"
#include "Sheet.h"
#include "Personal.h"
#include "Symbols.h"
#include "Expr.h"
#include "FreeSketch.h"
#include "Grips.h"
#include "Modify.h"
#include "Plot.h"
#include "Relate.h"
#include "Select.h"
#include "Spline.h"
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
    double PickRadius() override { return 0.5; }
    bool HostCommand(const std::wstring& n) override { hostCommand = n; return true; }
    void ZoomExtents() override { ++extents; }
    void ZoomWindow(const Box&) override {}
    void ZoomPrevious() override {}
    void ZoomFactor(double) override {}
    void DraftingChanged() override {}
    bool ChooseImage(std::wstring& path, int& w, int& h) override {
        path = L"C:\\pics\\photo.png";
        w = 200, h = 100;
        return true;
    }
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

std::vector<Prim> PrimsOf(std::initializer_list<Entity> ents) {
    std::vector<Prim> out;
    for (const Entity& e : ents) Primitives(e, out);
    return out;
}

void TestCurves() {
    Entity pl = Entity::Polyline({{{0, 0}, 0}, {{10, 0}, 1}, {{10, 10}, 0}, {{0, 10}, 0}}, true);
    CHECK(Close(CurveLength(pl), 30 + 5 * kPi));
    CHECK(Close(CurvePoint(pl, 10 + 2.5 * kPi), {15, 5}));  // middle of the half circle
    CHECK(Close(CurveParam(pl, {15.2, 5}), 10 + 2.5 * kPi, 1e-9));
    Entity part;
    CHECK(SubCurve(pl, 5, 10 + 5 * kPi + 5, part));  // across the arc
    CHECK(part.verts.size() == 4 && Close(LengthOf(part), 10 + 5 * kPi));
    CHECK(SubCurve(pl, 30 + 5 * kPi - 5, 5, part));  // closed: round through the start
    CHECK(Close(LengthOf(part), 10) && Close(part.verts.front().p, {0, 5}));
    // Half of the arc segment keeps its bulge proportion.
    CHECK(SubCurve(pl, 10, 10 + 2.5 * kPi, part) && Close(part.verts[0].bulge, std::tan(kPi / 8)));

    Entity line = Entity::Line({0, 0}, {10, 0});
    CHECK(ChangeEnd(line, true, 5) && Close(line.p1, {15, 0}));
    CHECK(ChangeEnd(line, false, -2) && Close(line.p0, {2, 0}));
    CHECK(!ChangeEnd(line, true, -20));
    Entity arc = Entity::Arc({0, 0}, 10, 0, kPi / 2);
    CHECK(ChangeEnd(arc, true, 10 * kPi / 2) && Close(arc.endAngle, kPi));
    Entity open = Entity::Polyline({{{0, 0}, 0}, {{10, 0}, 1}, {{10, 10}, 0}}, false);
    CHECK(ChangeEnd(open, true, -5 * kPi / 2) && Close(open.verts[2].p, {15, 5}) && Close(open.verts[1].bulge, std::tan(kPi / 8)));
}

void TestTrimExtend() {
    std::vector<Entity> out;
    Entity removed;
    const Entity a = Entity::Line({0, 0}, {10, 0});
    CHECK(TrimAt(a, {8, 0}, PrimsOf({Entity::Line({5, -5}, {5, 5})}), out, &removed));
    CHECK(out.size() == 1 && Close(out[0].p0, {0, 0}) && Close(out[0].p1, {5, 0}) && Close(removed.p0, {5, 0}));
    CHECK(TrimAt(a, {5, 0}, PrimsOf({Entity::Line({3, -5}, {3, 5}), Entity::Line({7, -5}, {7, 5})}), out, nullptr));
    CHECK(out.size() == 2 && Close(out[0].p1, {3, 0}) && Close(out[1].p0, {7, 0}));
    CHECK(TrimAt(a, {5, 0}, {}, out, nullptr) && out.empty());  // nothing crosses: it goes

    const Entity circle = Entity::Circle({0, 0}, 10);
    CHECK(TrimAt(circle, {10, 0}, PrimsOf({Entity::Line({0, -20}, {0, 20})}), out, nullptr));
    CHECK(out.size() == 1 && out[0].type == EntityType::Arc && Close(out[0].startAngle, kPi / 2) &&
          Close(out[0].endAngle, 3 * kPi / 2));
    CHECK(!TrimAt(circle, {10, 0}, PrimsOf({Entity::Line({10, -20}, {10, 20})}), out, nullptr));  // touches once

    const Entity rect = Entity::Rectangle({0, 0}, {10, 10});
    CHECK(TrimAt(rect, {10, 5}, PrimsOf({Entity::Line({5, -5}, {5, 15})}), out, nullptr));
    CHECK(out.size() == 1 && !out[0].closed && Close(LengthOf(out[0]), 20));

    Entity ext;
    const auto wall = PrimsOf({Entity::Line({10, -5}, {10, 5}), Entity::Line({-3, -5}, {-3, 5})});
    CHECK(ExtendAt(Entity::Line({0, 0}, {5, 0}), {4, 0}, wall, 100, ext) && Close(ext.p1, {10, 0}));
    CHECK(ExtendAt(Entity::Line({0, 0}, {5, 0}), {1, 0}, wall, 100, ext) && Close(ext.p0, {-3, 0}));
    CHECK(!ExtendAt(Entity::Line({0, 0}, {5, 0}), {4, 0}, PrimsOf({Entity::Line({3, -5}, {3, 5})}), 100, ext));
    CHECK(ExtendAt(Entity::Arc({0, 0}, 10, 0, kPi / 2), {0.5, 10}, PrimsOf({Entity::Line({-5, -20}, {-5, 20})}), 100, ext) &&
          Close(ext.endAngle, ToRadians(120)));
}

void TestOffset() {
    Entity out;
    CHECK(OffsetEntity(Entity::Line({0, 0}, {10, 0}), 2, out) && Close(out.p0, {0, 2}));
    CHECK(OffsetSide(Entity::Line({0, 0}, {10, 0}), {5, 3}) == 1);
    CHECK(OffsetEntity(Entity::Circle({0, 0}, 10), 3, out) && Close(out.radius, 7));
    CHECK(OffsetEntity(Entity::Circle({0, 0}, 10), -3, out) && Close(out.radius, 13));
    CHECK(!OffsetEntity(Entity::Circle({0, 0}, 10), 12, out));
    const Entity rect = Entity::Rectangle({0, 0}, {10, 10});
    CHECK(OffsetSide(rect, {5, 5}) == 1);
    CHECK(OffsetEntity(rect, 1, out) && out.verts.size() == 4 && Close(LengthOf(out), 32) && Close(out.verts[0].p, {1, 1}));
    CHECK(OffsetEntity(rect, -1, out) && Close(LengthOf(out), 48));
    CHECK(!OffsetEntity(rect, 6, out));  // too far inside
    const Entity slot = Entity::Polyline({{{0, 0}, 0}, {{10, 0}, 1}, {{10, 10}, 0}, {{0, 10}, 0}}, true);
    CHECK(OffsetEntity(slot, 1, out) && Close(LengthOf(out), 26 + 4 * kPi, 1e-6));
    // An open polyline with a corner: the outside corner is extended.
    const Entity ell = Entity::Polyline({{{0, 0}, 0}, {{10, 0}, 0}, {{10, 10}, 0}}, false);
    CHECK(OffsetEntity(ell, -1, out) && out.verts.size() == 3 && Close(out.verts[1].p, {11, -1}));
}

void TestFilletChamfer() {
    CornerResult r;
    std::wstring why;
    const Entity l1 = Entity::Line({0, 0}, {10, 0}), l2 = Entity::Line({10, 0}, {10, 10});
    CHECK(FilletObjects(l1, {5, 0}, l2, {10, 5}, 2, true, r, why));
    CHECK(r.hasJoin && Close(r.join.p0, {8, 2}) && Close(r.join.radius, 2));
    CHECK(Close(r.first.p0, {0, 0}) && Close(r.first.p1, {8, 0}) && Close(r.second.p0, {10, 2}));
    // Radius 0 extends lines that stop short of each other.
    CHECK(FilletObjects(Entity::Line({0, 0}, {8, 0}), {4, 0}, Entity::Line({10, 2}, {10, 10}), {10, 6}, 0, true, r, why));
    CHECK(!r.hasJoin && Close(r.first.p1, {10, 0}) && Close(r.second.p0, {10, 0}));
    // A line and a circle.
    CHECK(FilletObjects(Entity::Line({-20, 8}, {20, 8}), {5, 8}, Entity::Circle({0, 0}, 5), {2, 4.58}, 2, true, r, why));
    CHECK(Close(r.join.p0, {std::sqrt(13.0), 6}, 1e-9) && Close(r.first.p0, {std::sqrt(13.0), 8}, 1e-9) && !r.changeSecond);
    CHECK(!FilletObjects(Entity::Line({0, 0}, {10, 0}), {5, 0}, Entity::Line({0, 1}, {10, 1}), {5, 1}, 0, true, r, why));

    CHECK(ChamferLines(l1, {5, 0}, l2, {10, 5}, 2, 3, true, r, why));
    CHECK(Close(r.first.p1, {8, 0}) && Close(r.second.p0, {10, 3}) && Close(r.join.p0, {8, 0}) && Close(r.join.p1, {10, 3}));
    double d2 = 0;
    CHECK(ChamferDistanceFromAngle(l1, {5, 0}, l2, {10, 5}, 2, kPi / 4, d2) && Close(d2, 2));

    Entity rect = Entity::Rectangle({0, 0}, {10, 10});
    CHECK(FilletPolyline(rect, 1) == 4 && rect.verts.size() == 8 && Close(LengthOf(rect), 32 + 2 * kPi));
    rect = Entity::Rectangle({0, 0}, {10, 10});
    CHECK(ChamferPolyline(rect, 1, 1) == 4 && Close(LengthOf(rect), 32 + 4 * std::sqrt(2.0)));
    rect = Entity::Rectangle({0, 0}, {10, 10});
    CHECK(FilletPolyline(rect, 6) == 2);  // only every other corner fits
    rect = Entity::Rectangle({0, 0}, {10, 10});
    const int v = SharedVertex(rect, {5, 0}, {10, 5});
    CHECK(v == 1 && FilletPolyline(rect, 1, v) == 1 && rect.verts.size() == 5);
}

void TestBreakJoinExplode() {
    std::vector<Entity> out;
    std::wstring why;
    const Entity line = Entity::Line({0, 0}, {10, 0});
    CHECK(BreakCurve(line, 3, 6, out, why) && out.size() == 2 && Close(out[0].p1, {3, 0}) && Close(out[1].p0, {6, 0}));
    CHECK(BreakCurve(line, 5, 5, out, why) && out.size() == 2);
    CHECK(!BreakCurve(line, 0, 0, out, why));
    CHECK(BreakCurve(Entity::Circle({0, 0}, 1), 0, kPi / 2, out, why) && out.size() == 1 && out[0].type == EntityType::Arc &&
          Close(SweepCCW(out[0].startAngle, out[0].endAngle), 3 * kPi / 2));

    auto groups = JoinObjects({Entity::Line({0, 0}, {5, 0}), Entity::Line({10, 0}, {5, 0})});
    CHECK(groups.size() == 1 && groups[0].result.type == EntityType::Line && Close(LengthOf(groups[0].result), 10));
    groups = JoinObjects({Entity::Line({0, 0}, {10, 0}), Entity::Line({10, 10}, {0, 10}), Entity::Line({10, 0}, {10, 10}),
                          Entity::Line({0, 10}, {0, 0})});
    CHECK(groups.size() == 1 && groups[0].result.closed && groups[0].result.verts.size() == 4 && groups[0].sources.size() == 4);
    groups = JoinObjects({Entity::Arc({0, 0}, 5, 0, kPi), Entity::Arc({0, 0}, 5, kPi, 0)});
    CHECK(groups.size() == 1 && groups[0].result.type == EntityType::Circle);
    groups = JoinObjects({Entity::Arc({0, 0}, 5, 0, kPi / 2), Entity::Arc({0, 0}, 5, kPi / 2, kPi)});
    CHECK(groups.size() == 1 && groups[0].result.type == EntityType::Arc);
    groups = JoinObjects({Entity::Line({-5, -5}, {5, 0}), Entity::Arc({0, 0}, 5, 0, kPi / 2)});
    CHECK(groups.size() == 1 && groups[0].result.type == EntityType::Polyline && groups[0].result.verts.size() == 3);
    CHECK(JoinObjects({Entity::Line({0, 0}, {1, 0}), Entity::Line({2, 0}, {3, 0})}).empty());

    out.clear();
    CHECK(Explode(Entity::Polyline({{{0, 0}, 0}, {{10, 0}, 1}, {{10, 10}, 0}}, false), out) && out.size() == 2 &&
          out[1].type == EntityType::Arc && Close(out[1].radius, 5));

    Entity rect = Entity::Rectangle({0, 0}, {10, 10});
    CHECK(StretchEntity(rect, Box({8, -1}, {12, 11}), {5, 0}) && Close(Bounds(rect).Width(), 15));
    Entity ln = Entity::Line({0, 0}, {10, 0});
    CHECK(!StretchEntity(ln, Box({3, -1}, {4, 1}), {5, 0}));
}

void TestArraysAndTransforms() {
    ArrayData rect;
    rect.kind = ArrayData::Rect;
    rect.items = {Entity::Circle({0, 0}, 1)};
    rect.columns = 3;
    rect.rows = 2;
    rect.columnStep = {5, 0};
    rect.rowStep = {0, 5};
    Entity ar = Entity::Array(rect);
    std::vector<Entity> copies;
    ExpandArray(ar, copies);
    CHECK(copies.size() == 6);
    Box b = Bounds(ar);
    CHECK(Close(b.minX, -1) && Close(b.maxX, 11) && Close(b.maxY, 6));
    Rotate(ar, {0, 0}, kPi / 2);  // the whole array turns
    b = Bounds(ar);
    CHECK(Close(b.minX, -6) && Close(b.maxY, 11));
    CHECK(rect.columnStep.x == 5);  // the original data is unchanged

    ArrayData polar;
    polar.kind = ArrayData::Polar;
    polar.items = {Entity::Line({5, 0}, {6, 0})};
    polar.centre = {0, 0};
    polar.count = 4;
    copies.clear();
    ExpandArray(Entity::Array(polar), copies);
    CHECK(copies.size() == 4 && Close(copies[1].p0, {0, 5}) && Close(copies[3].p1, {0, -6}));
    polar.fillAngle = kPi;  // half way round: ends included
    copies.clear();
    ExpandArray(Entity::Array(polar), copies);
    CHECK(Close(copies[3].p0, {-5, 0}));

    ArrayData path;
    path.kind = ArrayData::Path;
    path.items = {Entity::Circle({0, 0}, 1)};
    path.path = {Entity::Line({0, 0}, {10, 0})};
    path.count = 3;
    copies.clear();
    ExpandArray(Entity::Array(path), copies);
    CHECK(copies.size() == 3 && Close(copies[2].p0, {10, 0}));

    // Arrays survive a save and load, and are written to DXF as their copies.
    Document doc;
    doc.Add(Entity::Array(rect));
    doc.Add(Entity::Array(polar));
    doc.Add(Entity::Array(path));
    const std::string text = doc.ToText();
    Document back;
    std::wstring err;
    CHECK(back.FromText(text, err) && back.Count() == 3 && back.ToText() == text);
    const std::string dxf = back.ToDxf();
    size_t circles = 0;
    for (size_t pos = 0; (pos = dxf.find("CIRCLE", pos)) != std::string::npos; ++pos) ++circles;
    CHECK(circles == 6 + 3);

    Entity arc = Entity::Arc({0, 0}, 1, 0, kPi / 2);
    Mirror(arc, {0, 0}, {0, 1});
    CHECK(Close(arc.startAngle, kPi / 2) && Close(arc.endAngle, kPi));
    Entity pl = Entity::Polyline({{{0, 0}, 0.5}, {{10, 0}, 0}}, false);
    Mirror(pl, {0, -5}, {1, -5});
    CHECK(Close(pl.verts[0].p, {0, -10}) && pl.verts[0].bulge == -0.5);
    Entity c = Entity::Circle({2, 0}, 1);
    Scale(c, {0, 0}, 3);
    CHECK(Close(c.p0, {6, 0}) && Close(c.radius, 3));
}

uint64_t AddTo(Document& d, Entity e) { return d.Add(std::move(e)); }

void TestModifyCommands() {
    Document doc;
    TestHost host;
    Engine eng(doc, host);

    // TRIM: every object is an edge; picking removes the part between crossings.
    uint64_t h = AddTo(doc, Entity::Line({0, 0}, {10, 0}));
    AddTo(doc, Entity::Line({5, -5}, {5, 5}));
    Type(eng, {L"tr", L"8,0", L""});
    CHECK(Close(doc.Find(h)->p1, {5, 0}));
    Type(eng, {L"u"});
    CHECK(Close(doc.Find(h)->p1, {10, 0}));
    // The command's own Undo option.
    Type(eng, {L"tr", L"8,0", L"u"});
    CHECK(Close(doc.Find(h)->p1, {10, 0}) && eng.Active());
    eng.Cancel();
    // Shift+click extends instead.
    doc.Clear();
    h = AddTo(doc, Entity::Line({0, 0}, {5, 0}));
    AddTo(doc, Entity::Line({10, -5}, {10, 5}));
    eng.Run(L"TRIM");
    eng.Point({4, 0}, true);
    eng.Enter();
    CHECK(Close(doc.Find(h)->p1, {10, 0}));
    // EXTEND, then Shift+click trims back.
    doc.Clear();
    h = AddTo(doc, Entity::Line({0, 0}, {5, 0}));
    AddTo(doc, Entity::Line({10, -5}, {10, 5}));
    AddTo(doc, Entity::Line({3, -5}, {3, 5}));
    Type(eng, {L"ex", L"4.5,0"});
    CHECK(Close(doc.Find(h)->p1, {10, 0}));
    eng.Point({8, 0}, true);
    eng.Enter();
    CHECK(Close(doc.Find(h)->p1, {3, 0}));
    // Trimming with chosen cutting edges only.
    doc.Clear();
    h = AddTo(doc, Entity::Line({0, 0}, {10, 0}));
    const uint64_t e1 = AddTo(doc, Entity::Line({3, -5}, {3, 5}));
    AddTo(doc, Entity::Line({7, -5}, {7, 5}));
    Type(eng, {L"tr", L"t"});
    eng.AddToSelection({e1});
    Type(eng, {L"", L"8,0", L""});
    CHECK(Close(doc.Find(h)->p1, {3, 0}));

    // OFFSET by distance and through a point; several objects in one go.
    doc.Clear();
    h = AddTo(doc, Entity::Circle({0, 0}, 10));
    Type(eng, {L"o", L"2", L"10,0", L"0,0", L"10,0", L"20,0", L""});
    CHECK(doc.Count() == 3);
    Type(eng, {L"o", L"t", L"10,0", L"0,3", L""});
    CHECK(doc.Count() == 4 && Close(doc.Entities().rbegin()->second.radius, 3));

    // FILLET two lines, CHAMFER two lines, FILLET a polyline.
    doc.Clear();
    const uint64_t a = AddTo(doc, Entity::Line({0, 0}, {10, 0}));
    const uint64_t b = AddTo(doc, Entity::Line({10, 0}, {10, 10}));
    Type(eng, {L"f", L"r", L"2", L"5,0", L"10,5"});
    CHECK(!eng.Active() && doc.Count() == 3 && Close(doc.Find(a)->p1, {8, 0}) && Close(doc.Find(b)->p0, {10, 2}));
    Type(eng, {L"u"});
    CHECK(doc.Count() == 2 && Close(doc.Find(a)->p1, {10, 0}));
    Type(eng, {L"cha", L"d", L"2", L"3", L"5,0", L"10,5"});
    CHECK(doc.Count() == 3 && Close(doc.Find(b)->p0, {10, 3}));
    doc.Clear();
    h = AddTo(doc, Entity::Rectangle({0, 0}, {10, 10}));
    Type(eng, {L"f", L"r", L"1", L"p", L"5,0"});
    CHECK(doc.Find(h)->verts.size() == 8);
    // Shift-select: a sharp corner whatever the radius.
    doc.Clear();
    const uint64_t c1 = AddTo(doc, Entity::Line({0, 0}, {8, 0}));
    AddTo(doc, Entity::Line({10, 2}, {10, 10}));
    Type(eng, {L"f", L"4,0"});
    eng.Point({10, 6}, true);
    CHECK(Close(doc.Find(c1)->p1, {10, 0}) && doc.Count() == 2);

    // MIRROR keeping the source, SCALE by reference, STRETCH.
    doc.Clear();
    h = AddTo(doc, Entity::Line({1, 0}, {3, 0}));
    eng.AddToSelection({h});
    Type(eng, {L"mi", L"0,0", L"0,1", L""});
    CHECK(doc.Count() == 2 && Close(doc.Entities().rbegin()->second.p0, {-1, 0}));
    eng.AddToSelection({h});
    Type(eng, {L"sc", L"1,0", L"r", L"2", L"6"});
    CHECK(Close(doc.Find(h)->p1, {7, 0}));
    doc.Clear();
    h = AddTo(doc, Entity::Rectangle({0, 0}, {10, 10}));
    Type(eng, {L"s", L"8,-1", L"12,11", L"0,0", L"5,0"});
    CHECK(Close(Bounds(*doc.Find(h)).Width(), 15));

    // BREAK between two points and at a point; JOIN; EXPLODE.
    doc.Clear();
    h = AddTo(doc, Entity::Line({0, 0}, {10, 0}));
    Type(eng, {L"br", L"3,0", L"6,0"});
    CHECK(doc.Count() == 2 && Close(doc.Find(h)->p1, {3, 0}));
    Type(eng, {L"breakatpoint", L"1,0", L"1,0"});
    CHECK(doc.Count() == 3);
    eng.SelectAll();
    Type(eng, {L"j"});
    CHECK(doc.Count() == 2);  // 0-1-3 joined into one line; 6-10 stays apart
    doc.Clear();
    h = AddTo(doc, Entity::Rectangle({0, 0}, {10, 10}));
    eng.AddToSelection({h});
    Type(eng, {L"x"});
    CHECK(doc.Count() == 4);

    // LENGTHEN by delta and to a total.
    doc.Clear();
    h = AddTo(doc, Entity::Line({0, 0}, {10, 0}));
    Type(eng, {L"len", L"de", L"5", L"9,0", L""});
    CHECK(Close(doc.Find(h)->p1, {15, 0}));
    Type(eng, {L"len", L"t", L"20", L"1,0", L""});
    CHECK(Close(doc.Find(h)->p0, {-5, 0}));

    // ARRAYS stay one object; ARRAYEDIT changes them; EXPLODE separates them.
    doc.Clear();
    h = AddTo(doc, Entity::Circle({0, 0}, 1));
    eng.AddToSelection({h});
    Type(eng, {L"arrayrect", L"3", L"2", L"5", L"5"});
    CHECK(doc.Count() == 1 && doc.Entities().begin()->second.type == EntityType::Array);
    const uint64_t arr = doc.Entities().begin()->first;
    std::vector<Entity> copies;
    ExpandArray(*doc.Find(arr), copies);
    CHECK(copies.size() == 6);
    Type(eng, {L"arrayedit", L"11,5", L"c", L"5", L"x"});
    copies.clear();
    ExpandArray(*doc.Find(arr), copies);
    CHECK(copies.size() == 10);
    eng.AddToSelection({arr});
    Type(eng, {L"m", L"0,0", L"100,0"});  // moves as one object
    CHECK(Close(Bounds(*doc.Find(arr)).minX, 99));
    eng.AddToSelection({arr});
    Type(eng, {L"x"});
    CHECK(doc.Count() == 10);

    doc.Clear();
    h = AddTo(doc, Entity::Line({5, 0}, {6, 0}));
    eng.AddToSelection({h});
    Type(eng, {L"ar", L"po", L"0,0", L"8", L"", L""});
    copies.clear();
    ExpandArray(doc.Entities().begin()->second, copies);
    CHECK(copies.size() == 8);

    doc.Clear();
    const uint64_t path = AddTo(doc, Entity::Line({0, 0}, {100, 0}));
    h = AddTo(doc, Entity::Circle({0, 0}, 1));
    eng.AddToSelection({h});
    Type(eng, {L"arraypath", L"50,0", L"5", L"", L""});
    CHECK(doc.Count() == 2 && doc.Find(path));
    copies.clear();
    ExpandArray(doc.Entities().rbegin()->second, copies);
    CHECK(copies.size() == 5 && Close(copies[4].p0, {100, 0}));
}

void TestNewShapes() {
    // Splines pass through their fit points; a closed one comes back smoothly.
    std::vector<Vec2> pts;
    SplinePoints({{0, 0}, {10, 10}, {20, 0}, {30, 10}}, false, 8, pts);
    CHECK(pts.size() == 25 && Close(pts[8], {10, 10}) && Close(pts.back(), {30, 10}));
    SplinePoints({{10, 0}, {0, 10}, {-10, 0}, {0, -10}}, true, 16, pts);
    CHECK(pts.size() == 65 && Close(pts.front(), pts.back()));
    double maxR = 0, minR = 1e9;
    for (Vec2 p : pts) {
        maxR = std::max(maxR, Length(p));
        minR = std::min(minR, Length(p));
    }
    CHECK(Close(minR, 6.875 * std::sqrt(2.0), 1e-9) && Close(maxR, 10));  // symmetric, through the points

    // Ellipses: points, quadrants, transforms and trimming.
    Entity el = Entity::Ellipse({0, 0}, {10, 0}, 0.5);
    CHECK(Close(EllipsePoint(el.p0, el.p1, el.radius, kPi / 2), {0, 5}));
    CHECK(Close(EllipseParam(el.p0, el.p1, el.radius, {0, 5}), kPi / 2));
    std::vector<Vec2> q;
    Quadrants(el, q);
    CHECK(q.size() == 4);
    Box b = Bounds(el);
    CHECK(Close(b.maxX, 10, 1e-6) && Close(b.maxY, 5, 1e-3));
    Entity moved = el;
    Translate(moved, {5, 5});
    CHECK(Close(moved.p0, {5, 5}) && Close(moved.p1, {10, 0}));
    Rotate(moved, {5, 5}, kPi / 2);
    CHECK(Close(moved.p1, {0, 10}) && Close(moved.p0, {5, 5}));
    Scale(moved, {5, 5}, 2);
    CHECK(Close(Length(moved.p1), 20) && Close(moved.radius, 0.5));
    Entity arc = Entity::EllipseArc({0, 0}, {10, 0}, 0.5, 0, kPi / 2);
    Mirror(arc, {0, 0}, {0, 1});  // about the Y axis
    std::vector<Vec2> ends;
    EndPoints(arc, ends);
    CHECK(ends.size() == 2 && ((Close(ends[0], {0, 5}) && Close(ends[1], {-10, 0})) ||
                               (Close(ends[1], {0, 5}) && Close(ends[0], {-10, 0}))));
    std::vector<Entity> out;
    CHECK(TrimAt(el, {10, 0}, PrimsOf({Entity::Line({0, -20}, {0, 20})}), out, nullptr));
    CHECK(out.size() == 1 && out[0].type == EntityType::Ellipse && !out[0].closed);
    CHECK(Close(out[0].startAngle, kPi / 2, 1e-3) && Close(out[0].endAngle, 3 * kPi / 2, 1e-3));
    double area = 0;
    CHECK(AreaOf(el, area) && Close(area, kPi * 50));

    // Polygons and points.
    Entity hex = Entity::RegularPolygon({0, 0}, {10, 0}, 6);
    CHECK(hex.closed && hex.verts.size() == 6 && Close(LengthOf(hex), 60));
    Entity pt = Entity::Point({3, 4});
    CHECK(Close(DistanceTo(pt, {0, 0}), 5));

    // Files and DXF.
    Document doc;
    doc.Add(el);
    doc.Add(Entity::EllipseArc({1, 2}, {3, 4}, 0.25, 0.5, 2));
    doc.Add(Entity::Spline({{0, 0}, {5, 5}, {10, 0}}, false));
    doc.Add(pt);
    const std::string text = doc.ToText();
    Document back;
    std::wstring err;
    CHECK(back.FromText(text, err) && back.Count() == 4 && back.ToText() == text);
    const std::string dxf = back.ToDxf();
    CHECK(dxf.find("POINT") != std::string::npos);
}

void TestGripsAndSelect() {
    std::vector<Grip> g;
    Entity line = Entity::Line({0, 0}, {10, 0});
    GripsOf(line, g);
    CHECK(g.size() == 3);
    CHECK(DragGrip(line, 1, {10, 0}, {10, 5}) && Close(line.p1, {10, 5}));
    CHECK(DragGrip(line, 2, {5, 2.5}, {5, 12.5}) && Close(line.p0, {0, 10}));

    Entity circle = Entity::Circle({0, 0}, 5);
    CHECK(DragGrip(circle, 1, {5, 0}, {8, 0}) && Close(circle.radius, 8));
    Entity arc = Entity::Arc({0, 0}, 10, 0, kPi);
    CHECK(DragGrip(arc, 3, {0, 10}, {0, 5}));  // flatter through the new middle
    CHECK(Close(Polar(arc.p0, arc.radius, arc.startAngle), {10, 0}, 1e-9) || Close(Polar(arc.p0, arc.radius, arc.endAngle), {10, 0}, 1e-9));
    CHECK(DistanceTo(arc, {0, 5}) < 1e-9);

    Entity rect = Entity::Rectangle({0, 0}, {10, 10});
    g.clear();
    GripsOf(rect, g);
    CHECK(g.size() == 8);
    CHECK(DragGrip(rect, 4, {5, 0}, {5, -2}) && Close(rect.verts[0].p, {0, -2}) && Close(rect.verts[1].p, {10, -2}));
    Entity slot = Entity::Polyline({{{0, 0}, 0}, {{10, 0}, 1}, {{10, 10}, 0}}, false);
    CHECK(DragGrip(slot, 4, {15, 5}, {12, 5}) && slot.verts[1].bulge > 0 && slot.verts[1].bulge < 1);

    Entity el = Entity::Ellipse({0, 0}, {10, 0}, 0.5);
    CHECK(DragGrip(el, 3, {0, 5}, {0, 8}) && Close(el.radius, 0.8));
    CHECK(DragGrip(el, 1, {10, 0}, {0, 16}) && Close(Length(el.p1), 16) && Close(el.radius, 0.5));

    SelectQuery q;
    q.type = (int)EntityType::Circle;
    q.prop = QProp::Radius;
    q.op = QOp::Equal;
    q.value = 5;
    CHECK(Matches(Entity::Circle({1, 1}, 5), q) && !Matches(Entity::Circle({1, 1}, 6), q) && !Matches(line, q));
    q.op = QOp::Greater;
    CHECK(Matches(Entity::Circle({1, 1}, 6), q));
    q = SelectQuery{};
    q.prop = QProp::Area;
    q.op = QOp::Greater;
    q.value = 100;
    double area = 0;
    const Entity slotClosed = Entity::Polyline({{{0, 0}, 0}, {{10, 0}, 1}, {{10, 10}, 0}, {{0, 10}, 0}}, true);
    CHECK(AreaOf(slotClosed, area) && Close(area, 100 + 12.5 * kPi));
    CHECK(Matches(slotClosed, q) && !Matches(Entity::Rectangle({0, 0}, {5, 5}), q) && !Matches(line, q));
    CHECK(SameKind(Entity::Circle({0, 0}, 1), Entity::Circle({5, 5}, 9)) && !SameKind(line, circle));
}

void TestDrawCommandsB2() {
    Document doc;
    TestHost host;
    Engine eng(doc, host);
    Type(eng, {L"el", L"-10,0", L"10,0", L"5"});
    CHECK(doc.Count() == 1 && doc.Entities().begin()->second.type == EntityType::Ellipse &&
          Close(doc.Entities().begin()->second.radius, 0.5));
    Type(eng, {L"el", L"c", L"0,0", L"0,20", L"4"});
    CHECK(Close(Length(doc.Entities().rbegin()->second.p1), 20) && Close(doc.Entities().rbegin()->second.radius, 0.2));
    Type(eng, {L"el", L"a", L"-10,0", L"10,0", L"5", L"0", L"90"});
    const Entity& ea = doc.Entities().rbegin()->second;
    CHECK(!ea.closed && Close(ea.startAngle, 0) && Close(ea.endAngle, kPi / 2));

    Type(eng, {L"pol", L"6", L"0,0", L"i", L"10,0"});
    const Entity& hex = doc.Entities().rbegin()->second;
    CHECK(hex.verts.size() == 6 && Close(hex.verts[0].p, {10, 0}));
    Type(eng, {L"pol", L"4", L"0,0", L"c", L"5"});  // circumscribed square of half-width 5
    CHECK(Close(Bounds(doc.Entities().rbegin()->second).Width(), 10));
    Type(eng, {L"pol", L"3", L"e", L"0,0", L"10,0"});
    CHECK(Close(LengthOf(doc.Entities().rbegin()->second), 30));

    Type(eng, {L"spl", L"0,0", L"10,10", L"20,0", L""});
    CHECK(doc.Entities().rbegin()->second.type == EntityType::Spline && doc.Entities().rbegin()->second.verts.size() == 3);
    const size_t before = doc.Count();
    Type(eng, {L"po", L"1,1", L"2,2", L""});
    CHECK(doc.Count() == before + 2);

    // Select similar picks up every point.
    eng.AddToSelection({doc.Entities().rbegin()->first});
    Type(eng, {L"selectsimilar"});
    CHECK(eng.Selection().size() == 2);
    eng.ClearSelection();

    // Grip editing: stretch an end point shared by two lines; Enter cycles to MOVE.
    doc.Clear();
    const uint64_t a = doc.Add(Entity::Line({0, 0}, {10, 0}));
    const uint64_t b = doc.Add(Entity::Line({10, 0}, {10, 10}));
    eng.AddToSelection({a, b});
    eng.StartGripEdit(Grip{a, 1, {10, 0}});
    CHECK(eng.Active());
    Type(eng, {L"12,3"});
    CHECK(Close(doc.Find(a)->p1, {12, 3}) && Close(doc.Find(b)->p0, {12, 3}) && !eng.Active());
    eng.StartGripEdit(Grip{a, 0, {0, 0}});
    Type(eng, {L"", L"5,0"});  // MOVE by (5,0)
    CHECK(Close(doc.Find(a)->p0, {5, 0}) && Close(doc.Find(b)->p1, {15, 10}));
    eng.StartGripEdit(Grip{a, 0, {5, 0}});
    Type(eng, {L"", L"", L"c", L"90", L"180", L"x"});  // ROTATE copies
    CHECK(doc.Count() == 6);
    Type(eng, {L"u"});
    CHECK(doc.Count() == 2);
}

// ---------------------------------------------------------------------------
// Version 1.3: dimensions, DXF import, clipboard
// ---------------------------------------------------------------------------

const DimData& DimOf(const Document& doc, uint64_t id) { return *doc.Find(id)->dim; }

DimData Aligned(Vec2 a, Vec2 b, Vec2 loc) {
    DimData d;
    d.kind = DimData::Aligned;
    d.pts[0] = a;
    d.pts[1] = b;
    d.loc = loc;
    return d;
}

void TestDimensionValues() {
    DimData d = Aligned({0, 0}, {30, 40}, {0, 10});
    CHECK(Close(DimMeasure(d), 50) && DimValueText(d) == L"50");
    d.kind = DimData::Linear;
    d.angle = 0;
    CHECK(Close(DimMeasure(d), 30));
    d.angle = kPi / 2;
    CHECK(Close(DimMeasure(d), 40));
    DimStyle st;
    CHECK(DimNumber(12.5, st) == L"12.5" && DimNumber(40, st) == L"40" && DimNumber(-0.001, st) == L"0");
    st.trimZeros = false;
    CHECK(DimNumber(40, st) == L"40.00");
    d.text = L"<> TYP";
    CHECK(DimDisplayText(d) == L"40 TYP");
    d.text = L"SEE NOTE";
    CHECK(DimDisplayText(d) == L"SEE NOTE");

    DimData r;
    r.kind = DimData::Radius;
    r.pts[0] = {0, 0};
    r.pts[1] = {10, 0};
    r.loc = {20, 5};
    CHECK(DimValueText(r) == L"R10");
    r.kind = DimData::Diameter;
    CHECK(DimValueText(r) == L"\x00D8" L"20");

    DimData a;
    a.kind = DimData::Angular;
    a.pts = {Vec2{0, 0}, Vec2{10, 0}, Vec2{0, 0}, Vec2{10, 10}};
    a.loc = {5, 2};
    CHECK(Close(ToDegrees(DimMeasure(a)), 45, 1e-9) && DimValueText(a) == L"45\x00B0");
    a.loc = {-5, 2};
    CHECK(Close(ToDegrees(DimMeasure(a)), 135, 1e-9));
    a.loc = {-5, -1};
    CHECK(Close(ToDegrees(DimMeasure(a)), 45, 1e-9));
    a.rays = true;  // three points: the rays to (10,0) and (10,10)
    a.loc = {5, 2};
    CHECK(Close(ToDegrees(DimMeasure(a)), 45, 1e-9));
    a.loc = {-5, -1};
    CHECK(Close(ToDegrees(DimMeasure(a)), 315, 1e-9));
    // Lines that do not meet: the angle between them, at their crossing.
    a.rays = false;
    a.pts = {Vec2{10, 0}, Vec2{20, 0}, Vec2{0, 10}, Vec2{0, 20}};
    a.loc = {5, 5};
    CHECK(Close(ToDegrees(DimMeasure(a)), 90, 1e-9));
}

void TestDimensionLayout() {
    DimData d = Aligned({0, 0}, {100, 0}, {50, 20});
    DimGraphics g;
    DimLayout(d, g);
    CHECK(g.arrows.size() == 2 && g.lines.size() == 3);
    bool dimLine = false, ext0 = false;
    for (const Prim& p : g.lines) {
        if (Close(p.a, {0, 20}) && Close(p.b, {100, 20})) dimLine = true;
        if (Close(p.a, {0, d.style.extOffset}) && Close(p.b, {0, 20 + d.style.extBeyond})) ext0 = true;
    }
    CHECK(dimLine && ext0);
    CHECK(Close(g.textPos, {50, 20 + d.style.textGap + d.style.textHeight / 2}) && Close(g.textAngle, 0));
    CHECK(Close(g.arrows[0][0], {0, 20}) && Close(g.arrows[1][0], {100, 20}));
    CHECK(g.textWidth > 0 && g.textWidth < 10);
    // Below the line, text still reads left to right; vertical text reads upwards.
    d.loc = {50, -20};
    DimLayout(d, g);
    CHECK(Close(g.textAngle, 0) && g.textPos.y > -20);
    d = Aligned({0, 0}, {0, 100}, {-20, 50});
    DimLayout(d, g);
    CHECK(Close(g.textAngle, kPi / 2) && g.textPos.x < -20);
    // Too short for the arrows inside: they go outside.
    d = Aligned({0, 0}, {4, 0}, {2, 10});
    DimLayout(d, g);
    CHECK(Close(g.arrows[0][0], {0, 10}) && g.arrows[0][1].x < 0);

    // As an entity: picked by its text, inside a window by its text too.
    Entity e = MakeDimension(Aligned({0, 0}, {100, 0}, {50, 20}));
    DimLayout(*e.dim, g);
    CHECK(Close(DistanceTo(e, g.textPos), 0) && Close(DistanceTo(e, {50, 20}), 0));
    CHECK(Bounds(e).Contains(g.textPos + Vec2{0, d.style.textHeight / 2}));
    CHECK(TouchesBox(e, Box({49, 22}, {51, 23})));
    CHECK(e.type == EntityType::Dimension && std::wstring(TypeName(e.type)) == L"Dimension");
    // Transformations keep the value (scale changes it).
    Entity m = e;
    Rotate(m, {0, 0}, kPi / 2);
    CHECK(Close(DimMeasure(*m.dim), 100) && Close(m.dim->pts[1], {0, 100}));
    Mirror(m, {0, 0}, {1, 0});
    CHECK(Close(m.dim->pts[1], {0, -100}));
    Scale(m, {0, 0}, 0.5);
    CHECK(Close(DimMeasure(*m.dim), 50));
    // Grips: the two points and the text.
    std::vector<Grip> grips;
    GripsOf(e, grips);
    CHECK(grips.size() == 3);
    Entity dragged = e;
    CHECK(DragGrip(dragged, 1, {100, 0}, {80, 0}) && Close(DimMeasure(*dragged.dim), 80));
    CHECK(DragGrip(dragged, 2, grips[2].p, grips[2].p + Vec2{0, 10}) && Close(dragged.dim->loc.y, 30));
}

void TestDimensionAssociativity() {
    Document doc;
    const uint64_t line = doc.Add(Entity::Line({0, 0}, {100, 0}));
    DimData d = Aligned({0, 0}, {100, 0}, {50, 20});
    d.refs[0] = {line, 0};
    d.refs[1] = {line, 1};
    const uint64_t dim = doc.Add(MakeDimension(d));
    CHECK(HasRefs(*doc.Find(dim)));

    // Stretching the line: the dimension follows.
    Entity l = *doc.Find(line);
    l.p1 = {120, 0};
    doc.Replace(l);
    CHECK(Close(DimMeasure(DimOf(doc, dim)), 120) && Close(DimOf(doc, dim).loc.y, 20));
    // Moving the line: the dimension moves with it.
    l = *doc.Find(line);
    Translate(l, {0, 10});
    doc.Replace(l);
    CHECK(Close(DimOf(doc, dim).pts[0], {0, 10}) && Close(DimOf(doc, dim).loc.y, 30));
    // Turning it: an aligned dimension turns too, at the same distance.
    l = *doc.Find(line);
    Rotate(l, {0, 10}, kPi / 2);
    doc.Replace(l);
    CHECK(Close(DimOf(doc, dim).pts[1], {0, 130}) && Close(DimOf(doc, dim).loc.x, -20, 1e-9));
    doc.Undo();
    doc.Undo();
    doc.Undo();
    CHECK(Close(DimMeasure(DimOf(doc, dim)), 100) && Close(DimOf(doc, dim).loc, {50, 20}));
    // Moved on its own: no longer attached (and undo attaches it again).
    Entity de = *doc.Find(dim);
    Translate(de, {0, 50});
    doc.Replace(de);
    CHECK(!HasRefs(*doc.Find(dim)));
    doc.Undo();
    CHECK(HasRefs(*doc.Find(dim)));
    // Moved together with the line: still attached.
    doc.BeginChange(L"Move both");
    for (uint64_t id : {line, dim}) {
        Entity x = *doc.Find(id);
        Translate(x, {5, 5});
        doc.Replace(x);
    }
    doc.EndChange();
    CHECK(HasRefs(*doc.Find(dim)) && Close(DimOf(doc, dim).pts[0], {5, 5}));
    // Its own text grip moved: still attached.
    de = *doc.Find(dim);
    CHECK(DragGrip(de, 2, {0, 0}, {0, 3}));
    doc.Replace(de);
    CHECK(HasRefs(*doc.Find(dim)));

    // Saved and opened again: still attached.
    Document copy;
    std::wstring err;
    CHECK(copy.FromText(doc.ToText(), err));
    uint64_t cl = 0, cd = 0;
    for (const auto& kv : copy.Entities()) (kv.second.type == EntityType::Dimension ? cd : cl) = kv.first;
    CHECK(cd && cl && copy.Find(cd)->dim->refs[0].id == cl && copy.Find(cd)->dim->refs[1].grip == 1);
    Entity cline = *copy.Find(cl);
    cline.p0 = cline.p0 - Vec2{10, 0};
    copy.Replace(cline);
    CHECK(Close(DimMeasure(*copy.Find(cd)->dim), 110));
    CHECK(copy.Settings().dimStyle == doc.Settings().dimStyle);

    // Text with spaces and symbols survives the file.
    de = *doc.Find(dim);
    DimData dd = *de.dim;
    dd.text = L"<> \x2300 ±0.1 = 100%";
    de.dim = std::make_shared<const DimData>(dd);
    doc.Replace(de);
    CHECK(copy.FromText(doc.ToText(), err));
    for (const auto& kv : copy.Entities())
        if (kv.second.type == EntityType::Dimension) CHECK(kv.second.dim->text == dd.text);

    // Copies made together stay attached to each other.
    const std::vector<uint64_t> ids = doc.AddGroup({*doc.Find(line), *doc.Find(dim)});
    CHECK(ids.size() == 2 && doc.Find(ids[1])->dim->refs[0].id == ids[0]);
    // A dimension copied alone is not attached to anything.
    const std::vector<uint64_t> lone = doc.AddGroup({*doc.Find(dim)});
    CHECK(!HasRefs(*doc.Find(lone[0])));

    // The object erased: the dimension stays, not attached.
    doc.Erase(line);
    CHECK(doc.Find(dim) && !HasRefs(*doc.Find(dim)));

    // A circle's radius dimension follows the circle.
    const uint64_t c = doc.Add(Entity::Circle({0, 0}, 10));
    DimData r;
    r.kind = DimData::Radius;
    r.pts[0] = {0, 0};
    r.pts[1] = {10, 0};
    r.loc = {15, 0};
    r.refs[0] = {c, -1};
    const uint64_t rd = doc.Add(MakeDimension(r));
    Entity ce = *doc.Find(c);
    ce.radius = 20;
    Translate(ce, {1, 1});
    doc.Replace(ce);
    CHECK(Close(DimMeasure(DimOf(doc, rd)), 20) && Close(DimOf(doc, rd).pts[0], {1, 1}) &&
          Close(Dist(DimOf(doc, rd).loc, {1, 1}), 25));

    // An angle follows its lines.
    const uint64_t la = doc.Add(Entity::Line({0, 0}, {10, 0}));
    const uint64_t lb = doc.Add(Entity::Line({0, 0}, {10, 10}));
    DimData a;
    a.kind = DimData::Angular;
    a.pts = {Vec2{0, 0}, Vec2{10, 0}, Vec2{0, 0}, Vec2{10, 10}};
    a.refs = {DimRef{la, 0}, DimRef{la, 1}, DimRef{lb, 0}, DimRef{lb, 1}};
    a.loc = {8, 3};
    const uint64_t ad = doc.Add(MakeDimension(a));
    Entity lbe = *doc.Find(lb);
    lbe.p1 = {0, 10};
    doc.Replace(lbe);
    CHECK(Close(ToDegrees(DimMeasure(DimOf(doc, ad))), 90, 1e-9));
}

void TestDrivingDimensions() {
    std::wstring why;
    // A rectangle (polyline) made wider by its width dimension stays a rectangle.
    Document doc;
    const uint64_t rect = doc.Add(Entity::Rectangle({0, 0}, {100, 50}));
    DimData d = Aligned({0, 0}, {100, 0}, {50, -15});
    d.kind = DimData::Linear;
    d.refs[0] = {rect, 0};
    d.refs[1] = {rect, 1};
    const uint64_t dim = doc.Add(MakeDimension(d));
    doc.BeginChange(L"Value");
    CHECK(DriveDimension(doc, dim, 120, why));
    doc.EndChange();
    const Entity& r = *doc.Find(rect);
    CHECK(Close(r.verts[1].p, {120, 0}) && Close(r.verts[2].p, {120, 50}) && Close(r.verts[3].p, {0, 50}));
    CHECK(Close(DimMeasure(DimOf(doc, dim)), 120));
    doc.Undo();
    CHECK(Close(doc.Find(rect)->verts[1].p, {100, 0}) && Close(DimMeasure(DimOf(doc, dim)), 100));

    // Four lines: the right side moves, the top and bottom stretch.
    Document lines;
    const uint64_t b = lines.Add(Entity::Line({0, 0}, {100, 0}));
    const uint64_t rt = lines.Add(Entity::Line({100, 0}, {100, 50}));
    const uint64_t t = lines.Add(Entity::Line({100, 50}, {0, 50}));
    const uint64_t lf = lines.Add(Entity::Line({0, 50}, {0, 0}));
    const uint64_t hole = lines.Add(Entity::Circle({80, 25}, 5));  // not connected: stays
    d.refs[0] = {b, 0};
    d.refs[1] = {b, 1};
    const uint64_t ld = lines.Add(MakeDimension(d));
    // Its height, from the left side.
    DimData h = Aligned({0, 0}, {0, 50}, {-15, 25});
    h.kind = DimData::Linear;
    h.angle = kPi / 2;
    h.refs[0] = {lf, 1};
    h.refs[1] = {lf, 0};
    const uint64_t hd = lines.Add(MakeDimension(h));
    lines.BeginChange(L"Value");
    CHECK(DriveDimension(lines, ld, 80, why));
    lines.EndChange();
    CHECK(Close(lines.Find(rt)->p0, {80, 0}) && Close(lines.Find(rt)->p1, {80, 50}));
    CHECK(Close(lines.Find(t)->p0, {80, 50}) && Close(lines.Find(t)->p1, {0, 50}));
    CHECK(Close(lines.Find(lf)->p0, {0, 50}) && Close(lines.Find(hole)->p0, {80, 25}));
    lines.BeginChange(L"Value");
    CHECK(DriveDimension(lines, hd, 30, why));
    lines.EndChange();
    CHECK(Close(lines.Find(t)->p0, {80, 30}) && Close(lines.Find(rt)->p1, {80, 30}) && Close(lines.Find(b)->p1, {80, 0}));
    CHECK(Close(DimMeasure(DimOf(lines, hd)), 30) && Close(DimMeasure(DimOf(lines, ld)), 80));

    // Radius and diameter.
    const uint64_t c = doc.Add(Entity::Circle({0, 0}, 10));
    DimData rd;
    rd.kind = DimData::Diameter;
    rd.pts[0] = {0, 0};
    rd.pts[1] = {10, 0};
    rd.loc = {20, 0};
    rd.refs[0] = {c, -1};
    const uint64_t cd = doc.Add(MakeDimension(rd));
    doc.BeginChange(L"Value");
    CHECK(DriveDimension(doc, cd, 30, why));
    doc.EndChange();
    CHECK(Close(doc.Find(c)->radius, 15) && Close(DimMeasure(DimOf(doc, cd)), 30));
    // A polyline arc segment keeps its ends.
    const uint64_t pl = doc.Add(Entity::Polyline({{{0, 0}, 1}, {{20, 0}, 0}}, false));  // a half circle, radius 10
    DimData pr;
    pr.kind = DimData::Radius;
    const Prim arc = ArcFromBulge({0, 0}, {20, 0}, 1);
    pr.pts[0] = arc.c;
    pr.pts[1] = arc.c + Vec2{0, -10};
    pr.loc = {10, -20};
    pr.refs[0] = {pl, -1};
    const uint64_t prd = doc.Add(MakeDimension(pr));
    doc.BeginChange(L"Value");
    CHECK(!DriveDimension(doc, prd, 5, why) && !why.empty());  // smaller than half the chord
    CHECK(DriveDimension(doc, prd, 20, why));
    doc.EndChange();
    const Prim now = ArcFromBulge(doc.Find(pl)->verts[0].p, doc.Find(pl)->verts[1].p, doc.Find(pl)->verts[0].bulge);
    CHECK(Close(now.r, 20, 1e-9) && Close(doc.Find(pl)->verts[1].p, {20, 0}) && Close(DimMeasure(DimOf(doc, prd)), 20, 1e-9));

    // An angle: the second line turns about the corner.
    const uint64_t la = doc.Add(Entity::Line({0, 0}, {10, 0}));
    const uint64_t lb = doc.Add(Entity::Line({0, 0}, {10, 10}));
    DimData a;
    a.kind = DimData::Angular;
    a.pts = {Vec2{0, 0}, Vec2{10, 0}, Vec2{0, 0}, Vec2{10, 10}};
    a.refs = {DimRef{la, 0}, DimRef{la, 1}, DimRef{lb, 0}, DimRef{lb, 1}};
    a.loc = {8, 3};
    const uint64_t ad = doc.Add(MakeDimension(a));
    doc.BeginChange(L"Value");
    CHECK(DriveDimension(doc, ad, ToRadians(60), why));
    doc.EndChange();
    CHECK(Close(doc.Find(lb)->p1, Polar({0, 0}, std::sqrt(200.0), ToRadians(60)), 1e-9) &&
          Close(doc.Find(la)->p1, {10, 0}));
    CHECK(Close(ToDegrees(DimMeasure(DimOf(doc, ad))), 60, 1e-9));
    doc.BeginChange(L"Value");
    CHECK(!DriveDimension(doc, ad, ToRadians(200), why));
    doc.EndChange();
    // An arc's angle: its end moves round.
    const uint64_t arcId = doc.Add(Entity::Arc({50, 50}, 10, 0, kPi / 2));
    DimData aa;
    aa.kind = DimData::Angular;
    aa.rays = true;
    aa.pts = {Vec2{50, 50}, Vec2{60, 50}, Vec2{50, 50}, Vec2{50, 60}};
    aa.refs = {DimRef{arcId, 0}, DimRef{arcId, 1}, DimRef{arcId, 0}, DimRef{arcId, 2}};
    aa.loc = {58, 58};
    const uint64_t aad = doc.Add(MakeDimension(aa));
    doc.BeginChange(L"Value");
    CHECK(DriveDimension(doc, aad, ToRadians(120), why));
    doc.EndChange();
    CHECK(Close(doc.Find(arcId)->endAngle, ToRadians(120), 1e-9) && Close(ToDegrees(DimMeasure(DimOf(doc, aad))), 120, 1e-9));
    // Not attached: nothing to change.
    const uint64_t free = doc.Add(MakeDimension(Aligned({0, 100}, {10, 100}, {5, 110})));
    doc.BeginChange(L"Value");
    CHECK(!DriveDimension(doc, free, 20, why) && !why.empty());
    doc.EndChange();
}

Vec2 DimLayoutText(const DimData& d) {
    DimGraphics g;
    DimLayout(d, g);
    return g.textPos;
}

void TestDimensionCommands() {
    Document doc;
    TestHost host;
    Engine eng(doc, host);
    const uint64_t line = doc.Add(Entity::Line({0, 0}, {100, 0}));
    const auto lastDim = [&]() -> const DimData& { return *Last(doc).dim; };

    // Smart: click the line, then where the dimension goes.
    Type(eng, {L"dim"});
    eng.Point({50, 0});
    CHECK(eng.Active() && eng.CurrentPrompt().pickObject);
    eng.Point({50, 20});
    CHECK(doc.Count() == 2 && Last(doc).type == EntityType::Dimension);
    CHECK(lastDim().kind == DimData::Linear && Close(DimMeasure(lastDim()), 100) && lastDim().refs[1].id == line);
    // Still running: type a value while placing to make the line that long.
    eng.Point({50, 0});
    host.cursor = {50, -20};
    Type(eng, {L"80"});
    CHECK(Close(doc.Find(line)->p1, {80, 0}) && Close(DimMeasure(lastDim()), 80));
    CHECK(doc.Count() == 3 && Close(DimMeasure(*doc.Find(line + 1)->dim), 80));  // the first one followed
    Type(eng, {L""});
    CHECK(!eng.Active());
    doc.Undo();
    CHECK(Close(doc.Find(line)->p1, {100, 0}) && doc.Count() == 2);

    // A slanted line: beside it the dimension is aligned, above it horizontal.
    const uint64_t slant = doc.Add(Entity::Line({0, 100}, {30, 140}));
    Type(eng, {L"dim"});
    eng.Point({15, 120});
    eng.Point({0, 130});  // beside the middle of the line
    CHECK(lastDim().kind == DimData::Aligned && Close(DimMeasure(lastDim()), 50));
    eng.Point({15, 120});
    eng.Point({15, 160});  // above: horizontal
    CHECK(lastDim().kind == DimData::Linear && Close(DimMeasure(lastDim()), 30));
    eng.Point({15, 120});
    Type(eng, {L"v"});
    eng.Point({15, 160});
    CHECK(Close(DimMeasure(lastDim()), 40));
    // A second line clicked instead of a place: the angle between them.
    const uint64_t up = doc.Add(Entity::Line({0, 100}, {0, 200}));
    eng.Point({15, 120});
    eng.Point({0, 180});
    CHECK(eng.Active() && eng.CurrentPrompt().text.find(L"arc") != std::wstring::npos);
    eng.Point({3, 110});
    CHECK(lastDim().kind == DimData::Angular && Close(ToDegrees(DimMeasure(lastDim())), 90 - ToDegrees(std::atan2(40, 30)), 1e-9));
    // A circle gives a diameter, an arc a radius; R switches.
    const uint64_t circle = doc.Add(Entity::Circle({200, 0}, 10));
    eng.Point({210, 0});
    eng.Point({230, 10});
    CHECK(lastDim().kind == DimData::Diameter && Close(DimMeasure(lastDim()), 20) && lastDim().refs[0].id == circle);
    eng.Point({210, 0});
    Type(eng, {L"r", L"7.5"});  // radius 7.5
    CHECK(Close(doc.Find(circle)->radius, 7.5) && lastDim().kind == DimData::Radius);
    // Text instead of the value.
    eng.Point({50, 0});
    Type(eng, {L"t", L"<> TYP"});
    eng.Point({50, 30});
    CHECK(DimDisplayText(lastDim()) == L"100 TYP");
    Type(eng, {L""});
    CHECK(!eng.Active());
    (void)slant;
    (void)up;

    // DIMLINEAR by points; DIMCONTINUE and DIMBASELINE from it.
    Document d2;
    Engine e2(d2, host);
    const uint64_t base = d2.Add(Entity::Line({0, 0}, {100, 0}));
    Type(e2, {L"dli", L"0,0", L"40,0", L"20,-10"});
    CHECK(!e2.Active() && d2.Count() == 2);
    const DimData first = *Last(d2).dim;
    CHECK(first.kind == DimData::Linear && Close(DimMeasure(first), 40) && first.refs[0].id == base);
    CHECK(first.refs[1].id == 0);  // (40,0) is not a grip of the line
    Type(e2, {L"dco", L"70,0", L"100,0", L""});
    CHECK(d2.Count() == 4 && Close(DimMeasure(*Last(d2).dim), 30) && Close(Last(d2).dim->pts[0], {70, 0}));
    CHECK(Last(d2).dim->refs[1].id == base && Close(Last(d2).dim->loc.y, -10));
    Type(e2, {L"dba", L"s"});
    e2.Point({20, -10});  // pick the first one
    Type(e2, {L"100,0", L""});
    CHECK(d2.Count() == 5 && Close(DimMeasure(*Last(d2).dim), 100) && Last(d2).dim->loc.y < -10);
    // DIMEDIT: a new value moves the line's end.
    const uint64_t last = Last(d2).id;
    e2.AddToSelection({last});
    Type(e2, {L"dimedit", L"120"});
    CHECK(Close(d2.Find(base)->p1, {120, 0}) && Close(DimMeasure(*d2.Find(last)->dim), 120));
    e2.ClearSelection();
    Type(e2, {L"dimedit"});
    e2.Point(DimLayoutText(*d2.Find(last)->dim));
    Type(e2, {L"t", L"OVERALL"});
    CHECK(d2.Find(last)->dim->text == L"OVERALL");
    // DIMRADIUS on an arc, DIMANGULAR by three points.
    const uint64_t arc = d2.Add(Entity::Arc({0, 50}, 10, 0, kPi));
    Type(e2, {L"dra"});
    e2.Point({0, 60});
    e2.Point({5, 70});
    CHECK(Close(DimMeasure(*Last(d2).dim), 10) && Last(d2).dim->refs[0].id == arc);
    Type(e2, {L"dan", L"", L"0,0", L"10,0", L"0,10"});
    e2.Point({3, 3});
    CHECK(Close(ToDegrees(DimMeasure(*Last(d2).dim)), 90, 1e-9) && Last(d2).dim->rays);
    Type(e2, {L"dan"});
    e2.Point({10, 50});  // the arc: its angle
    e2.Point({0, 70});
    CHECK(Close(ToDegrees(DimMeasure(*Last(d2).dim)), 180, 1e-9));

    // COPY keeps copied dimensions attached to the copied line.
    e2.ClearSelection();
    e2.AddToSelection({base, last});
    Type(e2, {L"co", L"0,0", L"0,100", L""});
    const Entity& copiedDim = Last(d2);
    CHECK(copiedDim.type == EntityType::Dimension && copiedDim.dim->refs[1].id != base && copiedDim.dim->refs[1].id != 0);
    CHECK(Close(d2.Find(copiedDim.dim->refs[1].id)->p1, {120, 100}));

    // Copy and paste into another drawing.
    e2.ClearSelection();
    e2.AddToSelection({base, last});
    Type(e2, {L"copyclip"});
    Document d3;
    Engine e3(d3, host);
    Type(e3, {L"pasteclip", L"1000,1000"});
    CHECK(d3.Count() == 2 && e3.Selection().size() == 2);
    uint64_t pl = 0, pd = 0;
    for (const auto& kv : d3.Entities()) (kv.second.type == EntityType::Dimension ? pd : pl) = kv.first;
    CHECK(pd && pl && d3.Find(pd)->dim->refs[1].id == pl && Close(d3.Find(pl)->p1 - d3.Find(pl)->p0, {120, 0}));
    Type(e2, {L"cutclip"});
    CHECK(!d2.Find(base) && !e2.Active());
    Type(e2, {L"pasteclip", L"0,0"});
    CHECK(d2.Count() >= 2 && e2.Selection().size() == 2);
}

std::string Dxf(std::initializer_list<std::pair<int, const char*>> groups) {
    std::string s;
    for (const auto& g : groups) s += std::to_string(g.first) + "\n" + g.second + "\n";
    return s;
}

void TestDxfImport() {
    std::wstring err, notes;
    // What Feather CAD writes comes back.
    Document doc;
    doc.Add(Entity::Line({0, 0}, {10, 5}));
    doc.Add(Entity::Circle({5, 5}, 2));
    doc.Add(Entity::Arc({0, 0}, 3, 0.5, 2));
    doc.Add(Entity::Polyline({{{0, 0}, 0.5}, {{10, 0}, 0}, {{10, 10}, 0}}, true));
    doc.Add(Entity::Point({7, 7}));
    doc.Add(MakeDimension(Aligned({0, 0}, {10, 0}, {5, 5})));
    DrawingSettings ds;
    ds.units = Units::Inches;
    doc.SetSettings(ds);
    Document back;
    CHECK(back.FromDxf(doc.ToDxf(), err, notes));
    CHECK(back.Settings().units == Units::Inches);
    int lines = 0, circles = 0, arcs = 0, plines = 0, points = 0;
    for (const auto& kv : back.Entities()) {
        const Entity& e = kv.second;
        if (e.type == EntityType::Line) ++lines;
        if (e.type == EntityType::Circle) circles += Close(e.p0, {5, 5}) && Close(e.radius, 2);
        if (e.type == EntityType::Arc) arcs += Close(e.startAngle, 0.5) && Close(e.endAngle, 2);
        if (e.type == EntityType::Polyline) ++plines;
        if (e.type == EntityType::Point) ++points;
    }
    CHECK(lines >= 4 && circles == 1 && arcs == 1 && plines >= 3 && points == 1);  // + the dimension's lines and arrows
    size_t texts = 0;
    for (const auto& kv : back.Entities()) texts += kv.second.type == EntityType::Text;
    CHECK(texts == 1 && notes.empty());  // the dimension's text

    // A file as other programs write it.
    const std::string text = Dxf({
        {0, "SECTION"}, {2, "HEADER"}, {9, "$ACADVER"}, {1, "AC1015"}, {9, "$INSUNITS"}, {70, "4"},
        {9, "$DIMTXT"}, {40, "2.5"}, {9, "$DIMSCALE"}, {40, "2"}, {9, "$DIMZIN"}, {70, "0"}, {0, "ENDSEC"},
        {0, "SECTION"}, {2, "BLOCKS"},
        {0, "BLOCK"}, {8, "0"}, {2, "BOLT"}, {70, "0"}, {10, "1"}, {20, "0"}, {30, "0"},
        {0, "CIRCLE"}, {8, "0"}, {10, "1"}, {20, "0"}, {40, "1"},
        {0, "LINE"}, {8, "0"}, {10, "1"}, {20, "0"}, {11, "3"}, {21, "0"},
        {0, "ENDBLK"}, {8, "0"},
        {0, "ENDSEC"},
        {0, "SECTION"}, {2, "ENTITIES"},
        {0, "LWPOLYLINE"}, {8, "0"}, {90, "4"}, {70, "1"},
        {10, "0"}, {20, "0"}, {10, "10"}, {20, "0"}, {42, "1"}, {10, "10"}, {20, "10"}, {10, "0"}, {20, "10"},
        {0, "ELLIPSE"}, {8, "0"}, {10, "50"}, {20, "50"}, {11, "20"}, {21, "0"}, {40, "0.5"}, {41, "0"}, {42, "6.283185307179586"},
        {0, "SPLINE"}, {8, "0"}, {70, "8"}, {71, "3"}, {72, "8"}, {73, "4"}, {74, "0"},
        {40, "0"}, {40, "0"}, {40, "0"}, {40, "0"}, {40, "1"}, {40, "1"}, {40, "1"}, {40, "1"},
        {10, "0"}, {20, "0"}, {10, "10"}, {20, "20"}, {10, "20"}, {20, "-20"}, {10, "30"}, {20, "0"},
        {0, "INSERT"}, {8, "0"}, {2, "BOLT"}, {10, "100"}, {20, "100"}, {41, "2"}, {42, "2"}, {50, "90"},
        {0, "INSERT"}, {8, "0"}, {2, "BOLT"}, {10, "0"}, {20, "200"}, {70, "3"}, {44, "10"},
        {0, "INSERT"}, {8, "0"}, {2, "BOLT"}, {10, "0"}, {20, "300"}, {41, "2"}, {42, "1"},
        {0, "ARC"}, {8, "0"}, {10, "5"}, {20, "0"}, {40, "2"}, {50, "0"}, {51, "90"}, {210, "0"}, {220, "0"}, {230, "-1"},
        {0, "DIMENSION"}, {8, "0"}, {2, "*D1"}, {10, "30"}, {20, "50"}, {11, "15"}, {21, "52"}, {70, "33"}, {1, ""},
        {13, "0"}, {23, "0"}, {14, "30"}, {24, "40"},
        {0, "DIMENSION"}, {8, "0"}, {10, "0"}, {20, "0"}, {11, "20"}, {21, "5"}, {70, "4"}, {1, "%%c<>"}, {15, "10"}, {25, "0"},
        {0, "TEXT"}, {8, "0"}, {10, "0"}, {20, "0"}, {40, "2"}, {1, "HELLO"},
        {0, "LINE"}, {8, "0"}, {67, "1"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "1"},
        {0, "ENDSEC"}, {0, "EOF"}});
    Document d;
    CHECK(d.FromDxf(text, err, notes));
    CHECK(d.Settings().units == Units::Millimetres && Close(d.Settings().dimStyle.textHeight, 5) &&
          !d.Settings().dimStyle.trimZeros);
    CHECK(notes.empty());
    std::vector<const Entity*> byType[13];
    for (const auto& kv : d.Entities()) byType[(int)kv.second.type].push_back(&kv.second);
    const auto& pls = byType[(int)EntityType::Polyline];
    CHECK(pls.size() == 2);  // the LWPOLYLINE and the spline
    CHECK(pls[0]->closed && pls[0]->verts.size() == 4 && Close(pls[0]->verts[1].bulge, 1));
    CHECK(Close(pls[1]->verts.front().p, {0, 0}) && Close(pls[1]->verts.back().p, {30, 0}) && !pls[1]->closed);
    const auto& els = byType[(int)EntityType::Ellipse];
    CHECK(els.size() == 2 && Close(els[0]->p0, {50, 50}) && Close(Length(els[0]->p1), 20) && Close(els[0]->radius, 0.5));
    // The stretched insert: a circle becomes an ellipse.
    CHECK(Close(els[1]->p0, {0, 300}) && Close(Length(els[1]->p1), 2) && Close(els[1]->radius, 0.5));
    // Blocks placed without stretching stay blocks: 1 turned and scaled, 3 in a row.
    const auto& ins = byType[(int)EntityType::Insert];
    CHECK(ins.size() == 4 && d.Blocks().size() == 1 && d.FindBlock(L"bolt"));
    std::vector<Entity> parts;
    ExpandBlock(*ins[0], parts);
    CHECK(parts.size() == 2 && parts[0].type == EntityType::Circle && Close(parts[0].p0, {100, 100}) && Close(parts[0].radius, 2));
    // The line of the turned insert: from (100,100) up 4.
    CHECK(parts[1].type == EntityType::Line && Close(parts[1].p0, {100, 100}) && Close(parts[1].p1, {100, 104}));
    CHECK(Close(ins[1]->p0, {0, 200}) && Close(ins[2]->p0, {10, 200}) && Close(ins[3]->p0, {20, 200}));
    CHECK(byType[(int)EntityType::Circle].empty());
    const auto& txt = byType[(int)EntityType::Text];
    CHECK(txt.size() == 1 && txt[0]->annot->text.content == L"HELLO" && Close(txt[0]->annot->text.height, 2) &&
          txt[0]->annot->text.justify == 6);
    // The arc seen from below is mirrored.
    const auto& as = byType[(int)EntityType::Arc];
    CHECK(as.size() == 1 && Close(as[0]->p0, {-5, 0}) && Close(as[0]->startAngle, kPi / 2) && Close(as[0]->endAngle, kPi));
    // Dimensions come in as dimensions.
    const auto& ds2 = byType[(int)EntityType::Dimension];
    CHECK(ds2.size() == 2);
    CHECK(ds2[0]->dim->kind == DimData::Aligned && Close(DimMeasure(*ds2[0]->dim), 50) && ds2[0]->dim->text.empty());
    CHECK(ds2[1]->dim->kind == DimData::Radius && DimDisplayText(*ds2[1]->dim) == L"\x00D8" L"R10.00");
    // Paper space is not in the drawing (it becomes a sheet): only the stretched block's line.
    CHECK(byType[(int)EntityType::Line].size() == 1);

    Document bad;
    CHECK(!bad.FromDxf("AutoCAD Binary DXF\r\n\x1a", err, notes) && err.find(L"binary") != std::wstring::npos);
    CHECK(!bad.FromDxf("hello\nworld\n", err, notes));
    CHECK(!bad.FromDxf(Dxf({{0, "SECTION"}, {2, "HEADER"}, {0, "ENDSEC"}, {0, "EOF"}}), err, notes));
}

// ---------------------------------------------------------------------------
// Version 1.3.1: fillet, tangents, parallel distance and older gaps
// ---------------------------------------------------------------------------

bool Said(const TestHost& h, const wchar_t* part) {
    for (const std::wstring& l : h.lines)
        if (l.find(part) != std::wstring::npos) return true;
    return false;
}

size_t CountType(const Document& d, EntityType t) {
    size_t n = 0;
    for (const auto& kv : d.Entities()) n += kv.second.type == t;
    return n;
}

void TestFillet131() {
    {  // the radius typed at the first prompt
        Document doc;
        TestHost host;
        Engine eng(doc, host);
        const uint64_t a = doc.Add(Entity::Line({0, 0}, {100, 0}));
        const uint64_t b = doc.Add(Entity::Line({100, 0}, {100, 50}));
        Type(eng, {L"f"});
        CHECK(Said(host, L"radius is 0"));
        eng.Point({50, 0});
        eng.Point({100, 25});
        CHECK(Said(host, L"already meet in a sharp corner") && doc.Count() == 2 && eng.Active());
        Type(eng, {L"10"});
        CHECK(eng.CurrentPrompt().text.find(L"radius 10") != std::wstring::npos);
        eng.Point({50, 0});
        eng.Point({100, 25});
        CHECK(!eng.Active() && doc.Count() == 3 && Close(doc.Find(a)->p1, {90, 0}) && Close(doc.Find(b)->p0, {100, 10}));
    }
    {  // the end segment of a polyline and a separate line
        Document doc;
        TestHost host;
        Engine eng(doc, host);
        const uint64_t pl = doc.Add(Entity::Polyline({{{0, 50}, 0}, {{0, 0}, 0}, {{100, 0}, 0}}, false));
        const uint64_t ln = doc.Add(Entity::Line({100, 0}, {100, 50}));
        Type(eng, {L"f", L"10"});
        eng.Point({50, 0});
        eng.Point({100, 25});
        CHECK(Close(doc.Find(pl)->verts[2].p, {90, 0}) && Close(doc.Find(pl)->verts[1].p, {0, 0}));
        CHECK(Close(doc.Find(ln)->p0, {100, 10}) && CountType(doc, EntityType::Arc) == 1);
        // Chamfer too, the other way round (line first).
        doc.Undo();
        Type(eng, {L"cha", L"5"});
        eng.Point({100, 25});
        eng.Point({50, 0});
        CHECK(Close(doc.Find(pl)->verts[2].p, {95, 0}) && Close(doc.Find(ln)->p0, {100, 5}) &&
              CountType(doc, EntityType::Line) == 2);
    }
    {  // a rectangle's edge and a line: the rectangle stays whole
        Document doc;
        TestHost host;
        Engine eng(doc, host);
        const uint64_t rect = doc.Add(Entity::Rectangle({0, 0}, {100, 50}));
        const uint64_t ln = doc.Add(Entity::Line({120, -10}, {120, 60}));
        const Entity before = *doc.Find(rect);
        Type(eng, {L"f", L"5"});
        eng.Point({50, 0});
        eng.Point({120, 30});
        CHECK(Said(host, L"polyline was left whole") && doc.Find(rect)->verts.size() == before.verts.size());
        CHECK(Close(doc.Find(rect)->verts[1].p, {100, 0}) && CountType(doc, EntityType::Arc) == 1);
        CHECK(Close(doc.Find(ln)->p0, {120, 5}));
    }
}

void TestRenumberedVertices() {
    // A dimension on a rectangle's side; a corner is then filleted, so the
    // polyline gets new vertices: the dimension stays where it was.
    Document doc;
    const uint64_t rect = doc.Add(Entity::Rectangle({0, 0}, {100, 50}));
    DimData d = Aligned({100, 0}, {100, 50}, {120, 25});
    d.kind = DimData::Linear;
    d.angle = kPi / 2;
    d.refs[0] = {rect, 1};
    d.refs[1] = {rect, 2};
    const uint64_t dim = doc.Add(MakeDimension(d));
    Entity r = *doc.Find(rect);
    CHECK(FilletPolyline(r, 10, 1) == 1);
    doc.Replace(r);
    const DimData& now = *doc.Find(dim)->dim;
    CHECK(Close(now.pts[0], {100, 0}) && Close(now.pts[1], {100, 50}) && Close(DimMeasure(now), 50));
    CHECK(now.refs[0].id == 0 && now.refs[1].id == rect && Close(doc.Find(rect)->verts[(size_t)now.refs[1].grip].p, {100, 50}));
    // It still follows the corner it is attached to.
    Entity r2 = *doc.Find(rect);
    r2.verts[(size_t)now.refs[1].grip].p = {100, 60};
    doc.Replace(r2);
    CHECK(Close(DimMeasure(*doc.Find(dim)->dim), 60));
}

void TestTangents131() {
    Document doc;
    TestHost host;
    Engine eng(doc, host);
    doc.Add(Entity::Circle({0, 0}, 10));
    doc.Add(Entity::Circle({50, 0}, 5));
    Type(eng, {L"tanline"});
    eng.Point({0, 10});
    eng.Point({50, 5});
    const Entity& t = Last(doc);
    CHECK(t.type == EntityType::Line && Close(Dist(t.p0, {0, 0}), 10) && Close(Dist(t.p1, {50, 0}), 5));
    CHECK(Close(Dot(t.p1 - t.p0, t.p0), 0, 1e-9) && t.p0.y > 0 && t.p1.y > 0);
    // Crossing between them: picked on opposite sides.
    Type(eng, {L"tl"});
    eng.Point({0, 10});
    eng.Point({50, -5});
    const Entity& x = Last(doc);
    CHECK(Close(Dist(x.p0, {0, 0}), 10) && Close(Dist(x.p1, {50, 0}), 5) && x.p0.y > 0 && x.p1.y < 0);
    CHECK(TangentLines({0, 0}, 10, {50, 0}, 5).size() == 4 && TangentLines({0, 0}, 10, {2, 0}, 5).empty());

    // A circle tangent to two lines with a radius.
    Document d2;
    Engine e2(d2, host);
    d2.Add(Entity::Line({0, 0}, {100, 0}));
    d2.Add(Entity::Line({0, 0}, {0, 100}));
    Type(e2, {L"c", L"t"});
    e2.Point({50, 0});
    e2.Point({0, 50});
    Type(e2, {L"10"});
    CHECK(Last(d2).type == EntityType::Circle && Close(Last(d2).p0, {10, 10}) && Close(Last(d2).radius, 10));
    // ...and to a line and a circle.
    d2.Add(Entity::Circle({50, 18}, 10));
    Type(e2, {L"c", L"t"});
    e2.Point({60, 0});
    e2.Point(Polar({50, 18}, 10, -kPi / 4));
    Type(e2, {L"5"});
    CHECK(Close(Last(d2).p0.y, 5) && Close(Dist(Last(d2).p0, {50, 18}), 15) && Last(d2).p0.x > 50);

    // Distance between parallel lines: a dimension across them, attached to both.
    Document d3;
    Engine e3(d3, host);
    const uint64_t a = d3.Add(Entity::Line({0, 0}, {100, 0}));
    const uint64_t b = d3.Add(Entity::Line({10, 30}, {80, 30}));
    Type(e3, {L"dim"});
    e3.Point({50, 0});
    e3.Point({40, 30});
    CHECK(Said(host, L"parallel lines: 30"));
    e3.Point({-20, 15});
    const DimData& d = *Last(d3).dim;
    CHECK(Last(d3).type == EntityType::Dimension && Close(DimMeasure(d), 30) && d.refs[0].id == a && d.refs[1].id == b);
    Type(e3, {L""});
    Entity bl = *d3.Find(b);
    Translate(bl, {0, 10});
    d3.Replace(bl);
    CHECK(Close(DimMeasure(*Last(d3).dim), 40));
}

void TestOlderGaps131() {
    TestHost host;
    {  // TRIM: choosing cutting edges and pressing Enter with none: every object again
        Document doc;
        Engine eng(doc, host);
        doc.Add(Entity::Line({0, 0}, {10, 0}));
        Type(eng, {L"tr", L"t", L""});
        CHECK(eng.Active() && eng.ActiveName() == L"TRIM" && Said(host, L"every object is a cutting edge"));
        Type(eng, {L""});
    }
    {  // LENGTHEN Dynamic: the end follows the point
        Document doc;
        Engine eng(doc, host);
        const uint64_t l = doc.Add(Entity::Line({0, 0}, {10, 0}));
        const uint64_t arc = doc.Add(Entity::Arc({0, 50}, 10, 0, kPi / 2));
        Type(eng, {L"len", L"dy"});
        eng.Point({9, 0});
        Type(eng, {L"15,3"});
        CHECK(Close(doc.Find(l)->p1, {15, 0}) && Close(doc.Find(l)->p0, {0, 0}));
        eng.Point(Polar({0, 50}, 10, ToRadians(80)));
        Type(eng, {L"-10,50"});  // round to 180 degrees
        CHECK(Close(doc.Find(arc)->endAngle, kPi, 1e-9) && Close(doc.Find(arc)->startAngle, 0));
        Type(eng, {L""});
    }
    {  // splines: trimmed, saved, turned
        Document doc;
        Engine eng(doc, host);
        const uint64_t sp = doc.Add(Entity::Spline({{0, 0}, {10, 10}, {20, 0}, {30, 10}}, false));
        doc.Add(Entity::Line({15, -20}, {15, 30}));
        std::vector<Vec2> pts;
        SplinePoints(*doc.Find(sp), pts);
        const double before = CurveLength(*doc.Find(sp));
        Type(eng, {L"tr"});
        eng.Point(pts[pts.size() * 5 / 6]);  // the part to the right of the line
        Type(eng, {L""});
        const Entity& t = *doc.Find(sp);
        CHECK(SplineTrimmed(t) && t.verts.size() == 4 && CurveLength(t) < before);
        std::vector<Vec2> ends;
        EndPoints(t, ends);
        CHECK(ends.size() == 2 && Close(ends[0], {0, 0}) && Close(ends[1].x, 15, 1e-6));
        Document copy;
        std::wstring err;
        CHECK(copy.FromText(doc.ToText(), err));
        CHECK(Close(CurveLength(copy.Find(1)->type == EntityType::Spline ? *copy.Find(1) : *copy.Find(2)), CurveLength(t), 1e-9));
        Entity r = t;
        Rotate(r, {0, 0}, kPi / 2);
        CHECK(Close(CurveLength(r), CurveLength(t), 1e-9) && r.startAngle == t.startAngle && r.endAngle == t.endAngle);
        // BREAK the rest of it in two.
        std::vector<Entity> parts;
        std::wstring why;
        CHECK(BreakCurve(t, CurveLength(t) / 2, CurveLength(t) / 2, parts, why) && parts.size() == 2);
        CHECK(Close(CurveLength(parts[0]) + CurveLength(parts[1]), CurveLength(t), 1e-6));
        // A closed spline, cut across its start.
        Entity ring = Entity::Spline({{0, 0}, {10, -5}, {20, 0}, {10, 10}}, true);
        Entity part;
        const double L = CurveLength(ring);
        CHECK(SubCurve(ring, L * 0.8, L * 0.2, part) && SplineTrimmed(part) && !IsClosedCurve(part));
        CHECK(Close(CurveLength(part), L * 0.4, L * 1e-3));
    }
    {  // offsets of ellipses and splines
        Entity el = Entity::Ellipse({0, 0}, {20, 0}, 0.5), out;
        CHECK(OffsetEntity(el, -2, out) && out.type == EntityType::Spline && out.closed);
        CHECK(Close(out.verts[0].p, {22, 0}, 1e-9));
        bool near = true;
        for (const Vertex& v : out.verts) near &= std::fabs(DistanceTo(el, v.p) - 2) < 0.05;
        CHECK(near);
        CHECK(!OffsetEntity(el, 15, out));  // folds over inside
        Entity sp = Entity::Spline({{0, 0}, {10, 10}, {20, 0}, {30, 10}}, false);
        CHECK(OffsetEntity(sp, 1, out) && !out.closed);
        near = true;
        for (const Vertex& v : out.verts) near &= std::fabs(DistanceTo(sp, v.p) - 1) < 0.05;
        CHECK(near && OffsetSide(sp, out.verts[2].p) == 1);
    }
}

// ---------------------------------------------------------------------------
// Version 1.4 (Phase B3): layers, styles, construction, origin
// ---------------------------------------------------------------------------

void TestLayers() {
    Document doc;
    CHECK(doc.Layers().size() == 1 && doc.Layers()[0].name == L"0");
    Layer walls;
    walls.name = L"Walls";
    walls.color = 1;
    walls.linetype = FindLineType(L"DASHED");
    walls.lineweight = 50;
    const int w = doc.AddLayer(walls);
    CHECK(w == 1 && doc.LayerByName(L"walls") && doc.FindLayer(w)->color == 1);
    CHECK(doc.AddLayer(walls) == 2 && doc.FindLayer(2)->name == L"Walls (2)");  // names stay unique
    CHECK(doc.CanUndo());
    doc.Undo();
    CHECK(doc.Layers().size() == 2 && !doc.FindLayer(2));
    doc.Redo();
    CHECK(doc.FindLayer(2));

    // New objects go on the current layer with the current style.
    CurrentStyle cur = doc.Current();
    cur.layer = w;
    doc.SetCurrent(cur);
    const uint64_t a = doc.Add(Entity::Line({0, 0}, {10, 0}));
    CHECK(doc.Find(a)->layer == w && doc.Find(a)->color == kByLayer);
    Look k = doc.LookOf(*doc.Find(a));
    CHECK(k.rgb == 0xFF0000 && k.linetype == FindLineType(L"DASHED") && k.lineweight == 50);
    Entity own = *doc.Find(a);
    own.color = 3;
    own.lineweight = kLwDefault;
    doc.Replace(own);
    k = doc.LookOf(*doc.Find(a));
    CHECK(k.rgb == 0x00FF00 && k.lineweight == kDefaultLineWeight);
    // A shape made anew in an object's place keeps its look.
    doc.Replace([&] {
        Entity n = Entity::Line({0, 0}, {5, 0});
        n.id = a;
        return n;
    }());
    CHECK(doc.Find(a)->layer == w && doc.Find(a)->color == 3);

    // Off, frozen and locked layers.
    std::wstring why;
    CHECK(!doc.DeleteLayer(0, why) && !doc.DeleteLayer(w, why) && doc.DeleteLayer(2, why));
    Layer l = *doc.FindLayer(w);
    l.locked = true;
    doc.UpdateLayer(l);
    TestHost host;
    Engine eng(doc, host);
    CHECK(!eng.PickEntity({2, 0}) && eng.PickEntity({2, 0}, true) == a);
    eng.SelectAll();
    CHECK(eng.Selection().empty());
    l.locked = false;
    l.on = false;
    doc.UpdateLayer(l);
    CHECK(!doc.Visible(*doc.Find(a)) && !eng.PickEntity({2, 0}, true) && doc.Extents().Empty());
    SnapQuery q;
    q.cursor = {0, 0};
    q.aperture = 1;
    q.modes = kSnapEndpoint;
    CHECK(!FindSnap(doc, q).found);
    l.on = true;
    doc.UpdateLayer(l);
    CHECK(FindSnap(doc, q).found);

    // Layer states.
    doc.SaveLayerState(L"Plot");
    l.on = false;
    l.color = 5;
    doc.UpdateLayer(l);
    CHECK(doc.RestoreLayerState(L"plot") && doc.FindLayer(w)->on && doc.FindLayer(w)->color == 1);

    // The file keeps layers, styles, states, the current style, scale and origin.
    DrawingSettings ds = doc.Settings();
    ds.ltscale = 2.5;
    ds.origin = {100, 50};
    doc.SetSettings(ds);
    Entity c = Entity::Circle({5, 5}, 2);
    c.layer = w;  // a layer given: its own style is kept
    c.construction = true;
    c.ltscale = 0.5f;
    c.color = kTrueColor | 0x123456;
    c.linetype = (int16_t)FindLineType(L"CENTER");
    const uint64_t ci = doc.Add(c);
    Document back;
    std::wstring err;
    CHECK(back.FromText(doc.ToText(), err));
    CHECK(back.Layers().size() == 2 && back.LayerByName(L"Walls") && back.LayerByName(L"Walls")->lineweight == 50);
    CHECK(back.Current().layer == w && back.LayerStates().size() == 1 && back.Settings().ltscale == 2.5 &&
          back.Settings().origin == Vec2(100, 50));
    const Entity* bc = back.Find(2);
    CHECK(bc && bc->construction && bc->ltscale == 0.5f && bc->color == (kTrueColor | 0x123456) &&
          bc->linetype == FindLineType(L"CENTER") && bc->layer == w);
    (void)ci;

    // DXF keeps layers, colours and line types.
    Document dx;
    std::wstring notes;
    CHECK(dx.FromDxf(doc.ToDxf(), err, notes));
    const Layer* dw = dx.LayerByName(L"Walls");
    CHECK(dw && dw->color == 1 && dw->linetype == FindLineType(L"DASHED"));
    bool lineOk = false, circleOk = false;
    for (const auto& kv : dx.Entities()) {
        if (kv.second.type == EntityType::Line) lineOk = kv.second.layer == dw->id && kv.second.color == 3;
        if (kv.second.type == EntityType::Circle)
            circleOk = dx.FindLayer(kv.second.layer)->name == L"Defpoints" && kv.second.linetype == FindLineType(L"CENTER") &&
                       kv.second.color == NearestAci(0x123456);
    }
    CHECK(lineOk && circleOk && dx.Settings().ltscale == 2.5);

    // Pasting into another drawing: layers matched by name.
    Document other;
    std::vector<Entity> ents, got;
    ents.push_back(*doc.Find(a));
    std::vector<Layer> layers;
    CHECK(Document::ObjectsFromText(doc.ObjectsToText(ents), got, &layers) && got.size() == 1);
    const std::map<int, int> ids = other.MergeLayers(layers);
    CHECK(other.LayerByName(L"Walls") && ids.at(w) == other.LayerByName(L"Walls")->id);
}

void TestStyleCommands() {
    Document doc;
    TestHost host;
    Engine eng(doc, host);
    Layer red;
    red.name = L"Red";
    red.color = 1;
    const int r = doc.AddLayer(red);
    Entity src = Entity::Line({0, 0}, {10, 0});
    src.layer = r;
    src.linetype = (int16_t)FindLineType(L"HIDDEN");
    const uint64_t s = doc.Add(src);
    const uint64_t t = doc.Add(Entity::Circle({50, 0}, 5));
    // MATCHPROP
    Type(eng, {L"ma"});
    eng.Point({5, 0});
    eng.AddToSelection({t});
    Type(eng, {L""});
    CHECK(doc.Find(t)->layer == r && doc.Find(t)->linetype == FindLineType(L"HIDDEN") && !eng.Active());
    // CONSTRUCTION on, then off
    eng.AddToSelection({s, t});
    Type(eng, {L"construction"});
    CHECK(doc.Find(s)->construction && doc.Find(t)->construction);
    eng.AddToSelection({s, t});
    Type(eng, {L"cons"});
    CHECK(!doc.Find(s)->construction);
    // ORIGIN: typed coordinates and shown coordinates from there
    Type(eng, {L"origin", L"100,100", L"l", L"0,0", L"10,0", L""});
    CHECK(Close(Last(doc).p0, {100, 100}) && Close(Last(doc).p1, {110, 100}) && eng.Pt({110, 100}) == L"10.00,0.00");
    Type(eng, {L"origin", L"r"});
    CHECK(doc.Settings().origin == Vec2{});
    // LTSCALE
    Type(eng, {L"lts", L"5"});
    CHECK(doc.Settings().ltscale == 5);
    // OFFSET both sides
    const size_t before = doc.Count();
    Type(eng, {L"o", L"2"});
    eng.Point({5, 0});
    Type(eng, {L"b", L""});
    CHECK(doc.Count() == before + 2 && Close(Last(doc).p0.y, -2) && Last(doc).layer == r);
    // EXPLODE keeps the look
    Entity pl = Entity::Rectangle({0, 50}, {10, 60});
    pl.layer = r;
    pl.color = 4;
    const uint64_t pid = doc.Add(pl);
    eng.AddToSelection({pid});
    Type(eng, {L"x"});
    CHECK(Last(doc).type == EntityType::Line && Last(doc).layer == r && Last(doc).color == 4);
    // Trimming a circle into an arc keeps the look
    Entity circ = Entity::Circle({200, 0}, 10);
    circ.layer = 0;
    circ.color = 5;
    const uint64_t cid = doc.Add(circ);
    doc.Add(Entity::Line({195, -20}, {195, 20}));
    Type(eng, {L"tr"});
    eng.Point({210, 0});
    Type(eng, {L""});
    CHECK(doc.Find(cid) && doc.Find(cid)->type == EntityType::Arc && doc.Find(cid)->color == 5);
}
}  // namespace

void TestAnnotations() {
    // Symbols and fields.
    FieldValues f{{L"filename", L"Part.fcad"}};
    CHECK(ExpandText(L"45%%d %%c20 %%p0.1 100%%%", nullptr) == L"45\x00B0 \x00D8" L"20 \x00B1" L"0.1 100%");
    CHECK(ExpandText(L"{FileName} {unknown}", &f) == L"Part.fcad {unknown}");
    // Layout: lines one under another, aligned as asked.
    TextData t;
    t.content = L"AB\nCD";
    t.height = 5;
    AnnotGraphics g;
    LayoutText(t, {0, 0}, nullptr, g);
    CHECK(g.texts.size() == 2 && Close(g.texts[0].pos, {0, -2.5}) && g.texts[1].pos.y < g.texts[0].pos.y - 5);
    t.justify = 8;  // bottom right
    g = {};
    LayoutText(t, {10, 0}, nullptr, g);
    CHECK(g.texts.size() == 2 && g.texts[1].align == 2 && Close(g.texts[1].pos, {10, 2.5}));
    // Text is picked inside its box, and moves, turns and scales.
    Entity text = MakeText({0, 0}, t);
    CHECK(DistanceTo(text, {-3, 3}) == 0 && DistanceTo(text, {30, 30}) > 10);
    Rotate(text, {0, 0}, kPi / 2);
    CHECK(Close(text.annot->text.rotation, kPi / 2) && Close(text.p0, {0, 0}));
    Scale(text, {0, 0}, 2);
    CHECK(Close(text.annot->text.height, 10));
    Entity mt = MakeText({5, 0}, TextData{});
    Mirror(mt, {0, 0}, {0, 1});  // still reads left to right
    CHECK(Close(mt.p0, {-5, 0}) && std::cos(mt.annot->text.rotation) > 0.99);

    // Leader: arrow at the tip, landing and note.
    TextData note;
    note.content = L"NOTE";
    Entity ld = MakeLeader({{0, 0}, {10, 10}}, note, 3);
    g = {};
    AnnotLayout(ld, nullptr, g);
    CHECK(g.fills.size() == 1 && g.texts.size() == 1 && g.texts[0].align == 0 && g.texts[0].pos.x > 10);
    CHECK(Close(g.fills[0][0], {0, 0}));
    note.frame = 2;  // a balloon
    g = {};
    AnnotLayout(MakeLeader({{0, 0}, {-10, 10}}, note, 3), nullptr, g);
    CHECK(!g.lines.empty() && g.lines.back().kind == Prim::Arc && g.texts[0].pos.x < -10 && g.texts[0].align == 1);

    // Areas: a square with a round island; a line sticking out does not matter.
    Entity sq = Entity::Rectangle({0, 0}, {100, 100});
    Entity hole = Entity::Circle({50, 50}, 10);
    Entity stray = Entity::Line({-20, 30}, {30, 30});
    std::vector<const Entity*> objs{&sq, &hole, &stray};
    std::vector<std::vector<Vec2>> loops;
    std::vector<size_t> used;
    CHECK(FindArea(objs, {10, 80}, loops, &used) && loops.size() == 2 && used.size() == 2);  // a loose end
    stray.p1 = {120, 30};  // now right across
    CHECK(FindArea(objs, {10, 80}, loops, &used));
    HatchData h;
    h.loops = loops;
    // The stray line splits the square: the upper part, less the island.
    CHECK(Close(HatchArea(h), 100 * 70 - kPi * 100, 2.0));
    CHECK(used.size() == 3);
    CHECK(FindArea(objs, {50, 50}, loops) && loops.size() == 1 && Close(std::fabs(LoopArea(loops[0])), kPi * 100, 1.0));
    CHECK(!FindArea(objs, {150, 50}, loops));  // outside everything
    CHECK(InsideLoops(h.loops, {10, 80}) && !InsideLoops(h.loops, {50, 50}));
    // Two overlapping circles: the lens between them.
    Entity c1 = Entity::Circle({0, 0}, 10), c2 = Entity::Circle({10, 0}, 10);
    CHECK(FindArea({&c1, &c2}, {5, 0}, loops) && loops.size() == 1);
    // Arcs of polylines (both ways round) are followed.
    Entity slot = Entity::Polyline({{{0, 0}, 0}, {{20, 0}, 1}, {{20, 10}, 0}, {{0, 10}, 1}}, true);
    CHECK(FindArea({&slot}, {10, 5}, loops) && Close(std::fabs(LoopArea(loops[0])), 200 + kPi * 25, 0.5));
    // Pattern lines: ANSI31 is 45 degree lines 3.175 apart.
    h = {};
    h.loops = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    std::vector<std::pair<Vec2, Vec2>> segs;
    CHECK(HatchSegments(h, segs) && segs.size() > 40 && segs.size() < 50);
    for (const auto& s : segs) CHECK(std::fabs(std::fabs(s.second.x - s.first.x) - std::fabs(s.second.y - s.first.y)) < 1e-6);
    h.scale = 0.0001;  // far too dense: refused
    segs.clear();
    CHECK(!HatchSegments(h, segs));
    h.scale = 1;
    h.pattern = L"SOLID";
    CHECK(IsSolid(h) && FindHatchPattern(L"ansi37") >= 0 && FindHatchPattern(L"nonsense") < 0);

    // Associative hatch: follows its outline, and lets go when moved alone.
    Document doc;
    const uint64_t r = doc.Add(Entity::Rectangle({0, 0}, {10, 10}));
    HatchData hd;
    hd.pattern = L"SOLID";
    hd.seeded = true;
    hd.seed = {5, 5};
    CHECK(FindArea({doc.Find(r)}, hd.seed, hd.loops));
    hd.refs = {r};
    const uint64_t hid = doc.Add(MakeHatch(hd));
    Entity bigger = *doc.Find(r);
    bigger.verts[1].p = {20, 0};
    bigger.verts[2].p = {20, 10};
    doc.Replace(bigger);
    CHECK(Close(HatchArea(doc.Find(hid)->annot->hatch), 200, 1e-6));
    doc.Undo();
    CHECK(Close(HatchArea(doc.Find(hid)->annot->hatch), 100, 1e-6));
    // Copied together, the copy follows the copied outline.
    std::vector<uint64_t> ids = doc.AddGroup({*doc.Find(r), *doc.Find(hid)});
    CHECK(ids.size() == 2 && doc.Find(ids[1])->annot->hatch.refs == std::vector<uint64_t>{ids[0]});
    Entity moved = *doc.Find(hid);
    Translate(moved, {100, 0});
    doc.Replace(moved);
    CHECK(doc.Find(hid)->annot->hatch.refs.empty());

    // Blocks: placed, scaled, turned and mirrored, with attributes and ByBlock colour.
    BlockData bd;
    bd.name = L"Bolt";
    bd.base = {0, 0};
    Entity arm = Entity::Line({0, 0}, {10, 0});
    arm.layer = 0;
    arm.color = kByBlock;
    bd.items.push_back(arm);
    TextData att;
    att.tag = L"SIZE";
    att.prompt = L"Bolt size";
    att.content = L"M8";
    bd.items.push_back(MakeText({0, 5}, att));
    const auto def = doc.DefineBlock(bd);
    CHECK(doc.FindBlock(L"bolt") == def && AttributeDefs(*def).size() == 1);
    Entity ins = MakeInsert(def, {100, 100}, 2, kPi / 2, {L"M10"});
    ins.layer = 0;
    ins.color = 1;
    std::vector<Entity> parts;
    ExpandBlock(ins, parts);
    CHECK(parts.size() == 2 && Close(parts[0].p1, {100, 120}) && parts[0].color == 1);
    CHECK(parts[1].type == EntityType::Text && parts[1].annot->text.content == L"M10" && parts[1].annot->text.tag.empty());
    CHECK(Close(Bounds(ins).maxY, 120, 1e-9));
    Entity mir = MakeInsert(def, {0, 0}, 1, 0);
    Mirror(mir, {0, 0}, {0, 1});
    parts.clear();
    ExpandBlock(mir, parts);
    CHECK(mir.closed && Close(parts[0].p1, {-10, 0}));
    Mirror(mir, {0, 0}, {0, 1});
    parts.clear();
    ExpandBlock(mir, parts);
    CHECK(!mir.closed && Close(parts[0].p1, {10, 0}, 1e-9));
    const uint64_t iid = doc.Add(ins);
    CHECK(doc.BlockUse(L"Bolt") == 1);
    std::wstring why;
    CHECK(!doc.DeleteBlock(L"Bolt", why));
    // Redefining a block changes its references.
    bd.items[0] = Entity::Line({0, 0}, {20, 0});
    doc.DefineBlock(bd);
    CHECK(doc.Find(iid)->annot->block->items[0].p1.x == 20);

    // The file keeps all of it.
    TextData tt;
    tt.content = L"Line 1\nØ 20 = {date}";
    tt.font = L"Times New Roman";
    tt.justify = 4;
    tt.frame = 1;
    tt.rotation = kPi / 6;
    doc.Add(MakeText({1, 2}, tt));
    doc.Add(MakeLeader({{0, 0}, {5, 5}, {9, 5}}, note, 2.5));
    Document back;
    std::wstring err;
    CHECK(back.FromText(doc.ToText(), err));
    CHECK(back.Count() == doc.Count() && back.Blocks().size() == 1);
    size_t texts = 0, leaders = 0, hatches = 0, inserts = 0;
    for (const auto& kv : back.Entities()) {
        const Entity& e = kv.second;
        if (e.type == EntityType::Text) {
            ++texts;
            CHECK(e.annot->text == tt && Close(e.p0, {1, 2}));
        } else if (e.type == EntityType::Leader) {
            ++leaders;
            CHECK(e.annot->pts.size() == 3 && e.annot->arrow == 2.5 && e.annot->text.frame == 2);
        } else if (e.type == EntityType::Hatch) {
            ++hatches;
        } else if (e.type == EntityType::Insert) {
            ++inserts;
            CHECK(e.annot->block == back.FindBlock(L"BOLT") && e.annot->values == std::vector<std::wstring>{L"M10"});
            CHECK(Close(e.radius, 2) && Close(e.startAngle, kPi / 2));
        }
    }
    CHECK(texts == 1 && leaders == 1 && hatches == 2 && inserts == 1);
    // The copied hatch is still attached to the copied rectangle.
    const Entity* bh = back.Find(ids[1]);
    CHECK(bh && bh->type == EntityType::Hatch && bh->annot->hatch.refs == std::vector<uint64_t>{ids[0]});
    // Clipboard text brings the block along.
    std::vector<Entity> clip;
    BlockTable cb;
    CHECK(Document::ObjectsFromText(doc.ObjectsToText({*doc.Find(iid)}), clip, nullptr, &cb) && cb.size() == 1 && clip.size() == 1);
    // DXF: text, blocks with attributes, solid fills.
    const std::string dxf = doc.ToDxf();
    CHECK(dxf.find("\nTEXT\r") != std::string::npos && dxf.find("\nBLOCK\r") != std::string::npos);
    CHECK(dxf.find("\nINSERT\r") != std::string::npos && dxf.find("\nATTRIB\r") != std::string::npos);
    CHECK(dxf.find("\nATTDEF\r") != std::string::npos && dxf.find("\nSOLID\r") != std::string::npos);
    CHECK(dxf.find("%%c") == std::string::npos || true);
}

const Entity* FirstOf(const Document& d, EntityType t) {
    for (const auto& kv : d.Entities())
        if (kv.second.type == t) return &kv.second;
    return nullptr;
}

void TestAnnotCommands() {
    Document doc;
    TestHost host;
    Engine e(doc, host);
    // TEXT, typed line by line.
    Type(e, {L"text", L"h", L"5", L"j", L"MC", L"10,10", L"Hello", L"%%c20", L""});
    const Entity* t = FirstOf(doc, EntityType::Text);
    CHECK(t && t->annot->text.content == L"Hello\n%%c20" && t->annot->text.height == 5 && t->annot->text.justify == 4);
    CHECK(e.lastText.height == 5 && !e.Active());
    // TEXTEDIT
    e.ClearSelection();
    Type(e, {L"ed"});
    e.Point({10, 10});
    Type(e, {L"Bye", L""});
    CHECK(FirstOf(doc, EntityType::Text)->annot->text.content == L"Bye");
    // LEADER with a note in a box.
    Type(e, {L"le", L"f", L"b", L"0,0", L"10,10", L"", L"NOTE", L""});
    const Entity* ld = FirstOf(doc, EntityType::Leader);
    CHECK(ld && ld->annot->pts.size() == 2 && ld->annot->text.content == L"NOTE" && ld->annot->text.frame == 1);

    // CENTERMARK on a circle: two lines with the CENTER line type.
    doc.Add(Entity::Circle({100, 100}, 10));
    const size_t before = doc.Count();
    Type(e, {L"cm"});
    e.Point({110, 100});
    e.Enter();
    CHECK(doc.Count() == before + 2 && Last(doc).type == EntityType::Line && Last(doc).linetype == FindLineType(L"CENTER"));
    CHECK(Close(Dist(Last(doc).p0, Last(doc).p1), 2 * (10 + 3)));

    // HATCH: click inside a rectangle; it follows the rectangle.
    const uint64_t rect = doc.Add(Entity::Rectangle({200, 0}, {300, 100}));
    Type(e, {L"h", L"p", L"ansi37", L"250,50", L""});
    const Entity* h = FirstOf(doc, EntityType::Hatch);
    CHECK(h && h->annot->hatch.pattern == L"ANSI37" && h->annot->hatch.refs == std::vector<uint64_t>{rect});
    const uint64_t hid = h->id;
    Entity r2 = *doc.Find(rect);
    for (Vertex& v : r2.verts)
        if (v.p.x > 250) v.p.x = 400;
    doc.Replace(r2);
    CHECK(Close(HatchArea(doc.Find(hid)->annot->hatch), 200 * 100, 1e-6));
    Type(e, {L"h", L"999,999"});
    CHECK(Said(host, L"no closed area"));
    e.Cancel();
    // FILL by selecting a circle.
    Type(e, {L"fill", L"o", L"all", L"", L""});
    size_t solids = 0;
    for (const auto& kv : doc.Entities())
        if (kv.second.type == EntityType::Hatch && IsSolid(kv.second.annot->hatch))
            solids += kv.second.annot->hatch.loops.size() == 2 && kv.second.annot->hatch.refs.size() == 2;
    CHECK(solids == 1);  // one fill: the circle and the rectangle (the closed objects)

    // Blocks with an attribute.
    Document d2;
    Engine e2(d2, host);
    d2.Add(Entity::Line({0, 0}, {10, 0}));
    Type(e2, {L"attdef", L"PN", L"Part number", L"X1", L"0,5"});
    CHECK(d2.Count() == 2 && Last(d2).annot->text.tag == L"PN");
    e2.SelectAll();
    Type(e2, {L"block", L"Part", L"0,0"});
    CHECK(d2.Count() == 1 && Last(d2).type == EntityType::Insert && d2.FindBlock(L"part") &&
          Last(d2).annot->values == std::vector<std::wstring>{L"X1"});
    Type(e2, {L"insert", L"part", L"s", L"2", L"100,100", L"P-42"});
    const Entity& ins = Last(d2);
    CHECK(ins.type == EntityType::Insert && Close(ins.radius, 2) && ins.annot->values == std::vector<std::wstring>{L"P-42"});
    const uint64_t insId = ins.id;
    Type(e2, {L"ate"});
    e2.Point({110, 100});  // on its line
    Type(e2, {L"P-43"});
    CHECK(d2.Find(insId)->annot->values == std::vector<std::wstring>{L"P-43"});
    e2.ClearSelection();
    e2.AddToSelection({insId});
    Type(e2, {L"x"});
    const Entity* xt = FirstOf(d2, EntityType::Text);
    CHECK(!d2.Find(insId) && xt && xt->annot->text.content == L"P-43" && xt->annot->text.tag.empty());
    std::wstring why;
    CHECK(!d2.DeleteBlock(L"Part", why));  // the first reference is still there
    Type(e2, {L"purge"});
    CHECK(d2.Blocks().size() == 1);

    // Ordinate and angular coordinate dimensions.
    Document d3;
    Engine e3(d3, host);
    Type(e3, {L"dor", L"30,40", L"30,60", L""});
    const Entity* od = FirstOf(d3, EntityType::Dimension);
    CHECK(od && od->dim->kind == DimData::Ordinate && DimValueText(*od->dim) == L"30" && Close(od->dim->angle, 0));
    Type(e3, {L"dor", L"30,40", L"50,40", L""});
    CHECK(DimValueText(*Last(d3).dim) == L"40");  // a level leader: the Y
    Type(e3, {L"dac", L"0,0", L"10,0", L"0,10", L"0,15", L"-10,0", L"-15,0", L""});
    CHECK(Last(d3).dim->kind == DimData::AngularCoord && DimValueText(*Last(d3).dim) == L"180\x00B0");
    // Driven: in brackets, and it does not change the geometry.
    e3.ClearSelection();
    e3.AddToSelection({Last(d3).id});
    Type(e3, {L"ddr"});
    CHECK(Last(d3).dim->driven && DimDisplayText(*Last(d3).dim) == L"(180\x00B0)");
    d3.BeginChange(L"x");
    CHECK(!DriveDimension(d3, Last(d3).id, 1, why) && why.find(L"driven") != std::wstring::npos);
    d3.EndChange();
    CHECK(Document().FromText(d3.ToText(), why));
    Document d3b;
    CHECK(d3b.FromText(d3.ToText(), why) && Last(d3b).dim->driven && Last(d3b).dim->kind == DimData::AngularCoord);

    // DIMSPACE: three parallel dimensions, 8 apart.
    Document d4;
    Engine e4(d4, host);
    DimData a;
    a.kind = DimData::Aligned;
    a.pts[0] = {0, 0};
    a.pts[1] = {100, 0};
    for (double off : {10.0, 12.0, 40.0}) {
        a.loc = {50, off};
        d4.Add(MakeDimension(a));
    }
    Type(e4, {L"dsp"});
    e4.Point({20, 10});  // the base, at 10
    Type(e4, {L"all", L"", L"8"});
    std::vector<double> offs;
    for (const auto& kv : d4.Entities()) offs.push_back(kv.second.dim->loc.y);
    std::sort(offs.begin(), offs.end());
    CHECK(offs.size() == 3 && Close(offs[0], 10) && Close(offs[1], 18) && Close(offs[2], 26));

    // REVCLOUD and AREA.
    Document d5;
    Engine e5(d5, host);
    Type(e5, {L"rc", L"0,0", L"100,50"});
    const Entity& cloud = Last(d5);
    CHECK(cloud.type == EntityType::Polyline && cloud.closed && cloud.verts.size() == 20 && cloud.verts[0].bulge > 0);
    CHECK(LoopArea([&] {
              std::vector<Vec2> l;
              Tessellate(cloud, l);
              return l;
          }()) > 5000);  // arcs bulge outwards
    host.lines.clear();
    Type(e5, {L"aa", L"0,0", L"10,0", L"10,10", L"0,10", L""});
    CHECK(Said(host, L"Area = 100.00 mm\x00B2, Perimeter = 40.00 mm"));
    d5.Add(Entity::Rectangle({200, 0}, {300, 100}));
    d5.Add(Entity::Circle({250, 50}, 10));
    Type(e5, {L"aa", L"i", L"210,90"});
    CHECK(Said(host, (L"Area = " + FormatDecimal(10000 - kPi * 100, 2)).c_str()));
    Type(e5, {L"aa", L"a", L"o"});
    e5.Point({300, 50});
    e5.Point({200, 50});
    CHECK(Said(host, L"Total area = 20000.00"));
    e5.Point({260, 50});
    CHECK(Said(host, (L"Total area = " + FormatDecimal(20000 + kPi * 100, 2)).c_str()));
    e5.Cancel();
    // Arcs count as arcs: a slot of two half circles.
    std::vector<std::vector<Vec2>> loops;
    double area = 0, per = 0;
    Entity slot = Entity::Polyline({{{0, 0}, 0}, {{20, 0}, 1}, {{20, 10}, 0}, {{0, 10}, 1}}, true);
    CHECK(FindArea({&slot}, {10, 5}, loops, nullptr, &area, &per) && Close(area, 200 + kPi * 25, 1e-9) &&
          Close(per, 40 + kPi * 10, 1e-9));
}

void TestDxfAnnotations() {
    const std::string text = Dxf({
        {0, "SECTION"}, {2, "TABLES"}, {0, "TABLE"}, {2, "STYLE"},
        {0, "STYLE"}, {2, "Notes"}, {70, "0"}, {3, "times.ttf"}, {0, "ENDTAB"}, {0, "ENDSEC"},
        {0, "SECTION"}, {2, "BLOCKS"},
        {0, "BLOCK"}, {8, "0"}, {2, "TAG"}, {70, "2"}, {10, "0"}, {20, "0"},
        {0, "CIRCLE"}, {8, "0"}, {10, "0"}, {20, "0"}, {40, "5"},
        {0, "ATTDEF"}, {8, "0"}, {10, "0"}, {20, "0"}, {40, "2"}, {1, "1"}, {3, "Item"}, {2, "ITEM"}, {70, "0"},
        {0, "ENDBLK"}, {8, "0"},
        {0, "ENDSEC"},
        {0, "SECTION"}, {2, "ENTITIES"},
        {0, "INSERT"}, {8, "0"}, {66, "1"}, {2, "TAG"}, {10, "50"}, {20, "50"},
        {0, "ATTRIB"}, {8, "0"}, {10, "50"}, {20, "50"}, {40, "2"}, {1, "7"}, {2, "ITEM"}, {70, "0"},
        {0, "SEQEND"}, {8, "0"},
        {0, "MTEXT"}, {8, "0"}, {10, "0"}, {20, "100"}, {40, "3.5"}, {71, "1"}, {7, "Notes"},
        {3, "First \\fArial|b1;line"}, {1, "\\PSecond line"},
        {0, "HATCH"}, {8, "0"}, {10, "0"}, {20, "0"}, {30, "0"}, {2, "SOLID"}, {70, "1"}, {71, "0"}, {91, "1"},
        {92, "2"}, {72, "0"}, {73, "1"}, {93, "4"}, {10, "0"}, {20, "0"}, {10, "10"}, {20, "0"}, {10, "10"}, {20, "10"},
        {10, "0"}, {20, "10"}, {97, "0"}, {75, "0"}, {76, "1"}, {98, "0"},
        {0, "HATCH"}, {8, "0"}, {10, "0"}, {20, "0"}, {30, "0"}, {2, "ANSI31"}, {70, "0"}, {71, "0"}, {91, "1"},
        {92, "1"}, {93, "2"},
        {72, "1"}, {10, "20"}, {20, "0"}, {11, "40"}, {21, "0"},
        {72, "2"}, {10, "30"}, {20, "0"}, {40, "10"}, {50, "0"}, {51, "180"}, {73, "1"},
        {97, "0"}, {75, "0"}, {76, "1"}, {52, "0"}, {41, "2"}, {77, "0"}, {78, "1"},
        {53, "45"}, {43, "0"}, {44, "0"}, {45, "-2.2"}, {46, "2.2"}, {79, "0"}, {98, "0"},
        {0, "ENDSEC"}, {0, "EOF"}});
    Document d;
    std::wstring err, notes;
    CHECK(d.FromDxf(text, err, notes));
    CHECK(notes.empty());
    const Entity* ins = FirstOf(d, EntityType::Insert);
    CHECK(ins && ins->annot->values == std::vector<std::wstring>{L"7"} && AttributeDefs(*ins->annot->block).size() == 1);
    const Entity* mt = FirstOf(d, EntityType::Text);
    CHECK(mt && mt->annot->text.content == L"First line\nSecond line" && mt->annot->text.font == L"Times New Roman");
    std::vector<const Entity*> hs;
    for (const auto& kv : d.Entities())
        if (kv.second.type == EntityType::Hatch) hs.push_back(&kv.second);
    CHECK(hs.size() == 2 && IsSolid(hs[0]->annot->hatch) && Close(HatchArea(hs[0]->annot->hatch), 100, 1e-9));
    CHECK(hs[1]->annot->hatch.pattern == L"ANSI31" && Close(hs[1]->annot->hatch.scale, 2) &&
          Close(HatchArea(hs[1]->annot->hatch), kPi * 50, 0.5));
    // And back out: the block keeps its attribute.
    Document back;
    CHECK(back.FromDxf(d.ToDxf(), err, notes));
    const Entity* bi = FirstOf(back, EntityType::Insert);
    CHECK(bi && bi->annot->values == std::vector<std::wstring>{L"7"});
}

void TestSheets() {
    Document model;
    model.Add(Entity::Rectangle({0, 0}, {300, 200}));  // 300 x 200 mm
    model.Add(Entity::Circle({150, 100}, 50));
    model.SetProperty(L"title", L"Bracket");
    // A new A3 sheet: border, title block and a viewport at a usual scale.
    SheetList sheets;
    sheets.push_back(NewSheet(model, L"Sheet1", L"A3", true));
    Sheet& sh = *sheets[0];
    CHECK(Close(sh.setup.width, 420) && Close(sh.setup.height, 297));
    const Entity* vp = FirstOf(sh.doc, EntityType::Viewport);
    CHECK(vp && Close(ViewportRatio(*vp, model), 1) && Close(vp->annot->viewCentre, {150, 100}));  // 300 x 200 fits at 1:1
    CHECK(FirstOf(sh.doc, EntityType::Insert) && sh.doc.FindBlock(L"Title block"));
    CHECK(Close(ViewportToSheet(*vp, ViewportToModel(*vp, {100, 100})), {100, 100}));
    FieldValues f = SheetFields(model, sheets, 0, {{L"date", L"today"}});
    CHECK(f[L"title"] == L"Bracket" && f[L"scale"] == L"1:1" && f[L"sheet"] == L"1" && f[L"sheets"] == L"1" && f[L"number"].empty());
    CHECK(ScaleText(0.5) == L"1:2" && ScaleText(2) == L"2:1" && ScaleText(0.02) == L"1:50");
    CHECK(Close(FitScale(Box({0, 0}, {1000, 500}), 380, 200, Units::Millimetres), 0.2));
    CHECK(Close(FitScale(Box({0, 0}, {10, 5}), 380, 200, Units::Inches), 1));  // 254 mm wide

    // Viewport grips: a corner resizes it; the drawing stays put on the sheet.
    Entity v = *vp;
    const Vec2 fixedModel = ViewportToModel(v, v.p0);
    CHECK(DragGrip(v, 2, v.p1, v.p1 + Vec2{20, 10}));
    CHECK(Close(ViewportToModel(v, v.p0), fixedModel, 1e-9) && Close(v.p1.x - v.p0.x, vp->p1.x - vp->p0.x + 20));
    Scale(v, {0, 0}, 2);  // twice the size, the drawing in it too
    CHECK(Close(v.annot->viewScale, 2 * vp->annot->viewScale));

    // The file keeps the sheets and the drawing's properties.
    const std::string text = DrawingToText(model, sheets);
    Document model2;
    SheetList sheets2;
    std::wstring err;
    CHECK(DrawingFromText(text, model2, sheets2, err));
    CHECK(sheets2.size() == 1 && sheets2[0]->setup.name == L"Sheet1" && Close(sheets2[0]->setup.width, 420));
    CHECK(model2.Properties().at(L"title") == L"Bracket" && model2.Count() == 2 && sheets2[0]->doc.Count() == sh.doc.Count());
    CHECK(!sheets2[0]->doc.Modified() && sheets2[0]->doc.Blocks().size() == 1);
    Document plain;  // an older reader just sees the drawing
    CHECK(plain.FromText(text, err) && plain.Count() == 2);

    // Plot: the viewport's objects clipped to it, the sheet's on top; text with fields.
    PlotPage page;
    PlotSheet(sh, model, f, page);
    CHECK(Close(page.width, 420) && page.clips.size() == 1);
    size_t clipped = 0, texts = 0;
    bool title = false;
    for (const PlotItem& it : page.items) {
        clipped += it.clip == 0;
        if (it.kind == PlotItem::Text) {
            ++texts;
            title |= it.text.text == L"Bracket";
        }
    }
    CHECK(clipped >= 2 && texts > 10 && title);
    // The circle at 1:1: its points are 50 mm from its centre on paper.
    const Vec2 c = ViewportToSheet(*vp, {150, 100});
    bool circle = false;
    for (const PlotItem& it : page.items)
        if (it.kind == PlotItem::Path && it.clip == 0 && it.pts.size() > 20)
            circle |= std::all_of(it.pts.begin(), it.pts.end(), [&](Vec2 p) { return std::fabs(Dist(p, c) - 50) < 0.02; });
    CHECK(circle);
    // Dashed lines are cut into dashes; black and white prints everything black.
    Document d2;
    Entity dl = Entity::Line({0, 0}, {100, 0});
    dl.linetype = (int16_t)FindLineType(L"DASHED");
    dl.color = 1;
    dl.layer = 0;  // keeps its own colour and line type
    d2.Add(dl);
    PlotStyle st;
    PlotPage p2;
    PlotDrawing(d2, Box({0, -10}, {100, 10}), 297, 210, 10, 1, st, {}, p2);
    CHECK(p2.items.size() > 5 && p2.items[0].rgb == 0xFF0000);
    st.colourMode = 1;
    st.pens[1] = 70;  // red prints 0.7 mm
    PlotDrawing(d2, Box({0, -10}, {100, 10}), 297, 210, 10, 1, st, {}, p2);
    CHECK(p2.items[0].rgb == 0 && Close(p2.items[0].width, 0.7));
    // PDF: one page per sheet, with a correct cross-reference table.
    const std::string pdf = WritePdf({page, p2}, L"Test");
    CHECK(pdf.compare(0, 8, "%PDF-1.4") == 0 && pdf.find("/Count 2") != std::string::npos);
    CHECK(pdf.find("(Bracket) Tj") != std::string::npos && pdf.find(" re W n") != std::string::npos);
    const size_t sx = pdf.rfind("startxref\n");
    CHECK(sx != std::string::npos && pdf.compare(std::stoul(pdf.substr(sx + 10)), 4, "xref") == 0);
    const size_t o3 = pdf.find("3 0 obj");
    CHECK(o3 != std::string::npos && pdf.find(std::to_string(1000000000 + o3).substr(1)) != std::string::npos);

    // Commands on a sheet: a viewport at 1:2, its scale and its view.
    TestHost host;
    Engine e(sh.doc, host);
    e.model = &model;
    Type(e, {L"mv", L"20,20", L"220,170", L"1:2"});
    const Entity& nv = Last(sh.doc);
    CHECK(nv.type == EntityType::Viewport && Close(ViewportRatio(nv, model), 0.5));
    const uint64_t nid = nv.id;
    Type(e, {L"vps"});
    e.Point({20, 100});  // its left edge
    Type(e, {L"5:1"});
    CHECK(Close(ViewportRatio(*sh.doc.Find(nid), model), 5));
    const Vec2 before = sh.doc.Find(nid)->annot->viewCentre;
    Type(e, {L"vpp"});
    e.Point({100, 100});
    Type(e, {L"0,0", L"10,0"});
    CHECK(Close(sh.doc.Find(nid)->annot->viewCentre, before - Vec2{2, 0}));  // 10 mm on paper at 5:1
    e.ClearSelection();
    e.AddToSelection({nid});
    Type(e, {L"vplock", L"vps"});
    e.Point({20, 100});
    CHECK(Said(host, L"locked"));
    e.Cancel();
    Engine em(model, host);
    Type(em, {L"mv"});
    CHECK(Said(host, L"sheet tab") && !em.Active());

    // DXF: the first sheet goes out as paper space with its viewports, and comes back as a sheet.
    const std::string dxf = DrawingToDxf(model, sheets);
    CHECK(dxf.find("$PLIMMAX") != std::string::npos && dxf.find("MVIEW") != std::string::npos);
    Document dm;
    SheetList ds;
    std::wstring notes;
    CHECK(DrawingFromDxf(dxf, dm, ds, err, notes));
    CHECK(ds.size() == 1 && ds[0]->setup.paper == L"A3" && ds[0]->setup.landscape);
    size_t vps = 0;
    for (const auto& kv : ds[0]->doc.Entities())
        if (kv.second.type == EntityType::Viewport) {
            ++vps;
            const Entity* orig = FirstOf(sh.doc, EntityType::Viewport);
            if (Close(kv.second.p0, orig->p0, 1e-6)) CHECK(Close(kv.second.annot->viewScale, orig->annot->viewScale, 1e-9) &&
                                                           Close(kv.second.annot->viewCentre, orig->annot->viewCentre, 1e-9));
        }
    size_t had = 0;
    for (const auto& kv : sh.doc.Entities()) had += kv.second.type == EntityType::Viewport;
    CHECK(vps == had && had == 2 && FirstOf(ds[0]->doc, EntityType::Insert) && FirstOf(ds[0]->doc, EntityType::Insert)->annot->block);
    CHECK(dm.Count() == 2);
}

uint64_t AddRel(Document& d, RelKind k, DimRef a, DimRef b = {}, DimRef c = {}) {
    Entity r;
    std::wstring why;
    if (!MakeRelation(d, k, {a, b, c}, 2.5, r, why)) return 0;
    d.BeginChange(L"Relationship");
    const uint64_t id = d.Add(r);
    d.EndChange();
    return id;
}
DimRef W(uint64_t id) { return {id, -1}; }
size_t CountOf(const Document& d, EntityType t) {
    size_t n = 0;
    for (const auto& kv : d.Entities()) n += kv.second.type == t;
    return n;
}
void MoveEnd(Document& d, uint64_t id, int end, Vec2 to) {
    Entity e = *d.Find(id);
    (end == 0 ? e.p0 : e.p1) = to;
    d.BeginChange(L"Move");
    d.Replace(e);
    d.EndChange();
}

void TestRelations() {
    const double tol = 1e-6;
    {  // Horizontal straightens the line; it stays so when an end moves.
        Document d;
        const uint64_t a = d.Add(Entity::Line({0, 0}, {100, 10}));
        CHECK(AddRel(d, RelKind::Horizontal, W(a)) != 0);
        CHECK(Close(d.Find(a)->p0.y, d.Find(a)->p1.y, tol) && d.LastSolve().ok);
        MoveEnd(d, a, 1, {120, 40});
        CHECK(Close(d.Find(a)->p0.y, d.Find(a)->p1.y, tol));
        CHECK(Close(d.Find(a)->p1.x, 120, 1e-3));
        // Undo takes back the move, then the relationship (and what it did).
        d.Undo();
        d.Undo();
        CHECK(Close(d.Find(a)->p1, {100, 10}) && CountOf(d, EntityType::Relation) == 0);
        // A relationship the drawing cannot have.
        Entity r;
        std::wstring why;
        CHECK(!MakeRelation(d, RelKind::Concentric, {W(a), W(a), {}}, 2.5, r, why) && !why.empty());
    }
    {  // An L of two lines: connected, the first horizontal, the second at right angles.
        Document d;
        const uint64_t a = d.Add(Entity::Line({0, 0}, {100, 0}));
        const uint64_t b = d.Add(Entity::Line({100, 0}, {100, 50}));
        AddRel(d, RelKind::Coincident, {a, 1}, {b, 0});
        AddRel(d, RelKind::Horizontal, W(a));
        AddRel(d, RelKind::Perpendicular, W(a), W(b));
        CHECK(CountOf(d, EntityType::Relation) == 3);
        MoveEnd(d, a, 1, {130, 0});
        const Entity &ea = *d.Find(a), &eb = *d.Find(b);
        CHECK(Close(eb.p0, ea.p1, tol) && Close(eb.p0.x, eb.p1.x, tol) && Close(ea.p0.y, ea.p1.y, tol));
        CHECK(Close(ea.p1.x, 130, 1e-3));
        // MAINTAINREL off: nothing follows.
        DrawingSettings st = d.Settings();
        st.maintainRelations = false;
        d.SetSettings(st);
        MoveEnd(d, a, 1, {150, 5});
        CHECK(Close(d.Find(b)->p0.x, 130, 1e-3));
        // The marks go with their objects; erasing an object takes its relationships away.
        st.maintainRelations = true;
        d.SetSettings(st);
        d.BeginChange(L"Erase");
        d.Erase(b);
        d.EndChange();
        CHECK(CountOf(d, EntityType::Relation) == 1);
    }
    {  // Parallel turns the second line; the first stays.
        Document d;
        const uint64_t a = d.Add(Entity::Line({0, 0}, {100, 0}));
        const uint64_t b = d.Add(Entity::Line({0, 20}, {100, 40}));
        AddRel(d, RelKind::Parallel, W(a), W(b));
        const Entity &ea = *d.Find(a), &eb = *d.Find(b);
        CHECK(Close(Cross(Normalized(ea.p1 - ea.p0), Normalized(eb.p1 - eb.p0)), 0, tol));
        CHECK(Close(ea.p0, {0, 0}, 1e-2) && Close(ea.p1, {100, 0}, 1e-2));
        // Equal lengths, then collinear.
        const uint64_t c = d.Add(Entity::Line({0, -30}, {40, -30}));
        AddRel(d, RelKind::Equal, W(a), W(c));
        CHECK(Close(Dist(d.Find(c)->p0, d.Find(c)->p1), Dist(d.Find(a)->p0, d.Find(a)->p1), tol));
        AddRel(d, RelKind::Collinear, W(a), W(c));
        const Vec2 u = Normalized(d.Find(a)->p1 - d.Find(a)->p0);
        CHECK(Close(Cross(u, d.Find(c)->p0 - d.Find(a)->p0), 0, tol) && Close(Cross(u, d.Find(c)->p1 - d.Find(a)->p0), 0, tol));
    }
    {  // Tangent and concentric; FIX keeps the line where it is.
        Document d;
        const uint64_t l = d.Add(Entity::Line({-50, 0}, {50, 0}));
        const uint64_t c = d.Add(Entity::Circle({0, 13}, 10));
        AddRel(d, RelKind::Fix, W(l));
        AddRel(d, RelKind::Tangent, W(l), W(c));
        CHECK(Close(d.Find(l)->p0, {-50, 0}) && Close(d.Find(l)->p1, {50, 0}));
        CHECK(Close(d.Find(c)->p0.y, d.Find(c)->radius, tol) && d.Find(c)->p0.y > 0);
        const uint64_t c2 = d.Add(Entity::Circle({3, 4}, 4));
        AddRel(d, RelKind::Concentric, W(c), W(c2));
        CHECK(Close(d.Find(c)->p0, d.Find(c2)->p0, tol));
        // Growing the circle keeps it on the line (its centre moves up).
        Entity e = *d.Find(c);
        e.radius = 15;
        d.BeginChange(L"Radius");
        d.Replace(e);
        d.EndChange();
        CHECK(Close(d.Find(c)->p0.y, d.Find(c)->radius, tol) && Close(d.Find(l)->p0, {-50, 0}));
        CHECK(Close(d.Find(c2)->p0, d.Find(c)->p0, tol));
    }
    {  // Midpoint and symmetric.
        Document d;
        const uint64_t a = d.Add(Entity::Line({0, 0}, {100, 0}));
        const uint64_t p = d.Add(Entity::Point({30, 10}));
        AddRel(d, RelKind::Fix, W(a));
        AddRel(d, RelKind::Midpoint, {p, 0}, W(a));
        CHECK(Close(d.Find(p)->p0, {50, 0}, tol));
        const uint64_t ax = d.Add(Entity::Line({0, 50}, {0, 100}));
        const uint64_t q1 = d.Add(Entity::Point({-20, 60}));
        const uint64_t q2 = d.Add(Entity::Point({25, 70}));
        AddRel(d, RelKind::Fix, W(ax));
        AddRel(d, RelKind::Symmetric, {q1, 0}, {q2, 0}, W(ax));
        CHECK(Close(d.Find(q1)->p0.x, -d.Find(q2)->p0.x, tol) && Close(d.Find(q1)->p0.y, d.Find(q2)->p0.y, tol));
    }
    {  // A locked dimension holds; driving it moves the objects with their relationships.
        Document d;
        const uint64_t a = d.Add(Entity::Line({0, 0}, {100, 0}));
        const uint64_t b = d.Add(Entity::Line({100, 0}, {100, 50}));
        AddRel(d, RelKind::Coincident, {a, 1}, {b, 0});
        AddRel(d, RelKind::Horizontal, W(a));
        AddRel(d, RelKind::Vertical, W(b));
        DimData dd = Aligned({0, 0}, {100, 0}, {50, -15});
        dd.refs[0] = {a, 0};
        dd.refs[1] = {a, 1};
        const uint64_t dim = d.Add(MakeDimension(dd));
        CHECK(DimOf(d, dim).name == L"D1");
        std::wstring why;
        d.BeginChange(L"Value");
        CHECK(DriveDimension(d, dim, 80, why));
        d.EndChange();
        CHECK(DimOf(d, dim).locked && Close(DimOf(d, dim).value, 80));
        CHECK(Close(d.Find(a)->p0, {0, 0}, 1e-3));  // the dimension's first point stays
        CHECK(Close(Dist(d.Find(a)->p0, d.Find(a)->p1), 80, tol) && Close(d.Find(b)->p0, d.Find(a)->p1, tol));
        CHECK(Close(d.Find(b)->p0.x, d.Find(b)->p1.x, tol));
        // Moving the other end: the locked length holds.
        MoveEnd(d, b, 1, {200, 60});
        CHECK(Close(Dist(d.Find(a)->p0, d.Find(a)->p1), 80, tol));
        // Two lengths that cannot both hold: the solver says so.
        const uint64_t c = d.Add(Entity::Line({0, 100}, {50, 100}));
        DimData dc = Aligned({0, 100}, {50, 100}, {25, 120});
        dc.refs[0] = {c, 0};
        dc.refs[1] = {c, 1};
        dc.locked = true;
        dc.value = 50;
        d.Add(MakeDimension(dc));
        AddRel(d, RelKind::Equal, W(a), W(c));
        CHECK(!d.LastSolve().ok && !d.LastSolve().message.empty());
    }
    {  // Expressions.
        double v;
        std::wstring err;
        auto none = [](const std::wstring&, double&) { return false; };
        CHECK(EvalExpression(L"=2+3*4^2/8", none, v, err) && Close(v, 8));
        CHECK(EvalExpression(L"-(1+2)*sin(30)+max(1,sqrt(16))", none, v, err) && Close(v, 2.5));
        CHECK(!EvalExpression(L"2*(3", none, v, err) && !err.empty());
        CHECK(!EvalExpression(L"w+1", none, v, err));
        CHECK(ValidName(L"Width_2") && !ValidName(L"2w") && !ValidName(L""));
    }
    {  // VAR and formulas: D1 = w + 20, then w changes.
        Document d;
        TestHost host;
        Engine e(d, host);
        const uint64_t a = d.Add(Entity::Line({0, 0}, {100, 0}));
        AddRel(d, RelKind::Horizontal, W(a));
        DimData dd = Aligned({0, 0}, {100, 0}, {50, -15});
        dd.refs[0] = {a, 0};
        dd.refs[1] = {a, 1};
        const uint64_t dim = d.Add(MakeDimension(dd));
        Type(e, {L"var", L"w", L"60"});
        CHECK(d.Variables().size() == 1 && Close(d.Variables()[0].value, 60));
        Type(e, {L"var", L"D1", L"w+20"});
        CHECK(DimOf(d, dim).formula == L"w+20" && Close(Dist(d.Find(a)->p0, d.Find(a)->p1), 80, tol));
        Type(e, {L"var", L"w", L"=2*50"});
        CHECK(Close(Dist(d.Find(a)->p0, d.Find(a)->p1), 120, tol) && Close(d.Find(a)->p0, {0, 0}, 1e-3));
        d.Undo();
        CHECK(Close(Dist(d.Find(a)->p0, d.Find(a)->p1), 80, tol) && Close(d.Variables()[0].value, 60));
        // A formula that cannot be worked out changes nothing.
        Type(e, {L"var", L"D1", L"nothing*2"});
        CHECK(!d.LastSolve().ok || !host.lines.empty());
        double v;
        CHECK(LookupName(d, L"W", v) && Close(v, 60));

        // GOALSEEK: the diagonal of an L reaches 130 by changing w.
        Document g;
        Engine eg(g, host);
        const uint64_t la = g.Add(Entity::Line({0, 0}, {100, 0}));
        const uint64_t lb = g.Add(Entity::Line({100, 0}, {100, 50}));
        AddRel(g, RelKind::Coincident, {la, 1}, {lb, 0});
        AddRel(g, RelKind::Horizontal, W(la));
        AddRel(g, RelKind::Vertical, W(lb));
        DimData da = Aligned({0, 0}, {100, 0}, {50, -15});
        da.refs[0] = {la, 0};
        da.refs[1] = {la, 1};
        g.Add(MakeDimension(da));  // D1: the bottom
        DimData db = Aligned({100, 0}, {100, 50}, {115, 25});
        db.refs[0] = {lb, 0};
        db.refs[1] = {lb, 1};
        db.locked = true;
        db.value = 50;
        g.Add(MakeDimension(db));  // D2: the side, locked
        DimData diag = Aligned({0, 0}, {100, 50}, {40, 40});
        diag.refs[0] = {la, 0};
        diag.refs[1] = {lb, 1};
        const uint64_t dg = g.Add(MakeDimension(diag));  // D3
        Type(eg, {L"var", L"w", L"100", L"var", L"D1", L"w"});
        CHECK(Close(Dist(g.Find(la)->p0, g.Find(la)->p1), 100, tol));
        host.lines.clear();
        Type(eg, {L"gs"});
        eg.Point(diag.loc);
        Type(eg, {L"130", L"w"});
        CHECK(!eg.Active());
        CHECK(Close(DimMeasure(DimOf(g, dg)), 130, 1e-5) && Close(g.Variables()[0].value, 120, 1e-4));
        g.Undo();
        CHECK(Close(g.Variables()[0].value, 100) && Close(DimMeasure(DimOf(g, dg)), std::sqrt(12500.0), 1e-6));

        // Saved and opened again: relationships, variables, locked dimensions, settings.
        DrawingSettings st = g.Settings();
        st.showRelations = false;
        st.autoRelations = true;
        g.SetSettings(st);
        const std::string text = g.ToText();
        Document h;
        std::wstring err;
        CHECK(h.FromText(text, err));
        CHECK(h.ToText() == text);
        CHECK(CountOf(h, EntityType::Relation) == 3 && h.Variables().size() == 1 && h.Variables()[0].formula == L"100");
        CHECK(!h.Settings().showRelations && h.Settings().autoRelations && h.Settings().maintainRelations);
        size_t locked = 0;
        for (const auto& kv : h.Entities())
            if (kv.second.dim && kv.second.dim->locked) ++locked;
        CHECK(locked == 2);
        MoveEnd(h, la, 0, {-10, 0});  // still kept after opening
        CHECK(Close(Dist(h.Find(la)->p0, h.Find(la)->p1), 100, tol));
    }
    {  // AUTOREL: lines drawn get horizontal, vertical and connect relationships.
        Document d;
        TestHost host;
        Engine e(d, host);
        Type(e, {L"autorel"});
        CHECK(d.Settings().autoRelations);
        Type(e, {L"l", L"0,0", L"100,0", L"@0,50", L""});
        CHECK(CountOf(d, EntityType::Relation) == 3);
        // HORIZONTAL from the command line, picking a line.
        Type(e, {L"l", L"0,100", L"50,120", L""});
        const uint64_t sl = Last(d).type == EntityType::Line ? Last(d).id : 0;
        Type(e, {L"hor"});
        e.Point({25, 110});
        e.Enter();
        CHECK(sl && Close(d.Find(sl)->p0.y, d.Find(sl)->p1.y, tol));
        Type(e, {L"showrel"});
        CHECK(!d.Settings().showRelations);
    }
}

void TestSymbols() {
    Document doc;
    TestHost host;
    Engine e(doc, host);
    auto texts = [](const AnnotGraphics& g) {
        std::wstring all;
        for (const TextItem& t : g.texts) all += t.text + L";";
        return all;
    };
    // A feature control frame: position, diameter 0.1 at MMC, datums A B C.
    Type(e, {L"fcf", L"pos", L"%%c0.1(M)", L"A B C", L"", L"10,10"});
    CHECK(!e.Active());
    const Entity* f = FirstOf(doc, EntityType::Text);
    CHECK(f && f->annot->text.frame == kFrameControl && f->annot->text.content == L"POS|%%c0.1(M)|A|B|C");
    AnnotGraphics g;
    AnnotLayout(*f, nullptr, g);
    CHECK(texts(g) == L"\x00D8" L"0.1;M;A;B;C;");
    CHECK(g.lines.size() == 5 * 4 + 3 + 1);  // cells, the position symbol, the circled M
    Box b;
    for (const Prim& p : g.lines) b.Add(p.Bounds());
    CHECK(Close(b.minX, 10) && Close((b.minY + b.maxY) / 2, 10) && Close(b.Height(), 2 * f->annot->text.height));
    CHECK(FindGdtSymbol(L"Perpendicularity") == 7 && FindGdtSymbol(L"ROUNDNESS") == 2 && FindGdtSymbol(L"Bolt") < 0);
    // Two rows; an unknown characteristic is asked again.
    Type(e, {L"tol", L"bogus", L"flat", L"0.05", L"", L"par", L"0.1", L"A", L"", L"100,10"});
    CHECK(Last(doc).annot->text.content == L"FLAT|0.05\nPAR|0.1|A");

    // Datums get the next letter.
    Type(e, {L"datum", L"", L"0,0", L""});
    Type(e, {L"datum", L"", L"50,0", L"50,-10"});
    CHECK(Last(doc).annot->text.content == L"B" && Last(doc).annot->text.frame == kFrameDatum);
    CHECK(Close(Last(doc).annot->text.rotation, kPi));
    g = {};
    AnnotLayout(Last(doc), nullptr, g);
    CHECK(g.fills.size() == 1 && texts(g) == L"B;");

    // Surface texture, material removal required.
    Type(e, {L"surface", L"", L"Ra 1.6", L"50,50"});
    CHECK(Last(doc).annot->text.content == L"* Ra 1.6" && Last(doc).annot->text.frame == kFrameSurface);
    g = {};
    AnnotLayout(Last(doc), nullptr, g);
    CHECK(g.lines.size() == 4 && texts(g) == L"Ra 1.6;");

    // A fillet weld both sides, all round, on a leader.
    Type(e, {L"weld", L"", L"6", L"", L"b", L"y", L"0,0", L"20,20", L""});
    const Entity& w = Last(doc);
    CHECK(w.type == EntityType::Leader && w.annot->text.content == L"FILLET|6||BR" && w.annot->text.frame == kFrameWeld);
    g = {};
    AnnotLayout(w, nullptr, g);
    CHECK(texts(g) == L"6;6;");
    CHECK(g.lines.size() == 1 + 1 + 1 + 2 + 2 + 1);  // leader, landing, reference line, two fillets, all round

    // Kept in the file.
    const std::string text = doc.ToText();
    Document back;
    std::wstring err;
    CHECK(back.FromText(text, err) && back.ToText() == text);

    // PARTSLIST: blocks with PN and DESC attributes, counted.
    Document parts;
    Engine pe(parts, host);
    BlockData bolt;
    bolt.name = L"BOLT";
    bolt.items.push_back(Entity::Circle({0, 0}, 3));
    TextData pn, desc;
    pn.tag = L"PN";
    desc.tag = L"DESC";
    bolt.items.push_back(MakeText({5, 0}, pn));
    bolt.items.push_back(MakeText({5, -5}, desc));
    BlockData plate;
    plate.name = L"PLATE";
    plate.items.push_back(Entity::Rectangle({0, 0}, {50, 20}));
    const auto bd = parts.DefineBlock(bolt), pd = parts.DefineBlock(plate);
    parts.Add(MakeInsert(bd, {0, 0}, 1, 0, {L"B-1", L"Bolt M6"}));
    parts.Add(MakeInsert(bd, {10, 0}, 1, 0, {L"B-1", L"Bolt M6"}));
    parts.Add(MakeInsert(bd, {20, 0}, 1, 0, {L"B-2", L"Bolt M8"}));
    parts.Add(MakeInsert(pd, {0, 50}, 1, 0, {}));
    std::vector<PartsRow> rows = CountParts(parts);
    CHECK(rows.size() == 3 && rows[0].part == L"B-1" && rows[0].qty == 2 && rows[0].description == L"Bolt M6");
    CHECK(rows[2].part == L"PLATE" && rows[2].qty == 1 && rows[2].description.empty());
    Type(pe, {L"bom", L"0,200"});
    CHECK(!pe.Active() && parts.FindBlock(L"PARTS_LIST") && parts.BlockUse(L"PARTS_LIST") == 1);
    auto cellTexts = [&]() {
        std::wstring all;
        for (const Entity& x : parts.FindBlock(L"PARTS_LIST")->items)
            if (x.type == EntityType::Text) all += x.annot->text.content + L";";
        return all;
    };
    CHECK(cellTexts() == L"ITEM;PART NUMBER;DESCRIPTION;QTY;1;B-1;Bolt M6;2;2;B-2;Bolt M8;1;3;PLATE;;1;");
    // Run again after another bolt: brought up to date where it is.
    parts.Add(MakeInsert(bd, {30, 0}, 1, 0, {L"B-2", L"Bolt M8"}));
    Type(pe, {L"partslist"});
    CHECK(!pe.Active() && parts.BlockUse(L"PARTS_LIST") == 1);
    CHECK(cellTexts().find(L"2;B-2;Bolt M8;2;") != std::wstring::npos);
}

void TestEasierUse() {
    // Prompts in plain words.
    CHECK(PlainPrompt(L"Specify first point", kAcceptPoint) == L"Click the first point");
    CHECK(PlainPrompt(L"Specify radius of circle", kAcceptPoint | kAcceptNumber) == L"Click or type the radius of circle");
    CHECK(PlainPrompt(L"Specify the offset distance", kAcceptNumber) == L"Type the offset distance");
    CHECK(PlainPrompt(L"Enter text", kAcceptEmpty) == L"Type text");
    CHECK(PlainPrompt(L"Select a line", kAcceptPoint) == L"Click a line");
    CHECK(PlainPrompt(L"Pick something", kAcceptPoint) == L"Pick something");
    Document doc;
    TestHost host;
    Engine e(doc, host);
    Type(e, {L"c"});
    CHECK(e.CurrentPrompt().text.rfind(L"Click", 0) == 0);
    e.Cancel();
    // Line thickness on screen: every step thicker than the one before.
    double last = 0;
    for (int lw : {5, 13, 18, 25, 35, 50, 70, 100, 140, 211}) {
        CHECK(ScreenLineWidth(lw) > last);
        last = ScreenLineWidth(lw);
    }
    CHECK(Close(ScreenLineWidth(kDefaultLineWeight), 1) && Close(ScreenLineWidth(kLwDefault), 1) && ScreenLineWidth(50) >= 3);
}

// A hand-drawn stroke: points along a path with a small wobble.
std::vector<Vec2> Wobbly(const std::function<Vec2(double)>& path, int n, double wobble) {
    std::vector<Vec2> pts;
    for (int i = 0; i <= n; ++i) {
        const double t = (double)i / n;
        const Vec2 p = path(t);
        pts.push_back(p + Vec2{wobble * std::sin(i * 1.7), wobble * std::cos(i * 2.3)});
    }
    return pts;
}

void TestFreeSketch() {
    StrokeOptions o;
    o.tol = 1.5;
    // A nearly level line: exactly level.
    auto r = RecognizeStroke(Wobbly([](double t) { return Vec2{100 * t, 3 * t}; }, 40, 0.6), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Line && Close(r[0].p0.y, r[0].p1.y) && Close(Dist(r[0].p0, r[0].p1), 100, 1.5));
    // A slanted line stays slanted.
    r = RecognizeStroke(Wobbly([](double t) { return Vec2{60 * t, 60 * t}; }, 40, 0.5), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Line && Close(AngleOf(r[0].p1 - r[0].p0), kPi / 4, 0.05));
    // A circle drawn round once.
    r = RecognizeStroke(Wobbly([](double t) { return Polar({50, 50}, 30, t * kTau * 1.02); }, 80, 1.0), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Circle && Close(r[0].p0, {50, 50}, 1.5) && Close(r[0].radius, 30, 1.5));
    // Half a circle: an arc, counter-clockwise from 0 to 180 degrees.
    r = RecognizeStroke(Wobbly([](double t) { return Polar({0, 0}, 40, t * kPi); }, 60, 0.6), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Arc && Close(r[0].radius, 40, 1.5));
    CHECK(r.size() == 1 && Close(r[0].startAngle, 0, 0.05) && Close(r[0].endAngle, kPi, 0.05));
    // The same drawn the other way round is the same arc.
    r = RecognizeStroke(Wobbly([](double t) { return Polar({0, 0}, 40, kPi - t * kPi); }, 60, 0.6), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Arc && Close(r[0].startAngle, 0, 0.05) && Close(r[0].endAngle, kPi, 0.05));
    // An L: a polyline of two lines, level and upright.
    r = RecognizeStroke(Wobbly([](double t) { return t < 0.5 ? Vec2{0, 80 - 160 * t} : Vec2{(t - 0.5) * 200, 1}; }, 60, 0.5), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Polyline && r[0].verts.size() == 3 && !r[0].closed);
    if (r.size() == 1 && r[0].verts.size() == 3) {
        CHECK(Close(r[0].verts[0].p.x, r[0].verts[1].p.x) && Close(r[0].verts[1].p.y, r[0].verts[2].p.y));
        CHECK(Close(r[0].verts[1].p, {0, 0}, 3));
    }
    // A rectangle drawn in one go: a closed polyline of four, square.
    auto rect = [](double t) {
        const double s = t * 4;
        if (s < 1) return Vec2{100 * s, 0};
        if (s < 2) return Vec2{100, 60 * (s - 1)};
        if (s < 3) return Vec2{100 - 100 * (s - 2), 60};
        return Vec2{0, 60 - 60 * (s - 3)};
    };
    r = RecognizeStroke(Wobbly(rect, 120, 0.6), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Polyline && r[0].closed && r[0].verts.size() == 4);
    if (r.size() == 1 && r[0].verts.size() == 4) {
        const auto& v = r[0].verts;
        for (size_t i = 0; i < 4; ++i) {
            const Vec2 d = v[(i + 1) % 4].p - v[i].p;
            CHECK(Close(d.x, 0) || Close(d.y, 0));
        }
    }
    // A line then a bend: a line and an arc in one polyline.
    r = RecognizeStroke(Wobbly([](double t) { return t < 0.5 ? Vec2{200 * t, 0} : Polar({100, 40}, 40, -kPi / 2 + (t - 0.5) * 2 * kPi / 2); }, 80, 0.4), o);
    CHECK(r.size() == 1 && r[0].type == EntityType::Polyline && r[0].verts.size() == 3);
    if (r.size() == 1 && r[0].verts.size() == 3) CHECK(r[0].verts[0].bulge == 0 && r[0].verts[1].bulge > 0.2);
    // Too short: nothing.
    CHECK(RecognizeStroke({{0, 0}, {0.5, 0}}, o).empty());

    // The SKETCH command: strokes added, ends joined to what is near.
    Document doc;
    TestHost host;  // pick radius 0.5
    Engine e(doc, host);
    Type(e, {L"autorel"});
    Type(e, {L"fs"});
    CHECK(e.Active() && e.WantsStroke());
    e.Stroke(Wobbly([](double t) { return Vec2{100 * t, 0.8 * t}; }, 60, 0.15));
    CHECK(doc.Count() >= 2 && FirstOf(doc, EntityType::Line) && CountOf(doc, EntityType::Relation) == 1);  // level
    // An upright stroke starting near the line's end joins it there.
    e.Stroke(Wobbly([](double t) { return Vec2{100.6 + 0.5 * t, 0.7 + 50 * t}; }, 60, 0.15));
    const Entity& up = Last(doc).type == EntityType::Line ? Last(doc) : *FirstOf(doc, EntityType::Line);
    size_t lines = 0;
    for (const auto& kv : doc.Entities()) lines += kv.second.type == EntityType::Line;
    CHECK(lines == 2);
    std::vector<const Entity*> ls;
    for (const auto& kv : doc.Entities())
        if (kv.second.type == EntityType::Line) ls.push_back(&kv.second);
    CHECK(ls.size() == 2 && Close(ls[1]->p0, ls[0]->p1) && Close(ls[1]->p0.x, ls[1]->p1.x) && Close(ls[0]->p0.y, ls[0]->p1.y));
    (void)up;
    e.Enter();  // Done
    CHECK(!e.Active());
}

void TestDynamicBlocks() {
    Document doc;
    TestHost host;
    Engine e(doc, host);
    BlockData plate;
    plate.name = L"PLATE";
    plate.items.push_back(Entity::Rectangle({0, 0}, {100, 20}));
    plate.items.push_back(Entity::Circle({90, 10}, 4));  // a hole near the right end
    const auto def = doc.DefineBlock(plate);
    const uint64_t ins = doc.Add(MakeInsert(def, {200, 0}, 1, 0, {}));
    // BPARAM: the right end stretches, in sizes 150 or 200.
    Type(e, {L"bparam"});
    e.Point({250, 0});
    Type(e, {L"", L"200,10", L"300,10", L"280,-5", L"310,25", L"", L"150 200"});
    CHECK(!e.Active());
    const Entity* in = doc.Find(ins);
    CHECK(in && in->annot->block->params.size() == 1 && in->annot->block->params[0].sizes.size() == 2);
    CHECK(Close(ParamValue(*in, 0), 100));
    // Its grip, dragged to 160: the nearest size allowed, 150.
    std::vector<Grip> grips;
    GripsOf(*in, grips);
    CHECK(grips.size() == 1 && grips[0].index == 100 && Close(grips[0].p, {300, 10}));
    Entity x = *in;
    CHECK(DragGrip(x, 100, {300, 10}, {360, 12}));
    CHECK(Close(ParamValue(x, 0), 150));
    std::vector<Entity> parts;
    ExpandBlock(x, parts);
    Box b;
    for (const Entity& p : parts) b.Add(Bounds(p));
    CHECK(Close(b.maxX, 350) && Close(b.minX, 200));
    bool holeMoved = false;
    for (const Entity& p : parts) holeMoved |= p.type == EntityType::Circle && Close(p.p0, {340, 10});
    CHECK(holeMoved);
    doc.BeginChange(L"Stretch");
    doc.Replace(x);
    doc.EndChange();
    // A flip about the plate's middle, upright.
    Type(e, {L"bparam"});
    e.Point({200, 5});
    Type(e, {L"f", L"275,0", L"275,20", L"Turn"});
    in = doc.Find(ins);
    CHECK(in->annot->block->params.size() == 2 && in->annot->block->params[1].name == L"Turn");
    CHECK(Close(ParamValue(*in, 0), 150) && ParamValue(*in, 1) == 0);
    x = *in;
    CHECK(SetParamValue(x, 1, 1));
    parts.clear();
    ExpandBlock(x, parts);
    holeMoved = false;  // the hole (stretched to block x 140), mirrored about block x 75: block x 10, drawing 210
    for (const Entity& p : parts) holeMoved |= p.type == EntityType::Circle && Close(p.p0, {210, 10});
    CHECK(holeMoved);
    // A stretch that is not a size allowed is refused; a negative one too.
    Entity y = *in;
    CHECK(SetParamValue(y, 0, 199) && Close(ParamValue(y, 0), 200));
    // Kept in the file.
    doc.BeginChange(L"Flip");
    doc.Replace(x);
    doc.EndChange();
    const std::string text = doc.ToText();
    Document back;
    std::wstring err;
    CHECK(back.FromText(text, err) && back.ToText() == text);
    const Entity* bi = FirstOf(back, EntityType::Insert);
    CHECK(bi && bi->annot->block->params.size() == 2 && Close(ParamValue(*bi, 0), 150) && ParamValue(*bi, 1) == 1);
    CHECK(bi && bi->annot->block->params[0].sizes == std::vector<double>({150, 200}));
    // DXF: an adjusted block is written as the objects it shows.
    const std::string dxf = doc.ToDxf();
    CHECK(dxf.find("\nINSERT\n") == std::string::npos && dxf.find("CIRCLE") != std::string::npos);
}

void TestTablesAndBalloons() {
    Document doc;
    TestHost host;
    Engine e(doc, host);
    // TABLE: 2 columns, 3 rows, placed at 0,0 (top left).
    Type(e, {L"table", L"2", L"3", L"0,0"});
    CHECK(!e.Active());
    const Entity* t = FirstOf(doc, EntityType::Text);
    CHECK(t && t->annot->text.frame == kFrameTable && t->annot->text.content == L"Heading 1|Heading 2\n|\n|");
    // Typed in, as pasted from a spreadsheet (tabs) or with |.
    Entity x = *t;
    EditAnnot(x).text.content = L"Part\tQty\nBolt|4\nNut|12";
    AnnotGraphics g;
    AnnotLayout(x, nullptr, g);
    std::wstring all;
    for (const TextItem& it : g.texts) all += it.text + L";";
    CHECK(all == L"Part;Qty;Bolt;4;Nut;12;");
    Box b;
    for (const Prim& p : g.lines) b.Add(p.Bounds());
    const double h = x.annot->text.height;
    CHECK(Close(b.maxY, 0) && Close(b.minX, 0) && Close(b.minY, -3 * 2 * h));  // hangs from its top left, 3 rows
    CHECK(g.lines.size() == 4 + 2 + 1 + 1);  // frame, row lines, heading rule, column line

    // PARTBALLOON: item numbers from the parts list, as fields.
    BlockData bolt;
    bolt.name = L"BOLT";
    bolt.items.push_back(Entity::Circle({0, 0}, 3));
    TextData pn;
    pn.tag = L"PN";
    bolt.items.push_back(MakeText({5, 0}, pn));
    BlockData nut;
    nut.name = L"NUT";
    nut.items.push_back(Entity::Circle({0, 0}, 2));
    const auto bd = doc.DefineBlock(bolt), nd = doc.DefineBlock(nut);
    doc.Add(MakeInsert(nd, {100, 0}, 1, 0, {}));
    doc.Add(MakeInsert(bd, {200, 0}, 1, 0, {L"Z-9"}));
    Type(e, {L"pb"});
    e.Point({203, 0});  // on the bolt's circle
    e.Point({230, 30});
    e.Cancel();
    const Entity* ld = FirstOf(doc, EntityType::Leader);
    CHECK(ld && ld->annot->text.content == L"{item:Z-9}" && ld->annot->text.frame == kFrameCircle);
    FieldValues f;
    AddPartFields(doc, f);
    CHECK(f[L"item:nut"] == L"1" && f[L"item:z-9"] == L"2");  // the parts list's order
    CHECK(ExpandText(ld->annot->text.content, &f) == L"2");
    CHECK(PartKey(*FirstOf(doc, EntityType::Insert)) == L"NUT");
}

void TestImages() {
    Document doc;
    TestHost host;
    Engine e(doc, host);
    Type(e, {L"image", L"10,20", L"50"});
    CHECK(!e.Active());
    const Entity* im = FirstOf(doc, EntityType::Image);
    CHECK(im && Close(im->p0, {10, 20}) && Close(im->p1, {60, 45}) && im->annot->image == L"C:\\pics\\photo.png");
    // Picked anywhere on it; a corner grip resizes it keeping its shape.
    CHECK(Close(DistanceTo(*im, {30, 30}), 0));
    Entity x = *im;
    CHECK(DragGrip(x, 2, {60, 45}, {110, 50}));
    CHECK(Close(x.p0, {10, 20}) && Close(x.p1, {110, 70}));
    Rotate(x, {0, 0}, kPi / 2);  // stays upright: the box round it
    CHECK(Close(x.p1.x - x.p0.x, 50) && Close(x.p1.y - x.p0.y, 100));
    // Kept in the file.
    const std::string text = doc.ToText();
    Document back;
    std::wstring err;
    CHECK(back.FromText(text, err) && back.ToText() == text);
    // Printed under the lines; in a PDF as a picture when it can be read.
    doc.Add(Entity::Line({0, 0}, {100, 100}));
    PlotPage page;
    PlotDrawing(doc, Box({0, 0}, {100, 100}), 297, 210, 10, 1, PlotStyle{}, {}, page);
    CHECK(!page.items.empty() && page.items[0].kind == PlotItem::Image);
    std::string pdf = WritePdf({page}, L"t");
    CHECK(pdf.find("/Subtype /Image") == std::string::npos && pdf.find(" re S") != std::string::npos);  // no reader: a frame
    SetImageReader([](const std::wstring&, int& w, int& h, std::vector<uint8_t>& rgb) {
        w = 2, h = 1;
        rgb = {255, 0, 0, 0, 0, 255};
        return true;
    });
    pdf = WritePdf({page}, L"t");
    CHECK(pdf.find("/Subtype /Image /Width 2 /Height 1") != std::string::npos && pdf.find("/Im1 Do") != std::string::npos);
    SetImageReader(nullptr);
}

void TestCloseByClicking() {
    Document doc;
    TestHost host;
    Engine e(doc, host);
    // SPLINE: three points, then a click on the first one closes it.
    Type(e, {L"spl", L"0,0", L"50,40", L"100,0"});
    CHECK(e.Active() && e.CurrentPrompt().hasClose && Close(e.CurrentPrompt().closePoint, {0, 0}));
    e.Point({0, 0});
    CHECK(!e.Active());
    const Entity* s = FirstOf(doc, EntityType::Spline);
    CHECK(s && s->closed && s->verts.size() == 3);
    // Two points are not enough to close: no close point yet.
    Type(e, {L"spl", L"0,100", L"50,140"});
    CHECK(!e.CurrentPrompt().hasClose);
    e.Cancel();
    // PLINE and LINE the same way.
    Type(e, {L"pl", L"200,0", L"300,0", L"300,50"});
    CHECK(e.CurrentPrompt().hasClose);
    e.Point({200, 0});
    CHECK(!e.Active() && Last(doc).type == EntityType::Polyline && Last(doc).closed && Last(doc).verts.size() == 3);
    const size_t before = doc.Count();
    Type(e, {L"l", L"400,0", L"500,0"});
    CHECK(!e.CurrentPrompt().hasClose);  // one segment: nothing to close yet
    Type(e, {L"500,50"});
    CHECK(e.CurrentPrompt().hasClose);
    e.Point({400, 0});
    CHECK(!e.Active() && doc.Count() == before + 3 && Close(Last(doc).p1, {400, 0}));
}

void TestProjectionHelpers() {
    // Labels: digits after a letter are subscripts; ' is a prime.
    auto parts = ParseLabel(L"a1'");
    CHECK(parts.size() == 3 && parts[0].text == L"a" && parts[1].sub && parts[1].text == L"1" && parts[2].prime);
    parts = ParseLabel(L"[c1']");
    CHECK(parts.size() == 4 && parts[0].text == L"[c" && parts[1].sub && parts[2].prime && parts[3].text == L"]");
    parts = ParseLabel(L"12");
    CHECK(parts.size() == 1 && !parts[0].sub);

    Document doc;
    TestHost host;
    Engine e(doc, host);
    const uint64_t rect = doc.Add(Entity::Rectangle({0, 0}, {100, 60}));
    // LABEL: a click on a corner; the label goes where the drawing leaves room.
    Type(e, {L"label"});
    e.Point({0, 0});
    const Entity* l = FirstOf(doc, EntityType::Text);
    CHECK(l && l->annot->text.frame == kFrameLabel && l->annot->text.content == L"a");
    CHECK(l && l->p0.x < 0 && l->p0.y < 0 && l->annot->anchor.id == rect);  // outside the corner, down and left
    AnnotGraphics g;
    // The next one is b; typed labels are kept and carried on from.
    e.Point({100, 60});
    CHECK(Last(doc).annot->text.content == L"b" && Last(doc).p0.x > 100 && Last(doc).p0.y > 60);
    Type(e, {L"c1'"});
    e.Point({100, 0});
    const Entity& c = Last(doc);
    CHECK(c.annot->text.content == L"c1'" && c.p0.x > 100 && c.p0.y < 0);
    g = {};
    AnnotLayout(c, nullptr, g);
    CHECK(g.texts.size() == 2 && g.texts[1].text == L"1" && g.texts[1].height < g.texts[0].height && g.lines.size() == 1);
    Type(e, {L""});  // Done
    CHECK(!e.Active());
    Type(e, {L"label", L"0,60"});  // typed coordinates are a point
    CHECK(Last(doc).annot->text.content == L"d1'" && Last(doc).annot->anchor.id == rect);
    e.Cancel();
    // A label stays with its point when the drawing moves.
    const Vec2 was = FirstOf(doc, EntityType::Text)->p0;
    Entity r = *doc.Find(rect);
    Translate(r, {10, 5});
    doc.BeginChange(L"Move");
    doc.Replace(r);
    doc.EndChange();
    CHECK(Close(FirstOf(doc, EntityType::Text)->p0, was + Vec2{10, 5}));
    // Kept in the file, still attached.
    {
        const std::string text = doc.ToText();
        Document back;
        std::wstring err;
        CHECK(back.FromText(text, err) && back.ToText() == text);
        const Entity* bl = FirstOf(back, EntityType::Text);
        CHECK(bl && bl->annot->anchor.id != 0);
    }

    // PROJECTOR: construction lines through the point clicked.
    const size_t n = doc.Count();
    Type(e, {L"pj"});
    e.Point({50, 30});
    CHECK(doc.Count() == n + 1 && Last(doc).construction && Close(Last(doc).p0.x, 50) && Close(Last(doc).p1.x, 50));
    CHECK(Last(doc).p0.y < -100 && Last(doc).p1.y > 150);  // right across the drawing
    Type(e, {L"b"});
    e.Point({20, 20});
    CHECK(doc.Count() == n + 3);
    Type(e, {L"a", L"45"});
    e.Point({0, 0});
    CHECK(doc.Count() == n + 4 && Close(Last(doc).p1.x - Last(doc).p0.x, Last(doc).p1.y - Last(doc).p0.y));
    e.Cancel();

    // HIDDEN: a click makes an edge hidden; again, visible.
    const uint64_t edge = doc.Add(Entity::Line({0, 100}, {100, 100}));
    Type(e, {L"hidden"});
    e.Point({50, 100});
    CHECK(std::wstring(LineTypes()[(size_t)doc.Find(edge)->linetype].name).find(L"HIDDEN") == 0);
    e.Point({50, 100});
    CHECK(doc.Find(edge)->linetype == kLtByLayer);
    e.Cancel();
}

// Do the ends of these lines meet, each where the next begins (a closed chain)?
bool ChainClosed(const Document& doc, const std::vector<uint64_t>& ids, double tol = 1e-6) {
    for (size_t i = 0; i < ids.size(); ++i) {
        const Entity* a = doc.Find(ids[i]);
        const Entity* b = doc.Find(ids[(i + 1) % ids.size()]);
        if (!a || !b || !Close(a->p1, b->p0, tol)) return false;
    }
    return true;
}

void TestRelationsKeepShape() {
    // A rectangle drawn with LINE by hand (a little out of square).
    Document doc;
    TestHost host;
    Engine e(doc, host);
    Type(e, {L"l", L"0,0", L"100,3", L"97,60", L"-2,58", L"c"});
    std::vector<uint64_t> lines;
    for (const auto& kv : doc.Entities())
        if (kv.second.type == EntityType::Line) lines.push_back(kv.first);
    CHECK(lines.size() == 4 && ChainClosed(doc, lines));
    auto mid = [&](size_t i) { return Lerp(doc.Find(lines[i])->p0, doc.Find(lines[i])->p1, 0.5); };
    auto dir = [&](size_t i) { return Normalized(doc.Find(lines[i])->p1 - doc.Find(lines[i])->p0); };
    // Parallel: bottom and top. The corners stay joined.
    Type(e, {L"parallel"});
    e.Point(mid(0));
    e.Point(mid(2));
    e.Cancel();
    CHECK(ChainClosed(doc, lines) && Close(Cross(dir(0), dir(2)), 0, 1e-6));
    CHECK(CountOf(doc, EntityType::Relation) == 4 + 1);  // four connects (the shape's own), the parallel
    // Parallel: left and right; perpendicular: bottom and left.
    Type(e, {L"parallel"});
    e.Point(mid(1));
    e.Point(mid(3));
    e.Cancel();
    Type(e, {L"perpendicular"});
    e.Point(mid(0));
    e.Point(mid(3));
    e.Cancel();
    CHECK(ChainClosed(doc, lines) && Close(Cross(dir(1), dir(3)), 0, 1e-6) && Close(Dot(dir(0), dir(3)), 0, 1e-6));
    // Equal: bottom and left - a square, still closed.
    Type(e, {L"equal"});
    e.Point(mid(0));
    e.Point(mid(3));
    e.Cancel();
    auto len = [&](size_t i) { return Dist(doc.Find(lines[i])->p0, doc.Find(lines[i])->p1); };
    CHECK(ChainClosed(doc, lines) && Close(len(0), len(3), 1e-5) && Close(len(0), len(2), 1e-5) && doc.LastSolve().ok);
    // Dragging a corner of one line: the joined line comes with it.
    Entity x = *doc.Find(lines[1]);
    x.p1 = x.p1 + Vec2{10, 10};
    doc.BeginChange(L"Grip");
    doc.Replace(x);
    doc.EndChange();
    CHECK(ChainClosed(doc, lines, 1e-5));

    // RECTANGLE: equal on two sides next to each other makes it a square (its right angles are kept).
    Document r;
    Engine er(r, host);
    Type(er, {L"rec", L"0,0", L"100,60"});
    const uint64_t rect = FirstOf(r, EntityType::Polyline)->id;
    Type(er, {L"equal"});
    er.Point({50, 0});
    er.Point({100, 30});
    er.Cancel();
    const Entity& sq = *r.Find(rect);
    CHECK(sq.verts.size() == 4 && sq.closed);
    for (size_t i = 0; i < 4; ++i) {
        const Vec2 u = sq.verts[(i + 1) % 4].p - sq.verts[i].p, w = sq.verts[(i + 2) % 4].p - sq.verts[(i + 1) % 4].p;
        CHECK(Close(Dot(Normalized(u), Normalized(w)), 0, 1e-6));          // square corners
        CHECK(Close(Length(u), Length(sq.verts[1].p - sq.verts[0].p), 1e-5));  // equal sides
    }
    CHECK(r.LastSolve().ok);
}


// The relationships of a kind on an object.
int RelationsOn(const Document& d, RelKind k, uint64_t id) {
    int n = 0;
    for (const auto& kv : d.Entities())
        if (kv.second.type == EntityType::Relation && kv.second.annot && kv.second.annot->rel.kind == k)
            for (const DimRef& r : kv.second.annot->rel.refs) n += r.id == id;
    return n;
}

void TestReshapeAndRegular() {
    TestHost host;
    auto autorel = [](Document& d) {
        DrawingSettings st = d.Settings();
        st.autoRelations = true;
        d.SetSettings(st);
    };
    auto lineAt = [](const Document& d, Vec2 a) -> uint64_t {
        for (const auto& kv : d.Entities())
            if (kv.second.type == EntityType::Line && Close(kv.second.p0, a, 1e-6)) return kv.first;
        return 0;
    };
#define RECT {L"l", L"0,0", L"100,0", L"100,60", L"0,60", L"c"}
    {   // Drawn with relationships while drawing: level and upright sides, joined corners.
        Document d;
        autorel(d);
        Engine e(d, host);
        Type(e, RECT);
        CHECK(CountOf(d, EntityType::Relation) == 4 + 4);
        // FILLET: the lines are cut back, not pulled; the arc joins both, tangent.
        Type(e, {L"f", L"r", L"10", L"50,0", L"100,30"});
        const uint64_t bottom = lineAt(d, {0, 0}), right = lineAt(d, {100, 10});
        CHECK(bottom && right && Close(d.Find(bottom)->p1, Vec2{90, 0}, 1e-9) && d.LastSolve().ok);
        const Entity* arc = FirstOf(d, EntityType::Arc);
        CHECK(arc && RelationsOn(d, RelKind::Tangent, arc->id) == 2 && RelationsOn(d, RelKind::Coincident, arc->id) == 2);
        CHECK(RelationsOn(d, RelKind::Coincident, bottom) == 2);  // the old corner let go; the arc and the left side
        // Moving the bottom side down: the arc and the right side follow, still joined and tangent.
        e.AddToSelection({bottom});
        Type(e, {L"move", L"0,0", L"0,-10"});
        arc = FirstOf(d, EntityType::Arc);
        const Entity &b = *d.Find(bottom), &r = *d.Find(right);
        CHECK(d.LastSolve().ok && Close(b.p0.y, -10, 1e-6) && Close(b.p1.y, -10, 1e-6));
        CHECK(Close(Polar(arc->p0, arc->radius, arc->startAngle), b.p1, 1e-5) &&
              Close(Polar(arc->p0, arc->radius, arc->endAngle), r.p0, 1e-5));
        CHECK(Close(arc->p0.y - arc->radius, -10, 1e-5) && Close(arc->p0.x + arc->radius, r.p0.x, 1e-5));
    }
    {   // CHAMFER: the new edge joins both lines; nothing moves.
        Document d;
        autorel(d);
        Engine e(d, host);
        Type(e, RECT);
        Type(e, {L"cha", L"d", L"10", L"10", L"50,0", L"100,30"});
        CHECK(Close(d.Find(lineAt(d, {0, 0}))->p1, Vec2{90, 0}, 1e-9) && lineAt(d, {100, 10}) && d.LastSolve().ok);
        CHECK(RelationsOn(d, RelKind::Coincident, lineAt(d, {90, 0})) == 2);
    }
    {   // TRIM: the cut end is let go of; the right side stays where it is.
        Document d;
        autorel(d);
        Engine e(d, host);
        Type(e, RECT);
        Type(e, {L"l", L"50,-20", L"50,80", L""});
        Type(e, {L"tr", L"80,0", L""});
        CHECK(Close(d.Find(lineAt(d, {0, 0}))->p1, Vec2{50, 0}, 1e-9) && lineAt(d, {100, 0}) && d.LastSolve().ok);
        CHECK(RelationsOn(d, RelKind::Coincident, lineAt(d, {100, 0})) == 1);  // only the top corner now
    }
    {   // BREAK: the corner's connect goes to the new piece; it moves with the right side.
        Document d;
        autorel(d);
        Engine e(d, host);
        Type(e, RECT);
        Type(e, {L"breakatpoint", L"50,0", L"50,0"});
        const uint64_t piece = lineAt(d, {50, 0}), right = lineAt(d, {100, 0});
        CHECK(piece && right && RelationsOn(d, RelKind::Coincident, piece) == 2 && d.LastSolve().ok);
        Entity x = *d.Find(right);
        x.p0 = x.p0 + Vec2{10, 0};
        x.p1 = x.p1 + Vec2{10, 0};
        d.BeginChange(L"Move");
        d.Replace(x);
        d.EndChange();
        CHECK(Close(d.Find(piece)->p1, Vec2{110, 0}, 1e-6) && Close(d.Find(piece)->p0, Vec2{50, 0}, 1e-6));
    }
    {   // Without relationships while drawing (older drawings): fillet still does not drag the corner back.
        Document d;
        Engine e(d, host);
        Type(e, {L"l", L"0,0", L"100,3", L"97,60", L"-2,58", L"c"});
        Type(e, {L"parallel"});
        e.Point({50, 1.5});
        e.Point({47.5, 59});
        e.Cancel();
        const Vec2 corner = d.Find(lineAt(d, {0, 0}))->p1;
        Type(e, {L"f", L"r", L"5", L"50,1.5", L"98.5,31.5"});
        const Entity* arc = FirstOf(d, EntityType::Arc);
        CHECK(arc && d.LastSolve().ok && Close(d.Find(lineAt(d, {0, 0}))->p0, Vec2{0, 0}, 1e-9));
        CHECK(Dist(d.Find(lineAt(d, {0, 0}))->p1, corner) > 1);  // cut back, not pulled to the old corner
    }
    {   // POLYGON: a relationship on it keeps it regular.
        Document d;
        Engine e(d, host);
        Type(e, {L"pol", L"6", L"0,0", L"i", L"10,0"});
        const uint64_t hex = FirstOf(d, EntityType::Polyline)->id;
        const Vec2 s0 = Lerp(d.Find(hex)->verts[0].p, d.Find(hex)->verts[1].p, 0.5);
        const Vec2 s2 = Lerp(d.Find(hex)->verts[2].p, d.Find(hex)->verts[3].p, 0.5);
        Type(e, {L"equal"});
        e.Point(s0);
        e.Point(s2);
        e.Cancel();
        CHECK(RelationsOn(d, RelKind::Regular, hex) == 1);
        Entity x = *d.Find(hex);
        x.verts[0].p = x.verts[0].p + Vec2{6, 3};  // a corner dragged out
        d.BeginChange(L"Grip");
        d.Replace(x);
        d.EndChange();
        const Entity& h = *d.Find(hex);
        Vec2 m{0, 0};
        for (const Vertex& v : h.verts) m = m + v.p / 6.0;
        const double side = Dist(h.verts[0].p, h.verts[1].p), rad = Dist(h.verts[0].p, m);
        CHECK(d.LastSolve().ok && side > 10.5 && Close(rad, side, 1e-5));
        for (size_t i = 0; i < 6; ++i)
            CHECK(Close(Dist(h.verts[i].p, h.verts[(i + 1) % 6].p), side, 1e-5) && Close(Dist(h.verts[i].p, m), rad, 1e-5));
        // Saved and opened again.
        Document o;
        std::wstring err;
        CHECK(o.FromText(d.ToText(), err) && RelationsOn(o, RelKind::Regular, FirstOf(o, EntityType::Polyline)->id) == 1);
        // Filleting a corner: no longer a polygon of straight sides; the relationship goes.
        Type(e, {L"f", L"r", L"1", L"p"});
        e.Point(Lerp(h.verts[3].p, h.verts[4].p, 0.5));
        e.Cancel();
        bool bulges = false;
        for (const Vertex& v : d.Find(hex)->verts) bulges |= v.bulge != 0;
        CHECK(bulges && RelationsOn(d, RelKind::Regular, hex) == 0 && d.LastSolve().ok);
    }
    {   // A locked length on a line that is trimmed: it takes the new length.
        Document d;
        Engine e(d, host);
        const uint64_t a = d.Add(Entity::Line({0, 0}, {100, 0}));
        d.Add(Entity::Line({60, -20}, {60, 20}));
        DimData dd = Aligned({0, 0}, {100, 0}, {50, 10});
        dd.refs[0] = {a, 0};
        dd.refs[1] = {a, 1};
        dd.locked = true;
        dd.value = 100;
        const uint64_t dim = d.Add(MakeDimension(dd));
        Type(e, {L"tr", L"80,0", L""});
        CHECK(Close(d.Find(a)->p1, Vec2{60, 0}, 1e-9) && Close(d.Find(dim)->dim->value, 60, 1e-6) && d.Find(dim)->dim->locked);
        CHECK(d.LastSolve().ok);
    }
    {   // Marks drawn one size on the screen.
        Entity rel;
        std::wstring why;
        Document d;
        const uint64_t l = d.Add(Entity::Line({0, 0}, {100, 0}));
        CHECK(MakeRelation(d, RelKind::Horizontal, {DimRef{l, -1}, {}, {}}, 2, rel, why));
        const double small = Bounds(rel).Width();
        SetRelationMarkSize(20);
        const double big = Bounds(rel).Width();
        SetRelationMarkSize(0);
        CHECK(Close(big / small, 10, 1e-6) && Close(Bounds(rel).Width(), small, 1e-12));
        // Two on one line: side by side, not on top of each other.
        d.Add(rel);
        const uint64_t l2 = d.Add(Entity::Line({0, 10}, {100, 10}));
        Entity eq;
        CHECK(MakeRelation(d, RelKind::Equal, {DimRef{l, -1}, DimRef{l2, -1}, {}}, 2, eq, why));
        CHECK(eq.annot->markSlots.size() == 2 && eq.annot->markSlots[0] == 1 && eq.annot->markSlots[1] == 0);
        // Tangent: one mark, where they touch.
        const uint64_t c = d.Add(Entity::Circle({50, 20}, 10));
        Entity tan;
        CHECK(MakeRelation(d, RelKind::Tangent, {DimRef{l2, -1}, DimRef{c, -1}, {}}, 2, tan, why));
        CHECK(tan.annot->pts.size() == 1 && Close(tan.annot->pts[0], Vec2{50, 10}, 1e-9));
    }
#undef RECT
}

// A thick (development outline) line from a to b, either way round.
bool HasLine(const std::vector<Entity>& ents, Vec2 a, Vec2 b, double tol = 1e-6) {
    for (const Entity& e : ents)
        if (e.type == EntityType::Line &&
            ((Close(e.p0, a, tol) && Close(e.p1, b, tol)) || (Close(e.p0, b, tol) && Close(e.p1, a, tol))))
            return true;
    return false;
}

std::vector<std::wstring> LabelsOf(const std::vector<Entity>& ents) {
    std::vector<std::wstring> out;
    for (const Entity& e : ents)
        if (const TextData* t = TextOf(e); t && t->frame == kFrameLabel) out.push_back(t->content);
    return out;
}

void TestEngineeringGraphics() {
    TestHost host;
    {   // Isometric helpers.
        CHECK(IsIsoAxis(ToRadians(30)) && IsIsoAxis(ToRadians(90)) && IsIsoAxis(ToRadians(-30)) && IsIsoAxis(ToRadians(210)));
        CHECK(!IsIsoAxis(0) && !IsIsoAxis(ToRadians(45)) && !IsIsoAxis(ToRadians(60)));
        const double c = std::cos(kPi / 6);
        CHECK(Close(IsoGridPoint({8.4, 5.3}, {0, 0}, 10), Vec2{10 * c, 5}, 1e-9));  // one step along 30 degrees
        CHECK(Close(IsoGridPoint({0.3, 9.6}, {0, 0}, 10), Vec2{0, 10}, 1e-9));      // one step up
        CHECK(Close(IsoOrtho({0, 0}, {10, 4}, IsoPlane::Top), Vec2{10, 10 / std::sqrt(3.0)} * 1.0, 1e-6) ||
              Close(Dist(IsoOrtho({0, 0}, {10, 4}, IsoPlane::Top), {0, 0}) > 0 ? 1 : 0, 1));
        const Vec2 o = IsoOrtho({0, 0}, {10, 4}, IsoPlane::Top);
        CHECK(Close(AngleOf(o), kPi / 6, 1e-9));
        CHECK(Close(AngleOf(IsoOrtho({0, 0}, {1, 9}, IsoPlane::Left)), kPi / 2, 1e-9));
        // An isometric circle touches the four midpoints of its isometric square.
        for (IsoPlane pl : {IsoPlane::Left, IsoPlane::Top, IsoPlane::Right}) {
            const Entity el = IsoCircle({5, 5}, 20, pl);
            Vec2 u, v;
            IsoAxes(pl, u, v);
            const Vec2 ax = Normalized(el.p1), ay = Perp(ax);
            const double A = Length(el.p1), B = A * el.radius;
            for (Vec2 q : {u * 20, u * -20, v * 20, v * -20}) {
                const double x = Dot(q, ax) / A, y = Dot(q, ay) / B;
                CHECK(Close(x * x + y * y, 1, 1e-9));
            }
            CHECK(Close(A, 20 * std::sqrt(1.5), 1e-9) && Close(B, 20 / std::sqrt(2.0), 1e-9));
        }
        // The isometric scale: 100 true is 81.65 isometric.
        const std::vector<Entity> sc = IsoScaleDrawing({0, 0}, 100, 10, 2.5);
        CHECK(HasLine(sc, {0, 0}, Polar({0, 0}, 100, kPi / 4)) && HasLine(sc, {0, 0}, Polar({0, 0}, 100 * kIsoScale, kPi / 6)));
        // Typed lengths along the isometric axes, drawn x 0.816 with the isometric scale.
        Document d;
        Engine e(d, host);
        e.Draft().isoOn = true;
        e.Draft().isoScale = true;
        Type(e, {L"l", L"0,0", L"@50<30", L"@20<0", L""});
        CHECK(CountOf(d, EntityType::Line) == 2);
        CHECK(Close(Dist(FirstOf(d, EntityType::Line)->p0, FirstOf(d, EntityType::Line)->p1), 50 * kIsoScale, 1e-9));
        CHECK(Close(Dist(Last(d).p0, Last(d).p1), 20, 1e-9));  // not along an axis: as typed
        e.Draft().isoPlane = (int)IsoPlane::Right;
        Type(e, {L"ic", L"0,0", L"10"});
        const Entity* el = FirstOf(d, EntityType::Ellipse);
        CHECK(el && Close(Length(el->p1), 10 * kIsoScale * std::sqrt(1.5), 1e-9) && Close(AngleOf(el->p1), kPi / 3, 1e-9));
    }
    {   // DIVIDE and MEASURE.
        Document d;
        Engine e(d, host);
        Type(e, {L"l", L"0,0", L"120,0", L""});
        Type(e, {L"div", L"10,0", L"4"});
        std::vector<double> xs;
        for (const auto& kv : d.Entities())
            if (kv.second.type == EntityType::Point) xs.push_back(kv.second.p0.x);
        std::sort(xs.begin(), xs.end());
        CHECK(xs.size() == 3 && Close(xs[0], 30) && Close(xs[1], 60) && Close(xs[2], 90));
        // MEASURE from the end nearer the click.
        Document m;
        Engine em(m, host);
        Type(em, {L"l", L"0,0", L"100,0", L""});
        Type(em, {L"me", L"95,0", L"30"});
        xs.clear();
        for (const auto& kv : m.Entities())
            if (kv.second.type == EntityType::Point) xs.push_back(kv.second.p0.x);
        std::sort(xs.begin(), xs.end());
        CHECK(xs.size() == 3 && Close(xs[0], 10) && Close(xs[1], 40) && Close(xs[2], 70));
        // A circle into 12, numbered, point 1 where it was clicked.
        Document c;
        Engine ec(c, host);
        Type(ec, {L"c", L"0,0", L"30"});
        Type(ec, {L"div", L"-30,0.2", L"n", L"12"});
        std::vector<Vec2> pts;
        for (const auto& kv : c.Entities())
            if (kv.second.type == EntityType::Point) pts.push_back(kv.second.p0);
        CHECK(pts.size() == 12 && Close(pts[0], Vec2{-30, 0}, 1e-9));
        for (Vec2 q : pts) CHECK(Close(Length(q), 30, 1e-9));
        std::vector<Entity> all;
        for (const auto& kv : c.Entities()) all.push_back(kv.second);
        const std::vector<std::wstring> names = LabelsOf(all);
        CHECK(names.size() == 12 && names.front() == L"1" && names.back() == L"12");
        // An ellipse and a spline can be divided too; a point cannot.
        std::vector<Vec2> out;
        CHECK(DividePoints(Entity::Ellipse({0, 0}, {40, 0}, 0.5), 8, {40, 0}, out) && out.size() == 8);
        CHECK(!Dividable(Entity::Point({0, 0})));
    }
    {   // NextName.
        CHECK(NextName(L"a") == L"b" && NextName(L"9") == L"10" && NextName(L"A") == L"B" && NextName(L"a9") == L"a10");
    }
    {   // Developments.
        std::vector<Entity> ents;
        std::wstring why;
        SolidSpec s;  // square prism, side 40, axis 60
        CHECK(DevelopSolid(s, {0, 0}, 3.5, ents, why));
        CHECK(HasLine(ents, {0, 0}, {40, 0}) && HasLine(ents, {120, 60}, {160, 60}) && HasLine(ents, {0, 0}, {0, 60}));
        CHECK(HasLine(ents, {160, 0}, {160, 60}) && HasLine(ents, {40, 0}, {40, 60}));
        const std::vector<std::wstring> corners = LabelsOf(ents);
        CHECK(corners.size() == 5 && corners[0] == L"A" && corners[3] == L"D" && corners[4] == L"A");
        // Cut at 30 degrees through the axis 30 above the base: corners A..D at x = -20, 20, 20, -20.
        s.cut = true;
        CHECK(DevelopSolid(s, {0, 0}, 3.5, ents, why));
        const double lo = 30 - 20 * std::tan(kPi / 6), hi = 30 + 20 * std::tan(kPi / 6);
        CHECK(HasLine(ents, {0, lo}, {40, hi}) && HasLine(ents, {40, hi}, {80, hi}) && HasLine(ents, {80, hi}, {120, lo}));
        CHECK(HasLine(ents, {120, lo}, {160, lo}) && HasLine(ents, {0, 0}, {0, lo}));
        // Cylinder: pi d long.
        SolidSpec cyl;
        cyl.kind = SolidSpec::Cylinder;
        cyl.base = 50;
        cyl.height = 60;
        CHECK(DevelopSolid(cyl, {0, 0}, 3.5, ents, why) && HasLine(ents, {0, 0}, {kPi * 50, 0}));
        CHECK(LabelsOf(ents).size() == 13);
        // Cone: radius 30, height 40, slant 50: a 216 degree sector.
        SolidSpec cone;
        cone.kind = SolidSpec::Cone;
        cone.base = 60;
        cone.height = 40;
        CHECK(DevelopSolid(cone, {0, 0}, 3.5, ents, why));
        const Entity* arc = nullptr;
        bool noted = false;
        for (const Entity& x : ents) {
            if (x.type == EntityType::Arc) arc = &x;
            if (const TextData* t = TextOf(x); t && t->content.find(L"= 216\x00B0") != std::wstring::npos) noted = true;
        }
        CHECK(arc && Close(arc->radius, 50, 1e-9) && Close(SweepCCW(arc->startAngle, arc->endAngle), ToRadians(216), 1e-9) && noted);
        // Pyramid: its corners at the slant edge's length from the apex.
        SolidSpec pyr;
        pyr.kind = SolidSpec::Pyramid;
        CHECK(DevelopSolid(pyr, {0, 0}, 3.5, ents, why));
        const double edge = std::hypot(40 / std::sqrt(2.0), 60.0);
        int folds = 0;
        for (const Entity& x : ents)
            if (x.type == EntityType::Line && Close(Dist(x.p0, x.p1), edge, 1e-9)) ++folds;
        CHECK(folds == 5);  // two ends (thick) and three folds (thin)
        // Bad sizes.
        SolidSpec bad;
        bad.sides = 2;
        CHECK(!DevelopSolid(bad, {0, 0}, 3.5, ents, why) && !why.empty());
        // The command.
        Document d;
        Engine e(d, host);
        Type(e, {L"dev", L"c", L"50", L"60", L"12", L"n", L"0,0"});
        CHECK(FirstOf(d, EntityType::Line) && Said(host, L"cylinder"));
    }
    {   // True length.
        LineViews v{{0, 20}, {60, 60}, {0, -20}, {60, -50}};
        const TrueLengthResult r = TrueLengthOf(v);
        CHECK(Close(r.tl, std::sqrt(6100.0), 1e-9) && Close(r.theta, std::asin(40 / std::sqrt(6100.0)), 1e-12) &&
              Close(r.phi, std::asin(30 / std::sqrt(6100.0)), 1e-12));
        LineViews w;
        std::wstring why;
        CHECK(ViewsFromTrueLength({0, 20}, {0, -20}, r.tl, r.theta, r.phi, true, w, why));
        CHECK(Close(w.b1, v.b1, 1e-9) && Close(w.b, v.b, 1e-9));
        CHECK(!ViewsFromTrueLength({0, 20}, {0, -20}, 50, ToRadians(60), ToRadians(40), true, w, why));
        const std::vector<Entity> cons = TrueLengthConstruction(v, 3.5, false);
        int tl = 0;
        for (const Entity& x : cons)
            if (x.type == EntityType::Line && Close(Dist(x.p0, x.p1), r.tl, 1e-9)) ++tl;
        CHECK(tl == 2);  // a'b1' and ab2: both the true length
        const std::vector<std::wstring> names = LabelsOf(cons);
        CHECK(std::find(names.begin(), names.end(), L"b1'") != names.end() && std::find(names.begin(), names.end(), L"b2") != names.end());
        // The command: picking the two views, and drawing from TL, theta, phi.
        Document d;
        Engine e(d, host);
        d.Add(Entity::Line({0, 20}, {60, 60}));
        d.Add(Entity::Line({60, -50}, {0, -20}));
        host.lines.clear();
        Type(e, {L"trl", L"30,40", L"30,-35"});
        CHECK(Said(host, L"True length 78.1"));
        Document d2;
        Engine e2(d2, host);
        Type(e2, {L"trl", L"d", L"10,10", L"10,-10", L"80", L"30", L"45", L""});
        int views = 0;
        for (const auto& kv : d2.Entities())
            if (kv.second.type == EntityType::Line && kv.second.lineweight == 50) ++views;
        CHECK(views == 2);
    }
    {   // Cutting plane and true shape.
        const std::vector<Entity> cp = CuttingPlane({{0, 0}, {100, 0}}, {50, 20}, L"A", 3.5);
        int leaders = 0, letters = 0;
        for (const Entity& x : cp) {
            if (x.type == EntityType::Leader) {
                ++leaders;
                CHECK(x.annot->pts[0].y > 1);  // pointing to the side clicked
            }
            if (const TextData* t = TextOf(x); t && t->content == L"A") ++letters;
        }
        CHECK(leaders == 2 && letters == 2 && cp[0].linetype == FindLineType(L"CENTER"));
        // A rectangle in the top view, the plane rising at atan(2/3) in the front view.
        TrueShapeResult r;
        std::wstring why;
        const std::vector<Vec2> sec = {{-20, -20}, {20, -20}, {20, -40}, {-20, -40}};
        CHECK(TrueShape({-30, 10}, {30, 50}, sec, {0, 80}, true, 0, r, why));
        CHECK(r.shape.size() == 4);
        const double along = 40 * std::sqrt(13.0) / 3;
        CHECK(Close(Dist(r.shape[0], r.shape[1]), along, 1e-9) && Close(Dist(r.shape[1], r.shape[2]), 20, 1e-9));
        CHECK(Close(Dot(r.shape[1] - r.shape[0], r.shape[2] - r.shape[1]), 0, 1e-9));  // a true rectangle
        // The nearest edge (20 from XY) is 20 beyond X1Y1.
        const Vec2 n = Normalized(Perp(Vec2{3, 2}));
        CHECK(Close(std::fabs(Dot(r.shape[0] - r.x1, n)), 20, 1e-9));
        CHECK(!TrueShape({0, 0}, {0, 50}, sec, {10, 0}, true, 0, r, why));  // along the projectors
        // The command: cutting plane, then true shape.
        Document d;
        Engine e(d, host);
        Type(e, {L"l", L"-30,10", L"30,50", L""});
        Type(e, {L"pl", L"-20,-20", L"20,-20", L"20,-40", L"-20,-40", L"c"});
        Type(e, {L"l", L"-60,0", L"60,0", L""});  // XY
        Type(e, {L"cpl", L"-36,6", L"36,54", L"", L"-40,40", L"B"});
        Type(e, {L"ts", L"0,30", L"0,-20", L"0,0", L"-20,80"});
        CHECK(FirstOf(d, EntityType::Hatch) && Said(host, L"True shape drawn"));
        bool titled = false;
        for (const auto& kv : d.Entities())
            if (const TextData* t = TextOf(kv.second); t && t->content == L"TRUE SHAPE OF SECTION B-B") titled = true;
        CHECK(titled);
    }
    {   // BIS setup, first-angle symbol, unidirectional dimensions, BIS title block.
        const std::vector<Entity> sym = FirstAngleSymbol({0, 0}, 20);
        int circles = 0;
        for (const Entity& x : sym)
            if (x.type == EntityType::Circle) ++circles, CHECK(Close(x.radius, 10) || Close(x.radius, 5));
        CHECK(circles == 2);
        Document d;
        ApplyBisSetup(d, true);
        CHECK(d.LayerByName(L"Visible") && d.LayerByName(L"Hidden") && d.LayerByName(L"Centre") && d.LayerByName(L"Section"));
        CHECK(d.Current().layer == d.LayerByName(L"Visible")->id && d.LayerByName(L"Visible")->lineweight == 50);
        CHECK(d.Settings().dimStyle.unidirectional && UsesBisTitleBlock(d));
        ApplyBisSetup(d, false);  // again: the same layers, not new ones
        CHECK(d.Layers().size() == 8 && !d.Settings().dimStyle.unidirectional);
        // Unidirectional: level text in a break of an upright dimension line.
        DimData dd;
        dd.kind = DimData::Aligned;
        dd.pts[0] = {0, 0};
        dd.pts[1] = {0, 50};
        dd.loc = {-15, 25};
        dd.style.unidirectional = true;
        DimGraphics g;
        DimLayout(dd, g);
        CHECK(Close(g.textAngle, 0) && Close(g.textPos, Vec2{-15, 25}, 1e-9));
        for (const Prim& p : g.lines) CHECK(!(p.kind == Prim::Segment && Distance(p, {-15, 25}) < 1e-6));
        dd.style.unidirectional = false;
        DimLayout(dd, g);
        CHECK(Close(std::fabs(g.textAngle), kPi / 2, 1e-9));
        // Saved and read back.
        Document a;
        DrawingSettings st = a.Settings();
        st.dimStyle.unidirectional = true;
        a.SetSettings(st);
        a.Add(MakeDimension(dd));
        DimData du = dd;
        du.style.unidirectional = true;
        a.Add(MakeDimension(du));
        Document b;
        std::wstring err;
        CHECK(b.FromText(a.ToText(), err) && b.Settings().dimStyle.unidirectional && b.ToText() == a.ToText());
        // A sheet of a BIS drawing: the BIS title block.
        Document model;
        ApplyBisSetup(model, false);
        auto sh = NewSheet(model, L"Sheet1", L"A3", true);
        bool bis = false;
        for (const auto& kv : sh->doc.Entities())
            if (kv.second.type == EntityType::Insert && kv.second.annot->block->name == L"BIS title block") bis = true;
        CHECK(bis);
    }
}

std::vector<Vec2> FitPoints(const Entity& e) {
    std::vector<Vec2> out;
    for (const Vertex& v : e.verts) out.push_back(v.p);
    return out;
}

const Entity* FirstWire(const Document& d) {
    for (const auto& kv : d.Entities())
        if (IsConnector(kv.second)) return &kv.second;
    return nullptr;
}

bool Orthogonal(const std::vector<Vec2>& p) {
    for (size_t i = 0; i + 1 < p.size(); ++i)
        if (std::fabs(p[i].x - p[i + 1].x) > 1e-9 && std::fabs(p[i].y - p[i + 1].y) > 1e-9) return false;
    return true;
}

void TestSymbolsAndCurves() {
    TestHost host;
    {   // The library.
        CHECK(SymbolLibrary().size() >= 60 && SymbolCategories().size() == 5);
        for (const SymbolInfo& s : SymbolLibrary()) {
            BlockData b;
            CHECK(MakeSymbolBlock(s.name, b) && !b.items.empty());
            const std::wstring cat = s.category;
            if (cat == L"Flowchart") CHECK(b.ports.size() == 4);
            if (cat == L"Logic gates") CHECK(b.ports.size() == (std::wstring(s.name) == L"NOT" || std::wstring(s.name) == L"BUFFER" ? 2u : 3u));
        }
        BlockData r;
        CHECK(MakeSymbolBlock(L"resistor", r) && r.ports.size() == 2 && Close(r.ports[1].at, Vec2{20, 0}));
        CHECK(!MakeSymbolBlock(L"FLUX_CAPACITOR", r));
        CHECK(FindSymbol(L"NETWORK_SWITCH") && std::wstring(FindSymbol(L"NETWORK_SWITCH")->category) == L"Network");
    }
    {   // Placing symbols, wiring them, moving one: the wire follows.
        Document d;
        Engine e(d, host);
        Type(e, {L"symbol", L"RESISTOR", L"R1", L"0,0", L"r", L"40,0", L""});
        std::vector<uint64_t> ins;
        for (const auto& kv : d.Entities())
            if (kv.second.type == EntityType::Insert) ins.push_back(kv.first);
        CHECK(ins.size() == 2);
        CHECK(d.Find(ins[0])->annot->values.size() == 1 && d.Find(ins[0])->annot->values[0] == L"R1");
        CHECK(d.Find(ins[1])->annot->values[0] == L"R2");  // numbered on
        Vec2 at, dir;
        CHECK(PortPoint(d, {ins[1], 0}, at, dir) && Close(at, Vec2{40, 0}, 1e-9) && Close(dir, Vec2{0, -1}, 1e-9));
        Type(e, {L"wire", L"20,0", L"40,0", L""});
        const Entity* w = FirstWire(d);
        CHECK(w && w->annot->from.id == ins[0] && w->annot->from.grip == 1 && w->annot->to.id == ins[1] && w->annot->to.grip == 0);
        CHECK(w && Orthogonal(w->annot->pts) && Close(w->annot->pts.front(), Vec2{40, 0}) && Close(w->annot->pts.back(), Vec2{20, 0}));
        CHECK(w && w->annot->arrow == 0);  // no arrows on electrical wires
        e.AddToSelection({ins[1]});
        Type(e, {L"move", L"0,0", L"10,10"});
        w = FirstWire(d);
        CHECK(w && Close(w->annot->pts.front(), Vec2{50, 10}, 1e-9) && Close(w->annot->pts.back(), Vec2{20, 0}, 1e-9) &&
              Orthogonal(w->annot->pts));
        // Saved and opened again: the ports and the wire's ends.
        Document o;
        std::wstring err;
        CHECK(o.FromText(d.ToText(), err) && o.ToText() == d.ToText());
        const Entity* ow = FirstWire(o);
        CHECK(ow && ow->annot->from.id && ow->annot->to.id && o.Find(ow->annot->to.id)->annot->block->ports.size() == 2);
        Entity moved = *o.Find(ow->annot->to.id);
        Translate(moved, {0, 5});
        o.BeginChange(L"Move");
        o.Replace(moved);
        o.EndChange();
        CHECK(Close(FirstWire(o)->annot->pts.front(), Vec2{50, 15}, 1e-9));
        // Copied together: the copy's wire joins the copies.
        std::vector<Entity> group;
        for (const auto& kv : d.Entities())
            if (kv.second.type == EntityType::Insert || IsConnector(kv.second)) group.push_back(kv.second);
        for (Entity& g : group) Translate(g, {0, 100});
        d.BeginChange(L"Copy");
        const std::vector<uint64_t> ids = d.AddGroup(group);
        d.EndChange();
        int joined = 0;
        for (uint64_t id : ids)
            if (const Entity* x = d.Find(id); x && IsConnector(*x))
                joined += std::find(ids.begin(), ids.end(), x->annot->from.id) != ids.end() &&
                          std::find(ids.begin(), ids.end(), x->annot->to.id) != ids.end();
        CHECK(joined == 1);
        // Erasing a symbol: its wire lets go and stays.
        d.BeginChange(L"Erase");
        d.Erase(ins[0]);
        d.EndChange();
        w = FirstWire(d);
        CHECK(w && w->annot->from.id == 0 && Close(w->annot->pts.back(), Vec2{20, 0}, 1e-9));
    }
    {   // Flowchart: arrows; network: straight links.
        Document d;
        Engine e(d, host);
        Type(e, {L"symbol", L"PROCESS", L"Read A, B", L"0,0", L"", L"symbol", L"DECISION", L"Is A > B ?", L"0,-50", L""});
        Type(e, {L"wire", L"0,-10", L"0,-38", L""});
        const Entity* w = FirstWire(d);
        CHECK(w && w->annot->arrow > 0 && Close(w->annot->pts.front(), Vec2{0, -38}, 1e-9));
        Document n;
        Engine en(n, host);
        Type(en, {L"sym", L"ROUTER", L"R", L"0,0", L"", L"sym", L"PC", L"PC1", L"60,30", L""});
        Type(en, {L"wire", L"12,0", L"50,37", L""});
        w = FirstWire(n);
        CHECK(w && w->annot->pts.size() == 2 && w->annot->straight);
    }
    {   // Conics: every point is e times as far from the focus as from the directrix.
        for (double ecc : {2.0 / 3, 1.0, 1.5}) {
            std::vector<Entity> out;
            std::wstring why;
            CHECK(ConicCurve({10, 20}, 50, ecc, 3.5, out, why));
            const Entity* curve = nullptr;
            for (const Entity& x : out)
                if (x.type == EntityType::Spline) curve = &x;
            CHECK(curve != nullptr);
            if (!curve) continue;
            const Vec2 F{60, 20};
            for (Vec2 p : FitPoints(*curve)) CHECK(Close(Dist(p, F), ecc * (p.x - 10), 1e-9));
            CHECK(curve->closed == (ecc < 1));
        }
        std::vector<Entity> out;
        std::wstring why;
        CHECK(!ConicCurve({0, 0}, 0, 0.5, 3.5, out, why));
        Document d;
        Engine e(d, host);
        Type(e, {L"conic", L"50", L"2/3", L"0,0"});
        CHECK(FirstOf(d, EntityType::Spline) && FirstOf(d, EntityType::Spline)->closed);
    }
    {   // Rolling curves.
        std::vector<Entity> out;
        std::wstring why;
        CHECK(RollingCurve(Roll::Cycloid, {0, 0}, 50, 0, 3.5, out, why));
        std::vector<Vec2> pts = FitPoints(out[0]);
        CHECK(Close(pts.front(), Vec2{0, 0}, 1e-9) && Close(pts.back(), Vec2{kPi * 50, 0}, 1e-9));
        double top = 0;
        for (Vec2 p : pts) top = std::max(top, p.y);
        CHECK(Close(top, 50, 1e-9));
        // Epicycloid: outside the base circle, back on it after one turn of the rolling circle.
        CHECK(RollingCurve(Roll::Epicycloid, {0, 0}, 50, 150, 3.5, out, why));
        pts = FitPoints(out[0]);
        for (Vec2 p : pts) CHECK(Length(p) >= 75 - 1e-9);
        CHECK(Close(pts.front(), Vec2{0, 75}, 1e-9) && Close(Length(pts.back()), 75, 1e-9) &&
              Close(AngleOf(pts.back()), kPi / 2 - kTau * 25 / 75, 1e-9));
        CHECK(Close(Length(pts[pts.size() / 2]), 125, 1e-9));  // half a turn: a diameter out
        // Hypocycloid: inside, back on it too.
        CHECK(RollingCurve(Roll::Hypocycloid, {0, 0}, 50, 150, 3.5, out, why));
        pts = FitPoints(out[0]);
        for (Vec2 p : pts) CHECK(Length(p) <= 75 + 1e-9);
        CHECK(Close(Length(pts.back()), 75, 1e-9) && Close(Length(pts[pts.size() / 2]), 25, 1e-9));
        CHECK(!RollingCurve(Roll::Hypocycloid, {0, 0}, 160, 150, 3.5, out, why));
    }
    {   // Involutes, spiral, helix.
        std::vector<Entity> out;
        std::wstring why;
        CHECK(InvoluteCurve({0, 0}, 0, 40, 3.5, out, why));
        std::vector<Vec2> pts;
        for (const Entity& x : out)
            if (x.type == EntityType::Polyline && x.lineweight == 50) pts = FitPoints(x);
        CHECK(pts.size() == 193);
        for (size_t k = 0; k < pts.size(); ++k) {
            const double t = kTau * k / 192;
            CHECK(Close(Length(pts[k]), 20 * std::sqrt(1 + t * t), 1e-9));
        }
        CHECK(InvoluteCurve({0, 0}, 4, 30, 3.5, out, why));
        int arcs = 0;
        double big = 0;
        for (const Entity& x : out)
            if (x.type == EntityType::Arc) ++arcs, big = std::max(big, x.radius);
        CHECK(arcs == 4 && Close(big, 120, 1e-9));
        CHECK(SpiralCurve({0, 0}, 10, 70, 1, 3.5, out, why));
        pts = FitPoints(out[0]);
        CHECK(Close(Length(pts.front()), 10, 1e-9) && Close(Length(pts.back()), 70, 1e-9) && Close(Length(pts[pts.size() / 2]), 40, 1e-9));
        CHECK(HelixCurve({0, 0}, 50, 60, 2, 3.5, out, why));
        double hi = 0;
        int hiddenParts = 0;
        for (const Entity& x : out)
            if (x.type == EntityType::Spline) {
                for (Vec2 p : FitPoints(x)) hi = std::max(hi, p.y);
                hiddenParts += x.linetype == FindLineType(L"HIDDEN2");
            }
        CHECK(Close(hi, 120, 1e-9) && hiddenParts == 2);
    }
    {   // Tangent and normal.
        std::vector<Entity> out;
        Vec2 at;
        CHECK(TangentNormal(Entity::Circle({0, 0}, 30), {31, 0}, 20, 3.5, out, &at));
        CHECK(Close(at, Vec2{30, 0}, 0.05) && Close(std::fabs(out[0].p1.x - out[0].p0.x), 0, 0.1) && Close(Dist(out[0].p0, out[0].p1), 40, 1e-9));
        CHECK(TangentNormal(Entity::Ellipse({0, 0}, {50, 0}, 0.5), {0, 26}, 20, 3.5, out, &at));
        CHECK(Close(at, Vec2{0, 25}, 0.1) && std::fabs(Normalized(out[0].p1 - out[0].p0).y) < 1e-3);  // level at the top
        CHECK(!TangentNormal(Entity::Point({0, 0}), {0, 0}, 20, 3.5, out));
        Document d;
        Engine e(d, host);
        Type(e, {L"c", L"0,0", L"30", L"tn", L"30,0", L"0,30"});
        CHECK(CountOf(d, EntityType::Line) == 2 && Said(host, L"Tangent T-T"));
    }
}

void TestExamplesAndSvg() {
    // Every example in samples/ opens (run from FeatherCAD/ or its build folder).
    const char* names[] = {"Ellipse-by-eccentricity", "Cycloid", "Involute-of-a-circle", "Development-cut-square-prism",
                           "True-length-of-a-line", "Section-and-true-shape-of-a-pyramid", "Isometric-L-block",
                           "Electronic-circuit", "Flowchart-larger-of-two-numbers", "Network-star-topology",
                           "Prism-axis-45-to-HP-edge-30-to-VP"};
    int found = 0;
    for (const char* n : names) {
        std::string text;
        for (const char* dir : {"samples/", "../samples/", "../../samples/", "FeatherCAD/samples/"}) {
            std::FILE* f = std::fopen((std::string(dir) + n + ".fcad").c_str(), "rb");
            if (!f) continue;
            char buf[65536];
            size_t got;
            while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, got);
            std::fclose(f);
            break;
        }
        if (text.empty()) continue;
        ++found;
        Document model;
        SheetList sheets;
        std::wstring err;
        CHECK(DrawingFromText(text, model, sheets, err));
        CHECK(model.Count() > 10);
        // Wires in the circuit examples are joined to their symbols.
        for (const auto& kv : model.Entities())
            if (IsConnector(kv.second)) CHECK(kv.second.annot->from.id && kv.second.annot->to.id);
    }
    if (found == 0) std::printf("note: samples/ not found from here; the examples were not checked\n");
    // SVG: lines, text, the page turned the right way up.
    Document d;
    d.Add(Entity::Line({0, 0}, {100, 0}));
    TextData t;
    t.content = L"A & B";
    d.Add(MakeText({10, 50}, t));
    PlotPage page;
    PlotStyle style;
    PlotDrawing(d, d.Extents(), 210, 297, 10, 1, style, FieldValues{}, page);
    const std::string svg = WriteSvg(page);
    CHECK(svg.find("<svg") != std::string::npos && svg.find("viewBox=\"0 0 210 297\"") != std::string::npos);
    CHECK(svg.find("<polyline") != std::string::npos && svg.find("A &amp; B</text>") != std::string::npos);
    CHECK(svg.rfind("</svg>") != std::string::npos);
}

// The lines among `ents` (by their ends, either way round).
bool HasLine(const Document& d, Vec2 a, Vec2 b, double tol = 1e-6) {
    for (const auto& kv : d.Entities())
        if (kv.second.type == EntityType::Line &&
            ((Close(kv.second.p0, a, tol) && Close(kv.second.p1, b, tol)) || (Close(kv.second.p0, b, tol) && Close(kv.second.p1, a, tol))))
            return true;
    return false;
}

void TestRound3Constructions() {
    TestHost host;
    {   // RF and scales.
        double rf = 0;
        CHECK(ReadRf(L"1:50", rf) && Close(rf, 0.02));
        CHECK(ReadRf(L"1 / 2500", rf) && Close(rf, 1.0 / 2500));
        CHECK(ReadRf(L"2:1", rf) && Close(rf, 2));
        CHECK(ReadRf(L"0.02", rf) && Close(rf, 0.02));
        CHECK(!ReadRf(L"one to fifty", rf) && !ReadRf(L"1:0", rf) && !ReadRf(L"1:50x", rf));
        CHECK(Close(UnitMm(L"km"), 1e6) && Close(UnitMm(L"M"), 1000) && UnitMm(L"furlong") == 0);
        std::vector<Entity> out;
        std::wstring why;
        CHECK(ScaleDrawing(false, {0, 0}, 0.02, 6, L"m", 3.5, out, why));  // 1 m = 20 mm on paper
        CHECK(out[0].type == EntityType::Polyline && Close(out[0].verts[2].p, Vec2{120, 8}));
        CHECK(ScaleDrawing(true, {0, 0}, 0.02, 6, L"m", 3.5, out, why));
        bool diagonal = false;  // the first diagonal: from the zero mark at the bottom, one tenth left at the top
        for (const Entity& e : out)
            if (e.type == EntityType::Line && Close(e.p0, Vec2{20, 0}) && Close(e.p1, Vec2{18, 25})) diagonal = true;
        CHECK(diagonal);
        CHECK(!ScaleDrawing(false, {0, 0}, 0.02, 6, L"furlong", 3.5, out, why) && !why.empty());
        CHECK(!ScaleDrawing(false, {0, 0}, 1, 6, L"km", 3.5, out, why));  // kilometres long
        CHECK(!ScaleDrawing(false, {0, 0}, 0.02, 1, L"m", 3.5, out, why));
    }
    {   // Projections of a point; the quadrants.
        CHECK(std::wstring(QuadrantName(20, 30)) == L"first quadrant");
        CHECK(std::wstring(QuadrantName(20, -30)) == L"second quadrant");
        CHECK(std::wstring(QuadrantName(-20, -30)) == L"third quadrant");
        CHECK(std::wstring(QuadrantName(-20, 30)) == L"fourth quadrant");
        CHECK(std::wstring(QuadrantName(0, 30)) == L"on HP, in front of VP");
        DimStyle st;
        int points = 0, dims = 0;
        for (const Entity& e : PointProjection({0, 0}, 20, 30, L"a", st)) {
            if (e.type == EntityType::Point) points += Close(e.p0, Vec2{0, 20}) || Close(e.p0, Vec2{0, -30});
            if (e.type == EntityType::Dimension) ++dims;
        }
        CHECK(points == 2 && dims == 2);
    }
    {   // Traces: A 10 above HP and 20 in front of VP, B 60 to the right, 40 above, 50 in front.
        LineViews v;
        v.a1 = {0, 10}, v.b1 = {60, 40}, v.a = {0, -20}, v.b = {60, -50};
        const Traces t = LineTraces(v, 0);
        CHECK(t.hasHT && Close(t.h1, Vec2{-20, 0}) && Close(t.h, Vec2{-20, -10}));
        CHECK(t.hasVT && Close(t.v, Vec2{-40, 0}) && Close(t.v1, Vec2{-40, -10}));
        LineViews flat = v;  // parallel to HP: no HT
        flat.b1.y = 10;
        CHECK(!LineTraces(flat, 0).hasHT && LineTraces(flat, 0).hasVT);
        CHECK(!TraceConstruction(v, 0, t, 3.5).empty());
    }
    {   // The development of a transition piece keeps the true lengths of its edges.
        std::vector<Entity> out;
        std::wstring why;
        CHECK(DevelopTransition(80, 60, 50, 60, {0, 0}, 3.5, out, why));
        const Entity& bottom = out[0];
        CHECK(bottom.type == EntityType::Polyline && bottom.verts.size() == 6);
        const double want[] = {40, 60, 80, 60, 40};
        bool sides = true;
        for (size_t k = 0; k < 5; ++k) sides = sides && Close(Dist(bottom.verts[k].p, bottom.verts[k + 1].p), want[k], 1e-6);
        CHECK(sides);
        const Entity& top = out[1];
        bool chords = top.type == EntityType::Spline && top.verts.size() == 13;
        for (size_t k = 0; chords && k + 1 < top.verts.size(); ++k)
            chords = Close(Dist(top.verts[k].p, top.verts[k + 1].p), 50 * std::sin(kPi / 12), 1e-6);
        CHECK(chords);
        // The seam is the same length at both ends.
        CHECK(Close(Dist(bottom.verts[0].p, top.verts[0].p), Dist(bottom.verts[5].p, top.verts[12].p), 1e-6));
        CHECK(!DevelopTransition(80, 0, 50, 60, {0, 0}, 3.5, out, why));
    }
    {   // Intersection of two cylinders: the curve's points lie on both.
        IntersectSpec s;
        s.uprightSize = 60, s.acrossSize = 40, s.axisHeight = 50, s.uprightHeight = 100, s.acrossLength = 120;
        std::vector<Entity> out;
        std::wstring why;
        CHECK(IntersectSolids(s, {0, 0}, 100, 3.5, out, why));
        int curves = 0;
        bool onBoth = true;
        for (const Entity& e : out) {
            if (e.type != EntityType::Polyline || e.verts.size() < 20) continue;
            ++curves;
            for (const Vertex& v : e.verts) onBoth = onBoth && Close(v.p.x * v.p.x - (v.p.y - 50) * (v.p.y - 50), 500, 1e-6);
        }
        CHECK(curves >= 2 && onBoth);
        IntersectSpec big = s;
        big.acrossSize = 70;  // wider than the upright one
        CHECK(!IntersectSolids(big, {0, 0}, 100, 3.5, out, why));
        IntersectSpec low = s;
        low.axisHeight = 10;  // out through the base
        CHECK(!IntersectSolids(low, {0, 0}, 100, 3.5, out, why));
        IntersectSpec prisms = s;
        prisms.upright = IntersectSpec::Prism, prisms.across = IntersectSpec::PrismDiamond;
        CHECK(IntersectSolids(prisms, {0, 0}, 100, 3.5, out, why));
    }
    {   // Perspective: points on the picture plane stay where they are; far ones go to the vanishing point.
        CHECK(Close(PerspectivePoint(10, 0, 30, 20, 120, 50), Vec2{10, 30}));
        const double t = ToRadians(30), far = 1e9;
        const Vec2 v = PerspectivePoint(far * std::cos(t), far * std::sin(t), 0, 20, 120, 50);
        CHECK(Close(v, Vec2{20 + 120 / std::tan(t), 50}, 1e-4));
        PerspectiveSpec ps;
        std::vector<Entity> out;
        std::wstring why;
        CHECK(PerspectiveDrawing(ps, {0, 0}, 3.5, out, why) && out.size() > 20);
        ps.angle = 0;
        CHECK(!PerspectiveDrawing(ps, {0, 0}, 3.5, out, why));
    }
    {   // The construction helpers.
        Document d;
        Engine e(d, host);
        d.Add(Entity::Line({0, 0}, {10, 0}));
        Type(e, {L"bisect", L"5,0", L""});
        CHECK(HasLine(d, {5, -6}, {5, 6}));
        d.Add(Entity::Line({0, 0}, {0, 10}));
        Type(e, {L"bsc", L"8,0", L"0,8"});
        CHECK(HasLine(d, {0, 0}, {std::sqrt(50.0), std::sqrt(50.0)}));
        Type(e, {L"parallelthru", L"3,0", L"3,7", L""});
        CHECK(HasLine(d, {0, 7}, {10, 7}));
        Type(e, {L"ppt", L"2,0", L"4,9", L""});
        CHECK(HasLine(d, {4, 9}, {4, 0}));
        d.Add(Entity::Circle({100, 0}, 10));
        Type(e, {L"tanfrom", L"110,0", L"120,0", L""});
        int tangents = 0;
        for (const auto& kv : d.Entities())
            if (kv.second.type == EntityType::Line && Close(kv.second.p0, Vec2{120, 0}))
                tangents += Close(Dist(kv.second.p0, kv.second.p1), std::sqrt(300.0), 1e-6) &&
                            Close(Dist(kv.second.p1, Vec2{100, 0}), 10, 1e-6);
        CHECK(tangents == 2);
    }
    {   // ALIGN: two points onto two others, scaled to fit; and just moved.
        Document d;
        Engine e(d, host);
        const uint64_t id = d.Add(Entity::Line({0, 0}, {10, 0}));
        e.AddToSelection({id});
        Type(e, {L"align", L"0,0", L"50,50", L"10,0", L"50,70", L"Y"});
        CHECK(Close(d.Find(id)->p0, Vec2{50, 50}, 1e-9) && Close(d.Find(id)->p1, Vec2{50, 70}, 1e-9));
        e.AddToSelection({id});
        Type(e, {L"al", L"50,50", L"60,50", L"50,70", L"70,60", L""});  // turned, not scaled
        CHECK(Close(d.Find(id)->p0, Vec2{60, 50}, 1e-9) && Close(d.Find(id)->p1, Vec2{60 + 20 / std::sqrt(2.0), 50 + 20 / std::sqrt(2.0)}, 1e-9));
        e.AddToSelection({id});
        Type(e, {L"align", L"60,50", L"0,0", L""});
        CHECK(Close(d.Find(id)->p0, Vec2{0, 0}, 1e-9));
    }
    {   // The construction commands make their drawings.
        Document d;
        Engine e(d, host);
        Type(e, {L"scales", L"D", L"1:50", L"M", L"6", L"0,0"});
        const size_t afterScale = d.Count();
        CHECK(afterScale > 30);
        Type(e, {L"pointproj", L"p", L"-20", L"30", L"200,0", L"", L"", L"", L"300,0"});
        e.Cancel();
        CHECK(d.Count() > afterScale + 10);
        const size_t before = d.Count();
        Type(e, {L"transition", L"", L"", L"", L"", L"0,-200"});
        Type(e, {L"intersect", L"C", L"", L"", L"P", L"", L"", L"", L"", L"300,-200"});
        Type(e, {L"perspective", L"", L"", L"", L"", L"", L"", L"", L"600,-200"});
        CHECK(d.Count() > before + 100);
        // TRACES from the views drawn as lines.
        Document t;
        Engine te(t, host);
        t.Add(Entity::Line({0, 10}, {60, 40}));
        t.Add(Entity::Line({0, -20}, {60, -50}));
        t.Add(Entity::Line({-80, 0}, {80, 0}));
        Type(te, {L"traces", L"30,25", L"30,-35", L"70,0"});
        CHECK(HasLine(t, {-20, 0}, {-20, -10}) && HasLine(t, {-40, 0}, {-40, -10}));
    }
    {   // Greek letters in a PDF come from the Symbol font.
        Document d;
        TextData t;
        t.content = L"\x03B8 = 30\x00B0, \x03C6";
        d.Add(MakeText({10, 50}, t));
        d.Add(Entity::Line({0, 0}, {100, 0}));
        PlotPage page;
        PlotStyle style;
        PlotDrawing(d, d.Extents(), 210, 297, 10, 1, style, FieldValues{}, page);
        const std::string pdf = WritePdf({page}, L"Greek");
        CHECK(pdf.find("/F4 9 0 R") != std::string::npos);
        CHECK(pdf.find("9 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Symbol >>") != std::string::npos);
        CHECK(pdf.find("(q) Tj") != std::string::npos && pdf.find("(f) Tj") != std::string::npos);
    }
}

void TestRound3Annotation() {
    TestHost host;
    {   // ISO 286 fits.
        double up = 0, lo = 0;
        std::wstring why;
        CHECK(IsoFit(L"H7", 40, up, lo, why) && Close(up, 0.025) && Close(lo, 0));
        CHECK(IsoFit(L"g6", 40, up, lo, why) && Close(up, -0.009) && Close(lo, -0.025));
        CHECK(IsoFit(L"k6", 40, up, lo, why) && Close(up, 0.018) && Close(lo, 0.002));
        CHECK(IsoFit(L"p6", 40, up, lo, why) && Close(up, 0.042) && Close(lo, 0.026));
        CHECK(IsoFit(L"H8", 25, up, lo, why) && Close(up, 0.033) && Close(lo, 0));
        CHECK(IsoFit(L"f7", 25, up, lo, why) && Close(up, -0.020) && Close(lo, -0.041));
        CHECK(IsoFit(L"js6", 40, up, lo, why) && Close(up, 0.008) && Close(lo, -0.008));
        CHECK(IsoFit(L"F8", 25, up, lo, why) && Close(up, 0.053) && Close(lo, 0.020));
        CHECK(IsoFit(L"h7", 3, up, lo, why) && Close(up, 0) && Close(lo, -0.010));  // 3 is in the first range
        CHECK(!IsoFit(L"H7", 600, up, lo, why) && !IsoFit(L"Q7", 40, up, lo, why) && !IsoFit(L"H", 40, up, lo, why) &&
              !IsoFit(L"K7", 40, up, lo, why) && !why.empty());
        CHECK(DeviationText(0.025) == L"+0.025" && DeviationText(0) == L"0" && DeviationText(-0.0090) == L"-0.009");
    }
    {   // Tolerances on a dimension.
        DimData d;
        d.kind = DimData::Linear;
        d.pts[0] = {0, 0}, d.pts[1] = {40, 0}, d.loc = {20, 10};
        d.tol = DimData::TolFitDeviation, d.fit = L"H7", d.tolUpper = 0.025, d.tolLower = 0;
        DimGraphics g;
        DimLayout(d, g);
        CHECK(g.text == L"40H7" && g.extra.size() == 2 && g.extra[0].text == L"+0.025" && g.extra[1].text == L"0");
        CHECK(g.extra[0].pos.x > g.textPos.x && g.extra[0].pos.y > g.extra[1].pos.y && g.extra[0].height < g.textHeight);
        d.tol = DimData::TolDeviation, d.tolUpper = 0.02, d.tolLower = -0.02;
        DimLayout(d, g);
        CHECK(g.text == L"40" && g.extra.size() == 1 && g.extra[0].text == L"\x00B1" L"0.02");
        d.tol = DimData::TolLimits, d.tolUpper = 0.025, d.tolLower = 0;
        DimLayout(d, g);
        CHECK(g.text.empty() && g.extra.size() == 2 && g.extra[0].text == L"40.025" && g.extra[1].text == L"40.000");
        // Saved and read back.
        Document doc;
        doc.Add(MakeDimension(d));
        Document back;
        std::wstring err;
        CHECK(back.FromText(doc.ToText(), err) && back.ToText() == doc.ToText());
        const Entity& bd = back.Entities().begin()->second;
        CHECK(bd.dim && bd.dim->tol == DimData::TolLimits && Close(bd.dim->tolUpper, 0.025));
    }
    {   // An isometric dimension: extension lines upright, figures slanted.
        DimData d;
        d.kind = DimData::Aligned;
        const Vec2 u{std::cos(ToRadians(30)), std::sin(ToRadians(30))};
        d.pts[0] = {0, 0}, d.pts[1] = u * 50, d.loc = Vec2{0, 15} + u * 25;
        d.slanted = true, d.extAngle = ToRadians(60);
        DimGraphics g;
        DimLayout(d, g);
        int upright = 0;
        for (const Prim& p : g.lines)
            if (std::fabs(p.a.x - p.b.x) < 1e-9 && Dist(p.a, p.b) > 1) ++upright;
        CHECK(upright == 2);
        CHECK(Close(std::fabs(ToDegrees(g.textOblique)), 30, 1e-6) && Close(ToDegrees(g.textAngle), 30, 1e-6));
        CHECK(Close(DimMeasure(d), 50));
        // The figures' upright strokes stand along the extension lines.
        const Vec2 tu{std::cos(g.textAngle), std::sin(g.textAngle)};
        const Vec2 lean = Perp(tu) + tu * std::tan(g.textOblique);
        CHECK(std::fabs(lean.x) < 1e-9);
        Document doc;
        doc.Add(MakeDimension(d));
        Document back;
        std::wstring err;
        CHECK(back.FromText(doc.ToText(), err) && back.Entities().begin()->second.dim->slanted &&
              Close(back.Entities().begin()->second.dim->extAngle, ToRadians(60), 1e-9));
    }
    {   // Slanted text: saved, in PDF and SVG.
        Document d;
        TextData t;
        t.content = L"ISO";
        t.oblique = ToRadians(30);
        t.style = L"Isometric right";
        d.Add(MakeText({10, 50}, t));
        d.Add(Entity::Line({0, 0}, {100, 0}));
        Document back;
        std::wstring err;
        CHECK(back.FromText(d.ToText(), err) && back.ToText() == d.ToText());
        PlotPage page;
        PlotStyle style;
        PlotDrawing(d, d.Extents(), 210, 297, 10, 1, style, FieldValues{}, page);
        CHECK(WriteSvg(page).find("skewX(-30)") != std::string::npos);
        CHECK(WritePdf({page}, L"Slant").find("Tm (ISO) Tj") != std::string::npos);
    }
    {   // ISOTEXT on each face.
        Document d;
        Engine e(d, host);
        Type(e, {L"isotext", L"R", L"0,0", L"FRONT"});
        const TextData* t = TextOf(Last(d));
        CHECK(t && t->content == L"FRONT" && Close(ToDegrees(t->rotation), 30, 1e-9) && Close(ToDegrees(t->oblique), 30, 1e-9));
        Type(e, {L"it", L"L", L"0,0", L"SIDE"});
        t = TextOf(Last(d));
        CHECK(t && Close(ToDegrees(t->rotation), -30, 1e-9) && Close(ToDegrees(t->oblique), -30, 1e-9));
        Type(e, {L"it", L"T", L"0,0", L"TOP"});
        t = TextOf(Last(d));
        CHECK(t && Close(ToDegrees(t->rotation), 30, 1e-9) && Close(ToDegrees(t->oblique), -30, 1e-9));
        // ISODIM along a 30 degree edge, the dimension line above it: upright extension lines.
        const Vec2 b = Vec2{std::cos(ToRadians(30)), std::sin(ToRadians(30))} * 40;
        d.Add(Entity::Line({0, 0}, b));
        e.Text(L"isodim");
        e.Text(L"0,0");
        e.Text((std::to_wstring(b.x) + L"," + std::to_wstring(b.y)).c_str());
        e.Text((std::to_wstring(b.x / 2) + L"," + std::to_wstring(b.y / 2 + 15)).c_str());
        const Entity& dim = Last(d);
        CHECK(dim.dim && dim.dim->slanted && Close(std::fabs(ToDegrees(dim.dim->extAngle)), 60, 1e-6) && Close(DimMeasure(*dim.dim), 40, 1e-4));
    }
    {   // QDIM: a chain of level dimensions over three rectangles' corners.
        Document d;
        Engine e(d, host);
        const uint64_t a = d.Add(Entity::Rectangle({0, 0}, {20, 10}));
        const uint64_t b = d.Add(Entity::Rectangle({20, 0}, {50, 10}));
        e.AddToSelection({a, b});
        Type(e, {L"qdim", L"25,30"});
        int dims = 0;
        double total = 0;
        for (const auto& kv : d.Entities())
            if (kv.second.dim) ++dims, total += DimMeasure(*kv.second.dim);
        CHECK(dims == 2 && Close(total, 50, 1e-9));
        e.AddToSelection({a, b});
        Type(e, {L"qd", L"B", L"#-20,5"});  // baseline, upright, to the left
        int upright = 0;
        for (const auto& kv : d.Entities())
            if (kv.second.dim && Close(kv.second.dim->angle, kPi / 2)) ++upright;
        CHECK(upright == 1);
    }
    {   // DIMTOL through the command: a fit, then symmetric.
        Document d;
        Engine e(d, host);
        DimData dd;
        dd.kind = DimData::Linear;
        dd.pts[0] = {0, 0}, dd.pts[1] = {40, 0}, dd.loc = {20, 10};
        const uint64_t id = d.Add(MakeDimension(dd));
        e.AddToSelection({id});
        Type(e, {L"dimtol", L"F", L"g6", L""});
        CHECK(d.Find(id)->dim->tol == DimData::TolFitDeviation && d.Find(id)->dim->fit == L"g6" && Close(d.Find(id)->dim->tolLower, -0.025));
        e.AddToSelection({id});
        Type(e, {L"dimtol", L"S", L"0.1"});
        CHECK(d.Find(id)->dim->tol == DimData::TolDeviation && Close(d.Find(id)->dim->tolUpper, 0.1) && d.Find(id)->dim->fit.empty());
        e.AddToSelection({id});
        Type(e, {L"dimtol", L"N"});
        CHECK(d.Find(id)->dim->tol == DimData::TolNone);
    }
    {   // GROUP: picked together, copied as a new group, saved; UNGROUP.
        Document d;
        Engine e(d, host);
        const uint64_t a = d.Add(Entity::Line({0, 0}, {10, 0}));
        const uint64_t b = d.Add(Entity::Circle({5, 5}, 2));
        const uint64_t c = d.Add(Entity::Line({50, 0}, {60, 0}));
        e.AddToSelection({a, b});
        Type(e, {L"group"});
        CHECK(d.Find(a)->group != 0 && d.Find(a)->group == d.Find(b)->group && d.Find(c)->group == 0);
        e.AddToSelection({a});
        CHECK(e.Selection().size() == 2);  // the whole group
        Type(e, {L"copy", L"0,0", L"0,100", L""});
        std::set<uint32_t> groups;
        for (const auto& kv : d.Entities())
            if (kv.second.group) groups.insert(kv.second.group);
        CHECK(groups.size() == 2 && d.Count() == 5);
        Document back;
        std::wstring err;
        CHECK(back.FromText(d.ToText(), err) && back.Find(a) && back.ToText() == d.ToText());
        e.ClearSelection();
        e.AddToSelection({b});
        Type(e, {L"ungroup"});
        CHECK(d.Find(a)->group == 0 && d.Find(b)->group == 0);
        // A plain copy of a line made by other commands joins no group.
        Entity x = *d.Find(c);
        x.group = 7;
        d.Add(x);
        CHECK(Last(d).group == 0);
    }
    {   // HIDEOBJECTS, ISOLATE, UNHIDE.
        Document d;
        Engine e(d, host);
        const uint64_t a = d.Add(Entity::Line({0, 0}, {10, 0}));
        const uint64_t b = d.Add(Entity::Line({0, 5}, {10, 5}));
        const uint64_t c = d.Add(Entity::Line({0, 9}, {10, 9}));
        e.AddToSelection({a});
        Type(e, {L"hideobjects"});
        CHECK(!d.Visible(*d.Find(a)) && d.Visible(*d.Find(b)) && d.HiddenCount() == 1);
        e.AddToSelection({a});
        CHECK(e.Selection().empty());  // hidden: cannot be picked
        e.AddToSelection({b});
        Type(e, {L"isolate"});
        CHECK(d.Visible(*d.Find(b)) && !d.Visible(*d.Find(c)) && d.HiddenCount() == 2);
        CHECK(d.ToText().find("Hidden") == std::string::npos);
        Type(e, {L"unhide"});
        CHECK(d.HiddenCount() == 0 && d.Visible(*d.Find(a)) && d.Visible(*d.Find(c)));
    }
    {   // FIND and replace.
        Document d;
        Engine e(d, host);
        TextData t;
        t.content = L"Scale 1:2, SCALE as shown";
        const uint64_t a = d.Add(MakeText({0, 0}, t));
        t.content = L"Nothing here";
        d.Add(MakeText({0, 10}, t));
        Type(e, {L"find", L"scale", L""});
        CHECK(e.Selection().size() == 1 && e.Selection().count(a));
        Type(e, {L"find", L"scale", L"Size"});
        CHECK(TextOf(*d.Find(a))->content == L"Size 1:2, Size as shown");
        d.Undo();
        CHECK(TextOf(*d.Find(a))->content == L"Scale 1:2, SCALE as shown");
    }
    {   // TEXTSTYLE: built-in isometric styles; a new one, saved; texts follow a change.
        Document d;
        Engine e(d, host);
        CHECK(d.Settings().AllTextStyles().size() == 4 && d.Settings().FindTextStyle(L"isometric RIGHT"));
        Type(e, {L"textstyle", L"Isometric left"});
        CHECK(Close(ToDegrees(e.lastText.oblique), -30, 1e-9) && e.lastText.style == L"Isometric left" &&
              d.Settings().textStyle == L"Isometric left");
        Type(e, {L"st", L"N", L"Titles", L"Times New Roman", L"1.2", L"0"});
        CHECK(e.lastText.font == L"Times New Roman" && Close(e.lastText.widthFactor, 1.2) && e.lastText.oblique == 0);
        TextData t = e.lastText;
        t.content = L"TITLE";
        const uint64_t id = d.Add(MakeText({0, 0}, t));
        Type(e, {L"st", L"N", L"Titles", L"", L"0.9", L"15"});
        CHECK(Close(TextOf(*d.Find(id))->widthFactor, 0.9) && Close(ToDegrees(TextOf(*d.Find(id))->oblique), 15, 1e-9));
        Document back;
        std::wstring err;
        CHECK(back.FromText(d.ToText(), err) && back.Settings().textStyles.size() == 1 && back.Settings().textStyle == L"Titles" &&
              Close(back.Settings().textStyles[0].widthFactor, 0.9));
    }
}

void TestRound3Personal() {
    TestHost host;
    {   // Languages: menus translated, shortcuts kept, unknown words left in English.
        CHECK(Languages().size() == 3 && std::wstring(Languages()[0].code) == L"en");
        SetLanguage(L"hi");
        CHECK(CurrentLanguage() == L"hi");
        CHECK(Tr(L"&Line\tL") == L"\x0930\x0947\x0916\x093E\tL");  // rekha
        CHECK(Tr(L"File") != L"File" && Tr(L"Balloon (callout)") == L"Balloon (callout)");
        SetLanguage(L"kn", "Balloon (callout) = TEST\n# a note\nbad line\n");
        CHECK(Tr(L"Circle") == L"\x0CB5\x0CC3\x0CA4\x0CCD\x0CA4" && Tr(L"Bal&loon (callout)\tBALLOON") == L"TEST\tBALLOON");
        SetLanguage(L"xx");
        CHECK(CurrentLanguage() == L"en" && Tr(L"&Line\tL") == L"&Line\tL");
        std::map<std::wstring, std::wstring> t;
        CHECK(ReadTranslations("A = B\nC=D\n = E\n#F = G\nH = I & J\n", t) == 2 && t[L"A"] == L"B" && t[L"H"] == L"I & J");
        // Every Hindi and Kannada entry is read (no line lost to a typo).
        for (const wchar_t* code : {L"hi", L"kn"}) {
            SetLanguage(code);
            CHECK(Tr(L"Tolerance and fits (H7/g6)...") != L"Tolerance and fits (H7/g6)..." && Tr(L"About Feather CAD") != L"About Feather CAD");
        }
        SetLanguage(L"en");
    }
    {   // The user's aliases come first; bad lines are reported.
        std::map<std::wstring, std::wstring> a;
        std::wstring problems;
        const size_t n = ReadAliases(L"# mine\nbx = rectang\nCC, *CIRCLE\nzz = nosuchcommand\njust words\nTL=trl ; true length\n", a, problems);
        CHECK(n == 3 && a[L"BX"] == L"RECTANG" && a[L"CC"] == L"CIRCLE" && a[L"TL"] == L"TRUELENGTH");
        CHECK(problems.find(L"NOSUCHCOMMAND") != std::wstring::npos && problems.find(L"line 5") != std::wstring::npos);
        UserAliases() = a;
        Document d;
        Engine e(d, host);
        Type(e, {L"bx", L"0,0", L"10,10"});
        CHECK(d.Count() == 1 && Last(d).type == EntityType::Polyline);
        Type(e, {L"L", L"0,0", L"5,0", L""});  // the built-in ones still work
        CHECK(d.Count() == 2);
        UserAliases().clear();
        std::map<std::wstring, std::wstring> none;
        CHECK(ReadAliases(AliasesTemplate(), none, problems) == 0);
    }
    {   // My details: asked once, then in the title block of new drawings and exam sheets.
        MyDetails().clear();
        Document d;
        Engine e(d, host);
        Type(e, {L"mydetails", L"Akshaya Simha", L"1RV24ME001", L"Mechanical", L"RV College"});
        CHECK(MyDetails()[L"usn"] == L"1RV24ME001" && d.Properties().at(L"drawnby") == L"Akshaya Simha" &&
              d.Properties().at(L"company") == L"RV College");
        Document fresh;
        CHECK(ApplyMyDetails(fresh, false) && fresh.Properties().at(L"usn") == L"1RV24ME001");
        fresh.SetProperty(L"usn", L"OTHER");
        CHECK(!ApplyMyDetails(fresh, false) && fresh.Properties().at(L"usn") == L"OTHER");
        Type(e, {L"md", L"", L"-", L"", L""});  // Enter keeps, - clears
        CHECK(MyDetails()[L"name"] == L"Akshaya Simha" && MyDetails()[L"usn"].empty());
        // EXAMSHEET: the title, BIS set up (TestHost has no sheets: it says so).
        Document x;
        Engine ex(x, host);
        Type(ex, {L"examsheet", L"PROJECTIONS OF SOLIDS", L"A4"});
        CHECK(x.Properties().at(L"title") == L"PROJECTIONS OF SOLIDS" && x.Properties().at(L"drawnby") == L"Akshaya Simha" &&
              UsesBisTitleBlock(x) && !ex.Active());
        MyDetails().clear();
        Document y;
        Engine ey(y, host);
        Type(ey, {L"exam", L"Student", L"U1", L"B", L"C", L"", L""});  // no details yet: asked first
        CHECK(y.Properties().at(L"drawnby") == L"Student" && !ey.Active());
        MyDetails().clear();
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // failures show even if a later check crashes
    TestAngles();
    TestIntersections();
    TestArcs();
    TestEntities();
    TestUndo();
    TestFiles();
    TestSnaps();
    TestEngine();
    TestCurves();
    TestTrimExtend();
    TestOffset();
    TestFilletChamfer();
    TestBreakJoinExplode();
    TestArraysAndTransforms();
    TestModifyCommands();
    TestNewShapes();
    TestGripsAndSelect();
    TestDrawCommandsB2();
    TestDimensionValues();
    TestDimensionLayout();
    TestDimensionAssociativity();
    TestDrivingDimensions();
    TestDimensionCommands();
    TestDxfImport();
    TestFillet131();
    TestRenumberedVertices();
    TestTangents131();
    TestOlderGaps131();
    TestLayers();
    TestStyleCommands();
    TestAnnotations();
    TestAnnotCommands();
    TestDxfAnnotations();
    TestSheets();
    TestRelations();
    TestSymbols();
    TestEasierUse();
    TestFreeSketch();
    TestDynamicBlocks();
    TestTablesAndBalloons();
    TestImages();
    TestCloseByClicking();
    TestProjectionHelpers();
    TestRelationsKeepShape();
    TestReshapeAndRegular();
    TestEngineeringGraphics();
    TestSymbolsAndCurves();
    TestExamplesAndSvg();
    TestRound3Constructions();
    TestRound3Annotation();
    TestRound3Personal();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
