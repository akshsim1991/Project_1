// Annot.h - annotation objects: text (one or more lines, with symbols and
// fields), leaders and callouts, hatches and fills, and blocks (a named
// group of objects placed many times, with attributes).
//
// Plain C++ (no Windows): the canvas draws what AnnotLayout returns.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Dimension.h"
#include "Entity.h"

// ---- text --------------------------------------------------------------------
struct TextData {
    std::wstring content;        // lines separated by \n; %%d %%c %%p symbols, {date} fields
    std::wstring font = L"Arial";
    double height = 3.5;         // capital letters this tall
    double rotation = 0;         // radians
    double widthFactor = 1;      // wider (> 1) or narrower letters
    double lineSpacing = 1;      // times the normal spacing (1.6667 x height)
    int justify = 0;             // 0 top left, 1 top centre, 2 top right, 3-5 middle, 6-8 bottom
    int frame = 0;               // 0 none, 1 box, 2 circle (a balloon)
    std::wstring tag, prompt;    // an attribute definition: its tag and question
    bool operator==(const TextData& o) const {
        return content == o.content && font == o.font && height == o.height && rotation == o.rotation &&
               widthFactor == o.widthFactor && lineSpacing == o.lineSpacing && justify == o.justify &&
               frame == o.frame && tag == o.tag && prompt == o.prompt;
    }
};

// Values of fields: "filename", "date", "time" and any the program adds.
using FieldValues = std::map<std::wstring, std::wstring>;
// Symbols (%%d degree, %%c diameter, %%p plus-minus, %%% percent) and
// fields ({date}, {time}, {filename}...) worked out. Unknown fields stay
// as they are.
std::wstring ExpandText(const std::wstring& s, const FieldValues* fields);
std::vector<std::wstring> SplitLines(const std::wstring& s);

// One line of text as drawn.
struct TextItem {
    std::wstring text;
    Vec2 pos;          // on the middle of the capitals: at its left, centre or right end
    int align = 0;     // 0 left, 1 centre, 2 right
    double angle = 0, height = 0, widthFactor = 1;
    double width = 0;  // estimated
    std::wstring font;
};
// What an annotation looks like: lines, filled shapes and text.
struct AnnotGraphics {
    std::vector<Prim> lines;
    std::vector<std::vector<Vec2>> fills;  // filled polygons (arrows); a solid hatch: its loops, even-odd
    bool solid = false;                    // `fills` are a solid hatch
    std::vector<TextItem> texts;
    std::vector<std::array<Vec2, 4>> boxes;  // around each text (for picking)
};

// Text laid out at `at` (its insertion point).
void LayoutText(const TextData& t, Vec2 at, const FieldValues* fields, AnnotGraphics& g);

// ---- hatch -------------------------------------------------------------------
struct HatchPatternLine {
    double angle;               // degrees
    double x, y;                // a point the lines go through
    double dx, dy;              // from one line to the next: along it and across it
    std::vector<double> dashes; // empty: solid lines; > 0 dash, < 0 gap, 0 dot
};
struct HatchPattern {
    const wchar_t* name;
    const wchar_t* description;
    std::vector<HatchPatternLine> lines;  // empty: a solid fill
};
const std::vector<HatchPattern>& HatchPatterns();  // millimetres, as AutoCAD's acadiso.pat
int FindHatchPattern(const std::wstring& name);   // -1 if unknown
bool SameName(const std::wstring& a, const std::wstring& b);  // ignoring case

struct HatchData {
    std::wstring pattern = L"ANSI31";      // "SOLID" fills
    double scale = 1;
    double angle = 0;                      // radians
    Vec2 origin;                           // where the pattern starts
    std::vector<std::vector<Vec2>> loops;  // closed outlines; inside is where an odd number enclose
    // Associative: the objects around it (and the point picked inside), so
    // it follows them when they change.
    std::vector<uint64_t> refs;
    bool seeded = false;
    Vec2 seed;
};
bool IsSolid(const HatchData& h);
// The pattern's line pieces inside the outlines (dots are pieces of no
// length). False if there would be more than `limit`: too dense to draw.
bool HatchSegments(const HatchData& h, std::vector<std::pair<Vec2, Vec2>>& out, size_t limit = 60000);
double LoopArea(const std::vector<Vec2>& loop);  // signed: > 0 counter-clockwise
double HatchArea(const HatchData& h);
bool InsideLoops(const std::vector<std::vector<Vec2>>& loops, Vec2 p);  // even-odd

// The area around `seed` closed by the objects (lines, arcs, circles,
// polylines, ellipses, splines): its outline and the islands inside it.
// `used` gets the indices of the objects that bound it. False if the
// point is not inside a closed area.
// `area` and `perimeter` get the exact values (arcs as arcs, not as the
// short straight pieces of the outlines).
bool FindArea(const std::vector<const Entity*>& objects, Vec2 seed, std::vector<std::vector<Vec2>>& loops,
              std::vector<size_t>* used = nullptr, double* area = nullptr, double* perimeter = nullptr);
// The outline of a closed object (circle, closed polyline, ellipse, spline), as points.
bool ClosedOutline(const Entity& e, std::vector<Vec2>& loop);
// Points along an object (curves cut into short straight pieces).
void Tessellate(const Entity& e, std::vector<Vec2>& pts);

