// Spline.cpp - see Spline.h.
#include "Spline.h"

#include <algorithm>

namespace {
// Solves a tridiagonal system (a: below, b: diagonal, c: above) in place;
// d becomes the solution.
void SolveTridiagonal(std::vector<double> a, std::vector<double> b, std::vector<double> c, std::vector<double>& d) {
    const size_t n = d.size();
    for (size_t i = 1; i < n; ++i) {
        const double m = a[i] / b[i - 1];
        b[i] -= m * c[i - 1];
        d[i] -= m * d[i - 1];
    }
    d[n - 1] /= b[n - 1];
    for (size_t i = n - 1; i-- > 0;) d[i] = (d[i] - c[i] * d[i + 1]) / b[i];
}

// Cyclic tridiagonal (corner entries a[0] and c[n-1]), by Sherman-Morrison.
void SolveCyclic(const std::vector<double>& a, const std::vector<double>& b, const std::vector<double>& c,
                 std::vector<double>& d) {
    const size_t n = d.size();
    if (n < 3) {  // too small for the trick: plain solve
        SolveTridiagonal(a, b, c, d);
        return;
    }
    const double gamma = -b[0];
    std::vector<double> bb = b;
    bb[0] = b[0] - gamma;
    bb[n - 1] = b[n - 1] - c[n - 1] * a[0] / gamma;
    std::vector<double> x = d;
    SolveTridiagonal(a, bb, c, x);
    std::vector<double> u(n, 0.0);
    u[0] = gamma;
    u[n - 1] = c[n - 1];
    SolveTridiagonal(a, bb, c, u);
    const double fact = (x[0] + a[0] * x[n - 1] / gamma) / (1 + u[0] + a[0] * u[n - 1] / gamma);
    for (size_t i = 0; i < n; ++i) d[i] = x[i] - fact * u[i];
}
}  // namespace

void SplinePoints(const std::vector<Vec2>& fitIn, bool closed, int perSpan, std::vector<Vec2>& out) {
    out.clear();
    // Repeated points add nothing.
    std::vector<Vec2> fit;
    for (Vec2 p : fitIn)
        if (fit.empty() || !Near(fit.back(), p, 1e-12)) fit.push_back(p);
    if (closed && fit.size() > 1 && Near(fit.front(), fit.back(), 1e-12)) fit.pop_back();
    if (fit.size() < 2) {
        out = fit;
        return;
    }
    if (fit.size() == 2) {
        out = fit;
        if (closed) out.push_back(fit[0]);
        return;
    }
    perSpan = std::max(1, perSpan);
    std::vector<Vec2> pts = fit;
    if (closed) pts.push_back(fit[0]);
    const size_t n = pts.size() - 1;  // spans
    std::vector<double> h(n);
    for (size_t i = 0; i < n; ++i) h[i] = std::max(Dist(pts[i], pts[i + 1]), 1e-12);

    // Second derivatives M at each point, for x and y.
    std::vector<double> mx(n + 1, 0.0), my(n + 1, 0.0);
    if (!closed) {
        // Natural ends: M = 0 at the first and last point.
        const size_t m = n - 1;
        std::vector<double> a(m), b(m), c(m), dx(m), dy(m);
        for (size_t k = 0; k < m; ++k) {
            const size_t i = k + 1;
            a[k] = h[i - 1];
            b[k] = 2 * (h[i - 1] + h[i]);
            c[k] = h[i];
            const Vec2 r = (pts[i + 1] - pts[i]) / h[i] - (pts[i] - pts[i - 1]) / h[i - 1];
            dx[k] = 6 * r.x;
            dy[k] = 6 * r.y;
        }
        SolveTridiagonal(a, b, c, dx);
        SolveTridiagonal(a, b, c, dy);
        for (size_t k = 0; k < m; ++k) {
            mx[k + 1] = dx[k];
            my[k + 1] = dy[k];
        }
    } else {
        // Periodic: the curve joins itself smoothly at the start.
        std::vector<double> a(n), b(n), c(n), dx(n), dy(n);
        for (size_t i = 0; i < n; ++i) {
            const size_t prev = (i + n - 1) % n;
            const double hp = h[prev], hi = h[i];
            a[i] = hp;
            b[i] = 2 * (hp + hi);
            c[i] = hi;
            const Vec2 next = pts[i + 1], cur = pts[i], before = pts[prev];
            const Vec2 r = (next - cur) / hi - (cur - before) / hp;
            dx[i] = 6 * r.x;
            dy[i] = 6 * r.y;
        }
        SolveCyclic(a, b, c, dx);
        SolveCyclic(a, b, c, dy);
        for (size_t i = 0; i < n; ++i) {
            mx[i] = dx[i];
            my[i] = dy[i];
        }
        mx[n] = mx[0];
        my[n] = my[0];
    }
    out.push_back(pts[0]);
    for (size_t i = 0; i < n; ++i) {
        const double hi = h[i];
        for (int k = 1; k <= perSpan; ++k) {
            if (k == perSpan) {
                out.push_back(pts[i + 1]);
                continue;
            }
            const double u = hi * k / perSpan, v = hi - u;
            auto eval = [&](double m0, double m1, double p0, double p1) {
                return m0 * v * v * v / (6 * hi) + m1 * u * u * u / (6 * hi) + (p0 / hi - m0 * hi / 6) * v +
                       (p1 / hi - m1 * hi / 6) * u;
            };
            out.push_back({eval(mx[i], mx[i + 1], pts[i].x, pts[i + 1].x), eval(my[i], my[i + 1], pts[i].y, pts[i + 1].y)});
        }
    }
}

double EllipseParam(Vec2 centre, Vec2 axis, double ratio, Vec2 p) {
    const double a = Length(axis);
    if (a <= 0 || ratio <= 0) return 0;
    const Vec2 u = axis / a;
    const Vec2 d = p - centre;
    const double x = Dot(d, u) / a, y = Dot(d, Perp(u)) / (a * ratio);
    return NormalizeAngle(std::atan2(y, x));
}
