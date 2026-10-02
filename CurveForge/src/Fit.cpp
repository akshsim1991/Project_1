// Fit.cpp - Levenberg-Marquardt with QR, statistics, and calculus helpers.
#include "Fit.h"

#include <algorithm>
#include <cmath>

namespace {
// Dense row-major matrix.
struct Mat {
    int rows = 0, cols = 0;
    std::vector<double> a;
    Mat() = default;
    Mat(int r, int c) : rows(r), cols(c), a((size_t)r * (size_t)c, 0.0) {}
    double& operator()(int r, int c) { return a[(size_t)r * (size_t)cols + (size_t)c]; }
    double operator()(int r, int c) const { return a[(size_t)r * (size_t)cols + (size_t)c]; }
};

// Householder QR of A (rows >= cols), in place. Returns R's diagonal in
// `diag`; A's upper triangle (above the diagonal) holds the rest of R, and
// `b` is transformed to Q^T b.
void HouseholderQR(Mat& A, std::vector<double>& b, std::vector<double>& diag) {
    const int m = A.rows, n = A.cols;
    diag.assign((size_t)n, 0.0);
    for (int k = 0; k < n; ++k) {
        double norm = 0;
        for (int i = k; i < m; ++i) norm = std::hypot(norm, A(i, k));
        if (norm == 0) {
            diag[(size_t)k] = 0;
            continue;
        }
        if (A(k, k) > 0) norm = -norm;
        for (int i = k; i < m; ++i) A(i, k) /= -norm;
        A(k, k) += 1.0;
        for (int j = k + 1; j < n; ++j) {
            double s = 0;
            for (int i = k; i < m; ++i) s += A(i, k) * A(i, j);
            s = -s / A(k, k);
            for (int i = k; i < m; ++i) A(i, j) += s * A(i, k);
        }
        if (!b.empty()) {
            double s = 0;
            for (int i = k; i < m; ++i) s += A(i, k) * b[(size_t)i];
            s = -s / A(k, k);
            for (int i = k; i < m; ++i) b[(size_t)i] += s * A(i, k);
        }
        diag[(size_t)k] = norm;
    }
}

// R (n x n upper triangular) from a factorised A.
Mat ExtractR(const Mat& A, const std::vector<double>& diag) {
    const int n = A.cols;
    Mat R(n, n);
    for (int i = 0; i < n; ++i) {
        R(i, i) = diag[(size_t)i];
        for (int j = i + 1; j < n; ++j) R(i, j) = A(i, j);
    }
    return R;
}

// Least squares: minimise |A x - b|. Rank-deficient directions get 0.
std::vector<double> SolveLeastSquares(Mat A, std::vector<double> b) {
    std::vector<double> diag;
    HouseholderQR(A, b, diag);
    const int n = A.cols;
    double maxDiag = 0;
    for (double d : diag) maxDiag = std::max(maxDiag, std::fabs(d));
    std::vector<double> x((size_t)n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
        const double d = diag[(size_t)i];
        if (std::fabs(d) <= maxDiag * 1e-13 || d == 0) continue;
        double s = b[(size_t)i];
        for (int j = i + 1; j < n; ++j) s -= A(i, j) * x[(size_t)j];
        x[(size_t)i] = s / d;
    }
    return x;
}

// Inverse of an upper-triangular matrix. False if singular.
bool InvertUpper(const Mat& R, Mat& inv) {
    const int n = R.rows;
    double maxDiag = 0;
    for (int i = 0; i < n; ++i) maxDiag = std::max(maxDiag, std::fabs(R(i, i)));
    inv = Mat(n, n);
    for (int i = 0; i < n; ++i)
        if (std::fabs(R(i, i)) <= maxDiag * 1e-14 || R(i, i) == 0) return false;
    for (int j = 0; j < n; ++j) {
        inv(j, j) = 1.0 / R(j, j);
        for (int i = j - 1; i >= 0; --i) {
            double s = 0;
            for (int k = i + 1; k <= j; ++k) s += R(i, k) * inv(k, j);
            inv(i, j) = -s / R(i, i);
        }
    }
    return true;
}

struct Problem {
    const FitInput* in = nullptr;
    std::vector<double> w;        // effective weights
    std::vector<int> freeIdx;     // indices of the fitted parameters
    std::vector<double> lo, hi;   // bounds per parameter
    int nParams = 0;

