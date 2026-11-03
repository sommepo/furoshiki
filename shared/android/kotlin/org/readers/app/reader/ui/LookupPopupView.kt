// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.ui

import android.animation.ValueAnimator
import android.annotation.SuppressLint
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.InsetDrawable
import android.graphics.drawable.StateListDrawable
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.AbsoluteSizeSpan
import android.text.style.ForegroundColorSpan
import android.text.style.ReplacementSpan
import android.text.style.StyleSpan
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.animation.LinearInterpolator
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.util.IdentityHashMap
import org.readers.app.R
import org.readers.app.reader.dict.GlossaryRenderer
import org.readers.app.reader.dict.LookupResult
import org.readers.app.reader.dict.TermEntry
import org.readers.app.reader.store.LookupHistory
import org.readers.app.reader.Console
import org.readers.app.reader.ReaderLog
import org.json.JSONArray
import org.json.JSONObject

/**
 * The dictionary popup: for words found always as wide as it may be (a message only as wide
 * as what it says), and only as tall as what it says. Per headword there is one line with the word, its reading and how it was inflected,
 * then one line per sense with its glosses run together. In a corner of each headword's part
 * (the bottom left unless the settings choose another, [cardCorner]) sits a boxed button: a
 * plus to make a card, or an open book when Anki has the word already (a turning ring while
 * that is not known yet). Where a headword's part
 * runs on past what the popup shows, its box waits at the popup's own edge on that side, so it
 * can be pressed without scrolling.
 */
@SuppressLint("ViewConstructor")
class LookupPopupView(context: Context) : FrameLayout(context) {
    /** What the box at the foot of a headword offers. */
    enum class CardState {
        /** Not in Anki, as far as is known: a plus that makes a card. */
        ADD,

        /** A card is being made. */
        WORKING,

        /** Anki has the word: an open book that shows its notes. */
        IN_DECK,

        /** Not known yet: a turning ring, which makes a card all the same. */
        CHECKING,

        /** Anki did not answer: a plus with a grey dot, which tries all the same. */
        AWAY
    }

    var onAdd: ((result: LookupResult) -> Unit)? = null
    var onShowNotes: ((result: LookupResult) -> Unit)? = null
    var onOpenDictionaries: (() -> Unit)? = null

    /** Keep offering the plus beside the book for a word Anki already has. */
    var allowDuplicates = false

    /** A word not in Anki gets a soft tint behind its headword. */
    var markUnknown = true

    /**
     * The popup's look: "classic" (drawn by these views, as before) or a theme of the popup page
     * ("console": the console's colours, "furoshiki", "yomitan-dark", "paper", "paper-dark"), drawn by a
     * [PopupPage] where a WebView can be had; else classic. Set before [showResults].
     */
    var theme = "classic"
        set(value) {
            field = value
            if (value == "classic" || !PopupPage.available) dropPage() else page?.setStyle("theme", PopupPage.themeCss(context, value))
        }

    /** The user's own CSS, applied after the theme. */
    var userCss = ""
        set(value) {
            if (field == value) return
            field = value
            page?.setStyle("user", value)
        }

    /** The question about a card's audio, answered in the page. */
    var onListen: (() -> Unit)? = null
    var onKeep: (() -> Unit)? = null
    var onDrop: (() -> Unit)? = null

    /**
     * Puts the question about a card's audio in the page, in the place of [result]'s buttons;
     * false where the page does not draw the popup (the question then has its own view).
     */
    fun askOnPage(result: LookupResult): Boolean {
        val page = page ?: return false
        if (scroll.visibility != GONE) return false
        val index = shownResults.indexOf(result)
        if (index < 0) return false
        page.call("Reader.ask($index)")
        return true
    }

    fun askPlaying(playing: Boolean) = page?.call("Reader.askPlaying($playing)")

    fun askDone() = page?.call("Reader.askDone()")

    /** The page laid the popup out anew: it may be another height now. */
    var onLayoutChanged: (() -> Unit)? = null

