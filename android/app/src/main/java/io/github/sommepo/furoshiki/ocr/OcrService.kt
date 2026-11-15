// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.ocr

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/**
 * The OCR service, asked to read one picture: `POST <address>/api/ocr/read` with the picture as
 * the body (Furochibi's service and the OCR helper answer it alike; furochibi/README.md). Never
 * on the main thread.
 */
class OcrService(private val address: String, private val token: String = "", private val engine: String = "") {
    /** What the service read in [picture] (a JPEG or PNG, [type] its media type). Throws when it could not. */
    fun read(picture: ByteArray, type: String = "image/jpeg"): OcrRead {
        val query = if (engine.isNotEmpty()) "?engine=" + URLEncoder.encode(engine, "UTF-8") else ""
        val connection = URL("$address/api/ocr/read$query").openConnection() as HttpURLConnection
        connection.connectTimeout = 4000
        connection.readTimeout = 20000
        connection.requestMethod = "POST"
        connection.doOutput = true
        connection.setRequestProperty("Content-Type", type)
        if (token.isNotEmpty()) connection.setRequestProperty("Authorization", "Bearer $token")
        connection.setFixedLengthStreamingMode(picture.size)
        try {
            connection.outputStream.use { it.write(picture) }
            val code = connection.responseCode
            val body = (if (code in 200..299) connection.inputStream else connection.errorStream)
                ?.bufferedReader()?.use { it.readText() }.orEmpty()
            if (code !in 200..299) {
                val said = runCatching { org.json.JSONObject(body).optString("error") }.getOrDefault("").ifEmpty { body.take(120) }
                throw IOException("HTTP $code" + if (said.isNotEmpty()) ": $said" else "")
            }
            return OcrRead.fromJson(body)
        } finally {
            connection.disconnect()
        }
    }
}
