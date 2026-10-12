// Canvas.h - the drawing area, drawn with Direct2D.
//
// The drawing (grid, axes and objects) is rendered into an off-screen
// layer that is only rebuilt when the drawing, the selection or the view
// changes. Moving the mouse just redraws the light overlay on top of it:
// crosshair, snap marker, rubber-band preview and the selection window.
//
// Mouse: left click picks points or objects (drag or click-click for a
// selection window: left to right selects what is inside, right to left
// what it crosses), wheel zooms at the cursor, middle drag pans, middle
// double-click zooms to the extents, right click opens the context menu.
#pragma once
#include <d2d1.h>
#include <deque>
#include <dwrite.h>
#include <map>

#include "Common.h"
#include "Engine.h"

class Canvas {
public:
    ~Canvas();
    bool Create(HWND parent, Engine* engine, Document* doc);
    HWND Hwnd() const { return m_hwnd; }

    // ---- drawings (tabs) -------------------------------------------------------
    // Shows another drawing: its engine and document.
    void SetDrawing(Engine* engine, Document* doc);
    struct View {
        Vec2 centre;
        double scale = 1;
        std::vector<std::pair<Vec2, double>> previous;
        bool set = false;  // false: never shown (zoom to its extents)
    };
    View GetView() const;
    void SetView(const View& v);

    // Values of text fields ({filename}, {date}...).
    void SetFields(const FieldValues& f);
    // Showing a sheet (its document is the one given to SetDrawing): drawn as
    // paper, with `model` seen through its viewports. nullptr: the drawing.
    void SetSheet(const struct Sheet* sheet, const Document* model);

    // ---- view ---------------------------------------------------------------
    void ZoomExtents();
    void ZoomWindow(const Box& box, bool remember = true);
    void ZoomPrevious();
    void ZoomFactor(double factor);  // around the centre of the view
    void PanBy(int dx, int dy);      // pixels
    Box ViewBox() const;

    // Call after anything changed (drawing, selection, drafting settings,
    // command state): redraws what needs it.
    void Refresh();
    void OnThemeChanged();
    void OnDpiChanged();

    // The point the next click would give (snapped), and whether the mouse
    // is over the drawing.
    bool CursorPoint(Vec2& p) const;
    Vec2 TrackedCursor() const { return m_tracked; }
    // How far from an object a click still picks it (drawing units).
    double PickRadius() const;
    // The snap found under the cursor (for the status bar), or nullptr.
    const wchar_t* SnapLabel() const;
    // What is being typed in the command line (shown at the cursor with
    // dynamic input).
    void SetTypedText(const std::wstring& text);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // coordinates
    Vec2 ToWorld(double sx, double sy) const;
    D2D1_POINT_2F ToScreen(Vec2 w) const;
    double Px(double dip) const { return dip * m_dpi / 96.0; }

    // rendering
    bool EnsureTarget();
    void ReleaseTarget();
    void Paint();
    void RenderLayer();
    void RenderOverlay();
    void DrawGrid(ID2D1RenderTarget* rt);
    void DrawUcsIcon(ID2D1RenderTarget* rt);
    void DrawPaper(ID2D1RenderTarget* rt);  // a sheet's page, and the drawing in its viewports
    // With `dashes` (pixels: on, off, on...) the lines are cut into dashes.
    ID2D1PathGeometry* BuildGeometry(const std::vector<const Entity*>& entities, const std::vector<float>* dashes = nullptr);
    void DrawEntities(ID2D1RenderTarget* rt, const std::vector<const Entity*>& ents, D2D1_COLOR_F color,
                      float width, ID2D1StrokeStyle* style, const std::vector<float>* dashes = nullptr);
    // The objects in their own colours, line types and weights.
    void DrawStyled(ID2D1RenderTarget* rt, const std::vector<const Entity*>& ents);
    // The dash pattern of line type `lt` at this zoom, in pixels (empty: solid).
    // `screen`: the same size at any zoom.
    std::vector<float> DashPattern(int lt, double scale, bool screen = false) const;
    // Arrays and blocks as the objects they stand for (kept in `store`).
    std::vector<const Entity*> Flatten(const std::vector<const Entity*>& ents, std::deque<Entity>& store) const;
    // Arrows, fills and text of the dimensions and annotations among `ents`.
    void DrawDimensionParts(ID2D1RenderTarget* rt, const std::vector<const Entity*>& ents, ID2D1Brush* brush);
    void DrawTextItem(ID2D1RenderTarget* rt, const TextItem& t, ID2D1Brush* brush);
    IDWriteTextFormat* FontFormat(const std::wstring& font);
    void DrawSnapMarker(ID2D1RenderTarget* rt);
    void DrawLabel(ID2D1RenderTarget* rt, const std::wstring& text, float x, float y);
    void LoadColours();

