// Models.h - the built-in models, their equations and automatic starting
// values.
#pragma once
#include "Common.h"

enum class ModelKind : int {
    Linear,
    Proportional,
    Polynomial,
    Inverse,
    Exponential,
    ExpOffset,
    DoubleExp,
    Logarithmic,
    Power,
    Gaussian,
    Lorentzian,
    GaussianPeaks,
    Logistic,
    DoseResponse,
    Hill,
    MichaelisMenten,
    Sine,
    DampedSine,
    Spline,  // cubic spline through the points (interpolation, no parameters)
    Custom,
    Count
};

struct ModelInfo {
    ModelKind kind;
    const wchar_t* key;      // stable name used in project files
    const wchar_t* name;     // shown to people
    const wchar_t* about;    // what the parameters mean
    bool hasOrder;           // polynomial degree / number of peaks
    bool linear;             // linear in its parameters (one exact solution)
    bool positiveX;          // needs x > 0
};

namespace Models {
const ModelInfo& Info(ModelKind k);
ModelKind FromKey(const std::wstring& key);  // Custom if unknown

// "y = a*exp(b*x)" etc. `order` is the polynomial degree or number of peaks.
std::wstring Formula(ModelKind k, int order);
int DefaultOrder(ModelKind k);
int MinOrder(ModelKind k);
int MaxOrder(ModelKind k);

// Starting values estimated from the data, by parameter name.
std::vector<std::pair<std::wstring, double>> Guess(ModelKind k, int order, const std::vector<double>& x,
                                                   const std::vector<double>& y);

// Natural cubic spline through the points (sorted by x; equal x averaged).
struct Spline {
    std::vector<double> x, y, m;  // m: second derivatives
    bool Build(std::vector<double> xs, std::vector<double> ys);
    double Eval(double v) const;
};
}  // namespace Models
