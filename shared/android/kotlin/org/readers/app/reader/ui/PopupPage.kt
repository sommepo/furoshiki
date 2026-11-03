// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.webkit.JavascriptInterface
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import org.json.JSONArray
import org.json.JSONObject
import org.readers.app.reader.ReaderLog

/**
 * The dictionary popup's page (shared/popup/, copied into the app's assets as reader/popup/ by
 * shared/android/reader.gradle) in a WebView: the same page and themes as on the desktop. The
 * popup ([LookupPopupView]) calls the page's script ([call]: "Reader.show(...)" and so on) and
 * hears what it says ([onMessage]: its size, a press). Calls made before the page is ready wait.
 */
@SuppressLint("SetJavaScriptEnabled", "ViewConstructor")
class PopupPage(context: Context, private val onMessage: (JSONObject) -> Unit) : WebView(context) {
    private val main = Handler(Looper.getMainLooper())
    private var ready = false
    private val waiting = ArrayList<String>()

    init {
        setBackgroundColor(Color.TRANSPARENT)
        settings.javaScriptEnabled = true
        settings.allowFileAccess = false
        settings.allowContentAccess = false
        isVerticalScrollBarEnabled = false
        isHorizontalScrollBarEnabled = false
        overScrollMode = OVER_SCROLL_NEVER
        isFocusable = false
        isFocusableInTouchMode = false
        webViewClient = object : WebViewClient() {
            // A link in a dictionary's text does not take the popup away.
            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest) = true
        }
        addJavascriptInterface(Bridge(), "readerAndroid")
        loadUrl(PAGE)
    }

    /** Runs [script] in the page once it is ready. */
    fun call(script: String) {
        if (ready) evaluateJavascript(script, null) else waiting.add(script)
    }

    fun setStyle(id: String, css: String) = call("Reader.setStyle(${JSONObject.quote(id)}, ${JSONObject.quote(css)})")

    private inner class Bridge {
        @JavascriptInterface
        fun post(text: String) {
            main.post {
                val message = runCatching { JSONObject(text) }.getOrNull() ?: return@post
                if (message.optString("type") == "ready" && !ready) {
                    ready = true
                    waiting.forEach { evaluateJavascript(it, null) }
                    waiting.clear()
                }
                onMessage(message)
            }
        }
    }

    companion object {
        private const val PAGE = "file:///android_asset/reader/popup/popup.html"
        private const val THEMES = "reader/popup/themes/"

        /** Whether a WebView can be had here (not in the JVM tests, which have none that runs). */
        val available: Boolean get() = Build.FINGERPRINT != "robolectric"

        /** A theme's CSS from the assets; empty for "console" (the page's own, in the console's colours). */
        fun themeCss(context: Context, theme: String): String =
            if (theme == "console") "" else runCatching {
                context.assets.open("$THEMES$theme.css").bufferedReader().use { it.readText() }
            }.getOrElse {
                ReaderLog.warning("[Reader] No popup theme $theme: ${it.message}")
                ""
            }

        /** The console's colours as the page's CSS variables; its ground is a painting, its middle stands for it. */
        fun consoleCss(context: Context): String {
            val ground = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888)
            ConsoleLook.ground(context.resources.displayMetrics.density).apply {
                setBounds(0, 0, 64, 64)
                draw(Canvas(ground))
            }
            val middle = ground.getPixel(32, 32)
            fun css(colour: Int) = String.format("#%06x", colour and 0xFFFFFF)
            return ":root { --text: ${css(ReaderStyle.POPUP_TEXT)}; --dim: ${css(ReaderStyle.POPUP_DIM)}; " +
                "--accent: ${css(ReaderStyle.POPUP_ACCENT)}; --ground: ${if (Color.alpha(middle) > 0) css(middle) else "#f0f0f4"}; " +
                "--edge: ${css(ReaderStyle.POPUP_EDGE)}; --box: ${css(ReaderStyle.POPUP_BOX)}; " +
                "--box-pressed: ${css(ReaderStyle.POPUP_BOX_PRESSED)}; --sign: ${css(ReaderStyle.POPUP_SIGN)}; " +
                "--book: ${css(ReaderStyle.POPUP_BOOK_SIGN)}; }"
        }

        fun model(entries: JSONArray, message: JSONObject?, corner: String, duplicates: Boolean, markUnknown: Boolean,
                  scale: Int, console: String, game: String, labels: JSONObject): JSONObject = JSONObject().apply {
            put("entries", entries)
            put("corner", corner)
            put("duplicates", duplicates)
            put("markUnknown", markUnknown)
            put("scale", scale)
            put("console", console)
            put("game", game)
            put("labels", labels)
            if (message != null) put("message", message)
        }
    }
}
