// Icons.h - toolbar pictures for the CAD tools, drawn as vector shapes
// (GDI+, anti-aliased), so they are sharp at every DPI and follow the
// light/dark theme colours.
#pragma once
#include "Common.h"

enum CadIcon : int {
    kIconNone = 0,
    kIconLine,
    kIconPolyline,
    kIconCircle,
    kIconArc,
    kIconRectangle,
    kIconMove,
    kIconCopy,
    kIconRotate,
    kIconErase,
    kIconZoomExtents,
    kIconZoomWindow,
    kIconDistance,
    kIconTrim,
    kIconExtend,
    kIconOffset,
    kIconFillet,
    kIconMirror,
    kIconScale,
    kIconArray,
    kIconPolygon,
    kIconEllipse,
    kIconSpline,
    kIconDimension,
    kIconLayers,
    kIconText,
    kIconHatch,
    kIconBlock,
};

// Draws `icon` centred in `rc` (a 20x20 DIP design) in `ink`, with key
// points in `accent`.
void DrawCadIcon(HDC dc, int icon, const RECT& rc, COLORREF ink, COLORREF accent, int dpi);
