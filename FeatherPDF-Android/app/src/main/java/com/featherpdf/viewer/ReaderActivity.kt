package com.featherpdf.viewer

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.CancellationSignal
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.print.PageRange
import android.print.PrintAttributes
import android.print.PrintDocumentAdapter
import android.print.PrintDocumentInfo
import android.print.PrintManager
import android.provider.OpenableColumns
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import android.widget.Toolbar
import java.io.File
import java.io.FileNotFoundException
import java.io.FileOutputStream

/**
 * One open document. Every document runs in its own task (see
 * documentLaunchMode in the manifest), so several PDFs appear as separate
 * entries in Android's recent apps screen: the phone equivalent of tabs.
 */
class ReaderActivity : Activity(), PageView.Host {
    private lateinit var prefs: Prefs
    private lateinit var palette: Palette
    private lateinit var root: FrameLayout
    internal lateinit var pageView: PageView
        private set
    private lateinit var topBox: LinearLayout
    private lateinit var toolbar: Toolbar
    private lateinit var searchBar: LinearLayout
    private lateinit var searchEdit: EditText
    private lateinit var searchCount: TextView
    private lateinit var matchCaseButton: TextView
    private lateinit var panel: SidePanel

    private var uri: Uri? = null
    private var name = "PDF"
    private var doc = 0L
    private var pageSizes = FloatArray(0)
    private var outline: OutlineData? = null
    private var meta: Array<String>? = null
    private var passwordAttempts = 0
    private var uiVisible = true
    private var systemTop = 0
    private var systemBottom = 0

    private val search = SearchState()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val searchDebounce = Runnable { startSearch() }

    // =====================================================================
    // Lifecycle
    // =====================================================================
    override fun onCreate(savedInstanceState: Bundle?) {
        prefs = Prefs(this)
        val dark = prefs.isDark(this)
        setTheme(if (dark) R.style.Feather_Dark else R.style.Feather_Light)
        super.onCreate(savedInstanceState)
        palette = Palette(dark)
        buildUi()
        setupWindow(dark)

        uri = intent?.data ?: streamExtra(intent)
        val u = uri
        if (u == null) {
            finish()
            return
        }
        pageView.restoreZoom(savedInstanceState?.getFloatArray("zoom"))
        pageView.setViewMode(prefs.viewMode)
        pageView.setCover(prefs.coverPage)
        pageView.setColors(prefs.pageColors)
        if (savedInstanceState == null && prefs.fitPage) pageView.setFit(2)
        val start = savedInstanceState?.getInt("page") ?: prefs.lastPage(u.toString())
        openDocument(null, start)
    }

