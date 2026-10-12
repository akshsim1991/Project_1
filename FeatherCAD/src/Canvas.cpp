// Canvas.cpp - the drawing area (see Canvas.h).
#include "Canvas.h"
#include "ImageFile.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include <windowsx.h>

#include "Annot.h"
#include "Dimension.h"
#include "Sheet.h"
#include "Spline.h"
#include "Theme.h"
#include "Util.h"

namespace {
const wchar_t kClass[] = L"FeatherCadCanvas";
constexpr double kMinScale = 1e-7, kMaxScale = 1e8;
constexpr int kPickboxDip = 5;     // half size of the pick box
constexpr int kRelationMarkDip = 14;  // relationship marks, whatever the zoom
constexpr int kApertureDip = 10;   // object snap search radius
constexpr int kDragDip = 4;        // movement that turns a click into a drag
constexpr double kSafeCoord = 1e6; // beyond this many pixels, clip before drawing
constexpr int kTrackDip = 9;       // how near a tracking line pulls the cursor onto it
constexpr UINT_PTR kDwellTimer = 7;  // resting on a snap point acquires it for tracking
constexpr size_t kMaxGripObjects = 100;
constexpr float kDimFontPx = 100;   // dimension text is laid out this size, then scaled

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
    SafeRelease(m_dimFormat);
    for (auto& kv : m_fonts) SafeRelease(kv.second);
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
    m_dwrite->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, kDimFontPx, L"", &m_dimFormat);
    if (m_dimFormat) m_dimFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

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

