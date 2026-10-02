// Expr.cpp - tokenizer, recursive-descent parser and stack evaluator.
#include "Expr.h"

#include <cmath>
#include <cwctype>

#include "Util.h"

namespace {
const double kPi = 3.14159265358979323846;

enum Func1 : int { fSin, fCos, fTan, fAsin, fAcos, fAtan, fSinh, fCosh, fTanh, fExp, fLn, fLog10, fLog2, fSqrt,
                   fAbs, fSign, fErf, fFloor, fCeil };
enum Func2 : int { fPow, fMin, fMax, fAtan2 };

struct FuncName {
    const wchar_t* name;
    int args;
    int id;
};
const FuncName kFuncs[] = {
    {L"sin", 1, fSin},     {L"cos", 1, fCos},     {L"tan", 1, fTan},   {L"asin", 1, fAsin}, {L"acos", 1, fAcos},
    {L"atan", 1, fAtan},   {L"sinh", 1, fSinh},   {L"cosh", 1, fCosh}, {L"tanh", 1, fTanh}, {L"exp", 1, fExp},
    {L"ln", 1, fLn},       {L"log", 1, fLog10},   {L"log10", 1, fLog10}, {L"log2", 1, fLog2}, {L"sqrt", 1, fSqrt},
    {L"abs", 1, fAbs},     {L"sign", 1, fSign},   {L"erf", 1, fErf},   {L"floor", 1, fFloor}, {L"ceil", 1, fCeil},
    {L"pow", 2, fPow},     {L"min", 2, fMin},     {L"max", 2, fMax},   {L"atan2", 2, fAtan2},
};

bool IsIdentStart(wchar_t c) { return iswalpha(c) || c == L'_' || c > 127; }
bool IsIdentChar(wchar_t c) { return iswalnum(c) || c == L'_' || c > 127; }
}  // namespace

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
class ExprParser {
public:
    ExprParser(Expr& e, const std::wstring& s, const std::vector<std::wstring>& vars) : m_e(e), m_s(s), m_vars(vars) {}

    bool Run(std::wstring& error) {
        Next();
        if (m_tok == T::End) return Fail(L"The equation is empty.", error);
        if (!ParseExpr()) return Fail(m_error, error);
        if (m_tok != T::End) return Fail(L"Unexpected \x201C" + Lexeme() + L"\x201D at position " +
                                         std::to_wstring(m_start + 1) + L".", error);
        return true;
    }

private:
    enum class T { End, Num, Ident, Plus, Minus, Star, Slash, Caret, LParen, RParen, Comma, Bad };

    bool Fail(const std::wstring& msg, std::wstring& error) {
        error = msg.empty() ? L"The equation could not be read." : msg;
        return false;
    }

    std::wstring Lexeme() const { return m_s.substr(m_start, m_pos - m_start); }

    void Next() {
        while (m_pos < m_s.size() && iswspace(m_s[m_pos])) ++m_pos;
        m_start = m_pos;
        if (m_pos >= m_s.size()) {
            m_tok = T::End;
            return;
        }
        const wchar_t c = m_s[m_pos];
        if (iswdigit(c) || (c == L'.' && m_pos + 1 < m_s.size() && iswdigit(m_s[m_pos + 1]))) {
            size_t p = m_pos;
            while (p < m_s.size() && (iswdigit(m_s[p]) || m_s[p] == L'.')) ++p;
            if (p < m_s.size() && (m_s[p] == L'e' || m_s[p] == L'E')) {
                size_t q = p + 1;
                if (q < m_s.size() && (m_s[q] == L'+' || m_s[q] == L'-')) ++q;
                if (q < m_s.size() && iswdigit(m_s[q])) {
                    while (q < m_s.size() && iswdigit(m_s[q])) ++q;
                    p = q;
                }
            }
            m_num = _wtof(m_s.substr(m_pos, p - m_pos).c_str());
            m_pos = p;
            m_tok = T::Num;
            return;
        }
        if (IsIdentStart(c)) {
            size_t p = m_pos;
            while (p < m_s.size() && IsIdentChar(m_s[p])) ++p;
            m_ident = m_s.substr(m_pos, p - m_pos);
            m_pos = p;
            m_tok = T::Ident;
            return;
        }
        ++m_pos;
        switch (c) {
            case L'+': m_tok = T::Plus; break;
            case L'-': case 0x2212: m_tok = T::Minus; break;
            case L'*': case 0x00D7: case 0x00B7: case 0x22C5:
                if (c == L'*' && m_pos < m_s.size() && m_s[m_pos] == L'*') {
                    ++m_pos;
                    m_tok = T::Caret;
                } else {
                    m_tok = T::Star;
                }
                break;
            case L'/': case 0x00F7: m_tok = T::Slash; break;
            case L'^': m_tok = T::Caret; break;
            case L'(': case L'[': m_tok = T::LParen; break;
            case L')': case L']': m_tok = T::RParen; break;
            case L',': case L';': m_tok = T::Comma; break;
            default: m_tok = T::Bad; break;
        }
    }

