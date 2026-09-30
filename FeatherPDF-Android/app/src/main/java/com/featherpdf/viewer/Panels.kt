package com.featherpdf.viewer

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.text.TextUtils
import android.util.TypedValue
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.OverScroller
import android.widget.TextView
import kotlin.math.max
import kotlin.math.min

/**
 * Page thumbnails in a grid, rendered lazily on the worker's low-priority
 * thumbnail list: only visible cells (plus one row below) are requested, and
 * a 12 MB LRU keeps recently shown thumbnails. Nothing renders while the
 * panel is closed.
 */
@SuppressLint("ViewConstructor")
class ThumbGridView(context: Context, private var palette: Palette) : View(context) {
    var onPageClick: ((Int) -> Unit)? = null

    private val density = resources.displayMetrics.density
    private fun dp(v: Float) = (v * density).toInt()

    private var doc = 0L
    private var sizes = FloatArray(0)
    private var rotation90 = 0
    private var colors = 0
    private var current = -1
    private var sy = 0
    private val count get() = sizes.size / 2

    private val cache = LinkedHashMap<Int, Bitmap>(32, 0.75f, true)
    private var cacheBytes = 0L
    private val cacheBudget = 12L * 1024 * 1024

    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val fill = Paint()
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textSize = 12f * density
        textAlign = Paint.Align.CENTER
    }
    private val scroller = OverScroller(context)

    // Grid geometry.
    private val pad get() = dp(12f)
    private val cols get() = max(1, width / dp(140f))
    private val cellW get() = (width - pad * (cols + 1)) / cols
    private val boxH get() = (cellW * 1.414f).toInt()
    private val rowH get() = boxH + dp(26f)
    private val totalH get() = pad + ((count + cols - 1) / cols) * rowH

    fun setDocument(handle: Long, pageSizes: FloatArray, rotation: Int, pageColors: Int) {
        if (handle != doc || rotation != rotation90 || pageColors != colors) clearCache()
        if (handle != doc) sy = 0
        doc = handle
        sizes = pageSizes
        rotation90 = rotation
        colors = pageColors
        invalidate()
    }

    fun setPalette(p: Palette) {
        palette = p
        invalidate()
    }

    fun setCurrentPage(page: Int) {
        if (page == current) return
        current = page
        if (width > 0 && page >= 0) {
            val top = pad + (page / cols) * rowH
            if (top < sy || top + rowH > sy + height) sy = clampScroll(top - pad)
        }
        invalidate()
    }

    fun clearCache() {
        cache.values.forEach { it.recycle() }
        cache.clear()
        cacheBytes = 0
    }

    fun stop() {
        PdfWorker.setWantedThumbs(0, emptyList()) { _, b -> b?.recycle() }
        clearCache()
    }

    private fun clampScroll(y: Int) = y.coerceIn(0, max(0, totalH - height))

    /** Size of page p's thumbnail fitted into a cell box. */
    private fun thumbSize(p: Int): Pair<Int, Int> {
        val w = if (rotation90 and 1 == 1) sizes[p * 2 + 1] else sizes[p * 2]
        val h = if (rotation90 and 1 == 1) sizes[p * 2] else sizes[p * 2 + 1]
        val k = min(cellW / w, boxH / h)
        return max(1, (w * k).toInt()) to max(1, (h * k).toInt())
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(palette.panel)
        if (count == 0 || width == 0) return
        val c = cols
        val firstRow = max(0, (sy - pad) / rowH)
        val lastRow = (sy + height) / rowH + 1  // one extra row = prefetch
        val wanted = ArrayList<TileRequest>()
        label.color = palette.barText
        for (row in firstRow..lastRow) {
            for (col in 0 until c) {
                val p = row * c + col
                if (p >= count) break
                val (w, h) = thumbSize(p)
                val cellX = pad + col * (cellW + pad)
                val top = pad + row * rowH - sy
                val x = cellX + (cellW - w) / 2f
                val y = top + (boxH - h).toFloat()
                val onScreen = top < height && top + rowH > 0
                if (onScreen) {
                    val frame = if (p == current) dp(3f) else 1
                    fill.color = if (p == current) palette.accent else palette.pageBorder
                    canvas.drawRect(x - frame, y - frame, x + w + frame, y + h + frame, fill)
                    fill.color = PageColors.paper(colors)
                    canvas.drawRect(x, y, x + w, y + h, fill)
                }
                val bmp = cache[p]
                if (bmp != null && bmp.width == w && bmp.height == h) {
                    if (onScreen) canvas.drawBitmap(bmp, Rect(0, 0, w, h), RectF(x, y, x + w, y + h), paint)
                } else {
                    wanted.add(TileRequest(p, w, 0, 0, 0, 0, w, h, w, h, rotation90, colors))
                }
                if (onScreen) {
                    label.color = if (p == current) palette.accent else palette.barText
                    canvas.drawText("${p + 1}", cellX + cellW / 2f, top + boxH + dp(18f).toFloat(), label)
                }
            }
        }
        val handle = doc
        val rot = rotation90
        val col = colors
        PdfWorker.setWantedThumbs(doc, wanted) { req, bmp ->
            if (bmp == null) return@setWantedThumbs
            if (handle != doc || rot != rotation90 || col != colors) {
                bmp.recycle()
                return@setWantedThumbs
            }
            cache.remove(req.page)?.let { cacheBytes -= it.byteCount; it.recycle() }
            cache[req.page] = bmp
            cacheBytes += bmp.byteCount
            while (cacheBytes > cacheBudget && cache.size > 1) {
                val eldest = cache.keys.first()
                cache.remove(eldest)?.let { cacheBytes -= it.byteCount; it.recycle() }
            }
            invalidate()
        }
    }

    private val gestures = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean {
            scroller.forceFinished(true)
            return true
        }

        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            sy = clampScroll(sy + dy.toInt())
            invalidate()
            return true
        }

        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            scroller.fling(0, sy, 0, (-vy).toInt(), 0, 0, 0, max(0, totalH - height))
            postInvalidateOnAnimation()
            return true
        }

        override fun onSingleTapUp(e: MotionEvent): Boolean {
            val row = ((e.y + sy - pad) / rowH).toInt()
            val col = ((e.x - pad) / (cellW + pad)).toInt()
            val p = row * cols + col
            if (col in 0 until cols && p in 0 until count) onPageClick?.invoke(p)
            return true
        }
    })

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent) = gestures.onTouchEvent(event)

    override fun computeScroll() {
        if (scroller.computeScrollOffset()) {
            sy = clampScroll(scroller.currY)
            postInvalidateOnAnimation()
        }
    }
}

