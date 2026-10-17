// GdiPlusInc.h - GDI+ needs min/max and IStream, which NOMINMAX and
// WIN32_LEAN_AND_MEAN remove.
#pragma once
#include "Common.h"

#include <algorithm>

#include <objidl.h>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
