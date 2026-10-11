// CoreTests.cpp - unit tests for the geometry, entities, undo history and
// file formats (no window needed). Prints each failure and returns the
// number of failures, so CI fails when anything breaks.
//   cmake -DFEATHERCAD_TESTS=ON ...   then run CoreTests.exe
#include <cstdio>
#include <string>

#include "Document.h"
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
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
