// Expr.cpp - see Expr.h.
#include "Expr.h"

#include <cmath>
#include <cwctype>
#include <vector>

#include "Geometry.h"

namespace {
bool NameChar(wchar_t c, bool first) { return std::iswalpha(c) || c == L'_' || (!first && std::iswdigit(c)); }

class Parser {
public:
    Parser(const std::wstring& s, const NameLookup& lookup) : m_s(s), m_lookup(lookup) {}

    bool Parse(double& v, std::wstring& error) {
        if (!Sum(v)) {
            error = m_error;
            return false;
        }
        Space();
        if (m_i != m_s.size()) {
            error = L"Unexpected \"" + m_s.substr(m_i, 1) + L"\".";
            return false;
        }
        if (!std::isfinite(v)) {
            error = L"The result is not a number (division by zero?).";
            return false;
        }
        return true;
    }

private:
    void Space() {
        while (m_i < m_s.size() && std::iswspace(m_s[m_i])) ++m_i;
    }
    bool Fail(const std::wstring& e) {
        if (m_error.empty()) m_error = e;
        return false;
    }
    bool Eat(wchar_t c) {
        Space();
        if (m_i < m_s.size() && m_s[m_i] == c) {
            ++m_i;
            return true;
        }
        return false;
    }
    bool Sum(double& v) {
        if (!Product(v)) return false;
        for (;;) {
            double r;
            if (Eat(L'+')) {
                if (!Product(r)) return false;
                v += r;
            } else if (Eat(L'-')) {
                if (!Product(r)) return false;
                v -= r;
            } else {
                return true;
            }
        }
    }
    bool Product(double& v) {
        if (!Power(v)) return false;
        for (;;) {
            double r;
            if (Eat(L'*')) {
                if (!Power(r)) return false;
                v *= r;
            } else if (Eat(L'/')) {
                if (!Power(r)) return false;
                if (r == 0) return Fail(L"Division by zero.");
                v /= r;
            } else {
                return true;
            }
        }
    }
    bool Power(double& v) {
        if (!Unary(v)) return false;
        if (Eat(L'^')) {
            double r;
            if (!Power(r)) return false;  // right to left: 2^3^2 = 2^9
            v = std::pow(v, r);
        }
        return true;
    }
    bool Unary(double& v) {
        if (Eat(L'-')) {
            if (!Unary(v)) return false;
            v = -v;
            return true;
        }
        if (Eat(L'+')) return Unary(v);
        return Atom(v);
    }
    bool Atom(double& v) {
        Space();
        if (m_i >= m_s.size()) return Fail(L"The formula ends too soon.");
        if (Eat(L'(')) {
            if (!Sum(v)) return false;
            if (!Eat(L')')) return Fail(L"A \")\" is missing.");
            return true;
        }
        const wchar_t c = m_s[m_i];
        if (std::iswdigit(c) || c == L'.') {
            size_t j = m_i;
            while (j < m_s.size() && (std::iswdigit(m_s[j]) || m_s[j] == L'.')) ++j;
            if (j < m_s.size() && (m_s[j] == L'e' || m_s[j] == L'E') && j + 1 < m_s.size() &&
                (std::iswdigit(m_s[j + 1]) || ((m_s[j + 1] == L'-' || m_s[j + 1] == L'+') && j + 2 < m_s.size() && std::iswdigit(m_s[j + 2])))) {
                j += 2;
                while (j < m_s.size() && std::iswdigit(m_s[j])) ++j;
            }
            const std::wstring num = m_s.substr(m_i, j - m_i);
            wchar_t* end = nullptr;
            v = std::wcstod(num.c_str(), &end);
            if (!end || *end) return Fail(L"\"" + num + L"\" is not a number.");
            m_i = j;
            return true;
        }
        if (!NameChar(c, true)) return Fail(L"Unexpected \"" + std::wstring(1, c) + L"\".");
        size_t j = m_i;
        while (j < m_s.size() && NameChar(m_s[j], j == m_i)) ++j;
        const std::wstring name = m_s.substr(m_i, j - m_i);
        m_i = j;
        std::wstring low = name;
        for (wchar_t& ch : low) ch = (wchar_t)std::towlower(ch);
        if (Eat(L'(')) {  // a function
            std::vector<double> args;
            if (!Eat(L')')) {
                for (;;) {
                    double a;
                    if (!Sum(a)) return false;
                    args.push_back(a);
                    if (Eat(L')')) break;
                    if (!Eat(L',')) return Fail(L"A \",\" or \")\" is missing.");
                }
            }
            return Function(low, args, v);
        }
        if (low == L"pi") {
            v = kPi;
            return true;
        }
        if (!m_lookup || !m_lookup(name, v)) return Fail(L"There is no variable or dimension named \"" + name + L"\".");
        return true;
    }
    bool Function(const std::wstring& f, const std::vector<double>& a, double& v) {
        auto one = [&](double (*fn)(double)) {
            if (a.size() != 1) return Fail(f + L"() takes one value.");
            v = fn(a[0]);
            return true;
        };
        if (f == L"sin") return one([](double x) { return std::sin(x * kPi / 180); });
        if (f == L"cos") return one([](double x) { return std::cos(x * kPi / 180); });
        if (f == L"tan") return one([](double x) { return std::tan(x * kPi / 180); });
        if (f == L"asin") return one([](double x) { return std::asin(x) * 180 / kPi; });
        if (f == L"acos") return one([](double x) { return std::acos(x) * 180 / kPi; });
        if (f == L"atan") return one([](double x) { return std::atan(x) * 180 / kPi; });
        if (f == L"sqrt") {
            if (a.size() != 1) return Fail(L"sqrt() takes one value.");
            if (a[0] < 0) return Fail(L"sqrt() of a negative number.");
            v = std::sqrt(a[0]);
            return true;
        }
        if (f == L"abs") return one([](double x) { return std::fabs(x); });
        if (f == L"round") return one([](double x) { return std::round(x); });
        if (f == L"floor") return one([](double x) { return std::floor(x); });
        if (f == L"ceil") return one([](double x) { return std::ceil(x); });
        if (f == L"min" || f == L"max") {
            if (a.empty()) return Fail(f + L"() needs values.");
            v = a[0];
            for (double x : a) v = f == L"min" ? std::fmin(v, x) : std::fmax(v, x);
            return true;
        }
        return Fail(L"There is no function \"" + f + L"\".");
    }

