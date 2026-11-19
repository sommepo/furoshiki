// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

package io.github.sommepo.furoshiki.companion

import android.app.Activity
import android.app.ActivityOptions
import android.content.ComponentName
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.util.TypedValue
import android.view.Display
import android.view.Gravity
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import io.github.sommepo.furoshiki.HomeScreen
import io.github.sommepo.furoshiki.LowerKeys
import io.github.sommepo.furoshiki.R
import io.github.sommepo.furoshiki.ocr.OcrSetup
import io.github.sommepo.furoshiki.ocr.OcrSource
import io.github.sommepo.furoshiki.reader.ReaderSession
import io.github.sommepo.furoshiki.reader.cursor.HoverCursor
import io.github.sommepo.furoshiki.reader.screen.ScreenGeometry
import io.github.sommepo.furoshiki.reader.ui.ReaderHost
import io.github.sommepo.furoshiki.reader.ui.ReaderOverlay
import io.github.sommepo.furoshiki.reader.ui.ScreenTextLayer

/**
 * Furoshiki as the companion of a game another app plays (GameNative, say): the game on the main
 * display, this on the second, where the line read by OCR of the screen stands large, a tap on a
 * word looks it up, and the keys below make a card, open the backlog, read again or set the text
 * area up. The reading session is the game's own as a console's would be: figures, backlog, cards,
 * the names, under the title's ID ([AppTitles]).
 *
 * Android asks the player to allow the capture each time; once it is allowed, the game is started
 * on the main display.
 *
 * Two ways of reading (the *Over game* key): the line large on this display, a tap on a word
 * looking it up here; or **over the game**, a see-through layer over the other app on the main
 * display (Android's *Display over other apps*) drawing the pointer, the words' marks and the
 * popup where the game shows its text, this display then the trackpad that moves the pointer, as
 * with a console's game on a handheld with two screens. Touches on the main display stay the game's.
 *
 * **On a device with one display** (a phone, a tablet) the companion starts on that display, and
 * over the game is the only way: the popup on the game's own screen, a tap on a word of the text
 * read looking it up (a small window over that text and over the open popup catches those taps,
 * every other touch is the game's), and a small button in the corner bringing this page back with
 * its keys. Android's *Display over other apps* is asked for before the capture.
 */
class CompanionActivity : Activity() {
    private lateinit var app: String
    private lateinit var appActivity: String
    private lateinit var gameId: String
    private lateinit var title: String
    private lateinit var status: TextView
    private lateinit var keys: FrameLayout
    private var ocr: OcrSource? = null
    private lateinit var stage: FrameLayout

    /** Over the game: the layer on the main display, while it is there. */
    private var above: ReaderOverlay? = null

    /** What the process had before: put back as the companion ends (a console's game comes after). */
    private val armedBefore = HoverCursor.armed
    private val geometryBefore = ScreenTextLayer.geometryOf
    private var setting = false
    private var launched = false
    private var ended = false

    /**
     * What starts the title's game itself, where its app exported it (GameNative's *Frontend
     * Sync*, a Winlator shortcut: [FrontendExports]); null for a title added by hand, whose app
     * opens as it would from the launcher.
     */
    private var launch: FrontendExports.Launch? = null

    private val frames get() = ScreenCapture.frames

    /** A device with one display: the companion and the game share it. */
    private val oneScreen: Boolean by lazy {
        @Suppress("DEPRECATION")
        val id = if (android.os.Build.VERSION.SDK_INT >= 30) display?.displayId else windowManager.defaultDisplay.displayId
        (id ?: Display.DEFAULT_DISPLAY) == Display.DEFAULT_DISPLAY
    }

