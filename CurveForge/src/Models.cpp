// Models.cpp - built-in model equations and starting-value estimates.
#include "Models.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
const ModelInfo kModels[] = {
    {ModelKind::Linear, L"linear", L"Straight line", L"a = intercept, b = slope", false, true, false},
    {ModelKind::Proportional, L"proportional", L"Line through the origin", L"b = slope", false, true, false},
    {ModelKind::Polynomial, L"polynomial", L"Polynomial", L"a0 = constant, a1, a2, \x2026 = coefficients of x, x\xB2, \x2026",
     true, true, false},
    {ModelKind::Inverse, L"inverse", L"Inverse (a + b/x)", L"a = value for large x, b = scale", false, true, false},
    {ModelKind::Exponential, L"exponential", L"Exponential", L"a = value at x = 0, b = growth rate (negative: decay)",
     false, false, false},
    {ModelKind::ExpOffset, L"exp_offset", L"Exponential with offset",
     L"a = amplitude, b = rate (negative: decay), c = level it approaches", false, false, false},
    {ModelKind::DoubleExp, L"double_exp", L"Double exponential", L"a, c = amplitudes; b, d = rates", false, false, false},
    {ModelKind::Logarithmic, L"logarithmic", L"Logarithmic", L"a = value at x = 1, b = change per factor e of x", false,
     true, true},
    {ModelKind::Power, L"power", L"Power law", L"a = value at x = 1, b = exponent", false, false, true},
    {ModelKind::Gaussian, L"gaussian", L"Gaussian peak", L"a = height, b = centre, c = width (\x03C3), d = baseline",
     false, false, false},
    {ModelKind::Lorentzian, L"lorentzian", L"Lorentzian peak",
     L"a = height, b = centre, c = half width at half maximum, d = baseline", false, false, false},
    {ModelKind::GaussianPeaks, L"gaussian_peaks", L"Several Gaussian peaks",
     L"aN = height, bN = centre, cN = width of peak N; d = baseline", true, false, false},
    {ModelKind::Logistic, L"logistic", L"Logistic (S-curve)",
     L"a = rise (top \x2212 bottom), b = steepness, c = midpoint, d = bottom", false, false, false},
    {ModelKind::DoseResponse, L"dose_response", L"Dose-response (4PL)",
     L"a = response at x = 0, d = response at large x, c = EC50 (midpoint), b = Hill slope", false, false, true},
    {ModelKind::Hill, L"hill", L"Hill equation", L"a = maximum, k = half-maximum point, n = Hill coefficient", false,
     false, true},
    {ModelKind::MichaelisMenten, L"michaelis_menten", L"Michaelis-Menten", L"a = Vmax (maximum), b = Km (half-maximum point)",
     false, false, false},
    {ModelKind::Sine, L"sine", L"Sine wave", L"a = amplitude, b = angular frequency (2\x03C0/period), c = phase, d = offset",
     false, false, false},
    {ModelKind::DampedSine, L"damped_sine", L"Damped sine wave",
     L"a = amplitude, k = damping rate, b = angular frequency, c = phase, d = offset", false, false, false},
    {ModelKind::Spline, L"spline", L"Cubic spline (through every point)",
     L"A smooth curve through every point, for interpolation. It has no parameters or statistics.", false, true, false},
    {ModelKind::Custom, L"custom", L"Your own equation",
     L"Type any equation in x, e.g. y = a*exp(-b*x) + c. Every other name is a parameter.", false, false, false},
};

struct Pt {
    double x, y;
};

std::vector<Pt> Sorted(const std::vector<double>& x, const std::vector<double>& y) {
    std::vector<Pt> p;
    for (size_t i = 0; i < x.size() && i < y.size(); ++i)
        if (std::isfinite(x[i]) && std::isfinite(y[i])) p.push_back({x[i], y[i]});
    std::sort(p.begin(), p.end(), [](const Pt& a, const Pt& b) { return a.x < b.x; });
    return p;
}

// Least squares y = a + b*g(x). Returns false if degenerate.
bool LineFit(const std::vector<double>& u, const std::vector<double>& v, double& a, double& b) {
    const size_t n = std::min(u.size(), v.size());
    if (n < 2) return false;
    double su = 0, sv = 0, suu = 0, suv = 0;
    for (size_t i = 0; i < n; ++i) {
        su += u[i];
        sv += v[i];
        suu += u[i] * u[i];
        suv += u[i] * v[i];
    }
    const double d = n * suu - su * su;
    if (std::fabs(d) < 1e-300) return false;
    b = (n * suv - su * sv) / d;
    a = (sv - b * su) / n;
    return std::isfinite(a) && std::isfinite(b);
}

