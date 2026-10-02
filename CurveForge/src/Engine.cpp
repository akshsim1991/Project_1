// Engine.cpp - gathering points and running a model fit.
#include "Engine.h"

#include <algorithm>
#include <cwctype>

Points GatherPoints(const Table& t, int xCol, int yCol, int sigmaCol, const std::vector<bool>* excluded) {
    Points p;
    for (int r = 0; r < t.Rows(); ++r) {
        const double x = t.Value(r, xCol), y = t.Value(r, yCol);
        if (!std::isfinite(x) || !std::isfinite(y)) continue;
        double w = 1;
        if (sigmaCol >= 0) {
            const double s = t.Value(r, sigmaCol);
            if (std::isfinite(s) && s > 0) {
                w = 1 / (s * s);
            } else {
                w = 0;
                ++p.badSigma;
            }
        }
        p.x.push_back(x);
        p.y.push_back(y);
        p.w.push_back(w);
        p.rows.push_back(r);
        p.excluded.push_back(excluded && r < (int)excluded->size() && (*excluded)[(size_t)r] ? 1 : 0);
    }
    return p;
}

double FitRun::Eval(double x) const {
    if (spline) return sp ? sp->Eval(x) : NAN;
    if (!expr || r.p.size() != expr->Params().size()) return NAN;
    return expr->Eval(&x, r.p.data());
}

FitRun FitModel(ModelKind kind, int order, const std::wstring& customEquation, const std::vector<ParamOption>& options,
                bool robust, int confidence, const std::vector<double>& x, const std::vector<double>& y,
                const std::vector<double>& w) {
    FitRun run;
    if (kind == ModelKind::Spline) {
        run.spline = true;
        run.sp = std::make_shared<Models::Spline>();
        if (!run.sp->Build(x, y)) {
            run.error = L"A spline needs at least 2 points with different x values.";
            return run;
        }
        run.ok = true;
        run.r.ok = true;
        run.r.n = (int)run.sp->x.size();
        return run;
    }
    const ModelInfo& info = Models::Info(kind);
    const std::wstring equation = kind == ModelKind::Custom ? customEquation : Models::Formula(kind, order);
    run.expr = std::make_shared<Expr>();
    std::wstring err;
    if (!run.expr->Parse(equation, {L"x"}, err)) {
        run.error = L"The equation can not be read: " + err;
        return run;
    }
    if (!run.expr->UsesVariable(0)) {
        run.error = L"The equation does not use x. Write it in terms of x, for example y = a*exp(-b*x) + c.";
        return run;
    }
    if (info.positiveX) {
        for (double v : x)
            if (v <= 0) {
                run.error = std::wstring(L"The ") + info.name +
                            L" model needs x values above 0. Leave out the rows with x \x2264 0, or choose another model.";
                return run;
            }
    }

    FitInput in;
    in.x = x;
    in.y = y;
    in.w = w;
    in.expr = run.expr.get();
    in.options = options;
    in.robust = robust;
    in.confidence = confidence / 100.0;
    in.multiStart = kind == ModelKind::Custom || !info.linear;
    // Starting values: estimated from the data, then the user's own.
    const auto guess = Models::Guess(kind, order, x, y);
    for (const std::wstring& name : run.expr->Params()) {
        double v = 1;
        for (const auto& g : guess)
            if (g.first == name) v = g.second;
        in.init.push_back(v);
    }
    // Built-in models already have good starting values; extra random starts
    // are only needed when the first attempt fails.
    if (kind != ModelKind::Custom) in.multiStart = false;
    run.r = Fit::Run(in);
    if (!run.r.ok) {
        run.error = run.r.error;
        return run;
    }
    run.ok = true;
    return run;
}

std::vector<std::wstring> ColumnVariables(const Table& t) {
    std::vector<std::wstring> out;
    for (int c = 0; c < t.Cols(); ++c) {
        std::wstring n;
        for (wchar_t ch : t.names[(size_t)c]) n += (iswalnum(ch) || ch == L'_') ? ch : L'_';
        while (n.find(L"__") != std::wstring::npos) n.erase(n.find(L"__"), 1);
        while (!n.empty() && n.back() == L'_') n.pop_back();
        if (n.empty()) n = L"c" + std::to_wstring(c + 1);
        if (iswdigit(n[0])) n = L"c" + n;
        static const wchar_t* reserved[] = {L"sin", L"cos", L"tan", L"asin", L"acos", L"atan", L"sinh", L"cosh",
                                            L"tanh", L"exp", L"ln", L"log", L"log10", L"log2", L"sqrt", L"abs",
                                            L"sign", L"erf", L"floor", L"ceil", L"pow", L"min", L"max", L"atan2",
                                            L"pi", L"e", L"row"};
        for (const wchar_t* r : reserved)
            if (_wcsicmp(n.c_str(), r) == 0) n += L"_";
        std::wstring unique = n;
        for (int k = 2; std::find(out.begin(), out.end(), unique) != out.end(); ++k) unique = n + L"_" + std::to_wstring(k);
        out.push_back(unique);
    }
    return out;
}
