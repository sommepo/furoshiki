// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader.ui

import android.animation.ArgbEvaluator
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Presentation
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.hardware.display.DisplayManager
import android.os.BatteryManager
import android.util.TypedValue
import android.view.Display
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.animation.DecelerateInterpolator
import android.view.animation.OvershootInterpolator
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.RelativeLayout
import android.widget.TextClock
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import org.citra.citra_emu.R
import org.citra.citra_emu.model.Game

/**
 * The game list in the manner of the 3DS's own HOME menu, which is two screens: the upper one
 * shows the chosen game (its picture, slowly bobbing, its name and maker, the date, the time
 * and the battery along the top), the lower one has the games' icons as tiles in a grid, the
 * chosen one in a frame that glows, its name in a bubble and a wide "Open" button.
 *
 * On a device with two displays the lower screen goes on the other display, as a window of
 * its own that takes touches; with one display the two are one above the other with the
 * hinge between. As on the console, a touch chooses a game (it hops) and a second touch on
 * it, or one on "Open", starts it; a controller moves the choice and its button starts it.
 *
 * It stands in the games screen's place rather than dressing it: the screen's own list is
 * put away while this is on, and comes back when it is switched off (Reader, Game list).
 */
class ThreeDsHome(
    /** Where the games screen keeps its list, and the list itself. */
    private val holder: RelativeLayout,
    private val ownList: View,
    private val host: Host
) {
    /** What the games screen does for this look. */
    class Host(
        val loadIcon: (Game, ImageView) -> Unit,
        val launch: (Game) -> Unit,
        /** The game's own page of properties (a long touch, or a controller's X). */
        val details: (Game) -> Unit,
        val readable: (Game) -> Boolean
    )

    var enabled = false
        private set

    private val context = holder.context
    private var games: List<Game> = emptyList()
    private var chosen = -1

    private var stack: LinearLayout? = null
    private var upper: FrameLayout? = null
    private var lower: FrameLayout? = null
    private var window: Presentation? = null
    private var grid: RecyclerView? = null
    private var bubble: TextView? = null
    private var open: TextView? = null
    private var bannerIcon: ImageView? = null
    private var bannerTitle: TextView? = null
    private var bannerMaker: TextView? = null
    private var bannerNote: TextView? = null
    private var battery: TextView? = null
    private var bob: ObjectAnimator? = null
    private var insetTop = 0
    private var insetBottom = 0
    private var ownListShown = View.VISIBLE
    private var tile = 0

    /** The chosen tile's frame brightens and dims, as the console's cursor does. */
    private val pulse = ValueAnimator.ofFloat(0f, 1f).apply {
        duration = 850L
        repeatCount = ValueAnimator.INFINITE
        repeatMode = ValueAnimator.REVERSE
        val blend = ArgbEvaluator()
        addUpdateListener {
            val colour = blend.evaluate(
                it.animatedValue as Float,
                colour(R.color.reader_3ds_cursor),
                colour(R.color.reader_3ds_cursor_light)
            ) as Int
            chosenTile()?.let { tile -> (tile.background as? GradientDrawable)?.setStroke(dp(3.5f), colour) }
        }
    }

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, context.resources.displayMetrics).toInt()

    private fun colour(id: Int) = ContextCompat.getColor(context, id)

    private fun ground() = GradientDrawable(
        GradientDrawable.Orientation.TOP_BOTTOM,
        intArrayOf(colour(R.color.reader_3ds_background_top), colour(R.color.reader_3ds_background_bottom))
    )

    /** True while the lower screen is on a display of its own. */
    val onTwoDisplays: Boolean get() = window != null

    /** The two screens, for whoever looks at them (tests). */
    val upperScreen: View? get() = upper
    val lowerScreen: View? get() = lower

    /** The games to show, as the games screen has them. */
    fun submit(list: List<Game>) {
        val was = games.getOrNull(chosen)?.path
        games = list
        chosen = list.indexOfFirst { it.path == was }
        grid?.adapter?.notifyDataSetChanged()
        show()
    }

    /** Puts the look on or takes it off. */
    fun set(on: Boolean) {
        if (on == enabled) return
        enabled = on
        if (on) {
            ownListShown = ownList.visibility
            ownList.visibility = View.GONE
            build()
            resume()
        } else {
            pause()
            pulse.cancel()
            bob?.cancel()
            bob = null
            stack?.let { holder.removeView(it) }
            upper?.let { (it.parent as? ViewGroup)?.removeView(it) }
            stack = null
            upper = null
            lower = null
            grid = null
            bubble = null
            open = null
            bannerIcon = null
            bannerTitle = null
            bannerMaker = null
            bannerNote = null
            battery = null
            ownList.visibility = ownListShown
        }
    }

    /** The parts of the screen the system's bars and the app's own bar take. */
    fun onInsets(top: Int, bottom: Int) {
        insetTop = top
        insetBottom = bottom
        place()
    }

    /** The games screen is in front: the lower screen goes to the other display if there is one. */
    fun resume() {
        if (!enabled) return
        battery?.text = batteryText()
        val other = otherDisplay()
        val screen = lower ?: return
        if (other != null && window == null) {
            (screen.parent as? ViewGroup)?.removeView(screen)
            stack?.visibility = View.GONE
            upper?.let { top ->
                (top.parent as? ViewGroup)?.removeView(top)
                holder.addView(top, RelativeLayout.LayoutParams(MATCH, MATCH))
            }
            window = try {
                LowerScreen(context, other, screen).also { it.show() }
            } catch (_: WindowManager.InvalidDisplayException) {
                null
            }
            if (window == null) together()
        } else if (other == null && window == null && screen.parent == null) {
            together()
        }
        place()
        upper?.requestFocus()
        if (bob?.isStarted != true) bob?.start()
    }

    /** The games screen is left (a game starts, another screen opens): the other display is given back. */
    fun pause() {
        window?.let {
            try {
                it.dismiss()
            } catch (_: IllegalArgumentException) {
            }
        }
        if (window != null) {
            window = null
            if (enabled) together()
        }
        bob?.cancel()
    }

    /** Both screens in the games screen's place, one above the other. */
    private fun together() {
        val column = stack ?: return
        val top = upper ?: return
        val bottom = lower ?: return
        (top.parent as? ViewGroup)?.removeView(top)
        (bottom.parent as? ViewGroup)?.removeView(bottom)
        column.removeAllViews()
        column.addView(top, LinearLayout.LayoutParams(MATCH, 0, 9f))
        column.addView(
            View(context).apply { setBackgroundColor(colour(R.color.reader_3ds_hinge)) },
            LinearLayout.LayoutParams(MATCH, dp(10f))
        )
        column.addView(bottom, LinearLayout.LayoutParams(MATCH, 0, 11f))
        column.visibility = View.VISIBLE
    }

    private fun otherDisplay(): Display? {
        val manager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        val mine = holder.display?.displayId ?: Display.DEFAULT_DISPLAY
        return manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .firstOrNull { it.displayId != mine && it.state != Display.STATE_OFF }
    }

    // ---- The two screens ----

    private fun build() {
        val ink = colour(R.color.reader_3ds_text)
        stack = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            holder.addView(this, RelativeLayout.LayoutParams(MATCH, MATCH))
        }

        // The upper screen: a strip with the date, the time and the battery, and the chosen game.
        upper = FrameLayout(context).apply {
            tag = TAG_UPPER
            background = ground()
            isFocusable = true
            isFocusableInTouchMode = true
            setOnKeyListener { _, code, event -> event.action == KeyEvent.ACTION_DOWN && key(code) }
        }
        val strip = LinearLayout(context).apply {
            tag = TAG_STRIP
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setBackgroundColor(colour(R.color.reader_3ds_bar))
            setPadding(dp(16f), dp(5f), dp(16f), dp(5f))
            addView(
                TextClock(context).apply {
                    format12Hour = "M/d (EEE)"
                    format24Hour = "M/d (EEE)"
                    textSize = 14f
                    setTextColor(ink)
                },
                LinearLayout.LayoutParams(0, WRAP, 1f)
            )
            addView(
                TextClock(context).apply {
                    format12Hour = "h:mm a"
                    format24Hour = "H:mm"
                    textSize = 14f
                    setTextColor(ink)
                },
                LinearLayout.LayoutParams(WRAP, WRAP)
            )
            battery = TextView(context).apply {
                textSize = 13f
                setTextColor(ink)
                text = batteryText()
            }
            addView(battery, LinearLayout.LayoutParams(WRAP, WRAP).apply { marginStart = dp(14f) })
        }
        upper!!.addView(strip, FrameLayout.LayoutParams(MATCH, WRAP, Gravity.TOP))
        val banner = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
        }
        val words = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        bannerIcon = ImageView(context).apply {
            scaleType = ImageView.ScaleType.CENTER_CROP
            background = GradientDrawable().apply {
                cornerRadius = dp(18f).toFloat()
                setColor(colour(R.color.reader_3ds_tile))
            }
            setPadding(dp(8f), dp(8f), dp(8f), dp(8f))
            elevation = dp(6f).toFloat()
            visibility = View.INVISIBLE
        }
        banner.addView(bannerIcon, LinearLayout.LayoutParams(dp(132f), dp(132f)))
        bannerTitle = TextView(context).apply {
            tag = TAG_TITLE
            textSize = 22f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            maxLines = 2
            setTextColor(ink)
            text = context.getString(R.string.reader_home_3ds_choose)
        }
        words.addView(bannerTitle, LinearLayout.LayoutParams(MATCH, WRAP))
        bannerMaker = TextView(context).apply {
            textSize = 15f
            gravity = Gravity.CENTER
            maxLines = 1
            setTextColor(ink)
            alpha = 0.75f
        }
        words.addView(bannerMaker, LinearLayout.LayoutParams(MATCH, WRAP).apply { topMargin = dp(4f) })
        bannerNote = TextView(context).apply {
            textSize = 13f
            gravity = Gravity.CENTER
            setTextColor(colour(R.color.reader_3ds_cursor))
            typeface = Typeface.DEFAULT_BOLD
        }
        words.addView(bannerNote, LinearLayout.LayoutParams(MATCH, WRAP).apply { topMargin = dp(8f) })
        banner.addView(words, LinearLayout.LayoutParams(MATCH, WRAP).apply { topMargin = dp(16f) })
        upper!!.addView(
            banner,
            FrameLayout.LayoutParams(MATCH, WRAP, Gravity.CENTER).apply {
                marginStart = dp(24f)
                marginEnd = dp(24f)
            }
        )
        // A screen of its own has room for a large picture over the name; half a small
        // display has not, and gets a smaller picture with the name beside it.
        upper!!.addOnLayoutChangeListener { view, _, top, _, bottom, _, oldTop, _, oldBottom ->
            if (bottom - top == oldBottom - oldTop) return@addOnLayoutChangeListener
            val room = bottom - top - view.paddingTop - view.paddingBottom - strip.height
            val low = room < dp(300f)
            val size = if (low) (room - dp(40f)).coerceIn(dp(56f), dp(132f)) else (room * 0.34f).toInt().coerceIn(dp(110f), dp(230f))
            view.post {
                banner.orientation = if (low) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
                banner.gravity = if (low) Gravity.CENTER_VERTICAL else Gravity.CENTER_HORIZONTAL
                bannerIcon?.layoutParams = LinearLayout.LayoutParams(size, size)
                words.layoutParams = if (low) {
                    LinearLayout.LayoutParams(0, WRAP, 1f).apply { marginStart = dp(22f) }
                } else {
                    LinearLayout.LayoutParams(MATCH, WRAP).apply { topMargin = dp(16f) }
                }
                val side = if (low) Gravity.START else Gravity.CENTER
                listOfNotNull(bannerTitle, bannerMaker, bannerNote).forEach { it.gravity = side }
                bannerTitle?.textSize = if (low) 20f else 24f
                (banner.layoutParams as FrameLayout.LayoutParams).let {
                    it.topMargin = strip.height / 2
                    it.marginStart = if (low) dp(48f) else dp(24f)
                    banner.layoutParams = it
                }
            }
        }
        // The chosen game's picture floats, as the console's banners move.
        bob = ObjectAnimator.ofFloat(bannerIcon, View.TRANSLATION_Y, -dp(5f).toFloat(), dp(5f).toFloat()).apply {
            duration = 1900L
            repeatCount = ValueAnimator.INFINITE
            repeatMode = ValueAnimator.REVERSE
        }

        // The lower screen: the chosen game's name, the tiles, "Open".
        lower = FrameLayout(context).apply {
            tag = TAG_LOWER
            background = ground()
        }
        val column = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        bubble = TextView(context).apply {
            tag = TAG_BUBBLE
            textSize = 16f
            maxLines = 1
            includeFontPadding = false
            gravity = Gravity.CENTER
            setTextColor(ink)
            setPadding(dp(22f), 0, dp(22f), 0)
            background = GradientDrawable().apply {
                cornerRadius = dp(20f).toFloat()
                setColor(colour(R.color.reader_3ds_tile))
                setStroke(dp(1.5f), colour(R.color.reader_3ds_bubble_edge))
            }
            visibility = View.INVISIBLE
        }
        column.addView(
            bubble,
            LinearLayout.LayoutParams(WRAP, dp(34f)).apply {
                gravity = Gravity.CENTER_HORIZONTAL
                topMargin = dp(10f)
                bottomMargin = dp(6f)
            }
        )
        grid = RecyclerView(context).apply {
            tag = TAG_GRID
            layoutManager = GridLayoutManager(context, 4)
            adapter = Tiles()
            clipToPadding = false
            setPadding(dp(12f), 0, dp(12f), dp(6f))
            overScrollMode = View.OVER_SCROLL_NEVER
            addOnLayoutChangeListener { _, left, _, right, _, oldLeft, _, oldRight, _ ->
                if (right - left != oldRight - oldLeft) post { columns() }
            }
        }
        column.addView(grid, LinearLayout.LayoutParams(MATCH, 0, 1f))
        open = TextView(context).apply {
            tag = TAG_OPEN
            text = context.getString(R.string.reader_home_3ds_open)
            textSize = 20f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            setTextColor(ink)
            background = GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                intArrayOf(colour(R.color.reader_3ds_open_top), colour(R.color.reader_3ds_open_bottom))
            ).apply {
                cornerRadius = dp(30f).toFloat()
                setStroke(dp(2f), colour(R.color.reader_3ds_cursor))
            }
            elevation = dp(3f).toFloat()
            alpha = 0.45f
            setOnClickListener { start() }
            setOnTouchListener { view, event -> give(view, event); false }
        }
        column.addView(
            open,
            LinearLayout.LayoutParams(dp(260f), dp(52f)).apply {
                gravity = Gravity.CENTER_HORIZONTAL
                topMargin = dp(4f)
                bottomMargin = dp(10f)
            }
        )
        lower!!.addView(column, FrameLayout.LayoutParams(MATCH, MATCH))
        together()
        show()
    }

    private fun place() {
        // On two displays the upper screen is all of this one, the lower all of the other;
        // together, the upper keeps clear of the system's bar and the lower of the app's own.
        upper?.setPadding(0, insetTop, 0, if (onTwoDisplays) insetBottom else 0)
        lower?.setPadding(0, 0, 0, if (onTwoDisplays) 0 else insetBottom)
    }

    private fun batteryText(): String {
        val level = try {
            val sticky = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val now = sticky?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
            val full = sticky?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
            if (now >= 0 && full > 0) now * 100 / full else -1
        } catch (_: Exception) {
            -1
        }
        return if (level >= 0) "$level%" else ""
    }

    /** As many columns as tiles of the console's size fit. */
    private fun columns() {
        val list = grid ?: return
        val room = list.width - list.paddingLeft - list.paddingRight
        if (room <= 0) return
        val count = (room / dp(104f)).coerceIn(3, 10)
        val manager = list.layoutManager as GridLayoutManager
        val size = room / count - 2 * dp(MARGIN) - 2 * dp(PADDING)
        if (manager.spanCount != count || size != tile) {
            manager.spanCount = count
            tile = size
            list.adapter?.notifyDataSetChanged()
        }
    }

    // ---- Choosing and starting ----

    /** What both screens say of the choice. */
    private fun show() {
        val game = games.getOrNull(chosen)
        bannerTitle?.text = game?.let(::name) ?: context.getString(R.string.reader_home_3ds_choose)
        bannerMaker?.text = game?.company ?: ""
        bannerNote?.text = if (game != null && host.readable(game)) context.getString(R.string.reader_home_3ds_readable) else ""
        bannerIcon?.let { icon ->
            if (game == null) {
                icon.visibility = View.INVISIBLE
            } else {
                host.loadIcon(game, icon)
                icon.visibility = View.VISIBLE
            }
        }
        bubble?.apply {
            text = game?.let(::name) ?: ""
            visibility = if (game == null) View.INVISIBLE else View.VISIBLE
        }
        open?.alpha = if (game == null) 0.45f else 1f
    }

    private fun name(game: Game): String = game.title.replace(Regex("[\\t\\n\\r]+"), " ")

    private fun chosenTile(): View? = grid?.findViewHolderForAdapterPosition(chosen)?.itemView?.findViewWithTag(TAG_TILE)

    /** Makes the game at [index] the chosen one: its tile hops, the screens say its name. */
    fun choose(index: Int) {
        if (index !in games.indices || index == chosen) return
        chosenTile()?.let { frame(it, false) }
        chosen = index
        show()
        grid?.scrollToPosition(index)
        grid?.post {
            chosenTile()?.let { tile ->
                frame(tile, true)
                tile.animate().cancel()
                tile.scaleX = 0.88f
                tile.scaleY = 0.88f
                tile.animate().scaleX(1f).scaleY(1f).setDuration(260L).setInterpolator(OvershootInterpolator(3f)).start()
            }
        }
        if (!pulse.isStarted) pulse.start()
        bubble?.apply {
            animate().cancel()
            scaleX = 0.85f
            scaleY = 0.85f
            animate().scaleX(1f).scaleY(1f).setDuration(160L).start()
        }
        bannerIcon?.apply {
            animate().cancel()
            scaleX = 0.8f
            scaleY = 0.8f
            animate().scaleX(1f).scaleY(1f).setDuration(240L).setInterpolator(OvershootInterpolator(2f)).start()
        }
    }

    /** The chosen game's index, or -1. */
    val chosenIndex: Int get() = chosen

    /** Starts the chosen game. */
    fun start() {
        games.getOrNull(chosen)?.let { host.launch(it) }
    }

    /** A touch on the tile at [index]: the chosen one starts, another becomes the chosen one. */
    private fun tap(index: Int) {
        if (index == chosen) start() else choose(index)
    }

    /** A controller's or a keyboard's key, from the upper screen. True when it was this menu's. */
    fun key(code: Int): Boolean {
        if (!enabled || games.isEmpty()) return false
        val span = (grid?.layoutManager as? GridLayoutManager)?.spanCount ?: 4
        val at = chosen
        val to = when (code) {
            KeyEvent.KEYCODE_DPAD_LEFT -> if (at < 0) 0 else if (at % span == 0) return false else at - 1
            KeyEvent.KEYCODE_DPAD_RIGHT -> if (at < 0) 0 else (at + 1).coerceAtMost(games.size - 1)
            KeyEvent.KEYCODE_DPAD_UP -> if (at < 0) 0 else if (at - span < 0) return false else at - span
            KeyEvent.KEYCODE_DPAD_DOWN ->
                if (at < 0) 0 else if (at + span <= games.size - 1) at + span
                else if (at / span < (games.size - 1) / span) games.size - 1 else return false
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_DPAD_CENTER,
            KeyEvent.KEYCODE_NUMPAD_ENTER -> {
                if (at < 0) return false
                start()
                return true
            }
            KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y -> {
                games.getOrNull(at)?.let { host.details(it) } ?: return false
                return true
            }
            else -> return false
        }
        choose(to)
        return true
    }

    private fun frame(tile: View, on: Boolean) {
        (tile.background as? GradientDrawable)?.setStroke(if (on) dp(3.5f) else 0, colour(R.color.reader_3ds_cursor))
    }

    /** What is pressed gives a little, and comes back. */
    private fun give(view: View, event: MotionEvent) {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN ->
                view.animate().scaleX(0.94f).scaleY(0.94f).setDuration(60L).start()
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL ->
                view.animate().scaleX(1f).scaleY(1f).setDuration(140L)
                    .setInterpolator(DecelerateInterpolator()).start()
        }
    }

    private inner class Tiles : RecyclerView.Adapter<Tiles.Holder>() {
        inner class Holder(view: View, val tile: FrameLayout, val icon: ImageView) : RecyclerView.ViewHolder(view)

        override fun getItemCount() = games.size

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
            val icon = ImageView(context).apply { scaleType = ImageView.ScaleType.CENTER_CROP }
            val tile = FrameLayout(context).apply {
                tag = TAG_TILE
                background = GradientDrawable().apply {
                    cornerRadius = dp(14f).toFloat()
                    setColor(colour(R.color.reader_3ds_tile))
                }
                elevation = dp(2f).toFloat()
                setPadding(dp(PADDING), dp(PADDING), dp(PADDING), dp(PADDING))
                addView(icon, FrameLayout.LayoutParams(dp(64f), dp(64f)))
                setOnTouchListener { view, event -> give(view, event); false }
            }
            val cell = FrameLayout(context).apply {
                layoutParams = RecyclerView.LayoutParams(MATCH, WRAP)
                addView(
                    tile,
                    FrameLayout.LayoutParams(WRAP, WRAP, Gravity.CENTER).apply {
                        setMargins(dp(MARGIN), dp(MARGIN), dp(MARGIN), dp(MARGIN))
                    }
                )
            }
            return Holder(cell, tile, icon)
        }

        override fun onBindViewHolder(holder: Holder, position: Int) {
            val game = games[position]
            val size = if (tile > 0) tile else dp(64f)
            holder.icon.layoutParams = holder.icon.layoutParams.apply {
                width = size
                height = size
            }
            host.loadIcon(game, holder.icon)
            ReaderBadge.mark(holder.icon, String.format("%016X", game.titleId))
            holder.tile.contentDescription = name(game)
            holder.tile.scaleX = 1f
            holder.tile.scaleY = 1f
            frame(holder.tile, position == chosen)
            holder.tile.setOnClickListener { tap(holder.bindingAdapterPosition) }
            holder.tile.setOnLongClickListener {
                games.getOrNull(holder.bindingAdapterPosition)?.let { host.details(it) }
                true
            }
        }
    }

    /** The lower screen as a window of its own on the other display: touched, never focused. */
    private class LowerScreen(context: Context, display: Display, private val screen: View) :
        Presentation(context, display) {
        override fun onCreate(savedInstanceState: android.os.Bundle?) {
            super.onCreate(savedInstanceState)
            window?.setFlags(
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
            )
            setContentView(screen, ViewGroup.LayoutParams(MATCH, MATCH))
        }
    }

    companion object {
        /**
         * The settings of Reader settings' Game list page (reader_prefs_console.xml): the game
         * list as the HOME menu, and only the games that can be read.
         */
        const val KEY_HOME_3DS = "reader_home_3ds"
        const val KEY_READABLE_ONLY = "reader_readable_only"

        fun shown(context: android.content.Context): Boolean =
            org.citra.citra_emu.reader.ReaderSettings(context).preferences.getBoolean(KEY_HOME_3DS, true)

        fun readableOnly(context: android.content.Context): Boolean =
            org.citra.citra_emu.reader.ReaderSettings(context).preferences.getBoolean(KEY_READABLE_ONLY, true)

        private const val WRAP = ViewGroup.LayoutParams.WRAP_CONTENT
        private const val MATCH = ViewGroup.LayoutParams.MATCH_PARENT
        private const val MARGIN = 7f
        private const val PADDING = 8f

        /** What the parts are tagged with. */
        const val TAG_UPPER = "reader-3ds-upper"
        const val TAG_LOWER = "reader-3ds-lower"
        const val TAG_STRIP = "reader-3ds-strip"
        const val TAG_TITLE = "reader-3ds-title"
        const val TAG_BUBBLE = "reader-3ds-bubble"
        const val TAG_GRID = "reader-3ds-grid"
        const val TAG_OPEN = "reader-3ds-open"
        const val TAG_TILE = "reader-3ds-tile"
    }
}
