// Search.cpp - merging and navigation of search results.
#include "Search.h"

#include <algorithm>

bool SearchState::AddHits(std::vector<SearchHit>&& hits) {
    if (hits.empty()) return false;
    const int page = hits.front().page;
    auto pos = std::lower_bound(matches.begin(), matches.end(), page,
                                [](const SearchHit& h, int p) { return h.page < p; });
    const int index = (int)(pos - matches.begin());
    const int added = (int)hits.size();
    matches.insert(pos, std::make_move_iterator(hits.begin()), std::make_move_iterator(hits.end()));

    // Keep the current selection pointing at the same match.
    if (current >= index) current += added;
    // The worker searches from the current page onward, so the first batch
    // with hits contains the first match "after" the reading position.
    if (current < 0) {
        current = index;
        return true;
    }
    return false;
}

std::pair<size_t, size_t> SearchState::RangeForPage(int page) const {
    auto lo = std::lower_bound(matches.begin(), matches.end(), page,
                               [](const SearchHit& h, int p) { return h.page < p; });
    auto hi = std::upper_bound(lo, matches.end(), page,
                               [](int p, const SearchHit& h) { return p < h.page; });
    return {(size_t)(lo - matches.begin()), (size_t)(hi - matches.begin())};
}

std::wstring SearchState::StatusText() const {
    if (query.empty()) return L"";
    if (matches.empty()) {
        if (running) {
            return L"Searching\x2026 " + std::to_wstring(pageCount ? pagesDone * 100 / pageCount : 0) +
                   L"%";
        }
        return L"No results";
    }
    std::wstring s = std::to_wstring(current + 1) + L" of " + std::to_wstring(matches.size());
    if (running) s += L"+";
    return s;
}