    const std::wstring& m_s;
    const NameLookup& m_lookup;
    size_t m_i = 0;
    std::wstring m_error;
};
}  // namespace

bool EvalExpression(const std::wstring& text, const NameLookup& lookup, double& value, std::wstring& error) {
    std::wstring s = text;
    if (!s.empty() && s[0] == L'=') s.erase(0, 1);  // "=2*Width" as in a spreadsheet
    Parser p(s, lookup);
    return p.Parse(value, error);
}

std::vector<std::wstring> ExpressionNames(const std::wstring& text) {
    std::vector<std::wstring> names;
    for (size_t i = 0; i < text.size();) {
        if (NameChar(text[i], true) && (i == 0 || !NameChar(text[i - 1], false))) {
            size_t j = i;
            while (j < text.size() && NameChar(text[j], j == i)) ++j;
            size_t k = j;
            while (k < text.size() && std::iswspace(text[k])) ++k;
            // A function call or a number's exponent is not a name.
            const bool exponent = i > 0 && (text[i] == L'e' || text[i] == L'E') && std::iswdigit(text[i - 1]);
            if ((k >= text.size() || text[k] != L'(') && !exponent) {
                std::wstring n = text.substr(i, j - i);
                std::wstring low = n;
                for (wchar_t& c : low) c = (wchar_t)std::towlower(c);
                if (low != L"pi") names.push_back(n);
            }
            i = j;
        } else {
            ++i;
        }
    }
    return names;
}

bool ValidName(const std::wstring& name) {
    if (name.empty() || name.size() > 40 || !NameChar(name[0], true)) return false;
    for (wchar_t c : name)
        if (!NameChar(c, false)) return false;
    std::wstring low = name;
    for (wchar_t& c : low) c = (wchar_t)std::towlower(c);
    return low != L"pi";
}