    void Emit(Expr::Op op, int index = 0, double value = 0) {
        m_e.m_code.push_back({op, index, value});
        switch (op) {
            case Expr::kConst: case Expr::kVar: case Expr::kParam: ++m_depth; break;
            case Expr::kNeg: case Expr::kFunc1: break;
            default: --m_depth; break;
        }
        if (m_depth > m_e.m_maxStack) m_e.m_maxStack = m_depth;
    }

    bool ParseExpr() {
        if (!ParseTerm()) return false;
        while (m_tok == T::Plus || m_tok == T::Minus) {
            const bool add = m_tok == T::Plus;
            Next();
            if (!ParseTerm()) return false;
            Emit(add ? Expr::kAdd : Expr::kSub);
        }
        return true;
    }

    bool ParseTerm() {
        if (!ParseUnary()) return false;
        for (;;) {
            if (m_tok == T::Star || m_tok == T::Slash) {
                const bool mul = m_tok == T::Star;
                Next();
                if (!ParseUnary()) return false;
                Emit(mul ? Expr::kMul : Expr::kDiv);
            } else if (m_tok == T::Num || m_tok == T::Ident || m_tok == T::LParen) {
                // Implicit multiplication: "2x", "3(x+1)", "a b".
                if (!ParsePower()) return false;
                Emit(Expr::kMul);
            } else {
                return true;
            }
        }
    }

    bool ParseUnary() {
        if (m_tok == T::Minus) {
            Next();
            if (!ParseUnary()) return false;
            Emit(Expr::kNeg);
            return true;
        }
        if (m_tok == T::Plus) {
            Next();
            return ParseUnary();
        }
        return ParsePower();
    }

    bool ParsePower() {
        if (!ParsePrimary()) return false;
        if (m_tok == T::Caret) {
            Next();
            if (!ParseUnary()) return false;  // right-associative: 2^3^2 = 2^9
            Emit(Expr::kPow);
        }
        return true;
    }

