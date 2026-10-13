// Symbols.h - the symbol library (electrical and electronic, logic gates,
// flowchart, computer networks, civil plans) as blocks with ports where
// wires join, and connector lines (wires, links, flow lines) whose ends
// stay on those ports when the symbols move.
//
// Plain C++ (no Windows).
#pragma once
#include <string>
#include <vector>

#include "Document.h"

struct SymbolInfo {
    const wchar_t* category;  // "Electrical"
    const wchar_t* name;      // "RESISTOR": what is typed; also the block's name
    const wchar_t* title;     // "Resistor"
};
const std::vector<SymbolInfo>& SymbolLibrary();
std::vector<std::wstring> SymbolCategories();
const SymbolInfo* FindSymbol(const std::wstring& name);  // not case sensitive
// The symbol's block (millimetres, its base where it hangs from), with its
// ports and a LABEL attribute (TEXT for flowchart shapes). False if unknown.
bool MakeSymbolBlock(const std::wstring& name, BlockData& out);
// The block in the drawing (defined from the library the first time).
std::shared_ptr<const BlockData> SymbolBlock(Document& doc, const std::wstring& name);

// ---- connectors ---------------------------------------------------------------
// Where port `ref.grip` of block reference `ref.id` is, and which way a wire
// leaves it (zero: any way). False if there is no such port.
bool PortPoint(const Document& doc, const DimRef& ref, Vec2& at, Vec2& dir);
// The port nearest p within tol (its id 0 if none); `at` gets its place.
DimRef NearestPort(const Document& doc, Vec2 p, double tol, Vec2* at = nullptr);
// The path of a connector from a (leaving along da) to b (arriving along -db):
// a straight line, or square bends (elbows) like a circuit wire.
std::vector<Vec2> RouteConnector(Vec2 a, Vec2 da, Vec2 b, Vec2 db, bool straight);
// A connector (a leader with no note) from a to b, each end on a port or
// free; an arrowhead at b if `arrow` > 0.
Entity MakeConnector(const Document& doc, const DimRef& from, Vec2 a, const DimRef& to, Vec2 b, bool straight, double arrow);
bool IsConnector(const Entity& e);
// Its path again from where its ports are now (a port that is gone lets go
// of that end). True if it changed.
bool RerouteConnector(const Document& doc, Entity& c);