// x where y first crosses `level` (linear interpolation).
double CrossingX(const std::vector<Pt>& p, double level) {
    for (size_t i = 1; i < p.size(); ++i) {
        const double y0 = p[i - 1].y - level, y1 = p[i].y - level;
        if ((y0 <= 0 && y1 >= 0) || (y0 >= 0 && y1 <= 0)) {
            if (y1 == y0) return p[i].x;
            return p[i - 1].x + (p[i].x - p[i - 1].x) * (-y0) / (y1 - y0);
        }
    }
    return p.empty() ? 0 : (p.front().x + p.back().x) / 2;
}

struct PeakGuess {
    double height, centre, width;
};

// Peak height/centre/width (sigma-like) above `base`.
PeakGuess OnePeak(const std::vector<Pt>& p, double base) {
    PeakGuess g{1, 0, 1};
    if (p.empty()) return g;
    size_t im = 0;
    for (size_t i = 1; i < p.size(); ++i)
        if (std::fabs(p[i].y - base) > std::fabs(p[im].y - base)) im = i;
    g.height = p[im].y - base;
    g.centre = p[im].x;
    const double half = base + g.height / 2;
    double left = p.front().x, right = p.back().x;
    for (size_t i = im; i > 0; --i)
        if ((p[i - 1].y - half) * (g.height > 0 ? 1 : -1) <= 0) {
            left = p[i - 1].x;
            break;
        }
    for (size_t i = im; i + 1 < p.size(); ++i)
        if ((p[i + 1].y - half) * (g.height > 0 ? 1 : -1) <= 0) {
            right = p[i + 1].x;
            break;
        }
    const double fwhm = std::max(right - left, (p.back().x - p.front().x) / 50);
    g.width = fwhm / 2.3548;
    return g;
}

void Add(std::vector<std::pair<std::wstring, double>>& out, const wchar_t* n, double v) {
    out.emplace_back(n, std::isfinite(v) ? v : 1.0);
}
}  // namespace

const ModelInfo& Models::Info(ModelKind k) {
    for (const ModelInfo& m : kModels)
        if (m.kind == k) return m;
    return kModels[0];
}

ModelKind Models::FromKey(const std::wstring& key) {
    for (const ModelInfo& m : kModels)
        if (key == m.key) return m.kind;
    return ModelKind::Custom;
}

int Models::DefaultOrder(ModelKind k) { return k == ModelKind::Polynomial ? 2 : k == ModelKind::GaussianPeaks ? 2 : 0; }
int Models::MinOrder(ModelKind k) { return k == ModelKind::Polynomial ? 2 : k == ModelKind::GaussianPeaks ? 2 : 0; }
int Models::MaxOrder(ModelKind k) { return k == ModelKind::Polynomial ? 10 : k == ModelKind::GaussianPeaks ? 6 : 0; }

std::wstring Models::Formula(ModelKind k, int order) {
    switch (k) {
        case ModelKind::Linear: return L"y = a + b*x";
        case ModelKind::Proportional: return L"y = b*x";
        case ModelKind::Polynomial: {
            order = std::min(std::max(order, 2), 10);
            std::wstring s = L"y = a0 + a1*x";
            for (int i = 2; i <= order; ++i) s += L" + a" + std::to_wstring(i) + L"*x^" + std::to_wstring(i);
            return s;
        }
        case ModelKind::Inverse: return L"y = a + b/x";
        case ModelKind::Exponential: return L"y = a*exp(b*x)";
        case ModelKind::ExpOffset: return L"y = a*exp(b*x) + c";
        case ModelKind::DoubleExp: return L"y = a*exp(b*x) + c*exp(d*x)";
        case ModelKind::Logarithmic: return L"y = a + b*ln(x)";
        case ModelKind::Power: return L"y = a*x^b";
        case ModelKind::Gaussian: return L"y = a*exp(-((x - b)/c)^2/2) + d";
        case ModelKind::Lorentzian: return L"y = a/(1 + ((x - b)/c)^2) + d";
        case ModelKind::GaussianPeaks: {
            order = std::min(std::max(order, 2), 6);
            std::wstring s = L"y = ";
            for (int i = 1; i <= order; ++i) {
                const std::wstring n = std::to_wstring(i);
                s += L"a" + n + L"*exp(-((x - b" + n + L")/c" + n + L")^2/2) + ";
            }
            return s + L"d";
        }
        case ModelKind::Logistic: return L"y = a/(1 + exp(-b*(x - c))) + d";
        case ModelKind::DoseResponse: return L"y = d + (a - d)/(1 + (x/c)^b)";
        case ModelKind::Hill: return L"y = a*x^n/(k^n + x^n)";
        case ModelKind::MichaelisMenten: return L"y = a*x/(b + x)";
        case ModelKind::Sine: return L"y = a*sin(b*x + c) + d";
        case ModelKind::DampedSine: return L"y = a*exp(-k*x)*sin(b*x + c) + d";
        case ModelKind::Spline: return L"y = cubic spline through the points";
        default: return L"";
    }
}