    private var page: PopupPage? = null
    private var pageHeight = 0
    /** Where the page says each result's plus is, in this view's pixels. */
    private val pageBoxes = HashMap<Int, android.graphics.Rect>()

    /** Where [result]'s plus is, in this view's pixels, or null when it shows none. */
    fun addBoxOf(result: LookupResult): android.graphics.Rect? {
        if (page != null && scroll.visibility == GONE) return pageBoxes[shownResults.indexOf(result)]
        val add = entries[result]?.add?.takeIf { it.isShown } ?: return null
        val at = IntArray(2).also { add.getLocationInWindow(it) }
        val me = IntArray(2).also { getLocationInWindow(it) }
        return android.graphics.Rect(at[0] - me[0], at[1] - me[1], at[0] - me[0] + add.width, at[1] - me[1] + add.height)
    }
    private var shownResults: List<LookupResult> = emptyList()

    /** The page that draws the popup, made when a theme first wants it. */
    private fun page(): PopupPage? {
        if (theme == "classic" || !PopupPage.available) return null
        page?.let { return it }
        return runCatching { PopupPage(context, ::onPageMessage) }.getOrElse {
            ReaderLog.warning("[Reader] No WebView for the popup: ${it.message}")
            null
        }?.also {
            addView(it, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
            it.setStyle("console", PopupPage.consoleCss(context))
            it.setStyle("theme", PopupPage.themeCss(context, theme))
            if (userCss.isNotEmpty()) it.setStyle("user", userCss)
            page = it
        }
    }

    private fun dropPage() {
        page?.let {
            removeView(it)
            it.destroy()
        }
        page = null
        scroll.visibility = VISIBLE
        alpha = 1f
    }

    private fun showOnPage(entries: JSONArray, message: JSONObject?) {
        val page = page() ?: return
        scroll.visibility = GONE
        // Seen once the page has said how tall it is.
        alpha = 0f
        val labels = JSONObject()
            .put("add", context.getString(R.string.reader_lookup_add))
            .put("book", context.getString(R.string.reader_lookup_in_deck))
            .put("dictionaries", context.getString(R.string.reader_lookup_open_dictionaries))
            .put("listen", context.getString(R.string.reader_audio_listen))
            .put("stop", context.getString(R.string.reader_audio_stop))
            .put("keep", context.getString(R.string.reader_audio_keep))
            .put("drop", context.getString(R.string.reader_audio_drop))
        val model = PopupPage.model(
            entries, message, cardCorner.name.lowercase(), allowDuplicates, markUnknown, textScale,
            Console.APP_KEY, org.readers.app.reader.ReaderSession.gameKey, labels
        )
        page.call("Reader.show($model)")
    }

    private fun onPageMessage(message: JSONObject) {
        val index = message.optInt("index", -1)
        when (message.optString("type")) {
            "size" -> {
                val density = resources.displayMetrics.density
                pageHeight = (message.optDouble("height", 0.0) * density).toInt()
                pageBoxes.clear()
                message.optJSONArray("boxes")?.let { boxes ->
                    for (i in 0 until boxes.length()) {
                        val box = boxes.getJSONObject(i)
                        if (box.optString("kind") != "add") continue
                        val x = (box.optDouble("x") * density).toInt()
                        val y = (box.optDouble("y") * density).toInt()
                        pageBoxes[box.optInt("index")] = android.graphics.Rect(
                            x, y, x + (box.optDouble("w") * density).toInt(), y + (box.optDouble("h") * density).toInt()
                        )
                    }
                }
                alpha = 1f
                requestLayout()
                onLayoutChanged?.invoke()
            }

            "add" -> shownResults.getOrNull(index)?.let { result ->
                if (cardState(result) != CardState.WORKING) onAdd?.invoke(result)
            }

            "book" -> shownResults.getOrNull(index)?.let { onShowNotes?.invoke(it) }
            "dictionaries" -> onOpenDictionaries?.invoke()
            "listen" -> onListen?.invoke()
            "keep" -> onKeep?.invoke()
            "drop" -> onDrop?.invoke()
        }
    }

    /** The corner of a headword's part where its boxes stand. */
    enum class Corner(val top: Boolean, val left: Boolean) {
        TOP_LEFT(true, true),
        TOP_RIGHT(true, false),
        BOTTOM_LEFT(false, true),
        BOTTOM_RIGHT(false, false);

        companion object {
            /** From the setting's value ("bottom_left" and so on); the bottom left otherwise. */
            fun of(value: String?): Corner = values().firstOrNull { it.name.equals(value, ignoreCase = true) } ?: BOTTOM_LEFT
        }
    }

    /** Where the boxes stand: set before [showResults]. */
    var cardCorner = Corner.BOTTOM_LEFT

    /** The popup grows with its content up to these, then wraps and scrolls. */
    var maxWidth: Int = Int.MAX_VALUE
        set(value) {
            if (field != value) {
                field = value
                requestLayout()
            }
        }
    var maxHeight: Int = Int.MAX_VALUE
        set(value) {
            if (field != value) {
                field = value
                requestLayout()
            }
        }

    /** Size of the popup's text relative to its normal size, in percent. */
    var textScale: Int = 100
        set(value) {
            field = value.coerceIn(50, 250)
        }

    /**
     * Words found: as wide as it may be ([maxWidth]), so that a short entry does not make a
     * narrow popup and one lookup after another keeps its size (the maintainer's wish). A
     * message is only as wide as what it says.
     */
    private var uniform = false
        set(value) {
            field = value
            val width = if (value) LayoutParams.MATCH_PARENT else LayoutParams.WRAP_CONTENT
            scroll.layoutParams = scroll.layoutParams.also { it.width = width }
            list.layoutParams = list.layoutParams.also { it.width = width }
        }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val width = limit(widthMeasureSpec, maxWidth)
        if (page != null && scroll.visibility == GONE) {
            // As wide as it may be, as tall as the page said its content is.
            val tall = minOf(pageHeight.coerceAtLeast(1), MeasureSpec.getSize(limit(heightMeasureSpec, maxHeight)))
            super.onMeasure(
                MeasureSpec.makeMeasureSpec(MeasureSpec.getSize(width), MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(tall, MeasureSpec.EXACTLY)
            )
            return
        }
        super.onMeasure(
            if (uniform) MeasureSpec.makeMeasureSpec(MeasureSpec.getSize(width), MeasureSpec.EXACTLY) else width,
            limit(heightMeasureSpec, maxHeight)
        )
    }

    /** At most [max], and never more than the parent allows. */
    private fun limit(spec: Int, max: Int): Int {
        val allowed = if (MeasureSpec.getMode(spec) == MeasureSpec.UNSPECIFIED) {
            Int.MAX_VALUE
        } else {
            MeasureSpec.getSize(spec)
        }
        return MeasureSpec.makeMeasureSpec(minOf(allowed, max).coerceAtMost(1 shl 29), MeasureSpec.AT_MOST)
    }

    private val scroll = ScrollView(context)

    // The lines between headwords are the layout's own dividers: a divider view would have to
    // say how wide it is, and "as wide as the popup" makes the popup as wide as it may be.
    private val list = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL
        showDividers = LinearLayout.SHOW_DIVIDER_MIDDLE
        dividerDrawable = ConsoleLook.rule(resources.displayMetrics.density)
    }