    double Model(const std::vector<double>& p, double x) const { return in->expr->Eval(&x, p.data()); }

    // Weighted residuals sqrt(w)*(y - f). Returns SSE (inf if any value is not finite).
    double Residuals(const std::vector<double>& p, std::vector<double>& r) const {
        const size_t n = in->x.size();
        r.resize(n);
        double sse = 0;
        for (size_t i = 0; i < n; ++i) {
            if (w[i] == 0) {
                r[i] = 0;
                continue;
            }
            const double f = Model(p, in->x[i]);
            r[i] = std::sqrt(w[i]) * (in->y[i] - f);
            if (!std::isfinite(r[i])) return INFINITY;
            sse += r[i] * r[i];
        }
        return sse;
    }

    // Jacobian of the residuals with respect to the free parameters.
    void Jacobian(const std::vector<double>& p, const std::vector<double>& r, bool central, Mat& J) const {
        const size_t n = in->x.size();
        const int m = (int)freeIdx.size();
        J = Mat((int)n, m);
        std::vector<double> q = p;
        for (int j = 0; j < m; ++j) {
            const int pi = freeIdx[(size_t)j];
            const double h = (central ? 1e-5 : 1e-7) * std::max(std::fabs(p[(size_t)pi]), 1e-3);
            for (size_t i = 0; i < n; ++i) {
                if (w[i] == 0) continue;
                const double sw = std::sqrt(w[i]);
                q[(size_t)pi] = p[(size_t)pi] + h;
                const double fp = Model(q, in->x[i]);
                double d;
                if (central) {
                    q[(size_t)pi] = p[(size_t)pi] - h;
                    const double fm = Model(q, in->x[i]);
                    d = (fp - fm) / (2 * h);
                } else {
                    const double f0 = in->y[i] - r[i] / sw;
                    d = (fp - f0) / h;
                }
                J((int)i, j) = std::isfinite(d) ? -sw * d : 0.0;
            }
            q[(size_t)pi] = p[(size_t)pi];
        }
    }

    void Clamp(std::vector<double>& p) const {
        for (int i = 0; i < nParams; ++i) p[(size_t)i] = std::min(std::max(p[(size_t)i], lo[(size_t)i]), hi[(size_t)i]);
    }
};

struct LmResult {
    std::vector<double> p;
    double sse = INFINITY;
    int iterations = 0;
    bool converged = false;
};

LmResult Levenberg(const Problem& pr, std::vector<double> p) {
    LmResult res;
    pr.Clamp(p);
    std::vector<double> r;
    double sse = pr.Residuals(p, r);
    res.p = p;
    res.sse = sse;
    if (!std::isfinite(sse)) return res;
    const int m = (int)pr.freeIdx.size();
    const int n = (int)pr.in->x.size();
    if (m == 0) {
        res.converged = true;
        return res;
    }
    double lambda = 1e-3;
    std::vector<double> scale((size_t)m, 0.0);
    Mat J;
    bool needJ = true;
    for (int it = 0; it < 400; ++it) {
        res.iterations = it + 1;
        if (needJ) {
            pr.Jacobian(p, r, false, J);
            for (int j = 0; j < m; ++j) {
                double s = 0;
                for (int i = 0; i < n; ++i) s += J(i, j) * J(i, j);
                scale[(size_t)j] = std::max(scale[(size_t)j], std::sqrt(s));
                if (scale[(size_t)j] == 0) scale[(size_t)j] = 1;
            }
            needJ = false;
        }
        // Solve [J D^-1; sqrt(lambda) I] (D dp) = [-r; 0]. Scaling the columns
        // keeps parameters of very different sizes (x^8 next to 1) solvable.
        Mat A(n + m, m);
        std::vector<double> b((size_t)(n + m), 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < m; ++j) A(i, j) = J(i, j) / scale[(size_t)j];
            b[(size_t)i] = -r[(size_t)i];
        }
        const double sl = std::sqrt(lambda);
        for (int j = 0; j < m; ++j) A(n + j, j) = sl;
        std::vector<double> dp = SolveLeastSquares(std::move(A), std::move(b));
        for (int j = 0; j < m; ++j) dp[(size_t)j] /= scale[(size_t)j];

        std::vector<double> q = p;
        double stepNorm = 0, pNorm = 0;
        for (int j = 0; j < m; ++j) {
            const int pi = pr.freeIdx[(size_t)j];
            q[(size_t)pi] += dp[(size_t)j];
            stepNorm += (dp[(size_t)j] * scale[(size_t)j]) * (dp[(size_t)j] * scale[(size_t)j]);
            pNorm += (p[(size_t)pi] * scale[(size_t)j]) * (p[(size_t)pi] * scale[(size_t)j]);
        }
        pr.Clamp(q);
        std::vector<double> rq;
        const double sseNew = pr.Residuals(q, rq);
        if (std::isfinite(sseNew) && sseNew <= sse) {
            const double improvement = sse - sseNew;
            p = std::move(q);
            r = std::move(rq);
            const bool tiny = improvement <= 1e-14 * std::max(sse, 1e-300) ||
                              std::sqrt(stepNorm) <= 1e-12 * (std::sqrt(pNorm) + 1e-12);
            sse = sseNew;
            lambda = std::max(lambda * 0.3, 1e-15);
            needJ = true;
            if (tiny || sse == 0) {
                res.converged = true;
                break;
            }
        } else {
            lambda *= 4;
            if (lambda > 1e16) {
                // No step reduces the error any more: a minimum.
                res.converged = true;
                break;
            }
        }
    }
    res.p = p;
    res.sse = sse;
    return res;
}