// ---- blocks ------------------------------------------------------------------
struct BlockData {
    std::wstring name;
    Vec2 base;                  // where it hangs from when placed
    std::vector<Entity> items;  // attribute definitions are texts with a tag
};

// ---- relationships (Relate.h) ---------------------------------------------------
enum class RelKind : uint8_t {
    Coincident, Horizontal, Vertical, Parallel, Perpendicular, Collinear, Tangent, Concentric, Equal, Fix, Symmetric, Midpoint
};
struct RelationData {
    RelKind kind = RelKind::Coincident;
    // The elements related: a point (a grip, as GripsOf numbers them) or a
    // whole line, circle or arc (-1, or -1 - i for segment i of a polyline).
    std::array<DimRef, 3> refs{};
    double param = 1;  // Tangent: which side (+1, -1)
};

// What annotation objects carry (Text, Leader, Hatch, Insert).
struct Annot {
    TextData text;                     // Text; Leader: its note
    std::vector<Vec2> pts;             // Leader: the arrow's tip first, then the bends
    double arrow = 3;                  // Leader: arrow size
    HatchData hatch;                   // Hatch
    std::shared_ptr<const BlockData> block;  // Insert
    std::vector<std::wstring> values;  // Insert: its attributes' values, in order
    // Viewport (p0, p1: its corners on the sheet): the drawing point at its
    // centre, sheet millimetres per drawing unit, and whether it is locked.
    Vec2 viewCentre;
    double viewScale = 1;
    bool viewLocked = false;
    RelationData rel;  // Relation (pts: where its marks are shown)
    // A hatch's pattern lines, worked out once (not copied with it).
    struct Cache {
        mutable std::shared_ptr<std::vector<std::pair<Vec2, Vec2>>> segs;
        mutable bool tooDense = false;
        Cache() = default;
        Cache(const Cache&) {}
        Cache& operator=(const Cache&) { segs.reset(); tooDense = false; return *this; }
    } cache;
};

Entity MakeText(Vec2 at, TextData t);
Entity MakeLeader(std::vector<Vec2> pts, TextData t, double arrow);
Entity MakeHatch(HatchData h);
// A viewport from corner a to corner b of a sheet, showing the drawing
// around `centre`, `scale` sheet units per drawing unit.
Entity MakeViewport(Vec2 a, Vec2 b, Vec2 centre, double scale, bool locked = false);
// Sheet point <-> drawing point through a viewport.
Vec2 ViewportToModel(const Entity& vp, Vec2 sheetPoint);
Vec2 ViewportToSheet(const Entity& vp, Vec2 modelPoint);
// The part of the drawing a viewport shows.
Box ViewportModelBox(const Entity& vp);
// A block placed at `at`, `scale` times its size, turned by `rotation`
// (radians), mirrored left to right first if `mirrored`.
Entity MakeInsert(std::shared_ptr<const BlockData> b, Vec2 at, double scale, double rotation,
                  std::vector<std::wstring> values = {}, bool mirrored = false);
// The objects a block reference stands for, where it is placed, with the
// attribute values filled in and ByBlock colours (and layer 0) taken from
// the reference. Blocks inside it are expanded too.
void ExpandBlock(const Entity& ref, std::vector<Entity>& out, int depth = 0, bool deep = true);
// A leader's note as a text of its own: what it says and where (and where
// the landing ends). False if it has none.
bool LeaderNote(const Entity& e, const FieldValues* fields, TextData& note, Vec2& at, Vec2* landing = nullptr);
// Attribute definitions of a block, in order.
std::vector<const TextData*> AttributeDefs(const BlockData& b);
// Does block `b` use block `name` (directly or inside another)?
bool BlockUses(const BlockData& b, const std::wstring& name, int depth = 0);

bool IsAnnotation(const Entity& e);  // text, leader, hatch, dimension: not drawing geometry
bool IsAnnotType(EntityType t);       // Text, Leader, Hatch, Insert (they carry an Annot)
// A private copy of the annotation data, to change.
Annot& EditAnnot(Entity& e);
const TextData* TextOf(const Entity& e);  // Text and Leader; nullptr otherwise

// What a text, leader or hatch looks like (block references: nothing; they
// are drawn as their objects).
void AnnotLayout(const Entity& e, const FieldValues* fields, AnnotGraphics& g);

// Geometry for hit tests and selection (Entity.cpp uses these).
void AnnotPrimitives(const Entity& e, std::vector<Prim>& out);
double AnnotDistance(const Entity& e, Vec2 p);
bool AnnotTouchesBox(const Entity& e, const Box& box);
// Moves, turns, scales or mirrors with `point`; `vec` maps directions.
void AnnotMap(Entity& e, const std::function<Vec2(Vec2)>& point, const std::function<Vec2(Vec2)>& vec,
              bool mirror, double factor);
// The grips: a text's insertion point, a leader's points, a block's base.
void AnnotGrips(const Entity& e, std::vector<std::pair<int, Vec2>>& out);
bool DragAnnotGrip(Entity& e, int index, Vec2 from, Vec2 to);
