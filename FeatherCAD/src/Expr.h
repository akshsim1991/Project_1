// Expr.h - formulas for variables and dimensions: numbers, names, + - * /
// ^, brackets and the functions sin cos tan asin acos atan sqrt abs min
// max round floor ceil, and pi. Angles are in degrees, as in the rest of
// the program.
//
// Plain C++ (no Windows).
#pragma once
#include <functional>
#include <string>
#include <vector>

// Looks up a name's value; false if there is no such name.
using NameLookup = std::function<bool(const std::wstring& name, double& value)>;

bool EvalExpression(const std::wstring& text, const NameLookup& lookup, double& value, std::wstring& error);
// The names a formula uses (to find the order to work them out in).
std::vector<std::wstring> ExpressionNames(const std::wstring& text);
// A name variables and dimensions may have: a letter, then letters, digits and _.
bool ValidName(const std::wstring& name);
