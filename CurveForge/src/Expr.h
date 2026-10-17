// Expr.h - a small, fast expression compiler for equations such as
// "a*exp(-b*x) + c".
//
// Names are resolved as follows:
//   * the variables given to Parse (e.g. "x", or a table's column names);
//   * the constants pi and e;
//   * function names when followed by "(";
//   * anything else is a parameter, in order of first appearance.
//
// Supported: + - * / ^ (also **), unary minus, implicit multiplication
// ("2x", "3(x+1)"), and the functions sin cos tan asin acos atan sinh cosh
// tanh exp ln log (base 10) log10 log2 sqrt abs sign erf floor ceil
// pow(a,b) min(a,b) max(a,b) atan2(y,x).
#pragma once
#include "Common.h"

class Expr {
public:
    // Parses `text`. A leading "y =" (or any "name =") is ignored. Returns
    // false with a readable message on syntax errors.
    bool Parse(const std::wstring& text, const std::vector<std::wstring>& variables, std::wstring& error);

    const std::vector<std::wstring>& Params() const { return m_params; }
    bool UsesVariable(int index) const;
    bool Empty() const { return m_code.empty(); }

    double Eval(const double* vars, const double* params) const;

    // `text` with each parameter name replaced by its value (formatted by
    // `fmt`), e.g. "a*exp(b*x)" -> "2.31*exp(0.452*x)".
    static std::wstring Substitute(const std::wstring& text, const std::vector<std::wstring>& names,
                                   const std::vector<std::wstring>& values);
    // Replaces whole-word `from` with `to` (e.g. x -> A2 for Excel).
    static std::wstring ReplaceWord(const std::wstring& text, const std::wstring& from, const std::wstring& to);
    // Removes a leading "y =" from an equation.
    static std::wstring RightHandSide(const std::wstring& text);

private:
    enum Op : int { kConst, kVar, kParam, kAdd, kSub, kMul, kDiv, kPow, kNeg, kFunc1, kFunc2 };
    struct Instr {
        Op op;
        int index = 0;      // var/param/function index
        double value = 0;   // constant
    };
    std::vector<Instr> m_code;
    std::vector<double> m_consts;
    std::vector<std::wstring> m_params;
    std::vector<bool> m_varUsed;
    int m_maxStack = 0;

    friend class ExprParser;
};
