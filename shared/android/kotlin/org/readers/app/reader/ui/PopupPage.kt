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
        /**
         * On a device the popup keeps the sizes it had before themes drew it (the maintainer,
         * 2026-11-17: "popup is now too big by default. frequencies too big, labels too big. the
         * size of the one before was fine"): every theme's colours and lines, the base sizes,
         * small labels. And every look compact (the maintainer, the same day: "frequencies not in
         * pills, a bit smaller, way less empty space"): tight spacing, frequencies as plain words.
         * The card's keys stay where the popup's corner setting puts them.
         * Applied after the theme; the user's own CSS still comes after it.
         */
        const val DEVICE_CSS = """
.popup { font-size: calc(13px * var(--scale)); line-height: 1.35;
  padding: calc(5px * var(--scale)) calc(8px * var(--scale)) calc(5px * var(--scale)) calc(8px * var(--scale)); }
.popup .tabs { margin-bottom: calc(2px * var(--scale)); }
.popup .entry { padding: calc(2px * var(--scale)) 0 calc(3px * var(--scale)); margin: 0; }
.popup .entry + .entry { padding-top: calc(4px * var(--scale)); margin-top: 0; }
.popup .head { gap: 0 0.5em; }
.popup .headword { font-size: calc(17px * var(--scale)); line-height: 1.45; }
.popup .tag, .popup .tag.frequency, .popup .reasons, .popup .pitch, .popup .lookups, .popup .dictionary-name,
.popup .romaji, .popup .more, .popup .reading, .popup .number {
  font-size: calc(9.5px * var(--scale)); letter-spacing: 0.04em; }
.popup .lookups { margin: 0; }
.popup .tags { gap: 0 0.5em; }
.popup .tag.frequency, .popup .tag.frequency .tag-name, .popup .tag.frequency .tag-value {
  background: none; border: none; box-shadow: none; padding: 0; border-radius: 0; }
.popup .tag.frequency .tag-value { letter-spacing: 0.02em; }
.popup .dictionary-name { margin: calc(2px * var(--scale)) 0 0; padding: 0; border: none; }
.popup .senses { margin: 0; }
.popup .sense { margin: 0; padding-top: 0; padding-bottom: 0; line-height: 1.35; }
.popup .forms { margin-top: calc(1px * var(--scale)); }
.popup .card-buttons { margin-top: calc(2px * var(--scale)); gap: calc(4px * var(--scale)); }
.popup .card { width: calc(22px * var(--scale)); height: calc(22px * var(--scale)); }
.popup .name-card { margin: calc(2px * var(--scale)) 0; gap: calc(8px * var(--scale)); }
.popup .name-card .portrait { width: calc(48px * var(--scale)); }
.popup[data-portrait="large"] .name-card .portrait { width: calc(72px * var(--scale)); }
.popup .who-name { font-size: calc(15px * var(--scale)); line-height: 1.6; }
.popup .about { margin-top: calc(2px * var(--scale)); font-size: calc(12px * var(--scale)); line-height: 1.35; }
.popup .also { margin-top: calc(2px * var(--scale)); }
"""

        private const val PAGE = "file:///android_asset/reader/popup/popup.html"
        private const val THEMES = "reader/popup/themes/"

        /** A word with its grammar entry: enough to see a theme, its size, its tabs. */
        val SAMPLE: JSONArray = JSONArray(
            """
            [{"expression":"風呂敷","reading":"ふろしき","reasons":[],"pitches":["[0]"],
              "frequencies":[{"dictionary":"JPDB","value":"18200"}],"unknown":true,"state":"add","forms":["ふろしき"],
              "senses":[
                {"heading":"JMdict","dictionary":"JMdict","kind":"word","number":1,"tags":["n"],"glosses":["wrapping cloth","cloth wrapper (traditionally used to carry clothes, gifts, or books)"],"notes":""},
                {"dictionary":"JMdict","kind":"word","number":2,"tags":[],"glosses":["traditional Japanese square cloth folded around items for transport"],"notes":""},
                {"heading":"大辞林","dictionary":"大辞林","kind":"word","number":1,"tags":[],"glosses":[],"notes":"",
                 "content":["物を包んで持ち運ぶための正方形の布。"]}
              ]},
             {"expression":"風呂","reading":"ふろ","reasons":[],"pitches":["[2]"],
              "frequencies":[{"dictionary":"JPDB","value":"3400"}],"unknown":false,"state":"inDeck","forms":[],
              "senses":[{"heading":"JMdict","dictionary":"JMdict","kind":"word","number":1,"tags":["n"],"glosses":["bath","bathhouse"],"notes":""}]},
             {"expression":"風","kanji":{"character":"風","dictionary":"KANJIDIC","meanings":["wind","air","style","manner"],"onyomi":["フウ","フ"],"kunyomi":["かぜ","かざ-"],"stats":{"strokes":9,"grade":2,"jlpt":4,"freq":558},"parts":["几","虫"]}},
             {"expression":"呂","kanji":{"character":"呂","dictionary":"KANJIDIC","meanings":["spine","backbone"],"onyomi":["ロ","リョ"],"kunyomi":["せぼね"],"stats":{"strokes":7,"grade":8,"jlpt":1,"freq":1978},"parts":["口","ノ"]}},
             {"expression":"敷","kanji":{"character":"敷","dictionary":"KANJIDIC","meanings":["spread","pave","sit","promulgate"],"onyomi":["フ"],"kunyomi":["し.く","-し.き"],"stats":{"strokes":15,"grade":8,"jlpt":1,"freq":1246},"parts":["甫","攵"]}}]
            """.trimIndent()
        )

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

        /**
         * A popup theme's colours for the session line over a game ([StatsLineView.Colours]): the
         * page's defaults (base.css), then the theme's own variables (the first of each), `var()`
         * followed; the console's colours for "console" and "classic". Null where none can be read.
         */
        fun themeColours(context: Context, theme: String): StatsLineView.Colours? = runCatching {
            val vars = HashMap<String, String>()
            fun read(css: String) {
                val seen = HashSet<String>()
                Regex("--([a-z-]+)\\s*:\\s*([^;\\}]+)").findAll(css).forEach { match ->
                    if (seen.add(match.groupValues[1])) vars[match.groupValues[1]] = match.groupValues[2].trim()
                }
            }
            read(context.assets.open("reader/popup/base.css").bufferedReader().use { it.readText() })
            read(if (theme == "console" || theme == "classic") consoleCss(context) else themeCss(context, theme))
            val rgba = Regex("rgba?\\(\\s*([\\d.]+)\\s*,\\s*([\\d.]+)\\s*,\\s*([\\d.]+)\\s*(?:,\\s*([\\d.]+)\\s*)?\\)")
            val reference = Regex("var\\(--([a-z-]+)\\)")
            fun colour(name: String, depth: Int = 0): Int? {
                val value = vars[name] ?: return null
                reference.matchEntire(value)?.let { return if (depth < 4) colour(it.groupValues[1], depth + 1) else null }
                rgba.matchEntire(value)?.let {
                    val alpha = (it.groupValues[4].ifEmpty { "1" }.toFloat() * 255).toInt().coerceIn(0, 255)
                    return Color.argb(alpha, it.groupValues[1].toFloat().toInt(), it.groupValues[2].toFloat().toInt(),
                        it.groupValues[3].toFloat().toInt())
                }
                return runCatching { Color.parseColor(value) }.getOrNull()
            }
            val ground = colour("raised") ?: colour("ground") ?: return@runCatching null
            val text = colour("text") ?: return@runCatching null
            StatsLineView.Colours(ground, text, colour("dim") ?: text, colour("accent") ?: text,
                colour("rule") ?: colour("edge") ?: colour("dim") ?: text)
        }.getOrNull()

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
                  scale: Int, console: String, game: String, labels: JSONObject, look: JSONObject? = null): JSONObject = JSONObject().apply {
            put("entries", entries)
            put("corner", corner)
            put("duplicates", duplicates)
            put("markUnknown", markUnknown)
            put("scale", scale)
            if (look != null) put("look", look)
            put("console", console)
            put("game", game)
            put("labels", labels)
            if (message != null) put("message", message)
        }
    }
}
