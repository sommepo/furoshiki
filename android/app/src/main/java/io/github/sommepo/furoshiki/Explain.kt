// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.content.Context
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.RelativeSizeSpan
import android.text.style.StyleSpan
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.security.MessageDigest
import kotlin.concurrent.thread

/**
 * Explain: the line being read put into English and taken apart (the desktop's
 * `shared/reader/desktop/explain.*`, Furochibi's 解説). Asked of Furochibi's service, which the
 * paired save sync service names (`POST /api/explain`, `{text, before, game, game_id}`): there
 * is no Claude Code on a handheld to ask instead, so a device that knows no such service has no
 * Explain. An answer is kept by its line (`files/explanations.json`, the desktop's form: the
 * SHA-1 of the trimmed line, the oldest of more than 4000 dropped), so a line is asked once.
 */
object Explain {
    private const val KEPT = 4000

    private fun file(context: Context) = File(context.filesDir, "explanations.json")

    /** Whether there is anyone to ask. */
    fun offered(context: Context): Boolean = Services.of(context, "furochibi").isNotEmpty()

    fun keyOf(line: String): String =
        MessageDigest.getInstance("SHA-1").digest(line.trim().toByteArray()).joinToString("") { "%02x".format(it) }

    /** [answer] kept for [line] in what was [kept] before, the oldest dropped past [most]. */
    fun keep(kept: JSONObject, line: String, answer: JSONObject, most: Int = KEPT): JSONObject {
        val key = keyOf(line)
        val order = kept.optJSONArray("order") ?: JSONArray()
        val answers = kept.optJSONObject("answers") ?: JSONObject()
        if (!answers.has(key)) order.put(key)
        answers.put(key, answer)
        val drop = order.length() - most
        val next = JSONArray()
        for (i in 0 until order.length()) {
            if (i < drop) answers.remove(order.getString(i)) else next.put(order.getString(i))
        }
        return JSONObject().put("order", next).put("answers", answers)
    }

    fun kept(context: Context, line: String): JSONObject? =
        runCatching { JSONObject(file(context).readText()).optJSONObject("answers")?.optJSONObject(keyOf(line)) }.getOrNull()

    /** The answer as it is read: the English, the note, then each part with its reading, sense and grammar. */
    fun text(answer: JSONObject, muted: Int): CharSequence {
        val out = SpannableStringBuilder()
        fun add(text: String, vararg spans: Any) {
            val from = out.length
            out.append(text)
            for (span in spans) out.setSpan(span, from, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        add(answer.optString("translation").trim() + "\n", RelativeSizeSpan(1.12f))
        answer.optString("note").trim().takeIf { it.isNotEmpty() }?.let { add("\n$it\n", ForegroundColorSpan(muted)) }
        val segments = answer.optJSONArray("segments") ?: JSONArray()
        for (i in 0 until segments.length()) {
            val part = segments.getJSONObject(i)
            add("\n" + part.optString("text"), StyleSpan(android.graphics.Typeface.BOLD), RelativeSizeSpan(1.12f))
            part.optString("reading").takeIf { it.isNotEmpty() && it != part.optString("text") }?.let { add("  $it", ForegroundColorSpan(muted)) }
            add("\n" + part.optString("gloss") + "\n")
            val grammar = part.optJSONArray("grammar") ?: JSONArray()
            for (g in 0 until grammar.length()) add("· " + grammar.optString(g) + "\n", ForegroundColorSpan(muted), RelativeSizeSpan(.92f))
        }
        return out
    }

    /**
     * Explains [line] over the game: what is kept at once, else asked. [shut] when the box closes
     * (the game stands still meanwhile).
     */
    fun show(activity: Activity, line: String, before: List<String>, game: String, gameId: String, shut: () -> Unit) {
        val themed = android.view.ContextThemeWrapper(activity, R.style.Theme_Reader)
        val density = activity.resources.displayMetrics.density
        val muted = 0xFF8A93B5.toInt()
        val body = TextView(themed).apply {
            textSize = 15f
            setLineSpacing(0f, 1.2f)
            val pad = (22 * density).toInt()
            setPadding(pad, pad / 2, pad, pad / 2)
            setTextIsSelectable(true)
        }
        val dialog = AlertDialog.Builder(themed)
            .setTitle(line.trim())
            .setView(ScrollView(themed).apply { addView(body) })
            .setPositiveButton(R.string.game_menu_resume, null)
            .setNeutralButton(R.string.explain_again, null)
            .create()
        var open = true
        dialog.setOnDismissListener {
            open = false
            shut()
        }
        fun ask(again: Boolean) {
            val had = if (again) null else kept(activity, line)
            if (had != null) {
                body.text = text(had, muted)
                return
            }
            body.text = activity.getString(R.string.explain_asking)
            val app = activity.applicationContext
            thread(name = "Explain") {
                val answer = runCatching {
                    val at = Services.of(app, "furochibi").ifEmpty { Services.ask(app); Services.of(app, "furochibi") }
                    if (at.isEmpty()) error(activity.getString(R.string.explain_no_service))
                    val sent = JSONObject().put("text", line.trim()).put("before", JSONArray(before.takeLast(3)))
                        .put("game", game).put("game_id", gameId)
                    val said = JSONObject(Http.post("$at/api/explain", sent.toString(), timeoutMs = 130_000))
                    synchronized(this) {
                        val all = runCatching { JSONObject(file(app).readText()) }.getOrDefault(JSONObject())
                        val next = File(file(app).path + ".new")
                        next.writeText(keep(all, line, said).toString())
                        next.renameTo(file(app))
                    }
                    said
                }
                activity.runOnUiThread {
                    if (!open) return@runOnUiThread
                    body.text = answer.fold({ text(it, muted) }, { activity.getString(R.string.explain_failed, it.message ?: "") })
                }
            }
        }
        dialog.show()
        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener { ask(again = true) }
        ask(again = false)
    }
}
