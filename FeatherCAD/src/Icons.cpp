// Icons.cpp - see Icons.h.
#include "Icons.h"

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
        default:
            break;
    }
}
