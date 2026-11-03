// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.reader

import android.content.Context
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.util.concurrent.Executors
import org.citra.citra_emu.reader.ReaderLog as Log
import org.citra.citra_emu.reader.cursor.HoverCursor
import org.citra.citra_emu.reader.screen.Area
import org.citra.citra_emu.reader.screen.ScreenGeometry
import org.citra.citra_emu.reader.ui.ScreenTextLayer
import org.citra.citra_emu.utils.PermissionsHandler

/**
 * What ties the reading layer, which every reader fork shares, to this app; the fork's own file.
 * The emulation screen (fragments/EmulationFragment) makes the reader's layer, starts and stops
 * the session, and the emulator's own second display (display/SecondaryDisplay) puts a layer
 * of its own over the lower screen there, at the places marked "Reader fork"; this is the rest.
 *
 * The 3DS has two screens, and the emulator draws them itself, on one display or on two: so
 * where they are comes from the emulator ([geometry]), and the lower screen, the game's touch
 * screen, is also where the cursor is steered from while its button is held.
 *
 * Every emulated button, a controller's, the on-screen controller's and the trigger axes', is
 * handed to [onButton] by the emulator's own numbers (NativeLibrary.ButtonType) on its way to
 * the game, and the sticks to [onStick].
 */
object ReaderGlue {
    private val worker = Executors.newSingleThreadExecutor { Thread(it, "ReaderGlue").apply { isDaemon = true } }

    /** The application has started: the reader knows it from here on. */
    @JvmStatic
    fun onApplication(context: Context) {
        val app = context.applicationContext
        ReaderNative.appContext = app
        noneIsMinusOne(app)
        ScreenTextLayer.geometryOf = { secondary, _, _ ->
            try {
                geometry(screenRects(secondary))
            } catch (_: UnsatisfiedLinkError) {
                null // no emulator behind this (off-device tests)
            }
        }
        worker.execute { moveUserProfiles(app) }
    }

    /** The reader's name for a game: its title ID as 16 capitals, or "" for none. */
    @JvmStatic
    fun gameId(titleId: Long): String = if (titleId == 0L) "" else "%016X".format(titleId)

    // ---- The two screens ----

    /**
     * Where the console's screens are drawn on the main display surface, or on the second
     * display's when [secondary] (jni/native.cpp); see [geometry] for the values.
     */
    private external fun screenRects(secondary: Boolean): IntArray?

    /**
     * Reads what [screenRects] returns: width, height, rotated, then for the upper screen, the
     * lower screen and the extra copy of a hybrid layout: enabled, left, top, right, bottom,
     * where the extra copy has one more value after "enabled" saying that it shows the lower
     * screen. The lower screen is also the trackpad. Null when there is nothing to map: no
     * surface, or the upright orientation, in which the screens are drawn turned by a quarter.
     */
    fun geometry(values: IntArray?): ScreenGeometry? {
        if (values == null || values.size < 19) return null
        if (values[2] == 0) return null
        fun area(at: Int): Area? = Area(
            values[at].toFloat(),
            values[at + 1].toFloat(),
            values[at + 2].toFloat(),
            values[at + 3].toFloat()
        ).takeUnless { it.isEmpty }
        val upper = ArrayList<Area>(2)
        val lower = ArrayList<Area>(2)
        if (values[3] != 0) area(4)?.let { upper.add(it) }
        if (values[8] != 0) area(9)?.let { lower.add(it) }
        if (values[13] != 0) area(15)?.let { (if (values[14] != 0) lower else upper).add(it) }
        return ScreenGeometry(values[0], values[1], game = upper, pad = lower, lower = lower)
    }

    // ---- The buttons ----

    /** Buttons whose press the reader took: their release is the reader's too. */
    private val taken = HashSet<Int>()

    /** Whether a game is being read, and what the statistics' button does. Replaceable for tests. */
    internal var reading: () -> Boolean = { ReaderSession.isActive }
    internal var onStatsButton: () -> Unit = { ReaderSession.toggleStatsPaused() }

    /**
     * An emulated button went down or up. True when the reader takes it and the game must not
     * hear of it: the cursor's button while there is text to point at (HoverCursor), the
     * statistics' and the next choice's buttons (ReaderInput's) while a game is read.
     */
    @JvmStatic
    fun onButton(button: Int, pressed: Boolean, source: Int): Boolean {
        if (HoverCursor.onButton(button, pressed, source)) return true
        synchronized(taken) {
            if (!pressed) return taken.remove(button)
            if (button in taken) return true // a held key repeats
            if (button < 0 || !reading()) return false
            when (button) {
                ReaderInput.statsButton -> onStatsButton()
                ReaderInput.nextChoiceButton -> NextChoice.toggle()
                else -> return false
            }
            taken.add(button)
            return true
        }
    }

    /**
     * A stick moved ([stick] by the emulator's numbers, [x] and [y] from -1 to 1). The game is
     * told of it all the same: while the cursor has the sticks the emulator shows the game
     * them at rest (ReaderNative.holdSticks), and lets it see where they are when it lets go.
     */
    @JvmStatic
    fun onStick(stick: Int, x: Float, y: Float): Boolean {
        HoverCursor.onStick(stick, x, y)
        return false
    }

    /**
     * Until 2026-10-17 this app wrote "no button" as 0, the other reader apps as -1, which the
     * shared settings offer now: a 0 kept from then is made -1 once.
     */
    private fun noneIsMinusOne(context: Context) {
        val preferences = ReaderSettings(context).preferences
        val keys = listOf(ReaderSettings.KEY_CURSOR_BUTTON, ReaderSettings.KEY_STATS_BUTTON, ReaderSettings.KEY_NEXT_CHOICE_BUTTON)
        val edit = preferences.edit()
        var changed = false
        for (key in keys) {
            if (preferences.getString(key, null) == "0") {
                edit.putString(key, "-1")
                changed = true
            }
        }
        if (changed) edit.apply()
    }

    // ---- The user's own scripts ----

    /**
     * The user's own game scripts were kept in the user folder's reader/profiles until
     * 2026-10-17; since then they are in the app's own folder, as in the other reader apps
     * (Reader settings, Supported games, Install from a file). What is in the old folder is
     * copied over once, never over a file of the same name.
     */
    private fun moveUserProfiles(context: Context) {
        val settings = ReaderSettings(context)
        if (settings.preferences.getBoolean(KEY_PROFILES_MOVED, false)) return
        val target = context.getExternalFilesDir(null)?.let { File(it, ReaderNative.PROFILE_USER_DIR) } ?: return
        try {
            val root = PermissionsHandler.citraDirectory
            if (root.toString().isEmpty()) return
            val old = DocumentFile.fromTreeUri(context, root)
                ?.findFile("reader")?.findFile("profiles")
            if (old != null && old.isDirectory) {
                for (file in old.listFiles()) {
                    val name = file.name ?: continue
                    if (!file.isFile || name.contains('/') || !Console.GAME_ID.matchesAt(name, 0)) continue
                    val copy = File(target, name)
                    if (copy.exists()) continue
                    target.mkdirs()
                    context.contentResolver.openInputStream(file.uri)?.use { input ->
                        copy.outputStream().use { input.copyTo(it) }
                    }
                    Log.info("[Reader] Moved the user's $name to the app's folder")
                }
            }
            settings.preferences.edit().putBoolean(KEY_PROFILES_MOVED, true).apply()
        } catch (e: Exception) {
            Log.warning("[Reader] The user's own scripts could not be moved: ${e.message}")
        }
    }

    private const val KEY_PROFILES_MOVED = "reader_profiles_moved"
}
