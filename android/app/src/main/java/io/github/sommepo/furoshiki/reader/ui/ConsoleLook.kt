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
        "3ds" -> ThreeDsLook
        "ps1", "psvita" -> PlayStationLook // Vita3K Reader's look is Station Reader's
        "ps2" -> Ps2Look
        else -> PspLook
    }

    fun ground(density: Float): Drawable = look.ground(density)

    fun rule(density: Float): Drawable = look.rule(density)

    fun entry(density: Float, first: Boolean, head: View): Drawable? = look.entry(density, first, head)

    fun drawPointer(canvas: Canvas, paint: Paint, x: Float, y: Float, size: Float) =
        look.drawPointer(canvas, paint, x, y, size)

    /** Dresses one of the reader's pages ([ReaderScreen]) in the console's look: nothing here. */
    @Suppress("UNUSED_PARAMETER")
    fun dressScreen(activity: android.app.Activity, root: android.widget.LinearLayout,
                    toolbar: com.google.android.material.appbar.MaterialToolbar, back: () -> Unit) {
    }

    /** The theme of the reader's own pages ([ReaderScreen]), set before they are made. */
    fun theme(activity: android.app.Activity) {
        activity.setTheme(R.style.Theme_Reader)
    }
}
