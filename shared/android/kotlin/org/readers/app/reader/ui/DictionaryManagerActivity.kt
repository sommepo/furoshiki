// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.annotation.SuppressLint
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.CheckBox
import android.widget.HorizontalScrollView
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.progressindicator.LinearProgressIndicator
import java.io.File
import java.text.NumberFormat
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import org.readers.app.R
import org.readers.app.reader.dict.DictionaryImporter
import org.readers.app.reader.dict.DictionaryInfo
import org.readers.app.reader.dict.DictionaryKinds
import org.readers.app.reader.ReaderSettings
import org.readers.app.reader.dict.DictionaryStore
import org.readers.app.reader.dict.SharedDictionaries

/**
 * Installed dictionaries: import Yomitan archives, choose their order (the first is shown first
 * in lookups), switch them on and off, delete them, and say which tab of the popup their entries
 * are in (a tap on the name: words, grammar, kanji, names; automatic by [DictionaryKinds]'s rule). A folder of the device's own storage can be
 * the Readers folder that every reader app is given: what is put in it is imported here (and
 * in each other reader app) when this screen opens, each file once.
 */
class DictionaryManagerActivity : ReaderScreen() {
    override val titleRes = R.string.reader_dictionaries

    private val worker = Executors.newSingleThreadExecutor()
    private val cancelled = AtomicBoolean(false)
    private lateinit var store: DictionaryStore
    private lateinit var settings: ReaderSettings
    private lateinit var status: TextView
    private lateinit var importButton: Button
    private lateinit var cancelButton: Button
    private val numbers = NumberFormat.getIntegerInstance()
    private var importing = false