void Canvas::SetDrawing(Engine* engine, Document* doc) {
    m_engine = engine;
    m_doc = doc;
    // Nothing shown or cached belongs to the new drawing.
    m_layerValid = false;
    m_shownRevision = m_shownSelection = 0;
    m_gripsDocRev = m_gripsSelRev = 0;
    m_grips.clear();
    m_hoverGrip = -1;
    m_hover = 0;
    m_snapHit = SnapResult{};
    m_preview.Clear();
    m_trackLines.clear();
    m_trackLabel.clear();
    m_acquired.clear();
    StopDwell();
    if (m_windowOn) {
        m_windowOn = false;
        if (GetCapture() == m_hwnd) ReleaseCapture();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

Canvas::View Canvas::GetView() const { return {m_centre, m_scale, m_previous, true}; }

void Canvas::SetView(const View& v) {
    if (!v.set) {
        m_previous.clear();
        ZoomExtents();
        m_previous.clear();
        return;
    }
    m_centre = v.centre;
    m_scale = v.scale;
    m_previous = v.previous;
    m_layerValid = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
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
    Box ext = m_sheet ? Box({0, 0}, {m_sheet->setup.width, m_sheet->setup.height}) : m_doc->Extents();
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

double Canvas::PickRadius() const { return Px(kPickboxDip) / m_scale; }

const wchar_t* Canvas::SnapLabel() const { return m_snapHit.found ? SnapName(m_snapHit.mode) : nullptr; }

void Canvas::Notify() { SendMessageW(GetParent(m_hwnd), WM_APP_CANVAS_STATE, 0, 0); }

void Canvas::SetTypedText(const std::wstring& text) {
    if (text == m_typed) return;
    m_typed = text;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Canvas::Refresh() {
    const Drafting& d = m_engine->Draft();
    if (!m_engine->Active() || !d.otrackOn) m_acquired.clear();
    if (m_shownRevision != m_doc->Revision() || m_shownSelection != m_engine->SelectionRevision() ||
        m_shownGrid != d.gridOn || m_shownGridSpacing != d.gridSpacing || m_shownLw != d.lwDisplay)
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
    if (CurrentTheme().dark && !m_sheet) {  // a sheet is white paper whatever the theme
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
        m_removeInk = Rgb(255, 90, 90);
        m_constructionInk = Rgb(120, 150, 200, 0.8f);
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
        m_removeInk = Rgb(220, 30, 30);
        m_constructionInk = Rgb(110, 140, 190, 0.8f);
    }
    m_outside = CurrentTheme().dark ? Rgb(64, 70, 80) : Rgb(150, 155, 164);
    m_gripFill = Rgb(40, 110, 255);
    m_gripHover = Rgb(255, 120, 150);
    m_gripHot = Rgb(235, 40, 40);
    m_trackInk = Rgb(60, 200, 110, 0.9f);
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
    for (auto& kv : m_images) SafeRelease(kv.second);
    m_images.clear();
    SafeRelease(m_layer);
    SafeRelease(m_brush);
    SafeRelease(m_target);
}

ID2D1PathGeometry* Canvas::BuildGeometry(const std::vector<const Entity*>& entities, const std::vector<float>* dashes) {
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
    // Figures go through the pen: with a dash pattern (pixels: on, off, on...)
    // it lifts and lowers itself along the way, the pattern starting again at
    // each figure.
    struct Pen {
        ID2D1GeometrySink* sink;
        const std::vector<float>* dash;
        D2D1_POINT_2F start{}, last{};
        size_t index = 0;
        float left = 0;
        bool down = false;
        void Begin(D2D1_POINT_2F p) {
            start = last = p;
            if (!dash) {
                sink->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW);
                return;
            }
            index = 0;
            left = (*dash)[0];
            down = false;
        }
        void Line(D2D1_POINT_2F p) {
            if (!dash) {
                sink->AddLine(p);
                last = p;
                return;
            }
            const float dx = p.x - last.x, dy = p.y - last.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            auto advance = [&] {
                index = (index + 1) % dash->size();
                left = (*dash)[index];
            };
            float at = 0;
            int guard = 0;
            while (at < len - 1e-6f && ++guard < 100000) {
                const bool on = index % 2 == 0;
                const D2D1_POINT_2F a = {last.x + dx * at / len, last.y + dy * at / len};
                if (on && left <= 0.01f) {  // a dot: a short mark
                    if (down) sink->EndFigure(D2D1_FIGURE_END_OPEN), down = false;
                    sink->BeginFigure(a, D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine({a.x + dx / len * 0.75f, a.y + dy / len * 0.75f});
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    advance();
                    continue;
                }
                const float step = std::min(left, len - at);
                const D2D1_POINT_2F b = {last.x + dx * (at + step) / len, last.y + dy * (at + step) / len};
                if (on) {
                    if (!down) sink->BeginFigure(a, D2D1_FIGURE_BEGIN_HOLLOW), down = true;
                    sink->AddLine(b);
                }
                at += step;
                left -= step;
                if (left <= 1e-4f) {
                    if (on && down) sink->EndFigure(D2D1_FIGURE_END_OPEN), down = false;
                    advance();
                }
            }
            last = p;
        }
        void End(bool closed) {
            if (!dash) {
                sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
                return;
            }
            if (closed) Line(start);
            if (down) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            down = false;
        }
    } pen{sink, dashes && !dashes->empty() ? dashes : nullptr};

    auto line = [&](Vec2 a, Vec2 b) {
        if (!safe.Contains(a) || !safe.Contains(b)) {
            if (!ClipSegment(a, b, cull)) return;
        }
        pen.Begin(ToScreen(a));
        pen.Line(ToScreen(b));
        pen.End(false);
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
            pen.Line(ToScreen(i == n ? (fromStart ? p.b : p.a) : Polar(p.c, p.r, p.start + p.sweep * t)));
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
        pen.Begin(ToScreen(p.a));
        addArc(p, true);
        pen.End(p.FullCircle());
    };

    // Arrays are drawn as their copies.
    std::vector<Entity> expanded;
    std::vector<const Entity*> list;
    list.reserve(entities.size());
    for (const Entity* e : entities) {
        if (e->type != EntityType::Array) {
            list.push_back(e);
        } else if (Bounds(*e).Intersects(cull)) {
            ExpandArray(*e, expanded);
        }
    }
    for (const Entity& e : expanded) list.push_back(&e);

    for (const Entity* e : list) {
        const Box bounds = Bounds(*e);
        if (!bounds.Intersects(cull)) continue;
        switch (e->type) {
            case EntityType::Array:
                break;
            case EntityType::Point: {
                const double d = Px(3) / m_scale;
                line(e->p0 - Vec2{d, d}, e->p0 + Vec2{d, d});
                line(e->p0 - Vec2{d, -d}, e->p0 + Vec2{d, -d});
                break;
            }
            case EntityType::Ellipse:
            case EntityType::Spline: {
                std::vector<Vec2> pts;
                if (e->type == EntityType::Ellipse) EllipsePoints(*e, pts);
                else SplinePoints(*e, pts);
                if (pts.size() < 2) break;
                if (!safe.Contains(bounds)) {
                    for (size_t i = 0; i + 1 < pts.size(); ++i)
                        if (Box(pts[i], pts[i + 1]).Intersects(cull)) line(pts[i], pts[i + 1]);
                    break;
                }
                pen.Begin(ToScreen(pts[0]));
                for (size_t i = 1; i < pts.size(); ++i) pen.Line(ToScreen(pts[i]));
                pen.End(false);
                break;
            }
            case EntityType::Line:
                line(e->p0, e->p1);
                break;
            case EntityType::Dimension: {
                if (!e->dim) break;
                DimGraphics g;
                DimLayout(*e->dim, g);
                for (const Prim& p : g.lines) {
                    if (p.kind == Prim::Segment) line(p.a, p.b);
                    else arc(p);
                }
                break;
            }
            case EntityType::Text:
            case EntityType::Leader:
            case EntityType::Hatch:
            case EntityType::Relation: {
                AnnotGraphics g;
                AnnotLayout(*e, &m_fields, g);
                const double dot = Px(1) / m_scale;
                for (const Prim& p : g.lines) {
                    if (p.kind == Prim::Arc) arc(p);
                    else if (Near(p.a, p.b, 1e-12)) line(p.a, p.a + Vec2{dot, 0});  // a pattern's dot
                    else line(p.a, p.b);
                }
                break;
            }
            case EntityType::Insert:  // drawn as its objects (Flatten)
                break;
            case EntityType::Viewport:  // its edge
            case EntityType::Image:
                line(e->p0, {e->p1.x, e->p0.y});
                line({e->p1.x, e->p0.y}, e->p1);
                line(e->p1, {e->p0.x, e->p1.y});
                line({e->p0.x, e->p1.y}, e->p0);
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
                pen.Begin(ToScreen(e->verts[0].p));
                const size_t n = SegmentCount(*e);
                for (size_t i = 0; i < n; ++i) {
                    const Vertex& v = e->verts[i];
                    const Vertex& w = e->verts[(i + 1) % e->verts.size()];
                    if (v.bulge == 0) {
                        pen.Line(ToScreen(w.p));
                        continue;
                    }
                    // ArcFromBulge returns the arc counter-clockwise: a clockwise
                    // bulge is travelled from its end back to its start.
                    addArc(ArcFromBulge(v.p, w.p, v.bulge), v.bulge > 0);
                }
                pen.End(e->closed);
                break;
            }
        }
    }
    sink->Close();
    sink->Release();
    return geo;
}

std::vector<const Entity*> Canvas::Flatten(const std::vector<const Entity*>& ents, std::deque<Entity>& store) const {
    std::vector<const Entity*> out;
    out.reserve(ents.size());
    const Box cull = ViewBox().Inflated(Px(4) / m_scale);
    std::vector<Entity> parts;
    for (const Entity* e : ents) {
        if (e->type != EntityType::Insert && e->type != EntityType::Array) {
            out.push_back(e);
            continue;
        }
        if (!Bounds(*e).Intersects(cull)) continue;
        parts.clear();
        if (e->type == EntityType::Insert) {
            ExpandBlock(*e, parts);
        } else {
            ExpandArray(*e, parts);
            std::vector<Entity> inner;
            for (const Entity& x : parts) {
                if (x.type == EntityType::Insert) ExpandBlock(x, inner);
                else inner.push_back(x);
            }
            parts.swap(inner);
        }
        for (Entity& x : parts) {
            store.push_back(std::move(x));
            out.push_back(&store.back());
        }
    }
    return out;
}

void Canvas::SetFields(const FieldValues& f) {
    m_fields = f;
    m_layerValid = false;
}

void Canvas::DrawEntities(ID2D1RenderTarget* rt, const std::vector<const Entity*>& entsIn, D2D1_COLOR_F color,
                          float width, ID2D1StrokeStyle* style, const std::vector<float>* dashes) {
    if (entsIn.empty()) return;
    std::deque<Entity> store;
    const std::vector<const Entity*> ents = Flatten(entsIn, store);
    if (ents.empty()) return;
    ID2D1PathGeometry* geo = BuildGeometry(ents, dashes);
    if (!geo) return;
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(color, &brush);
    if (brush) {
        rt->DrawGeometry(geo, brush, width, style);
        DrawDimensionParts(rt, ents, brush);
        brush->Release();
    }
    geo->Release();
}

std::vector<float> Canvas::DashPattern(int lt, double scale, bool screen) const {
    std::vector<float> out;
    if (lt <= 0 || (size_t)lt >= LineTypes().size()) return out;
    const std::vector<double>& pat = LineTypes()[(size_t)lt].pattern;
    if (pat.empty()) return out;
    // Patterns are in millimetres: in drawing units, then pixels.
    double perMm = 1;
    switch (m_doc->Settings().units) {
        case Units::Centimetres: perMm = 0.1; break;
        case Units::Metres: perMm = 0.001; break;
        case Units::Inches: perMm = 1 / 25.4; break;
        case Units::Feet: perMm = 1 / 304.8; break;
        default: break;
    }
    const double px = screen ? Px(1.5) : perMm * scale * m_scale;
    double total = 0;
    for (double d : pat) total += std::fabs(d) * px;
    if (total < 4 || total > 1e7) return out;  // too fine to see (or absurd): solid
    for (double d : pat) out.push_back((float)(std::fabs(d) * px));
    if (out.size() % 2) out.push_back(0);
    return out;
}

void Canvas::DrawImages(ID2D1RenderTarget* rt, const std::vector<const Entity*>& ents) {
    for (const Entity* e : ents) {
        if (e->type != EntityType::Image || !e->annot) continue;
        const std::wstring& path = e->annot->image;
        auto it = m_images.find(path);
        if (it == m_images.end()) {
            ID2D1Bitmap* bmp = nullptr;
            int w = 0, h = 0;
            std::vector<uint8_t> px;
            if (ReadImageBgra(path, w, h, px)) {
                // Premultiplied alpha, as Direct2D wants it.
                for (size_t i = 0; i < px.size(); i += 4)
                    for (int c = 0; c < 3; ++c) px[i + c] = (uint8_t)(px[i + c] * px[i + 3] / 255);
                const D2D1_BITMAP_PROPERTIES props =
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                if (FAILED(rt->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h), px.data(), (UINT32)w * 4, props, &bmp))) bmp = nullptr;
            }
            it = m_images.emplace(path, bmp).first;
        }
        const D2D1_POINT_2F a = ToScreen(e->p0), b = ToScreen(e->p1);
        const D2D1_RECT_F r = D2D1::RectF(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
        if (it->second) {
            rt->DrawBitmap(it->second, r, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {  // missing: a grey cross where it would be
            m_brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.6f));
            rt->DrawLine({r.left, r.top}, {r.right, r.bottom}, m_brush, 1.0f);
            rt->DrawLine({r.left, r.bottom}, {r.right, r.top}, m_brush, 1.0f);
        }
    }
}

void Canvas::DrawStyled(ID2D1RenderTarget* rt, const std::vector<const Entity*>& entsIn) {
    // Blocks' objects have colours of their own.
    std::deque<Entity> store;
    const std::vector<const Entity*> ents = Flatten(entsIn, store);
    DrawImages(rt, ents);  // pictures under the lines
    // Objects that look alike are drawn together.
    struct Key {
        uint32_t rgb;
        bool fg, construction, locked;
        int lt, lw;
        double lts;
        bool operator<(const Key& o) const {
            return std::tie(rgb, fg, construction, locked, lt, lw, lts) <
                   std::tie(o.rgb, o.fg, o.construction, o.locked, o.lt, o.lw, o.lts);
        }
    };
    std::map<Key, std::vector<const Entity*>> groups;
    const bool lwOn = m_engine->Draft().lwDisplay;
    for (const Entity* e : ents) {
        const Look k = m_doc->LookOf(*e);
        groups[{k.rgb, k.foreground, k.construction, k.locked, k.linetype, lwOn ? k.lineweight : 0, k.ltscale}].push_back(e);
    }
    for (const auto& g : groups) {
        const Key& k = g.first;
        D2D1_COLOR_F color = k.fg ? m_fg : Rgb((k.rgb >> 16) & 255, (k.rgb >> 8) & 255, k.rgb & 255);
        float width = 1.0f;
        std::vector<float> dashes;
        if (k.construction) {
            color = m_constructionInk;
            dashes = DashPattern(FindLineType(L"CONSTRUCTION"), 1, true);  // the same at any zoom
        } else {
            // Line weight on screen, so that each step looks different: 0.25 mm
            // (the default) is one pixel, 0.5 mm three, 1 mm seven, 2 mm fifteen;
            // thinner ones are drawn lighter than one pixel.
            if (lwOn) width = (float)(ScreenLineWidth(k.lw) * m_dpi / 96.0);
            dashes = DashPattern(k.lt, k.lts);
        }
        if (k.locked) color = D2D1::ColorF((color.r + m_bg.r) / 2, (color.g + m_bg.g) / 2, (color.b + m_bg.b) / 2, color.a);
        DrawEntities(rt, g.second, color, width, nullptr, &dashes);
    }
}

void Canvas::DrawDimensionParts(ID2D1RenderTarget* rt, const std::vector<const Entity*>& ents, ID2D1Brush* brush) {
    std::vector<const Entity*> dims, annots;
    const Box cull = ViewBox().Inflated(Px(4) / m_scale);
    for (const Entity* e : ents) {
        if (e->type == EntityType::Dimension && e->dim && Bounds(*e).Intersects(cull)) dims.push_back(e);
        if ((e->type == EntityType::Text || e->type == EntityType::Leader || e->type == EntityType::Hatch ||
             e->type == EntityType::Relation) && e->annot &&
            Bounds(*e).Intersects(cull))
            annots.push_back(e);
    }
    if (dims.empty() && annots.empty()) return;
    std::vector<AnnotGraphics> ag(annots.size());
    for (size_t i = 0; i < annots.size(); ++i) AnnotLayout(*annots[i], &m_fields, ag[i]);
    // Solid fills: each one even-odd (islands stay empty).
    for (const AnnotGraphics& g : ag) {
        if (!g.solid || g.fills.empty()) continue;
        ID2D1PathGeometry* fill = nullptr;
        ID2D1GeometrySink* fs = nullptr;
        if (SUCCEEDED(m_factory->CreatePathGeometry(&fill)) && SUCCEEDED(fill->Open(&fs))) {
            fs->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
            for (const auto& loop : g.fills) {
                if (loop.size() < 3) continue;
                fs->BeginFigure(ToScreen(loop[0]), D2D1_FIGURE_BEGIN_FILLED);
                for (size_t k = 1; k < loop.size(); ++k) fs->AddLine(ToScreen(loop[k]));
                fs->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            fs->Close();
            fs->Release();
            rt->FillGeometry(fill, brush);
        }
        SafeRelease(fill);
    }
    // Filled arrows, all in one geometry.
    ID2D1PathGeometry* geo = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(m_factory->CreatePathGeometry(&geo)) && SUCCEEDED(geo->Open(&sink))) {
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        for (const Entity* e : dims) {
            DimGraphics g;
            DimLayout(*e->dim, g);
            for (const auto& a : g.arrows) {
                sink->BeginFigure(ToScreen(a[0]), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(ToScreen(a[1]));
                sink->AddLine(ToScreen(a[2]));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
        }
        for (const AnnotGraphics& g : ag) {  // leader arrows
            if (g.solid) continue;
            for (const auto& f : g.fills) {
                if (f.size() < 3) continue;
                sink->BeginFigure(ToScreen(f[0]), D2D1_FIGURE_BEGIN_FILLED);
                for (size_t k = 1; k < f.size(); ++k) sink->AddLine(ToScreen(f[k]));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
        }
        sink->Close();
        sink->Release();
        rt->FillGeometry(geo, brush);
    }
    SafeRelease(geo);
    for (const AnnotGraphics& g : ag)
        for (const TextItem& t : g.texts) DrawTextItem(rt, t, brush);
    // Text: laid out once at a fixed size, then scaled and turned into place.
    if (!m_dimFormat) return;
    D2D1_MATRIX_3X2_F saved;
    rt->GetTransform(&saved);
    for (const Entity* e : dims) {
        DimGraphics g;
        DimLayout(*e->dim, g);
        const double px = g.textHeight * m_scale;
        if (g.text.empty() || px < 2.5 || px > 5000) continue;  // too small to read, or absurdly big
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(m_dwrite->CreateTextLayout(g.text.c_str(), (UINT32)g.text.size(), m_dimFormat, 100000, 1000, &layout)))
            continue;
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        DWRITE_LINE_METRICS lm{};
        UINT32 lines = 0;
        layout->GetLineMetrics(&lm, 1, &lines);
        const float k = (float)(FontSizeForHeight(g.textHeight) * m_scale / kDimFontPx);
        // The middle of the capitals sits on the text position.
        const float capMiddle = lm.baseline - (float)(0.716 * kDimFontPx / 2);
        const D2D1_POINT_2F at = ToScreen(g.textPos);
        rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k) *
                         D2D1::Matrix3x2F::Rotation((float)-ToDegrees(g.textAngle)) *
                         D2D1::Matrix3x2F::Translation(at.x, at.y) * saved);
        rt->DrawTextLayout(D2D1::Point2F(-tm.width / 2 - tm.left, -capMiddle), layout, brush);
        layout->Release();
    }
    rt->SetTransform(saved);
}

IDWriteTextFormat* Canvas::FontFormat(const std::wstring& font) {
    if (font.empty() || font == L"Arial") return m_dimFormat;
    auto it = m_fonts.find(font);
    if (it != m_fonts.end()) return it->second ? it->second : m_dimFormat;
    IDWriteTextFormat* f = nullptr;
    m_dwrite->CreateTextFormat(font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, kDimFontPx, L"", &f);
    if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_fonts[font] = f;
    return f ? f : m_dimFormat;
}

void Canvas::DrawTextItem(ID2D1RenderTarget* rt, const TextItem& t, ID2D1Brush* brush) {
    const double px = t.height * m_scale;
    if (t.text.empty() || px < 1.5 || px > 5000) return;  // too small to read, or absurdly big
    IDWriteTextFormat* format = FontFormat(t.font);
    if (!format) return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(m_dwrite->CreateTextLayout(t.text.c_str(), (UINT32)t.text.size(), format, 100000, 1000, &layout))) return;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    DWRITE_LINE_METRICS lm{};
    UINT32 lines = 0;
    layout->GetLineMetrics(&lm, 1, &lines);
    const float k = (float)(FontSizeForHeight(t.height) * m_scale / kDimFontPx);
    const float wf = (float)(t.widthFactor > 0 ? t.widthFactor : 1);
    const float capMiddle = lm.baseline - (float)(0.716 * kDimFontPx / 2);
    const float x = t.align == 0 ? -tm.left : t.align == 1 ? -tm.width / 2 - tm.left : -tm.width - tm.left;
    D2D1_MATRIX_3X2_F saved;
    rt->GetTransform(&saved);
    const D2D1_POINT_2F at = ToScreen(t.pos);
    rt->SetTransform(D2D1::Matrix3x2F::Scale(k * wf, k) * D2D1::Matrix3x2F::Rotation((float)-ToDegrees(t.angle)) *
                     D2D1::Matrix3x2F::Translation(at.x, at.y) * saved);
    rt->DrawTextLayout(D2D1::Point2F(x, -capMiddle), layout, brush);
    rt->SetTransform(saved);
    layout->Release();
}

void Canvas::SetSheet(const Sheet* sheet, const Document* model) {
    m_sheet = sheet;
    m_model = model;
    LoadColours();
    m_layerValid = false;
}

void Canvas::DrawPaper(ID2D1RenderTarget* rt) {
    rt->Clear(m_outside);
    const SheetSetup& s = m_sheet->setup;
    const D2D1_POINT_2F a = ToScreen({0, s.height}), b = ToScreen({s.width, 0});
    // The page with a soft shadow, and its printable edge (dotted).
    const float sh = (float)Px(4);
    m_brush->SetColor(D2D1::ColorF(0, 0, 0, 0.25f));
    rt->FillRectangle(D2D1::RectF(a.x + sh, a.y + sh, b.x + sh, b.y + sh), m_brush);
    m_brush->SetColor(m_bg);
    rt->FillRectangle(D2D1::RectF(a.x, a.y, b.x, b.y), m_brush);
    const D2D1_POINT_2F ma = ToScreen({s.margin, s.height - s.margin}), mb = ToScreen({s.width - s.margin, s.margin});
    m_brush->SetColor(Rgb(190, 195, 205));
    rt->DrawRectangle(D2D1::RectF(ma.x, ma.y, mb.x, mb.y), m_brush, 1.0f, m_dashed);
    if (!m_model) return;
    // The drawing through each viewport, cut off at its edges.
    std::vector<const Entity*> model;
    for (const auto& kv : m_model->Entities())
        if (m_model->Visible(kv.second)) model.push_back(&kv.second);
    for (const auto& kv : m_doc->Entities()) {
        const Entity& vp = kv.second;
        if (vp.type != EntityType::Viewport || !vp.annot || !m_doc->Visible(vp)) continue;
        const D2D1_POINT_2F p = ToScreen(vp.p0), q = ToScreen(vp.p1);
        const D2D1_RECT_F clip = D2D1::RectF(std::min(p.x, q.x), std::min(p.y, q.y), std::max(p.x, q.x), std::max(p.y, q.y));
        if (clip.right < 0 || clip.bottom < 0 || clip.left > m_width || clip.top > m_height) continue;
        rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        // Seen through it, the drawing is a view of its own.
        const Vec2 savedCentre = m_centre;
        const double savedScale = m_scale;
        Document* savedDoc = m_doc;
        const Vec2 c = Lerp(vp.p0, vp.p1, 0.5);
        m_centre = vp.annot->viewCentre + (m_centre - c) / vp.annot->viewScale;
        m_scale *= vp.annot->viewScale;
        m_doc = const_cast<Document*>(m_model);
        SetRelationMarkSize(Px(kRelationMarkDip) / m_scale);
        DrawStyled(rt, model);
        SetRelationMarkSize(Px(kRelationMarkDip) / savedScale);
        m_doc = savedDoc;
        m_centre = savedCentre;
        m_scale = savedScale;
        rt->PopAxisAlignedClip();
    }
}

void Canvas::DrawGrid(ID2D1RenderTarget* rt) {
    const Drafting& d = m_engine->Draft();
    double spacing = d.gridSpacing;
    if (!(spacing > 0)) return;
    // Adaptive grid: coarser when zoomed out, so lines stay apart.
    const double minPx = Px(8);
    for (int i = 0; spacing * m_scale < minPx && i < 40; ++i) spacing *= (i % 2 == 0) ? 5 : 2;
    const Box view = ViewBox();
    const Vec2 org = m_doc->Settings().origin;  // the grid starts at the origin
    const long long x0 = (long long)std::floor((view.minX - org.x) / spacing), x1 = (long long)std::ceil((view.maxX - org.x) / spacing);
    const long long y0 = (long long)std::floor((view.minY - org.y) / spacing), y1 = (long long)std::ceil((view.maxY - org.y) / spacing);
    if (x1 - x0 > 4000 || y1 - y0 > 4000) return;
    // Major lines every fifth line (counted from the origin).
    constexpr long long majorEvery = 5;
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (int pass = 0; pass < 2; ++pass) {
        m_brush->SetColor(pass == 0 ? m_gridMinor : m_gridMajor);
        for (long long i = x0; i <= x1; ++i) {
            const bool major = i % majorEvery == 0;
            if (major != (pass == 1)) continue;
            const float x = std::floor(ToScreen({org.x + i * spacing, 0}).x) + 0.5f;
            rt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, (float)m_height), m_brush, 1.0f);
        }
        for (long long j = y0; j <= y1; ++j) {
            const bool major = j % majorEvery == 0;
            if (major != (pass == 1)) continue;
            const float y = std::floor(ToScreen({0, org.y + j * spacing}).y) + 0.5f;
            rt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F((float)m_width, y), m_brush, 1.0f);
        }
    }
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Canvas::DrawUcsIcon(ID2D1RenderTarget* rt) {
    // X and Y arrows at the bottom-left corner (or at the origin when it is in view).
    const float len = (float)Px(36), m = (float)Px(14);
    D2D1_POINT_2F o = ToScreen(m_doc->Settings().origin);
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
    const Drafting& d = m_engine->Draft();
    if (m_sheet) {
        DrawPaper(rt);
    } else {
        rt->Clear(m_bg);
        if (d.gridOn) DrawGrid(rt);
        // Axes through the origin.
        const D2D1_POINT_2F o = ToScreen(m_doc->Settings().origin);
        m_brush->SetColor(D2D1::ColorF(m_axisX.r, m_axisX.g, m_axisX.b, 0.45f));
        rt->DrawLine(D2D1::Point2F(0, o.y), D2D1::Point2F((float)m_width, o.y), m_brush, 1.0f);
        m_brush->SetColor(D2D1::ColorF(m_axisY.r, m_axisY.g, m_axisY.b, 0.45f));
        rt->DrawLine(D2D1::Point2F(o.x, 0), D2D1::Point2F(o.x, (float)m_height), m_brush, 1.0f);
    }

    std::vector<const Entity*> normal, selected;
    normal.reserve(m_doc->Count());
    const auto& sel = m_engine->Selection();
    for (const auto& kv : m_doc->Entities()) {
        if (!m_doc->Visible(kv.second)) continue;  // layer off or frozen
        (sel.count(kv.first) ? selected : normal).push_back(&kv.second);
    }
    DrawStyled(rt, normal);
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
    m_shownLw = d.lwDisplay;
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
        case kSnapClose: {  // a green ring with a dot: "close the shape here"
            m_brush->SetColor(D2D1::ColorF(0.1f, 0.65f, 0.25f));
            rt->DrawEllipse(D2D1::Ellipse(c, s * 1.4f, s * 1.4f), m_brush, w);
            rt->FillEllipse(D2D1::Ellipse(c, s * 0.55f, s * 0.55f), m_brush);
            break;
        }
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
    const bool picking = m_engine->WantsPoint() && m_engine->CurrentPrompt().pickObject;
    const bool wantsPoint = m_engine->WantsPoint() && !picking;
    const bool selecting = (m_engine->WantsSelection() || picking) && !m_engine->PanMode();

    // Highlight the object under the cursor.
    if (m_hover && selecting && !m_windowOn && !m_engine->Selection().count(m_hover)) {
        if (const Entity* e = m_doc->Find(m_hover)) DrawEntities(rt, {e}, m_fg, 2.6f, nullptr);
    }
    // What the command would take away (trim), in red and dashed.
    if (!m_preview.removed.empty()) {
        std::vector<const Entity*> ptrs;
        for (const Entity& e : m_preview.removed) ptrs.push_back(&e);
        DrawEntities(rt, ptrs, m_removeInk, 2.0f, m_dashed);
    }
    // What the command would create.
    if (!m_preview.entities.empty()) {
        std::vector<const Entity*> ptrs;
        for (const Entity& e : m_preview.entities) ptrs.push_back(&e);
        DrawEntities(rt, ptrs, m_previewInk, 1.0f, nullptr);
    }
    // A FreeSketch stroke as it is drawn.
    if (m_stroking && m_stroke.size() >= 2) {
        m_brush->SetColor(m_previewInk);
        for (size_t i = 1; i < m_stroke.size(); ++i) rt->DrawLine(ToScreen(m_stroke[i - 1]), ToScreen(m_stroke[i]), m_brush, 2.0f);
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
    DrawTracking(rt);
    DrawGrips(rt);
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

        // Tooltip: the snap or tracking found; with dynamic input the prompt
        // and what is being typed; length and angle from the base point.
        std::wstring tip;
        auto line = [&](const std::wstring& l) {
            if (l.empty()) return;
            if (!tip.empty()) tip += L"\n";
            tip += l;
        };
        if (!m_trackLabel.empty()) line(m_trackLabel);
        else if (m_snapHit.found) line(SnapName(m_snapHit.mode));
        const Prompt& pr = m_engine->CurrentPrompt();
        if (m_engine->Draft().dynOn && m_engine->Active() && !pr.text.empty())
            line(pr.text + (m_typed.empty() ? std::wstring() : L":  " + m_typed + L"_"));
        if (wantsPoint && pr.hasBase && m_trackLabel.empty() && !Near(m_tracked, pr.base, 1e-12)) {
            const Vec2 d = m_tracked - pr.base;
            line(L"Length " + m_engine->Len(Length(d)) + L"    Angle " + m_engine->Ang(NormalizeAngle(AngleOf(d))) + L"\x00B0");
        }
        if (!tip.empty()) DrawLabel(rt, tip, c.x + (float)Px(18), c.y + (float)Px(18));
    }
}

