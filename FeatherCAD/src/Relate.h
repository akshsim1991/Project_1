// Relate.h - relationships (geometric constraints) and the solver that
// keeps them: horizontal, vertical, parallel, perpendicular, collinear,
// tangent, concentric, equal, coincident, midpoint, symmetric and fixed,
// together with locked dimensions (which hold their value) and formulas.
//
// The solver works on the coordinates of the objects involved: after any
// change it moves them as little as possible (the ones just edited least of
// all) so that every relationship and locked dimension holds again.
//
// Plain C++ (no Windows).
#pragma once
#include <set>
#include <utility>
#include <string>

#include "Document.h"

const wchar_t* RelKindName(RelKind k);
// How many elements a kind relates (1, 2 or 3).
int RelElementCount(RelKind k);

// What an element of an object is: a point, a straight line or a circle.
enum class ElemKind { None, Point, Line, Circle };
struct Element {
    ElemKind kind = ElemKind::None;
    Vec2 a, b;     // Point: a; Line: a to b; Circle: centre a
    double r = 0;  // Circle
};
Element ElementOf(const Entity& e, int grip);
Element ElementOf(const Document& doc, const DimRef& ref);

// The relationship between these elements, if it can be made (`why` if not).
// Its marks are placed and Tangent's side chosen from the drawing as it is.
bool MakeRelation(const Document& doc, RelKind kind, const std::array<DimRef, 3>& refs, double markSize, Entity& out,
                  std::wstring& why);
// Puts a relationship's marks next to its elements again. False if an
// element is gone (the relationship should go too).
bool PlaceRelationMarks(const Document& doc, Entity& rel);

// Moves the objects related to `changed` until their relationships and
// locked dimensions hold, keeping the ones in `keep` (just edited) as they
// are where it can, and the `pinned` points (object, grip) most of all.
// Call inside BeginChange / EndChange.
SolveReport SolveRelations(Document& doc, const std::set<uint64_t>& changed, const std::set<uint64_t>& keep,
                           const std::set<std::pair<uint64_t, int>>& pinned = {});

// Works out the variables and the dimensions that have formulas: those
// dimensions are locked to the result. Changed dimensions are put in
// `changed`. False (with `error`) if a formula cannot be worked out.
bool EvaluateFormulas(Document& doc, std::set<uint64_t>& changed, std::wstring& error);
// The value of a variable or a named dimension (degrees for angles).
bool LookupName(const Document& doc, const std::wstring& name, double& value);

// The relationships a shape already has without saying so, made explicit
// before other relationships are kept on it (as in Solid Edge): objects
// whose ends meet stay joined (a connect relationship at each such corner,
// all through the shape of joined objects), and a rectangle keeps its right
// angles. For the objects `ids` and everything joined to them. Call inside
// BeginChange / EndChange; returns how many relationships were added.
int AddImpliedRelations(Document& doc, const std::set<uint64_t>& ids);

// Relationships the drawing suggests for a new line (horizontal or
// vertical, ends on other objects' points): added when the drawing's
// "add relationships while drawing" is on.
void AutoRelate(Document& doc, uint64_t lineId);