    bool ParsePrimary() {
        switch (m_tok) {
            case T::Num:
                Emit(Expr::kConst, 0, m_num);
                Next();
                return true;
            case T::LParen:
                Next();
                if (!ParseExpr()) return false;
                if (m_tok != T::RParen) return Error(L"A closing bracket \x201C)\x201D is missing.");
                Next();
                return true;
            case T::Ident: {
                const std::wstring name = m_ident;
                const size_t at = m_start;
                Next();
                if (m_tok == T::LParen) {
                    const std::wstring low = ToLower(name);
                    for (const FuncName& f : kFuncs) {
                        if (low != f.name) continue;
                        Next();
                        for (int a = 0; a < f.args; ++a) {
                            if (a > 0) {
                                if (m_tok != T::Comma) return Error(name + L"() needs " + std::to_wstring(f.args) +
                                                                    L" values separated by a comma.");
                                Next();
                            }
                            if (!ParseExpr()) return false;
                        }
                        if (m_tok != T::RParen) return Error(L"A closing bracket \x201C)\x201D is missing after " + name + L"(.");
                        Next();
                        Emit(f.args == 1 ? Expr::kFunc1 : Expr::kFunc2, f.id);
                        return true;
                    }
                    // Not a function: a name followed by brackets means multiplication.
                }
                for (size_t i = 0; i < m_vars.size(); ++i) {
                    if (m_vars[i] == name) {
                        m_e.m_varUsed[i] = true;
                        Emit(Expr::kVar, (int)i);
                        return true;
                    }
                }
                const std::wstring low = ToLower(name);
                if (low == L"pi" || name == L"\x03C0") {
                    Emit(Expr::kConst, 0, kPi);
                    return true;
                }
                if (name == L"e") {
                    Emit(Expr::kConst, 0, 2.718281828459045);
                    return true;
                }
                for (const FuncName& f : kFuncs)
                    if (low == f.name) {
                        m_start = at;
                        return Error(L"\x201C" + name + L"\x201D is a function: write " + name + L"(\x2026).");
                    }
                size_t idx = 0;
                while (idx < m_e.m_params.size() && m_e.m_params[idx] != name) ++idx;
                if (idx == m_e.m_params.size()) m_e.m_params.push_back(name);
                Emit(Expr::kParam, (int)idx);
                return true;
            }
            case T::End: return Error(L"The equation ends too early.");
            case T::RParen: return Error(L"There is an extra closing bracket \x201C)\x201D.");
            case T::Bad: return Error(L"The character \x201C" + Lexeme() + L"\x201D is not allowed.");
            default: return Error(L"A value is missing before \x201C" + Lexeme() + L"\x201D.");
        }
    }

    bool Error(const std::wstring& msg) {
        if (m_error.empty()) m_error = msg;
        return false;
    }

    Expr& m_e;
    const std::wstring& m_s;
    const std::vector<std::wstring>& m_vars;
    size_t m_pos = 0, m_start = 0;
    T m_tok = T::End;
    double m_num = 0;
    std::wstring m_ident, m_error;
    int m_depth = 0;
};

std::wstring Expr::RightHandSide(const std::wstring& text) {
    const size_t eq = text.find(L'=');
    return eq == std::wstring::npos ? text : text.substr(eq + 1);
}

bool Expr::Parse(const std::wstring& text, const std::vector<std::wstring>& variables, std::wstring& error) {
    m_code.clear();
    m_params.clear();
    m_maxStack = 0;
    m_varUsed.assign(variables.size(), false);
    const std::wstring rhs = RightHandSide(text);
    ExprParser p(*this, rhs, variables);
    if (!p.Run(error)) {
        m_code.clear();
        m_params.clear();
        return false;
    }
    return true;
}

bool Expr::UsesVariable(int index) const { return index >= 0 && index < (int)m_varUsed.size() && m_varUsed[(size_t)index]; }

double Expr::Eval(const double* vars, const double* params) const {
    double stackBuf[64];
    std::vector<double> big;
    double* st = stackBuf;
    if (m_maxStack > 64) {
        big.resize((size_t)m_maxStack);
        st = big.data();
    }
    int sp = 0;
    for (const Instr& in : m_code) {
        switch (in.op) {
            case kConst: st[sp++] = in.value; break;
            case kVar: st[sp++] = vars[in.index]; break;
            case kParam: st[sp++] = params[in.index]; break;
            case kAdd: --sp; st[sp - 1] += st[sp]; break;
            case kSub: --sp; st[sp - 1] -= st[sp]; break;
            case kMul: --sp; st[sp - 1] *= st[sp]; break;
            case kDiv: --sp; st[sp - 1] /= st[sp]; break;
            case kPow: {
                --sp;
                const double b = st[sp], a = st[sp - 1];
                if (b == 2) st[sp - 1] = a * a;
                else if (b == 3) st[sp - 1] = a * a * a;
                else st[sp - 1] = std::pow(a, b);
                break;
            }
            case kNeg: st[sp - 1] = -st[sp - 1]; break;
            case kFunc1: {
                double& v = st[sp - 1];
                switch (in.index) {
                    case fSin: v = std::sin(v); break;
                    case fCos: v = std::cos(v); break;
                    case fTan: v = std::tan(v); break;
                    case fAsin: v = std::asin(v); break;
                    case fAcos: v = std::acos(v); break;
                    case fAtan: v = std::atan(v); break;
                    case fSinh: v = std::sinh(v); break;
                    case fCosh: v = std::cosh(v); break;
                    case fTanh: v = std::tanh(v); break;
                    case fExp: v = std::exp(v); break;
                    case fLn: v = std::log(v); break;
                    case fLog10: v = std::log10(v); break;
                    case fLog2: v = std::log2(v); break;
                    case fSqrt: v = std::sqrt(v); break;
                    case fAbs: v = std::fabs(v); break;
                    case fSign: v = v > 0 ? 1.0 : v < 0 ? -1.0 : 0.0; break;
                    case fErf: v = std::erf(v); break;
                    case fFloor: v = std::floor(v); break;
                    case fCeil: v = std::ceil(v); break;
                }
                break;
            }
            case kFunc2: {
                --sp;
                const double b = st[sp];
                double& a = st[sp - 1];
                switch (in.index) {
                    case fPow: a = std::pow(a, b); break;
                    case fMin: a = a < b ? a : b; break;
                    case fMax: a = a > b ? a : b; break;
                    case fAtan2: a = std::atan2(a, b); break;
                }
                break;
            }
        }
    }
    return sp == 1 ? st[0] : NAN;
}

