// NumFormat.h - numbers for people: "0.000123", "1.23e-8", "12345.6".
#pragma once
#include "Common.h"

// `sig` significant digits, trailing zeros removed, exponent written short
// ("1.5e-7" rather than "1.500000e-007").
std::wstring FormatNumber(double v, int sig = 6);
// Tick labels: as few digits as the tick spacing needs.
std::wstring FormatTick(double v, double step);
// Full precision, for values copied to other programs.
std::wstring FormatExact(double v);
