// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the ARMSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.Context
import android.view.ContextThemeWrapper
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import org.readers.app.R
import org.json.JSONObject

/**
 * Progress of the game's own set by hand, for routes read in another copy of the game: what the
 * running game's script offers (reader.progress in the engine; CLANNAD's routes and lights),
 * each with a box. A box changed is written into the game's memory at once by the script; the
 * game keeps it as it keeps its own progress, which the script's note says how. The desktop
 * sibling's *Routes and progress...* window, as a list.
 */
object Routes {
    data class Item(val id: String, val label: String, val group: String, val done: Boolean)
    data class Offer(val title: String, val note: String, val items: List<Item>)

    /** What the script offers now, or null. */
    fun offer(): Offer? {
        val json = try {
            ReaderNative.progress()
        } catch (_: UnsatisfiedLinkError) {
            null // no emulator behind this (tests)
        } ?: return null
        return parse(json)
    }

    internal fun parse(json: String): Offer? = try {
        val root = JSONObject(json)
        val list = root.getJSONArray("items")
        val items = (0 until list.length()).map { i ->
            val o = list.getJSONObject(i)
            Item(o.getString("id"), o.optString("label", o.getString("id")), o.optString("group"),
                o.optBoolean("done"))
        }
        if (items.isEmpty()) null else Offer(root.optString("title"), root.optString("note"), items)
    } catch (_: Exception) {
        null
    }

    /** The list with its boxes; [onClose] when it is left. Main thread. */
    fun show(context: Context, onClose: () -> Unit = {}) {
        val offer = offer()
        if (offer == null) {
            Toast.makeText(context, "The game's script offers nothing to mark", Toast.LENGTH_SHORT).show()
            onClose()
            return
        }
        // Each row says its group in front, as the list has no headings.
        val labels = offer.items.map { item ->
            if (item.group.isEmpty()) item.label else "${item.group} · ${item.label}"
        }.toTypedArray()
        val checked = offer.items.map { it.done }.toBooleanArray()
        val themed = ContextThemeWrapper(context, R.style.Theme_Reader)
        val dialog = AlertDialog.Builder(themed)
            .setTitle(offer.title.ifEmpty { "Routes" })
            .setMultiChoiceItems(labels, checked, null)
            .setPositiveButton(android.R.string.ok, null)
            .apply { if (offer.note.isNotEmpty()) setNeutralButton("How the game keeps them", null) }
            .setOnDismissListener { onClose() }
            .create()
        dialog.setOnShowListener {
            // The note in a box of its own, leaving the list up.
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL)?.setOnClickListener {
                AlertDialog.Builder(themed).setMessage(offer.note)
                    .setPositiveButton(android.R.string.ok, null).show()
            }
            val view = dialog.listView
            view.setOnItemClickListener { _, _, position, _ ->
                val item = offer.items[position]
                val wanted = view.isItemChecked(position)
                val error = try {
                    ReaderNative.markProgress(item.id, wanted)
                } catch (_: UnsatisfiedLinkError) {
                    "no emulator"
                }
                if (error != null) {
                    Toast.makeText(context, "Not changed: $error", Toast.LENGTH_LONG).show()
                }
                // A mark may change others (a route's light): every box shows the game's state.
                offer()?.items?.forEachIndexed { index, now -> view.setItemChecked(index, now.done) }
            }
        }
        dialog.show()
    }
}