std::wstring Expr::ReplaceWord(const std::wstring& text, const std::wstring& from, const std::wstring& to) {
    std::wstring out;
    size_t i = 0;
    while (i < text.size()) {
        if (IsIdentStart(text[i])) {
            size_t j = i;
            while (j < text.size() && IsIdentChar(text[j])) ++j;
            const std::wstring word = text.substr(i, j - i);
            // Skip the exponent of a number such as 1e5 (preceded by a digit).
            const bool afterDigit = i > 0 && (iswdigit(text[i - 1]) || text[i - 1] == L'.');
            out += (!afterDigit && word == from) ? to : word;
            i = j;
        } else {
            out += text[i++];
        }
    }
    return out;
}

std::wstring Expr::Substitute(const std::wstring& text, const std::vector<std::wstring>& names,
                              const std::vector<std::wstring>& values) {
    std::wstring out;
    size_t i = 0;
    while (i < text.size()) {
        if (IsIdentStart(text[i]) && !(i > 0 && (iswdigit(text[i - 1]) || text[i - 1] == L'.'))) {
            size_t j = i;
            while (j < text.size() && IsIdentChar(text[j])) ++j;
            const std::wstring word = text.substr(i, j - i);
            size_t k = 0;
            while (k < names.size() && names[k] != word) ++k;
            if (k < names.size() && k < values.size()) {
                std::wstring v = values[k];
                // A negative value needs brackets unless it starts a group: "(-3*x)".
                size_t back = out.size();
                while (back > 0 && out[back - 1] == L' ') --back;
                const wchar_t prev = back ? out[back - 1] : L'(';
                size_t ahead = j;
                while (ahead < text.size() && text[ahead] == L' ') ++ahead;
                const bool power = ahead < text.size() && text[ahead] == L'^';
                if (!v.empty() && v[0] == L'-' && (power || (prev != L'(' && prev != L',' && prev != L'=')))
                    v = L"(" + v + L")";
                out += v;
            } else {
                out += word;
            }
            i = j;
        } else {
            out += text[i++];
        }
    }
    // Tidy "+ (-3)" into "- 3" and "- (-3)" into "+ 3".
    auto tidy = [&](const wchar_t* from, const wchar_t* to) {
        size_t pos;
        while ((pos = out.find(from)) != std::wstring::npos) {
            const size_t close = out.find(L')', pos);
            if (close == std::wstring::npos) break;
            const size_t len = wcslen(from);
            out = out.substr(0, pos) + to + out.substr(pos + len, close - pos - len) + out.substr(close + 1);
        }
    };
    tidy(L"+ (-", L"- ");
    tidy(L"- (-", L"+ ");
    tidy(L"+(-", L"-");
    tidy(L"-(-", L"+");
    return out;
}
