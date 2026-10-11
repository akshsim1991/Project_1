// CoreTests.cpp - unit tests for the geometry, entities, undo history and
// file formats (no window needed). Prints each failure and returns the
// number of failures, so CI fails when anything breaks.
//   cmake -DFEATHERCAD_TESTS=ON ...   then run CoreTests.exe
#include <cstdio>
#include <string>

#include "Dimension.h"
#include "Document.h"
#include "DxfImport.h"
#include "Engine.h"
#include "Grips.h"
#include "Modify.h"
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
    CHECK(notes.find(L"TEXT") != std::wstring::npos);

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
    CHECK(notes.find(L"1 TEXT") != std::wstring::npos);
    std::vector<const Entity*> byType[9];
    for (const auto& kv : d.Entities()) byType[(int)kv.second.type].push_back(&kv.second);
    const auto& pls = byType[(int)EntityType::Polyline];
    CHECK(pls.size() == 2);  // the LWPOLYLINE and the spline
    CHECK(pls[0]->closed && pls[0]->verts.size() == 4 && Close(pls[0]->verts[1].bulge, 1));
    CHECK(Close(pls[1]->verts.front().p, {0, 0}) && Close(pls[1]->verts.back().p, {30, 0}) && !pls[1]->closed);
    const auto& els = byType[(int)EntityType::Ellipse];
    CHECK(els.size() == 2 && Close(els[0]->p0, {50, 50}) && Close(Length(els[0]->p1), 20) && Close(els[0]->radius, 0.5));
    // The stretched insert: a circle becomes an ellipse.
    CHECK(Close(els[1]->p0, {0, 300}) && Close(Length(els[1]->p1), 2) && Close(els[1]->radius, 0.5));
    const auto& cs = byType[(int)EntityType::Circle];
    CHECK(cs.size() == 4);  // 1 turned and scaled, 3 in a row
    CHECK(Close(cs[0]->p0, {100, 100}) && Close(cs[0]->radius, 2));
    CHECK(Close(cs[1]->p0, {0, 200}) && Close(cs[2]->p0, {10, 200}) && Close(cs[3]->p0, {20, 200}));
    // The line of the turned insert: from (100,100) up 4.
    bool turned = false;
    for (const Entity* e : byType[(int)EntityType::Line]) turned |= Close(e->p0, {100, 100}) && Close(e->p1, {100, 104});
    CHECK(turned);
    // The arc seen from below is mirrored.
    const auto& as = byType[(int)EntityType::Arc];
    CHECK(as.size() == 1 && Close(as[0]->p0, {-5, 0}) && Close(as[0]->startAngle, kPi / 2) && Close(as[0]->endAngle, kPi));
    // Dimensions come in as dimensions.
    const auto& ds2 = byType[(int)EntityType::Dimension];
    CHECK(ds2.size() == 2);
    CHECK(ds2[0]->dim->kind == DimData::Aligned && Close(DimMeasure(*ds2[0]->dim), 50) && ds2[0]->dim->text.empty());
    CHECK(ds2[1]->dim->kind == DimData::Radius && DimDisplayText(*ds2[1]->dim) == L"\x00D8" L"R10.00");
    // Paper space is left out: the block's line, five times.
    CHECK(byType[(int)EntityType::Line].size() == 5);

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
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
