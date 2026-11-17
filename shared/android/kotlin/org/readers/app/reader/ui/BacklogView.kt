// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.util.TypedValue
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
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
    openDictionaries: () -> Unit,
) : LinearLayout(context) {
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    private val db: ReaderDatabase = ReaderSession.database(context)
    private val settings = ReaderSettings(context)
    private val listView: RecyclerView
    private val empty: TextView
    private val adapter = LineAdapter()
    private val time = DateTimeFormatter.ofPattern("MM-dd HH:mm")
    private var query = ""
    private var loading = false
    private var reachedStart = false
    private var gone = false

    /** The popup over the lines. */
    val lookups: LookupLayer

    init {
        orientation = VERTICAL
        ReaderTextView.markStyle = settings.wordMark
        if (searchable) {
            val search = EditText(context).apply {
                setHint(R.string.reader_backlog_search)
                isSingleLine = true
                setPadding(dp(16f), dp(8f), dp(16f), dp(8f))
                addTextChangedListener(object : TextWatcher {
                    override fun afterTextChanged(s: Editable) {
                        query = s.toString().trim()
                        reload()
                    }

                    override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) = Unit
                    override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) = Unit
                })
            }
            addView(search, LayoutParams(MATCH, WRAP))
        }
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
        reload()
    }

    /** Lets its worker go: the view is not shown again. */
    fun release() {
        gone = true
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

        fun replace(entries: List<BacklogEntry>) {
            items.clear()
            items.addAll(entries)
            notifyDataSetChanged()
        }

        fun prepend(entries: List<BacklogEntry>) {
            if (entries.isEmpty()) return
            items.addAll(0, entries)
            notifyItemRangeInserted(0, entries.size)
        }

        fun oldestId(): Long? = items.firstOrNull()?.id

        override fun getItemCount() = items.size

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): LineHolder {
            val row = LinearLayout(parent.context).apply {
                orientation = VERTICAL
                setPadding(dp(16f), dp(8f), dp(16f), dp(8f))
                layoutParams = RecyclerView.LayoutParams(MATCH, WRAP)
            }
            val caption = text("", 13f, com.google.android.material.R.attr.colorOnSurfaceVariant)
            val line = ReaderTextView(parent.context).apply {
                setTextSize(TypedValue.COMPLEX_UNIT_SP, settings.fontSizeSp * 0.9f)
                setLineSpacing(0f, 1.1f)
                wordHighlightColor = ReaderStyle.HIGHLIGHT
            }
            row.addView(caption)
            row.addView(line)
            return LineHolder(row, caption, line)
        }

        override fun onBindViewHolder(holder: LineHolder, position: Int) {
            val entry = items[position]
            val line = entry.line
            val stamp = time.format(Instant.ofEpochMilli(line.timestamp).atZone(ZoneId.systemDefault()))
            holder.caption.text = if (line.speaker.isEmpty()) stamp else "${line.speaker} · $stamp"
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
                ?: PresentedLine(line, LineAccounting(entry.characters, entry.reread, true, null), entry.id)
            holder.text.onCharacterTap = { offset, bounds ->
                // Right next to the tapped word, above or below its row.
                val area = LookupLayer.areaOf(holder.text, lookups)
                val row = Area(area.left, area.top + bounds.top, area.right, area.top + bounds.bottom)
                lookups.lookup(holder.text, presented, offset, LookupLayer.Anchor(row, area.left + bounds.left))
            }
            holder.text.onLongPress = {
                ReaderSession.copyToClipboard(line.text)
                android.widget.Toast.makeText(context, R.string.reader_line_copied, android.widget.Toast.LENGTH_SHORT).show()
            }
        }
    }

    private class LineHolder(row: View, val caption: TextView, val text: ReaderTextView) : RecyclerView.ViewHolder(row)

    private companion object {
        const val MATCH = ViewGroup.LayoutParams.MATCH_PARENT
        const val WRAP = ViewGroup.LayoutParams.WRAP_CONTENT
        const val PAGE = 200
        const val SEARCH_LIMIT = 500
    }
}
