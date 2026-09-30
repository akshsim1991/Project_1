package com.featherpdf.viewer

/** Identifies a rendered tile: page, scale (px per pt x 1000), column, row. */
data class TileKey(val page: Int, val scaleKey: Int, val tx: Int, val ty: Int)

/**
 * A tile the view would like rendered. All geometry is computed by the view
 * so view and worker always agree on pixel sizes.
 */
class TileRequest(
    val page: Int,
    val scaleKey: Int,
    val tx: Int,
    val ty: Int,
    val x: Int,
    val y: Int,
    val w: Int,
    val h: Int,
    val pageW: Int,
    val pageH: Int,
    val rotate: Int,
    val colors: Int,
) {
    val key get() = TileKey(page, scaleKey, tx, ty)
}

/** Where a link or bookmark goes. destY is PDF user space, or < 0. */
class LinkTarget(val page: Int, val destY: Float, val uri: String?)

/** A caret position: before character [index] of [page]. */
data class TextPos(val page: Int, val index: Int) : Comparable<TextPos> {
    override fun compareTo(other: TextPos) =
        if (page != other.page) page.compareTo(other.page) else index.compareTo(other.index)
}

/** One search match: its rectangles (l, t, r, b quadruples) in page points. */
class SearchHit(val page: Int, val rects: FloatArray)

/** Page colour modes applied to rendered pixels. */
object PageColors {
    const val NORMAL = 0
    const val DARK = 1
    const val DIM = 2

    fun paper(mode: Int) = when (mode) {
        DARK -> 0xFF1E1E1E.toInt()
        DIM -> 0xFFC8C8C8.toInt()
        else -> 0xFFFFFFFF.toInt()
    }
}

object ViewMode {
    const val SINGLE = 0
    const val CONTINUOUS = 1
    const val TWO_PAGE = 2
}
