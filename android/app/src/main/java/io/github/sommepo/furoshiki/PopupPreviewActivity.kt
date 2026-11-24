// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.app.ActivityManager
import android.content.SharedPreferences
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.widget.FrameLayout
import android.widget.TextView
import io.github.sommepo.furoshiki.reader.Console
import io.github.sommepo.furoshiki.reader.ReaderSettings
import io.github.sommepo.furoshiki.reader.ui.PopupPage
import io.github.sommepo.furoshiki.reader.ui.ReaderSettingsActivity
import org.json.JSONObject

/**
 * The popup as its settings make it, on the first display while those settings are changed on
 * the second (the maintainer's choice of 2026-11-17, 4A: "the top shows what the page changes:
 * the popup"). A made-up word and grammar entry, drawn again at every change. It runs in the
 * settings page's own process (`:game`), where their changes are heard, and goes when that page
 * does.
 */
class PopupPreviewActivity : Activity(), SharedPreferences.OnSharedPreferenceChangeListener {
    private lateinit var page: PopupPage
    private lateinit var settings: ReaderSettings

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        settings = ReaderSettings(this)
        val density = resources.displayMetrics.density
        val root = FrameLayout(this).apply { setBackgroundColor(0xB3000000.toInt()) }
        val caption = TextView(this).apply {
            text = "POPUP  ·  PREVIEW"
            typeface = Typeface.MONOSPACE
            letterSpacing = .16f
            textSize = 12f
            setTextColor(0xFFECE5D3.toInt())
        }
        root.addView(caption, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.START).apply {
            setMargins((40 * density).toInt(), (24 * density).toInt(), 0, 0)
        })
        page = PopupPage(this) { message ->
            if (message.optString("type") == "size") {
                val height = (message.optDouble("height", 0.0) * density).toInt()
                if (height > 0) page.layoutParams = page.layoutParams.also { it.height = height }
            }
        }
        page.setBackgroundColor(Color.TRANSPARENT)
        root.addView(page, FrameLayout.LayoutParams((420 * density).toInt(), (300 * density).toInt(), Gravity.CENTER))
        setContentView(root)
        show()
        watch()
    }

    override fun onStart() {
        super.onStart()
        settings.preferences.registerOnSharedPreferenceChangeListener(this)
    }

    override fun onStop() {
        settings.preferences.unregisterOnSharedPreferenceChangeListener(this)
        super.onStop()
    }

    override fun onSharedPreferenceChanged(prefs: SharedPreferences?, key: String?) {
        if (key == null || key.startsWith("reader_popup") || key == ReaderSettings.KEY_SHELF_THEME || key == ReaderSettings.KEY_CARD_CORNER) show()
    }

    private fun show() {
        val theme = settings.popupTheme
        val css = when (theme) {
            "console", "classic" -> PopupPage.consoleCss(this)
            else -> PopupPage.themeCss(this, theme)
        }
        page.setStyle("theme", css)
        page.setStyle("device", PopupPage.DEVICE_CSS)
        val labels = JSONObject()
            .put("add", getString(R.string.reader_lookup_add))
            .put("addAudio", getString(R.string.reader_lookup_add_audio))
            .put("book", getString(R.string.reader_lookup_in_deck))
            .put("more", getString(R.string.reader_lookup_more))
        val model = PopupPage.model(PopupPage.SAMPLE, null, settings.cardCorner, false, true, settings.popupScale, Console.APP_KEY, "", labels, settings.popupLook)
        page.call("Reader.show($model)")
    }

    /** Gone with the page of settings (another task, maybe another display): looked at twice a second. */
    private fun watch() {
        val tasks = getSystemService(ActivityManager::class.java)
        val name = ReaderSettingsActivity::class.java.name
        var seen = false
        var waited = 0
        val check = object : Runnable {
            override fun run() {
                if (isFinishing) return
                waited++
                val open = runCatching {
                    tasks.appTasks.any { it.taskInfo.baseActivity?.className == name || it.taskInfo.topActivity?.className == name }
                }.getOrDefault(false)
                if (open) seen = true
                if (open || (!seen && waited < 20)) window.decorView.postDelayed(this, 500) else finish()
            }
        }
        window.decorView.postDelayed(check, 500)
    }
}
