// Icons.cpp - see Icons.h.
#include "Icons.h"

#include <algorithm>
#include <cmath>

#include "GdiPlusInc.h"

using namespace Gdiplus;

namespace {
Color C(COLORREF c) { return Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }
}  // namespace

void DrawCadIcon(HDC dc, int icon, const RECT& rc, COLORREF ink, COLORREF accent, int dpi) {
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    // 20x20 design units, centred in the button.
    const REAL s = dpi / 96.0f;
    g.TranslateTransform((rc.left + rc.right) / 2.0f - 10 * s, (rc.top + rc.bottom) / 2.0f - 10 * s);
    g.ScaleTransform(s, s);
    Pen pen(C(ink), 1.5f);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    SolidBrush dot(C(accent));
    SolidBrush inkBrush(C(ink));
    auto point = [&](REAL x, REAL y) { g.FillRectangle(&dot, x - 1.8f, y - 1.8f, 3.6f, 3.6f); };
    auto arrowHead = [&](REAL x, REAL y, REAL dx, REAL dy) {  // tip at x,y pointing along dx,dy
        const REAL px = -dy, py = dx;
        PointF tri[3] = {PointF(x, y), PointF(x - 3.5f * dx + 2.2f * px, y - 3.5f * dy + 2.2f * py),
                         PointF(x - 3.5f * dx - 2.2f * px, y - 3.5f * dy - 2.2f * py)};
        g.FillPolygon(&inkBrush, tri, 3);
    };

    switch (icon) {
        case kIconLine:
            g.DrawLine(&pen, 4.0f, 16.0f, 16.0f, 4.0f);
            point(4, 16);
            point(16, 4);
            break;
        case kIconPolyline: {
            PointF pts[4] = {PointF(2.5f, 15.5f), PointF(7.5f, 5.5f), PointF(12.5f, 13.5f), PointF(17.5f, 4.5f)};
            g.DrawLines(&pen, pts, 4);
            for (const PointF& p : pts) point(p.X, p.Y);
            break;
        }
        case kIconCircle:
            g.DrawEllipse(&pen, 3.5f, 3.5f, 13.0f, 13.0f);
            point(10, 10);
            break;
        case kIconArc:
            g.DrawArc(&pen, 3.0f, 5.0f, 14.0f, 14.0f, 180.0f, 180.0f);
            point(3, 12);
            point(17, 12);
            point(10, 5);
            break;
        case kIconRectangle:
            g.DrawRectangle(&pen, 3.0f, 5.0f, 14.0f, 10.0f);
            point(3, 5);
            point(17, 15);
            break;
        case kIconMove:
            g.DrawLine(&pen, 10.0f, 3.0f, 10.0f, 17.0f);
            g.DrawLine(&pen, 3.0f, 10.0f, 17.0f, 10.0f);
            arrowHead(10, 2, 0, -1);
            arrowHead(10, 18, 0, 1);
            arrowHead(2, 10, -1, 0);
            arrowHead(18, 10, 1, 0);
            break;
        case kIconCopy:
            g.DrawRectangle(&pen, 3.0f, 3.0f, 9.0f, 9.0f);
            g.DrawRectangle(&pen, 8.0f, 8.0f, 9.0f, 9.0f);
            point(3, 3);
            point(8, 8);
            break;
        case kIconRotate:
            g.DrawArc(&pen, 3.5f, 3.5f, 13.0f, 13.0f, -60.0f, 290.0f);
            arrowHead(16.2f, 5.8f, 0.55f, 0.83f);
            point(10, 10);
            break;
        case kIconErase: {
            PointF body[4] = {PointF(3, 13), PointF(10, 4), PointF(17, 10), PointF(10.5f, 17)};
            g.DrawPolygon(&pen, body, 4);
            g.DrawLine(&pen, 6.5f, 8.5f, 13.5f, 14.0f);
            g.DrawLine(&pen, 10.5f, 17.0f, 17.5f, 17.0f);
            break;
        }
        case kIconZoomExtents:
            // Corner brackets around a magnifier.
            g.DrawLine(&pen, 2.5f, 6.0f, 2.5f, 2.5f);
            g.DrawLine(&pen, 2.5f, 2.5f, 6.0f, 2.5f);
            g.DrawLine(&pen, 14.0f, 2.5f, 17.5f, 2.5f);
            g.DrawLine(&pen, 17.5f, 2.5f, 17.5f, 6.0f);
            g.DrawLine(&pen, 2.5f, 14.0f, 2.5f, 17.5f);
            g.DrawLine(&pen, 2.5f, 17.5f, 6.0f, 17.5f);
            g.DrawLine(&pen, 14.0f, 17.5f, 17.5f, 17.5f);
            g.DrawLine(&pen, 17.5f, 17.5f, 17.5f, 14.0f);
            g.DrawEllipse(&pen, 6.0f, 6.0f, 6.5f, 6.5f);
            g.DrawLine(&pen, 11.6f, 11.6f, 14.0f, 14.0f);
            break;
        case kIconZoomWindow: {
            Pen dash(C(ink), 1.2f);
            dash.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&dash, 2.5f, 2.5f, 15.0f, 11.0f);
            g.DrawEllipse(&pen, 8.0f, 7.0f, 6.0f, 6.0f);
            g.DrawLine(&pen, 13.2f, 12.2f, 16.5f, 15.5f);
            break;
        }
        case kIconDistance:
            g.DrawLine(&pen, 3.0f, 5.0f, 3.0f, 15.0f);
            g.DrawLine(&pen, 17.0f, 5.0f, 17.0f, 15.0f);
            g.DrawLine(&pen, 4.5f, 10.0f, 15.5f, 10.0f);
            arrowHead(3.5f, 10, -1, 0);
            arrowHead(16.5f, 10, 1, 0);
            break;
        case kIconTrim: {
            // A line cut back at a crossing edge; the removed part dashed.
            g.DrawLine(&pen, 11.0f, 2.5f, 11.0f, 17.5f);
            g.DrawLine(&pen, 2.5f, 10.0f, 11.0f, 10.0f);
            Pen dash(C(accent), 1.5f);
            const REAL pattern[] = {1.2f, 1.4f};
            dash.SetDashPattern(pattern, 2);
            g.DrawLine(&dash, 13.0f, 10.0f, 18.0f, 10.0f);
            g.DrawLine(&dash, 14.0f, 6.5f, 18.0f, 6.5f);
            g.DrawLine(&dash, 14.0f, 13.5f, 18.0f, 13.5f);
            break;
        }
        case kIconExtend:
            g.DrawLine(&pen, 17.0f, 2.5f, 17.0f, 17.5f);
            g.DrawLine(&pen, 2.5f, 10.0f, 9.0f, 10.0f);
            {
                Pen dash(C(accent), 1.5f);
                dash.SetDashStyle(DashStyleDash);
                g.DrawLine(&dash, 9.0f, 10.0f, 15.0f, 10.0f);
            }
            arrowHead(16.5f, 10, 1, 0);
            break;
        case kIconOffset:
            g.DrawArc(&pen, 2.5f, 6.5f, 15.0f, 15.0f, 200.0f, 140.0f);
            {
                Pen second(C(accent), 1.5f);
                g.DrawArc(&second, 6.0f, 10.0f, 8.0f, 8.0f, 200.0f, 140.0f);
            }
            break;
        case kIconFillet:
            g.DrawLine(&pen, 3.0f, 17.0f, 3.0f, 10.0f);
            g.DrawLine(&pen, 10.0f, 3.0f, 17.0f, 3.0f);
            {
                Pen arc(C(accent), 1.5f);
                g.DrawArc(&arc, 3.0f, 3.0f, 14.0f, 14.0f, 180.0f, 90.0f);
            }
            break;
        case kIconMirror: {
            Pen dash(C(accent), 1.2f);
            dash.SetDashStyle(DashStyleDash);
            g.DrawLine(&dash, 10.0f, 2.0f, 10.0f, 18.0f);
            PointF left[3] = {PointF(8, 4), PointF(8, 16), PointF(2.5f, 16)};
            PointF right[3] = {PointF(12, 4), PointF(12, 16), PointF(17.5f, 16)};
            g.DrawPolygon(&pen, left, 3);
            g.DrawPolygon(&pen, right, 3);
            break;
        }
        case kIconScale:
            g.DrawRectangle(&pen, 2.5f, 9.5f, 7.0f, 7.0f);
            {
                Pen big(C(accent), 1.5f);
                g.DrawRectangle(&big, 2.5f, 2.5f, 15.0f, 14.0f);
            }
            arrowHead(16.0f, 4.0f, 0.7f, -0.7f);
            break;
        case kIconArray:
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 3; ++c) g.DrawRectangle(&pen, 2.5f + c * 5.5f, 5.0f + r * 6.0f, 4.0f, 4.0f);
            point(4.5f, 7.0f);
            break;
        case kIconPolygon: {
            PointF hex[6];
            for (int i = 0; i < 6; ++i)
                hex[i] = PointF(10 + 7.5f * std::cos(i * 3.14159265f / 3), 10 + 7.5f * std::sin(i * 3.14159265f / 3));
            g.DrawPolygon(&pen, hex, 6);
            point(10, 10);
            break;
        }
        case kIconEllipse:
            g.DrawEllipse(&pen, 2.0f, 5.5f, 16.0f, 9.0f);
            point(10, 10);
            break;
        case kIconSpline: {
            PointF pts[4] = {PointF(2.5f, 15), PointF(7, 4), PointF(13, 16), PointF(17.5f, 5)};
            g.DrawCurve(&pen, pts, 4);
            for (const PointF& p : pts) point(p.X, p.Y);
            break;
        }
        case kIconDimension:
            g.DrawLine(&pen, 3.0f, 17.0f, 3.0f, 6.0f);
            g.DrawLine(&pen, 17.0f, 17.0f, 17.0f, 6.0f);
            g.DrawLine(&pen, 3.0f, 9.0f, 17.0f, 9.0f);
            arrowHead(3, 9, -1, 0);
            arrowHead(17, 9, 1, 0);
            {
                Pen text(C(accent), 1.5f);
                g.DrawLine(&text, 7.5f, 4.5f, 12.5f, 4.5f);
            }
            point(3, 17);
            point(17, 17);
            break;
        case kIconLayers: {
            // Three sheets stacked.
            for (int k = 2; k >= 0; --k) {
                const REAL y = 5.0f + k * 4.0f;
                PointF sheet[4] = {PointF(10, y - 3.5f), PointF(18, y), PointF(10, y + 3.5f), PointF(2, y)};
                SolidBrush fill(k == 0 ? C(accent) : Color(255, 255, 255, 255));
                if (k != 0) fill.SetColor(Color(0, 0, 0, 0));
                g.FillPolygon(&fill, sheet, 4);
                g.DrawPolygon(&pen, sheet, 4);
            }
            break;
        }
        case kIconText:
            // A capital A on its baseline.
            g.DrawLine(&pen, 4.0f, 17.0f, 10.0f, 3.0f);
            g.DrawLine(&pen, 10.0f, 3.0f, 16.0f, 17.0f);
            g.DrawLine(&pen, 6.6f, 11.5f, 13.4f, 11.5f);
            {
                Pen base(C(accent), 1.5f);
                g.DrawLine(&base, 2.0f, 18.5f, 18.0f, 18.5f);
            }
            break;
        case kIconHatch: {
            // A square filled with slanted lines.
            g.DrawRectangle(&pen, 3.0f, 3.0f, 14.0f, 14.0f);
            Pen hatch(C(accent), 1.2f);
            for (int k = 0; k < 5; ++k) {
                const REAL d = 10.0f + k * 5.0f;  // the line x + y = d, inside the square
                const REAL xa = std::max(3.0f, d - 17.0f), xb = std::min(17.0f, d - 3.0f);
                g.DrawLine(&hatch, xa, d - xa, xb, d - xb);
            }
            break;
        }
        case kIconBlock: {
            // A shape in a dashed box, hanging from its base point.
            Pen dash(C(ink), 1.0f);
            dash.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&dash, 2.5f, 2.5f, 15.0f, 15.0f);
            g.DrawEllipse(&pen, 6.0f, 6.0f, 8.0f, 8.0f);
            g.DrawLine(&pen, 5.0f, 10.0f, 15.0f, 10.0f);
            point(2.5f, 17.5f);
            break;
        }
        case kIconPoint:
            // A small cross with the point in its middle.
            g.DrawLine(&pen, 5.0f, 10.0f, 15.0f, 10.0f);
            g.DrawLine(&pen, 10.0f, 5.0f, 10.0f, 15.0f);
            g.FillEllipse(&dot, 7.2f, 7.2f, 5.6f, 5.6f);
            break;
        case kIconProjector: {
            // A square (a view) with a dashed line carried up from its corner.
            g.DrawRectangle(&pen, 2.5f, 11.0f, 7.5f, 6.5f);
            Pen dash(C(accent), 1.3f);
            dash.SetDashStyle(DashStyleDash);
            g.DrawLine(&dash, 10.0f, 11.0f, 10.0f, 1.5f);
            g.DrawLine(&dash, 10.0f, 11.0f, 18.5f, 11.0f);
            point(10, 11);
            break;
        }
        case kIconLabel: {
            // A corner with the letter "a" and a prime beside it.
            g.DrawLine(&pen, 3.0f, 17.0f, 3.0f, 9.0f);
            g.DrawLine(&pen, 3.0f, 17.0f, 11.0f, 17.0f);
            point(3, 17);
            FontFamily family(L"Arial");
            Font font(&family, 9.5f, FontStyleBold, UnitPixel);
            SolidBrush accentBrush(C(accent));
            g.DrawString(L"a", 1, &font, PointF(7.5f, 2.0f), &accentBrush);
            g.DrawLine(&pen, 15.0f, 3.0f, 14.0f, 6.0f);  // the prime
            break;
        }
        case kIconHidden: {
            // A solid edge above, a hidden (dashed) one below.
            g.DrawLine(&pen, 2.5f, 6.0f, 17.5f, 6.0f);
            Pen dash(C(accent), 1.6f);
            dash.SetDashStyle(DashStyleDash);
            g.DrawLine(&dash, 2.5f, 14.0f, 17.5f, 14.0f);
            break;
        }
        default:
            break;
    }
}