std::vector<std::pair<std::wstring, double>> Models::Guess(ModelKind k, int order, const std::vector<double>& xs,
                                                           const std::vector<double>& ys) {
    std::vector<std::pair<std::wstring, double>> g;
    const std::vector<Pt> p = Sorted(xs, ys);
    if (p.empty()) return g;
    double ymin = p[0].y, ymax = p[0].y;
    for (const Pt& q : p) {
        ymin = std::min(ymin, q.y);
        ymax = std::max(ymax, q.y);
    }
    const double xmin = p.front().x, xmax = p.back().x, xr = std::max(xmax - xmin, 1e-12);
    double mean = 0;
    for (const Pt& q : p) mean += q.y;
    mean /= (double)p.size();
    std::vector<double> X, Y;
    for (const Pt& q : p) {
        X.push_back(q.x);
        Y.push_back(q.y);
    }

    switch (k) {
        case ModelKind::Linear: {
            double a = 0, b = 0;
            LineFit(X, Y, a, b);
            Add(g, L"a", a);
            Add(g, L"b", b);
            break;
        }
        case ModelKind::Proportional: {
            double sxy = 0, sxx = 0;
            for (const Pt& q : p) {
                sxy += q.x * q.y;
                sxx += q.x * q.x;
            }
            Add(g, L"b", sxx > 0 ? sxy / sxx : 1);
            break;
        }
        case ModelKind::Polynomial:
            for (int i = 0; i <= std::max(order, 2); ++i) Add(g, (L"a" + std::to_wstring(i)).c_str(), 0);
            break;
        case ModelKind::Inverse: {
            std::vector<double> u, v;
            for (const Pt& q : p)
                if (q.x != 0) {
                    u.push_back(1 / q.x);
                    v.push_back(q.y);
                }
            double a = 0, b = 1;
            LineFit(u, v, a, b);
            Add(g, L"a", a);
            Add(g, L"b", b);
            break;
        }
        case ModelKind::Exponential:
        case ModelKind::DoubleExp: {
            std::vector<double> u, v;
            const double sign = mean < 0 ? -1 : 1;
            for (const Pt& q : p)
                if (q.y * sign > 0) {
                    u.push_back(q.x);
                    v.push_back(std::log(q.y * sign));
                }
            double la = 0, b = 0;
            if (!LineFit(u, v, la, b)) {
                la = std::log(std::max(std::fabs(mean), 1e-12));
                b = 0;
            }
            const double a = sign * std::exp(la);
            Add(g, L"a", k == ModelKind::DoubleExp ? a / 2 : a);
            Add(g, L"b", b);
            if (k == ModelKind::DoubleExp) {
                Add(g, L"c", a / 2);
                Add(g, L"d", b * 5 + (b == 0 ? -1 / xr : 0));
            }
            break;
        }
        case ModelKind::ExpOffset: {
            // dy/dx = b*(y - c): regress the slope on y to get b, then a and c are linear.
            std::vector<double> u, v;
            for (size_t i = 1; i < p.size(); ++i) {
                const double dx = p[i].x - p[i - 1].x;
                if (dx <= 0) continue;
                u.push_back((p[i].y + p[i - 1].y) / 2);
                v.push_back((p[i].y - p[i - 1].y) / dx);
            }
            double i0 = 0, b = 0;
            if (!LineFit(u, v, i0, b) || b == 0) b = -3 / xr;
            std::vector<double> e;
            for (const Pt& q : p) e.push_back(std::exp(b * (q.x - xmin)));
            double c = 0, a = 1;
            if (!LineFit(e, Y, c, a)) {
                c = mean;
                a = ymax - ymin;
            }
            // a was found for exp(b*(x - xmin)); move it to exp(b*x).
            Add(g, L"a", a * std::exp(-b * xmin));
            Add(g, L"b", b);
            Add(g, L"c", c);
            break;
        }
        case ModelKind::Logarithmic: {
            std::vector<double> u, v;
            for (const Pt& q : p)
                if (q.x > 0) {
                    u.push_back(std::log(q.x));
                    v.push_back(q.y);
                }
            double a = 0, b = 1;
            LineFit(u, v, a, b);
            Add(g, L"a", a);
            Add(g, L"b", b);
            break;
        }
        case ModelKind::Power: {
            std::vector<double> u, v;
            const double sign = mean < 0 ? -1 : 1;
            for (const Pt& q : p)
                if (q.x > 0 && q.y * sign > 0) {
                    u.push_back(std::log(q.x));
                    v.push_back(std::log(q.y * sign));
                }
            double la = 0, b = 1;
            LineFit(u, v, la, b);
            Add(g, L"a", sign * std::exp(la));
            Add(g, L"b", b);
            break;
        }
        case ModelKind::Gaussian:
        case ModelKind::Lorentzian: {
            // The baseline is at the opposite end from the peak.
            const double med = mean;
            const bool up = (ymax - med) >= (med - ymin);
            const double base = up ? ymin : ymax;
            const PeakGuess pk = OnePeak(p, base);
            Add(g, L"a", pk.height);
            Add(g, L"b", pk.centre);
            Add(g, L"c", k == ModelKind::Lorentzian ? pk.width * 1.1774 : pk.width);
            Add(g, L"d", base);
            break;
        }
        case ModelKind::GaussianPeaks: {
            const int n = std::min(std::max(order, 2), 6);
            const double base = ymin;
            // Local maxima of a lightly smoothed curve, highest first.
            std::vector<double> s(p.size());
            for (size_t i = 0; i < p.size(); ++i) {
                double sum = 0;
                int cnt = 0;
                for (size_t j = (i >= 2 ? i - 2 : 0); j <= std::min(p.size() - 1, i + 2); ++j) {
                    sum += p[j].y;
                    ++cnt;
                }
                s[i] = sum / cnt;
            }
            std::vector<size_t> peaks;
            for (size_t i = 1; i + 1 < s.size(); ++i)
                if (s[i] >= s[i - 1] && s[i] > s[i + 1]) peaks.push_back(i);
            std::sort(peaks.begin(), peaks.end(), [&](size_t a, size_t b) { return s[a] > s[b]; });
            std::vector<size_t> chosen;
            for (size_t i : peaks) {
                bool distinct = true;
                for (size_t c : chosen)
                    if (std::fabs(p[i].x - p[c].x) < xr / (4.0 * n)) distinct = false;
                if (distinct) chosen.push_back(i);
                if ((int)chosen.size() == n) break;
            }
            std::sort(chosen.begin(), chosen.end());
            for (int i = 0; i < n; ++i) {
                const std::wstring id = std::to_wstring(i + 1);
                double h, c;
                if (i < (int)chosen.size()) {
                    h = p[chosen[(size_t)i]].y - base;
                    c = p[chosen[(size_t)i]].x;
                } else {
                    h = (ymax - base) / 2;
                    c = xmin + xr * (i + 1) / (n + 1);
                }
                Add(g, (L"a" + id).c_str(), h);
                Add(g, (L"b" + id).c_str(), c);
                Add(g, (L"c" + id).c_str(), xr / (6.0 * n));
            }
            Add(g, L"d", base);
            break;
        }
        case ModelKind::Logistic: {
            const bool rising = p.back().y >= p.front().y;
            Add(g, L"a", ymax - ymin);
            Add(g, L"b", (rising ? 1 : -1) * 8 / xr);
            Add(g, L"c", CrossingX(p, (ymin + ymax) / 2));
            Add(g, L"d", ymin);
            break;
        }
        case ModelKind::DoseResponse: {
            Add(g, L"d", p.back().y);
            Add(g, L"a", p.front().y);
            double c = CrossingX(p, (p.front().y + p.back().y) / 2);
            if (c <= 0) c = std::max(xmin, 1e-6) + xr / 2;
            Add(g, L"c", c);
            Add(g, L"b", 1);
            break;
        }
        case ModelKind::Hill: {
            Add(g, L"a", ymax * 1.1);
            double kx = CrossingX(p, ymax / 2);
            if (kx <= 0) kx = xmin + xr / 2;
            Add(g, L"k", kx);
            Add(g, L"n", 1);
            break;
        }
        case ModelKind::MichaelisMenten: {
            const double a = ymax * 1.2;
            double kx = CrossingX(p, a / 2);
            if (kx <= 0) kx = xr / 2;
            Add(g, L"a", a);
            Add(g, L"b", kx);
            break;
        }
        case ModelKind::Sine:
        case ModelKind::DampedSine: {
            // Frequency from the number of mean crossings; amplitude and phase
            // by linear least squares on sin and cos.
            int crossings = 0;
            for (size_t i = 1; i < p.size(); ++i)
                if ((p[i - 1].y - mean) * (p[i].y - mean) < 0) ++crossings;
            const double period = crossings > 0 ? 2 * xr / crossings : xr;
            const double b = 2 * 3.14159265358979 / period;
            double ss = 0, sc = 0, scc = 0, sss = 0, ssc = 0;
            for (const Pt& q : p) {
                const double sn = std::sin(b * q.x), cs = std::cos(b * q.x), v = q.y - mean;
                ss += sn * v;
                sc += cs * v;
                sss += sn * sn;
                scc += cs * cs;
                ssc += sn * cs;
            }
            const double det = sss * scc - ssc * ssc;
            double A = (ymax - ymin) / 2, phase = 0;
            if (std::fabs(det) > 1e-300) {
                const double u = (ss * scc - sc * ssc) / det;  // coefficient of sin
                const double v = (sc * sss - ss * ssc) / det;  // coefficient of cos
                A = std::hypot(u, v);
                phase = std::atan2(v, u);
            }
            Add(g, L"a", A);
            if (k == ModelKind::DampedSine) Add(g, L"k", 0.5 / xr);
            Add(g, L"b", b);
            Add(g, L"c", phase);
            Add(g, L"d", mean);
            break;
        }
        default: break;
    }
    return g;
}

