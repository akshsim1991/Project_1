package com.featherpdf.viewer

import android.annotation.SuppressLint
import android.app.ActivityManager
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.view.ActionMode
import android.view.GestureDetector
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.Menu
import android.view.MenuItem
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.widget.OverScroller
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/**
 * The page canvas: layout, scrolling, zoom, navigation, tile requests,
 * painting, text selection, links and search highlights.
 *
 * RENDERING PIPELINE (same as the Windows version)
 *   onDraw finds the visible pages (binary search over row offsets, so it
 *   costs the same for 10 or 10,000 pages), draws cached 512 px tiles (and
 *   stretched tiles of the previous zoom as placeholders during a pinch),
 *   then hands the list of missing visible tiles plus a budgeted prefetch
 *   band to [PdfWorker], replacing its previous list. Finished tiles come
 *   back on the main thread, go into the [TileCache] and invalidate the view.
 */
@SuppressLint("ViewConstructor")
class PageView(context: Context, private var palette: Palette) : View(context) {

    interface Host {
        fun onPageChanged(page: Int, count: Int)
        fun onToggleUi()
        fun onExternalLink(uri: String)
        fun onTextCopied(text: String)
        fun onShareText(text: String)
    }

    var host: Host? = null

    private val density = resources.displayMetrics.density
    private fun dp(v: Float) = v * density

    // --- document --------------------------------------------------------
    private var doc = 0L
    private var token = 0  // changes with every document; stale results are ignored
    private var sizes = FloatArray(0)  // (w, h) in points, unrotated
    val pageCount get() = sizes.size / 2
    val hasDocument get() = doc != 0L
    var message = ""
        set(v) {
            field = v
            invalidate()
        }

    // --- modes -----------------------------------------------------------
    var viewMode = ViewMode.CONTINUOUS
        private set
    var coverPage = true
        private set
    var rotation90 = 0  // extra rotation in quarter turns (clockwise)
        private set
    var pageColors = PageColors.NORMAL
        private set

    /** 0 = custom zoom, 1 = fit width, 2 = fit page. */
    var fitMode = 1
        private set
    private var customZoom = 1f  // multiple of fit-width
    private var scale = 1.0      // pixels per point (quantised)
    private var scaleKey = 1000

    // --- layout ----------------------------------------------------------
    private var pLeft = LongArray(0)
    private var pTop = LongArray(0)
    private var pW = IntArray(0)
    private var pH = IntArray(0)
    private var rowTop = LongArray(0)
    private var rowBottom = LongArray(0)
    private var first = 0
    private var last = -1
    private var singlePage = 0
    private var docW = 0L
    private var docH = 0L
    private var sx = 0L
    private var sy = 0L
    private var forcedPage = -1
    private var lastReportedPage = -1
    private val margin get() = dp(8f).roundToInt()
    var topInset = 0
        private set
    var bottomInset = 0
        private set

    // --- rendering -------------------------------------------------------
    private val cache = TileCache(computeBudget())
    private val failed = HashSet<TileKey>()
    private val tilePaint = Paint()
    private val stretchPaint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val fillPaint = Paint()
    private val highlightPaint = Paint().apply { color = 0x66FFD600 }
    private val currentPaint = Paint().apply { color = 0x88FF8C00.toInt() }
    private val selectionPaint = Paint().apply { color = 0x553D8BFD }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { textSize = dp(15f) }
    private val bubblePaint = Paint(Paint.ANTI_ALIAS_FLAG)
    var search: SearchState? = null

    // --- text layers, selection, links -----------------------------------
    private val textLayers = LinkedHashMap<Int, PageText>(16, 0.75f, true)
    private val textPending = HashSet<Int>()
    private var selStart: TextPos? = null
    private var selEnd: TextPos? = null
    val hasSelection get() = selStart != null && selEnd != null && selStart != selEnd
    private var pendingLongPress: Pair<Float, Float>? = null
    private var actionMode: ActionMode? = null
    private var draggingHandle = -1  // 0 = start, 1 = end

