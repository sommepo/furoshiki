// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import androidx.test.core.app.ApplicationProvider
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.GraphicsMode

/** A spine drawn in its console's look where none is found (Spines), as the desktop draws one. */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class SpinesTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()

    @Test
    fun aGameWithoutASpineGetsOneInItsConsolesLook() {
        val games = listOf(
            Triple("ps2", "SLPM-66302", "CLANNAD"),
            Triple("ps2", "SLPS-25088", "ファイナルファンタジーX インターナショナル"),
            Triple("psp", "ULJM-05887", "STEINS;GATE (Japan)"),
            Triple("ps1", "SLPS-01645", "弟切草 蘇生篇"),
            Triple("dc", "HDR-0164", "シェンムーII"),
            Triple("3ds", "0004000000088A00", "真・女神転生IV"),
            Triple("psvita", "PCSG00488", "英雄伝説 空の軌跡 FC Evolution"),
            Triple("switch", "0100F160092D2000", "STEINS;GATE ELITE"),
        )
        val drawn = games.map { (key, id, title) -> Spines.drawn(context, key, id, title) }
        drawn.forEach { file ->
            assertNotNull(file)
            val picture = BitmapFactory.decodeFile(file!!.path)
            assertEquals(1000, picture.height)
        }
        // Made once: the same file while the title stays the same.
        val again = Spines.drawn(context, "ps2", "SLPM-66302", "CLANNAD")
        assertEquals(drawn[0]!!.lastModified(), again!!.lastModified())
        assertNull(Spines.drawn(context, "nds", "AWIJ", "ウィッシュルーム"))

        // Side by side, for whoever looks (READER_PREVIEW_DIR).
        val dir = System.getenv("READER_PREVIEW_DIR") ?: return
        val pictures = drawn.map { BitmapFactory.decodeFile(it!!.path) }
        val sheet = Bitmap.createBitmap(pictures.sumOf { it.width + 24 } + 24, 1048, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(sheet)
        canvas.drawColor(Color.rgb(0x10, 0x1a, 0x3a))
        var x = 24f
        for (picture in pictures) {
            canvas.drawBitmap(picture, x, 24f, null)
            x += picture.width + 24
        }
        File(dir).mkdirs()
        File(dir, "spines-drawn.png").outputStream().use { sheet.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
}
