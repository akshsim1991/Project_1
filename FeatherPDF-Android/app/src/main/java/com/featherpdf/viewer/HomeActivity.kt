package com.featherpdf.viewer

import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.text.TextUtils
import android.text.format.DateUtils
import android.view.Gravity
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import android.widget.Toolbar

/**
 * Launcher screen: an "Open PDF" button and the recent-documents list.
 * Documents themselves open in [ReaderActivity], one Recents entry each.
 */
class HomeActivity : Activity() {
    private lateinit var prefs: Prefs
    private lateinit var palette: Palette
    private lateinit var list: ListView
    private lateinit var empty: TextView
    private val adapter = RecentAdapter()
    private var items: List<Prefs.Recent> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        prefs = Prefs(this)
        val dark = prefs.isDark(this)
        setTheme(if (dark) R.style.Feather_Dark else R.style.Feather_Light)
        super.onCreate(savedInstanceState)
        palette = Palette(dark)
        buildUi()
        @Suppress("DEPRECATION")
        window.statusBarColor = palette.bar
        @Suppress("DEPRECATION")
        window.navigationBarColor = palette.canvas
        if (!dark) {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or
                (if (android.os.Build.VERSION.SDK_INT >= 26) View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR else 0)
        }
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun dp(v: Float) = (v * resources.displayMetrics.density + 0.5f).toInt()

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(palette.canvas)
        }
        val toolbar = Toolbar(this).apply {
            title = "Feather PDF"
            setTitleTextColor(palette.barText)
            setBackgroundColor(palette.bar)
            elevation = dp(2f).toFloat()
            buildMenu(menu)
            setOnMenuItemClickListener { onMenu(it) }
        }
        root.addView(toolbar, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(56f)))

        val open = Button(this).apply {
            text = "Open PDF"
            setCompoundDrawablesRelativeWithIntrinsicBounds(R.drawable.ic_folder_open, 0, 0, 0)
            compoundDrawablePadding = dp(8f)
            setOnClickListener { pickDocument() }
        }
        root.addView(open, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            gravity = Gravity.CENTER_HORIZONTAL
            topMargin = dp(16f)
            bottomMargin = dp(8f)
        })

        root.addView(TextView(this).apply {
            text = "Recent"
            setTextColor(palette.barTextDim)
            textSize = 13f
            setTypeface(typeface, Typeface.BOLD)
            setPadding(dp(16f), dp(8f), dp(16f), dp(4f))
        })

        val body = FrameLayout(this)
        list = ListView(this).apply {
            adapter = this@HomeActivity.adapter
            divider = null
            setOnItemClickListener { _, _, pos, _ -> openRecent(items[pos]) }
            setOnItemLongClickListener { _, _, pos, _ -> confirmRemove(items[pos]); true }
            clipToPadding = false
        }
        empty = TextView(this).apply {
            text = "Documents you open appear here.\nYou can also open PDFs from your file manager, email or browser."
            setTextColor(palette.barTextDim)
            gravity = Gravity.CENTER
            setPadding(dp(32f), dp(32f), dp(32f), dp(32f))
        }
        body.addView(list)
        body.addView(empty)
        root.addView(body, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)

        root.setOnApplyWindowInsetsListener { v, insets ->
            @Suppress("DEPRECATION")
            v.setPadding(0, insets.systemWindowInsetTop, 0, 0)
            @Suppress("DEPRECATION")
            list.setPadding(0, 0, 0, insets.systemWindowInsetBottom)
            insets
        }
    }

    private fun refresh() {
        items = prefs.recents()
        adapter.notifyDataSetChanged()
        empty.visibility = if (items.isEmpty()) View.VISIBLE else View.GONE
    }

    // --- menu ------------------------------------------------------------------
    private fun buildMenu(m: Menu) {
        m.add(0, M_OPEN, 0, "Open PDF").setIcon(R.drawable.ic_folder_open).setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
        val theme = m.addSubMenu(0, M_THEME, 1, "Theme")
        theme.add(G_THEME, M_THEME_SYSTEM, 0, "Follow system")
        theme.add(G_THEME, M_THEME_LIGHT, 1, "Light")
        theme.add(G_THEME, M_THEME_DARK, 2, "Dark")
        theme.setGroupCheckable(G_THEME, true, true)
        theme.findItem(M_THEME_SYSTEM + prefs.themeMode)?.isChecked = true
        m.add(0, M_CLEAR, 2, "Clear recent list")
        m.add(0, M_ABOUT, 3, "About")
    }

    private fun onMenu(item: MenuItem): Boolean {
        when (item.itemId) {
            M_OPEN -> pickDocument()
            M_THEME_SYSTEM, M_THEME_LIGHT, M_THEME_DARK -> {
                val mode = item.itemId - M_THEME_SYSTEM
                if (mode != prefs.themeMode) {
                    prefs.themeMode = mode
                    recreate()
                }
            }
            M_CLEAR -> AlertDialog.Builder(this)
                .setMessage("Clear the list of recent documents?")
                .setPositiveButton("Clear") { _, _ ->
                    releaseAll()
                    prefs.clearRecents()
                    refresh()
                }
                .setNegativeButton(android.R.string.cancel, null).show()
            M_ABOUT -> ReaderActivity.showAbout(this)
            else -> return false
        }
        return true
    }

    // --- opening documents -------------------------------------------------------
    private fun pickDocument() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/pdf")
        try {
            @Suppress("DEPRECATION")
            startActivityForResult(i, REQ_OPEN)
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(this, "No document picker is available on this device.", Toast.LENGTH_LONG).show()
        }
    }

    @Deprecated("Framework result API (no AndroidX dependency)")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_OPEN || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        try {
            // Keep access across restarts so the recent list keeps working.
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
            // Provider does not offer persistable grants; the document still opens now.
        }
        openReader(uri)
    }

    private fun openRecent(r: Prefs.Recent) = openReader(Uri.parse(r.uri))

    private fun openReader(uri: Uri) {
        val i = Intent(Intent.ACTION_VIEW, uri, this, ReaderActivity::class.java)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_DOCUMENT)
        startActivity(i)
    }

    private fun confirmRemove(r: Prefs.Recent) {
        AlertDialog.Builder(this)
            .setTitle(r.name)
            .setMessage("Remove from the recent list?")
            .setPositiveButton("Remove") { _, _ ->
                release(r.uri)
                prefs.forget(r.uri)
                refresh()
            }
            .setNegativeButton(android.R.string.cancel, null).show()
    }

    private fun release(uri: String) {
        try {
            contentResolver.releasePersistableUriPermission(Uri.parse(uri), Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: SecurityException) {
        }
    }

    private fun releaseAll() {
        for (p in contentResolver.persistedUriPermissions) release(p.uri.toString())
    }

    // --- recent list -------------------------------------------------------------
    private inner class RecentAdapter : BaseAdapter() {
        override fun getCount() = items.size
        override fun getItem(position: Int) = items[position]
        override fun getItemId(position: Int) = position.toLong()

        override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
            val row = convertView as? LinearLayout ?: makeRow()
            val r = items[position]
            (row.getChildAt(1) as LinearLayout).let {
                (it.getChildAt(0) as TextView).text = r.name.ifEmpty { "Untitled" }
                val ago = DateUtils.getRelativeTimeSpanString(r.time, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS)
                (it.getChildAt(1) as TextView).text = "Page ${r.page + 1} · $ago"
            }
            return row
        }

        private fun makeRow(): LinearLayout = LinearLayout(this@HomeActivity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            minimumHeight = dp(64f)
            setPadding(dp(16f), dp(8f), dp(16f), dp(8f))
            val attrs = obtainStyledAttributes(intArrayOf(android.R.attr.selectableItemBackground))
            background = attrs.getDrawable(0)
            attrs.recycle()
            addView(ImageView(context).apply {
                setImageResource(R.drawable.ic_pdf)
                setColorFilter(palette.accent)
            }, LinearLayout.LayoutParams(dp(28f), dp(28f)).apply { marginEnd = dp(16f) })
            addView(LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                addView(TextView(context).apply {
                    setTextColor(palette.barText)
                    textSize = 16f
                    maxLines = 1
                    ellipsize = TextUtils.TruncateAt.MIDDLE
                })
                addView(TextView(context).apply {
                    setTextColor(palette.barTextDim)
                    textSize = 13f
                    maxLines = 1
                })
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
    }

    companion object {
        private const val REQ_OPEN = 1
        private const val M_OPEN = 1
        private const val M_THEME = 2
        private const val M_CLEAR = 3
        private const val M_ABOUT = 4
        private const val M_THEME_SYSTEM = 10
        private const val M_THEME_LIGHT = 11
        private const val M_THEME_DARK = 12
        private const val G_THEME = 1
    }
}
