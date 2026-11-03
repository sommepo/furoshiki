// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.materialswitch.MaterialSwitch
import java.io.File
import java.text.NumberFormat
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import org.readers.app.R
import org.readers.app.reader.dict.DictionaryImporter
import org.readers.app.reader.dict.DictionaryInfo
import org.readers.app.reader.dict.DictionaryStore
import org.readers.app.reader.dict.SharedDictionaries

/**
 * Installed dictionaries: import Yomitan archives, choose their order (the first is shown first
 * in lookups), switch them on and off, delete them. A folder of the device's own storage can be
 * the Readers folder that every reader app is given: what is put in it is imported here (and
 * in each other reader app) when this screen opens, each file once.
 */
class DictionaryManagerActivity : ReaderScreen() {
    override val titleRes = R.string.reader_dictionaries

    private val worker = Executors.newSingleThreadExecutor()
    private val cancelled = AtomicBoolean(false)
    private lateinit var store: DictionaryStore
    private lateinit var list: LinearLayout
    private lateinit var status: TextView
    private lateinit var importButton: Button
    private lateinit var cancelButton: Button
    private val numbers = NumberFormat.getIntegerInstance()

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
    private lateinit var folderButton: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        store = DictionaryStore.get(this)
        val column = scrollingColumn()

        importButton = Button(this).apply {
            setText(R.string.reader_dict_import)
            isAllCaps = false
            setOnClickListener {
                pickFiles.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
            }
        }
        cancelButton = Button(this, null, android.R.attr.borderlessButtonStyle).apply {
            setText(R.string.reader_dict_cancel)
            isAllCaps = false
            visibility = View.GONE
            setOnClickListener { cancelled.set(true) }
        }
        folderButton = Button(this, null, android.R.attr.borderlessButtonStyle).apply {
            setText(R.string.reader_dict_folder)
            isAllCaps = false
            setOnClickListener { pickFolder.launch(SharedDictionaries.folder(this@DictionaryManagerActivity)) }
        }
        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        buttons.addView(importButton)
        buttons.addView(folderButton)
        buttons.addView(cancelButton)
        column.addView(buttons)
        folderNote = note("")
        column.addView(folderNote)

        status = note("").apply { setPadding(0, dp(8f), 0, dp(8f)) }
        column.addView(status)

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        column.addView(list)
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

    /** What is new in the Readers folder is imported. */
    private fun importFromFolder() {
        worker.execute {
            val fresh = SharedDictionaries.newFiles(this)
            if (fresh.isEmpty()) return@execute
            runOnUiThread { if (!isDestroyed) import(fresh.map { it.uri }) { done -> fresh.filter { it.uri in done }.forEach { SharedDictionaries.markSeen(this, it) } } }
        }
    }

    override fun onDestroy() {
        cancelled.set(true)
        worker.shutdown()
        super.onDestroy()
    }

    private fun reload() {
        worker.execute {
            val dictionaries = store.database.dictionaries()
            runOnUiThread { if (!isDestroyed) render(dictionaries) }
        }
    }

