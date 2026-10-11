// Canvas.cpp - the drawing area (see Canvas.h).
#include "Canvas.h"

#include <algorithm>
#include <cmath>

#include <windowsx.h>

#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClass[] = L"FeatherCadCanvas";
constexpr double kMinScale = 1e-7, kMaxScale = 1e8;
constexpr int kPickboxDip = 5;     // half size of the pick box
constexpr int kApertureDip = 10;   // object snap search radius
constexpr int kDragDip = 4;        // movement that turns a click into a drag
constexpr double kSafeCoord = 1e6; // beyond this many pixels, clip before drawing

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

D2D1_COLOR_F Rgb(int r, int g, int b, float a = 1.0f) { return D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a); }

// Clips the segment to the box (Liang-Barsky). False if it is outside.
bool ClipSegment(Vec2& a, Vec2& b, const Box& box) {
    double t0 = 0, t1 = 1;
    const Vec2 d = b - a;
    const double p[4] = {-d.x, d.x, -d.y, d.y};
    const double q[4] = {a.x - box.minX, box.maxX - a.x, a.y - box.minY, box.maxY - a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0) return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    const Vec2 a0 = a;
    a = a0 + d * t0;
    b = a0 + d * t1;
    return true;
}
}  // namespace

Canvas::~Canvas() {
    ReleaseTarget();
    SafeRelease(m_dashed);
    SafeRelease(m_textFormat);
    SafeRelease(m_dwrite);
    SafeRelease(m_factory);
}

bool Canvas::Create(HWND parent, Engine* engine, Document* doc) {
    m_engine = engine;
    m_doc = doc;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&m_dwrite))))
        return false;
    const float dashes[] = {4.0f, 3.0f};
    m_factory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                    D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f),
        dashes, 2, &m_dashed);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &Canvas::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    OnDpiChanged();
    LoadColours();
    return true;
}

// ---------------------------------------------------------------------------
// Coordinates and view
// ---------------------------------------------------------------------------

Vec2 Canvas::ToWorld(double sx, double sy) const {
    return {(sx - m_width / 2.0) / m_scale + m_centre.x, (m_height / 2.0 - sy) / m_scale + m_centre.y};
}

D2D1_POINT_2F Canvas::ToScreen(Vec2 w) const {
    return D2D1::Point2F((float)((w.x - m_centre.x) * m_scale + m_width / 2.0),
                         (float)(m_height / 2.0 - (w.y - m_centre.y) * m_scale));
}

Box Canvas::ViewBox() const { return Box(ToWorld(0, m_height), ToWorld(m_width, 0)); }

void Canvas::PushView() {
    m_previous.push_back({m_centre, m_scale});
    if (m_previous.size() > 20) m_previous.erase(m_previous.begin());
}

