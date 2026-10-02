// Chart.h - the interactive graph: data points, fitted curve, confidence
// and prediction bands, and a residual plot underneath.
//
// Mouse: drag to pan, wheel to zoom (Shift: only x, Ctrl: only y),
// Ctrl+drag to zoom into a box, double-click to show everything, click a
// point to leave it out of the fit (click again to bring it back).
#pragma once
#include <functional>

#include "Common.h"

struct ChartModel {
    std::vector<double> x, y;
    std::vector<char> excluded;
    std::vector<double> robustWeight;          // optional: robust fits mark ignored outliers
    std::function<double(double)> fit;         // empty: no fitted curve
    // Half-widths of the confidence and prediction bands at x.
    std::function<bool(double x, double& conf, double& pred)> bands;
    std::wstring xTitle, yTitle, fitLabel;
    int confidencePercent = 95;
};

struct ChartOptions {
    bool confidenceBand = true;
    bool predictionBand = false;
    bool residuals = true;
    bool grid = true;
    bool logX = false, logY = false;
};

// Drawing surface used for the screen, PNG and SVG alike (pixels).
class Painter {
public:
    struct Pt {
        double x, y;
    };
    virtual ~Painter() = default;
    virtual void FillRect(double x, double y, double w, double h, COLORREF c) = 0;
    virtual void Rect(double x, double y, double w, double h, COLORREF c, double width) = 0;
    virtual void Line(double x1, double y1, double x2, double y2, COLORREF c, double width, bool dashed) = 0;
    virtual void Polyline(const std::vector<Pt>& pts, COLORREF c, double width, bool dashed) = 0;
    virtual void FillPolygon(const std::vector<Pt>& pts, COLORREF c, int alpha) = 0;
    virtual void Marker(double x, double y, double r, COLORREF fill, COLORREF stroke, double width, bool filled) = 0;
    // align: 0 left, 1 centre, 2 right; valign: 0 top, 1 middle, 2 bottom.
    virtual void Text(const std::wstring& s, double x, double y, int align, int valign, COLORREF c, double px,
                      bool bold, bool vertical) = 0;
    virtual double TextWidth(const std::wstring& s, double px, bool bold) = 0;
    virtual void Clip(double x, double y, double w, double h) = 0;
    virtual void Unclip() = 0;
};

class Chart {
public:
    bool Create(HWND parent);
    HWND Hwnd() const { return m_hwnd; }

    void SetModel(ChartModel model, bool resetView);
    void SetOptions(const ChartOptions& o);
    const ChartOptions& Options() const { return m_opt; }
    void ResetView();
    void Zoom(double factor);
    void OnThemeChanged() { InvalidateRect(m_hwnd, nullptr, FALSE); }

    // Export (always the light, print-friendly style).
    bool SavePng(const std::wstring& path, int width, int height) const;
    bool SaveSvg(const std::wstring& path, int width, int height) const;
    bool CopyToClipboard(HWND owner, int width, int height) const;
    std::string PngBytes(int width, int height) const;  // for reports

    std::function<void(int index)> onTogglePoint;
    std::function<void(const std::wstring& text)> onHover;
    std::function<void(POINT screen)> onContextMenu;

private:
    struct View {
        double x0 = 0, x1 = 1, y0 = 0, y1 = 1;
    };
    struct Layout {
        double left, top, width, height;      // main plot
        double rTop = 0, rHeight = 0;         // residual plot (rHeight 0: none)
        double scale;
    };
    struct Palette;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HDC hdc);
    void Render(Painter& p, double w, double h, double scale, bool exportStyle) const;
    Layout ComputeLayout(Painter& p, double w, double h, double scale) const;
    View AutoView() const;
    bool HasData() const;
    double Tx(double v) const;  // to axis space (log10 for log axes)
    double Ty(double v) const;
    double Ux(double t) const;  // from axis space
    double Uy(double t) const;
    int HitPoint(POINT pt) const;
    void ResidualRange(double& lo, double& hi) const;

    HWND m_hwnd = nullptr;
    ChartModel m_model;
    ChartOptions m_opt;
    View m_view;
    bool m_viewSet = false;
    int m_dpi = 96;
    // interaction
    bool m_down = false, m_dragging = false, m_boxZoom = false;
    POINT m_downPt{}, m_curPt{};
    View m_downView;
    int m_hover = -1;
    mutable Layout m_lastLayout{};
};