    private val pickFiles =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (!uris.isNullOrEmpty()) import(uris)
        }

    private val pickFolder =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { tree ->
            if (tree == null) return@registerForActivityResult
            try {
                contentResolver.takePersistableUriPermission(tree, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (_: SecurityException) {
            }
            SharedDictionaries.setFolder(this, tree)
            showFolder()
            importFromFolder()
        }

    // The list and its order (the maintainer, 2026-11-23: "a brilliant way of reordering
    // dictionaries on all platforms", the desktop's Settings › Dictionaries): a handle (or a long
    // press) drags a row, live; marked rows are switched, moved to an end or deleted at once; a move
    // mode carries them with a controller's d-pad; a sort by kind; every change of the order undone.
    private lateinit var recycler: RecyclerView
    private lateinit var touch: ItemTouchHelper
    private lateinit var barText: TextView
    private lateinit var bar: LinearLayout
    private val rows = ArrayList<DictionaryInfo>()
    private val marked = LinkedHashSet<Long>()
    private var undoOrder: List<Long>? = null
    private var undone = false
    private var dragFrom: List<Long>? = null
    private var moving = false
    private var movingFrom: List<Long> = emptyList()

    // The import's card (the maintainer, 2026-11-24, on the Fold: "i imported a dict on my fold and
    // didnt see a progress bar or confirmation the upload had even happened"): how far it is with a
    // bar, the dictionary coming in, then what came of it, kept until OK. Each dictionary shows in
    // the list as it comes in, marked New until then.
    private lateinit var importCard: LinearLayout
    private lateinit var importHead: TextView
    private lateinit var importName: TextView
    private lateinit var importBar: LinearProgressIndicator
    private lateinit var importDetail: TextView
    private lateinit var importOk: Button
    private var beforeImport: Set<Long> = emptySet()
    private val arrived = HashSet<Long>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        store = DictionaryStore.get(this)
        settings = ReaderSettings(this)
        // The header stands still and the list scrolls under it (the list drags its rows).
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16f), dp(8f), dp(16f), 0)
        }
        content.addView(column, LinearLayout.LayoutParams(MATCH, WRAP))

        importButton = Button(this).apply {
            setText(R.string.reader_dict_import)
            isAllCaps = false
            // Files or a folder, as the desktop asks (the maintainer, 2026-11-24: "the import choice
            // should just be "files" or "folders""). A folder is the Readers folder: what is put in
            // it is imported whenever this screen opens.
            setOnClickListener {
                MaterialAlertDialogBuilder(this@DictionaryManagerActivity)
                    .setTitle(R.string.reader_dict_import)
                    .setItems(arrayOf(getString(R.string.reader_dict_import_files), getString(R.string.reader_dict_import_folders))) { _, which ->
                        if (which == 0) {
                            pickFiles.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
                        } else {
                            pickFolder.launch(SharedDictionaries.folder(this@DictionaryManagerActivity))
                        }
                    }
                    .show()
            }
        }
        cancelButton = Button(this, null, android.R.attr.borderlessButtonStyle).apply {
            setText(R.string.reader_dict_cancel)
            isAllCaps = false
            visibility = View.GONE
            setOnClickListener { cancelled.set(true) }
        }
        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        buttons.addView(importButton)
        buttons.addView(cancelButton)
        column.addView(buttons)
        folderNote = note("")
        column.addView(folderNote)

        status = note("").apply { setPadding(0, dp(8f), 0, dp(8f)) }
        column.addView(status)
        importCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14f), dp(10f), dp(14f), dp(10f))
            background = android.graphics.drawable.GradientDrawable().apply {
                cornerRadius = dp(12f).toFloat()
                setColor(MaterialColors.getColor(column, com.google.android.material.R.attr.colorSurfaceVariant))
            }
            visibility = View.GONE
        }
        importHead = note("").apply { setTextColor(MaterialColors.getColor(column, com.google.android.material.R.attr.colorPrimary)) }
        importName = body("").apply { textSize = 17f }
        importBar = LinearProgressIndicator(this).apply {
            max = 1000
            setPadding(0, dp(8f), 0, dp(4f))
        }
        importDetail = note("").apply { visibility = View.GONE }
        importOk = Button(this).apply {
            setText(R.string.reader_dict_import_ok)
            isAllCaps = false
            visibility = View.GONE
            setOnClickListener { closeImportCard() }
        }
        importCard.addView(importHead)
        importCard.addView(importName)
        importCard.addView(importBar, LinearLayout.LayoutParams(MATCH, WRAP))
        importCard.addView(importDetail)
        importCard.addView(importOk, LinearLayout.LayoutParams(WRAP, WRAP).apply { gravity = Gravity.END })
        column.addView(importCard, LinearLayout.LayoutParams(MATCH, WRAP).apply { bottomMargin = dp(8f) })

        val tools = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        tools.addView(flat(getString(R.string.reader_dict_sort)) { sortByKind() })
        tools.addView(flat(getString(R.string.reader_dict_mark_all)) { markAll() })
        column.addView(tools)

        recycler = RecyclerView(this).apply {
            layoutManager = LinearLayoutManager(this@DictionaryManagerActivity)
            adapter = this@DictionaryManagerActivity.adapter
            setPadding(dp(8f), 0, dp(8f), 0)
            clipToPadding = false
        }
        content.addView(recycler, LinearLayout.LayoutParams(MATCH, 0, 1f))
        touch = ItemTouchHelper(dragging).also { it.attachToRecyclerView(recycler) }

        bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16f), dp(4f), dp(16f), dp(8f))
        }
        barText = note("").apply { setPadding(0, 0, dp(8f), 0) }
        content.addView(HorizontalScrollView(this).apply { addView(bar) }, LinearLayout.LayoutParams(MATCH, WRAP))
        renderBar()
        reload()
        showFolder()
        importFromFolder()
    }

    private lateinit var folderNote: TextView

    private fun showFolder() {
        val plain = SharedDictionaries.plainName(this)
        val folder = SharedDictionaries.folder(this)
        folderNote.text = when {
            plain != null -> getString(R.string.reader_dict_folder_set, plain)
            folder == null -> getString(R.string.reader_dict_folder_none)
            else -> getString(R.string.reader_dict_folder_set, SharedDictionaries.name(this, folder))
        }
    }

    private fun submit(task: () -> Unit) {
        if (!worker.isShutdown && !isDestroyed) runCatching { worker.execute(task) }
    }

    /** What is new in the Readers folder is imported. */
    private fun importFromFolder() {
        submit {
            val fresh = SharedDictionaries.newFiles(this)
            if (fresh.isEmpty()) return@submit
            runOnUiThread {
                if (!isDestroyed && !importing) {
                    import(fresh.map { it.uri }) { done ->
                        fresh.filter { it.uri in done }.forEach { SharedDictionaries.markSeen(this, it) }
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        cancelled.set(true)
        worker.shutdown()
        super.onDestroy()
    }

    /** Must run on [worker]. */
    private fun loadNow() {
        val dictionaries = store.database.dictionaries()
        runOnUiThread { if (!isDestroyed) render(dictionaries) }
    }

    private fun reload() {
        submit { loadNow() }
    }

    private fun render(dictionaries: List<DictionaryInfo>) {
        val prevIds = rows.map { it.id }.toSet()
        val nextIds = dictionaries.map { it.id }.toSet()
        if (prevIds != nextIds) {
            undoOrder = null
            undone = false
            moving = false
        }
        rows.clear()
        rows.addAll(dictionaries)
        marked.retainAll(nextIds)
        adapter.notifyDataSetChanged()
        if (dictionaries.isEmpty()) {
            status.text = getString(R.string.reader_dict_none)
        } else if (status.text == getString(R.string.reader_dict_none)) {
            status.text = ""
        }
        renderBar()
    }

    private fun flat(text: String, action: () -> Unit) =
        Button(this, null, android.R.attr.borderlessButtonStyle).apply {
            this.text = text
            isAllCaps = false
            setOnClickListener { action() }
        }

    /** The foot: what the marked rows can have done to them, a move's two ways out, and undo. */
    private fun renderBar() {
        bar.removeAllViews()
        bar.addView(barText)
        if (moving) {
            barText.text = resources.getQuantityString(R.plurals.reader_dict_moving, marked.size, marked.size)
            bar.addView(flat(getString(R.string.reader_dict_done)) { endMove(true) })
            bar.addView(flat(getString(R.string.reader_dict_put_back)) { endMove(false) })
            return
        }
        barText.text = if (marked.isEmpty()) "" else getString(R.string.reader_dict_marked, marked.size)
        if (marked.isNotEmpty()) {
            bar.addView(flat(getString(R.string.reader_dict_on)) { switchMarked(true) })
            bar.addView(flat(getString(R.string.reader_dict_off)) { switchMarked(false) })
            bar.addView(flat(getString(R.string.reader_dict_move)) { beginMove() })
            bar.addView(flat(getString(R.string.reader_dict_top)) { toEnd(true) })
            bar.addView(flat(getString(R.string.reader_dict_bottom)) { toEnd(false) })
            bar.addView(flat(getString(R.string.reader_dict_delete_many)) { deleteMarked() })
        }
        if (undoOrder != null) {
            bar.addView(flat(getString(if (undone) R.string.reader_dict_redo else R.string.reader_dict_undo)) { undo() })
        }
    }

    private class Row(view: LinearLayout) : RecyclerView.ViewHolder(view)

    private val adapter = object : RecyclerView.Adapter<Row>() {
        override fun getItemCount() = rows.size

        @SuppressLint("ClickableViewAccessibility")
        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Row {
            val row = LinearLayout(this@DictionaryManagerActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(0, dp(4f), 0, dp(4f))
                isFocusable = true
                layoutParams = RecyclerView.LayoutParams(MATCH, WRAP)
            }
            val holder = Row(row)
            // The handle: a finger on it drags the row at once.
            row.addView(ImageView(this@DictionaryManagerActivity).apply {
                setImageResource(R.drawable.ic_reader_drag)
                imageTintList = android.content.res.ColorStateList.valueOf(
                    MaterialColors.getColor(this, com.google.android.material.R.attr.colorOnSurfaceVariant)
                )
                contentDescription = getString(R.string.reader_dict_drag)
                setPadding(dp(8f), dp(10f), dp(8f), dp(10f))
                setOnTouchListener { _, event ->
                    if (!moving && !importing && event.actionMasked == MotionEvent.ACTION_DOWN) touch.startDrag(holder)
                    false
                }
            }, LinearLayout.LayoutParams(dp(40f), dp(44f)))
            row.addView(CheckBox(this@DictionaryManagerActivity))
            row.addView(LinearLayout(this@DictionaryManagerActivity).apply { orientation = LinearLayout.VERTICAL },
                LinearLayout.LayoutParams(0, WRAP, 1f))
            row.addView(ImageButton(this@DictionaryManagerActivity, null, android.R.attr.borderlessButtonStyle).apply {
                setImageResource(R.drawable.ic_reader_x_delete)
                imageTintList = android.content.res.ColorStateList.valueOf(
                    MaterialColors.getColor(this, com.google.android.material.R.attr.colorOnSurface)
                )
                contentDescription = getString(R.string.reader_dict_delete)
            }, LinearLayout.LayoutParams(dp(44f), dp(44f)))
            row.addView(MaterialSwitch(this@DictionaryManagerActivity))
            return holder
        }

        override fun onBindViewHolder(holder: Row, position: Int) {
            val info = rows[position]
            val row = holder.itemView as LinearLayout
            val check = row.getChildAt(1) as CheckBox
            val text = row.getChildAt(2) as LinearLayout
            val delete = row.getChildAt(3) as ImageButton
            val use = row.getChildAt(4) as MaterialSwitch
            check.setOnCheckedChangeListener(null)
            check.isChecked = info.id in marked
            check.setOnCheckedChangeListener { _, on -> if (on) marked.add(info.id) else marked.remove(info.id); renderBar() }
            text.removeAllViews()
            text.addView(body(if (info.id in arrived) newTitle(position, info.title) else "${position + 1}. ${info.title}"))
            val counts = when {
                info.termCount == 0L && info.kanjiCount > 0L && info.metaCount == 0L ->
                    getString(R.string.reader_dict_kanji_count, info.kanjiCount)
                info.termCount == 0L && info.kanjiCount > 0L ->
                    getString(R.string.reader_dict_kanji_and_meta, info.kanjiCount, info.metaCount)
                info.kanjiCount > 0L ->
                    getString(R.string.reader_dict_entry_counts, info.termCount, info.metaCount) +
                        " · " + getString(R.string.reader_dict_kanji_count, info.kanjiCount)
                else ->
                    getString(R.string.reader_dict_entry_counts, info.termCount, info.metaCount)
            }
            text.addView(
                note(
                    counts +
                        (if (info.revision.isNotEmpty()) " · ${info.revision}" else "") +
                        " · " + getString(R.string.reader_dict_kind_tab, kindName(kindOf(info)))
                )
            )
            text.setOnClickListener { chooseKind(info) }
            delete.setOnClickListener { confirmDelete(info) }
            use.setOnCheckedChangeListener(null)
            use.isChecked = info.enabled
            use.contentDescription = getString(R.string.reader_dict_enabled, info.title)
            val id = info.id
            use.setOnCheckedChangeListener { _, checked ->
                val idx = rows.indexOfFirst { it.id == id }
                if (idx >= 0) rows[idx] = rows[idx].copy(enabled = checked)
                submit { store.database.setEnabled(id, checked) }
            }
            // Being moved: lifted, in the accent's tint.
            row.setBackgroundColor(
                if (moving && info.id in marked)
                    (MaterialColors.getColor(row, com.google.android.material.R.attr.colorPrimary) and 0x00FFFFFF) or 0x30000000
                else 0
            )
            // A controller's confirm on a row marks it, as a finger on its box does.
            row.setOnClickListener { if (!moving) check.isChecked = !check.isChecked }
        }
    }

    /** Dragging a row: it follows the finger, the others make room; the order is written on letting go. */
    private val dragging = object : ItemTouchHelper.SimpleCallback(ItemTouchHelper.UP or ItemTouchHelper.DOWN, 0) {
        override fun isLongPressDragEnabled() = !moving && !importing

        override fun onMove(view: RecyclerView, from: RecyclerView.ViewHolder, to: RecyclerView.ViewHolder): Boolean {
            if (dragFrom == null) dragFrom = rows.map { it.id }
            val a = from.bindingAdapterPosition
            val b = to.bindingAdapterPosition
            if (a < 0 || b < 0) return false
            rows.add(b, rows.removeAt(a))
            adapter.notifyItemMoved(a, b)
            return true
        }

        override fun onSwiped(holder: RecyclerView.ViewHolder, direction: Int) {}

        override fun clearView(view: RecyclerView, holder: RecyclerView.ViewHolder) {
            super.clearView(view, holder)
            val before = dragFrom ?: return
            dragFrom = null
            val after = rows.map { it.id }
            if (after != before) saveOrder(after, before)
            // The numbers in front of the titles again.
            recycler.post { adapter.notifyDataSetChanged() }
        }
    }

    /** Writes [order], keeping [before] for undo. */
    private fun saveOrder(order: List<Long>, before: List<Long>) {
        undoOrder = before
        undone = false
        submit { store.database.setOrder(order) }
        renderBar()
    }

    private fun applyOrder(order: List<Long>) {
        val byId = rows.associateBy { it.id }
        val next = order.mapNotNull { byId[it] }
        rows.clear()
        rows.addAll(next)
        adapter.notifyDataSetChanged()
    }

    private fun undo() {
        val back = undoOrder ?: return
        undoOrder = rows.map { it.id }
        undone = !undone
        applyOrder(back)
        submit { store.database.setOrder(back) }
        renderBar()
    }

    private fun markAll() {
        if (rows.all { it.id in marked }) marked.clear() else rows.forEach { marked.add(it.id) }
        adapter.notifyDataSetChanged()
        renderBar()
    }

    private fun switchMarked(on: Boolean) {
        val ids = marked.toList()
        submit {
            store.database.setEnabled(ids, on)
            loadNow()
        }
    }

    private fun toEnd(top: Boolean) {
        val before = rows.map { it.id }
        val them = before.filter { it in marked }
        val rest = before.filter { it !in marked }
        val order = if (top) them + rest else rest + them
        if (order == before) return
        applyOrder(order)
        saveOrder(order, before)
    }

    private fun deleteMarked() {
        val ids = marked.toList()
        MaterialAlertDialogBuilder(this)
            .setMessage(resources.getQuantityString(R.plurals.reader_dict_delete_many_confirm, ids.size, ids.size))
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.reader_dict_delete) { _, _ ->
                marked.clear()
                submit {
                    store.database.delete(ids)
                    loadNow()
                }
            }
            .show()
    }

    /** Words, grammar, kanji, names, then frequency and pitch lists: each kind's own order kept. */
    private fun sortByKind() {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.reader_dict_sort)
            .setItems(arrayOf(getString(R.string.reader_dict_sort_words_first))) { _, _ ->
                val rank = { info: DictionaryInfo ->
                    if (info.termCount == 0L && info.kanjiCount == 0L && info.metaCount > 0L) 9 else when (kindOf(info)) {
                        DictionaryKinds.WORD -> 0
                        DictionaryKinds.GRAMMAR -> 1
                        DictionaryKinds.KANJI -> 2
                        DictionaryKinds.NAME -> 3
                        else -> 4
                    }
                }
                val before = rows.map { it.id }
                val order = rows.sortedBy(rank).map { it.id }
                if (order == before) return@setItems
                applyOrder(order)
                saveOrder(order, before)
                status.text = getString(R.string.reader_dict_sort_done)
            }
            .show()
    }

    /** A controller's way: the marked rows carried by the d-pad, set down by A, put back by B. */
    private fun beginMove() {
        if (marked.isEmpty()) {
            status.text = getString(R.string.reader_dict_mark_first)
            return
        }
        moving = true
        movingFrom = rows.map { it.id }
        adapter.notifyDataSetChanged()
        renderBar()
    }

    private fun endMove(keep: Boolean) {
        if (!moving) return
        moving = false
        val now = rows.map { it.id }
        if (!keep) {
            applyOrder(movingFrom)
        } else if (now != movingFrom) {
            saveOrder(now, movingFrom)
        }
        adapter.notifyDataSetChanged()
        renderBar()
    }

    private fun step(up: Boolean) {
        val ids = rows.map { it.id }.toMutableList()
        if (up) {
            for (i in 1 until ids.size) if (ids[i] in marked && ids[i - 1] !in marked) ids[i] = ids[i - 1].also { ids[i - 1] = ids[i] }
        } else {
            for (i in ids.size - 2 downTo 0) if (ids[i] in marked && ids[i + 1] !in marked) ids[i] = ids[i + 1].also { ids[i + 1] = ids[i] }
        }
        applyOrder(ids)
        rows.indexOfFirst { it.id in marked }.takeIf { it >= 0 }?.let { recycler.scrollToPosition(it) }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (moving) {
            val handled = when (event.keyCode) {
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
                KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_BUTTON_L1,
                KeyEvent.KEYCODE_PAGE_DOWN, KeyEvent.KEYCODE_BUTTON_R1,
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_DPAD_CENTER,
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_ESCAPE -> true
                else -> false
            }
            if (handled) {
                if (event.action == KeyEvent.ACTION_DOWN) {
                    when (event.keyCode) {
                        KeyEvent.KEYCODE_DPAD_UP -> step(true)
                        KeyEvent.KEYCODE_DPAD_DOWN -> step(false)
                        KeyEvent.KEYCODE_PAGE_UP, KeyEvent.KEYCODE_BUTTON_L1 -> repeat(10) { step(true) }
                        KeyEvent.KEYCODE_PAGE_DOWN, KeyEvent.KEYCODE_BUTTON_R1 -> repeat(10) { step(false) }
                        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_DPAD_CENTER -> endMove(true)
                        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_ESCAPE -> endMove(false)
                    }
                }
                return true
            }
        }
        return super.dispatchKeyEvent(event)
    }

    private fun kindName(kind: String): String = getString(
        when (kind) {
            DictionaryKinds.GRAMMAR -> R.string.reader_dict_kind_grammar
            DictionaryKinds.KANJI -> R.string.reader_dict_kind_kanji
            DictionaryKinds.NAME -> R.string.reader_dict_kind_name
            else -> R.string.reader_dict_kind_word
        }
    )

    private fun automaticKind(info: DictionaryInfo): String = when {
        info.scope.isNotEmpty() -> DictionaryKinds.NAME
        info.kanjiCount > 0L && info.termCount == 0L -> DictionaryKinds.KANJI
        else -> DictionaryKinds.ofTitle(info.title) ?: DictionaryKinds.WORD
    }

    /** The kind the popup gives [info]'s entries: the user's, or what its title says. */
    private fun kindOf(info: DictionaryInfo): String =
        settings.dictionaryKinds[info.title] ?: automaticKind(info)

    /** Which tab of the popup [info]'s entries are in: automatic, or a kind chosen here. */
    private fun chooseKind(info: DictionaryInfo) {
        val chosen = settings.dictionaryKinds
        val automatic = automaticKind(info)
        val values = listOf<String?>(null) + DictionaryKinds.ALL
        val names = values.map { if (it == null) getString(R.string.reader_dict_kind_auto, kindName(automatic)) else kindName(it) }
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.reader_dict_kind)
            .setSingleChoiceItems(names.toTypedArray(), values.indexOf(chosen[info.title])) { dialog, which ->
                settings.preferences.edit()
                    .putString(ReaderSettings.KEY_DICTIONARY_KINDS, DictionaryKinds.with(chosen, info.title, values[which]))
                    .apply()
                dialog.dismiss()
                reload()
            }
            .show()
    }

    private fun confirmDelete(info: DictionaryInfo) {
        MaterialAlertDialogBuilder(this)
            .setMessage(getString(R.string.reader_dict_delete_confirm, info.title))
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.reader_dict_delete) { _, _ ->
                submit {
                    store.database.delete(info.id)
                    loadNow()
                }
            }
            .show()
    }

    /** Imports [uris]; [finished] is told which of them are in the store afterwards (imported, or already there). */
    internal fun import(uris: List<Uri>, finished: (Set<Uri>) -> Unit = {}) {
        if (importing) return
        cancelled.set(false)
        setImporting(true)
        beforeImport = rows.map { it.id }.toSet()
        arrived.clear()
        status.text = ""
        openImportCard(uris.size)
        submit {
            val problems = ArrayList<String>()
            val done = HashSet<Uri>()
            var imported = 0
            var already = 0
            var last: DictionaryImporter.Result? = null
            val importer = DictionaryImporter(store.database)
            val folder = File(cacheDir, "reader/import").apply { mkdirs() }
            for ((index, uri) in uris.withIndex()) {
                if (cancelled.get()) break
                val copy = File.createTempFile("dict-", ".zip", folder)
                try {
                    progress(index, uris.size, getString(R.string.reader_dict_import_reading), 0, 0)
                    (contentResolver.openInputStream(uri) ?: throw java.io.IOException("Couldn't open file")).use { input ->
                        copy.outputStream().use { input.copyTo(it) }
                    }
                    if (cancelled.get()) break
                    val result = importer.import(
                        copy,
                        progress = { part, whole, title -> progress(index, uris.size, title, part, whole) },
                        keepGoing = { !cancelled.get() }
                    )
                    imported++
                    last = result
                    done.add(uri)
                    // In the list at once, marked New.
                    val now = store.database.dictionaries()
                    runOnUiThread { if (!isDestroyed) arrive(now) }
                } catch (e: DictionaryImporter.ImportException) {
                    if (cancelled.get()) break
                    // One that is there already (from another file of it) counts as done.
                    if (e.alreadyThere) {
                        done.add(uri)
                        already++
                    } else {
                        problems.add(getString(R.string.reader_dict_failed, e.message ?: e.javaClass.simpleName))
                    }
                } catch (e: Exception) {
                    if (cancelled.get()) break
                    problems.add(getString(R.string.reader_dict_failed, e.message ?: e.javaClass.simpleName))
                } finally {
                    copy.delete()
                }
            }
            val dictionaries = store.database.dictionaries()
            // The library's index of every dictionary, once the batch is over (the engine's).
            if (done.isNotEmpty()) runCatching { store.database.compact() }
            finished(done)
            val said = ArrayList<String>()
            val one = last
            when {
                imported == 1 && uris.size == 1 && one != null ->
                    said.add(getString(R.string.reader_dict_import_one, one.title, numbers.format(one.terms + one.metas + one.kanji)))
                imported == 1 -> said.add(getString(R.string.reader_dict_import_single))
                imported > 1 -> said.add(getString(R.string.reader_dict_import_many, imported))
            }
            if (already > 0) said.add(getString(R.string.reader_dict_import_already, already))
            if (problems.isNotEmpty()) {
                said.add(if (problems.size == 1) getString(R.string.reader_dict_import_error) else getString(R.string.reader_dict_import_errors, problems.size))
            }
            if (cancelled.get()) said.add(getString(R.string.reader_dict_import_cancelled))
            if (said.isEmpty()) said.add(getString(R.string.reader_dict_import_nothing))
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                setImporting(false)
                arrive(dictionaries)
                finishImportCard(problems.isNotEmpty(), said.joinToString(" · "), problems.joinToString("\n"))
            }
        }
    }

    private fun openImportCard(files: Int) {
        importHead.text = getString(R.string.reader_dict_import_count, 1, files)
        importName.text = getString(R.string.reader_dict_import_reading)
        importBar.isIndeterminate = false
        importBar.setProgressCompat(0, false)
        importBar.visibility = View.VISIBLE
        importDetail.visibility = View.GONE
        importOk.visibility = View.GONE
        importCard.visibility = View.VISIBLE
    }

    /** How far the import is: the files before this one, and this one's part. */
    private fun progress(index: Int, files: Int, title: String, part: Int, whole: Int) {
        runOnUiThread {
            if (isDestroyed) return@runOnUiThread
            importHead.text = getString(R.string.reader_dict_import_count, index + 1, files) +
                (if (arrived.isNotEmpty()) " · " + resources.getQuantityString(R.plurals.reader_dict_imported, arrived.size, arrived.size) else "")
            importName.text = title
            val share = (index + if (whole > 0) part.toDouble() / whole else 0.0) / files.coerceAtLeast(1)
            importBar.setProgressCompat((share * 1000).toInt().coerceIn(0, 1000), true)
        }
    }

    private fun finishImportCard(trouble: Boolean, said: String, problems: String) {
        importHead.setText(if (trouble) R.string.reader_dict_import_finished_errors else R.string.reader_dict_import_finished)
        importName.text = said
        importBar.visibility = View.GONE
        importDetail.text = problems
        importDetail.visibility = if (problems.isEmpty()) View.GONE else View.VISIBLE
        importOk.visibility = View.VISIBLE
        importCard.visibility = View.VISIBLE
        importCard.announceForAccessibility(said)
    }

    private fun closeImportCard() {
        importCard.visibility = View.GONE
        arrived.clear()
        adapter.notifyDataSetChanged()
    }

    /** The list as it now is, what came in since the import began marked New, the newest in sight. */
    private fun arrive(dictionaries: List<DictionaryInfo>) {
        var newest = -1
        for ((at, info) in dictionaries.withIndex()) {
            if (info.id !in beforeImport && arrived.add(info.id)) newest = at
        }
        render(dictionaries)
        if (newest >= 0) recycler.scrollToPosition(newest)
    }

    private fun newTitle(position: Int, title: String): CharSequence {
        val mark = getString(R.string.reader_dict_new)
        val text = android.text.SpannableString("${position + 1}. $title  $mark")
        val colour = MaterialColors.getColor(recycler, com.google.android.material.R.attr.colorPrimary)
        text.setSpan(android.text.style.ForegroundColorSpan(colour), text.length - mark.length, text.length, 0)
        text.setSpan(android.text.style.StyleSpan(android.graphics.Typeface.BOLD), text.length - mark.length, text.length, 0)
        return text
    }

    private fun setImporting(importing: Boolean) {
        this.importing = importing
        importButton.isEnabled = !importing
        cancelButton.visibility = if (importing) View.VISIBLE else View.GONE
        if (importing) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }
}