    @Suppress("DEPRECATION")
    private fun streamExtra(i: Intent?): Uri? =
        if (i?.action == Intent.ACTION_SEND) i.getParcelableExtra(Intent.EXTRA_STREAM) else null

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt("page", pageView.currentPage())
        outState.putFloatArray("zoom", pageView.zoomState())
    }

    override fun onPause() {
        super.onPause()
        rememberPosition()
    }

    override fun onDestroy() {
        mainHandler.removeCallbacksAndMessages(null)
        if (search.running) PdfWorker.cancelSearch()
        panel.thumbs.stop()
        PdfWorker.close(doc)
        doc = 0
        super.onDestroy()
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        // In the background: give every rendered page back to the system.
        if (level >= TRIM_MEMORY_UI_HIDDEN) {
            pageView.trimMemory()
            panel.thumbs.stop()
        }
    }

    private fun rememberPosition() {
        val u = uri ?: return
        if (doc != 0L) prefs.remember(u.toString(), name, pageView.currentPage())
    }

    @Deprecated("Framework back handling (no AndroidX dependency)")
    override fun onBackPressed() {
        when {
            panel.isOpen -> panel.close()
            searchBar.visibility == View.VISIBLE -> hideSearch()
            pageView.hasSelection -> pageView.clearSelection()
            else -> @Suppress("DEPRECATION") super.onBackPressed()
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN && event.isCtrlPressed) {
            when (event.keyCode) {
                KeyEvent.KEYCODE_F -> { showSearch(); return true }
                KeyEvent.KEYCODE_P -> { print(); return true }
                KeyEvent.KEYCODE_G -> { goToPageDialog(); return true }
            }
        }
        return super.dispatchKeyEvent(event)
    }

    // =====================================================================
    // UI
    // =====================================================================
    private fun dp(v: Float) = (v * resources.displayMetrics.density).toInt()

    private fun buildUi() {
        root = FrameLayout(this)
        pageView = PageView(this, palette).also { it.host = this }
        root.addView(pageView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        topBox = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(palette.bar)
            elevation = dp(2f).toFloat()
        }
        toolbar = Toolbar(this).apply {
            setTitleTextColor(palette.barText)
            setSubtitleTextColor(palette.barTextDim)
            setNavigationIcon(R.drawable.ic_back)
            navigationContentDescription = "Back"
            setNavigationOnClickListener { finish() }
            setOnMenuItemClickListener { onMenu(it) }
        }
        topBox.addView(toolbar, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(56f)))
        buildSearchBar()
        topBox.addView(searchBar)
        root.addView(topBox, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP))

        panel = SidePanel(this, palette).apply {
            onOutlineClick = { i -> outlineClicked(i) }
            thumbs.onPageClick = { p ->
                pageView.goToPage(p)
                close()
            }
        }
        root.addView(panel, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        topBox.addOnLayoutChangeListener { _, _, top, _, bottom, _, _, _, _ ->
            if (uiVisible) pageView.setInsets(bottom - top, systemBottom)
        }
        setContentView(root)
        buildMenu()
    }

    private fun buildSearchBar() {
        searchBar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(12f), 0, dp(4f), dp(4f))
            visibility = View.GONE
        }
        searchEdit = EditText(this).apply {
            hint = "Find in document"
            setSingleLine()
            imeOptions = EditorInfo.IME_ACTION_SEARCH
            inputType = InputType.TYPE_CLASS_TEXT
            setTextColor(palette.barText)
            setHintTextColor(palette.barTextDim)
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun afterTextChanged(s: Editable?) {
                    mainHandler.removeCallbacks(searchDebounce)
                    mainHandler.postDelayed(searchDebounce, 350)
                }
            })
            setOnEditorActionListener { _, action, _ ->
                if (action == EditorInfo.IME_ACTION_SEARCH) { findNext(true); true } else false
            }
        }
        searchCount = TextView(this).apply {
            setTextColor(palette.barTextDim)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            setPadding(dp(8f), 0, dp(4f), 0)
        }
        matchCaseButton = TextView(this).apply {
            text = "Aa"
            gravity = Gravity.CENTER
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            contentDescription = "Match case"
            setOnClickListener {
                prefs.matchCase = !prefs.matchCase
                updateMatchCase()
                startSearch()
            }
        }
        updateMatchCase()
        searchBar.addView(searchEdit, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        searchBar.addView(searchCount)
        searchBar.addView(matchCaseButton, LinearLayout.LayoutParams(dp(44f), dp(44f)))
        searchBar.addView(iconButton(R.drawable.ic_up, "Previous match") { findNext(false) })
        searchBar.addView(iconButton(R.drawable.ic_down, "Next match") { findNext(true) })
        searchBar.addView(iconButton(R.drawable.ic_close, "Close search") { hideSearch() })
    }

    private fun updateMatchCase() {
        matchCaseButton.setTextColor(if (prefs.matchCase) palette.accent else palette.barTextDim)
        matchCaseButton.paint.isUnderlineText = prefs.matchCase
        matchCaseButton.invalidate()
    }

    private fun iconButton(icon: Int, description: String, onClick: () -> Unit) = ImageButton(this).apply {
        setImageResource(icon)
        contentDescription = description
        background = null
        setOnClickListener { onClick() }
        layoutParams = LinearLayout.LayoutParams(dp(44f), dp(44f))
    }

    /** Edge-to-edge window; the toolbar pads itself below the status bar. */
    @Suppress("DEPRECATION")
    private fun setupWindow(dark: Boolean) {
        window.statusBarColor = Color.TRANSPARENT
        window.navigationBarColor = Color.TRANSPARENT
        if (Build.VERSION.SDK_INT >= 28) {
            window.attributes = window.attributes.apply {
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
        }
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
            val light = if (dark) 0 else WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or
                WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
            window.insetsController?.setSystemBarsAppearance(light,
                WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS)
        } else {
            var flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            if (!dark) flags = flags or View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
            window.decorView.systemUiVisibility = flags
        }
        root.setOnApplyWindowInsetsListener { _, insets ->
            val top: Int
            val bottom: Int
            val left: Int
            val right: Int
            if (Build.VERSION.SDK_INT >= 30) {
                // Stable values, so hiding the bars does not move the content.
                val i = insets.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                top = i.top; bottom = i.bottom; left = i.left; right = i.right
            } else {
                top = insets.stableInsetTop; bottom = insets.stableInsetBottom
                left = insets.stableInsetLeft; right = insets.stableInsetRight
            }
            systemTop = top
            systemBottom = bottom
            topBox.setPadding(left, top, right, 0)
            panel.setTopInset(top)
            root.setPadding(0, 0, 0, 0)
            pageView.setInsets(topBox.height.coerceAtLeast(top + dp(56f)), bottom)
            insets
        }
    }

    // Immersive reading: tap the page to hide or show the toolbar and system bars.
    override fun onToggleUi() {
        setUiVisible(!uiVisible)
    }

    @Suppress("DEPRECATION")
    private fun setUiVisible(visible: Boolean) {
        uiVisible = visible
        topBox.visibility = if (visible) View.VISIBLE else View.GONE
        if (Build.VERSION.SDK_INT >= 30) {
            val c = window.insetsController ?: return
            if (visible) {
                c.show(WindowInsets.Type.systemBars())
            } else {
                c.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                c.hide(WindowInsets.Type.systemBars())
            }
        } else {
            val base = View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or (if (!palette.dark) View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR else 0)
            window.decorView.systemUiVisibility = if (visible) base else base or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_FULLSCREEN
        }
    }

    // =====================================================================
    // Opening
    // =====================================================================
    private fun openDocument(password: String?, startPage: Int) {
        val u = uri ?: return
        pageView.message = "Opening…"
        val resolver = contentResolver
        PdfWorker.post {
            var err = 0
            var handle = 0L
            var sizes: FloatArray? = null
            var ol: OutlineData? = null
            var md: Array<String>? = null
            var displayName = u.lastPathSegment ?: "PDF"
            try {
                resolver.query(u, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                    if (c.moveToFirst()) c.getString(0)?.let { displayName = it }
                }
            } catch (e: Exception) {
                // name stays the path segment
            }
            try {
                val pfd = resolver.openFileDescriptor(u, "r")
                if (pfd == null) {
                    err = ERR_FILE
                } else {
                    pfd.use {
                        val e = IntArray(1)
                        handle = Native.nOpen(it.fd, password, e)  // native keeps its own dup
                        err = e[0]
                    }
                }
            } catch (e: SecurityException) {
                err = ERR_DENIED
            } catch (e: FileNotFoundException) {
                err = ERR_NOT_FOUND
            } catch (e: Exception) {
                err = ERR_FILE
            }
            if (handle != 0L) {
                sizes = Native.nPageSizes(handle)
                ol = Native.nOutline(handle)
                md = Native.nMetadata(handle)
            }
            PdfWorker.toMain { onOpened(handle, err, sizes, ol, md, displayName, startPage) }
        }
    }

    private fun onOpened(handle: Long, err: Int, sizes: FloatArray?, ol: OutlineData?, md: Array<String>?,
                         displayName: String, startPage: Int) {
        if (isDestroyed) {
            PdfWorker.close(handle)
            return
        }
        name = displayName
        toolbar.title = name
        if (handle == 0L || sizes == null) {
            if (err == ERR_PASSWORD) {
                askPassword(startPage)
                return
            }
            val text = when (err) {
                ERR_NOT_FOUND -> "The file could not be found."
                ERR_DENIED -> "Feather PDF is no longer allowed to open this file. Please open it again."
                ERR_FORMAT -> "The file is damaged or is not a PDF document."
                ERR_SECURITY -> "This PDF uses an unsupported encryption method."
                ERR_NO_PAGES -> "The document does not contain any pages."
                else -> "The file could not be read."
            }
            if (err == ERR_NOT_FOUND || err == ERR_DENIED) prefs.forget(uri.toString())
            pageView.message = ""
            AlertDialog.Builder(this).setTitle(name).setMessage(text)
                .setPositiveButton(android.R.string.ok) { _, _ -> finish() }
                .setOnCancelListener { finish() }
                .show()
            return
        }
        doc = handle
        pageSizes = sizes
        outline = ol
        meta = md
        passwordAttempts = 0
        panel.outlineAdapter.outline = ol
        pageView.setDocument(handle, sizes, startPage)
        prefs.remember(uri.toString(), name, startPage)
        @Suppress("DEPRECATION") setTaskDescription(android.app.ActivityManager.TaskDescription(name))
        updateMenuState()
    }

    private fun askPassword(startPage: Int) {
        val edit = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            hint = "Password"
        }
        val box = FrameLayout(this).apply {
            setPadding(dp(20f), dp(8f), dp(20f), 0)
            addView(edit)
        }
        val msg = (if (passwordAttempts > 0) "Incorrect password. " else "") +
            "“$name” is protected. Enter the password to open it."
        AlertDialog.Builder(this).setTitle("Password required").setMessage(msg).setView(box)
            .setPositiveButton("Open") { _, _ ->
                passwordAttempts++
                openDocument(edit.text.toString(), startPage)
            }
            .setNegativeButton(android.R.string.cancel) { _, _ -> finish() }
            .setOnCancelListener { finish() }
            .show()
        edit.requestFocus()
    }

    // =====================================================================
    // PageView.Host
    // =====================================================================
    override fun onPageChanged(page: Int, count: Int) {
        toolbar.subtitle = "Page ${page + 1} of $count"
        if (panel.isOpen && panel.showingThumbs) panel.thumbs.setCurrentPage(page)
    }

    override fun onExternalLink(uri: String) {
        // Show where a link goes before leaving the app.
        val scheme = Uri.parse(uri).scheme?.lowercase()
        if (scheme != "http" && scheme != "https" && scheme != "mailto") {
            Toast.makeText(this, "This link type is not supported for safety.", Toast.LENGTH_SHORT).show()
            return
        }
        AlertDialog.Builder(this).setTitle("Open link?").setMessage(uri)
            .setPositiveButton("Open") { _, _ ->
                try {
                    startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(uri)))
                } catch (e: Exception) {
                    Toast.makeText(this, "No app can open this link.", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    override fun onTextCopied(text: String) {
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("PDF text", text))
        if (Build.VERSION.SDK_INT < 33) Toast.makeText(this, "Copied", Toast.LENGTH_SHORT).show()
    }

    override fun onShareText(text: String) {
        val send = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, text)
        startActivity(Intent.createChooser(send, "Share text"))
    }

    private fun outlineClicked(index: Int) {
        val o = outline ?: return
        val page = o.pages[index]
        val u = o.uris[index]
        panel.close()
        if (page >= 0) pageView.goToTarget(LinkTarget(page, o.ys[index], null))
        else if (u != null) onExternalLink(u)
    }

    // =====================================================================
    // Search
    // =====================================================================
    private fun showSearch() {
        if (doc == 0L) return
        if (!uiVisible) setUiVisible(true)
        searchBar.visibility = View.VISIBLE
        searchEdit.requestFocus()
        searchEdit.selectAll()
        (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
            .showSoftInput(searchEdit, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun hideSearch() {
        mainHandler.removeCallbacks(searchDebounce)
        PdfWorker.cancelSearch()
        search.reset()
        search.query = ""
        pageView.search = null
        searchBar.visibility = View.GONE
        (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
            .hideSoftInputFromWindow(searchEdit.windowToken, 0)
        pageView.invalidate()
    }

    private fun startSearch() {
        mainHandler.removeCallbacks(searchDebounce)
        PdfWorker.cancelSearch()
        search.reset()
        search.query = searchEdit.text.toString()
        val id = ++search.id
        pageView.search = search
        if (search.query.isNotEmpty() && doc != 0L) {
            search.running = true
            search.pageCount = pageView.pageCount
            PdfWorker.startSearch(doc, pageView.pageCount, pageView.currentPage(), search.query, prefs.matchCase) { _, hits, done, finished ->
                if (id != search.id || isDestroyed) return@startSearch
                search.pagesDone = done
                if (finished) search.running = false
                if (search.add(hits)) search.currentHit()?.let { pageView.scrollToHit(it) }
                if (hits.isNotEmpty()) pageView.invalidate()
                searchCount.text = search.status()
            }
        }
        searchCount.text = search.status()
        pageView.invalidate()
    }

    private fun findNext(forward: Boolean) {
        if (searchEdit.text.toString() != search.query) {
            startSearch()
            return
        }
        if (search.matches.isEmpty()) return
        if (forward) search.next() else search.prev()
        search.currentHit()?.let { pageView.scrollToHit(it) }
        searchCount.text = search.status()
        pageView.invalidate()
        (getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
            .hideSoftInputFromWindow(searchEdit.windowToken, 0)
    }

    // =====================================================================
    // Menu
    // =====================================================================
    private fun buildMenu() {
        val m = toolbar.menu
        m.add(0, M_SEARCH, 0, "Search").setIcon(R.drawable.ic_search).setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS)
        m.add(0, M_CONTENTS, 1, "Contents and pages").setIcon(R.drawable.ic_toc).setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS)
        m.add(0, M_GOTO, 2, "Go to page…")
        m.add(0, M_THUMBS, 3, "Page thumbnails")
        val layout = m.addSubMenu(0, M_LAYOUT, 4, "Page layout")
        layout.add(G_LAYOUT, M_SINGLE, 0, "Single page")
        layout.add(G_LAYOUT, M_CONTINUOUS, 1, "Continuous")
        layout.add(G_LAYOUT, M_TWO, 2, "Two pages")
        layout.setGroupCheckable(G_LAYOUT, true, true)
        layout.add(0, M_COVER, 3, "Show cover page separately").isCheckable = true
        m.add(0, M_FIT_WIDTH, 5, "Fit width")
        m.add(0, M_FIT_PAGE, 6, "Fit page")
        m.add(0, M_ROT_LEFT, 7, "Rotate left")
        m.add(0, M_ROT_RIGHT, 8, "Rotate right")
        val colors = m.addSubMenu(0, M_COLORS, 9, "Page colours")
        colors.add(G_COLORS, M_COLORS_NORMAL, 0, "Normal")
        colors.add(G_COLORS, M_COLORS_DARK, 1, "Dark (night mode)")
        colors.add(G_COLORS, M_COLORS_DIM, 2, "Dimmed")
        colors.setGroupCheckable(G_COLORS, true, true)
        val theme = m.addSubMenu(0, M_THEME, 10, "Theme")
        theme.add(G_THEME, M_THEME_SYSTEM, 0, "System default")
        theme.add(G_THEME, M_THEME_LIGHT, 1, "Light")
        theme.add(G_THEME, M_THEME_DARK, 2, "Dark")
        theme.setGroupCheckable(G_THEME, true, true)
        m.add(0, M_PRINT, 11, "Print…")
        m.add(0, M_SHARE, 12, "Share document")
        m.add(0, M_OPEN_WITH, 13, "Open with…")
        m.add(0, M_SHARE_IMAGE, 14, "Share page as image")
        m.add(0, M_SELECT_ALL, 15, "Select all text")
        m.add(0, M_PROPERTIES, 16, "Document properties")
        m.add(0, M_ABOUT, 17, "About Feather PDF")
        updateMenuState()
    }

    private fun updateMenuState() {
        val m = toolbar.menu
        m.findItem(M_SINGLE)?.isChecked = pageView.viewMode == ViewMode.SINGLE
        m.findItem(M_CONTINUOUS)?.isChecked = pageView.viewMode == ViewMode.CONTINUOUS
        m.findItem(M_TWO)?.isChecked = pageView.viewMode == ViewMode.TWO_PAGE
        m.findItem(M_COVER)?.apply {
            isChecked = pageView.coverPage
            isEnabled = pageView.viewMode == ViewMode.TWO_PAGE
        }
        m.findItem(M_COLORS_NORMAL + pageView.pageColors)?.isChecked = true
        m.findItem(M_THEME_SYSTEM + prefs.themeMode)?.isChecked = true
        val hasDoc = doc != 0L
        for (id in intArrayOf(M_SEARCH, M_CONTENTS, M_GOTO, M_THUMBS, M_PRINT, M_SHARE_IMAGE, M_SELECT_ALL, M_PROPERTIES))
            m.findItem(id)?.isEnabled = hasDoc
    }

    private fun onMenu(item: MenuItem): Boolean {
        when (item.itemId) {
            M_SEARCH -> showSearch()
            M_CONTENTS -> panel.open(false)
            M_THUMBS -> openThumbnails()
            M_GOTO -> goToPageDialog()
            M_SINGLE -> setViewMode(ViewMode.SINGLE)
            M_CONTINUOUS -> setViewMode(ViewMode.CONTINUOUS)
            M_TWO -> setViewMode(ViewMode.TWO_PAGE)
            M_COVER -> {
                prefs.coverPage = !pageView.coverPage
                pageView.setCover(prefs.coverPage)
            }
            M_FIT_WIDTH -> { prefs.fitPage = false; pageView.setFit(1) }
            M_FIT_PAGE -> { prefs.fitPage = true; pageView.setFit(2) }
            M_ROT_LEFT -> pageView.rotate(-1)
            M_ROT_RIGHT -> pageView.rotate(1)
            M_COLORS_NORMAL, M_COLORS_DARK, M_COLORS_DIM -> {
                prefs.pageColors = item.itemId - M_COLORS_NORMAL
                pageView.setColors(prefs.pageColors)
            }
            M_THEME_SYSTEM, M_THEME_LIGHT, M_THEME_DARK -> {
                prefs.themeMode = item.itemId - M_THEME_SYSTEM
                recreate()
            }
            M_PRINT -> print()
            M_SHARE -> shareDocument()
            M_OPEN_WITH -> openWith()
            M_SHARE_IMAGE -> sharePageImage()
            M_SELECT_ALL -> pageView.selectAll()
            M_PROPERTIES -> showProperties()
            M_ABOUT -> showAbout(this)
            else -> return false
        }
        updateMenuState()
        return true
    }

    private fun setViewMode(mode: Int) {
        prefs.viewMode = mode
        pageView.setViewMode(mode)
    }

    private fun openThumbnails() {
        panel.thumbs.setDocument(doc, pageSizes, pageView.rotation90, pageView.pageColors)
        panel.thumbs.setCurrentPage(pageView.currentPage())
        panel.open(true)
    }

    private fun goToPageDialog() {
        if (doc == 0L) return
        val edit = EditText(this).apply {
            inputType = InputType.TYPE_CLASS_NUMBER
            hint = "1 – ${pageView.pageCount}"
        }
        val box = FrameLayout(this).apply {
            setPadding(dp(20f), dp(8f), dp(20f), 0)
            addView(edit)
        }
        AlertDialog.Builder(this).setTitle("Go to page").setView(box)
            .setPositiveButton("Go") { _, _ ->
                edit.text.toString().toIntOrNull()?.let { pageView.goToPage(it - 1) }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
        edit.requestFocus()
    }

    // =====================================================================
    // Print, share, properties
    // =====================================================================
    /** Printing hands the original PDF to Android's print system (page ranges, copies, fit). */
    private fun print() {
        val u = uri ?: return
        if (doc == 0L) return
        val pm = getSystemService(Context.PRINT_SERVICE) as PrintManager
        val count = pageView.pageCount
        val jobName = name
        pm.print(jobName, object : PrintDocumentAdapter() {
            override fun onLayout(old: PrintAttributes?, new: PrintAttributes?, cancel: CancellationSignal?,
                                  callback: LayoutResultCallback, extras: Bundle?) {
                if (cancel?.isCanceled == true) {
                    callback.onLayoutCancelled()
                    return
                }
                val info = PrintDocumentInfo.Builder(jobName)
                    .setContentType(PrintDocumentInfo.CONTENT_TYPE_DOCUMENT)
                    .setPageCount(count)
                    .build()
                callback.onLayoutFinished(info, old != new)
            }

            override fun onWrite(pages: Array<out PageRange>?, destination: ParcelFileDescriptor,
                                 cancel: CancellationSignal?, callback: WriteResultCallback) {
                Thread {
                    try {
                        contentResolver.openInputStream(u)?.use { input ->
                            FileOutputStream(destination.fileDescriptor).use { input.copyTo(it) }
                        }
                        runOnUiThread { callback.onWriteFinished(arrayOf(PageRange.ALL_PAGES)) }
                    } catch (e: Exception) {
                        runOnUiThread { callback.onWriteFailed(e.message) }
                    }
                }.start()
            }
        }, null)
    }

    private fun shareDocument() {
        val u = uri ?: return
        try {
            val send = Intent(Intent.ACTION_SEND).setType("application/pdf")
                .putExtra(Intent.EXTRA_STREAM, u)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            send.clipData = ClipData.newRawUri(name, u)
            startActivity(Intent.createChooser(send, "Share $name"))
        } catch (e: Exception) {
            Toast.makeText(this, "This file can not be shared.", Toast.LENGTH_SHORT).show()
        }
    }

    private fun openWith() {
        val u = uri ?: return
        try {
            val view = Intent(Intent.ACTION_VIEW).setDataAndType(u, "application/pdf")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            val chooser = Intent.createChooser(view, "Open with")
            chooser.putExtra(Intent.EXTRA_EXCLUDE_COMPONENTS, arrayOf(ComponentName(this, ReaderActivity::class.java)))
            startActivity(chooser)
        } catch (e: Exception) {
            Toast.makeText(this, "No other app can open this file.", Toast.LENGTH_SHORT).show()
        }
    }

    /** Renders the current page at 200 DPI (max ~8 megapixels) and shares it as PNG. */
    private fun sharePageImage() {
        if (doc == 0L) return
        val page = pageView.currentPage()
        val (w, h) = pageView.imageSize(page, 200f, 8e6)
        val rot = pageView.rotation90
        val handle = doc
        val dir = File(cacheDir, ImageProvider.DIR).apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() }  // keep at most the latest image
        val file = File(dir, "page-${page + 1}.png")
        Toast.makeText(this, "Preparing image…", Toast.LENGTH_SHORT).show()
        PdfWorker.post {
            var ok = false
            try {
                val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
                if (Native.nRender(handle, page, bmp, 0, 0, w, h, w, h, rot, PageColors.NORMAL)) {
                    FileOutputStream(file).use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
                    ok = true
                }
                bmp.recycle()
            } catch (e: Throwable) {
                ok = false
            }
            PdfWorker.toMain {
                if (!ok || isDestroyed) return@toMain
                val imageUri = ImageProvider.uriFor(this, file)
                val send = Intent(Intent.ACTION_SEND).setType("image/png")
                    .putExtra(Intent.EXTRA_STREAM, imageUri)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                send.clipData = ClipData.newRawUri(file.name, imageUri)
                startActivity(Intent.createChooser(send, "Share page ${page + 1}"))
            }
        }
    }

    private fun showProperties() {
        val md = meta ?: return
        val sb = StringBuilder()
        fun line(label: String, value: String?) {
            if (!value.isNullOrEmpty()) sb.append(label).append(": ").append(value).append('\n')
        }
        line("File", name)
        uri?.let { u ->
            try {
                contentResolver.query(u, arrayOf(OpenableColumns.SIZE), null, null, null)?.use { c ->
                    if (c.moveToFirst() && !c.isNull(0)) line("File size", formatBytes(c.getLong(0)))
                }
            } catch (e: Exception) {
                // size unknown
            }
        }
        sb.append('\n')
        line("Title", md[0]); line("Author", md[1]); line("Subject", md[2]); line("Keywords", md[3])
        line("Created", formatPdfDate(md[6])); line("Modified", formatPdfDate(md[7]))
        line("Creator", md[4]); line("Producer", md[5])
        sb.append('\n')
        md[8].toIntOrNull()?.takeIf { it > 0 }?.let { line("Version", "PDF ${it / 10}.${it % 10}") }
        line("Pages", pageView.pageCount.toString())
        val p = pageView.currentPage()
        if (p * 2 + 1 < pageSizes.size) {
            val w = pageSizes[p * 2]
            val h = pageSizes[p * 2 + 1]
            line("Page size", String.format("%.0f × %.0f mm (%.2f × %.2f in)", w / 72 * 25.4, h / 72 * 25.4, w / 72, h / 72))
        }
        line("Encrypted", if (md[9] == "1") "Yes" else "No")
        AlertDialog.Builder(this).setTitle("Document properties").setMessage(sb.toString().trim())
            .setPositiveButton(android.R.string.ok, null).show()
    }

    companion object {
        // Error codes: PDFium's (2-5), plus our own.
        const val ERR_FILE = 2
        const val ERR_FORMAT = 3
        const val ERR_PASSWORD = 4
        const val ERR_SECURITY = 5
        const val ERR_NO_PAGES = 6
        const val ERR_DENIED = 7
        const val ERR_NOT_FOUND = 8

        private const val M_SEARCH = 1
        private const val M_CONTENTS = 2
        private const val M_GOTO = 3
        private const val M_THUMBS = 4
        private const val M_LAYOUT = 5
        private const val M_SINGLE = 6
        private const val M_CONTINUOUS = 7
        private const val M_TWO = 8
        private const val M_COVER = 9
        private const val M_FIT_WIDTH = 10
        private const val M_FIT_PAGE = 11
        private const val M_ROT_LEFT = 12
        private const val M_ROT_RIGHT = 13
        private const val M_COLORS = 14
        private const val M_COLORS_NORMAL = 15  // + PageColors
        private const val M_COLORS_DARK = 16
        private const val M_COLORS_DIM = 17
        private const val M_THEME = 18
        private const val M_THEME_SYSTEM = 19  // + theme mode
        private const val M_THEME_LIGHT = 20
        private const val M_THEME_DARK = 21
        private const val M_PRINT = 22
        private const val M_SHARE = 23
        private const val M_OPEN_WITH = 24
        private const val M_SHARE_IMAGE = 25
        private const val M_SELECT_ALL = 26
        private const val M_PROPERTIES = 27
        private const val M_ABOUT = 28
        private const val G_LAYOUT = 1
        private const val G_COLORS = 2
        private const val G_THEME = 3

        fun formatBytes(bytes: Long): String = when {
            bytes >= 1 shl 20 -> String.format("%.1f MB", bytes / 1048576.0)
            bytes >= 1024 -> String.format("%.1f KB", bytes / 1024.0)
            else -> "$bytes bytes"
        }

        /** "D:20240131154500+01'00'" -> "2024-01-31 15:45" */
        fun formatPdfDate(d: String): String {
            val s = d.removePrefix("D:")
            if (s.length < 8 || !s.take(8).all { it.isDigit() }) return d
            var out = "${s.substring(0, 4)}-${s.substring(4, 6)}-${s.substring(6, 8)}"
            if (s.length >= 12 && s.substring(8, 12).all { it.isDigit() }) out += " ${s.substring(8, 10)}:${s.substring(10, 12)}"
            return out
        }

        fun showAbout(activity: Activity) {
            AlertDialog.Builder(activity).setTitle("Feather PDF")
                .setMessage("Version ${BuildConfigCompat.versionName(activity)} for Android\n\n" +
                    "© 2026 Akshaya Simha.\nDeveloped for faster experience.\n\n" +
                    "PDF rendering: PDFium (BSD-3-Clause / Apache-2.0), Copyright The PDFium Authors.")
                .setPositiveButton(android.R.string.ok, null).show()
        }
    }
}

/** Version name without enabling the BuildConfig feature. */
object BuildConfigCompat {
    fun versionName(context: Context): String = try {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: ""
    } catch (e: Exception) {
        ""
    }
}