void Canvas::Paint() {
    if (!EnsureTarget()) return;
    SetRelationMarkSize(Px(kRelationMarkDip) / m_scale);
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
    SetRelationMarkSize(Px(kRelationMarkDip) / m_scale);
    const double tol = Px(kPickboxDip) / m_scale;
    const Box box = Box(w, w).Inflated(tol);
    double best = tol;
    uint64_t id = 0;
    for (const auto& kv : m_doc->Entities()) {
        if (!m_doc->Editable(kv.second) || !Bounds(kv.second).Intersects(box)) continue;
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
    if (m_stroking) {  // FreeSketch: follow the hand, no snapping
        if (m_stroke.empty() || Dist(m_stroke.back(), m_raw) * m_scale >= 1.5) m_stroke.push_back(m_raw);
        m_tracked = m_raw;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    m_snapHit = SnapResult{};
    m_hover = 0;
    const bool shift = (keys & MK_SHIFT) || (GetKeyState(VK_SHIFT) < 0);
    if (m_engine->WantsPoint() && m_engine->CurrentPrompt().pickObject && !m_panning) {
        // Picking an object: the raw point, the object under it highlighted.
        m_tracked = m_raw;
        m_hover = PickAt(m_raw);
        m_engine->GetPreview(m_raw, m_preview);
    } else if (m_engine->WantsPoint() && !m_panning) {
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
        // Back at the start of the shape being drawn: closing it comes first.
        if (pr.hasClose && Dist(m_raw, pr.closePoint) * m_scale <= Px(kApertureDip) * 1.5) {
            m_snapHit = SnapResult{};
            m_snapHit.found = true;
            m_snapHit.mode = kSnapClose;
            m_snapHit.point = pr.closePoint;
        }
        Vec2 p = m_raw;
        m_trackLines.clear();
        m_trackLabel.clear();
        const bool ortho = d.orthoOn != shift;  // Shift turns ortho on (or off) while it is held
        if (m_snapHit.found) {
            p = m_snapHit.point;
            if (d.otrackOn) StartDwell(p);
        } else {
            StopDwell();
            if (!ResolveTracking(pr, d, p)) {
                if (d.snapOn && d.snapSpacing > 0) {
                    const Vec2 org = m_doc->Settings().origin;
                    p = {org.x + std::round((p.x - org.x) / d.snapSpacing) * d.snapSpacing,
                         org.y + std::round((p.y - org.y) / d.snapSpacing) * d.snapSpacing};
                }
                if (ortho && pr.hasBase) {
                    const Vec2 delta = p - pr.base;
                    if (std::fabs(delta.x) >= std::fabs(delta.y)) p.y = pr.base.y;
                    else p.x = pr.base.x;
                }
            }
        }
        m_tracked = p;
        m_engine->GetPreview(p, m_preview);
    } else {
        m_tracked = m_raw;
        m_preview.Clear();
        m_trackLines.clear();
        m_trackLabel.clear();
        StopDwell();
        m_hoverGrip = -1;
        if (m_engine->WantsSelection() && !m_windowOn && !m_engine->PanMode() && !m_panning) {
            UpdateGrips();
            m_hoverGrip = GripAt(m_raw);
            if (m_hoverGrip >= 0) m_tracked = m_grips[(size_t)m_hoverGrip].p;
            else m_hover = PickAt(m_raw);
        }
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
    if (m_engine->WantsStroke()) {
        m_stroking = true;
        m_stroke.clear();
        m_strokeStart = {x, y};
        m_stroke.push_back(ToWorld(x, y));
        SetCapture(m_hwnd);
        return;
    }
    if (m_engine->WantsPoint()) {
        OnMouseMove(x, y, keys);
        m_engine->Point(m_tracked, shift);
        Notify();
        Refresh();
        return;
    }
    if (!m_engine->WantsSelection()) return;
    if (m_windowOn) {  // second click of a click-click window
        FinishWindow(x, y, shift);
        return;
    }
    if (!m_engine->Active()) {
        UpdateGrips();
        const int g = GripAt(ToWorld(x, y));
        if (g >= 0) {  // a grip: edit with it
            m_hotGripPos = m_grips[(size_t)g].p;
            m_engine->StartGripEdit(m_grips[(size_t)g]);
            Notify();
            Refresh();
            return;
        }
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
    if (m_stroking) {
        m_stroking = false;
        if (GetCapture() == m_hwnd) ReleaseCapture();
        std::vector<Vec2> stroke;
        stroke.swap(m_stroke);
        stroke.push_back(ToWorld(x, y));
        const bool click = std::abs(x - m_strokeStart.x) + std::abs(y - m_strokeStart.y) < 4 && stroke.size() < 6;
        if (click) m_engine->Point(stroke.front(), false);
        else m_engine->Stroke(stroke);
        Notify();
        Refresh();
        return;
    }
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
            // The first size with real area (not the empty one at creation).
            const bool first = (m_width <= 1 || m_height <= 1) && LOWORD(lp) > 1 && HIWORD(lp) > 1;
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
        case WM_TIMER:
            if (wp == kDwellTimer) {
                KillTimer(m_hwnd, kDwellTimer);
                m_dwellOn = false;
                if (m_snapHit.found && Near(m_snapHit.point, m_dwellPoint, 1e-9)) {
                    // Rest on a point to acquire it for tracking; again to drop it.
                    auto it = std::find_if(m_acquired.begin(), m_acquired.end(),
                                           [&](Vec2 a) { return Near(a, m_dwellPoint, 1e-9); });
                    if (it != m_acquired.end()) m_acquired.erase(it);
                    else m_acquired.push_back(m_dwellPoint);
                    if (m_acquired.size() > 7) m_acquired.erase(m_acquired.begin());
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            return 0;
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
        case WM_LBUTTONDBLCLK: {
            // Double-clicking a dimension, text, leader or block edits it.
            const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (!m_engine->Active() || m_engine->ActiveName() == L"GRIP") {
                const uint64_t id = PickAt(ToWorld(x, y));
                const Entity* e = id ? m_doc->Find(id) : nullptr;
                if (e && (e->type == EntityType::Dimension || e->type == EntityType::Text || e->type == EntityType::Leader ||
                          e->type == EntityType::Insert)) {
                    if (m_engine->Active()) m_engine->Cancel();
                    if (m_windowOn) {
                        m_windowOn = false;
                        if (GetCapture() == m_hwnd) ReleaseCapture();
                    }
                    SendMessageW(GetParent(m_hwnd), WM_APP_CANVAS_EDIT, 0, (LPARAM)&id);
                    return 0;
                }
            }
            OnLeftDown(x, y, wp);
            return 0;
        }
        case WM_LBUTTONDOWN:
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
                if (m_stroking) {  // lost the mouse part way: the stroke is dropped
                    m_stroking = false;
                    m_stroke.clear();
                }
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

// ---------------------------------------------------------------------------
// Grips
// ---------------------------------------------------------------------------

bool Canvas::GripsVisible() const {
    const size_t n = m_engine->Selection().size();
    if (n == 0 || n > kMaxGripObjects) return false;
    return !m_engine->Active() || m_engine->ActiveName() == L"GRIP";
}

void Canvas::UpdateGrips() {
    if (m_gripsDocRev == m_doc->Revision() && m_gripsSelRev == m_engine->SelectionRevision()) return;
    m_gripsDocRev = m_doc->Revision();
    m_gripsSelRev = m_engine->SelectionRevision();
    m_grips.clear();
    if (m_engine->Selection().size() > kMaxGripObjects) return;
    for (uint64_t id : m_engine->Selection())
        if (const Entity* e = m_doc->Find(id)) GripsOf(*e, m_grips);
}

int Canvas::GripAt(Vec2 w) const {
    if (!GripsVisible()) return -1;
    const double tol = Px(kPickboxDip + 1) / m_scale;
    int best = -1;
    double bestDist = tol;
    for (size_t i = 0; i < m_grips.size(); ++i) {
        const double d = std::max(std::fabs(m_grips[i].p.x - w.x), std::fabs(m_grips[i].p.y - w.y));
        if (d <= bestDist) {
            bestDist = d;
            best = (int)i;
        }
    }
    return best;
}

void Canvas::DrawGrips(ID2D1RenderTarget* rt) {
    if (!GripsVisible()) return;
    UpdateGrips();
    const float h = (float)Px(4);
    const bool editing = m_engine->ActiveName() == L"GRIP";
    for (size_t i = 0; i < m_grips.size(); ++i) {
        const D2D1_POINT_2F c = ToScreen(m_grips[i].p);
        if (c.x < -h || c.y < -h || c.x > m_width + h || c.y > m_height + h) continue;
        const D2D1_RECT_F r = D2D1::RectF(std::floor(c.x) - h + 0.5f, std::floor(c.y) - h + 0.5f,
                                          std::floor(c.x) + h + 0.5f, std::floor(c.y) + h + 0.5f);
        D2D1_COLOR_F fill = m_gripFill;
        if (editing && Near(m_grips[i].p, m_hotGripPos, 1e-9)) fill = m_gripHot;
        else if ((int)i == m_hoverGrip) fill = m_gripHover;
        m_brush->SetColor(fill);
        rt->FillRectangle(r, m_brush);
        m_brush->SetColor(D2D1::ColorF(0, 0, 0, 0.55f));
        rt->DrawRectangle(r, m_brush, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Polar tracking and object snap tracking
// ---------------------------------------------------------------------------

void Canvas::StartDwell(Vec2 p) {
    if (!m_typed.empty()) return;  // not while a value is being typed
    if (m_dwellOn && Near(p, m_dwellPoint, 1e-9)) return;
    m_dwellPoint = p;
    m_dwellOn = true;
    SetTimer(m_hwnd, kDwellTimer, 450, nullptr);
}

void Canvas::StopDwell() {
    if (!m_dwellOn) return;
    KillTimer(m_hwnd, kDwellTimer);
    m_dwellOn = false;
}

bool Canvas::ResolveTracking(const Prompt& pr, const Drafting& d, Vec2& p) {
    struct Cand {
        TrackLine line;
        double dist;
    };
    std::vector<Cand> cands;
    const double tol = Px(kTrackDip) / m_scale;
    const bool ortho = d.orthoOn != ((GetKeyState(VK_SHIFT) < 0));
    std::vector<double> angles;
    if (d.polarOn && !ortho && d.polarAngle > 0) {
        const int n = std::min(720, (int)std::floor(360.0 / d.polarAngle + 1e-9));
        for (int k = 0; k < n; ++k) angles.push_back(ToRadians(d.polarAngle * k));
    }
    auto consider = [&](Vec2 o, double ang, bool ray) {
        const Vec2 dir{std::cos(ang), std::sin(ang)};
        const Vec2 v = m_raw - o;
        if (ray && Dot(v, dir) <= tol) return;
        const double dist = std::fabs(Cross(dir, v));
        if (dist <= tol) cands.push_back({{o, dir, ray}, dist});
    };
    if (pr.hasBase)
        for (double a : angles) consider(pr.base, a, true);
    if (d.otrackOn) {
        std::vector<double> lineAngles;
        for (double a : angles)
            if (a < kPi - 1e-9) lineAngles.push_back(a);
        if (lineAngles.empty()) lineAngles = {0, kPi / 2};
        for (Vec2 a : m_acquired)
            for (double ang : lineAngles) consider(a, ang, false);
    }
    if (cands.empty()) return false;
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
    const TrackLine& first = cands[0].line;
    p = first.origin + first.dir * Dot(m_raw - first.origin, first.dir);
    m_trackLines = {first};
    // Two tracking lines crossing near the cursor: their crossing point.
    for (size_t i = 1; i < cands.size(); ++i) {
        const TrackLine& second = cands[i].line;
        const double den = Cross(first.dir, second.dir);
        if (std::fabs(den) < 1e-9) continue;
        const double t = Cross(second.origin - first.origin, second.dir) / den;
        const Vec2 x = first.origin + first.dir * t;
        if (Dist(x, m_raw) <= tol * 1.5) {
            p = x;
            m_trackLines.push_back(second);
        }
        break;
    }
    const TrackLine& shown = m_trackLines[0];
    const Vec2 from = m_trackLines.size() > 1 || !shown.ray ? shown.origin : pr.base;
    const Vec2 dv = p - from;
    m_trackLabel = std::wstring(shown.ray ? L"Polar: " : L"Tracking: ") + m_engine->Len(Length(dv)) + L"  <  " +
                   m_engine->Ang(NormalizeAngle(AngleOf(dv))) + L"\x00B0";
    if (m_trackLines.size() > 1) m_trackLabel = L"Tracking crossing";
    return true;
}

void Canvas::DrawTracking(ID2D1RenderTarget* rt) {
    const float s = (float)Px(4);
    m_brush->SetColor(m_trackInk);
    // Acquired points: small crosses.
    for (Vec2 a : m_acquired) {
        const D2D1_POINT_2F c = ToScreen(a);
        rt->DrawLine(D2D1::Point2F(c.x - s, c.y), D2D1::Point2F(c.x + s, c.y), m_brush, 1.5f);
        rt->DrawLine(D2D1::Point2F(c.x, c.y - s), D2D1::Point2F(c.x, c.y + s), m_brush, 1.5f);
    }
    if (m_trackLines.empty() || !m_engine->WantsPoint()) return;
    const double reach = (m_width + m_height) / m_scale * 2;
    for (const TrackLine& l : m_trackLines) {
        const Vec2 a = l.ray ? l.origin : l.origin - l.dir * reach;
        const Vec2 b = l.origin + l.dir * reach;
        Vec2 ca = a, cb = b;
        if (!ClipSegment(ca, cb, ViewBox().Inflated(Px(2) / m_scale))) continue;
        rt->DrawLine(ToScreen(ca), ToScreen(cb), m_brush, 1.0f, m_dashed);
    }
}
