package com.featherpdf.viewer

import android.graphics.Bitmap

/**
 * JNI entry points into PDFium (app/src/main/cpp/pdf_jni.cpp).
 *
 * PDFium is not thread-safe, so these are called ONLY from the single
 * [PdfWorker] thread, never from the UI thread.
 */
object Native {
    init {
        System.loadLibrary("pdfium")
        System.loadLibrary("featherpdf")
        nInit()
    }

    @JvmStatic private external fun nInit()

    /** Returns a document handle, or 0 with err[0] = PDFium error code. */
    @JvmStatic external fun nOpen(fd: Int, password: String?, err: IntArray): Long
    @JvmStatic external fun nClose(doc: Long)
    @JvmStatic external fun nTrim(doc: Long)

    /** Page sizes in points as (width, height) pairs, already honouring /Rotate. */
    @JvmStatic external fun nPageSizes(doc: Long): FloatArray

    @JvmStatic external fun nRender(
        doc: Long, page: Int, bitmap: Bitmap, x: Int, y: Int, w: Int, h: Int,
        pageW: Int, pageH: Int, rotate: Int, colors: Int,
    ): Boolean

    @JvmStatic external fun nPageText(doc: Long, page: Int): PageText

    /** Search hits on one page: [hitIndex, left, top, right, bottom] per rectangle. */
    @JvmStatic external fun nSearch(doc: Long, page: Int, query: String, matchCase: Boolean): FloatArray

    @JvmStatic external fun nExtractText(doc: Long, p0: Int, i0: Int, p1: Int, i1: Int): String
    @JvmStatic external fun nOutline(doc: Long): OutlineData

    /** title, author, subject, keywords, creator, producer, created, modified, version, encrypted */
    @JvmStatic external fun nMetadata(doc: Long): Array<String>
}

/**
 * Text layer and links of one page. Boxes are in unrotated page points
 * (top-left origin), 4 floats per character (NaN when the character has no
 * box, e.g. line breaks PDFium inserted).
 */
class PageText(
    val codepoints: IntArray,
    val boxes: FloatArray,
    val linkRects: FloatArray,
    val linkPages: IntArray,
    val linkYs: FloatArray,
    val linkUris: Array<String?>,
) {
    val charCount get() = codepoints.size
    fun hasBox(i: Int) = !boxes[i * 4].isNaN()
}

/** Bookmarks flattened depth-first. */
class OutlineData(
    val titles: Array<String>,
    val levels: IntArray,
    val pages: IntArray,
    val ys: FloatArray,
    val uris: Array<String?>,
) {
    val size get() = titles.size
}