// Regularised incomplete beta function (Numerical Recipes).
double BetaCf(double a, double b, double x) {
    const double eps = 3e-16, fpmin = 1e-300;
    double qab = a + b, qap = a + 1, qam = a - 1, c = 1, d = 1 - qab * x / qap;
    if (std::fabs(d) < fpmin) d = fpmin;
    d = 1 / d;
    double h = d;
    for (int m = 1; m <= 300; ++m) {
        const int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1 + aa * d;
        if (std::fabs(d) < fpmin) d = fpmin;
        c = 1 + aa / c;
        if (std::fabs(c) < fpmin) c = fpmin;
        d = 1 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1 + aa * d;
        if (std::fabs(d) < fpmin) d = fpmin;
        c = 1 + aa / c;
        if (std::fabs(c) < fpmin) c = fpmin;
        d = 1 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1) < eps) break;
    }
    return h;
}

double IncompleteBeta(double a, double b, double x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    const double bt = std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) + b * std::log(1 - x));
    if (x < (a + 1) / (a + b + 2)) return bt * BetaCf(a, b, x) / a;
    return 1 - bt * BetaCf(b, a, 1 - x) / b;
}

double Median(std::vector<double> v) {
    if (v.empty()) return 0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (long)mid, v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) {
        const double lower = *std::max_element(v.begin(), v.begin() + (long)mid);
        m = (m + lower) / 2;
    }
    return m;
}

// Deterministic random numbers, so a fit gives the same answer every time.
struct Rng {
    unsigned long long s = 0x9E3779B97F4A7C15ull;
    double Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (double)(s >> 11) / 9007199254740992.0;
    }
};
}  // namespace

double Fit::TQuantile(double confidence, int dof) {
    if (dof <= 0) return NAN;
    const double alpha = 1 - confidence;
    // Two-sided tail probability at t: I_{dof/(dof+t^2)}(dof/2, 1/2).
    double lo = 0, hi = 1e7;
    for (int i = 0; i < 200; ++i) {
        const double t = (lo + hi) / 2;
        const double p = IncompleteBeta(dof / 2.0, 0.5, dof / (dof + t * t));
        if (p > alpha) lo = t;
        else hi = t;
    }
    return (lo + hi) / 2;
}

