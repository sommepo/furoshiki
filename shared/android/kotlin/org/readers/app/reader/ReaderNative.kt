// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Eden Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.annotation.Keep
import java.io.File
import java.io.IOException
import org.readers.app.reader.ReaderLog as Log

/**
 * The bridge to the text engine in the emulator's native library (its reader_jni.cpp).
 * The functions marked external are answered there; the others are called from there, on a
 * thread of the bridge's own.
 */
object ReaderNative {
    /** Tells the native side where to send what the engine reports. Once is enough. */
    external fun attach()

    /** Loads the running game's profile again (after it was edited). */
    external fun reloadProfile()

    /** Keeps the last [seconds] of the game's sound, for a card's audio. */
    external fun setAudioCapture(enabled: Boolean, seconds: Int)

    external fun audioPosition(): Long

    external fun audioSampleRate(): Int

    external fun readAudio(start: Long, end: Long, actualStart: LongArray): ShortArray?

    /**
     * The surface's width and height in pixels, then the left, top, right and bottom of the
     * game's picture on it. Null while no game is running.
     */
    external fun pictureRect(width: Int, height: Int): IntArray?

    /** While held, the game sees its stick at rest: the reader's cursor has the sticks. */
    external fun holdSticks(held: Boolean)


    /** Presses or releases a button of the console, as [Console] numbers its buttons. */
    external fun pressButton(input: Int, down: Boolean)

    /**
     * Which answer of the choice on screen the game's cursor is on, as the profile reports it
     * (reader.cursor): the answer (from 0; -1 while on none yet) and 1 when the answers are side
     * by side. Null when the profile says nothing about it.
     */
    external fun answerCursor(): IntArray?

    /** True while the game itself is in front: not the emulator's menus, not its pause screen. */
    external fun inGame(): Boolean

    /**
     * Runs the game at full speed, pressing yes, until its next choice (reader/core/next_choice.h);
     * stops it when it is running. [NextChoice] is told when it starts and ends.
     */
    external fun toggleNextChoice()

    external fun goingToNextChoice(): Boolean

    /** What the game's script offers to mark by hand (its routes), as JSON, or null ([Routes]). */
    external fun progress(): String?

    /** Marks one item of [progress]; null when done, else what went wrong. */
    external fun markProgress(id: String, done: Boolean): String?

    /** The running profile's actions (reader.action), their labels one per line (UTF-8); empty when none. */
    external fun profileActions(): ByteArray?

    /** Runs the action at [index]; what the profile says about it (UTF-8), or null. */
    external fun runProfileAction(index: Int): ByteArray?

    /** What the running game's profile offers the player to do in the game (reader.action). */
    fun actions(): List<String> = try {
        profileActions()?.toString(Charsets.UTF_8)?.split('\n')?.filter { it.isNotEmpty() } ?: emptyList()
    } catch (_: UnsatisfiedLinkError) {
        emptyList() // no emulator behind this (off-device tests)
    }

    /** Runs one; returns what the profile has to say about it. */
    fun runAction(index: Int): String? = try {
        runProfileAction(index)?.toString(Charsets.UTF_8)
    } catch (_: UnsatisfiedLinkError) {
        null
    }

    internal const val PROFILE_ASSET_DIR = "reader/profiles"
    internal const val PROFILE_USER_DIR = "reader/profiles"

    private val main = Handler(Looper.getMainLooper())

    /** The application, for the profiles in its assets and in its folder. */
    @Volatile
    var appContext: Context? = null

    /** What is told when a game starts or stops, and when the emulator's menu asks for the reader. */
    interface Frontend {
        fun onGameStarted(gameId: String, title: String)
        fun onGameStopped()
        fun onMenu(what: String)
    }

    @Volatile
    var frontend: Frontend? = null

    @Volatile
    private var currentGameId: String = ""

    private var attached = false

    /** [attach], once, and without failing where there is no native library (tests). */
    fun attachOnce() {
        if (attached) return
        try {
            attach()
            attached = true
        } catch (_: UnsatisfiedLinkError) {
            // No emulator behind this (off-device tests).
        }
    }

    // ---- Called from native code ----

