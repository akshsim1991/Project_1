package com.featherpdf.viewer

/**
 * Results of a text search, merged as they stream in from the worker and
 * kept sorted by page so next/previous and per-page lookups are simple.
 */
class SearchState {
    var id = 0
    var query = ""
    var running = false
    var pagesDone = 0
    var pageCount = 0
    val matches = ArrayList<SearchHit>()
    var current = -1

    fun reset() {
        matches.clear()
        current = -1
        running = false
        pagesDone = 0
    }

    /** Merges one page's hits; returns true when this produced the first match. */
    fun add(hits: List<SearchHit>): Boolean {
        if (hits.isEmpty()) return false
        val page = hits[0].page
        var pos = matches.indexOfFirst { it.page > page }
        if (pos < 0) pos = matches.size
        matches.addAll(pos, hits)
        if (current >= pos) current += hits.size
        if (current < 0) {
            current = pos  // the worker searches from the reading position onward
            return true
        }
        return false
    }

    fun next() {
        if (matches.isNotEmpty()) current = (current + 1).mod(matches.size)
    }

    fun prev() {
        if (matches.isNotEmpty()) current = (current - 1).mod(matches.size)
    }

    fun currentHit(): SearchHit? = matches.getOrNull(current)

    /** Indices of matches on [page]. */
    fun rangeFor(page: Int): IntRange {
        var lo = 0
        var hi = matches.size
        while (lo < hi) {
            val mid = (lo + hi) ushr 1
            if (matches[mid].page < page) lo = mid + 1 else hi = mid
        }
        var end = lo
        while (end < matches.size && matches[end].page == page) end++
        return lo until end
    }

    fun status(): String = when {
        query.isEmpty() -> ""
        matches.isEmpty() && running -> "Searching… ${if (pageCount > 0) pagesDone * 100 / pageCount else 0}%"
        matches.isEmpty() -> "No results"
        else -> "${current + 1} of ${matches.size}${if (running) "+" else ""}"
    }
}
