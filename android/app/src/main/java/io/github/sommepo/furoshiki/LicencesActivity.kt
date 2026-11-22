// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.app.Activity
import android.graphics.Typeface
import android.os.Bundle
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.ListView
import android.widget.TextView

/**
 * Settings › About › Open-source licences: the notices (NOTICES.txt), the source offer, the GPL,
 * and every licence text the app carries (assets/licenses, written at build time by
 * home/tools/licences.py), each shown whole. A text is shown as its paragraphs in a list, so a
 * long one (a core's libraries) opens at once and the d-pad scrolls it.
 */
class LicencesActivity : Activity() {
    private class Entry(val title: String, val path: String)

    private lateinit var list: ListView
    private var entries: List<Entry> = emptyList()
    private var reading = false

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        list = ListView(this).apply {
            isFocusable = true
            itemsCanFocus = false
            dividerHeight = 0
            val pad = dp(16)
            setPadding(pad, pad, pad, pad)
            clipToPadding = false
        }
        setContentView(list)
        entries = listEntries()
        showList()
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()

    private fun listEntries(): List<Entry> {
        val found = mutableListOf<Entry>()
        val top = assets.list("")?.toSet().orEmpty()
        if ("NOTICES.txt" in top) found += Entry("Notices", "NOTICES.txt")
        if ("SOURCE.txt" in top) found += Entry("Source code", "SOURCE.txt")
        if ("COPYING.txt" in top) found += Entry("GNU General Public License, version 3", "COPYING.txt")
        for (name in assets.list("licenses").orEmpty().sorted()) {
            if (name == "third-party") continue
            found += Entry(name.substringBeforeLast('.').replace('-', ' '), "licenses/$name")
        }
        for (name in assets.list("licenses/third-party").orEmpty().sorted()) {
            val fork = name.substringBeforeLast('.')
            found += Entry("Libraries: ${EMULATORS[fork] ?: fork}", "licenses/third-party/$name")
        }
        return found
    }

    private fun row(text: String, size: Float, mono: Boolean): TextView = TextView(this).apply {
        this.text = text
        setTextSize(TypedValue.COMPLEX_UNIT_SP, size)
        if (mono) typeface = Typeface.MONOSPACE
        val pad = dp(10)
        setPadding(pad, pad, pad, pad)
    }

    private fun adapter(items: List<String>, size: Float, mono: Boolean) =
        object : ArrayAdapter<String>(this, 0, items) {
            override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
                val view = (convertView as? TextView) ?: row("", size, mono)
                view.text = getItem(position)
                return view
            }
        }

    private fun showList() {
        reading = false
        title = "Open-source licences"
        list.adapter = adapter(listOf("Open-source licences") + entries.map { it.title }, 18f, false)
        list.onItemClickListener = AdapterView.OnItemClickListener { _, _, position, _ ->
            if (position > 0) showText(entries[position - 1])
        }
        list.setSelection(0)
    }

    private fun showText(entry: Entry) {
        val text = runCatching { assets.open(entry.path).bufferedReader().use { it.readText() } }
            .getOrElse { "Couldn't open ${entry.path}" }
        // An HTML list (PCSX2's) shown as its text.
        val plain = if (entry.path.endsWith(".html")) {
            android.text.Html.fromHtml(text, android.text.Html.FROM_HTML_MODE_LEGACY).toString()
        } else text
        val paragraphs = plain.replace("\r\n", "\n").split(Regex("\n\\s*\n")).filter { it.isNotBlank() }
        reading = true
        title = entry.title
        list.adapter = adapter(listOf(entry.title) + paragraphs, 13f, true)
        list.onItemClickListener = null
        list.setSelection(0)
    }

    private companion object {
        /** Each emulator's list of libraries (third-party/<fork>.txt), by the console it plays. */
        val EMULATORS = mapOf(
            "station" to "PlayStation (DuckStation)", "armsx2" to "PlayStation 2 (ARMSX2)", "ppsspp" to "PSP (PPSSPP)",
            "vita3k" to "PS Vita (Vita3K)", "flycast" to "Dreamcast (Flycast)", "azahar" to "Nintendo 3DS (Azahar)",
            "melonds" to "Nintendo DS (melonDS)", "pcsx2" to "PlayStation 2 (PCSX2)",
        )
    }

    @Deprecated("Back goes from a text to the list, then out")
    override fun onBackPressed() {
        if (reading) showList() else super.onBackPressed()
    }
}
