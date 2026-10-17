// Fit.h - least-squares fitting and its statistics.
//
// Every model is an Expr in x with parameters. Fitting uses
// Levenberg-Marquardt, solving each step with a Householder QR
// factorisation (numerically safe even for high-degree polynomials), with:
//   * weights from known errors (w = 1/sigma^2);
//   * robust fitting (iteratively reweighted, Tukey bisquare) that ignores
//     outliers;
//   * fixed parameters and lower/upper bounds;
//   * several starting points for non-linear models.
#pragma once
#include <functional>

#include "Expr.h"

struct ParamOption {
    std::wstring name;
    bool hasInit = false;
    double init = 1;
    bool fixed = false;
    double lo = -INFINITY, hi = INFINITY;
};

struct FitInput {
    std::vector<double> x, y, w;  // w empty = all 1
    const Expr* expr = nullptr;   // in x (variable 0)
    std::vector<double> init;     // starting values (one per parameter)
    std::vector<ParamOption> options;  // matched to parameters by name
    bool robust = false;
    bool multiStart = true;       // try other starting points if needed
    double confidence = 0.95;
};

struct FitResult {
    bool ok = false;
    std::wstring error;
    std::vector<std::wstring> names;
    std::vector<double> p, se, ciLo, ciHi;
    std::vector<bool> fixed;
    std::vector<std::vector<double>> cov;  // over all parameters (0 for fixed ones)
    double sse = 0, rmse = 0, r2 = 0, adjR2 = 0, aic = 0, aicc = 0, bic = 0, sigma2 = 0;
    double tValue = 0, confidence = 0.95;
    int n = 0, k = 0, dof = 0, iterations = 0;
    bool converged = false;
    std::vector<double> robustWeights;  // robust fits: final weight of each point (0..1)
};

namespace Fit {
FitResult Run(const FitInput& in);

// Value, confidence band and prediction band of a fitted model at x.
// Returns false if the band can not be computed (e.g. no covariance).
bool Bands(const Expr& e, const FitResult& r, double x, double& y, double& confHalf, double& predHalf);

// Two-sided Student-t critical value, e.g. TQuantile(0.95, 10) = 2.228.
double TQuantile(double confidence, int dof);

double Derivative(const std::function<double(double)>& f, double x);
double Integral(const std::function<double(double)>& f, double a, double b);
// Every x in [a, b] with f(x) = y.
std::vector<double> Solve(const std::function<double(double)>& f, double y, double a, double b);
}  // namespace Fit
