// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.app.AlertDialog
import android.content.Context
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.LinearLayout
import android.widget.ListView
import androidx.preference.ListPreference
import org.json.JSONObject
import org.readers.app.R
import org.readers.app.reader.Console
import org.readers.app.reader.ReaderSettings

/**
 * The popup theme chosen with the popup in front of you (the maintainer, 2026-11-24: "Lots of popup
 * themes but without a preview of them on the popup page ... impossible to know the difference"):
 * in place of the list's plain radio buttons, a dialog with the popup drawn above the themes, a
 * word and its grammar entry in the theme in hand. A tap on a theme, or a controller's d-pad
 * moving to it, draws it there at once; OK keeps it, Cancel leaves the setting as it was.
 */
object ThemeChooser {
    fun show(context: Context, preference: ListPreference) {
        val settings = ReaderSettings(context)
        val names = preference.entries ?: return
        val values = preference.entryValues ?: return
        val density = context.resources.displayMetrics.density
        fun dp(value: Int) = (value * density).toInt()
        var picked = values.indexOfFirst { it.toString() == preference.value }.coerceAtLeast(0)
        // As tall as the room allows beside the list: on a phone held sideways, less.
        val room = context.resources.displayMetrics.heightPixels
        val previewTall = minOf(dp(230), room * 38 / 100)
        val listTall = minOf(dp(320), room * 34 / 100)

        val box = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(8), dp(16), 0)
        }
        val page = PopupPage(context) { }
        box.addView(page, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, previewTall))
        val labels = JSONObject()
            .put("add", context.getString(R.string.reader_lookup_add))
            .put("addAudio", context.getString(R.string.reader_lookup_add_audio))
            .put("book", context.getString(R.string.reader_lookup_in_deck))
            .put("more", context.getString(R.string.reader_lookup_more))
        var drawn: String? = null
        fun draw(index: Int) {
            val theme = settings.themeOf(values[index].toString())
            if (theme == drawn) return
            drawn = theme
            page.setStyle("theme", if (theme == "console" || theme == "classic") PopupPage.consoleCss(context) else PopupPage.themeCss(context, theme))
            page.setStyle("device", PopupPage.DEVICE_CSS)
            val look = settings.popupLook.put("fit", false)
            page.call("Reader.show(${PopupPage.model(PopupPage.SAMPLE, null, settings.cardCorner, false, true, settings.popupScale, Console.APP_KEY, "", labels, look)})")
        }

        val list = ListView(context).apply {
            choiceMode = ListView.CHOICE_MODE_SINGLE
            adapter = ArrayAdapter(context, android.R.layout.simple_list_item_single_choice, names.map { it.toString() })
            setItemChecked(picked, true)
            setSelection(picked)
            onItemClickListener = AdapterView.OnItemClickListener { _, _, position, _ ->
                picked = position
                draw(position)
            }
            // A controller's d-pad: the theme the focus is on is shown before it is taken.
            onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) = draw(position)
                override fun onNothingSelected(parent: AdapterView<*>?) = draw(picked)
            }
        }
        box.addView(list, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, listTall))
        draw(picked)

        AlertDialog.Builder(context)
            .setTitle(preference.title)
            .setView(box)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val value = values[picked].toString()
                if (preference.callChangeListener(value)) preference.value = value
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setOnDismissListener { page.destroy() }
            .show()
    }
}