    // --- gestures --------------------------------------------------------
    private val scroller = OverScroller(context)
    private var scaling = false
    private var draggingScroller = false
    private var lastScrollTime = 0L
    private val handler = android.os.Handler(android.os.Looper.getMainLooper())

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        contentDescription = "PDF page"
    }

    private fun computeBudget(): Long {
        // A quarter of this app's memory class, between 24 and 128 MB.
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val mb = (am.memoryClass / 4).coerceIn(24, 128)
        return mb * 1024L * 1024L
    }

    // =====================================================================
    // Document
    // =====================================================================
    fun setDocument(handle: Long, pageSizes: FloatArray, startPage: Int) {
        doc = handle
        token++
        sizes = pageSizes
        cache.clear()
        failed.clear()
        textLayers.clear()
        textPending.clear()
        clearSelection()
        message = ""
        sx = 0
        sy = 0
        singlePage = startPage.coerceIn(0, max(0, pageCount - 1))
        updateScale()
        relayout()
        goToPage(singlePage)
        centerHorizontally(singlePage)
        invalidate()
    }

    fun setPalette(p: Palette) {
        palette = p
        invalidate()
    }

    fun setInsets(top: Int, bottom: Int) {
        if (top == topInset && bottom == bottomInset) return
        val page = currentPage()
        topInset = top
        bottomInset = bottom
        relayout()
        if (hasDocument) goToPage(page)
        invalidate()
    }

    fun trimMemory() {
        cache.clear()
        textLayers.clear()
        BitmapPool.trim()
        if (hasDocument) PdfWorker.setWantedTiles(doc, emptyList()) { _, b -> b?.let { BitmapPool.release(it) } }
        PdfWorker.post { if (doc != 0L) Native.nTrim(doc) }
    }

    // =====================================================================
    // Layout
    // =====================================================================
    private fun dispW(p: Int) = if (rotation90 and 1 == 1) sizes[p * 2 + 1] else sizes[p * 2]
    private fun dispH(p: Int) = if (rotation90 and 1 == 1) sizes[p * 2] else sizes[p * 2 + 1]
    private val paged get() = viewMode == ViewMode.SINGLE

    /** Width/height (points) of the row containing [page], and its gap count. */
    private fun rowExtent(page: Int): Triple<Float, Float, Int> {
        if (viewMode != ViewMode.TWO_PAGE || pageCount < 2) return Triple(dispW(page), dispH(page), 0)
        var firstOfPair = if (coverPage) (if (page == 0) 1 else (page - 1) / 2 * 2 + 1) else page / 2 * 2
        if (firstOfPair + 1 >= pageCount) firstOfPair = pageCount - 2
        return Triple(
            dispW(firstOfPair) + dispW(firstOfPair + 1),
            max(dispH(firstOfPair), dispH(firstOfPair + 1)), 1,
        )
    }

    private fun fitWidthScale(page: Int): Double {
        if (pageCount == 0 || width == 0) return 1.0
        val (w, _, gaps) = rowExtent(page.coerceIn(0, pageCount - 1))
        return max(20.0, (width - (2 + gaps) * margin).toDouble()) / w
    }

    private fun fitPageScale(page: Int): Double {
        if (pageCount == 0 || height == 0) return 1.0
        val (_, h, _) = rowExtent(page.coerceIn(0, pageCount - 1))
        val availH = max(20.0, (height - topInset - bottomInset - 2 * margin).toDouble())
        return min(fitWidthScale(page), availH / h)
    }

    private fun updateScale(refPage: Int = currentPage()) {
        val s = when (fitMode) {
            1 -> fitWidthScale(refPage)
            2 -> fitPageScale(refPage)
            else -> fitWidthScale(refPage) * customZoom
        }
        scaleKey = max(1, (s * 1000).roundToInt())
        scale = scaleKey / 1000.0
    }

    private fun relayout() {
        val n = pageCount
        if (n == 0) {
            first = 0; last = -1; docW = 0; docH = 0
            return
        }
        if (pLeft.size != n) {
            pLeft = LongArray(n); pTop = LongArray(n); pW = IntArray(n); pH = IntArray(n)
            rowTop = LongArray(n); rowBottom = LongArray(n)
        }
        if (paged) {
            singlePage = singlePage.coerceIn(0, n - 1)
            first = singlePage; last = singlePage
        } else {
            first = 0; last = n - 1
        }
        val two = viewMode == ViewMode.TWO_PAGE
        val rowFirst = ArrayList<Int>()
        val rowLast = ArrayList<Int>()
        val rowWidth = ArrayList<Long>()
        var y = (topInset + margin).toLong()
        var maxRowW = 0L
        var i = first
        while (i <= last) {
            var l = i
            if (two && !(coverPage && i == 0) && i + 1 <= last) l = i + 1
            var rw = 0L
            var rh = 0L
            for (k in i..l) {
                pW[k] = max(1L, (dispW(k) * scale).roundToLong()).toInt()
                pH[k] = max(1L, (dispH(k) * scale).roundToLong()).toInt()
                rw += pW[k] + if (k > i) margin else 0
                rh = max(rh, pH[k].toLong())
            }
            for (k in i..l) {
                rowTop[k] = y
                rowBottom[k] = y + rh
                pTop[k] = y + (rh - pH[k]) / 2
            }
            rowFirst.add(i); rowLast.add(l); rowWidth.add(rw)
            maxRowW = max(maxRowW, rw)
            y += rh + margin
            i = l + 1
        }
        docW = maxRowW + 2 * margin
        docH = y + bottomInset
        val area = max(docW, width.toLong())
        for (r in rowFirst.indices) {
            var x = (area - rowWidth[r]) / 2
            if (two && rowFirst[r] == rowLast[r] && !(coverPage && rowFirst[r] == 0)) x = (area - maxRowW) / 2
            for (k in rowFirst[r]..rowLast[r]) {
                pLeft[k] = x
                x += pW[k] + margin
            }
        }
        sx = sx.coerceIn(0, maxX())
        sy = sy.coerceIn(0, maxY())
    }

    private fun maxX() = max(0L, docW - width)
    private fun maxY() = max(0L, docH - height)

    /** Last page whose row starts at or above docY. */
    private fun pageAtY(docY: Long): Int {
        var lo = first
        var hi = last
        while (lo < hi) {
            val mid = lo + (hi - lo + 1) / 2
            if (rowTop[mid] <= docY) lo = mid else hi = mid - 1
        }
        return lo
    }

    private fun rowFirstOf(p: Int): Int {
        var q = p
        while (q > first && rowTop[q - 1] == rowTop[q]) q--
        return q
    }

    private fun pageAt(docX: Long, docY: Long): Int {
        val lastInRow = pageAtY(docY)
        var best = lastInRow
        var bestD = Long.MAX_VALUE
        for (p in rowFirstOf(lastInRow)..lastInRow) {
            val d = when {
                docX < pLeft[p] -> pLeft[p] - docX
                docX > pLeft[p] + pW[p] -> docX - pLeft[p] - pW[p]
                else -> 0L
            }
            if (d < bestD) { bestD = d; best = p }
        }
        return best
    }

    /** First and last page intersecting [y0, y1). */
    private fun visibleRange(y0: Long, y1: Long): IntRange {
        if (last < first) return IntRange.EMPTY
        var lo = first
        var hi = last + 1
        while (lo < hi) {
            val mid = (lo + hi) ushr 1
            if (rowBottom[mid] <= y0) lo = mid + 1 else hi = mid
        }
        var l = lo - 1
        var i = lo
        while (i <= last && rowTop[i] < y1) { l = i; i++ }
        return lo..l
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val page = currentPage()
        updateScale(page)
        relayout()
        if (hasDocument) goToPage(page)
        val screen = w.toLong() * h * 4
        cache.budgetBytes = max(computeBudget(), screen * 4)
    }

    // =====================================================================
    // Scrolling & navigation
    // =====================================================================
    private fun scrollToPos(x: Long, y: Long) {
        val nx = x.coerceIn(0, maxX())
        val ny = y.coerceIn(0, maxY())
        if (nx == sx && ny == sy) return
        forcedPage = -1
        sx = nx
        sy = ny
        lastScrollTime = System.currentTimeMillis()
        invalidate()
        reportPage()
    }

    fun scrollByPx(dx: Float, dy: Float) = scrollToPos(sx + dx.roundToLong(), sy + dy.roundToLong())

    fun currentPage(): Int {
        if (pageCount == 0) return 0
        if (paged) return singlePage
        if (forcedPage in 0 until pageCount) return forcedPage
        if (last < first) return 0
        val y0 = sy + topInset
        val y1 = sy + height - bottomInset
        val range = visibleRange(y0, y1)
        if (range.isEmpty()) return first.coerceIn(0, pageCount - 1)
        var best = range.first
        var bestVis = -1L
        for (p in range) {
            val vis = min(pTop[p] + pH[p], y1) - max(pTop[p], y0)
            if (vis > bestVis) { bestVis = vis; best = p }
        }
        return best
    }

    private fun reportPage() {
        val p = currentPage()
        if (p != lastReportedPage) {
            lastReportedPage = p
            host?.onPageChanged(p, pageCount)
        }
    }

    fun goToPage(page: Int) {
        if (!hasDocument) return
        val p = page.coerceIn(0, pageCount - 1)
        scroller.forceFinished(true)
        if (paged) {
            if (p != singlePage) {
                singlePage = p
                if (fitMode != 0) updateScale(p)
                relayout()
            }
            sy = 0
            sx = sx.coerceIn(0, maxX())
            invalidate()
            reportPage()
            return
        }
        scrollToPos(sx, rowTop[p] - topInset - margin)
        forcedPage = p
        reportPage()
    }

    fun nextPage() {
        if (!hasDocument) return
        val cur = currentPage()
        if (paged) return goToPage(cur + 1)
        var p = cur
        while (p < last && rowTop[p] == rowTop[cur]) p++
        if (rowTop[p] != rowTop[cur]) goToPage(p)
    }

    fun prevPage() {
        if (!hasDocument) return
        val cur = currentPage()
        if (paged) return goToPage(cur - 1)
        val f = rowFirstOf(cur)
        if (f > first) goToPage(rowFirstOf(f - 1))
    }

    fun goToTarget(t: LinkTarget) {
        if (t.page !in 0 until pageCount) return
        goToPage(t.page)
        if (t.destY < 0 || paged || rotation90 != 0) return
        val fromTop = (sizes[t.page * 2 + 1] - t.destY).coerceIn(0f, sizes[t.page * 2 + 1])
        scrollToPos(sx, pTop[t.page] + (fromTop * scale).toLong() - topInset - margin)
        forcedPage = t.page
    }

    private fun centerHorizontally(page: Int) {
        if (page !in first..last) return
        sx = (pLeft[page] + pW[page] / 2 - width / 2).coerceIn(0, maxX())
    }

    fun scrollToHit(hit: SearchHit) {
        if (hit.page !in 0 until pageCount) return
        if (paged && hit.page != singlePage) goToPage(hit.page)
        if (hit.page !in first..last || hit.rects.isEmpty()) return
        val u = toView(hit.rects, 0, hit.page)
        for (k in 4 until hit.rects.size step 4) {
            val r = toView(hit.rects, k, hit.page)
            u.union(r)
        }
        val x0 = pLeft[hit.page] + (u.left * scale).toLong()
        val y0 = pTop[hit.page] + (u.top * scale).toLong()
        val y1 = pTop[hit.page] + (u.bottom * scale).toLong()
        var nx = sx
        var ny = sy
        if (y0 < sy + topInset || y1 > sy + height - bottomInset) ny = y0 - topInset - (height - topInset) / 3
        if (x0 < sx || x0 > sx + width - dp(40f)) nx = x0 - width / 4
        scrollToPos(nx, ny)
        invalidate()
    }

    // =====================================================================
    // Modes & zoom
    // =====================================================================
    fun setViewMode(mode: Int) {
        if (mode == viewMode) return
        val page = currentPage()
        viewMode = mode
        singlePage = page
        cache.clear()
        updateScale(page)
        relayout()
        goToPage(page)
        centerHorizontally(page)
        invalidate()
    }

    fun setCover(cover: Boolean) {
        if (cover == coverPage) return
        coverPage = cover
        if (viewMode != ViewMode.TWO_PAGE) return
        val page = currentPage()
        updateScale(page)
        relayout()
        goToPage(page)
        invalidate()
    }

    fun rotate(quarterTurns: Int) {
        val page = currentPage()
        rotation90 = (rotation90 + quarterTurns).mod(4)
        cache.clear()
        failed.clear()
        updateScale(page)
        relayout()
        if (hasDocument) {
            goToPage(page)
            centerHorizontally(page)
        }
        invalidate()
    }

    fun setColors(mode: Int) {
        if (mode == pageColors) return
        pageColors = mode
        cache.clear()
        failed.clear()
        invalidate()
    }

    fun setFit(mode: Int) {  // 1 = width, 2 = page
        val page = currentPage()
        fitMode = mode
        applyScale(if (mode == 2) fitPageScale(page) else fitWidthScale(page), width / 2f, topInset.toFloat())
        if (mode == 2) goToPage(page)
        centerHorizontally(page)
        invalidate()
    }

    /** Changes the scale keeping the document point under (fx, fy) in place. */
    private fun applyScale(newScale: Double, fx: Float, fy: Float) {
        if (!hasDocument) return
        val docX = sx + fx.toLong()
        val docY = sy + fy.toLong()
        val page = pageAt(docX, docY)
        val ptX = (docX - pLeft[page]) / scale
        val ptY = (docY - pTop[page]) / scale
        val fw = fitWidthScale(page)
        val clamped = newScale.coerceIn(fw * 0.25, fw * 12.0)
        if (fitMode == 0) customZoom = (clamped / fw).toFloat()
        scaleKey = max(1, (clamped * 1000).roundToInt())
        scale = scaleKey / 1000.0
        relayout()
        sx = (pLeft[page] + (ptX * scale).roundToLong() - fx.toLong()).coerceIn(0, maxX())
        sy = (pTop[page] + (ptY * scale).roundToLong() - fy.toLong()).coerceIn(0, maxY())
        failed.clear()
        invalidate()
        reportPage()
    }

    // =====================================================================
    // Coordinates (rotation)
    // =====================================================================
    /** Rectangle k (l, t, r, b at rects[k..k+3], unrotated points) in rotated view points. */
    private fun toView(rects: FloatArray, k: Int, page: Int): RectF {
        val w = sizes[page * 2]
        val h = sizes[page * 2 + 1]
        val l = rects[k]; val t = rects[k + 1]; val r = rects[k + 2]; val b = rects[k + 3]
        return when (rotation90) {
            1 -> RectF(h - b, l, h - t, r)
            2 -> RectF(w - r, h - b, w - l, h - t)
            3 -> RectF(t, w - r, b, w - l)
            else -> RectF(l, t, r, b)
        }
    }

    /** View point (rotated page points) back to unrotated page points. */
    private fun fromView(vx: Float, vy: Float, page: Int): Pair<Float, Float> {
        val w = sizes[page * 2]
        val h = sizes[page * 2 + 1]
        return when (rotation90) {
            1 -> Pair(vy, h - vx)
            2 -> Pair(w - vx, h - vy)
            3 -> Pair(w - vy, vx)
            else -> Pair(vx, vy)
        }
    }

    private fun screenRect(page: Int, r: RectF): RectF {
        val left = (pLeft[page] - sx).toFloat()
        val top = (pTop[page] - sy).toFloat()
        return RectF(left + r.left * scale.toFloat(), top + r.top * scale.toFloat(),
            left + r.right * scale.toFloat(), top + r.bottom * scale.toFloat())
    }

    // =====================================================================
    // Drawing
    // =====================================================================
    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(palette.canvas)
        if (!hasDocument) {
            val text = message.ifEmpty { "" }
            textPaint.color = palette.barTextDim
            textPaint.textAlign = Paint.Align.CENTER
            canvas.drawText(text, width / 2f, height / 2f, textPaint)
            return
        }
        cache.beginFrame()
        val missing = ArrayList<Pair<Long, TileRequest>>()
        var visibleBytes = 0L
        for (p in visibleRange(sy, sy + height)) visibleBytes += drawPage(canvas, p, missing)
        requestTiles(missing, visibleBytes)
        if (missing.isEmpty() && !scaling) cache.dropScalesOtherThan(scaleKey)
        drawSelectionHandles(canvas)
        drawFastScroller(canvas)
        if (!scaling && scroller.isFinished && !draggingScroller) prefetchTextLayers()
    }

    /** Draws one page; returns the bytes of its visible tiles. */
    private fun drawPage(canvas: Canvas, page: Int, missing: MutableList<Pair<Long, TileRequest>>): Long {
        val left = pLeft[page] - sx
        val top = pTop[page] - sy
        val vx0 = max(0L, -left); val vx1 = min(pW[page].toLong(), width - left)
        val vy0 = max(0L, -top); val vy1 = min(pH[page].toLong(), height - top)
        if (vx0 >= vx1 || vy0 >= vy1) return 0
        // Paper and a thin border.
        fillPaint.color = palette.pageBorder
        canvas.drawRect((left - 1).toFloat(), (top - 1).toFloat(), (left + pW[page] + 1).toFloat(),
            (top + pH[page] + 1).toFloat(), fillPaint)
        fillPaint.color = PageColors.paper(pageColors)
        canvas.drawRect(left.toFloat(), top.toFloat(), (left + pW[page]).toFloat(), (top + pH[page]).toFloat(), fillPaint)

        canvas.save()
        canvas.clipRect(left.toFloat(), top.toFloat(), (left + pW[page]).toFloat(), (top + pH[page]).toFloat())
        // 1. Placeholders from other zoom levels, stretched.
        cache.forEachOtherScale(page, scaleKey) { t ->
            val f = (scale / (t.key.scaleKey / 1000.0)).toFloat()
            val dst = RectF(left + t.x * f, top + t.y * f, left + (t.x + t.w) * f, top + (t.y + t.h) * f)
            if (dst.right > 0 && dst.left < width && dst.bottom > 0 && dst.top < height)
                canvas.drawBitmap(t.bitmap, Rect(0, 0, t.w, t.h), dst, stretchPaint)
        }
        // 2. Sharp tiles; collect the missing ones.
        val tile = BitmapPool.TILE
        var bytes = 0L
        val cx = width / 2
        val cy = height / 2
        for (ty in (vy0 / tile).toInt()..((vy1 - 1) / tile).toInt()) {
            for (tx in (vx0 / tile).toInt()..((vx1 - 1) / tile).toInt()) {
                val x = tx * tile
                val y = ty * tile
                val w = min(tile, pW[page] - x)
                val h = min(tile, pH[page] - y)
                bytes += w.toLong() * h * 4
                val key = TileKey(page, scaleKey, tx, ty)
                val t = cache.use(key)
                if (t != null) {
                    val dl = (left + x).toFloat()
                    val dt = (top + y).toFloat()
                    canvas.drawBitmap(t.bitmap, Rect(0, 0, w, h), RectF(dl, dt, dl + w, dt + h), tilePaint)
                } else if (key !in failed) {
                    val dist = abs(left + x + w / 2 - cx) + abs(top + y + h / 2 - cy)
                    missing.add(dist to TileRequest(page, scaleKey, tx, ty, x, y, w, h, pW[page], pH[page], rotation90, pageColors))
                }
            }
        }
        // 3. Search highlights.
        search?.let { s ->
            for (i in s.rangeFor(page)) {
                val hit = s.matches[i]
                val paint = if (i == s.current) currentPaint else highlightPaint
                for (k in hit.rects.indices step 4) canvas.drawRect(screenRect(page, toView(hit.rects, k, page)), paint)
            }
        }
        // 4. Text selection.
        drawSelection(canvas, page)
        canvas.restore()
        return bytes
    }

    private fun requestTiles(missing: MutableList<Pair<Long, TileRequest>>, visibleBytes: Long) {
        val tok = token
        val sink: (TileRequest, Bitmap?) -> Unit = { req, bmp -> onTile(tok, req, bmp) }
        if (scaling) {  // wait until the pinch ends
            PdfWorker.setWantedTiles(doc, emptyList(), sink)
            return
        }
        missing.sortBy { it.first }
        val list = ArrayList<TileRequest>(missing.size + 8)
        missing.forEach { list.add(it.second) }
        // Prefetch the next screen (reading direction) and half a screen above,
        // but only what fits in the cache budget.
        var left = cache.budgetBytes * 9 / 10 - visibleBytes
        left = addPrefetch(sy + height, sy + 2L * height, left, list)
        addPrefetch(sy - height / 2, sy, left, list)
        PdfWorker.setWantedTiles(doc, list, sink)
    }

    private fun addPrefetch(y0: Long, y1: Long, budget: Long, out: MutableList<TileRequest>): Long {
        var left = budget
        val tile = BitmapPool.TILE
        for (p in visibleRange(y0, y1)) {
            val pl = pLeft[p] - sx
            val vx0 = max(0L, -pl); val vx1 = min(pW[p].toLong(), width - pl)
            val vy0 = max(0L, y0 - pTop[p]); val vy1 = min(pH[p].toLong(), y1 - pTop[p])
            if (vx0 >= vx1 || vy0 >= vy1) continue
            for (ty in (vy0 / tile).toInt()..((vy1 - 1) / tile).toInt()) {
                for (tx in (vx0 / tile).toInt()..((vx1 - 1) / tile).toInt()) {
                    val x = tx * tile
                    val y = ty * tile
                    val w = min(tile, pW[p] - x)
                    val h = min(tile, pH[p] - y)
                    left -= tile.toLong() * tile * 4
                    if (left < 0) return left
                    val key = TileKey(p, scaleKey, tx, ty)
                    if (cache.touch(key) || key in failed) continue
                    out.add(TileRequest(p, scaleKey, tx, ty, x, y, w, h, pW[p], pH[p], rotation90, pageColors))
                }
            }
        }
        return left
    }

    private fun onTile(tok: Int, r: TileRequest, bmp: Bitmap?) {
        if (tok != token || r.page !in first..last) {
            bmp?.let { BitmapPool.release(it) }
            return
        }
        if (bmp == null) {
            failed.add(r.key)
            return
        }
        // Drop results for a zoom, rotation or colour mode already left.
        if (r.scaleKey != scaleKey || r.pageW != pW[r.page] || r.rotate != rotation90 || r.colors != pageColors) {
            BitmapPool.release(bmp)
            return
        }
        cache.insert(TileCache.Tile(r.key, r.x, r.y, r.w, r.h, bmp))
        val left = pLeft[r.page] - sx + r.x
        val top = pTop[r.page] - sy + r.y
        if (left < width && top < height && left + r.w > 0 && top + r.h > 0) invalidate()
    }

    // =====================================================================
    // Text layers, selection and links
    // =====================================================================
    private fun requestText(page: Int) {
        if (page in textPending || textLayers.containsKey(page) || !hasDocument) return
        textPending.add(page)
        val tok = token
        val handle = doc
        PdfWorker.post {
            val text = Native.nPageText(handle, page)
            PdfWorker.toMain { onText(tok, page, text) }
        }
    }

    private fun onText(tok: Int, page: Int, text: PageText) {
        if (tok != token) return
        textPending.remove(page)
        textLayers[page] = text
        while (textLayers.size > 12) textLayers.remove(textLayers.keys.first())
        pendingLongPress?.let { (x, y) ->
            if (pageAt(sx + x.toLong(), sy + y.toLong()) == page) {
                pendingLongPress = null
                selectWordAt(x, y)
            }
        }
        if (hasSelection) invalidate()
    }

    /** Loads text/links for the visible pages once the view is at rest. */
    private fun prefetchTextLayers() {
        var budget = 2
        for (p in visibleRange(sy, sy + height)) {
            if (budget == 0) return
            if (!textLayers.containsKey(p) && p !in textPending) {
                requestText(p)
                budget--
            }
        }
    }

    /** Nearest character to a view point: (caret, char index) or null. */
    private fun hitText(x: Float, y: Float, strict: Boolean): Pair<TextPos, Int>? {
        if (!hasDocument || last < first) return null
        val docX = sx + x.toLong()
        val docY = sy + y.toLong()
        val page = pageAt(docX, docY)
        val layer = textLayers[page] ?: run { requestText(page); return null }
        val (px, py) = fromView(((docX - pLeft[page]) / scale).toFloat(), ((docY - pTop[page]) / scale).toFloat(), page)
        var best = -1
        var bestScore = 0f
        var bestDx = 0f
        var bestDy = 0f
        val b = layer.boxes
        for (i in 0 until layer.charCount) {
            if (!layer.hasBox(i)) continue
            val dx = max(0f, max(b[i * 4] - px, px - b[i * 4 + 2]))
            val dy = max(0f, max(b[i * 4 + 1] - py, py - b[i * 4 + 3]))
            val score = dy * 1000f + dx
            if (best < 0 || score < bestScore) {
                best = i; bestScore = score; bestDx = dx; bestDy = dy
            }
        }
        if (best < 0) return null
        if (strict && (bestDy > 2f || bestDx > 16f)) return null
        val mid = (b[best * 4] + b[best * 4 + 2]) / 2
        return TextPos(page, if (px < mid) best else best + 1) to best
    }

    private fun isWordChar(cp: Int) = cp == '_'.code || Character.isLetterOrDigit(cp)

    private fun selectWordAt(x: Float, y: Float) {
        val docX = sx + x.toLong()
        val docY = sy + y.toLong()
        val page = pageAt(docX, docY)
        if (!textLayers.containsKey(page)) {
            pendingLongPress = x to y
            requestText(page)
            return
        }
        val (_, ci) = hitText(x, y, true) ?: return
        val layer = textLayers[page] ?: return
        var l = ci
        var r = ci
        if (isWordChar(layer.codepoints[ci])) {
            while (l > 0 && layer.hasBox(l - 1) && isWordChar(layer.codepoints[l - 1])) l--
            while (r + 1 < layer.charCount && layer.hasBox(r + 1) && isWordChar(layer.codepoints[r + 1])) r++
        }
        selStart = TextPos(page, l)
        selEnd = TextPos(page, r + 1)
        performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        startSelectionMode()
        invalidate()
    }

    fun selectAll() {
        if (!hasDocument) return
        selStart = TextPos(0, 0)
        selEnd = TextPos(pageCount - 1, Int.MAX_VALUE / 2)
        startSelectionMode()
        invalidate()
    }

    fun clearSelection() {
        if (selStart == null && selEnd == null) return
        selStart = null
        selEnd = null
        actionMode?.finish()
        actionMode = null
        invalidate()
    }

    private fun withSelectedText(action: (String) -> Unit) {
        val s = selStart ?: return
        val e = selEnd ?: return
        val a = minOf(s, e)
        val b = maxOf(s, e)
        val handle = doc
        PdfWorker.post {
            val text = Native.nExtractText(handle, a.page, a.index, b.page, b.index)
            PdfWorker.toMain { action(text) }
        }
    }

    private fun startSelectionMode() {
        val mode = actionMode
        if (mode != null) {
            mode.invalidateContentRect()
            return
        }
        actionMode = startActionMode(object : ActionMode.Callback2() {
            override fun onCreateActionMode(mode: ActionMode, menu: Menu): Boolean {
                menu.add(0, 1, 0, "Copy")
                menu.add(0, 2, 1, "Select all")
                menu.add(0, 3, 2, "Share")
                return true
            }

            override fun onPrepareActionMode(mode: ActionMode, menu: Menu) = false

            override fun onActionItemClicked(mode: ActionMode, item: MenuItem): Boolean {
                when (item.itemId) {
                    1 -> { withSelectedText { host?.onTextCopied(it) }; clearSelection() }
                    2 -> selectAll()
                    3 -> { withSelectedText { host?.onShareText(it) }; clearSelection() }
                }
                return true
            }

            override fun onDestroyActionMode(mode: ActionMode) {
                actionMode = null
            }

            override fun onGetContentRect(mode: ActionMode, view: View, outRect: Rect) {
                val r = selectionBounds()
                if (r == null) outRect.set(0, 0, width, height / 3)
                else outRect.set(r.left.toInt(), r.top.toInt(), r.right.toInt(), r.bottom.toInt())
            }
        }, ActionMode.TYPE_FLOATING)
    }

    /** Visible bounds of the selection in view coordinates. */
    private fun selectionBounds(): RectF? {
        val (a, b) = orderedSelection() ?: return null
        var out: RectF? = null
        for (p in visibleRange(sy, sy + height)) {
            if (p < a.page || p > b.page) continue
            forEachSelectedBand(p, a, b) { r -> if (out == null) out = RectF(r) else out!!.union(r) }
        }
        return out
    }

    private fun orderedSelection(): Pair<TextPos, TextPos>? {
        val s = selStart ?: return null
        val e = selEnd ?: return null
        if (s == e) return null
        return minOf(s, e) to maxOf(s, e)
    }

    /** Calls fn with each merged line band (view coordinates) of the selection on a page. */
    private inline fun forEachSelectedBand(page: Int, a: TextPos, b: TextPos, fn: (RectF) -> Unit) {
        val layer = textLayers[page] ?: return
        val n = layer.charCount
        val s = if (page == a.page) min(a.index, n) else 0
        val e = if (page == b.page) min(b.index, n) else n
        if (e <= s) return
        val bx = layer.boxes
        var band: RectF? = null
        for (i in s until e) {
            if (!layer.hasBox(i)) continue
            val l = bx[i * 4]; val t = bx[i * 4 + 1]; val r = bx[i * 4 + 2]; val bt = bx[i * 4 + 3]
            val cur = band
            if (cur != null) {
                val overlap = min(cur.bottom, bt) - max(cur.top, t)
                val h = min(cur.bottom - cur.top, bt - t)
                if (overlap > h * 0.5f && l >= cur.left - 1f) {
                    cur.union(l, t, r, bt)
                    continue
                }
                fn(screenRect(page, toView(floatArrayOf(cur.left, cur.top, cur.right, cur.bottom), 0, page)))
            }
            band = RectF(l, t, r, bt)
        }
        band?.let { fn(screenRect(page, toView(floatArrayOf(it.left, it.top, it.right, it.bottom), 0, page))) }
    }

    private fun drawSelection(canvas: Canvas, page: Int) {
        val (a, b) = orderedSelection() ?: return
        if (page < a.page || page > b.page) return
        if (!textLayers.containsKey(page)) {
            requestText(page)
            return
        }
        forEachSelectedBand(page, a, b) { canvas.drawRect(it, selectionPaint) }
    }

    /** Screen position of a caret: bottom of the character before/after it. */
    private fun caretPoint(pos: TextPos, atEnd: Boolean): Pair<Float, Float>? {
        if (pos.page !in first..last) return null
        val layer = textLayers[pos.page] ?: return null
        var i = if (atEnd) min(pos.index, layer.charCount) - 1 else pos.index
        if (atEnd) { while (i >= 0 && !layer.hasBox(i)) i-- } else { while (i < layer.charCount && !layer.hasBox(i)) i++ }
        if (i < 0 || i >= layer.charCount) return null
        val r = screenRect(pos.page, toView(layer.boxes, i * 4, pos.page))
        return (if (atEnd) r.right else r.left) to r.bottom
    }

    private fun drawSelectionHandles(canvas: Canvas) {
        val (a, b) = orderedSelection() ?: return
        bubblePaint.color = palette.accent
        val radius = dp(9f)
        caretPoint(a, false)?.let { (x, y) ->
            canvas.drawRect(x - dp(1f), y - dp(14f), x + dp(1f), y, bubblePaint)
            canvas.drawCircle(x, y + radius, radius, bubblePaint)
        }
        caretPoint(b, true)?.let { (x, y) ->
            canvas.drawRect(x - dp(1f), y - dp(14f), x + dp(1f), y, bubblePaint)
            canvas.drawCircle(x, y + radius, radius, bubblePaint)
        }
    }

    private fun hitLink(x: Float, y: Float): LinkTarget? {
        if (!hasDocument || last < first) return null
        val docX = sx + x.toLong()
        val docY = sy + y.toLong()
        val page = pageAt(docX, docY)
        if (docX < pLeft[page] || docX > pLeft[page] + pW[page] || docY < pTop[page] || docY > pTop[page] + pH[page]) return null
        val layer = textLayers[page] ?: return null
        val (px, py) = fromView(((docX - pLeft[page]) / scale).toFloat(), ((docY - pTop[page]) / scale).toFloat(), page)
        val slop = dp(6f) / scale.toFloat()  // fingers are wider than mouse pointers
        val r = layer.linkRects
        for (i in layer.linkPages.indices) {
            if (px >= r[i * 4] - slop && px <= r[i * 4 + 2] + slop && py >= r[i * 4 + 1] - slop && py <= r[i * 4 + 3] + slop)
                return LinkTarget(layer.linkPages[i], layer.linkYs[i], layer.linkUris[i])
        }
        return null
    }

    // =====================================================================
    // Fast scroller (drag the thumb on the right edge to move through long documents)
    // =====================================================================
    private fun scrollerVisible() = hasDocument && !paged && docH > height * 3L &&
        (draggingScroller || System.currentTimeMillis() - lastScrollTime < 1500)

    private fun thumbRect(): RectF {
        val trackTop = topInset + dp(8f)
        val trackBottom = height - bottomInset - dp(8f)
        val th = dp(44f)
        val frac = if (maxY() > 0) sy.toFloat() / maxY() else 0f
        val top = trackTop + (trackBottom - trackTop - th) * frac
        return RectF(width - dp(10f), top, width - dp(4f), top + th)
    }

    private fun drawFastScroller(canvas: Canvas) {
        if (!scrollerVisible()) return
        val r = thumbRect()
        bubblePaint.color = palette.accent
        canvas.drawRoundRect(r, dp(3f), dp(3f), bubblePaint)
        if (draggingScroller) {
            val label = "${currentPage() + 1} / $pageCount"
            textPaint.textAlign = Paint.Align.RIGHT
            textPaint.color = 0xFFFFFFFF.toInt()
            val tw = textPaint.measureText(label)
            val bubble = RectF(r.left - tw - dp(36f), r.centerY() - dp(18f), r.left - dp(12f), r.centerY() + dp(18f))
            canvas.drawRoundRect(bubble, dp(18f), dp(18f), bubblePaint)
            canvas.drawText(label, bubble.right - dp(12f), r.centerY() + dp(5f), textPaint)
        }
        // Hide again after a moment without scrolling.
        handler.removeCallbacks(hideScroller)
        handler.postDelayed(hideScroller, 1600)
    }

    private val hideScroller = Runnable { invalidate() }

    // =====================================================================
    // Touch
    // =====================================================================
    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean {
            scroller.forceFinished(true)
            return true
        }

        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            scrollByPx(dx, dy)
            return true
        }

        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            if (paged && abs(vx) > abs(vy) * 1.5f && (maxX() == 0L || (vx < 0 && sx >= maxX()) || (vx > 0 && sx <= 0))) {
                if (vx < 0) nextPage() else prevPage()  // swipe to turn the page
                return true
            }
            scroller.fling(sx.toInt(), sy.toInt(), (-vx).toInt(), (-vy).toInt(), 0, maxX().toInt(), 0,
                maxY().coerceAtMost(Int.MAX_VALUE.toLong()).toInt())
            postInvalidateOnAnimation()
            return true
        }

        override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
            if (hasSelection) {
                clearSelection()
                return true
            }
            hitLink(e.x, e.y)?.let { t ->
                if (t.page >= 0) goToTarget(t) else t.uri?.let { host?.onExternalLink(it) }
                return true
            }
            if (paged && hasDocument) {  // tap the side of the page to turn it
                if (e.x < width * 0.25f) return true.also { prevPage() }
                if (e.x > width * 0.75f) return true.also { nextPage() }
            }
            host?.onToggleUi()
            return true
        }

        override fun onDoubleTap(e: MotionEvent): Boolean {
            if (!hasDocument) return false
            val page = currentPage()
            val fw = fitWidthScale(page)
            if (fitMode != 0 || scale < fw * 1.5) {
                fitMode = 0
                customZoom = 2.5f
                applyScale(fw * 2.5, e.x, e.y)
            } else {
                setFit(1)
            }
            return true
        }

        override fun onLongPress(e: MotionEvent) {
            if (hasDocument) selectWordAt(e.x, e.y)
        }
    })

    private val scaleDetector = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
            if (!hasDocument) return false
            scaling = true
            fitMode = 0
            scroller.forceFinished(true)
            return true
        }

        override fun onScale(detector: ScaleGestureDetector): Boolean {
            applyScale(scale * detector.scaleFactor, detector.focusX, detector.focusY)
            return true
        }

        override fun onScaleEnd(detector: ScaleGestureDetector) {
            scaling = false
            invalidate()  // now render the new zoom level sharply
        }
    }).apply { isQuickScaleEnabled = false }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (handleSpecialTouch(e)) return true
        scaleDetector.onTouchEvent(e)
        if (!scaleDetector.isInProgress) gestures.onTouchEvent(e)
        return true
    }

    /** Selection handles and the fast scroller take precedence over gestures. */
    private fun handleSpecialTouch(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if (scrollerVisible()) {
                    val r = thumbRect()
                    if (e.x > width - dp(36f) && e.y > r.top - dp(24f) && e.y < r.bottom + dp(24f)) {
                        draggingScroller = true
                        scroller.forceFinished(true)
                        invalidate()
                        return true
                    }
                }
                val (a, b) = orderedSelection() ?: return false
                val touch = dp(30f)
                caretPoint(a, false)?.let { (x, y) ->
                    if (abs(e.x - x) < touch && abs(e.y - (y + dp(9f))) < touch) { draggingHandle = 0; return true }
                }
                caretPoint(b, true)?.let { (x, y) ->
                    if (abs(e.x - x) < touch && abs(e.y - (y + dp(9f))) < touch) { draggingHandle = 1; return true }
                }
                return false
            }
            MotionEvent.ACTION_MOVE -> {
                if (draggingScroller) {
                    val trackTop = topInset + dp(8f)
                    val trackBottom = height - bottomInset - dp(8f)
                    val frac = ((e.y - trackTop) / max(1f, trackBottom - trackTop)).coerceIn(0f, 1f)
                    scrollToPos(sx, (maxY() * frac).toLong())
                    return true
                }
                if (draggingHandle >= 0) {
                    // Aim a little above the finger so the text stays visible.
                    val (pos, _) = hitText(e.x, e.y - dp(24f), false) ?: return true
                    val (a, b) = orderedSelection() ?: return true
                    if (draggingHandle == 0) {
                        selStart = pos; selEnd = b
                        if (pos > b) { draggingHandle = 1 }
                    } else {
                        selStart = a; selEnd = pos
                        if (pos < a) { draggingHandle = 0 }
                    }
                    if (e.y < topInset + dp(48f)) scrollByPx(0f, -dp(12f))
                    if (e.y > height - bottomInset - dp(48f)) scrollByPx(0f, dp(12f))
                    invalidate()
                    return true
                }
                return false
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (draggingScroller) {
                    draggingScroller = false
                    lastScrollTime = System.currentTimeMillis()
                    invalidate()
                    return true
                }
                if (draggingHandle >= 0) {
                    draggingHandle = -1
                    actionMode?.invalidateContentRect()
                    return true
                }
                return false
            }
        }
        return false
    }

    override fun computeScroll() {
        if (scroller.computeScrollOffset()) {
            scrollToPos(scroller.currX.toLong(), scroller.currY.toLong())
            postInvalidateOnAnimation()
        }
    }

    // Keyboards (Chromebooks, tablets with keyboards).
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val line = dp(40f)
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_DOWN -> scrollByPx(0f, line)
            KeyEvent.KEYCODE_DPAD_UP -> scrollByPx(0f, -line)
            KeyEvent.KEYCODE_PAGE_DOWN, KeyEvent.KEYCODE_DPAD_RIGHT -> nextPage()
            KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_DPAD_LEFT -> prevPage()
            KeyEvent.KEYCODE_SPACE -> scrollByPx(0f, (if (event.isShiftPressed) -1 else 1) * (height - topInset - line))
            KeyEvent.KEYCODE_MOVE_HOME -> goToPage(0)
            KeyEvent.KEYCODE_MOVE_END -> goToPage(pageCount - 1)
            else -> return super.onKeyDown(keyCode, event)
        }
        return true
    }

    // =====================================================================
    // State
    // =====================================================================
    fun zoomState(): FloatArray = floatArrayOf(fitMode.toFloat(), customZoom)

    fun restoreZoom(state: FloatArray?) {
        if (state == null || state.size < 2) return
        fitMode = state[0].toInt()
        customZoom = state[1]
    }

    /** For the page-image share: a size capped at about [maxPixels]. */
    fun imageSize(page: Int, dpi: Float, maxPixels: Double): Pair<Int, Int> {
        val w = dispW(page)
        val h = dispH(page)
        var k = dpi / 72.0
        if (w * h * k * k > maxPixels) k = kotlin.math.sqrt(maxPixels / (w * h))
        return ceil(w * k).toInt() to ceil(h * k).toInt()
    }
}
