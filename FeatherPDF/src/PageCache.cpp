// PageCache.cpp - bounded LRU tile cache (see PageCache.h for the strategy).
#include "PageCache.h"

const Tile* PageCache::Use(const TileKey& key) {
    auto it = m_map.find(key);
    if (it == m_map.end()) return nullptr;
    m_lru.splice(m_lru.begin(), m_lru, it->second);
    it->second->frame = m_frame;
    return &*it->second;
}

bool PageCache::Touch(const TileKey& key) {
    auto it = m_map.find(key);
    if (it == m_map.end()) return false;
    m_lru.splice(m_lru.begin(), m_lru, it->second);
    return true;
}

void PageCache::Insert(const TileKey& key, int x, int y, PixelBuffer&& pixels) {
    auto it = m_map.find(key);
    if (it != m_map.end()) {  // replace (should be rare)
        m_bytes -= it->second->pixels.Bytes();
        it->second->pixels.Free();
        m_lru.erase(it->second);
        m_map.erase(it);
    }
    Tile t;
    t.key = key;
    t.x = x;
    t.y = y;
    t.pixels = pixels;
    t.frame = 0;
    pixels = PixelBuffer{};  // ownership moved into the cache
    m_bytes += t.pixels.Bytes();
    m_lru.push_front(t);
    m_map[key] = m_lru.begin();
    Evict();
}

void PageCache::Evict() {
    // Free least-recently-used tiles until under budget, but never a tile
    // drawn in the current frame (everything in front of a pinned tile was
    // used even more recently, so we can stop there).
    while (m_bytes > m_budget && !m_lru.empty()) {
        Tile& victim = m_lru.back();
        if (victim.frame == m_frame) break;
        m_bytes -= victim.pixels.Bytes();
        victim.pixels.Free();
        m_map.erase(victim.key);
        m_lru.pop_back();
    }
}

void PageCache::DropScalesOtherThan(int scaleKey) {
    for (auto it = m_lru.begin(); it != m_lru.end();) {
        if (it->key.scaleKey != scaleKey) {
            m_bytes -= it->pixels.Bytes();
            it->pixels.Free();
            m_map.erase(it->key);
            it = m_lru.erase(it);
        } else {
            ++it;
        }
    }
}

void PageCache::Clear() {
    for (Tile& t : m_lru) t.pixels.Free();
    m_lru.clear();
    m_map.clear();
    m_bytes = 0;
}