FitResult Fit::Run(const FitInput& in) {
    FitResult out;
    out.confidence = in.confidence;
    if (!in.expr || in.expr->Empty()) {
        out.error = L"There is no equation to fit.";
        return out;
    }
    const std::vector<std::wstring>& names = in.expr->Params();
    const int np = (int)names.size();
    out.names = names;

    Problem pr;
    pr.in = &in;
    pr.nParams = np;
    pr.lo.assign((size_t)np, -INFINITY);
    pr.hi.assign((size_t)np, INFINITY);
    std::vector<double> p0((size_t)np, 1.0);
    for (int i = 0; i < np && i < (int)in.init.size(); ++i)
        if (std::isfinite(in.init[(size_t)i])) p0[(size_t)i] = in.init[(size_t)i];
    out.fixed.assign((size_t)np, false);
    for (int i = 0; i < np; ++i) {
        for (const ParamOption& o : in.options) {
            if (o.name != names[(size_t)i]) continue;
            if (o.hasInit || o.fixed) p0[(size_t)i] = o.init;
            out.fixed[(size_t)i] = o.fixed;
            pr.lo[(size_t)i] = o.lo;
            pr.hi[(size_t)i] = o.hi;
        }
        if (!out.fixed[(size_t)i]) pr.freeIdx.push_back(i);
    }

    const size_t n = in.x.size();
    pr.w.assign(n, 1.0);
    for (size_t i = 0; i < n && i < in.w.size(); ++i)
        pr.w[i] = std::isfinite(in.w[i]) && in.w[i] > 0 ? in.w[i] : 0.0;
    int used = 0;
    for (size_t i = 0; i < n; ++i)
        if (pr.w[i] > 0 && std::isfinite(in.x[i]) && std::isfinite(in.y[i])) ++used;
        else pr.w[i] = 0;
    const int m = (int)pr.freeIdx.size();
    if (used == 0) {
        out.error = L"There are no points to fit. Choose the X and Y columns, and check that they contain numbers.";
        return out;
    }
    if (used < m) {
        out.error = L"This model has " + std::to_wstring(m) + L" parameters but there are only " +
                    std::to_wstring(used) + L" points. Add points or choose a simpler model.";
        return out;
    }

    // Fit from several starting points and keep the best.
    auto fitFrom = [&](const std::vector<double>& start) { return Levenberg(pr, start); };
    LmResult best = fitFrom(p0);
    const bool firstFailed = !std::isfinite(best.sse) || !best.converged;
    if (m > 0 && (in.multiStart || firstFailed)) {
        Rng rng;
        std::vector<std::vector<double>> starts;
        for (double f : {-1.0, 0.1, 10.0, 0.5, 2.0, -0.1}) {
            std::vector<double> s = p0;
            for (int idx : pr.freeIdx) s[(size_t)idx] = (p0[(size_t)idx] == 0 ? 1.0 : p0[(size_t)idx]) * f;
            starts.push_back(s);
        }
        const int extra = n > 20000 ? 4 : n > 2000 ? 10 : 24;
        for (int k = 0; k < extra; ++k) {
            std::vector<double> s = p0;
            for (int idx : pr.freeIdx) {
                const double mag = std::pow(10.0, rng.Next() * 4 - 2);
                const double base = p0[(size_t)idx] == 0 ? 1.0 : std::fabs(p0[(size_t)idx]);
                s[(size_t)idx] = base * mag * (rng.Next() < 0.25 ? -1 : 1);
            }
            starts.push_back(s);
        }
        for (const auto& s : starts) {
            LmResult r = fitFrom(s);
            if (std::isfinite(r.sse) && (!std::isfinite(best.sse) || r.sse < best.sse * (1 - 1e-12))) best = r;
        }
    }
    if (!std::isfinite(best.sse)) {
        out.error = L"The model could not be calculated for these data (for example a logarithm of a negative "
                    L"number, or a division by zero). Try another model, or set starting values in Parameters.";
        return out;
    }

    // Robust fitting: reweight to ignore outliers (Tukey bisquare).
    if (in.robust && m > 0) {
        const std::vector<double> base = pr.w;
        std::vector<double> rw(n, 1.0);
        for (int iter = 0; iter < 30; ++iter) {
            std::vector<double> abs;
            std::vector<double> e(n, 0.0);
            for (size_t i = 0; i < n; ++i) {
                if (base[i] == 0) continue;
                e[i] = std::sqrt(base[i]) * (in.y[i] - pr.Model(best.p, in.x[i]));
                abs.push_back(std::fabs(e[i]));
            }
            const double s = Median(abs) / 0.6745;
            if (!(s > 0)) break;
            double change = 0;
            for (size_t i = 0; i < n; ++i) {
                if (base[i] == 0) continue;
                const double u = e[i] / (4.685 * s);
                const double nw = std::fabs(u) < 1 ? (1 - u * u) * (1 - u * u) : 0.0;
                change = std::max(change, std::fabs(nw - rw[i]));
                rw[i] = nw;
                pr.w[i] = base[i] * nw;
            }
            best = Levenberg(pr, best.p);
            if (change < 1e-6) break;
        }
        out.robustWeights = rw;
        used = 0;
        for (size_t i = 0; i < n; ++i)
            if (pr.w[i] > 0) ++used;
    }

    out.p = best.p;
    out.iterations = best.iterations;
    out.converged = best.converged;
    out.n = used;
    out.k = m;
    out.dof = used - m;

    std::vector<double> r;
    out.sse = pr.Residuals(out.p, r);
    double sw = 0, swy = 0;
    for (size_t i = 0; i < n; ++i) {
        if (pr.w[i] == 0) continue;
        sw += pr.w[i];
        swy += pr.w[i] * in.y[i];
    }
    const double ybar = sw > 0 ? swy / sw : 0;
    double sst = 0;
    for (size_t i = 0; i < n; ++i)
        if (pr.w[i] > 0) sst += pr.w[i] * (in.y[i] - ybar) * (in.y[i] - ybar);
    out.r2 = sst > 0 ? 1 - out.sse / sst : (out.sse == 0 ? 1.0 : NAN);
    out.adjR2 = out.dof > 0 ? 1 - (1 - out.r2) * (used - 1) / (double)out.dof : NAN;
    out.rmse = std::sqrt(out.sse / used);
    out.sigma2 = out.dof > 0 ? out.sse / out.dof : NAN;
    const double ll = used * std::log(std::max(out.sse / used, 1e-300));
    const int kk = m + 1;  // parameters plus the error variance
    out.aic = ll + 2.0 * kk;
    out.aicc = (used - kk - 1) > 0 ? out.aic + 2.0 * kk * (kk + 1) / (used - kk - 1) : NAN;
    out.bic = ll + kk * std::log((double)used);
    out.tValue = TQuantile(in.confidence, out.dof);

    // Covariance from a central-difference Jacobian at the solution.
    out.se.assign((size_t)np, NAN);
    out.ciLo.assign((size_t)np, NAN);
    out.ciHi.assign((size_t)np, NAN);
    out.cov.assign((size_t)np, std::vector<double>((size_t)np, 0.0));
    for (int i = 0; i < np; ++i)
        if (out.fixed[(size_t)i]) out.se[(size_t)i] = 0;
    if (m > 0 && out.dof > 0) {
        Mat J;
        pr.Jacobian(out.p, r, true, J);
        // Scale columns to unit length first (see Levenberg), then undo it.
        std::vector<double> colScale((size_t)m, 1.0);
        for (int j = 0; j < m; ++j) {
            double s2 = 0;
            for (int i = 0; i < J.rows; ++i) s2 += J(i, j) * J(i, j);
            colScale[(size_t)j] = s2 > 0 ? std::sqrt(s2) : 1.0;
            for (int i = 0; i < J.rows; ++i) J(i, j) /= colScale[(size_t)j];
        }
        std::vector<double> none, diag;
        HouseholderQR(J, none, diag);
        Mat R = ExtractR(J, diag), Rinv;
        if (InvertUpper(R, Rinv)) {
            for (int a = 0; a < m; ++a)
                for (int b = 0; b < m; ++b) {
                    double s = 0;
                    for (int k = std::max(a, b); k < m; ++k) s += Rinv(a, k) * Rinv(b, k);
                    out.cov[(size_t)pr.freeIdx[(size_t)a]][(size_t)pr.freeIdx[(size_t)b]] =
                        s * out.sigma2 / (colScale[(size_t)a] * colScale[(size_t)b]);
                }
            for (int a = 0; a < m; ++a) {
                const int pi = pr.freeIdx[(size_t)a];
                const double se = std::sqrt(std::max(0.0, out.cov[(size_t)pi][(size_t)pi]));
                out.se[(size_t)pi] = se;
                out.ciLo[(size_t)pi] = out.p[(size_t)pi] - out.tValue * se;
                out.ciHi[(size_t)pi] = out.p[(size_t)pi] + out.tValue * se;
            }
        }
    }
    for (int i = 0; i < np; ++i)
        if (out.fixed[(size_t)i]) out.ciLo[(size_t)i] = out.ciHi[(size_t)i] = out.p[(size_t)i];
    out.ok = true;
    return out;
}