    /** One headword's part of the popup. */
    private class Entry(
        val result: LookupResult,
        val column: View,
        val headline: TextView,
        var headlineText: CharSequence,
        val last: TextView,
        var lastText: CharSequence,
        val add: ImageButton,
        val book: ImageButton,
        val actions: View
    ) {
        var state = CardState.ADD

        /** Not in Anki, as far as is known: the headword is marked. */
        var unknown = false
        lateinit var view: View
    }

    private val entries = IdentityHashMap<LookupResult, Entry>()

    init {
        background = ConsoleLook.ground(resources.displayMetrics.density)
        elevation = dp(6f)
        isClickable = true // taps inside never fall through to the game
        scroll.setPadding(dp(7f).toInt(), dp(5f).toInt(), dp(5f).toInt(), dp(4f).toInt())
        scroll.isVerticalScrollBarEnabled = true
        // The frame around the first entry (ConsoleLook.entry), where a console's look has one, is
        // a little wider than the entry.
        list.clipChildren = false
        scroll.clipChildren = false
        scroll.clipToPadding = false
        scroll.setOnScrollChangeListener { _, _, _, _, _ -> placeBoxes() }
        scroll.addView(list, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
        addView(scroll, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        placeBoxes()
    }

    /**
     * Keeps every box in reach: one whose headword runs on below what is shown (above it, for a
     * box in a top corner) is moved to that edge of what is shown, as long as that keeps it
     * within its headword's part.
     */
    private fun placeBoxes() {
        val shownFrom = scroll.scrollY
        val shownTo = scroll.scrollY + scroll.height - scroll.paddingTop - scroll.paddingBottom
        for (entry in entries.values) {
            val most = (entry.view.height - entry.actions.height).coerceAtLeast(0)
            entry.actions.translationY = if (cardCorner.top) {
                (shownFrom - entry.view.top).coerceIn(0, most).toFloat()
            } else {
                -(entry.view.bottom - shownTo).coerceIn(0, most).toFloat()
            }
        }
    }

    /** How far the list is scrolled, for tests and for callers that want to scroll it. */
    val scrolled: Int get() = scroll.scrollY

    fun scrollTo(y: Int) = scroll.scrollTo(0, y)

    /**
     * Shows the words found. With [history] (one per result, from the reader database), a quiet
     * line under each headword says how often it was looked up before; [withWhere] adds when
     * and in which game first.
     */
    fun showResults(results: List<LookupResult>, history: List<LookupHistory> = emptyList(), withWhere: Boolean = false) {
        uniform = true
        list.removeAllViews()
        entries.clear()
        scroll.scrollTo(0, 0)
        shownResults = results
        if (page() != null) {
            val model = JSONArray()
            for ((index, result) in results.withIndex()) {
                model.put(PopupModel.entry(result, history.getOrNull(index)?.takeIf { it.count > 0 }?.let { historyLine(it, withWhere) }))
            }
            showOnPage(model, null)
        }
        for ((index, result) in results.withIndex()) {
            // As wide as the popup, so that every button box sits at its right edge.
            list.addView(
                resultView(result, history.getOrNull(index)?.takeIf { it.count > 0 }?.let { historyLine(it, withWhere) }),
                LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT)
            )
        }
    }

    fun showMessage(message: String, offerDictionaries: Boolean) {
        uniform = false
        shownResults = emptyList()
        if (page() != null) showOnPage(JSONArray(), JSONObject().put("text", message).put("dictionaries", offerDictionaries))
        list.removeAllViews()
        entries.clear()
        list.addView(label(message, SENSE_SP, ReaderStyle.POPUP_TEXT).apply { setPadding(0, 0, dp(4f).toInt(), 0) })
        if (offerDictionaries) {
            list.addView(
                label(context.getString(R.string.reader_lookup_open_dictionaries), SENSE_SP, ReaderStyle.POPUP_ACCENT).apply {
                    setPadding(0, dp(8f).toInt(), dp(4f).toInt(), dp(4f).toInt())
                    setOnClickListener { onOpenDictionaries?.invoke() }
                }
            )
        }
    }

    /** Marks [result]'s headword as not in Anki, or takes the mark away. */
    fun setUnknown(result: LookupResult, unknown: Boolean) {
        val entry = entries[result] ?: return
        if (entry.unknown == unknown) return
        entry.unknown = unknown
        page?.call("Reader.setUnknown(${shownResults.indexOf(result)}, $unknown)")
        if (entry.last === entry.headline) entry.lastText = headline(result, unknown)
        else entry.headlineText = headline(result, unknown)
        apply(entry)
    }

    /** Whether [result]'s headword is marked as not in Anki. For tests. */
    fun isUnknown(result: LookupResult): Boolean = entries[result]?.unknown == true

    private fun historyLine(history: LookupHistory, withWhere: Boolean): String {
        val count = if (history.count == 1) {
            context.getString(R.string.reader_lookup_history_once)
        } else {
            context.getString(R.string.reader_lookup_history_times, history.count)
        }
        if (!withWhere || history.firstTs <= 0) return count
        val day = java.text.SimpleDateFormat("d MMM", java.util.Locale.getDefault()).format(java.util.Date(history.firstTs))
        val where = history.firstTitle.ifEmpty { history.firstGame }
        return if (where.isEmpty()) {
            context.getString(R.string.reader_lookup_history_first, count, day)
        } else {
            context.getString(R.string.reader_lookup_history_first_in, count, day, where)
        }
    }

    /** What the box of [result] offers, or null when the popup no longer shows it. */
    fun cardState(result: LookupResult): CardState? = entries[result]?.state

    /** Changes what the box of [result] offers. Does nothing when it is no longer shown. */
    fun setCardState(result: LookupResult, state: CardState) {
        val entry = entries[result] ?: return
        entry.state = state
        page?.call("Reader.setBox(${shownResults.indexOf(result)}, ${JSONObject.quote(PopupModel.stateName(state))})")
        apply(entry)
    }

    private fun resultView(result: LookupResult, history: String?): View {
        val column = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(3f).toInt(), 0, bottomRoom())
        }
        val corner = cardCorner
        val headline = label("", HEAD_SP, ReaderStyle.POPUP_TEXT)
        column.addView(headline, LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
        if (history != null) {
            column.addView(
                label(history, SMALL_SP, ReaderStyle.POPUP_DIM),
                LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT)
            )
        }
        val senses = senses(result)
        val last: TextView
        val lastText: CharSequence
        if (senses.isNotEmpty()) {
            headline.text = headline(result)
            last = label("", SENSE_SP, ReaderStyle.POPUP_TEXT).apply { setLineSpacing(0f, 1.08f) }
            lastText = senses
            column.addView(last, LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
        } else {
            last = headline
            lastText = headline(result)
        }

        val add = box(R.drawable.ic_reader_add, R.string.reader_lookup_add, ReaderStyle.POPUP_SIGN, corner)
        add.setOnClickListener { onAdd?.invoke(result) }
        val book = box(R.drawable.ic_reader_open_book, R.string.reader_lookup_in_deck, ReaderStyle.POPUP_BOOK_SIGN, corner)
        book.setOnClickListener { onShowNotes?.invoke(result) }
        val actions = LinearLayout(context).apply { orientation = LinearLayout.HORIZONTAL }
        val size = LinearLayout.LayoutParams(boxSize() + slopLeft(), boxSize() + slopTop())
        // The plus is the one nearer the corner, the book beside it.
        for (box in if (corner.left) listOf(add, book) else listOf(book, add)) {
            actions.addView(box, LinearLayout.LayoutParams(size))
        }

        val entry = Entry(result, column, headline, if (senses.isNotEmpty()) headline.text else lastText, last, lastText, add, book, actions)
        entries[result] = entry
        apply(entry)
        entry.view = FrameLayout(context).apply {
            background = ConsoleLook.entry(resources.displayMetrics.density, entries.size == 1, headline)
            addView(column, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT))
            addView(
                actions,
                LayoutParams(
                    LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT,
                    (if (corner.top) Gravity.TOP else Gravity.BOTTOM) or (if (corner.left) Gravity.START else Gravity.END)
                )
            )
        }
        return entry.view
    }

    /** Shows the boxes [entry]'s state calls for and keeps the text clear of them. */
    private fun apply(entry: Entry) {
        val inDeck = entry.state == CardState.IN_DECK
        val working = entry.state == CardState.WORKING
        entry.book.visibility = if (inDeck) VISIBLE else GONE
        entry.add.visibility = if (inDeck && !allowDuplicates) GONE else VISIBLE
        entry.add.isEnabled = !working
        entry.add.alpha = if (working) 0.4f else 1f
        (entry.add.drawable as? Ring)?.stop()
        when (entry.state) {
            CardState.CHECKING -> entry.add.setImageDrawable(Ring(ReaderStyle.POPUP_SIGN).also { it.start() })
            CardState.AWAY -> entry.add.setImageDrawable(
                Away(context.getDrawable(R.drawable.ic_reader_add)!!.mutate(), ReaderStyle.POPUP_SIGN)
            )
            else -> if (entry.add.drawable is Ring || entry.add.drawable is Away) {
                entry.add.setImageResource(R.drawable.ic_reader_add)
            }
        }
        val boxes = (if (entry.book.visibility == VISIBLE) 1 else 0) + (if (entry.add.visibility == VISIBLE) 1 else 0)
        val room = boxes * (boxSize() + slopLeft())
        val corner = cardCorner
        // The text keeps clear of the boxes. Bottom right: a blank as wide as them ends the
        // text, so the last line stops short of them (or the blank takes a line of its own).
        // Top left: such a blank begins the headline. Top right: the headline stops short of
        // them. Bottom left: they have a row of their own under the text.
        fun withRoom(text: CharSequence, before: Boolean, after: Boolean) = SpannableStringBuilder().apply {
            if (before && room > 0) {
                append("￼", Room(room), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                append(' ')
            }
            append(text)
            if (after && room > 0) {
                append(' ')
                append("￼", Room(room), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }
        val first = corner == Corner.TOP_LEFT
        val end = corner == Corner.BOTTOM_RIGHT
        if (entry.last === entry.headline) {
            entry.last.text = withRoom(entry.lastText, first, end)
        } else {
            entry.headline.text = withRoom(entry.headlineText, first, false)
            entry.last.text = withRoom(entry.lastText, false, end)
        }
        entry.headline.setPadding(0, 0, if (corner == Corner.TOP_RIGHT) room else 0, 0)
        entry.column.setPadding(
            0, dp(3f).toInt(), 0,
            bottomRoom() + if (corner == Corner.BOTTOM_LEFT && boxes > 0) boxSize() + dp(2f).toInt() else 0
        )
    }

    /**
     * The turning ring of a box whose word is still being looked for in Anki: a faint circle
     * and three quarters of it turning. It stands still where the system's animations are off.
     */
    private class Ring(private val colour: Int) : Drawable() {
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeCap = Paint.Cap.ROUND
        }
        private var turned = 0f
        private val animator = ValueAnimator.ofFloat(0f, 360f).apply {
            duration = 900
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener {
                turned = it.animatedValue as Float
                invalidateSelf()
            }
        }

        fun start() = animator.start()

        fun stop() = animator.cancel()

        override fun draw(canvas: Canvas) {
            val box = bounds
            val stroke = maxOf(1.6f, box.width() / 9f)
            paint.strokeWidth = stroke
            val oval = RectF(box).apply { inset(stroke, stroke) }
            paint.color = (colour and 0x00FFFFFF) or 0x50000000
            canvas.drawOval(oval, paint)
            paint.color = colour
            canvas.drawArc(oval, turned - 90f, 270f, false, paint)
        }

        override fun getIntrinsicWidth() = 24

        override fun getIntrinsicHeight() = 24

        override fun setAlpha(alpha: Int) {
            paint.alpha = alpha
        }

        override fun setColorFilter(colorFilter: ColorFilter?) = Unit

        @Deprecated("Deprecated in Java")
        override fun getOpacity() = PixelFormat.TRANSLUCENT
    }

    /** The plus of a box whose Anki did not answer: dimmed, with a grey dot in its corner. */
    private class Away(private val plus: Drawable, colour: Int) : Drawable() {
        private val dot = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF8A8AA0.toInt() }

        init {
            plus.setTint(colour)
            plus.alpha = 150
        }

        override fun draw(canvas: Canvas) {
            plus.bounds = bounds
            plus.draw(canvas)
            val radius = bounds.width() * 0.17f
            canvas.drawCircle(bounds.right - radius, bounds.top + radius, radius, dot)
        }

        override fun getIntrinsicWidth() = plus.intrinsicWidth

        override fun getIntrinsicHeight() = plus.intrinsicHeight

        override fun setAlpha(alpha: Int) = Unit

        override fun setColorFilter(colorFilter: ColorFilter?) = Unit

        @Deprecated("Deprecated in Java")
        override fun getOpacity() = PixelFormat.TRANSLUCENT
    }

    /** A soft tint behind a headword not in Anki, and a line under it. */
    private class UnknownMark(private val colour: Int, private val radius: Float) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence?, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let { paint.getFontMetricsInt(it) }
            return paint.measureText(text, start, end).toInt() + (2 * radius).toInt()
        }

        override fun draw(
            canvas: Canvas,
            text: CharSequence?,
            start: Int,
            end: Int,
            x: Float,
            top: Int,
            y: Int,
            bottom: Int,
            paint: Paint
        ) {
            val width = paint.measureText(text, start, end) + 2 * radius
            val textColour = paint.color
            val style = paint.style
            paint.style = Paint.Style.FILL
            paint.color = (colour and 0x00FFFFFF) or 0x30000000
            canvas.drawRoundRect(x, top.toFloat(), x + width, bottom.toFloat(), radius, radius, paint)
            paint.color = (colour and 0x00FFFFFF) or 0xA0000000.toInt()
            val line = maxOf(2f, radius / 2)
            canvas.drawRoundRect(x + radius / 2, bottom - line, x + width - radius / 2, bottom.toFloat(), line / 2, line / 2, paint)
            paint.color = textColour
            paint.style = style
            canvas.drawText(text ?: return, start, end, x + radius, y.toFloat(), paint)
        }
    }

    /** Space at the end of a text for what is drawn over that corner. */
    private class Room(val width: Int) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence?, start: Int, end: Int, fm: Paint.FontMetricsInt?) =
            width

        override fun draw(
            canvas: Canvas,
            text: CharSequence?,
            start: Int,
            end: Int,
            x: Float,
            top: Int,
            y: Int,
            bottom: Int,
            paint: Paint
        ) = Unit
    }

    // The box is a little taller than a line of senses. It stands on the bottom edge of its
    // headword's part, and that part's text ends [bottomRoom] above the edge, so the box
    // reaches no higher than the last line does.
    private fun boxSize() = dp(BOX_DP * textScale / 100f).toInt()

    private fun bottomRoom() = dp(BOTTOM_DP * textScale / 100f).toInt()

    // The button takes touches a little beyond the box it shows, towards the middle of the popup.
    private fun slopLeft() = dp(8f).toInt()

    private fun slopTop() = dp(10f).toInt()

    private fun box(icon: Int, description: Int, colour: Int, corner: Corner) = ImageButton(context).apply {
        setImageResource(icon)
        imageTintList = ColorStateList.valueOf(colour)
        scaleType = ImageView.ScaleType.FIT_CENTER
        contentDescription = context.getString(description)
        val left = if (corner.left) 0 else slopLeft()
        val right = if (corner.left) slopLeft() else 0
        val top = if (corner.top) 0 else slopTop()
        val bottom = if (corner.top) slopTop() else 0
        background = InsetDrawable(boxBackground(colour), left, top, right, bottom)
        val inset = (boxSize() * 0.17f).toInt()
        setPadding(left + inset, top + inset, right + inset, bottom + inset)
    }

    // Filled, not see-through: a box that waits in the popup's corner has text passing under it.
    private fun boxBackground(colour: Int): Drawable {
        fun shape(fill: Int) = GradientDrawable().apply {
            setColor(fill)
            cornerRadius = dp(4f)
            setStroke(dp(1f).toInt().coerceAtLeast(1), (colour and 0x00FFFFFF) or 0xA0000000.toInt())
        }
        return StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_pressed), shape(ReaderStyle.POPUP_BOX_PRESSED))
            addState(intArrayOf(), shape(ReaderStyle.POPUP_BOX))
        }
    }

    /** The word, its reading, how it was inflected, and what frequency and pitch lists say. */
    private fun headline(result: LookupResult, unknown: Boolean = false): CharSequence = SpannableStringBuilder().apply {
        append(result.expression, StyleSpan(Typeface.BOLD), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (unknown && markUnknown) {
            setSpan(UnknownMark(ReaderStyle.POPUP_ACCENT, dp(4f)), 0, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val small = (px(SMALL_SP)).toInt()
        fun addSmall(text: String, colour: Int) {
            val start = length
            append("  ").append(text)
            setSpan(AbsoluteSizeSpan(small), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            setSpan(ForegroundColorSpan(colour), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        if (result.displayReading.isNotEmpty()) addSmall(result.displayReading, ReaderStyle.POPUP_DIM)
        if (result.reasons.isNotEmpty()) addSmall("« " + result.reasons.joinToString(" « "), ReaderStyle.POPUP_ACCENT)
        for (pitch in result.pitches) addSmall(pitch.positions.joinToString(",", "[", "]"), ReaderStyle.POPUP_ACCENT)
        for (frequency in result.frequencies) {
            addSmall("${frequency.dictionary} ${frequency.display}", ReaderStyle.POPUP_DIM)
        }
    }

    /**
     * One line per sense: its number, its tags where they differ from the sense before, its
     * glosses run together, and the dictionary's notes on it. With entries from several
     * dictionaries, each dictionary's senses are headed by its name.
     */
    private fun senses(result: LookupResult): CharSequence = SpannableStringBuilder().apply {
        val small = px(SMALL_SP).toInt()
        fun dim(text: String) {
            val start = length
            append(text)
            setSpan(AbsoluteSizeSpan(small), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            setSpan(ForegroundColorSpan(ReaderStyle.POPUP_DIM), start, length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val entries = result.entries.filterNot(::isFormsList)
        val several = entries.map { it.dictionary }.distinct().size > 1
        var dictionary: String? = null
        var number = 0
        var lastTags = ""
        for (entry in entries) {
            val definitions = GlossaryRenderer.compact(entry.glossaryJson)
            if (definitions.isEmpty()) continue
            if (isNotEmpty()) append('\n')
            if (entry.dictionary != dictionary) {
                dictionary = entry.dictionary
                number = 0
                lastTags = ""
                if (several) {
                    dim(entry.dictionary)
                    append('\n')
                }
            }
            number++
            // Numbers among the tags only repeat the order of the senses.
            val tags = entry.definitionTags.split(' ').filter { it.isNotBlank() && it.toIntOrNull() == null }
                .joinToString(",")
            dim("$number ")
            if (tags.isNotEmpty() && tags != lastTags) dim("$tags ")
            lastTags = tags
            append(definitions.joinToString(" | ") { it.glosses })
            val notes = definitions.map { it.notes }.filter { it.isNotEmpty() }.joinToString("; ")
            if (notes.isNotEmpty()) dim(" ($notes)")
        }
    }

    /** JMdict lists a word's other spellings as an entry of its own; that is not a sense. */
    private fun isFormsList(entry: TermEntry): Boolean =
        entry.definitionTags.split(' ').any { it == "forms" }

    private fun label(text: String, sizeSp: Float, color: Int) = TextView(context).apply {
        this.text = text
        setTextSize(TypedValue.COMPLEX_UNIT_PX, px(sizeSp))
        setTextColor(color)
        includeFontPadding = false
    }

    /** A text size in pixels, with the popup's own scale applied. */
    private fun px(sizeSp: Float): Float =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, sizeSp, resources.displayMetrics) * textScale / 100f

    private fun dp(value: Float) =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, resources.displayMetrics)

    private companion object {
        const val HEAD_SP = 17f
        const val SENSE_SP = 13f
        const val SMALL_SP = 11f
        const val BOX_DP = 24f
        const val BOTTOM_DP = 8f
    }
}
