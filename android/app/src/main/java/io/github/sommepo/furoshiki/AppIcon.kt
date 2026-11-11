// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.CheckedTextView
import androidx.appcompat.app.AlertDialog
import org.json.JSONObject

/**
 * The launcher's icon. A launcher takes an app's icon from its manifest, so each colour of
 * home/icon/icon.json is an activity alias of HomeActivity (home/tools/icon.py writes them) and
 * choosing one enables that alias alone. What is enabled is the choice: nothing else keeps it.
 */
object AppIcon {
    private class Choice(val key: String, val name: String) {
        override fun toString() = "$key $name"
    }

    private class Table(val default: String, val choices: List<Choice>)

    private fun table(context: Context): Table {
        val text = context.assets.open("front/icon.json").bufferedReader().use { it.readText() }
        val root = JSONObject(text)
        val colours = root.getJSONArray("colours")
        val choices = List(colours.length()) {
            colours.getJSONObject(it).run { Choice(getString("key"), getString("name")) }
        }
        return Table(root.getString("default"), choices)
    }

    private fun alias(context: Context, key: String) = ComponentName(context, "${context.packageName}.Icon$key")

    private fun chosen(context: Context, table: Table): Choice {
        val manager = context.packageManager
        return table.choices.firstOrNull {
            manager.getComponentEnabledSetting(alias(context, it.key)) == PackageManager.COMPONENT_ENABLED_STATE_ENABLED
        } ?: table.choices.first { it.key == table.default }
    }

    /** What the choice is called, for the page: "30 Solarized". */
    fun label(context: Context) = chosen(context, table(context)).toString()

    private fun choose(context: Context, table: Table, key: String) {
        val manager = context.packageManager
        val set = { target: String, state: Int ->
            manager.setComponentEnabledSetting(alias(context, target), state, PackageManager.DONT_KILL_APP)
        }
        // The new one first, so that the launcher is never left with no way in.
        set(key, PackageManager.COMPONENT_ENABLED_STATE_ENABLED)
        for (other in table.choices) {
            if (other.key != key) set(other.key, PackageManager.COMPONENT_ENABLED_STATE_DISABLED)
        }
    }

    /** The colours as a list with their pictures; `done` runs after a choice. */
    fun pick(context: Context, done: () -> Unit) {
        val table = table(context)
        val rows = object : ArrayAdapter<Choice>(context, android.R.layout.select_dialog_singlechoice, table.choices) {
            override fun getView(position: Int, reuse: View?, parent: ViewGroup): View {
                val row = super.getView(position, reuse, parent) as CheckedTextView
                val size = (40 * context.resources.displayMetrics.density).toInt()
                val id = context.resources.getIdentifier("ic_furoshiki_${table.choices[position].key}", "mipmap", context.packageName)
                val picture = context.getDrawable(id)?.apply { setBounds(0, 0, size, size) }
                row.setCompoundDrawablesRelative(picture, null, null, null)
                row.compoundDrawablePadding = size / 3
                return row
            }
        }
        AlertDialog.Builder(context)
            .setTitle("Icon")
            .setSingleChoiceItems(rows, table.choices.indexOf(chosen(context, table))) { dialog, which ->
                choose(context, table, table.choices[which].key)
                dialog.dismiss()
                done()
            }
            .show()
    }
}