void Canvas::ZoomWindow(const Box& box, bool remember) {
    if (box.Empty()) return;
    if (remember) PushView();
    const double w = std::max(box.Width(), 1e-9), h = std::max(box.Height(), 1e-9);
    m_centre = box.Center();
    m_scale = std::clamp(std::min(m_width / w, m_height / h), kMinScale, kMaxScale);
    m_layerValid = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void Canvas::ZoomExtents() {
    Box ext = m_doc->Extents();
    if (ext.Empty()) {
        // An empty drawing: show an A3 sheet's worth of space from the origin.
        const bool imperial = m_doc->Settings().units == Units::Inches || m_doc->Settings().units == Units::Feet;
        ext = imperial ? Box({0, 0}, {17, 11}) : Box({0, 0}, {420, 297});
    }
    const double margin = std::max(ext.Width(), ext.Height()) * 0.05 + 1e-6;
    ZoomWindow(ext.Inflated(margin));
}

void Canvas::ZoomPrevious() {
    if (m_previous.empty()) return;
    m_centre = m_previous.back().first;
    m_scale = m_previous.back().second;
    m_previous.pop_back();
    m_layerValid = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void Canvas::ZoomFactor(double factor) {
    PushView();
    m_scale = std::clamp(m_scale * factor, kMinScale, kMaxScale);
    m_layerValid = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void Canvas::ZoomAt(double factor, int x, int y) {
    const ULONGLONG now = GetTickCount64();
    if (now - m_lastWheelPush > 1500) PushView();  // one "previous view" per wheel gesture
    m_lastWheelPush = now;
    const Vec2 w = ToWorld(x, y);
    m_scale = std::clamp(m_scale * factor, kMinScale, kMaxScale);
    // Keep the point under the cursor where it is.
    m_centre = {w.x - (x - m_width / 2.0) / m_scale, w.y - (m_height / 2.0 - y) / m_scale};
    m_layerValid = false;
    OnMouseMove(x, y, 0);
}

void Canvas::PanBy(int dx, int dy) {
    m_centre.x -= dx / m_scale;
    m_centre.y += dy / m_scale;
    m_layerValid = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

bool Canvas::CursorPoint(Vec2& p) const {
    p = m_tracked;
    return m_inside;
}

const wchar_t* Canvas::SnapLabel() const { return m_snapHit.found ? SnapName(m_snapHit.mode) : nullptr; }

void Canvas::Notify() { SendMessageW(GetParent(m_hwnd), WM_APP_CANVAS_STATE, 0, 0); }

void Canvas::Refresh() {
    const Drafting& d = m_engine->Draft();
    if (m_shownRevision != m_doc->Revision() || m_shownSelection != m_engine->SelectionRevision() ||
        m_shownGrid != d.gridOn || m_shownGridSpacing != d.gridSpacing)
        m_layerValid = false;
    if (!m_engine->WantsSelection() && m_windowOn) {
        m_windowOn = false;
        if (GetCapture() == m_hwnd) ReleaseCapture();
    }
    if (m_inside) {
        OnMouseMove(m_mouse.x, m_mouse.y, 0);
    } else {
        m_preview.Clear();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Canvas::OnThemeChanged() {
    LoadColours();
    m_layerValid = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Canvas::OnDpiChanged() {
    m_dpi = GetWindowDpi(m_hwnd);
    SafeRelease(m_textFormat);
    m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, (float)Px(12), L"", &m_textFormat);
    m_layerValid = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Canvas::LoadColours() {
    if (CurrentTheme().dark) {
        m_bg = Rgb(33, 38, 46);
        m_fg = Rgb(236, 239, 244);
        m_gridMinor = Rgb(44, 50, 60);
        m_gridMajor = Rgb(58, 66, 79);
        m_axisX = Rgb(170, 70, 70, 0.8f);
        m_axisY = Rgb(70, 160, 90, 0.8f);
        m_select = Rgb(80, 160, 255);
        m_previewInk = Rgb(236, 239, 244, 0.85f);
        m_guide = Rgb(160, 170, 185, 0.9f);
        m_snap = Rgb(255, 196, 40);
        m_cross = Rgb(220, 225, 232, 0.55f);
        m_labelBg = Rgb(52, 58, 70, 0.95f);
        m_labelText = Rgb(236, 239, 244);
    } else {
        m_bg = Rgb(255, 255, 255);
        m_fg = Rgb(20, 20, 24);
        m_gridMinor = Rgb(238, 240, 244);
        m_gridMajor = Rgb(216, 220, 228);
        m_axisX = Rgb(200, 80, 80, 0.8f);
        m_axisY = Rgb(60, 150, 80, 0.8f);
        m_select = Rgb(0, 100, 220);
        m_previewInk = Rgb(20, 20, 24, 0.85f);
        m_guide = Rgb(110, 115, 125, 0.9f);
        m_snap = Rgb(230, 140, 0);
        m_cross = Rgb(40, 40, 48, 0.5f);
        m_labelBg = Rgb(250, 250, 252, 0.97f);
        m_labelText = Rgb(20, 20, 24);
    }
    m_windowFill = Rgb(60, 120, 255, 0.12f);
    m_windowEdge = Rgb(60, 120, 255, 0.9f);
    m_crossFill = Rgb(40, 190, 90, 0.12f);
    m_crossEdge = Rgb(40, 190, 90, 0.9f);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

bool Canvas::EnsureTarget() {
    if (m_target) return true;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const D2D1_RENDER_TARGET_PROPERTIES props =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96.0f, 96.0f);
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hprops = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(std::max(1L, rc.right), std::max(1L, rc.bottom)), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    if (FAILED(m_factory->CreateHwndRenderTarget(props, hprops, &m_target))) return false;
    m_target->CreateSolidColorBrush(m_fg, &m_brush);
    m_layerValid = false;
    return true;
}

void Canvas::ReleaseTarget() {
    SafeRelease(m_layer);
    SafeRelease(m_brush);
    SafeRelease(m_target);
}

ID2D1PathGeometry* Canvas::BuildGeometry(const std::vector<const Entity*>& entities) {
    ID2D1PathGeometry* geo = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(m_factory->CreatePathGeometry(&geo))) return nullptr;
    if (FAILED(geo->Open(&sink))) {
        geo->Release();
        return nullptr;
    }
    const Box view = ViewBox();
    const Box cull = view.Inflated(Px(4) / m_scale);
    const Box safe = view.Inflated(kSafeCoord / m_scale);
    std::vector<Prim> prims;

    auto line = [&](Vec2 a, Vec2 b) {
        if (!safe.Contains(a) || !safe.Contains(b)) {
            if (!ClipSegment(a, b, cull)) return;
        }
        sink->BeginFigure(ToScreen(a), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(ToScreen(b));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    };
    // Arcs are drawn as short straight pieces, fine enough that they never
    // stray more than a fraction of a pixel from the true curve (the same on
    // every graphics driver, and exact at any zoom).
    auto steps = [&](const Prim& p) {
        const double rpx = p.r * m_scale;
        if (rpx < 0.5) return 4;
        const double step = 2 * std::acos(std::max(0.0, 1 - 0.2 / rpx));
        return (int)std::clamp(std::ceil(p.sweep / std::max(step, 1e-4)), 4.0, 20000.0);
    };
    // Continues the open figure along the arc, from its start (or from its end).
    auto addArc = [&](const Prim& p, bool fromStart) {
        const int n = steps(p);
        for (int i = 1; i <= n; ++i) {
            const double t = fromStart ? (double)i / n : 1.0 - (double)i / n;
            sink->AddLine(ToScreen(i == n ? (fromStart ? p.b : p.a) : Polar(p.c, p.r, p.start + p.sweep * t)));
        }
    };
    auto arc = [&](const Prim& p) {
        if (!safe.Contains(p.Bounds())) {
            // Far beyond the window: only the pieces that can be seen, clipped.
            const int n = steps(p);
            Vec2 prev = p.a;
            for (int i = 1; i <= n; ++i) {
                const Vec2 next = Polar(p.c, p.r, p.start + p.sweep * i / n);
                if (Box(prev, next).Intersects(cull)) line(prev, next);
                prev = next;
            }
            return;
        }
        sink->BeginFigure(ToScreen(p.a), D2D1_FIGURE_BEGIN_HOLLOW);
        addArc(p, true);
        sink->EndFigure(p.FullCircle() ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    };

    for (const Entity* e : entities) {
        const Box bounds = Bounds(*e);
        if (!bounds.Intersects(cull)) continue;
        switch (e->type) {
            case EntityType::Line:
                line(e->p0, e->p1);
                break;
            case EntityType::Circle:
            case EntityType::Arc:
                prims.clear();
                Primitives(*e, prims);
                arc(prims[0]);
                break;
            case EntityType::Polyline: {
                if (e->verts.empty()) break;
                if (!safe.Contains(bounds) || e->verts.size() < 2) {
                    // Far outside the window: piece by piece, clipped.
                    prims.clear();
                    Primitives(*e, prims);
                    for (const Prim& p : prims) {
                        if (!p.Bounds().Intersects(cull)) continue;
                        if (p.kind == Prim::Segment) line(p.a, p.b);
                        else arc(p);
                    }
                    break;
                }
                // One figure, so the corners are joined properly.
                sink->BeginFigure(ToScreen(e->verts[0].p), D2D1_FIGURE_BEGIN_HOLLOW);
                const size_t n = SegmentCount(*e);
                for (size_t i = 0; i < n; ++i) {
                    const Vertex& v = e->verts[i];
                    const Vertex& w = e->verts[(i + 1) % e->verts.size()];
                    if (v.bulge == 0) {
                        sink->AddLine(ToScreen(w.p));
                        continue;
                    }
                    // ArcFromBulge returns the arc counter-clockwise: a clockwise
                    // bulge is travelled from its end back to its start.
                    addArc(ArcFromBulge(v.p, w.p, v.bulge), v.bulge > 0);
                }
                sink->EndFigure(e->closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
                break;
            }
        }
    }
    sink->Close();
    sink->Release();
    return geo;
}

void Canvas::DrawEntities(ID2D1RenderTarget* rt, const std::vector<const Entity*>& ents, D2D1_COLOR_F color,
                          float width, ID2D1StrokeStyle* style) {
    if (ents.empty()) return;
    ID2D1PathGeometry* geo = BuildGeometry(ents);
    if (!geo) return;
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(color, &brush);
    if (brush) {
        rt->DrawGeometry(geo, brush, width, style);
        brush->Release();
    }
    geo->Release();
}

void Canvas::DrawGrid(ID2D1RenderTarget* rt) {
    const Drafting& d = m_engine->Draft();
    double spacing = d.gridSpacing;
    if (!(spacing > 0)) return;
    // Adaptive grid: coarser when zoomed out, so lines stay apart.
    const double minPx = Px(8);
    for (int i = 0; spacing * m_scale < minPx && i < 40; ++i) spacing *= (i % 2 == 0) ? 5 : 2;
    const Box view = ViewBox();
    const long long x0 = (long long)std::floor(view.minX / spacing), x1 = (long long)std::ceil(view.maxX / spacing);
    const long long y0 = (long long)std::floor(view.minY / spacing), y1 = (long long)std::ceil(view.maxY / spacing);
    if (x1 - x0 > 4000 || y1 - y0 > 4000) return;
    // Major lines every fifth line (counted from the origin).
    constexpr long long majorEvery = 5;
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (int pass = 0; pass < 2; ++pass) {
        m_brush->SetColor(pass == 0 ? m_gridMinor : m_gridMajor);
        for (long long i = x0; i <= x1; ++i) {
            const bool major = i % majorEvery == 0;
            if (major != (pass == 1)) continue;
            const float x = std::floor(ToScreen({i * spacing, 0}).x) + 0.5f;
            rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, (float)m_height), m_brush, 1.0f);
        }
        for (long long j = y0; j <= y1; ++j) {
            const bool major = j % majorEvery == 0;
            if (major != (pass == 1)) continue;
            const float y = std::floor(ToScreen({0, j * spacing}).y) + 0.5f;
            rt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F((float)m_width, y), m_brush, 1.0f);
        }
    }
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Canvas::DrawUcsIcon(ID2D1RenderTarget* rt) {
    // X and Y arrows at the bottom-left corner (or at the origin when it is in view).
    const float len = (float)Px(36), m = (float)Px(14);
    D2D1_POINT_2F o = ToScreen({0, 0});
    if (o.x < m || o.x > m_width - len - m || o.y < len + m || o.y > m_height - m)
        o = D2D1::Point2F(m, (float)m_height - m);
    const float w = (float)Px(1.5), h = (float)Px(5);
    m_brush->SetColor(m_axisX);
    rt->DrawLine(o, D2D1::Point2F(o.x + len, o.y), m_brush, w);
    rt->DrawLine(D2D1::Point2F(o.x + len - h, o.y - h * 0.6f), D2D1::Point2F(o.x + len, o.y), m_brush, w);
    rt->DrawLine(D2D1::Point2F(o.x + len - h, o.y + h * 0.6f), D2D1::Point2F(o.x + len, o.y), m_brush, w);
    m_brush->SetColor(m_axisY);
    rt->DrawLine(o, D2D1::Point2F(o.x, o.y - len), m_brush, w);
    rt->DrawLine(D2D1::Point2F(o.x - h * 0.6f, o.y - len + h), D2D1::Point2F(o.x, o.y - len), m_brush, w);
    rt->DrawLine(D2D1::Point2F(o.x + h * 0.6f, o.y - len + h), D2D1::Point2F(o.x, o.y - len), m_brush, w);
    if (m_textFormat) {
        m_brush->SetColor(m_axisX);
        rt->DrawText(L"X", 1, m_textFormat, D2D1::RectF(o.x + len + 3, o.y - Px(9), o.x + len + 30, o.y + 10), m_brush);
        m_brush->SetColor(m_axisY);
        rt->DrawText(L"Y", 1, m_textFormat, D2D1::RectF(o.x - Px(4), o.y - len - Px(20), o.x + 20, o.y - len), m_brush);
    }
}

void Canvas::RenderLayer() {
    if (!m_layer && FAILED(m_target->CreateCompatibleRenderTarget(&m_layer))) return;
    ID2D1BitmapRenderTarget* rt = m_layer;
    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->Clear(m_bg);
    const Drafting& d = m_engine->Draft();
    if (d.gridOn) DrawGrid(rt);
    // Axes through the origin.
    const D2D1_POINT_2F o = ToScreen({0, 0});
    m_brush->SetColor(D2D1::ColorF(m_axisX.r, m_axisX.g, m_axisX.b, 0.45f));
    rt->DrawLine(D2D1::Point2F(0, o.y), D2D1::Point2F((float)m_width, o.y), m_brush, 1.0f);
    m_brush->SetColor(D2D1::ColorF(m_axisY.r, m_axisY.g, m_axisY.b, 0.45f));
    rt->DrawLine(D2D1::Point2F(o.x, 0), D2D1::Point2F(o.x, (float)m_height), m_brush, 1.0f);

    std::vector<const Entity*> normal, selected;
    normal.reserve(m_doc->Count());
    const auto& sel = m_engine->Selection();
    for (const auto& kv : m_doc->Entities()) (sel.count(kv.first) ? selected : normal).push_back(&kv.second);
    DrawEntities(rt, normal, m_fg, 1.0f, nullptr);
    DrawEntities(rt, selected, m_select, 1.6f, m_dashed);
    DrawUcsIcon(rt);
    if (rt->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) {
        ReleaseTarget();
        return;
    }
    m_shownRevision = m_doc->Revision();
    m_shownSelection = m_engine->SelectionRevision();
    m_shownGrid = d.gridOn;
    m_shownGridSpacing = d.gridSpacing;
    m_layerValid = true;
}

void Canvas::DrawLabel(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y) {
    if (!m_textFormat || text.empty()) return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(m_dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(), m_textFormat, 800, 200, &layout))) return;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    const float pad = (float)Px(4);
    // Keep it inside the window.
    if (x + tm.width + 2 * pad > m_width) x = m_width - tm.width - 2 * pad - 2;
    if (y + tm.height + 2 * pad > m_height) y -= tm.height + 2 * pad + (float)Px(36);
    const D2D1_ROUNDED_RECT box = D2D1::RoundedRect(D2D1::RectF(x, y, x + tm.width + 2 * pad, y + tm.height + 2 * pad),
                                                    (float)Px(3), (float)Px(3));
    m_brush->SetColor(m_labelBg);
    rt->FillRoundedRectangle(box, m_brush);
    m_brush->SetColor(m_guide);
    rt->DrawRoundedRectangle(box, m_brush, 1.0f);
    m_brush->SetColor(m_labelText);
    rt->DrawTextLayout(D2D1::Point2F(x + pad, y + pad), layout, m_brush);
    layout->Release();
}

void Canvas::DrawSnapMarker(ID2D1RenderTarget* rt) {
    if (!m_snapHit.found) return;
    const D2D1_POINT_2F c = ToScreen(m_snapHit.point);
    const float s = (float)Px(6), w = (float)Px(2);
    m_brush->SetColor(m_snap);
    auto L = [&](float x0, float y0, float x1, float y1) {
        rt->DrawLine(D2D1::Point2F(c.x + x0, c.y + y0), D2D1::Point2F(c.x + x1, c.y + y1), m_brush, w);
    };
    switch (m_snapHit.mode) {
        case kSnapEndpoint:
            rt->DrawRectangle(D2D1::RectF(c.x - s, c.y - s, c.x + s, c.y + s), m_brush, w);
            break;
        case kSnapMidpoint:
            L(-s, s, s, s);
            L(s, s, 0, -s);
            L(0, -s, -s, s);
            break;
        case kSnapCentre:
            rt->DrawEllipse(D2D1::Ellipse(c, s, s), m_brush, w);
            break;
        case kSnapQuadrant:
            L(0, -s, s, 0);
            L(s, 0, 0, s);
            L(0, s, -s, 0);
            L(-s, 0, 0, -s);
            break;
        case kSnapIntersection:
            L(-s, -s, s, s);
            L(-s, s, s, -s);
            break;
        case kSnapPerpendicular:
            L(-s, s, s, s);
            L(-s, s, -s, -s);
            L(-s, 0, 0, 0);
            L(0, 0, 0, s);
            break;
        case kSnapTangent:
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + s * 0.3f), s * 0.7f, s * 0.7f), m_brush, w);
            L(-s, -s * 0.4f, s, -s * 0.4f);
            break;
        default:  // nearest: an hourglass
            L(-s, -s, s, -s);
            L(s, -s, -s, s);
            L(-s, s, s, s);
            L(s, s, -s, -s);
            break;
    }
}

void Canvas::RenderOverlay() {
    ID2D1HwndRenderTarget* rt = m_target;
    ID2D1Bitmap* bmp = nullptr;
    if (m_layer && SUCCEEDED(m_layer->GetBitmap(&bmp))) {
        rt->DrawBitmap(bmp);
        bmp->Release();
    } else {
        rt->Clear(m_bg);
    }
    const bool wantsPoint = m_engine->WantsPoint();
    const bool selecting = m_engine->WantsSelection() && !m_engine->PanMode();

    // Highlight the object under the cursor.
    if (m_hover && selecting && !m_windowOn && !m_engine->Selection().count(m_hover)) {
        if (const Entity* e = m_doc->Find(m_hover)) DrawEntities(rt, {e}, m_fg, 2.6f, nullptr);
    }
    // What the command would create.
    if (!m_preview.entities.empty()) {
        std::vector<const Entity*> ptrs;
        for (const Entity& e : m_preview.entities) ptrs.push_back(&e);
        DrawEntities(rt, ptrs, m_previewInk, 1.0f, nullptr);
    }
    for (const auto& g : m_preview.guides) {
        m_brush->SetColor(m_guide);
        rt->DrawLine(ToScreen(g.first), ToScreen(g.second), m_brush, 1.0f, m_dashed);
    }
    // Selection window.
    if (m_windowOn && m_inside) {
        const bool crossing = m_mouse.x < m_windowStart.x;
        const D2D1_RECT_F r = D2D1::RectF((float)std::min(m_windowStart.x, m_mouse.x),
                                          (float)std::min(m_windowStart.y, m_mouse.y),
                                          (float)std::max(m_windowStart.x, m_mouse.x),
                                          (float)std::max(m_windowStart.y, m_mouse.y));
        m_brush->SetColor(crossing ? m_crossFill : m_windowFill);
        rt->FillRectangle(r, m_brush);
        m_brush->SetColor(crossing ? m_crossEdge : m_windowEdge);
        rt->DrawRectangle(r, m_brush, 1.0f, crossing ? m_dashed : nullptr);
    }
    DrawSnapMarker(rt);

    // Crosshair (the Windows pointer is hidden over the drawing).
    if (m_inside && !m_engine->PanMode() && !m_panning) {
        const D2D1_POINT_2F c = wantsPoint ? ToScreen(m_tracked) : D2D1::Point2F((float)m_mouse.x, (float)m_mouse.y);
        const float x = std::floor(c.x) + 0.5f, y = std::floor(c.y) + 0.5f;
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        m_brush->SetColor(m_cross);
        rt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F((float)m_width, y), m_brush, 1.0f);
        rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, (float)m_height), m_brush, 1.0f);
        if (selecting) {
            const float b = (float)Px(kPickboxDip);
            m_brush->SetColor(m_fg);
            rt->DrawRectangle(D2D1::RectF(x - b, y - b, x + b, y + b), m_brush, 1.0f);
        }
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Tooltip: the snap found, and length and angle from the base point.
        std::wstring tip;
        if (m_snapHit.found) tip = SnapName(m_snapHit.mode);
        const Prompt& pr = m_engine->CurrentPrompt();
        if (wantsPoint && pr.hasBase && !Near(m_tracked, pr.base, 1e-12)) {
            const Vec2 d = m_tracked - pr.base;
            if (!tip.empty()) tip += L"\n";
            tip += m_engine->Len(Length(d)) + L"  <  " + m_engine->Ang(NormalizeAngle(AngleOf(d))) + L"\x00B0";
        }
        if (!tip.empty()) DrawLabel(rt, tip, c.x + (float)Px(18), c.y + (float)Px(18));
    }
}