/** The document outline (bookmarks) as an indented list. */
class OutlineAdapter(private val context: Context, private var palette: Palette) : BaseAdapter() {
    var outline: OutlineData? = null
        set(v) {
            field = v
            notifyDataSetChanged()
        }

    private val density = context.resources.displayMetrics.density

    override fun getCount() = outline?.size ?: 0
    override fun getItem(position: Int): Any = position
    override fun getItemId(position: Int) = position.toLong()

    override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
        val tv = (convertView as? TextView) ?: TextView(context).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            maxLines = 2
            ellipsize = TextUtils.TruncateAt.END
            minHeight = (44 * density).toInt()
            gravity = Gravity.CENTER_VERTICAL
        }
        val o = outline ?: return tv
        val level = min(o.levels[position], 6)
        tv.text = o.titles[position].ifEmpty { "(untitled)" }
        tv.setTextColor(palette.barText)
        tv.paint.isFakeBoldText = level == 0
        tv.setPadding(((16 + level * 16) * density).toInt(), 0, (16 * density).toInt(), 0)
        return tv
    }
}

/**
 * The slide-in side panel: "Contents" (bookmarks) and "Pages" (thumbnails)
 * tabs, over a dimmed scrim that closes it when tapped.
 */
@SuppressLint("ViewConstructor")
class SidePanel(context: Context, private var palette: Palette) : FrameLayout(context) {
    private val density = resources.displayMetrics.density
    private fun dp(v: Float) = (v * density).toInt()

