// Engine.h - from a table and a model to a finished fit. Shared by the
// main window, "Best fit" and "Batch fit".
#pragma once
#include <memory>

#include "Data.h"

// The points of a table: every row whose X and Y are numbers.
struct Points {
    std::vector<double> x, y, w;  // w: 1/sigma^2 (1 without an error column; 0 = unusable sigma)
    std::vector<int> rows;        // table row of each point
    std::vector<char> excluded;   // left out by the user
    int badSigma = 0;             // rows whose sigma is missing or not above 0
};
Points GatherPoints(const Table& t, int xCol, int yCol, int sigmaCol, const std::vector<bool>* excluded);

struct FitRun {
    bool ok = false;
    std::wstring error;
    bool spline = false;
    std::shared_ptr<Expr> expr;
    std::shared_ptr<Models::Spline> sp;
    FitResult r;
    double Eval(double x) const;
};

// Fits the included points (`x`, `y`, `w` hold only those).
FitRun FitModel(ModelKind kind, int order, const std::wstring& customEquation, const std::vector<ParamOption>& options,
                bool robust, int confidence, const std::vector<double>& x, const std::vector<double>& y,
                const std::vector<double>& w);

// Identifier-safe names for table columns (for formulas).
std::vector<std::wstring> ColumnVariables(const Table& t);