void Canvas::Paint() {
    if (!EnsureTarget()) return;
    if (!m_layerValid) RenderLayer();
    if (!m_target) return;
    m_target->BeginDraw();
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
    RenderOverlay();
    if (m_target->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) {
        ReleaseTarget();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

uint64_t Canvas::PickAt(Vec2 w) const {
    const double tol = Px(kPickboxDip) / m_scale;
    const Box box = Box(w, w).Inflated(tol);
    double best = tol;
    uint64_t id = 0;
    for (const auto& kv : m_doc->Entities()) {
        if (!Bounds(kv.second).Intersects(box)) continue;
        const double d = DistanceTo(kv.second, w);
        if (d <= best) {  // the later (top) object wins ties
            best = d;
            id = kv.first;
        }
    }
    return id;
}

void Canvas::OnMouseMove(int x, int y, WPARAM keys) {
    if (!m_tracking) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
        m_tracking = TrackMouseEvent(&tme) != FALSE;
    }
    m_inside = true;
    if (m_panning) {
        PanBy(x - m_panLast.x, y - m_panLast.y);
        m_panLast = {x, y};
    }
    m_mouse = {x, y};
    m_raw = ToWorld(x, y);
    m_snapHit = SnapResult{};
    m_hover = 0;
    const bool shift = (keys & MK_SHIFT) || (GetKeyState(VK_SHIFT) < 0);
    if (m_engine->WantsPoint() && !m_panning) {
        const Prompt& pr = m_engine->CurrentPrompt();
        const Drafting& d = m_engine->Draft();
        unsigned modes = m_engine->SnapOverride();
        if (modes == kSnapNone) modes = 0;
        else if (!modes) modes = d.osnapOn ? d.osnapModes : 0;
        if (modes) {
            SnapQuery q;
            q.cursor = m_raw;
            q.aperture = Px(kApertureDip) / m_scale;
            q.modes = modes;
            q.hasBase = pr.hasBase;
            q.base = pr.base;
            m_snapHit = FindSnap(*m_doc, q);
        }
        Vec2 p = m_raw;
        if (m_snapHit.found) {
            p = m_snapHit.point;
        } else {
            if (d.snapOn && d.snapSpacing > 0)
                p = {std::round(p.x / d.snapSpacing) * d.snapSpacing, std::round(p.y / d.snapSpacing) * d.snapSpacing};
            // Shift turns ortho on (or off) while it is held.
            if ((d.orthoOn != shift) && pr.hasBase) {
                const Vec2 delta = p - pr.base;
                if (std::fabs(delta.x) >= std::fabs(delta.y)) p.y = pr.base.y;
                else p.x = pr.base.x;
            }
        }
        m_tracked = p;
        m_engine->GetPreview(p, m_preview);
    } else {
        m_tracked = m_raw;
        m_preview.Clear();
        if (m_engine->WantsSelection() && !m_windowOn && !m_engine->PanMode() && !m_panning) m_hover = PickAt(m_raw);
    }
    if (m_windowOn && (keys & MK_LBUTTON) &&
        (std::abs(x - m_windowStart.x) > Px(kDragDip) || std::abs(y - m_windowStart.y) > Px(kDragDip)))
        m_windowDragged = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void Canvas::FinishWindow(int x, int y, bool shift) {
    m_windowOn = false;
    if (GetCapture() == m_hwnd) ReleaseCapture();
    const bool crossing = x < m_windowStart.x;
    const Box box(ToWorld(m_windowStart.x, m_windowStart.y), ToWorld(x, y));
    std::vector<uint64_t> ids;
    for (const auto& kv : m_doc->Entities())
        if (crossing ? TouchesBox(kv.second, box) : InsideBox(kv.second, box)) ids.push_back(kv.first);
    if (shift) m_engine->RemoveFromSelection(ids);
    else m_engine->AddToSelection(ids);
    Notify();
    Refresh();
}

void Canvas::OnLeftDown(int x, int y, WPARAM keys) {
    const bool shift = (keys & MK_SHIFT) != 0;
    if (m_engine->PanMode()) {
        PushView();
        m_panning = true;
        m_panLast = {x, y};
        SetCapture(m_hwnd);
        return;
    }
    if (m_engine->WantsPoint()) {
        OnMouseMove(x, y, keys);
        m_engine->Point(m_tracked);
        Notify();
        Refresh();
        return;
    }
    if (!m_engine->WantsSelection()) return;
    if (m_windowOn) {  // second click of a click-click window
        FinishWindow(x, y, shift);
        return;
    }
    const uint64_t id = PickAt(ToWorld(x, y));
    if (id) {
        if (shift) m_engine->RemoveFromSelection({id});
        else m_engine->AddToSelection({id});
        Notify();
        Refresh();
        return;
    }
    m_windowOn = true;
    m_windowDragged = false;
    m_windowStart = {x, y};
    SetCapture(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Canvas::OnLeftUp(int x, int y, WPARAM keys) {
    if (m_panning && m_engine->PanMode()) {
        m_panning = false;
        ReleaseCapture();
        return;
    }
    if (m_windowOn && m_windowDragged) FinishWindow(x, y, (keys & MK_SHIFT) != 0);
    // A plain click leaves the window open until the second click.
}

void Canvas::UpdateCursor(WPARAM) {
    if (m_engine->PanMode() || m_panning) SetCursor(LoadCursorW(nullptr, IDC_HAND));
    else SetCursor(nullptr);  // the crosshair is drawn instead
}

LRESULT CALLBACK Canvas::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Canvas* self;
    if (msg == WM_NCCREATE) {
        self = (Canvas*)((CREATESTRUCTW*)lp)->lpCreateParams;
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (Canvas*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Canvas::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: {
            const int w = std::max(1, (int)LOWORD(lp)), h = std::max(1, (int)HIWORD(lp));
            const bool first = m_width <= 1;
            m_width = w;
            m_height = h;
            if (m_target) {
                m_target->Resize(D2D1::SizeU(w, h));
                SafeRelease(m_layer);
            }
            m_layerValid = false;
            if (first) ZoomExtents();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            Paint();
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_DISPLAYCHANGE:
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                UpdateCursor(0);
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
            return 0;
        case WM_MOUSELEAVE:
            m_tracking = false;
            if (!m_panning && !m_windowOn) {
                m_inside = false;
                m_snapHit = SnapResult{};
                m_hover = 0;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                Notify();
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            OnLeftDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
            return 0;
        case WM_LBUTTONUP:
            OnLeftUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
            return 0;
        case WM_MBUTTONDOWN:
            PushView();
            m_panning = true;
            m_panLast = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SetCapture(m_hwnd);
            UpdateCursor(0);
            return 0;
        case WM_MBUTTONUP:
            if (m_panning) {
                m_panning = false;
                ReleaseCapture();
                UpdateCursor(0);
                OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
            }
            return 0;
        case WM_MBUTTONDBLCLK:
            ZoomExtents();
            return 0;
        case WM_CAPTURECHANGED:
            if ((HWND)lp != m_hwnd) {
                m_panning = false;
                if (m_windowOn && m_windowDragged) m_windowOn = false;
            }
            return 0;
        case WM_RBUTTONUP: {
            if (m_windowOn) {  // right click abandons a selection window
                m_windowOn = false;
                if (GetCapture() == m_hwnd) ReleaseCapture();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(m_hwnd, &pt);
            SendMessageW(GetParent(m_hwnd), WM_APP_CANVAS_MENU, 0, MAKELPARAM(pt.x, pt.y));
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(m_hwnd, &pt);
            const double notches = GET_WHEEL_DELTA_WPARAM(wp) / (double)WHEEL_DELTA;
            ZoomAt(std::pow(1.25, notches), pt.x, pt.y);
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