    /** One display: the taps on the text read (and on the open popup), and the button back here. */
    private var catcher: android.view.View? = null
    private var catcherParams: WindowManager.LayoutParams? = null
    private var button: android.view.View? = null
    private var waitingForLayer = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Asked to quit, or to make way for another game, with none running: nothing to do here.
        if (intent.getBooleanExtra(EXTRA_QUIT, false) || intent.hasExtra(EXTRA_NEXT_PATH)) {
            intent.getStringExtra(EXTRA_NEXT_PATH)?.let {
                HomeScreen.setNext(this, it, intent.getStringExtra(EXTRA_NEXT_CONSOLE).orEmpty(), intent.getStringExtra(EXTRA_NEXT_GAME).orEmpty())
            }
            HomeScreen.setPlaying(this, null)
            backToShelf()
            return
        }
        val path = intent.getStringExtra(EXTRA_PATH).orEmpty()
        val (named, activity) = AppTitles.appOf(path) ?: run {
            finish()
            return
        }
        app = named
        appActivity = activity
        gameId = intent.getStringExtra(EXTRA_GAME).orEmpty()
        title = intent.getStringExtra(EXTRA_TITLE).orEmpty().ifEmpty { gameId }
        launch = AppTitles.launchOf(this, gameId)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        ReaderSession.start(this, gameId, title)
        HomeScreen.setPlaying(this, path, AppTitles.CONSOLE, gameId)
        ReaderSession.media?.frameSource = { maxWidth, done -> frames.copy(maxWidth, done) }