    @Keep
    @JvmStatic
    fun onTextEvent(json: ByteArray) {
        ReaderSession.onNativeEvent(String(json, Charsets.UTF_8))
    }

    @Keep
    @JvmStatic
    fun onLayout(json: ByteArray) {
        ReaderSession.onNativeLayout(String(json, Charsets.UTF_8))
    }

    @Keep
    @JvmStatic
    fun onLog(level: Int, message: ByteArray) {
        val text = String(message, Charsets.UTF_8)
        when (level) {
            0 -> Log.info("[Reader] $text")
            1 -> Log.warning("[Reader] $text")
            else -> Log.error("[Reader] $text")
        }
        ReaderSession.onNativeLog(level, text)
    }

    @Keep
    @JvmStatic
    fun onProfileStatus(loaded: Boolean, hooks: Int, message: ByteArray) {
        ReaderSession.onNativeProfileStatus(loaded, hooks, String(message, Charsets.UTF_8))
    }

    /** A game is loaded: its ID as its disc gives it ([Console.GAME_ID]) and its name. */
    @Keep
    @JvmStatic
    fun onGameStarted(gameId: String, title: ByteArray) {
        currentGameId = gameId
        val name = String(title, Charsets.UTF_8)
        main.post { frontend?.onGameStarted(gameId, name) }
    }

    @Keep
    @JvmStatic
    fun onGameStopped() {
        main.post { frontend?.onGameStopped() }
    }

    /** Whether the game has a profile, bundled or the user's: a console menu's list and its mark. */
    @Keep
    @JvmStatic
    fun hasProfile(gameId: String): Boolean = appContext?.let { ReaderCatalog.covers(it, gameId) } ?: false

    /** Going on to the next choice started ([running]) or ended (why, and how many texts passed). */
    @Keep
    @JvmStatic
    fun onNextChoice(running: Boolean, end: Int, passed: Long) {
        main.post { NextChoice.onChange(running, end, passed) }
    }

    /** The emulator's own menu asks for one of the reader's screens. */
    @Keep
    @JvmStatic
    fun onMenu(what: ByteArray) {
        val name = String(what, Charsets.UTF_8)
        main.post { frontend?.onMenu(name) }
    }

    /**
     * The profile for a game: a file of the user's wins over the bundled one. Null when there
     * is none.
     */
    @Keep
    @JvmStatic
    fun findProfile(gameId: String): ByteArray? {
        currentGameId = gameId
        // Where the app starts its session before the game's ID is known, it learns it here.
        ReaderSession.adoptTitleId(gameId)
        return profileSource(gameId)
    }

    @Keep
    @JvmStatic
    fun profileSource(gameId: String): ByteArray? =
        readUserFile("$gameId.lua") ?: readAsset("$gameId.lua")

    /** A data file a profile asks for with reader.data(name): `<GAMEID>.name`. */
    @Keep
    @JvmStatic
    fun loadProfileData(name: String): ByteArray? {
        if (name.contains("..") || name.contains('/') || name.contains('\\')) return null
        val scoped = "$currentGameId.$name"
        return readUserFile(scoped) ?: readAsset(scoped)
    }

    fun hasUserProfile(gameId: String): Boolean = userProfileFile("$gameId.lua") != null

    fun hasBundledProfile(gameId: String): Boolean = try {
        appContext?.assets?.open("$PROFILE_ASSET_DIR/$gameId.lua")?.close() != null
    } catch (_: IOException) {
        false
    }

    private fun readAsset(name: String): ByteArray? = try {
        appContext?.assets?.open("$PROFILE_ASSET_DIR/$name")?.use { it.readBytes() }
    } catch (_: IOException) {
        null
    }

    private fun userProfileFile(name: String): File? = try {
        val root = appContext?.getExternalFilesDir(null)
        root?.let { File(File(it, PROFILE_USER_DIR), name) }?.takeIf { it.isFile }
    } catch (e: Exception) {
        Log.warning("[Reader] Cannot look for $name in the app's folder: ${e.message}")
        null
    }

    private fun readUserFile(name: String): ByteArray? {
        val file = userProfileFile(name) ?: return null
        return try {
            file.readBytes()
        } catch (e: Exception) {
            Log.warning("[Reader] Cannot read $file: ${e.message}")
            null
        }
    }
}