    // input
    void OnMouseMove(int x, int y, WPARAM keys);
    void OnLeftDown(int x, int y, WPARAM keys);
    void OnLeftUp(int x, int y, WPARAM keys);
    void UpdateCursor(WPARAM keys);
    uint64_t PickAt(Vec2 w) const;
    void FinishWindow(int x, int y, bool shift);
    void PushView();
    // grips and tracking
    void UpdateGrips();
    bool GripsVisible() const;
    int GripAt(Vec2 w) const;
    void DrawGrips(ID2D1RenderTarget* rt);
    bool ResolveTracking(const Prompt& pr, const Drafting& d, Vec2& p);
    void DrawTracking(ID2D1RenderTarget* rt);
    void StartDwell(Vec2 p);
    void StopDwell();
    void ZoomAt(double factor, int x, int y);
    void Notify();

    HWND m_hwnd = nullptr;
    Engine* m_engine = nullptr;
    Document* m_doc = nullptr;
    int m_dpi = 96;

    // Direct2D
    ID2D1Factory* m_factory = nullptr;
    IDWriteFactory* m_dwrite = nullptr;
    IDWriteTextFormat* m_textFormat = nullptr;
    IDWriteTextFormat* m_dimFormat = nullptr;  // dimension text, drawn scaled
    std::map<std::wstring, IDWriteTextFormat*> m_fonts;  // text in other fonts, the same way
    FieldValues m_fields;
    const struct Sheet* m_sheet = nullptr;
    const Document* m_model = nullptr;
    D2D1_COLOR_F m_outside{};  // around the paper
    ID2D1HwndRenderTarget* m_target = nullptr;
    ID2D1BitmapRenderTarget* m_layer = nullptr;
    ID2D1SolidColorBrush* m_brush = nullptr;
    ID2D1StrokeStyle* m_dashed = nullptr;
    bool m_layerValid = false;

    // colours
    D2D1_COLOR_F m_bg, m_fg, m_gridMinor, m_gridMajor, m_axisX, m_axisY, m_select, m_previewInk, m_guide, m_snap,
        m_cross, m_windowFill, m_windowEdge, m_crossFill, m_crossEdge, m_labelBg, m_labelText, m_removeInk,
        m_constructionInk;

    // view: world point at the window centre and pixels per drawing unit
    Vec2 m_centre{150, 100};
    double m_scale = 2;
    int m_width = 1, m_height = 1;
    std::vector<std::pair<Vec2, double>> m_previous;
    ULONGLONG m_lastWheelPush = 0;

    // what the layer shows (to know when to rebuild it)
    uint64_t m_shownRevision = 0, m_shownSelection = 0;
    bool m_shownGrid = false, m_shownLw = false;
    double m_shownGridSpacing = 0;

    // cursor and interaction state
    bool m_inside = false, m_tracking = false;
    POINT m_mouse{};
    Vec2 m_raw, m_tracked;
    SnapResult m_snapHit;
    uint64_t m_hover = 0;
    bool m_panning = false;
    bool m_stroking = false;          // a FreeSketch stroke is being drawn
    std::vector<Vec2> m_stroke;       // its points (drawing coordinates)
    POINT m_strokeStart{};
    POINT m_panLast{};
    bool m_windowOn = false, m_windowDragged = false;
    POINT m_windowStart{};
    Preview m_preview;

    // grips of the selection
    std::vector<Grip> m_grips;
    uint64_t m_gripsDocRev = 0, m_gripsSelRev = 0;
    int m_hoverGrip = -1;
    Vec2 m_hotGripPos;

    // polar and object snap tracking
    struct TrackLine {
        Vec2 origin, dir;
        bool ray;  // from the base point only (polar), or a whole line
    };
    std::vector<TrackLine> m_trackLines;
    std::wstring m_trackLabel;
    std::vector<Vec2> m_acquired;  // points rested on (object snap tracking)
    Vec2 m_dwellPoint;
    bool m_dwellOn = false;
    std::wstring m_typed;
    D2D1_COLOR_F m_gripFill, m_gripHover, m_gripHot, m_trackInk;
};
