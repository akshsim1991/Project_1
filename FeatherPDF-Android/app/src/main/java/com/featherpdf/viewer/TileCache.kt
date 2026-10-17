package com.featherpdf.viewer

import android.graphics.Bitmap

/**
 * Bounded LRU cache of rendered tiles (UI thread only).
 *
 * MEMORY STRATEGY (same as the Windows version)
 *  - Pages are cut into 512 x 512 tiles; only tiles on screen plus a small
 *    prefetch band are ever requested.
 *  - The byte budget is derived from the device's per-app memory class;
 *    least-recently-used tiles are evicted (and their bitmaps recycled
 *    through [BitmapPool]) as soon as the budget is exceeded.
 *  - Tiles drawn in the current frame are pinned, so a small budget can not
 *    cause a render/evict loop.
 *  - After a zoom, tiles of the old scale only serve as stretched
 *    placeholders until the new scale is sharp, then they are dropped.
 *  - Everything is released when the app goes to the background.
 */
class TileCache(var budgetBytes: Long) {
    class Tile(val key: TileKey, val x: Int, val y: Int, val w: Int, val h: Int, val bitmap: Bitmap) {
        var frame = 0L
    }

    private val map = LinkedHashMap<TileKey, Tile>(64, 0.75f, true)  // access order = LRU
    var bytes = 0L
        private set
    private var frame = 0L

    fun beginFrame() {
        frame++
    }

    /** Returns the tile (marking it used and pinned for this frame) or null. */
    fun use(key: TileKey): Tile? = map[key]?.also { it.frame = frame }

    /** Marks a prefetched tile as recently used without pinning it. */
    fun touch(key: TileKey): Boolean = map[key] != null

    fun insert(tile: Tile) {
        map.remove(tile.key)?.let { free(it) }
        map[tile.key] = tile
        bytes += tile.bitmap.byteCount
        evict()
    }

    inline fun forEachOtherScale(page: Int, scaleKey: Int, fn: (Tile) -> Unit) {
        for (t in snapshot()) if (t.key.page == page && t.key.scaleKey != scaleKey) fn(t)
    }

    fun snapshot(): List<Tile> = map.values.toList()

    fun dropScalesOtherThan(scaleKey: Int) {
        val it = map.values.iterator()
        while (it.hasNext()) {
            val t = it.next()
            if (t.key.scaleKey != scaleKey) {
                it.remove()
                free(t)
            }
        }
    }

    fun clear() {
        map.values.forEach { free(it) }
        map.clear()
        bytes = 0
    }

    private fun evict() {
        val it = map.values.iterator()
        while (bytes > budgetBytes && it.hasNext()) {
            val t = it.next()  // eldest first
            if (t.frame == frame) break  // everything after it was used even more recently
            it.remove()
            free(t)
        }
    }

    private fun free(t: Tile) {
        bytes -= t.bitmap.byteCount
        BitmapPool.release(t.bitmap)
    }
}
