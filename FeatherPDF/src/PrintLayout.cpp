// PrintLayout.cpp - see PrintLayout.h.
#include "PrintLayout.h"

#include <algorithm>
#include <cmath>

namespace {
SizeF PageSize(const std::vector<SizeF>& sizes, int page) {
    if (page < 0 || page >= (int)sizes.size()) return {595.28f, 841.89f};
    return sizes[(size_t)page];
}

// Columns and rows of n pages on a sheet that is landscape or not.
void Grid(int n, bool landscape, int& cols, int& rows) {
    switch (n) {
        case 2: cols = 2, rows = 1; break;
        case 4: cols = 2, rows = 2; break;
        case 6: cols = 3, rows = 2; break;
        case 9: cols = 3, rows = 3; break;
        default: cols = rows = 1; break;
    }
    if (!landscape) std::swap(cols, rows);
}
}  // namespace

bool PrintLandscape(const PrintOptions& o, const std::vector<SizeF>& sizes) {
    if (o.orientation == 1) return false;
    if (o.orientation == 2) return true;
    const SizeF first = PageSize(sizes, o.pages.empty() ? 0 : o.pages.front());
    const bool wide = first.w > first.h;
    if (o.booklet) return !wide;               // two upright pages side by side
    if (o.perSheet == 2 || o.perSheet == 6) return !wide;
    return wide;                               // 1, 4 and 9: like the pages
}

std::vector<PrintSheet> LayoutSheets(const PrintOptions& o, const std::vector<SizeF>& sizes, const PaperInfo& paper,
                                     bool withCopies) {
    std::vector<PrintSheet> sheets;
    if (o.pages.empty()) return sheets;
    const bool landscape = paper.w > paper.h;
    const int per = o.booklet ? 2 : std::max(1, o.perSheet);
    int cols = 1, rows = 1;
    Grid(per, landscape, cols, rows);

    // Which page goes in which place (-1: left empty).
    std::vector<std::vector<int>> slots;
    if (o.booklet) {
        const int n = (int)o.pages.size();
        const int padded = (n + 3) / 4 * 4;
        auto at = [&](int i) { return i < n ? o.pages[(size_t)i] : -1; };
        for (int s = 0; s < padded / 4; ++s) {
            slots.push_back({at(padded - 1 - 2 * s), at(2 * s)});      // front
            slots.push_back({at(2 * s + 1), at(padded - 2 - 2 * s)});  // back
        }
    } else {
        for (size_t i = 0; i < o.pages.size(); i += (size_t)per) {
            std::vector<int> cells;
            for (size_t k = 0; k < (size_t)per; ++k) cells.push_back(i + k < o.pages.size() ? o.pages[i + k] : -1);
            slots.push_back(cells);
        }
    }

    const RectF area = paper.printable;
    const float gap = per > 1 && !o.booklet ? 12.0f : 0.0f;
    const float cw = std::max(1.0f, (area.right - area.left - (cols - 1) * gap) / cols);
    const float ch = std::max(1.0f, (area.bottom - area.top - (rows - 1) * gap) / rows);
    for (const auto& sheetSlots : slots) {
        PrintSheet sheet;
        for (int i = 0; i < (int)sheetSlots.size(); ++i) {
            const int page = sheetSlots[(size_t)i];
            if (page < 0) continue;
            const int col = i % cols, row = i / cols;
            const float cx = area.left + col * (cw + gap), cy = area.top + row * (ch + gap);
            const SizeF s = PageSize(sizes, page);
            // A page turned across the place it goes is printed turned.
            const bool turn = std::fabs(s.w - s.h) > 1 && (s.w > s.h) != (cw > ch);
            const float pw = turn ? s.h : s.w, ph = turn ? s.w : s.h;
            const float fit = std::min(cw / std::max(1.0f, pw), ch / std::max(1.0f, ph));
            float k = fit;
            if (per == 1 && o.scale == 1) k = 1;
            if (per == 1 && o.scale == 2) k = std::min(1.0f, fit);
            const float w = pw * k, h = ph * k;
            float x = cx + (cw - w) / 2;
            if (o.booklet) x = col == 0 ? cx + cw - w : cx;  // against the fold
            const float y = cy + (ch - h) / 2;
            PrintCell cell;
            cell.page = page;
            cell.rect = {x, y, x + w, y + h};
            cell.rotate = turn ? 1 : 0;
            sheet.cells.push_back(cell);
        }
        sheets.push_back(sheet);
    }

    const int copies = withCopies ? std::max(1, o.copies) : 1;
    if (copies > 1) {
        std::vector<PrintSheet> all;
        if (o.collate || o.booklet) {  // a booklet is only useful as whole sets
            for (int c = 0; c < copies; ++c) all.insert(all.end(), sheets.begin(), sheets.end());
        } else {
            for (const PrintSheet& s : sheets) all.insert(all.end(), (size_t)copies, s);
        }
        sheets.swap(all);
    }
    return sheets;
}