    val thumbs = ThumbGridView(context, palette)
    val outlineAdapter = OutlineAdapter(context, palette)
    private val outlineList = ListView(context).apply {
        adapter = outlineAdapter
        divider = null
    }
    private val emptyText = TextView(context).apply {
        text = "This document has no bookmarks"
        gravity = Gravity.CENTER
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
    }
    private val tabContents = tab("Contents")
    private val tabPages = tab("Pages")
    private val content = FrameLayout(context)
    private val sheet = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
    private val scrim = View(context).apply { setBackgroundColor(0x66000000) }

    var showingThumbs = false
        private set
    var onClosed: (() -> Unit)? = null
    var onOutlineClick: ((Int) -> Unit)? = null

    init {
        visibility = GONE
        addView(scrim, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        val tabs = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(tabContents, LinearLayout.LayoutParams(0, dp(48f), 1f))
            addView(tabPages, LinearLayout.LayoutParams(0, dp(48f), 1f))
        }
        sheet.addView(tabs)
        content.addView(outlineList)
        content.addView(emptyText)
        content.addView(thumbs)
        sheet.addView(content, LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f))
        sheet.elevation = dp(8f).toFloat()
        addView(sheet, LayoutParams(dp(340f), LayoutParams.MATCH_PARENT, Gravity.START))
        scrim.setOnClickListener { close() }
        tabContents.setOnClickListener { select(false) }
        tabPages.setOnClickListener { select(true) }
        outlineList.setOnItemClickListener { _, _, pos, _ -> onOutlineClick?.invoke(pos) }
        applyPalette()
    }

    private fun tab(text: String) = TextView(context).apply {
        this.text = text
        gravity = Gravity.CENTER
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
        isAllCaps = false
    }

    fun setPalette(p: Palette) {
        palette = p
        thumbs.setPalette(p)
        applyPalette()
    }

    private fun applyPalette() {
        sheet.setBackgroundColor(palette.panel)
        emptyText.setTextColor(palette.barTextDim)
        tabContents.setTextColor(if (!showingThumbs) palette.accent else palette.barTextDim)
        tabPages.setTextColor(if (showingThumbs) palette.accent else palette.barTextDim)
        tabContents.paint.isUnderlineText = !showingThumbs
        tabPages.paint.isUnderlineText = showingThumbs
        tabContents.invalidate()
        tabPages.invalidate()
        outlineAdapter.notifyDataSetChanged()
    }

    fun setTopInset(top: Int) {
        sheet.setPadding(0, top, 0, 0)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val lp = sheet.layoutParams
        lp.width = min(dp(360f), (w * 0.85f).toInt())
        sheet.layoutParams = lp
    }

    fun select(thumbnails: Boolean) {
        showingThumbs = thumbnails
        val hasOutline = (outlineAdapter.outline?.size ?: 0) > 0
        outlineList.visibility = if (!thumbnails && hasOutline) VISIBLE else GONE
        emptyText.visibility = if (!thumbnails && !hasOutline) VISIBLE else GONE
        thumbs.visibility = if (thumbnails) VISIBLE else GONE
        if (!thumbnails) thumbs.stop()
        applyPalette()
    }

    val isOpen get() = visibility == VISIBLE

    fun open(thumbnails: Boolean) {
        visibility = VISIBLE
        select(thumbnails)
        sheet.translationX = -dp(360f).toFloat()
        sheet.animate().translationX(0f).setDuration(150).start()
    }

    fun close() {
        if (!isOpen) return
        thumbs.stop()
        visibility = GONE
        onClosed?.invoke()
    }
}
