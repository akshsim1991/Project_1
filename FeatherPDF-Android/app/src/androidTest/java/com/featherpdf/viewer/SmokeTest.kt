package com.featherpdf.viewer

import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.os.StrictMode
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * On-device smoke tests: the PDFium bridge (open, text, search, outline,
 * links, render, passwords) and the reader opening a real document.
 * Screenshots go to the additional test output directory, which the
 * Gradle connected-test task pulls into build/outputs.
 */
@RunWith(AndroidJUnit4::class)
class SmokeTest {
    private val instr = InstrumentationRegistry.getInstrumentation()
    private val ctx = instr.targetContext

    private fun asset(name: String): File {
        val f = File(ctx.cacheDir, name)
        instr.context.assets.open(name).use { input -> f.outputStream().use { input.copyTo(it) } }
        return f
    }

    /** Runs [block] on the PDFium worker thread, as the app does. */
    private fun <T> onWorker(block: () -> T): T {
        val latch = CountDownLatch(1)
        var result: Result<T>? = null
        PdfWorker.post {
            result = runCatching(block)
            latch.countDown()
        }
        assertTrue("worker timed out", latch.await(30, TimeUnit.SECONDS))
        return result!!.getOrThrow()
    }

    private fun open(file: File, password: String?, err: IntArray): Long =
        ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY).use {
            Native.nOpen(it.fd, password, err)
        }

    @Test
    fun engine() {
        val file = asset("sample.pdf")
        onWorker {
            val err = IntArray(1)
            val doc = open(file, null, err)
            assertNotEquals("open failed, error ${err[0]}", 0L, doc)
            try {
                val sizes = Native.nPageSizes(doc)
                assertEquals(6, sizes.size)
                assertTrue("page 3 is landscape", sizes[4] > sizes[5])

                val text = Native.nPageText(doc, 0)
                val s = String(text.codepoints, 0, text.charCount)
                assertTrue(s, s.contains("Chapter 1"))
                assertTrue("page 1 has an internal and a web link", text.linkPages.size >= 2)
                assertTrue(text.linkPages.contains(2))
                assertTrue(text.linkUris.any { it == "https://example.com/" })

                val hits = Native.nSearch(doc, 1, "needle", false)
                assertEquals(5, hits.size)
                assertEquals(0, Native.nSearch(doc, 1, "needle", true).size)
                assertEquals(0, Native.nSearch(doc, 0, "needle", false).size)

                val outline = Native.nOutline(doc)
                assertEquals(3, outline.size)
                assertEquals("Chapter 2", outline.titles[1])
                assertEquals(1, outline.pages[1])

                val meta = Native.nMetadata(doc)
                assertEquals("Feather sample", meta[0])

                val bmp = Bitmap.createBitmap(256, 256, Bitmap.Config.ARGB_8888)
                assertTrue(Native.nRender(doc, 0, bmp, 0, 0, 256, 256, 256, 362, 0, PageColors.NORMAL))
                var dark = 0
                for (y in 0 until 256 step 2) for (x in 0 until 256 step 2) {
                    if (bmp.getPixel(x, y) and 0xFF < 128) dark++
                }
                assertTrue("rendered page has text pixels", dark > 20)
                bmp.recycle()
            } finally {
                Native.nClose(doc)
            }
        }
    }

    @Test
    fun password() {
        val file = asset("locked.pdf")
        onWorker {
            val err = IntArray(1)
            assertEquals(0L, open(file, null, err))
            assertEquals(ReaderActivity.ERR_PASSWORD, err[0])
            assertEquals(0L, open(file, "wrong", err))
            val doc = open(file, "secret", err)
            assertNotEquals(0L, doc)
            Native.nClose(doc)
        }
    }

    @Test
    fun readerOpensDocument() {
        // file:// URIs are fine inside our own process; relax the default VM policy for the test.
        StrictMode.setVmPolicy(StrictMode.VmPolicy.Builder().build())
        val uri = Uri.fromFile(asset("sample.pdf"))
        val intent = Intent(Intent.ACTION_VIEW, uri, ctx, ReaderActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        val activity = instr.startActivitySync(intent) as ReaderActivity
        try {
            waitFor { activity.pageView.hasDocument }
            instr.runOnMainSync { assertEquals(3, activity.pageView.pageCount) }
            SystemClock.sleep(1500)  // let tiles render
            screenshot("01-reader")

            instr.runOnMainSync { activity.pageView.goToPage(2) }
            SystemClock.sleep(1500)
            instr.runOnMainSync { assertEquals(2, activity.pageView.currentPage()) }
            screenshot("02-landscape-page")

            instr.runOnMainSync {
                activity.pageView.setColors(PageColors.DARK)
                activity.pageView.setViewMode(ViewMode.TWO_PAGE)
            }
            SystemClock.sleep(1500)
            screenshot("03-two-page-dark")
        } finally {
            instr.runOnMainSync {
                activity.pageView.setColors(PageColors.NORMAL)
                activity.finish()
            }
            // Drop the recent-list entry the reader created.
            Prefs(ctx).forget(uri.toString())
        }
    }

    private fun waitFor(cond: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + 20_000
        while (SystemClock.uptimeMillis() < end) {
            var ok = false
            instr.runOnMainSync { ok = cond() }
            if (ok) return
            SystemClock.sleep(100)
        }
        throw AssertionError("timed out waiting for the document to open")
    }

    private fun screenshot(name: String) {
        val bmp = instr.uiAutomation.takeScreenshot() ?: return
        val dirArg = InstrumentationRegistry.getArguments().getString("additionalTestOutputDir")
        val dir = if (dirArg != null) File(dirArg) else File(ctx.getExternalFilesDir(null), "screens")
        dir.mkdirs()
        File(dir, "$name.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bmp.recycle()
    }
}
