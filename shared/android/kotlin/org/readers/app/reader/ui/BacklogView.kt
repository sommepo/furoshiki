// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.color.MaterialColors
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.concurrent.Executors
import org.readers.app.R
import org.readers.app.reader.PresentedLine
import org.readers.app.reader.ReaderSession
import org.readers.app.reader.ReaderSettings
import org.readers.app.reader.screen.Area
import org.readers.app.reader.stats.LineAccounting
import org.readers.app.reader.stats.ReadingRules
import org.readers.app.reader.store.BacklogEntry
import org.readers.app.reader.store.ReaderDatabase

/**
 * Every line read in a game, oldest at the top, searchable. Words can be looked up and made into
 * cards here as in the game; a card from an older line gets that line's kept screenshot and
 * audio when they are still held. The backlog screen's content ([ReaderBacklogActivity]), and the
 * backlog an app's companion shows in its own window over another app's game, where a screen of
 * its own would take the keys from the game (and GameNative pauses then).
 */
class BacklogView(
    context: Context,
    private val game: String,
    /** The search field: none where typing would take the keys from the game. */
    searchable: Boolean = true,
    /** The lines' size beside the game's text size: larger on a screen of its own, read from a distance. */
    private val textScale: Float = 1.15f,
    /** Follow new lines as they arrive in play (live backlog on a second display). */
    private val live: Boolean = false,
    openDictionaries: () -> Unit,
) : LinearLayout(context) {
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    private val db: ReaderDatabase = ReaderSession.database(context)
    private val settings = ReaderSettings(context)
    private val listView: RecyclerView
    private val empty: TextView
    private val adapter = LineAdapter()
    private val time = DateTimeFormatter.ofPattern("yyyy/MM/dd HH:mm")
    private var query = ""
    private var loading = false
    private var reachedStart = false
    private var gone = false
    private val liveListener = object : ReaderSession.Listener {
        override fun onLine(presented: PresentedLine) {
            if (gone || query.isNotEmpty() || presented.backlogId <= 0L) return
            if (ReaderSession.gameKey.isNotEmpty() && ReaderSession.gameKey != game) return
            val lm = listView.layoutManager as LinearLayoutManager
            val nearBottom = adapter.itemCount == 0 || lm.findLastVisibleItemPosition() >= adapter.itemCount - 2
            val entry = BacklogEntry(
                id = presented.backlogId,
                game = game,
                line = presented.line,
                characters = presented.accounting.characters,
                reread = presented.accounting.reread,
                counted = presented.accounting.counted && presented.accounting.characters > 0,
            )
            if (adapter.append(entry)) {
                empty.visibility = View.GONE
                if (nearBottom) listView.scrollToPosition(adapter.itemCount - 1)
            }
        }
    }

    /** The popup over the lines. */
    val lookups: LookupLayer

    init {
        orientation = VERTICAL
        ReaderTextView.markStyle = settings.wordMark
        val topBar = LinearLayout(context).apply {
            orientation = HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(12f), dp(6f), dp(12f), dp(6f))
        }
        if (searchable) {
            val search = EditText(context).apply {
                setHint(R.string.reader_backlog_search)
                isSingleLine = true
                setPadding(dp(8f), dp(6f), dp(8f), dp(6f))
                addTextChangedListener(object : TextWatcher {
                    override fun afterTextChanged(s: Editable) {
                        query = s.toString().trim()
                        reload()
                    }

                    override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) = Unit
                    override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) = Unit
                })
            }
            topBar.addView(search, LayoutParams(0, WRAP, 1f))
        } else {
            topBar.addView(View(context), LayoutParams(0, 1, 1f))
        }
        val clearBtn = TextView(context).apply {
            text = context.getString(R.string.reader_backlog_clear_duplicates)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f)
            setTextColor(MaterialColors.getColor(this, com.google.android.material.R.attr.colorOnSurface))
            setPadding(dp(12f), dp(6f), dp(12f), dp(6f))
            val stroke = MaterialColors.getColor(this, com.google.android.material.R.attr.colorOutline)
            background = GradientDrawable().apply {
                cornerRadius = dp(6f).toFloat()
                setStroke(dp(1f), stroke)
            }
            setOnClickListener { clearDuplicates() }
        }
        topBar.addView(clearBtn, LayoutParams(WRAP, WRAP).apply { marginStart = dp(8f) })
        addView(topBar, LayoutParams(MATCH, WRAP))
        val frame = FrameLayout(context)
        listView = RecyclerView(context).apply {
            layoutManager = LinearLayoutManager(context)
            adapter = this@BacklogView.adapter
            addOnScrollListener(object : RecyclerView.OnScrollListener() {
                override fun onScrolled(recyclerView: RecyclerView, dx: Int, dy: Int) {
                    val first = (layoutManager as LinearLayoutManager).findFirstVisibleItemPosition()
                    if (first in 0..4) loadOlder()
                }
            })
        }
        frame.addView(listView, FrameLayout.LayoutParams(MATCH, MATCH))
        empty = text(context.getString(R.string.reader_backlog_empty), 15f, com.google.android.material.R.attr.colorOnSurface).apply {
            setPadding(dp(16f), dp(24f), dp(16f), 0)
            visibility = View.GONE
        }
        frame.addView(empty)
        lookups = LookupLayer(context) { openDictionaries() }
        lookups.passThrough = listView
        frame.addView(lookups)
        addView(frame, LayoutParams(MATCH, 0, 1f))
        if (live) ReaderSession.addListener(liveListener)
        reload()
    }

    /** Scrolls the lines by [dy] pixels (a controller's d-pad or stick, the pointer at an edge). */
    fun scrollLines(dy: Int, smooth: Boolean = true) {
        if (smooth) listView.smoothScrollBy(0, dy) else listView.scrollBy(0, dy)
    }

    /** Moves the controller-selected row by [delta] and scrolls it into view. */
    fun moveSelection(delta: Int) {
        if (adapter.itemCount == 0) return
        val next = (adapter.selected + delta).coerceIn(0, adapter.itemCount - 1)
        adapter.setSelected(next)
        listView.scrollToPosition(next)
    }

    /** Toggles whether the currently selected line is counted for reading stats. */
    fun toggleSelectedCounted() {
        val pos = adapter.selected
        if (pos in 0 until adapter.itemCount) {
            toggleCounted(pos)
        }
    }

    /** Opens a dictionary lookup on the first character of the currently selected line. */
    fun lookupSelected() {
        val pos = adapter.selected
        if (pos !in 0 until adapter.itemCount) return
        val holder = listView.findViewHolderForAdapterPosition(pos) as? LineHolder ?: return
        val entry = adapter.entryAt(pos) ?: return
        if (entry.line.text.isEmpty()) return
        val presented = ReaderSession.recentLine(entry.id)
            ?: PresentedLine(entry.line, LineAccounting(entry.characters, entry.reread, entry.counted, null), entry.id)
        val area = LookupLayer.areaOf(holder.text, lookups)
        val row = Area(area.left, area.top, area.right, area.top + dp(36f))
        lookups.lookup(holder.text, presented, 0, LookupLayer.Anchor(row, area.left))
    }

    /** Removes duplicate lines for this game and refreshes the list. */
    fun clearDuplicates() {
        if (game.isEmpty()) return
        lookups.close()
        worker.execute {
            val removed = runCatching { db.clearDuplicateLines(game) }.getOrDefault(0)
            main.post {
                if (gone) return@post
                reload()
                val msg = when {
                    removed <= 0 -> context.getString(R.string.reader_backlog_duplicates_none)
                    removed == 1 -> context.getString(R.string.reader_backlog_duplicates_removed_one)
                    else -> context.getString(R.string.reader_backlog_duplicates_removed, removed)
                }
                Toast.makeText(context, msg, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun toggleCounted(position: Int) {
        val entry = adapter.entryAt(position) ?: return
        val next = !entry.counted
        worker.execute {
            val changed = runCatching { db.toggleLineCounted(entry.id, next) }.getOrDefault(false)
            main.post {
                if (gone) return@post
                if (changed) {
                    val chars = if (next) ReadingRules.countCharacters(entry.line.text) else 0
                    adapter.updateCounted(entry.id, next, chars)
                    Toast.makeText(
                        context,
                        if (next) R.string.reader_backlog_marked_counted else R.string.reader_backlog_marked_uncounted,
                        Toast.LENGTH_SHORT
                    ).show()
                }
            }
        }
    }

    /** Lets its worker go: the view is not shown again. */
    fun release() {
        gone = true
        if (live) ReaderSession.removeListener(liveListener)
        worker.shutdown()
    }

    /** Loads the newest page (or the search result) and scrolls to the latest line. */
    fun reload() {
        val wanted = query
        loading = true
        worker.execute {
            val page = if (wanted.isEmpty()) db.recentLines(game, PAGE) else db.searchLines(game, wanted, SEARCH_LIMIT)
            main.post {
                if (gone || wanted != query) return@post
                loading = false
                reachedStart = wanted.isNotEmpty() || page.size < PAGE
                lookups.close()
                adapter.replace(page.asReversed())
                empty.visibility = if (page.isEmpty()) View.VISIBLE else View.GONE
                if (page.isNotEmpty()) listView.scrollToPosition(adapter.itemCount - 1)
            }
        }
    }

    private fun loadOlder() {
        if (loading || reachedStart || query.isNotEmpty()) return
        val oldest = adapter.oldestId() ?: return
        loading = true
        worker.execute {
            val page = db.recentLines(game, PAGE, beforeId = oldest)
            main.post {
                if (gone) return@post
                loading = false
                reachedStart = page.size < PAGE
                adapter.prepend(page.asReversed())
            }
        }
    }

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics).toInt()

    private fun text(value: CharSequence, sizeSp: Float, colour: Int) = TextView(context).apply {
        text = value
        setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        setTextColor(MaterialColors.getColor(this, colour))
        setLineSpacing(0f, 1.15f)
    }

    private inner class LineAdapter : RecyclerView.Adapter<LineHolder>() {
        private val items = ArrayList<BacklogEntry>()
        var selected: Int = -1
            private set

        fun setSelected(pos: Int) {
            if (pos == selected) return
            val prev = selected
            selected = pos
            if (prev in items.indices) notifyItemChanged(prev)
            if (selected in items.indices) notifyItemChanged(selected)
        }

        fun entryAt(pos: Int): BacklogEntry? = items.getOrNull(pos)

        fun updateCounted(id: Long, counted: Boolean, chars: Int) {
            val idx = items.indexOfFirst { it.id == id }
            if (idx >= 0) {
                items[idx] = items[idx].copy(counted = counted, characters = chars)
                notifyItemChanged(idx)
            }
        }

        fun replace(entries: List<BacklogEntry>) {
            items.clear()
            items.addAll(entries)
            selected = if (items.isEmpty()) -1 else items.lastIndex
            notifyDataSetChanged()
        }

        fun prepend(entries: List<BacklogEntry>) {
            if (entries.isEmpty()) return
            items.addAll(0, entries)
            if (selected >= 0) selected += entries.size
            notifyItemRangeInserted(0, entries.size)
        }

        fun append(entry: BacklogEntry): Boolean {
            if (items.lastOrNull()?.id == entry.id) return false
            items.add(entry)
            selected = items.lastIndex
            notifyItemInserted(items.lastIndex)
            return true
        }

        fun oldestId(): Long? = items.firstOrNull()?.id

        override fun getItemCount() = items.size

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): LineHolder {
            val row = LinearLayout(parent.context).apply {
                orientation = VERTICAL
                setPadding(dp(16f), dp(10f), dp(16f), dp(10f))
                layoutParams = RecyclerView.LayoutParams(MATCH, WRAP)
            }
            val headerRow = LinearLayout(parent.context).apply {
                orientation = HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
            }
            val caption = text("", if (textScale > 1f) 15f else 13f, com.google.android.material.R.attr.colorOnSurfaceVariant)
            val badge = TextView(parent.context).apply {
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f)
                setPadding(dp(8f), dp(2f), dp(8f), dp(2f))
            }
            headerRow.addView(caption, LayoutParams(0, WRAP, 1f))
            headerRow.addView(badge, LayoutParams(WRAP, WRAP).apply { marginStart = dp(8f) })
            val line = ReaderTextView(parent.context).apply {
                setTextSize(TypedValue.COMPLEX_UNIT_SP, settings.fontSizeSp * textScale)
                setLineSpacing(0f, 1.2f)
                wordHighlightColor = ReaderStyle.HIGHLIGHT
            }
            row.addView(headerRow, LayoutParams(MATCH, WRAP))
            row.addView(line, LayoutParams(MATCH, WRAP).apply { topMargin = dp(4f) })
            return LineHolder(row, caption, badge, line)
        }

        override fun onBindViewHolder(holder: LineHolder, position: Int) {
            val entry = items[position]
            val line = entry.line
            val stamp = time.format(Instant.ofEpochMilli(line.timestamp).atZone(ZoneId.systemDefault()))
            holder.caption.text = if (line.speaker.isEmpty()) stamp else "${line.speaker} · $stamp"
            val isSelected = position == selected
            if (isSelected) {
                val primary = MaterialColors.getColor(holder.itemView, com.google.android.material.R.attr.colorPrimary)
                holder.itemView.setBackgroundColor((primary and 0x00FFFFFF) or 0x22000000)
            } else {
                holder.itemView.background = null
            }
            val accent = MaterialColors.getColor(holder.badge, com.google.android.material.R.attr.colorPrimary)
            val muted = MaterialColors.getColor(holder.badge, com.google.android.material.R.attr.colorOnSurfaceVariant)
            holder.badge.text = context.getString(
                if (entry.counted) R.string.reader_backlog_counted else R.string.reader_backlog_uncounted
            )
            holder.badge.setTextColor(if (entry.counted) accent else muted)
            holder.badge.background = GradientDrawable().apply {
                cornerRadius = dp(4f).toFloat()
                if (entry.counted) {
                    setColor((accent and 0x00FFFFFF) or 0x26000000)
                    setStroke(dp(1f), (accent and 0x00FFFFFF) or 0x99000000.toInt())
                } else {
                    setStroke(dp(1f), (muted and 0x00FFFFFF) or 0x66000000)
                }
            }
            holder.badge.setOnClickListener {
                val pos = holder.bindingAdapterPosition
                if (pos != RecyclerView.NO_POSITION) {
                    setSelected(pos)
                    toggleCounted(pos)
                }
            }
            holder.text.setTextColor(
                MaterialColors.getColor(
                    holder.text,
                    if (entry.reread && settings.rereadColour) {
                        com.google.android.material.R.attr.colorError
                    } else {
                        com.google.android.material.R.attr.colorOnSurface
                    }
                )
            )
            holder.text.setLine(line.text, line.ruby, settings.showFurigana)
            // Lines of this run still know their audio position and may have a screenshot.
            val presented = ReaderSession.recentLine(entry.id)
                ?: PresentedLine(line, LineAccounting(entry.characters, entry.reread, entry.counted, null), entry.id)
            holder.text.onCharacterTap = { offset, bounds ->
                val pos = holder.bindingAdapterPosition
                if (pos != RecyclerView.NO_POSITION) setSelected(pos)
                // Right next to the tapped word, above or below its row.
                val area = LookupLayer.areaOf(holder.text, lookups)
                val row = Area(area.left, area.top + bounds.top, area.right, area.top + bounds.bottom)
                lookups.lookup(holder.text, presented, offset, LookupLayer.Anchor(row, area.left + bounds.left))
            }
            holder.text.onLongPress = {
                ReaderSession.copyToClipboard(line.text)
                Toast.makeText(context, R.string.reader_line_copied, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private class LineHolder(row: View, val caption: TextView, val badge: TextView, val text: ReaderTextView) : RecyclerView.ViewHolder(row)

    private companion object {
        const val MATCH = ViewGroup.LayoutParams.MATCH_PARENT
        const val WRAP = ViewGroup.LayoutParams.WRAP_CONTENT
        const val PAGE = 200
        const val SEARCH_LIMIT = 500
    }
}