    private fun render(dictionaries: List<DictionaryInfo>) {
        list.removeAllViews()
        if (dictionaries.isEmpty()) {
            list.addView(body(getString(R.string.reader_dict_none)))
            return
        }
        dictionaries.forEachIndexed { index, info ->
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(0, dp(8f), 0, dp(8f))
            }
            val text = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            text.addView(body(info.title))
            text.addView(
                note(
                    getString(R.string.reader_dict_entry_counts, info.termCount, info.metaCount) +
                        if (info.revision.isNotEmpty()) " · ${info.revision}" else ""
                )
            )
            row.addView(text, LinearLayout.LayoutParams(0, WRAP, 1f))

            row.addView(
                icon(R.drawable.ic_reader_down, R.string.reader_dict_move_up, enabled = index > 0) {
                    move(dictionaries, index, index - 1)
                }.apply { rotation = 180f }
            )
            row.addView(
                icon(R.drawable.ic_reader_down, R.string.reader_dict_move_down, enabled = index < dictionaries.lastIndex) {
                    move(dictionaries, index, index + 1)
                }
            )
            row.addView(icon(R.drawable.ic_reader_x_delete, R.string.reader_dict_delete, enabled = true) { confirmDelete(info) })
            row.addView(
                MaterialSwitch(this).apply {
                    isChecked = info.enabled
                    setOnCheckedChangeListener { _, checked ->
                        worker.execute { store.database.setEnabled(info.id, checked) }
                    }
                }
            )
            list.addView(row)
        }
    }

    private fun icon(drawable: Int, description: Int, enabled: Boolean, action: () -> Unit) =
        ImageButton(this, null, android.R.attr.borderlessButtonStyle).apply {
            setImageResource(drawable)
            imageTintList = android.content.res.ColorStateList.valueOf(
                MaterialColors.getColor(this, com.google.android.material.R.attr.colorOnSurface)
            )
            contentDescription = getString(description)
            isEnabled = enabled
            alpha = if (enabled) 1f else 0.3f
            layoutParams = LinearLayout.LayoutParams(dp(44f), dp(44f))
            setOnClickListener { action() }
        }

    private fun move(dictionaries: List<DictionaryInfo>, from: Int, to: Int) {
        val ids = dictionaries.map { it.id }.toMutableList()
        ids.add(to, ids.removeAt(from))
        worker.execute {
            store.database.setOrder(ids)
            reload()
        }
    }

    private fun confirmDelete(info: DictionaryInfo) {
        MaterialAlertDialogBuilder(this)
            .setMessage(getString(R.string.reader_dict_delete_confirm, info.title))
            .setNegativeButton(android.R.string.cancel, null)
            .setPositiveButton(R.string.reader_dict_delete) { _, _ ->
                worker.execute {
                    store.database.delete(info.id)
                    reload()
                }
            }
            .show()
    }

    /** Imports [uris]; [finished] is told which of them are in the store afterwards (imported, or already there). */
    private fun import(uris: List<Uri>, finished: (Set<Uri>) -> Unit = {}) {
        cancelled.set(false)
        setImporting(true)
        worker.execute {
            val messages = ArrayList<String>()
            val done = HashSet<Uri>()
            val importer = DictionaryImporter(store.database)
            val folder = File(cacheDir, "reader/import").apply { mkdirs() }
            for (uri in uris) {
                if (cancelled.get()) break
                val copy = File(folder, "dictionary.zip")
                try {
                    show(getString(R.string.reader_dict_copying))
                    contentResolver.openInputStream(uri)!!.use { input ->
                        copy.outputStream().use { input.copyTo(it) }
                    }
                    val result = importer.import(
                        copy,
                        progress = { done, total, title ->
                            show(getString(R.string.reader_dict_importing, title, done, total))
                        },
                        keepGoing = { !cancelled.get() }
                    )
                    messages.add(
                        getString(
                            R.string.reader_dict_imported,
                            result.title,
                            result.terms.toInt(),
                            result.metas.toInt()
                        )
                    )
                    done.add(uri)
                } catch (e: DictionaryImporter.ImportException) {
                    // One that is there already (from another file of it) counts as done.
                    if (e.alreadyThere) done.add(uri)
                    messages.add(getString(R.string.reader_dict_failed, e.message ?: e.javaClass.simpleName))
                } catch (e: Exception) {
                    messages.add(getString(R.string.reader_dict_failed, e.message ?: e.javaClass.simpleName))
                } finally {
                    copy.delete()
                }
            }
            val dictionaries = store.database.dictionaries()
            finished(done)
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                setImporting(false)
                status.text = messages.joinToString("\n")
                render(dictionaries)
            }
        }
    }

    private fun show(message: String) {
        runOnUiThread { if (!isDestroyed) status.text = message }
    }

    private fun setImporting(importing: Boolean) {
        importButton.isEnabled = !importing
        folderButton.isEnabled = !importing
        cancelButton.visibility = if (importing) View.VISIBLE else View.GONE
        if (importing) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }
}
