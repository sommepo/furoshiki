// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.annotation.SuppressLint
import android.app.Activity
import android.app.ActivityOptions
import android.os.Bundle
import android.view.KeyEvent
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.webkit.WebViewClient
import org.json.JSONObject

/**
 * The second display while Furoshiki is the home screen ([HomeScreen]): Android's home of that
 * display (`SECONDARY_HOME`), so it is there from the device's start and behind everything
 * else that display shows. With the shelf in front it is the title in hand and its keys, told
 * by [HomeActivity] as its Presentation was; with anything else in front (another app, a game
 * that leaves the display alone) it is the clock, the battery, the volume and the brightness,
 * and the key back to the shelf. A game's trackpad or lower screen lies over it.
 *
 * A touch here gives this display the keys, as Android has it (a window that refuses the focus
 * leaves the device waiting for one): what the controller says then is handed to the page.
 */
class LowerActivity : Activity() {
    private lateinit var view: WebView
    private var loaded = false

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        view = WebView(this)
        view.setBackgroundColor(android.graphics.Color.BLACK)
        // No grey veil once a key is pressed (Android's default focus highlight): HomeActivity.
        view.defaultFocusHighlightEnabled = false
        view.settings.javaScriptEnabled = true
        view.settings.allowFileAccess = true
        view.isVerticalScrollBarEnabled = false
        view.addJavascriptInterface(Bridge(), "FuroshikiHost")
        view.webViewClient = object : WebViewClient() {
            override fun onPageFinished(page: WebView, url: String) {
                loaded = true
                tell()
            }
        }
        setContentView(view)
        view.loadUrl("file:///android_asset/front/index.html#lower")
        live = this
        // The page learns that the keys are here now, and lays itself out as all shelf.
        HomeActivity.front?.lowerCame()
    }

    override fun onResume() {
        super.onResume()
        window.insetsController?.let {
            it.hide(WindowInsets.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        tell()
    }

    /**
     * The Home key while this display has the keys (it was touched last): Android brings this
     * display's home, which is here already. What was meant is the shelf.
     */
    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        if (HomeActivity.front == null) {
            runCatching { startActivity(HomeScreen.home(), ActivityOptions.makeBasic().setLaunchDisplayId(0).toBundle()) }
        } else {
            HomeActivity.front?.remote("home")
        }
    }

    /**
     * A touch here took the keys from the first display. With the shelf there they are given
     * straight back (the page is asked to the front, where it is already), so the controller and
     * the Home key go on working the shelf. With another app there nothing may be asked of it:
     * SHELF is the way back.
     */
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus || HomeActivity.front == null) return
        view.postDelayed({
            if (HomeActivity.front == null || !hasWindowFocus()) return@postDelayed
            runCatching {
                startActivity(
                    android.content.Intent(this, HomeActivity::class.java).setAction(HomeScreen.ACTION_FOCUS)
                        .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_NO_ANIMATION),
                    ActivityOptions.makeBasic().setLaunchDisplayId(0).toBundle()
                )
            }
        }, 350)
    }

    override fun onDestroy() {
        if (live === this) live = null
        super.onDestroy()
    }

    /** Shows what the page last said, and whether the page is in front. */
    fun tell() {
        if (!loaded) return
        val shown = if (lastShown.isEmpty()) "null" else JSONObject.quote(lastShown)
        view.evaluateJavascript("if (window.Lower) Lower.tell($shown, ${HomeActivity.front == null})", null)
    }

    // A key that comes here after all goes where it would have gone.
    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        HomeActivity.front?.dispatchKeyEvent(event) ?: super.dispatchKeyEvent(event)

    // The sticks, the triggers and a d-pad that is a hat come as motion, not keys: they go to the
    // shelf too, so that a touch here never leaves the controller working only this display (the
    // maintainer, 2026-11-17: "i need the menu experience to be one entire thing").
    override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean {
        val joystick = event.source and android.view.InputDevice.SOURCE_CLASS_JOYSTICK != 0
        return (if (joystick) HomeActivity.front?.dispatchGenericMotionEvent(event) else null)
            ?: super.dispatchGenericMotionEvent(event)
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() = Unit

    private inner class Bridge {
        /** What a finger did on the keys of the title in hand: the page's to do. */
        @JavascriptInterface
        fun remote(what: String) = runOnUiThread { HomeActivity.front?.remote(what) }

        @JavascriptInterface
        fun status(): String = Device.status(this@LowerActivity).put("dim", Device.mayDim(this@LowerActivity)).toString()

        @JavascriptInterface
        fun volume(direction: Int): Int = Device.stepVolume(this@LowerActivity, direction)

        @JavascriptInterface
        fun brightness(direction: Int): Int = Device.stepBrightness(this@LowerActivity, direction)

        /** The shelf, on the first display. */
        @JavascriptInterface
        fun home() = runOnUiThread {
            runCatching { startActivity(HomeScreen.home(), ActivityOptions.makeBasic().setLaunchDisplayId(0).toBundle()) }
        }
    }

    companion object {
        /** The one on the second display, while there is one. */
        @Volatile
        var live: LowerActivity? = null

        /** What the page last said the second display shows. */
        @Volatile
        var lastShown = ""
    }
}
