package com.featherpdf.viewer

import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.os.Process

/**
 * The one background thread that owns PDFium, shared by every open document
 * (PDFium is not thread-safe, not even across documents).
 *
 * Priorities, as in the Windows version: commands (open, close, text, copy)
 * > visible/prefetch tiles > thumbnails > search (one page per step). The
 * tile and thumbnail lists are REPLACED on every repaint, so scrolling fast
 * through a long document never builds a backlog.
 *
 * All callbacks are delivered on the main thread.
 */
object PdfWorker {
    private val main = Handler(Looper.getMainLooper())
    private val lock = Object()
    private val commands = ArrayDeque<() -> Unit>()

    private var tiles: List<TileRequest> = emptyList()
    private var tilePos = 0
    private var tileDoc = 0L
    private var tileSink: ((TileRequest, Bitmap?) -> Unit)? = null
    private var inFlight: TileKey? = null

    private var thumbs: List<TileRequest> = emptyList()
    private var thumbPos = 0
    private var thumbDoc = 0L
    private var thumbSink: ((TileRequest, Bitmap?) -> Unit)? = null

    private class SearchJob(
        val doc: Long, val pageCount: Int, val startPage: Int, val query: String,
        val matchCase: Boolean, val sink: (page: Int, hits: List<SearchHit>, done: Int, finished: Boolean) -> Unit,
    ) {
        var done = 0
    }
    private var search: SearchJob? = null

    init {
        Thread({ run() }, "pdf-worker").apply {
            isDaemon = true
            start()
        }
    }

    /** Runs [block] on the worker thread (FIFO, before any tile work). */
    fun post(block: () -> Unit) {
        synchronized(lock) {
            commands.addLast(block)
            lock.notifyAll()
        }
    }

    /** Delivers [block] on the main thread. */
    fun toMain(block: () -> Unit) {
        main.post(block)
    }

    fun setWantedTiles(doc: Long, list: List<TileRequest>, sink: (TileRequest, Bitmap?) -> Unit) {
        synchronized(lock) {
            val busy = inFlight
            tiles = if (busy == null) list else list.filter { it.key != busy }
            tilePos = 0
            tileDoc = doc
            tileSink = sink
            lock.notifyAll()
        }
    }

    fun setWantedThumbs(doc: Long, list: List<TileRequest>, sink: (TileRequest, Bitmap?) -> Unit) {
        synchronized(lock) {
            thumbs = list
            thumbPos = 0
            thumbDoc = doc
            thumbSink = sink
            lock.notifyAll()
        }
    }

    fun startSearch(
        doc: Long, pageCount: Int, startPage: Int, query: String, matchCase: Boolean,
        sink: (page: Int, hits: List<SearchHit>, done: Int, finished: Boolean) -> Unit,
    ) {
        synchronized(lock) {
            search = SearchJob(doc, pageCount, startPage, query, matchCase, sink)
            lock.notifyAll()
        }
    }

    fun cancelSearch() {
        synchronized(lock) { search = null }
    }

    /**
     * Closes a document. Pending work for it is dropped first, so nothing can
     * touch the handle after it is freed.
     */
    fun close(doc: Long) {
        if (doc == 0L) return
        synchronized(lock) {
            if (tileDoc == doc) tiles = emptyList()
            if (thumbDoc == doc) thumbs = emptyList()
            if (search?.doc == doc) search = null
            commands.addLast { Native.nClose(doc) }
            lock.notifyAll()
        }
    }

    private fun run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND + Process.THREAD_PRIORITY_MORE_FAVORABLE)
        while (true) {
            var command: (() -> Unit)? = null
            var tile: TileRequest? = null
            var thumb: TileRequest? = null
            var doc = 0L
            var sink: ((TileRequest, Bitmap?) -> Unit)? = null
            var job: SearchJob? = null
            synchronized(lock) {
                while (commands.isEmpty() && tilePos >= tiles.size && thumbPos >= thumbs.size &&
                    search == null
                ) {
                    lock.wait()
                }
                when {
                    commands.isNotEmpty() -> command = commands.removeFirst()
                    tilePos < tiles.size -> {
                        tile = tiles[tilePos++]
                        doc = tileDoc
                        sink = tileSink
                        inFlight = tile!!.key
                    }
                    thumbPos < thumbs.size -> {
                        thumb = thumbs[thumbPos++]
                        doc = thumbDoc
                        sink = thumbSink
                    }
                    else -> job = search
                }
            }
            try {
                when {
                    command != null -> command!!()
                    tile != null -> {
                        val bmp = BitmapPool.obtainTile()
                        val t = tile!!
                        val ok = Native.nRender(doc, t.page, bmp, t.x, t.y, t.w, t.h, t.pageW, t.pageH, t.rotate, t.colors)
                        if (!ok) BitmapPool.release(bmp)
                        val s = sink
                        main.post { s?.invoke(t, if (ok) bmp else null) }
                        synchronized(lock) { inFlight = null }
                    }
                    thumb != null -> {
                        val t = thumb!!
                        val bmp = Bitmap.createBitmap(t.w, t.h, Bitmap.Config.ARGB_8888)
                        val ok = Native.nRender(doc, t.page, bmp, 0, 0, t.w, t.h, t.pageW, t.pageH, t.rotate, t.colors)
                        val s = sink
                        main.post { s?.invoke(t, if (ok) bmp else null) }
                    }
                    job != null -> searchStep(job!!)
                }
            } catch (e: Throwable) {
                // Never let one bad page stop the worker.
                synchronized(lock) { inFlight = null }
            }
        }
    }

    private fun searchStep(job: SearchJob) {
        val page = (job.startPage + job.done).mod(job.pageCount)
        val raw = Native.nSearch(job.doc, page, job.query, job.matchCase)
        val hits = ArrayList<SearchHit>()
        var i = 0
        while (i < raw.size) {
            val id = raw[i]
            var j = i
            while (j < raw.size && raw[j] == id) j += 5
            val rects = FloatArray((j - i) / 5 * 4)
            var k = 0
            var m = i
            while (m < j) {
                System.arraycopy(raw, m + 1, rects, k, 4)
                k += 4
                m += 5
            }
            hits.add(SearchHit(page, rects))
            i = j
        }
        job.done++
        val finished = job.done >= job.pageCount
        synchronized(lock) { if (finished && search === job) search = null }
        if (hits.isNotEmpty() || finished || job.done % 32 == 0) {
            val done = job.done
            main.post { job.sink(page, hits, done, finished) }
        }
    }
}

/**
 * Reuses tile bitmaps so scrolling does not allocate (and garbage-collect)
 * a megabyte per tile. Every tile is TILE x TILE; edge tiles use part of it.
 */
object BitmapPool {
    const val TILE = 512
    private const val MAX_SPARE = 12
    private val spare = ArrayDeque<Bitmap>()

    fun obtainTile(): Bitmap = synchronized(spare) { spare.removeLastOrNull() }
        ?: Bitmap.createBitmap(TILE, TILE, Bitmap.Config.ARGB_8888)

    fun release(bitmap: Bitmap) {
        if (bitmap.width != TILE || bitmap.height != TILE || bitmap.isRecycled) return
        synchronized(spare) {
            if (spare.size < MAX_SPARE) spare.addLast(bitmap) else bitmap.recycle()
        }
    }

    fun trim() {
        synchronized(spare) {
            spare.forEach { it.recycle() }
            spare.clear()
        }
    }
}