        val density = resources.displayMetrics.density
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.BLACK)
        }
        status = TextView(this).apply {
            setTextColor(0xFF8A93B5.toInt())
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            typeface = Typeface.MONOSPACE
            letterSpacing = .12f
            gravity = Gravity.CENTER_VERTICAL
            val pad = (16 * density).toInt()
            setPadding(pad, (10 * density).toInt(), pad, (6 * density).toInt())
        }
        root.addView(status, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        // The line and its lookups (the panel holds the line large), or the trackpad over the game.
        stage = FrameLayout(this)
        root.addView(stage, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        // OCR's lines are what is pointed at here, a game script or not; the main display's layer
        // is the whole captured screen, as the places of the text read are fractions of it.
        HoverCursor.armed = { ReaderSession.isActive }
        ScreenTextLayer.geometryOf = { secondary, width, height ->
            // One display: no trackpad, the game's screen is tapped.
            if (secondary) (if (oneScreen) null else ScreenGeometry.padOnly(width, height))
            else ScreenGeometry.fromPicture(intArrayOf(width, height, 0, 0, width, height))
        }
        keys = FrameLayout(this)
        root.addView(keys, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        setContentView(root)

        // Over the game, the capture holds Furoshiki's own layer: what it draws (the popup, the word's
        // mark, the cursor) is left out of the look, so the game moving on is seen with the popup
        // open; the popup then closes, and the new line is read once nothing lies over it (the
        // maintainer, 2026-11-17, Paranormasight: the popup stayed over the next line).
        ocr = OcrSource(this, frames, gameId) { frames.capturing && !setting && !ended }.also { source ->
            source.drawn = {
                val layer = above
                val width = layer?.width ?: 0
                val height = layer?.height ?: 0
                if (layer == null || width <= 0 || height <= 0) emptyList()
                else layer.drawnAreas().map { android.graphics.RectF(it.left / width, it.top / height, it.right / width, it.bottom / height) }
            }
            source.onChanged = { if (above?.popupOpen == true) ReaderSession.dispatch(ReaderSession.Command.CLOSE_POPUP) }
        }
        frames.onChanged = { capturing ->
            showKeys()
            say()
            if (capturing) {
                ocr?.start()
                if (!launched) startGame()
            }
        }
        val saved = getSharedPreferences("game", MODE_PRIVATE)
        over = oneScreen || saved.getBoolean(KEY_OVER, true)
        // The line below by itself (it was the way that was not over the game, before 2026-11-17).
        line = !oneScreen && saved.getBoolean(KEY_LINE, !over)
        applyWay()
        say()
        showKeys()
        when {
            frames.capturing -> frames.onChanged?.invoke(true)
            // One display: the layer over the game first, as there is nowhere else to read.
            oneScreen && !android.provider.Settings.canDrawOverlays(this) -> askLayer()
            else -> askCapture()
        }
    }

    private fun askLayer() {
        waitingForLayer = true
        Toast.makeText(this, R.string.companion_allow_over, Toast.LENGTH_LONG).show()
        runCatching {
            startActivity(Intent(android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION, android.net.Uri.parse("package:$packageName")))
        }
    }

    // ---- Over the game, or the line below ----

    private var over = false

    /**
     * The line read, large on this display, a tap on a word looking it up here: a switch of its own
     * beside *Over game* (the maintainer, 2026-11-17: "the text on bottom should be a separate
     * toggle"; one key had cycled through the line, the layer and nothing). With both off, OCR
     * still counts what is read.
     */
    private var line = false

    /** One of the reader's own screens (the backlog...) opened here: the game is given the keys back after. */
    private var ownScreen = false

    /** Builds this display's overlay, and the main display's layer when reading over the game. */
    private fun applyWay() {
        if (over && !android.provider.Settings.canDrawOverlays(this)) over = false
        closeBacklog()
        stage.removeAllViews()
        removeAbove()
        // The line on this display when its switch is on; else the main display's layer holds the
        // panel (and hides it while the text read has its places), this one the trackpad alone. With
        // one display this page, when it is in front, shows the line as the second display would.
        val host = ReaderHost(this, { line }, onLeaving = { ownScreen = true }, backlog = { showBacklog(true) })
        stage.addView(
            ReaderOverlay(this, if (oneScreen) ReaderHost(this, { true }) else host, secondary = true),
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        )
        if (over) {
            addAbove(host)
            if (oneScreen) addTouches()
        }
        HoverCursor.refresh()
    }

    private fun addAbove(host: ReaderHost) {
        val display = getSystemService(android.hardware.display.DisplayManager::class.java).getDisplay(Display.DEFAULT_DISPLAY) ?: return
        val type = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        val windowContext = createDisplayContext(display).createWindowContext(type, null)
        val context = android.view.ContextThemeWrapper(windowContext, R.style.Theme_Furoshiki_Game)
        val layer = ReaderOverlay(context, host, secondary = false)
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT, type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            android.graphics.PixelFormat.TRANSLUCENT
        )
        params.title = "Furoshiki over the game"
        runCatching { windowContext.getSystemService(WindowManager::class.java).addView(layer, params) }
            .onSuccess { above = layer }
            .onFailure { Toast.makeText(this, it.message ?: "Overlay unavailable", Toast.LENGTH_LONG).show() }
    }

    private fun removeAbove() {
        stage.removeCallbacks(followText)
        for (view in listOfNotNull(catcher, button)) {
            runCatching { (view.context.getSystemService(WINDOW_SERVICE) as WindowManager).removeView(view) }
        }
        catcher = null
        catcherParams = null
        button = null
        val layer = above ?: return
        above = null
        runCatching { (layer.context.getSystemService(WINDOW_SERVICE) as WindowManager).removeView(layer) }
    }

    /**
     * One display: a window over the text read and the open popup that hands its touches to the
     * layer (where they look a word up, or press the popup's buttons), and the button back here.
     */
    private fun addTouches() {
        val layer = above ?: return
        val manager = layer.context.getSystemService(WINDOW_SERVICE) as WindowManager
        val type = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        val taps = object : android.view.View(layer.context) {
            override fun dispatchTouchEvent(event: android.view.MotionEvent): Boolean {
                val target = above ?: return false
                // A touch anywhere else is the game's, and closes the popup as a tap off it would.
                if (event.actionMasked == android.view.MotionEvent.ACTION_OUTSIDE) {
                    if (target.readerArea() != null) ReaderSession.dispatch(ReaderSession.Command.CLOSE_POPUP)
                    return true
                }
                val params = catcherParams ?: return false
                val moved = android.view.MotionEvent.obtain(event)
                moved.offsetLocation(params.x.toFloat(), params.y.toFloat())
                target.dispatchTouchEvent(moved)
                moved.recycle()
                return true
            }
        }
        val params = WindowManager.LayoutParams(
            1, 1, type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            android.graphics.PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            title = "Furoshiki: the text read"
        }
        runCatching { manager.addView(taps, params) }.onSuccess {
            catcher = taps
            catcherParams = params
        }
        // The button back to the companion's page and its keys: small, in the top left corner.
        val density = resources.displayMetrics.density
        val size = (40 * density).toInt()
        val back = TextView(layer.context).apply {
            text = "ふ"
            gravity = Gravity.CENTER
            setTextColor(0xFFECE5D3.toInt())
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
            background = android.graphics.drawable.GradientDrawable().apply {
                shape = android.graphics.drawable.GradientDrawable.OVAL
                setColor(0x99101A3A.toInt())
            }
            setOnClickListener { comeBack() }
        }
        val at = WindowManager.LayoutParams(
            size, size, type,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            android.graphics.PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = (8 * density).toInt()
            // Below the status bar, which stands over every app's layer and would take the touch.
            val bar = resources.getIdentifier("status_bar_height", "dimen", "android")
            y = (if (bar > 0) resources.getDimensionPixelSize(bar) else 0) + (8 * density).toInt()
            title = "Furoshiki: back to the companion"
        }
        runCatching { manager.addView(back, at) }.onSuccess { button = back }
        stage.post(followText)
    }

    /** The catching window follows the text read and the popup, a few times a second. */
    private val followText = object : Runnable {
        override fun run() {
            val layer = above ?: return
            val taps = catcher ?: return
            val params = catcherParams ?: return
            val area = layer.readerArea()
            val touchable = area != null && !area.isEmpty
            val flags = if (touchable) params.flags and WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE.inv()
            else params.flags or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
            val x = area?.left?.toInt() ?: 0
            val y = area?.top?.toInt() ?: 0
            val w = area?.width?.toInt()?.coerceAtLeast(1) ?: 1
            val h = area?.height?.toInt()?.coerceAtLeast(1) ?: 1
            if (flags != params.flags || x != params.x || y != params.y || w != params.width || h != params.height) {
                params.flags = flags
                params.x = x
                params.y = y
                params.width = w
                params.height = h
                runCatching { (taps.context.getSystemService(WINDOW_SERVICE) as WindowManager).updateViewLayout(taps, params) }
            }
            stage.postDelayed(this, FOLLOW_MS)
        }
    }

    /** One display: this page, with the line and its keys, in front of the game. */
    private fun comeBack() {
        startActivity(Intent(this, CompanionActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    /** The *Over game* key: changes the way, asking Android first for the layer over other apps. */
    private fun toggleWay() {
        if (!over && !android.provider.Settings.canDrawOverlays(this)) {
            Toast.makeText(this, R.string.companion_allow_over, Toast.LENGTH_LONG).show()
            runCatching {
                startActivity(Intent(android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION, android.net.Uri.parse("package:$packageName")))
            }
            return
        }
        over = !over
        getSharedPreferences("game", MODE_PRIVATE).edit().putBoolean(KEY_OVER, over).apply()
        applyWay()
        showKeys()
    }

    // ---- The backlog, in this window ----

    private var backlog: android.view.View? = null
    private var backlogView: io.github.sommepo.furoshiki.reader.ui.BacklogView? = null

    /**
     * The backlog over this display's trackpad, in this window: a screen of its own on the second
     * display would take the keys from the game (GameNative pauses then, and the keys were left
     * with no window: "isn't responding"; seen 2026-11-17). *Close* or LOG again puts it away.
     */
    private fun showBacklog(shown: Boolean) {
        if (!shown) {
            closeBacklog()
            showKeys()
            return
        }
        if (backlog != null) return
        val themed = android.view.ContextThemeWrapper(this, R.style.Theme_Reader)
        val view = io.github.sommepo.furoshiki.reader.ui.BacklogView(themed, gameId, searchable = false) {}
        val density = resources.displayMetrics.density
        val sheet = LinearLayout(themed).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(com.google.android.material.color.MaterialColors.getColor(themed, com.google.android.material.R.attr.colorSurface, Color.BLACK))
            isClickable = true
        }
        val close = TextView(themed).apply {
            text = getString(R.string.companion_backlog_close).uppercase()
            setTextColor(0xFFECE5D3.toInt())
            typeface = Typeface.MONOSPACE
            letterSpacing = .12f
            gravity = Gravity.CENTER
            val pad = (12 * density).toInt()
            setPadding(pad, pad, pad, pad)
            setOnClickListener { showBacklog(false) }
        }
        sheet.addView(close, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        sheet.addView(view, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        stage.addView(sheet, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        backlog = sheet
        backlogView = view
        showKeys()
    }

    private fun closeBacklog() {
        backlogView?.release()
        backlogView = null
        backlog?.let { stage.removeView(it) }
        backlog = null
    }

    /** The *Line* key: the line read on this display, or not. */
    private fun toggleLine() {
        line = !line
        getSharedPreferences("game", MODE_PRIVATE).edit().putBoolean(KEY_LINE, line).apply()
        applyWay()
        showKeys()
    }

    /**
     * Once the game plays, this display never takes the controller from it: its window is not
     * focusable, so a tap here (the trackpad, a key) leaves the keys and the window's focus with the
     * game. Before, a tap took them, the game (GameNative) paused as it lost the focus, and the next
     * button brought it back by starting its app again (the maintainer, 2026-11-17: "pressing a
     * seems to pause the game on top after tapping the bottom").
     */
    private fun leaveFocusToGame() {
        if (oneScreen) return
        window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
    }

    override fun onStart() {
        super.onStart()
        ReaderSession.setReading(true)
        // One display: this page in front, the layer over the game stands aside until it is back.
        if (oneScreen) showAbove(false)
    }

    private fun showAbove(shown: Boolean) {
        val visibility = if (shown) android.view.View.VISIBLE else android.view.View.GONE
        for (view in listOfNotNull(above, catcher, button)) view.visibility = visibility
        if (!shown) ReaderSession.dispatch(ReaderSession.Command.CLOSE_POPUP)
    }

    override fun onStop() {
        // One display: the game in front is what is being read, the layer over it shown again.
        if (!oneScreen) ReaderSession.setReading(false) else if (!ended) showAbove(true)
        super.onStop()
    }

    override fun onResume() {
        super.onResume()
        ocr?.reload()
        if (launched && !oneScreen) {
            leaveFocusToGame()
            // Back from the settings or the statistics, which took the keys: the game is given them
            // back (the backlog is shown here, in this window, so as not to take them at all).
            if (ownScreen) {
                ownScreen = false
                frontGame()
            }
        }
        if (waitingForLayer && android.provider.Settings.canDrawOverlays(this)) {
            // One display, back from Android's page with the layer allowed: now the capture.
            waitingForLayer = false
            over = true
            applyWay()
            showKeys()
            if (!frames.capturing) askCapture()
            return
        }
        // Back from Android's page for the layer over other apps: over the game, now it is allowed.
        if (!over && android.provider.Settings.canDrawOverlays(this) && getSharedPreferences("game", MODE_PRIVATE).getBoolean(KEY_OVER, true) && above == null && stage.childCount > 0) {
            over = true
            applyWay()
            showKeys()
        }
    }

    /**
     * One companion at a time (singleTask): Start again on the title in play brings its game back;
     * another title, a console's game or *Quit game* on the shelf end this one first (the
     * maintainer, 2026-11-19: "it's easy to accidentally start several furoshiki instances,
     * especially after quitting gamenative", "not an obvious easy way to come out of gamenative
     * cleanly"). The next game starts from the shelf once this process is gone, as after a game.
     */
    override fun onNewIntent(next: Intent) {
        super.onNewIntent(next)
        val quit = next.getBooleanExtra(EXTRA_QUIT, false)
        val nextPath = next.getStringExtra(EXTRA_NEXT_PATH) ?: next.getStringExtra(EXTRA_PATH)?.takeIf { it != intent.getStringExtra(EXTRA_PATH) }
        if (!quit && nextPath == null) {
            // The same title: its game in front again, or the question about the screen asked again
            // where it was turned down.
            when {
                launched -> frontGame()
                !frames.capturing -> if (oneScreen && !android.provider.Settings.canDrawOverlays(this)) askLayer() else askCapture()
            }
            return
        }
        if (nextPath != null) {
            val console = next.getStringExtra(EXTRA_NEXT_CONSOLE) ?: AppTitles.CONSOLE
            val game = next.getStringExtra(EXTRA_NEXT_GAME) ?: next.getStringExtra(EXTRA_GAME).orEmpty()
            HomeScreen.setNext(this, nextPath, console, game)
        }
        end()
        backToShelf()
    }

    /** This process gone and the shelf in front, which starts the next game where one was asked for. */
    private fun backToShelf() {
        runCatching {
            startActivity(
                Intent(this, io.github.sommepo.furoshiki.HomeActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT),
                ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY).toBundle()
            )
        }
        finishAndRemoveTask()
        window.decorView.postDelayed({ android.os.Process.killProcess(android.os.Process.myPid()) }, 300)
    }

    override fun onDestroy() {
        end()
        super.onDestroy()
    }

    private fun end() {
        if (ended) return
        ended = true
        frames.onChanged = null
        removeAbove()
        HoverCursor.armed = armedBefore
        ScreenTextLayer.geometryOf = geometryBefore
        closeBacklog()
        HoverCursor.refresh()
        ocr?.stop()
        ScreenCapture.stop(this)
        ReaderSession.stop()
        HomeScreen.setPlaying(this, null)
    }

    // ---- The capture and the game ----

    private fun askCapture() {
        val manager = getSystemService(MediaProjectionManager::class.java)
        // The whole screen, never one app: Android 14 offers to share a single app, which would be
        // chosen before the game's app is even open, and OCR would then see nothing of the game.
        val ask = if (android.os.Build.VERSION.SDK_INT >= 34) {
            manager.createScreenCaptureIntent(android.media.projection.MediaProjectionConfig.createConfigForDefaultDisplay())
        } else {
            manager.createScreenCaptureIntent()
        }
        runCatching { startActivityForResult(ask, ASK_CAPTURE) }
            .onFailure { Toast.makeText(this, getString(R.string.companion_no_capture), Toast.LENGTH_LONG).show() }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != ASK_CAPTURE) return
        if (resultCode == RESULT_OK && data != null) {
            ScreenCapture.start(this, resultCode, data)
        } else {
            say()
            showKeys()
        }
    }

    /**
     * A touch on this display took the controller from the game (Android gives the keys to the
     * display touched last), and its B button then closed this page (the maintainer, 2026-11-17,
     * GameNative: "button presses after setting up OCR made the bottom screen go back"). The game
     * is brought to the front again a moment after, as the shelf does from its second display.
     */
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus || !launched || oneScreen || ended) return
        window.decorView.postDelayed({ if (hasWindowFocus() && !ended) frontGame() }, 350)
    }

    /** A controller's buttons are the game's, never this page's (no B closing it meanwhile). */
    override fun dispatchKeyEvent(event: android.view.KeyEvent): Boolean {
        val pad = event.source and (android.view.InputDevice.SOURCE_GAMEPAD or android.view.InputDevice.SOURCE_JOYSTICK) != 0
        if (!oneScreen && (pad || android.view.KeyEvent.isGamepadButton(event.keyCode))) {
            if (event.action == android.view.KeyEvent.ACTION_DOWN && event.repeatCount == 0 && launched) frontGame()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    /** The game's app to the front of the main display again, as it is (no new start). */
    private fun frontGame() {
        val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
            .setComponent(ComponentName(app, appActivity))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_NO_ANIMATION)
        runCatching { startActivity(intent, ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY).toBundle()) }
    }

    /**
     * The game's app on the main display, in front there: the game itself where its title says
     * what starts it ([launch]), otherwise the app as from the launcher (its own library, where
     * the player picks the game).
     */
    private fun startGame() {
        launched = true
        leaveFocusToGame()
        val options = ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY)
        val direct = launch?.let { intentOf(it) }
        if (direct != null) {
            val started = runCatching { startActivity(direct, options.toBundle()) }
            if (started.isSuccess) {
                android.util.Log.i("Furoshiki", "Companion: started ${direct.component?.flattenToShortString()} ${direct.action} itself")
                return
            }
            // An app that no longer takes it (moved, updated away): its library, as by hand.
            android.util.Log.w("Furoshiki", "Companion: the game's own start was refused (${started.exceptionOrNull()?.message}); its app instead")
        }
        val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
            .setComponent(ComponentName(app, appActivity))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED)
        runCatching { startActivity(intent, options.toBundle()) }
            .onFailure { Toast.makeText(this, getString(R.string.companion_no_app, app), Toast.LENGTH_LONG).show() }
    }

    /** The intent [one] says starts its game (an int extra stays an int: GameNative reads `app_id` so). */
    private fun intentOf(one: FrontendExports.Launch): Intent {
        val intent = Intent().setComponent(ComponentName(one.app, one.activity))
        one.action?.let { intent.action = it }
        for ((key, value) in one.ints) intent.putExtra(key, value)
        for ((key, value) in one.strings) intent.putExtra(key, value)
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        if (one.clearTask) intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK)
        return intent
    }

    // ---- What the display says and its keys ----

    private fun say() {
        val what = when {
            frames.capturing -> ""
            else -> getString(R.string.companion_allow)
        }
        status.text = (if (what.isEmpty()) title else "$title  ·  $what").uppercase()
    }

    private fun showKeys() {
        keys.removeAllViews()
        val list = if (frames.capturing) listOf(
            LowerKeys.Key(getString(R.string.companion_key_over), { over }) { toggleWay() }.takeIf { !oneScreen },
            LowerKeys.Key(getString(R.string.lower_key_line), { line }) { toggleLine() }.takeIf { !oneScreen },
            LowerKeys.Key(getString(R.string.lower_key_read)) { ocr?.readNow() },
            // A "?" while the game still has the first guess of where its text is (the lower third).
            LowerKeys.Key(getString(R.string.companion_key_area) + if (ocr?.areaSet == false) " ?" else "") { setUpArea() },
            LowerKeys.Key(getString(R.string.lower_key_stats)) { ReaderSession.toggleStatsPaused() },
            LowerKeys.Key(getString(R.string.lower_key_log), { backlog != null }) { showBacklog(backlog == null) },
            // A game started by itself is not started again (GameNative would be asked to launch
            // it a second time): its app is brought to the front.
            LowerKeys.Key(getString(R.string.companion_key_game)) { if (launched && launch != null) frontGame() else startGame() },
            LowerKeys.Key(getString(R.string.companion_key_end)) { finish() },
        ).filterNotNull() else listOf(
            LowerKeys.Key(getString(R.string.companion_key_allow)) { if (oneScreen && !android.provider.Settings.canDrawOverlays(this)) askLayer() else askCapture() },
            LowerKeys.Key(getString(R.string.companion_key_end)) { finish() },
        )
        keys.addView(LowerKeys.view(this, list))
    }

    /** The text area drawn over the screen as it stands (the same setup as over a console's game). */
    private fun setUpArea() {
        val source = ocr ?: return
        if (setting || frames.width <= 0) return
        setting = true
        frames.copy(frames.width) { bitmap ->
            if (bitmap == null || ended) {
                setting = false
                return@copy
            }
            val whole = android.graphics.Rect(0, 0, bitmap.width, bitmap.height)
            OcrSetup(
                this, bitmap, whole, source.game,
                read = { areas -> source.read(bitmap, whole, areas) },
                onSave = {
                    source.setGame(it)
                    showKeys()
                },
                onClosed = {
                    setting = false
                    // The box took the keys: the game has them again.
                    if (!oneScreen) frontGame()
                },
            ).show()
        }
    }

    companion object {
        const val EXTRA_PATH = "path"
        const val EXTRA_GAME = "game"
        const val EXTRA_TITLE = "title"
        /** Quit the title in play (the shelf's *Quit game*). */
        const val EXTRA_QUIT = "quit"
        /** Another game asked for while this one plays: started once this one has ended. */
        const val EXTRA_NEXT_PATH = "next_path"
        const val EXTRA_NEXT_CONSOLE = "next_console"
        const val EXTRA_NEXT_GAME = "next_game"
        private const val ASK_CAPTURE = 41
        private const val KEY_OVER = "companion_over"
        private const val KEY_LINE = "companion_line"
        private const val FOLLOW_MS = 200L
    }
}
