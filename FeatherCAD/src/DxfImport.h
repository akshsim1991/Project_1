// DxfImport.h - reads AutoCAD DXF drawings (ASCII, any release from R12 to
// today) made by AutoCAD, LibreCAD, DraftSight, QCAD, Inkscape and others.
//
// Lines, circles, arcs, polylines (light and old style, with arcs),
// ellipses, splines, points, solids, leaders and dimensions come in. Blocks
// (INSERT, also as arrays) are brought in as their objects, moved, scaled
// and turned as inserted. Paper space is left out. What cannot be brought
// in yet (text, hatching, images) is counted for the message.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "Document.h"

struct DxfResult {
    std::vector<Entity> entities;
    bool hasUnits = false;
    Units units = Units::Millimetres;
    bool hasDimStyle = false;
    DimStyle dimStyle;
    std::vector<Layer> layers;   // "0" first (id 0); the objects' layer ids are these
    double ltscale = 1;          // $LTSCALE
    std::wstring currentLayer;   // $CLAYER
    std::map<std::string, size_t> skipped;  // DXF type -> how many were left out
    std::wstring Notes() const;             // "Not brought in: 12 TEXT, 3 HATCH."
};

bool ReadDxf(const std::string& text, DxfResult& out, std::wstring& error);
