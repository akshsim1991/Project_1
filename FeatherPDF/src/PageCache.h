// PageCache.h - bounded LRU cache of rendered page tiles.
//
// MEMORY STRATEGY
// ---------------
// * Pages are never rendered as whole bitmaps. They are cut into tiles
//   (a full-width row of up to 2048 px, or 1024 px columns for very wide
//   pages, each 512 px tall). Only tiles intersecting the viewport, plus a
//   small prefetch band around it, are ever requested.
// * The cache has a byte budget derived from the viewport size (see
//   PdfView::UpdateCacheBudget), typically 64-180 MB. When an insert pushes
//   it over budget, least-recently-used tiles are freed immediately.
// * Tiles used in the current frame are "pinned" and never evicted, so a
//   tight budget can not cause a render/evict/render loop.
// * After a zoom change, tiles of the old scale remain only as stretched
//   placeholders until the new scale is fully rendered, then are dropped.
// * The cache is emptied when the window is minimised.
//
// The cache is only touched by the UI thread, so it needs no locking.
#pragma once
#include <list>
#include <unordered_map>

#include "RenderTypes.h"

struct TileKey {
    int page = 0, scaleKey = 0, tx = 0, ty = 0;
    bool operator==(const TileKey& o) const {
        return page == o.page && scaleKey == o.scaleKey && tx == o.tx && ty == o.ty;
    }
};

struct TileKeyHash {
    size_t operator()(const TileKey& k) const {
        uint64_t h = (uint64_t)(uint32_t)k.page * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)(uint32_t)k.scaleKey * 0xC2B2AE3D27D4EB4Full;
        h ^= ((uint64_t)(uint32_t)k.tx << 32 | (uint32_t)k.ty) * 0x165667B19E3779F9ull;
        return (size_t)(h ^ (h >> 29));
    }
};

struct Tile {
    TileKey key;
    int x = 0, y = 0;  // position inside the page bitmap at key.scaleKey
    PixelBuffer pixels;
    uint64_t frame = 0;  // last frame in which the tile was drawn
    bool stale = false;  // content out of date (document edited): shown
                         // only as a placeholder until re-rendered
};

class PageCache {
public:
    ~PageCache() { Clear(); }

    void SetBudget(size_t bytes) { m_budget = bytes; }
    size_t Budget() const { return m_budget; }
    size_t Bytes() const { return m_bytes; }

    // Start a new paint pass; tiles fetched with Use() during it are pinned.
    void BeginFrame() { ++m_frame; }

    // Returns the tile (and marks it most recently used and pinned for the
    // current frame), or nullptr. Stale tiles are pinned but not returned.
    const Tile* Use(const TileKey& key);
    // Like Use() but does not pin: for prefetched tiles.
    bool Touch(const TileKey& key);

    void Insert(const TileKey& key, int x, int y, PixelBuffer&& pixels);

    // Calls fn(const Tile&) for every cached tile of `page` whose scale is
    // not `excludeScale`, or that is stale (used to draw placeholders while
    // zooming or after an edit).
    template <typename Fn>
    void ForEachPlaceholder(int page, int excludeScale, Fn&& fn) const {
        for (const Tile& t : m_lru)
            if (t.key.page == page && (t.key.scaleKey != excludeScale || t.stale)) fn(t);
    }

    // Drops placeholders: tiles of other scales and stale tiles.
    void DropScalesOtherThan(int scaleKey);
    // Keeps every tile as a placeholder but renders it again.
    void MarkAllStale();
    void Clear();

private:
    void Evict();

    std::list<Tile> m_lru;  // front = most recently used
    std::unordered_map<TileKey, std::list<Tile>::iterator, TileKeyHash> m_map;
    size_t m_bytes = 0;
    size_t m_budget = 64u << 20;
    uint64_t m_frame = 0;
};