bool Models::Spline::Build(std::vector<double> xs, std::vector<double> ys) {
    std::vector<Pt> p = Sorted(xs, ys);
    x.clear();
    y.clear();
    m.clear();
    for (size_t i = 0; i < p.size();) {
        size_t j = i;
        double sum = 0;
        while (j < p.size() && p[j].x == p[i].x) sum += p[j++].y;
        x.push_back(p[i].x);
        y.push_back(sum / (double)(j - i));
        i = j;
    }
    const size_t n = x.size();
    if (n < 2) return false;
    m.assign(n, 0.0);
    if (n == 2) return true;
    // Tridiagonal system for natural spline second derivatives.
    std::vector<double> a(n, 0.0), b(n, 0.0), c(n, 0.0), d(n, 0.0);
    for (size_t i = 1; i + 1 < n; ++i) {
        const double h0 = x[i] - x[i - 1], h1 = x[i + 1] - x[i];
        a[i] = h0;
        b[i] = 2 * (h0 + h1);
        c[i] = h1;
        d[i] = 6 * ((y[i + 1] - y[i]) / h1 - (y[i] - y[i - 1]) / h0);
    }
    for (size_t i = 2; i + 1 < n; ++i) {
        const double w = a[i] / b[i - 1];
        b[i] -= w * c[i - 1];
        d[i] -= w * d[i - 1];
    }
    for (size_t i = n - 2; i >= 1; --i) {
        m[i] = (d[i] - c[i] * m[i + 1]) / b[i];
        if (i == 1) break;
    }
    return true;
}

double Models::Spline::Eval(double v) const {
    const size_t n = x.size();
    if (n == 0) return NAN;
    if (n == 1) return y[0];
    size_t i = (size_t)(std::upper_bound(x.begin(), x.end(), v) - x.begin());
    i = std::min(std::max(i, (size_t)1), n - 1);
    const double h = x[i] - x[i - 1];
    const double t1 = (x[i] - v) / h, t2 = (v - x[i - 1]) / h;
    // Outside the data: continue the end cubic (natural spline extrapolation).
    return t1 * y[i - 1] + t2 * y[i] + ((t1 * t1 * t1 - t1) * m[i - 1] + (t2 * t2 * t2 - t2) * m[i]) * h * h / 6;
}
