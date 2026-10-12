// DxfImport.h - reads AutoCAD DXF drawings (ASCII, any release from R12 to
// today) made by AutoCAD, LibreCAD, DraftSight, QCAD, Inkscape and others.
//
// Lines, circles, arcs, polylines (light and old style, with arcs),
// ellipses, splines, points, solids, leaders, dimensions, text (single and
// multi-line), hatches and blocks with attributes come in. Blocks placed
// without stretching stay blocks; stretched ones are brought in as their
// objects. Paper space is left out. What cannot be brought in (images,
// 3D objects) is counted for the message.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "Annot.h"
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
    std::vector<std::shared_ptr<const BlockData>> blocks;  // kept as blocks
    std::map<std::string, size_t> skipped;  // DXF type -> how many were left out
    std::wstring Notes() const;             // "Not brought in: 12 TEXT, 3 HATCH."
};

bool ReadDxf(const std::string& text, DxfResult& out, std::wstring& error);
