// NumFormat.cpp - number formatting.
#include "NumFormat.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {
std::wstring Tidy(std::wstring s) {
    // Split off the exponent.
    std::wstring exp;
    const size_t e = s.find_first_of(L"eE");
    if (e != std::wstring::npos) {
        exp = s.substr(e + 1);
        s = s.substr(0, e);
        // "+05" -> "5", "-007" -> "-7"
        bool neg = !exp.empty() && exp[0] == L'-';
        size_t i = (!exp.empty() && (exp[0] == L'-' || exp[0] == L'+')) ? 1 : 0;
        while (i + 1 < exp.size() && exp[i] == L'0') ++i;
        exp = (neg ? L"-" : L"") + exp.substr(i);
    }
    if (s.find(L'.') != std::wstring::npos) {
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
    }
    if (s == L"-0") s = L"0";
    return exp.empty() ? s : s + L"e" + exp;
}
}  // namespace

std::wstring FormatNumber(double v, int sig) {
    if (std::isnan(v)) return L"\x2013";
    if (std::isinf(v)) return v > 0 ? L"\x221E" : L"\x2212\x221E";
    if (v == 0) return L"0";
    if (sig < 1) sig = 1;
    if (sig > 17) sig = 17;
    wchar_t b[64];
    const double a = std::fabs(v);
    if (a >= 1e-4 && a < std::pow(10.0, sig)) {
        // Fixed notation with `sig` significant digits.
        const int digits = sig - 1 - (int)std::floor(std::log10(a));
        swprintf_s(b, L"%.*f", digits < 0 ? 0 : digits > 20 ? 20 : digits, v);
    } else {
        swprintf_s(b, L"%.*e", sig - 1, v);
    }
    return Tidy(b);
}

std::wstring FormatTick(double v, double step) {
    if (std::fabs(v) < step * 1e-9) v = 0;
    const double a = std::fabs(v);
    wchar_t b[64];
    if (a != 0 && (a >= 1e6 || a < 1e-4)) {
        const int sig = std::max(1, (int)std::ceil(std::log10(a / step)) + 1);
        swprintf_s(b, L"%.*e", std::min(sig, 12) - 1 > 0 ? std::min(sig, 12) - 1 : 0, v);
        return Tidy(b);
    }
    int decimals = step > 0 ? (int)std::ceil(-std::log10(step) - 1e-9) : 0;
    if (decimals < 0) decimals = 0;
    if (decimals > 12) decimals = 12;
    swprintf_s(b, L"%.*f", decimals, v);
    return Tidy(b);
}

std::wstring FormatExact(double v) {
    if (std::isnan(v)) return L"";
    wchar_t b[64];
    swprintf_s(b, L"%.15g", v);
    return Tidy(b);
}
