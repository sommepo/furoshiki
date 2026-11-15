// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import android.content.Context
import java.io.File
import org.json.JSONObject

/** The engines a game's text is read with. */
enum class OcrEngine(val id: String) {
    /** An OCR service on another machine: Furochibi's on a home server, or the OCR helper on a computer. */
    SERVICE("service"),

    /** meikiocr on this device ([MeikiOcr]), with nothing sent anywhere. */
    DEVICE("device");

    companion object {
        fun of(id: String): OcrEngine? = values().firstOrNull { it.id == id }
    }
}

/**
 * The OCR settings of the device, in a file of the app's (`files/ocr.json`) that both processes
 * read fresh: the page's writes it, a game's reads it as it starts. [engine] is "" for the
 * default, else an [OcrEngine]'s id; [address] and [token] are the service's as typed (empty:
 * the one the paired home server names, without a token).
 */
data class OcrSettings(val engine: String = "", val address: String = "", val token: String = "") {
    fun toJson(): String = JSONObject().put("engine", engine).put("address", address).put("token", token).toString()

    companion object {
        private fun file(context: Context) = File(context.filesDir, "ocr.json")

        fun parse(text: String): OcrSettings {
            val root = runCatching { JSONObject(text) }.getOrNull() ?: return OcrSettings()
            val engine = root.optString("engine").takeIf { OcrEngine.of(it) != null }.orEmpty()
            return OcrSettings(engine, clean(root.optString("address")), root.optString("token").trim())
        }

        fun read(context: Context): OcrSettings = parse(runCatching { file(context).readText() }.getOrDefault(""))

        fun write(context: Context, settings: OcrSettings) {
            val next = File(file(context).path + ".new")
            next.writeText(settings.toJson())
            if (!next.renameTo(file(context))) file(context).writeText(settings.toJson())
        }

        /** An address as typed made one to ask: a scheme where none was given, no slash at its end. */
        fun clean(address: String): String {
            val trimmed = address.trim().trimEnd('/')
            if (trimmed.isEmpty()) return ""
            return if ("://" in trimmed) trimmed else "http://$trimmed"
        }

        /**
         * The engine a game is read with. Chosen in Settings, it is that one, as long as it can be
         * had (no service known: the device's; no models in this build: the service). By default a
         * build of the maintainer's own line ([dev]) reads with the service where one is known, as
         * its home server's engines are the best; a published build reads on the device, which
         * needs nothing else. Null when there is neither.
         */
        fun choose(chosen: String, dev: Boolean, serviceKnown: Boolean, deviceAvailable: Boolean): OcrEngine? {
            val wanted = OcrEngine.of(chosen) ?: if (dev && serviceKnown) OcrEngine.SERVICE else OcrEngine.DEVICE
            return when {
                wanted == OcrEngine.SERVICE && serviceKnown -> OcrEngine.SERVICE
                wanted == OcrEngine.DEVICE && deviceAvailable -> OcrEngine.DEVICE
                deviceAvailable -> OcrEngine.DEVICE
                serviceKnown -> OcrEngine.SERVICE
                else -> null
            }
        }

        /** Whether this build is the maintainer's own line: a published one has this line cut. */
        val dev: Boolean = run {
            var own = false
            own
        }
    }
}
