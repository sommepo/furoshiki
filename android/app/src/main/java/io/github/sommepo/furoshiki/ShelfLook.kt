// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import io.github.sommepo.furoshiki.reader.ReaderSettings
import org.json.JSONObject

/**
 * The home page's look for what is drawn outside the page: the menus over a game ([ShelfMenu])
 * and the reader's own screens (reader/ui/ConsoleLook.dressScreen). The page's themes are the
 * desktop's files (`assets/front/themes/<key>.json`: Kon, Mono, Paper, Solarized), six colours
 * each; the one chosen on the page is kept for the reader as it leaves the front
 * (HomeActivity.keepShelfTheme, [ReaderSettings.KEY_SHELF_THEME]). The faces are the page's too
 * (`assets/front/fonts`).
 */
object ShelfLook {
    class Look(val key: String, val ground: Int, val raised: Int, val ink: Int, val muted: Int, val accent: Int, val rule: Int) {
        /** Whether the ground is a light one (Paper): the system's bars take dark marks then. */
        val light: Boolean get() = Color.luminance(ground) > 0.5f
    }

    /** Kon, the page's first theme: what is used where a theme's file cannot be read. */
    val KON = Look("kon", 0xFF101A3A.toInt(), 0xFF18244D.toInt(), 0xFFECE5D3.toInt(), 0xFF8A93B5.toInt(), 0xFFD9472B.toInt(), 0x3DECE5D3)

    /** "#101a3a" or "rgba(236,229,211,0.24)" as a colour, or null. */
    fun colour(text: String): Int? {
        val value = text.trim()
        if (value.startsWith("#")) return runCatching { Color.parseColor(value) }.getOrNull()
        val parts = Regex("rgba?\\(([^)]*)\\)").find(value)?.groupValues?.get(1)?.split(',')?.map { it.trim() } ?: return null
        if (parts.size < 3) return null
        val rgb = parts.take(3).map { it.toFloatOrNull()?.toInt()?.coerceIn(0, 255) ?: return null }
        val alpha = ((parts.getOrNull(3)?.toFloatOrNull() ?: 1f).coerceIn(0f, 1f) * 255).toInt()
        return Color.argb(alpha, rgb[0], rgb[1], rgb[2])
    }

    /** A theme's file as a look; what it lacks is Kon's. */
    fun read(key: String, json: String): Look {
        val file = runCatching { JSONObject(json) }.getOrNull() ?: return KON
        fun of(name: String, otherwise: Int) = colour(file.optString(name)) ?: otherwise
        return Look(key, of("ground", KON.ground), of("raised", KON.raised), of("ink", KON.ink), of("muted", KON.muted), of("accent", KON.accent), of("rule", KON.rule))
    }

    /** One colour laid over another, for where a see-through line has to be a solid one. */
    fun over(top: Int, ground: Int): Int {
        val a = Color.alpha(top) / 255f
        fun mix(t: Int, g: Int) = (t * a + g * (1 - a)).toInt()
        return Color.rgb(mix(Color.red(top), Color.red(ground)), mix(Color.green(top), Color.green(ground)), mix(Color.blue(top), Color.blue(ground)))
    }

    /** The key of the theme the page has now ("kon", "mono", "paper", "solarized"). */
    fun key(context: Context): String =
        runCatching { ReaderSettings.fresh(context).getString(ReaderSettings.KEY_SHELF_THEME, null) }.getOrNull()
            ?.takeIf { it.matches(Regex("[a-z0-9-]{1,32}")) } ?: "kon"

    fun of(context: Context): Look {
        val key = key(context)
        val json = runCatching { context.assets.open("front/themes/$key.json").bufferedReader().use { it.readText() } }.getOrNull()
            ?: return KON
        return read(key, json)
    }

    private val faces = HashMap<String, Typeface>()

    private fun face(context: Context, file: String, otherwise: Typeface): Typeface = synchronized(faces) {
        faces.getOrPut(file) {
            runCatching { Typeface.createFromAsset(context.applicationContext.assets, "front/fonts/$file") }.getOrDefault(otherwise)
        }
    }

    /** The page's text face (Zen Kaku Gothic New), medium or bold. */
    fun text(context: Context, bold: Boolean = false): Typeface =
        face(context, if (bold) "ZenKakuGothicNew-Bold.ttf" else "ZenKakuGothicNew-Medium.ttf", if (bold) Typeface.DEFAULT_BOLD else Typeface.DEFAULT)

    /** The page's face for small capitals and figures (IBM Plex Mono). */
    fun mono(context: Context): Typeface = face(context, "IBMPlexMono-Medium.ttf", Typeface.MONOSPACE)
}