bool Fit::Bands(const Expr& e, const FitResult& r, double x, double& y, double& confHalf, double& predHalf) {
    y = e.Eval(&x, r.p.data());
    confHalf = predHalf = NAN;
    if (!r.ok || !std::isfinite(r.sigma2) || !std::isfinite(r.tValue)) return false;
    const size_t np = r.p.size();
    std::vector<double> g(np, 0.0), q = r.p;
    for (size_t j = 0; j < np; ++j) {
        if (r.fixed[j]) continue;
        const double h = 1e-6 * std::max(std::fabs(r.p[j]), 1e-3);
        q[j] = r.p[j] + h;
        const double fp = e.Eval(&x, q.data());
        q[j] = r.p[j] - h;
        const double fm = e.Eval(&x, q.data());
        q[j] = r.p[j];
        g[j] = (fp - fm) / (2 * h);
    }
    double var = 0;
    for (size_t a = 0; a < np; ++a)
        for (size_t b = 0; b < np; ++b) var += g[a] * r.cov[a][b] * g[b];
    if (!std::isfinite(var) || var < 0) return false;
    confHalf = r.tValue * std::sqrt(var);
    predHalf = r.tValue * std::sqrt(var + r.sigma2);
    return std::isfinite(y);
}

double Fit::Derivative(const std::function<double(double)>& f, double x) {
    const double h = 1e-5 * std::max(1.0, std::fabs(x));
    // Richardson extrapolation of central differences.
    const double d1 = (f(x + h) - f(x - h)) / (2 * h);
    const double d2 = (f(x + h / 2) - f(x - h / 2)) / h;
    return (4 * d2 - d1) / 3;
}

