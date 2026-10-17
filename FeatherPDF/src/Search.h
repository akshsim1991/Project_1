// Search.h - UI-side state of a text search.
//
// The heavy lifting (text extraction, matching) happens page by page on the
// render worker. Results stream in and are merged here, kept sorted by page
// so "next"/"previous" and per-page highlight lookup are simple.
#pragma once
#include <utility>

#include "RenderTypes.h"

class SearchState {
public:
    void Reset() {
        matches.clear();
        current = -1;
        running = false;
        pagesDone = 0;
    }

    // Merges the hits of one page. Returns true if this made the first
    // match of the search available (caller then scrolls to it).
    bool AddHits(std::vector<SearchHit>&& hits);

    // [first, last) indices of matches on `page`.
    std::pair<size_t, size_t> RangeForPage(int page) const;

    void Next() {
        if (!matches.empty()) current = (current + 1) % (int)matches.size();
    }
    void Prev() {
        if (!matches.empty()) current = (current - 1 + (int)matches.size()) % (int)matches.size();
    }
    const SearchHit* Current() const {
        return current >= 0 && current < (int)matches.size() ? &matches[(size_t)current] : nullptr;
    }

    std::wstring StatusText() const;

    uint32_t id = 0;         // increases with every new search
    std::wstring query;      // query of the current/last search
    bool matchCase = false;
    bool running = false;
    int pagesDone = 0;
    int pageCount = 0;
    std::vector<SearchHit> matches;  // sorted by page
    int current = -1;
};
