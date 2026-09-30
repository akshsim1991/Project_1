package com.featherpdf.viewer

import android.content.Context
import android.content.res.Configuration
import org.json.JSONArray
import org.json.JSONObject

/** Persisted preferences and the recent-documents list. */
class Prefs(context: Context) {
    private val sp = context.getSharedPreferences("feather", Context.MODE_PRIVATE)

    var themeMode: Int  // 0 = system, 1 = light, 2 = dark
        get() = sp.getInt("theme", 0)
        set(v) = sp.edit().putInt("theme", v).apply()

    var viewMode: Int
        get() = sp.getInt("viewMode", ViewMode.CONTINUOUS)
        set(v) = sp.edit().putInt("viewMode", v).apply()

    var coverPage: Boolean
        get() = sp.getBoolean("cover", true)
        set(v) = sp.edit().putBoolean("cover", v).apply()

    var pageColors: Int
        get() = sp.getInt("colors", PageColors.NORMAL)
        set(v) = sp.edit().putInt("colors", v).apply()

    var fitPage: Boolean  // false = fit width (default)
        get() = sp.getBoolean("fitPage", false)
        set(v) = sp.edit().putBoolean("fitPage", v).apply()

    var matchCase: Boolean
        get() = sp.getBoolean("matchCase", false)
        set(v) = sp.edit().putBoolean("matchCase", v).apply()

    fun isDark(context: Context) = when (themeMode) {
        1 -> false
        2 -> true
        else -> (context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) ==
            Configuration.UI_MODE_NIGHT_YES
    }

    // --- recent documents ------------------------------------------------------
    class Recent(val uri: String, val name: String, val page: Int, val time: Long)

    fun recents(): List<Recent> {
        val arr = try { JSONArray(sp.getString("recents", "[]")) } catch (e: Exception) { JSONArray() }
        return (0 until arr.length()).mapNotNull { i ->
            arr.optJSONObject(i)?.let {
                Recent(it.optString("uri"), it.optString("name"), it.optInt("page"), it.optLong("time"))
            }
        }
    }

    fun lastPage(uri: String) = recents().firstOrNull { it.uri == uri }?.page ?: 0

    fun remember(uri: String, name: String, page: Int) {
        val list = recents().filter { it.uri != uri }.toMutableList()
        list.add(0, Recent(uri, name, page, System.currentTimeMillis()))
        save(list.take(MAX_RECENTS))
    }

    fun forget(uri: String) = save(recents().filter { it.uri != uri })

    fun clearRecents() = save(emptyList())

    private fun save(list: List<Recent>) {
        val arr = JSONArray()
        list.forEach {
            arr.put(JSONObject().put("uri", it.uri).put("name", it.name).put("page", it.page).put("time", it.time))
        }
        sp.edit().putString("recents", arr.toString()).apply()
    }

    companion object {
        const val MAX_RECENTS = 30
    }
}

/** Colours of the reader chrome, matching the Windows app's light/dark palette. */
class Palette(val dark: Boolean) {
    val bar = if (dark) 0xFF202020.toInt() else 0xFFF9F9F9.toInt()
    val barText = if (dark) 0xFFEBEBEB.toInt() else 0xFF1C1C1C.toInt()
    val barTextDim = if (dark) 0xFF9A9A9A.toInt() else 0xFF6E6E6E.toInt()
    val canvas = if (dark) 0xFF2B2B2B.toInt() else 0xFFE8E8E8.toInt()
    val pageBorder = if (dark) 0xFF141414.toInt() else 0xFFBEBEBE.toInt()
    val accent = if (dark) 0xFF60CDFF.toInt() else 0xFF005FB8.toInt()
    val panel = if (dark) 0xFF262626.toInt() else 0xFFFFFFFF.toInt()
    val divider = if (dark) 0xFF3A3A3A.toInt() else 0xFFDDDDDD.toInt()
    val field = if (dark) 0xFF2D2D2D.toInt() else 0xFFFFFFFF.toInt()
}