namespace {
double SimpsonStep(const std::function<double(double)>& f, double a, double b, double fa, double fm, double fb,
                   double whole, double eps, int depth) {
    const double m = (a + b) / 2, lm = (a + m) / 2, rm = (m + b) / 2;
    const double flm = f(lm), frm = f(rm);
    const double left = (m - a) / 6 * (fa + 4 * flm + fm), right = (b - m) / 6 * (fm + 4 * frm + fb);
    const double diff = left + right - whole;
    if (depth <= 0 || std::fabs(diff) <= 15 * eps) return left + right + diff / 15;
    return SimpsonStep(f, a, m, fa, flm, fm, left, eps / 2, depth - 1) +
           SimpsonStep(f, m, b, fm, frm, fb, right, eps / 2, depth - 1);
}
}  // namespace

double Fit::Integral(const std::function<double(double)>& f, double a, double b) {
    if (a == b) return 0;
    // Split into pieces first so narrow features are not missed.
    const int pieces = 64;
    double total = 0;
    for (int i = 0; i < pieces; ++i) {
        const double x0 = a + (b - a) * i / pieces, x1 = a + (b - a) * (i + 1) / pieces;
        const double f0 = f(x0), f1 = f(x1), fm = f((x0 + x1) / 2);
        const double whole = (x1 - x0) / 6 * (f0 + 4 * fm + f1);
        total += SimpsonStep(f, x0, x1, f0, fm, f1, whole, 1e-12 * std::max(1.0, std::fabs(whole)), 40);
    }
    return total;
}

std::vector<double> Fit::Solve(const std::function<double(double)>& f, double y, double a, double b) {
    std::vector<double> roots;
    const int steps = 4000;
    double px = a, pv = f(a) - y;
    for (int i = 1; i <= steps; ++i) {
        const double x = a + (b - a) * i / steps;
        const double v = f(x) - y;
        if (std::isfinite(pv) && std::isfinite(v)) {
            if (pv == 0) {
                if (roots.empty() || std::fabs(roots.back() - px) > (b - a) * 1e-9) roots.push_back(px);
            } else if ((pv < 0) != (v < 0) && v != 0) {
                double lo = px, hi = x, flo = pv;
                for (int k = 0; k < 200; ++k) {
                    const double mid = (lo + hi) / 2;
                    const double fm = f(mid) - y;
                    if (!std::isfinite(fm)) break;
                    if ((fm < 0) == (flo < 0)) {
                        lo = mid;
                        flo = fm;
                    } else {
                        hi = mid;
                    }
                }
                const double root = (lo + hi) / 2;
                // A real crossing, not a jump across a pole.
                if (std::fabs(f(root) - y) < 1e-6 * (std::fabs(y) + 1) + 1e-3 * std::fabs(v - pv))
                    roots.push_back(root);
            }
        }
        px = x;
        pv = v;
    }
    if (std::isfinite(pv) && pv == 0) roots.push_back(b);
    return roots;
}
