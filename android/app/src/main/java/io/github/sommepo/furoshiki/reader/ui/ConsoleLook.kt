// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.reader.ui

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.drawable.Drawable
import android.view.View
import io.github.sommepo.furoshiki.R
import io.github.sommepo.furoshiki.reader.Console

/**
 * The console's look for the word popup, the question about a card's audio and the pointer, in
 * the place of a reader app's file of this name: each console's is its fork's drawing, chosen by
 * the console this process plays. A new console adds its look here and in [Console].
 */
object ConsoleLook {
    interface Look {
        fun ground(density: Float): Drawable
        fun rule(density: Float): Drawable
        fun entry(density: Float, first: Boolean, head: View): Drawable?
        fun drawPointer(canvas: Canvas, paint: Paint, x: Float, y: Float, size: Float)
    }

    private val look: Look get() = when (Console.KEY) {
        "dc" -> DreamcastLook
        "3ds", "nds" -> ThreeDsLook // melonDS Reader's look is Azahar Reader's
        "ps1", "psvita" -> PlayStationLook // Vita3K Reader's look is Station Reader's
        "ps2" -> Ps2Look
        else -> PspLook
    }

    fun ground(density: Float): Drawable = look.ground(density)

    fun rule(density: Float): Drawable = look.rule(density)

    fun entry(density: Float, first: Boolean, head: View): Drawable? = look.entry(density, first, head)

    fun drawPointer(canvas: Canvas, paint: Paint, x: Float, y: Float, size: Float) =
        look.drawPointer(canvas, paint, x, y, size)

    /** The screens that are not a page of the settings: their header is their own name alone. */
    private val OWN_HEADER = setOf("ReaderStatsActivity", "ReaderBacklogActivity", "ReaderStartActivity")

    /**
     * Dresses one of the reader's screens ([ReaderScreen]: the settings' pages, Dictionaries, Anki
     * setup, Statistics, Game scripts, the CRT filter) as the home page is drawn, so that a
     * setting does not change its look one tap in (the maintainer, 2026-11-20): the page's faces
     * (its text face for names, its small tracked capitals for values, buttons and the header),
     * the theme's colours ([theme]), the header as the page's "‹ Settings · Reading" with a
     * hairline under it. The rows of the settings themselves are layout/shelf_preference. Views
     * that come later (a list's rows) are dressed as they are laid out.
     */
    @Suppress("UNUSED_PARAMETER")
    fun dressScreen(activity: android.app.Activity, root: android.widget.LinearLayout,
                    toolbar: com.google.android.material.appbar.MaterialToolbar, back: () -> Unit) {
        val look = io.github.sommepo.furoshiki.ShelfLook.of(activity)
        val text = io.github.sommepo.furoshiki.ShelfLook.text(activity)
        val mono = io.github.sommepo.furoshiki.ShelfLook.mono(activity)
        root.setBackgroundColor(look.ground)
        activity.window.statusBarColor = look.ground
        activity.window.navigationBarColor = look.ground
        // Dark marks on a light ground (Paper). Once the window has its frame: it has none yet here.
        val marks = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
        root.post { runCatching { activity.window.decorView.windowInsetsController?.setSystemBarsAppearance(if (look.light) marks else 0, marks) } }
        toolbar.setBackgroundColor(look.ground)
        toolbar.setTitleTextColor(look.ink)
        toolbar.setSubtitleTextColor(look.muted)
        toolbar.navigationIcon?.mutate()?.setTint(look.muted)
        // The page's rule under its header.
        val density = activity.resources.displayMetrics.density
        root.addView(
            android.view.View(activity).apply { setBackgroundColor(look.rule) },
            minOf(1, root.childCount),
            android.widget.LinearLayout.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, 1).apply {
                marginStart = (20 * density).toInt()
                marginEnd = (20 * density).toInt()
            }
        )
        // "Settings · Reading" for a page of the settings; "Settings" itself and the screens that
        // are no page of them keep their own name.
        val prefix = if (activity.javaClass.simpleName in OWN_HEADER) "" else activity.getString(R.string.reader_title)
        val header = object : android.text.method.TransformationMethod {
            override fun getTransformation(source: CharSequence?, view: View?): CharSequence {
                val name = source?.toString().orEmpty()
                return (if (prefix.isEmpty() || name == prefix || name.isEmpty()) name else "$prefix · $name").uppercase()
            }

            override fun onFocusChanged(view: View?, sourceText: CharSequence?, focused: Boolean, direction: Int, previouslyFocusedRect: android.graphics.Rect?) = Unit
        }
        fun dress(view: View, inToolbar: Boolean) {
            if (view is android.view.ViewGroup) {
                val bar = inToolbar || view === toolbar
                for (i in 0 until view.childCount) dress(view.getChildAt(i), bar)
            }
            if (view !is android.widget.TextView || view.getTag(R.id.shelf_dressed) != null) return
            view.setTag(R.id.shelf_dressed, true)
            when {
                // The header: the page's small tracked capitals; the subtitle a line of them, muted.
                inToolbar -> {
                    view.typeface = mono
                    view.letterSpacing = .14f
                    view.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12f)
                    view.transformationMethod = if (view.text?.toString() == toolbar.subtitle?.toString() && toolbar.subtitle != null) {
                        android.text.method.SingleLineTransformationMethod.getInstance().let { _ ->
                            object : android.text.method.TransformationMethod by header {
                                override fun getTransformation(source: CharSequence?, v: View?) = source?.toString().orEmpty().uppercase()
                            }
                        }
                    } else header
                }
                // What a row is set to, a slider's value, a key: small tracked capitals.
                view.id == android.R.id.summary || view.id == R.id.seekbar_value -> view.typeface = mono
                view is android.widget.Button && view !is android.widget.CompoundButton -> {
                    view.typeface = mono
                    view.letterSpacing = .1f
                    view.isAllCaps = true
                    view.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 13f)
                }
                view is android.widget.EditText -> view.typeface = text
                else -> view.typeface = text
            }
        }
        root.viewTreeObserver.addOnGlobalLayoutListener { dress(root, false) }
        dress(root, false)
    }

    /** The key of the page's theme, and its colours over the reader's own ([dressScreen] has the rest). */
    private val OVERLAYS = mapOf(
        "kon" to R.style.ThemeOverlay_Reader_Kon, "mono" to R.style.ThemeOverlay_Reader_Mono,
        "paper" to R.style.ThemeOverlay_Reader_Paper, "solarized" to R.style.ThemeOverlay_Reader_Solarized,
    )

    /**
     * The theme of the reader's own pages ([ReaderScreen]), set before they are made: the
     * reader's, in the colours of the theme the home page has (Kon, Mono, Paper, Solarized).
     */
    fun theme(activity: android.app.Activity) {
        activity.setTheme(R.style.Theme_Reader)
        OVERLAYS[io.github.sommepo.furoshiki.ShelfLook.key(activity)]?.let { activity.theme.applyStyle(it, true) }
    }
}
