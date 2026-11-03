// SPDX-FileCopyrightText: ARMSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

package com.armsx2.reader.ui

import android.content.Context
import android.view.KeyEvent as AndroidKeyEvent
import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/**
 * The game list in the manner of the PlayStation 2's system menu: its main menu (Browser and
 * System Configuration beside the slowly turning lights), the Browser with the games as
 * pictures afloat in the haze and the chosen one's name in the top right corner, and System
 * Configuration as a list of names and values. The same screens as the desktop app's
 * (reader/qt/ps2_home.cpp in the PCSX2 Reader fork).
 *
 * It stands where the app's own library screen stands and knows nothing of the emulator: the
 * games, what starts one and the lines of System Configuration are handed to it.
 */
object Ps2Home {
    /** The colours of the system menu: a blue so deep that it is nearly black. */
    object Look {
        val ground = Color(0xFF060A1E)
        val groundLow = Color(0xFF010208)
        val text = Color(0xFFC9D8F6)
        val faint = Color(0xFF7082AE)
        val line = Color(0xFF2B3A68)
        val accent = Color(0xFF9CD4FF)
        val glow = Color(0xFF3E8CFF)
        val raised = Color(0xFF111A3A)
        val pressed = Color(0xFF1D3570)
    }

    /** A game as the Browser shows it. */
    data class Game(
        val key: String,
        val name: String,
        val serial: String,
        val cover: String?,
        val readable: Boolean,
    )

    /** A line of System Configuration: a name, what it is set to, what choosing it does. */
    class Setting(
        val key: String,
        val name: String,
        val value: () -> String = { "" },
        val choose: () -> Unit,
    )

    enum class Screen { Menu, Browser, Configuration }

    private const val PREFS = "reader"
    private const val KEY_HOME = "reader_ps2_home"

    /** The menu is the game list (on unless switched off under System Configuration). */
    fun wanted(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_HOME, true)

    fun setWanted(context: Context, wanted: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_HOME, wanted).apply()
        wantedState.value = wanted
    }

    /** Follows [setWanted], for the screen that chooses between this menu and the library. */
    val wantedState = mutableStateOf<Boolean?>(null)

    private const val KEY_ALL_GAMES = "reader_all_games"

    /**
     * The Browser shows every game found, not only those whose text can be read (off unless
     * switched on under System Configuration, "Games Shown").
     */
    fun allGames(context: Context): Boolean =
        allGamesState.value ?: context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getBoolean(KEY_ALL_GAMES, false).also { allGamesState.value = it }

    fun setAllGames(context: Context, all: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_ALL_GAMES, all).apply()
        allGamesState.value = all
    }

    private val allGamesState = mutableStateOf<Boolean?>(null)

    // What is up and what is chosen outlives a game: back from one, the Browser is where it was.
    private var screen by mutableStateOf(Screen.Menu)
    private var menuChoice by mutableIntStateOf(0)
    private var gameChoice by mutableIntStateOf(0)
    private var settingChoice by mutableIntStateOf(0)

    /** For tests: what the menu shows. */
    fun describe(): String = "screen=$screen menu=$menuChoice game=$gameChoice setting=$settingChoice"

    fun reset() {
        screen = Screen.Menu
        menuChoice = 0
        gameChoice = 0
        settingChoice = 0
    }

    @Composable
    fun Screen(
        games: List<Game>,
        settings: List<Setting>,
        onLaunch: (Game) -> Unit,
        onAddGames: () -> Unit,
        onGameOptions: (Game) -> Unit = {},
    ) {
        // The lights turn and the games float by this clock.
        var seconds by remember { mutableFloatStateOf(0f) }
        LaunchedEffect(Unit) {
            val start = withFrameNanos { it }
            while (true) {
                withFrameNanos { seconds = (it - start) / 1_000_000_000f }
            }
        }
        val focus = remember { FocusRequester() }
        LaunchedEffect(screen) { runCatching { focus.requestFocus() } }
        if (gameChoice >= games.size) gameChoice = (games.size - 1).coerceAtLeast(0)
        if (settingChoice >= settings.size) settingChoice = (settings.size - 1).coerceAtLeast(0)

        BoxWithConstraints(
            Modifier
                .fillMaxSize()
                .background(Look.groundLow)
                .focusRequester(focus)
                .focusable()
                .onPreviewKeyEvent { event ->
                    if (event.nativeKeyEvent.action != AndroidKeyEvent.ACTION_DOWN) {
                        // The release of a key the menu took is the menu's too.
                        return@onPreviewKeyEvent isMenuKey(event.nativeKeyEvent.keyCode)
                    }
                    onKey(event.nativeKeyEvent.keyCode, games, settings, columnsFor(maxWidthPx), onLaunch, onAddGames, onGameOptions)
                }
        ) {
            maxWidthPx = constraints.maxWidth
            val unit = minOf(maxWidth / 640f, maxHeight / 448f)
            Crossfade(targetState = screen, animationSpec = tween(380), label = "ps2-screen") { which ->
                Box(Modifier.fillMaxSize()) {
                    Canvas(Modifier.fillMaxSize()) { drawGround(which, seconds) }
                    when (which) {
                        Screen.Menu -> MenuScreen(unit, seconds)
                        Screen.Browser -> BrowserScreen(unit, seconds, games, onLaunch, onAddGames, onGameOptions)
                        Screen.Configuration -> ConfigurationScreen(unit, seconds, settings)
                    }
                }
            }
        }
    }

    private var maxWidthPx = 0

    private fun columnsFor(widthPx: Int): Int = lastColumns.coerceAtLeast(1)

    private var lastColumns = 4

    private fun isMenuKey(code: Int): Boolean = when (code) {
        AndroidKeyEvent.KEYCODE_DPAD_UP, AndroidKeyEvent.KEYCODE_DPAD_DOWN, AndroidKeyEvent.KEYCODE_DPAD_LEFT,
        AndroidKeyEvent.KEYCODE_DPAD_RIGHT, AndroidKeyEvent.KEYCODE_DPAD_CENTER, AndroidKeyEvent.KEYCODE_ENTER,
        AndroidKeyEvent.KEYCODE_NUMPAD_ENTER, AndroidKeyEvent.KEYCODE_BUTTON_A, AndroidKeyEvent.KEYCODE_BUTTON_B,
        AndroidKeyEvent.KEYCODE_BUTTON_Y, AndroidKeyEvent.KEYCODE_ESCAPE -> true
        AndroidKeyEvent.KEYCODE_BACK -> screen != Screen.Menu
        else -> false
    }

    /** A key of a controller or keyboard. True when the menu took it. */
    fun onKey(
        code: Int,
        games: List<Game>,
        settings: List<Setting>,
        columns: Int,
        onLaunch: (Game) -> Unit,
        onAddGames: () -> Unit,
        onGameOptions: (Game) -> Unit,
    ): Boolean {
        when (code) {
            AndroidKeyEvent.KEYCODE_DPAD_UP -> move(0, -1, games.size, settings.size, columns)
            AndroidKeyEvent.KEYCODE_DPAD_DOWN -> move(0, 1, games.size, settings.size, columns)
            AndroidKeyEvent.KEYCODE_DPAD_LEFT -> move(-1, 0, games.size, settings.size, columns)
            AndroidKeyEvent.KEYCODE_DPAD_RIGHT -> move(1, 0, games.size, settings.size, columns)
            AndroidKeyEvent.KEYCODE_DPAD_CENTER, AndroidKeyEvent.KEYCODE_ENTER, AndroidKeyEvent.KEYCODE_NUMPAD_ENTER,
            AndroidKeyEvent.KEYCODE_BUTTON_A -> enter(games, settings, onLaunch, onAddGames)
            AndroidKeyEvent.KEYCODE_BUTTON_B, AndroidKeyEvent.KEYCODE_ESCAPE -> back()
            AndroidKeyEvent.KEYCODE_BACK -> {
                // On the main menu Back is the system's: it leaves the app.
                if (screen == Screen.Menu) return false
                back()
            }
            AndroidKeyEvent.KEYCODE_BUTTON_Y -> {
                if (screen == Screen.Browser) games.getOrNull(gameChoice)?.let(onGameOptions)
            }
            else -> return false
        }
        return true
    }

    private fun back() {
        if (screen != Screen.Menu) screen = Screen.Menu
    }

    private fun enter(games: List<Game>, settings: List<Setting>, onLaunch: (Game) -> Unit, onAddGames: () -> Unit) {
        when (screen) {
            Screen.Menu -> screen = if (menuChoice == 0) Screen.Browser else Screen.Configuration
            Screen.Browser -> games.getOrNull(gameChoice)?.let(onLaunch) ?: onAddGames()
            Screen.Configuration -> settings.getOrNull(settingChoice)?.choose?.invoke()
        }
    }

    private fun move(dx: Int, dy: Int, gameCount: Int, settingCount: Int, columns: Int) {
        when (screen) {
            Screen.Menu -> {
                menuChoice = (menuChoice + dy).coerceIn(0, 1)
                if (dx > 0) screen = if (menuChoice == 0) Screen.Browser else Screen.Configuration
            }
            Screen.Configuration -> {
                settingChoice = (settingChoice + dy).coerceIn(0, (settingCount - 1).coerceAtLeast(0))
                if (dx < 0) back()
            }
            Screen.Browser -> {
                if (gameCount == 0) return
                var next = gameChoice
                if (dx < 0 && gameChoice % columns > 0) next--
                else if (dx > 0 && gameChoice % columns < columns - 1 && gameChoice + 1 < gameCount) next++
                if (dy < 0 && gameChoice - columns >= 0) next -= columns
                else if (dy > 0) {
                    if (gameChoice + columns < gameCount) next += columns
                    else if (gameChoice / columns < (gameCount - 1) / columns) next = gameCount - 1
                }
                gameChoice = next
            }
        }
    }

    // ---- The three screens ----

    /** What is chosen is lit from within; what is not is faint. */
    private fun entryStyle(chosen: Boolean, size: Float, seconds: Float): TextStyle {
        if (!chosen) return TextStyle(color = Look.faint, fontSize = size.sp, fontWeight = FontWeight.Light)
        val breath = 0.75f + 0.25f * sin(seconds * 2f * PI.toFloat() / 1.8f)
        return TextStyle(
            color = lerp(Look.accent, Color.White, 0.35f * breath),
            fontSize = size.sp,
            fontWeight = FontWeight.Light,
            shadow = Shadow(color = Look.glow.copy(alpha = 0.9f * breath), blurRadius = size * 1.1f),
        )
    }

    private fun lerp(a: Color, b: Color, t: Float): Color = androidx.compose.ui.graphics.lerp(a, b, t.coerceIn(0f, 1f))

    @Composable
    private fun MenuScreen(unit: Dp, seconds: Float) {
        Box(Modifier.fillMaxSize()) {
            Canvas(Modifier.fillMaxSize()) {
                drawLights(Offset(size.width * 0.68f, size.height * 0.48f), 78f * unit.toPx(), seconds, 1f)
            }
            Column(
                Modifier.align(Alignment.CenterStart).padding(start = unit * 90),
                verticalArrangement = Arrangement.spacedBy(unit * 14),
            ) {
                listOf("Browser", "System Configuration").forEachIndexed { index, name ->
                    Text(
                        name,
                        style = entryStyle(index == menuChoice, 27f * unit.value, seconds),
                        modifier = Modifier
                            .testTag("ps2-menu-$index")
                            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {
                                menuChoice = index
                                screen = if (index == 0) Screen.Browser else Screen.Configuration
                            },
                    )
                }
            }
            Hints(unit, listOf("cross" to "Enter"), Modifier.align(Alignment.BottomEnd))
        }
    }

    @Composable
    private fun BrowserScreen(
        unit: Dp,
        seconds: Float,
        games: List<Game>,
        onLaunch: (Game) -> Unit,
        onAddGames: () -> Unit,
        onGameOptions: (Game) -> Unit,
    ) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val tileWidth = unit * 92
            val gap = unit * 30
            val side = unit * 64
            val columns = (((maxWidth - side * 2 + gap) / (tileWidth + gap)).toInt()).coerceAtLeast(1)
            lastColumns = columns
            val chosen = games.getOrNull(gameChoice)

            // In the top right corner, the chosen game's name, as the console names what is chosen.
            Column(
                Modifier.align(Alignment.TopEnd).padding(top = unit * 22, end = unit * 46).fillMaxWidth(0.66f),
                horizontalAlignment = Alignment.End,
            ) {
                Text(
                    chosen?.name ?: "",
                    color = Color(0xFFF2F6FF),
                    fontSize = (21f * unit.value).sp,
                    fontWeight = FontWeight.Light,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = TextStyle(shadow = Shadow(Color(0x99000000), Offset(2f, 2f), 3f)),
                    modifier = Modifier.testTag("ps2-chosen-name"),
                )
                if (chosen != null) {
                    Text(
                        listOfNotNull(chosen.serial.ifEmpty { null }, if (chosen.readable) "Its text can be read" else null)
                            .joinToString("  ·  "),
                        color = Color(0xFFD2DEF6),
                        fontSize = (13f * unit.value).sp,
                        maxLines = 1,
                    )
                }
            }
            Text(
                "Browser",
                color = Color(0xFFE6EEFF).copy(alpha = 0.85f),
                fontSize = (14f * unit.value).sp,
                modifier = Modifier.align(Alignment.TopStart).padding(top = unit * 30, start = unit * 46),
            )

            if (games.isEmpty()) {
                Text(
                    "No data",
                    color = Color(0xFFE6EEFF),
                    fontSize = (22f * unit.value).sp,
                    fontWeight = FontWeight.Light,
                    modifier = Modifier.align(Alignment.Center).clickable { onAddGames() },
                )
            } else {
                val grid = rememberLazyGridState()
                LaunchedEffect(gameChoice) { runCatching { grid.animateScrollToItem(gameChoice.coerceIn(0, games.size - 1)) } }
                LazyVerticalGrid(
                    columns = GridCells.Fixed(columns),
                    state = grid,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(top = unit * 96, bottom = unit * 56, start = side, end = side),
                    horizontalArrangement = Arrangement.spacedBy(gap),
                    verticalArrangement = Arrangement.spacedBy(unit * 34),
                ) {
                    itemsIndexed(games, key = { _, game -> game.key }) { index, game ->
                        GameTile(unit, seconds, index, game, index == gameChoice,
                            onTap = {
                                // As a controller's Enter: the chosen game starts; another is chosen first.
                                if (index == gameChoice) onLaunch(game) else gameChoice = index
                            })
                    }
                }
            }
            Hints(
                unit,
                if (games.isEmpty()) listOf("cross" to "Add games", "circle" to "Back")
                else listOf("cross" to "Enter", "circle" to "Back", "triangle" to "Options"),
                Modifier.align(Alignment.BottomEnd),
                onHint = { mark ->
                    when (mark) {
                        "circle" -> back()
                        "triangle" -> chosen?.let(onGameOptions)
                        else -> chosen?.let(onLaunch) ?: onAddGames()
                    }
                },
            )
        }
    }

    @Composable
    private fun GameTile(unit: Dp, seconds: Float, index: Int, game: Game, chosen: Boolean, onTap: () -> Unit) {
        // Each floats a little, out of step with its neighbours; the chosen one comes forward.
        val grow by animateFloatAsState(if (chosen) 1.11f else 1f, tween(250), label = "ps2-tile")
        val bob = sin(seconds * 1.1f + index * 1.3f) * 2.2f
        val breath = 0.6f + 0.4f * sin(seconds * 2f * PI.toFloat() / 1.8f)
        Box(
            Modifier
                .padding(vertical = unit * 8)
                .fillMaxWidth()
                .aspectRatio(92f / 130f)
                .offset(y = unit * bob)
                .graphicsLayer {
                    scaleX = grow
                    scaleY = grow
                    alpha = if (chosen) 1f else 0.82f
                }
                .then(
                    if (chosen) Modifier.border(unit * 2.4f, Color(0xFFBEE2FF).copy(alpha = 0.35f + 0.5f * breath), RoundedCornerShape(unit * 3))
                    else Modifier
                )
                .clip(RoundedCornerShape(unit * 3))
                .background(Brush.linearGradient(listOf(Color(0xFF2A3C74), Color(0xFF0E1430))))
                .semantics { contentDescription = game.name }
                .testTag("ps2-game-$index")
                .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { onTap() },
        ) {
            // A game without a picture is a plain case with its name on it; a picture covers it.
            Text(
                game.name,
                color = Color(0xFFDCE8FF),
                fontSize = (10.5f * unit.value).sp,
                textAlign = TextAlign.Center,
                modifier = Modifier.align(Alignment.Center).padding(unit * 6),
            )
            if (game.cover != null) {
                AsyncImage(
                    model = game.cover,
                    contentDescription = null,
                    contentScale = ContentScale.Crop,
                    modifier = Modifier.fillMaxSize(),
                )
            }
            if (game.readable) {
                // A game whose text can be read carries a boxed book in its corner.
                Canvas(
                    Modifier
                        .align(Alignment.TopEnd)
                        .padding(unit * 3)
                        .size(unit * 17)
                        .clip(RoundedCornerShape(unit * 3))
                        .background(Color(0xE60A1026))
                        .border(unit * 1.2f, Color(0xFFF5C518), RoundedCornerShape(unit * 3))
                ) { drawBook(Color(0xFFF5C518)) }
            }
        }
    }

    @Composable
    private fun ConfigurationScreen(unit: Dp, seconds: Float, settings: List<Setting>) {
        Box(Modifier.fillMaxSize()) {
            Canvas(Modifier.fillMaxSize()) {
                drawLights(Offset(size.width * 0.84f, size.height * 0.2f), 34f * unit.toPx(), seconds, 0.55f)
            }
            Column(Modifier.fillMaxSize().padding(horizontal = unit * 46)) {
                Spacer(Modifier.height(unit * 34))
                Text(
                    "System Configuration",
                    color = Look.accent,
                    fontSize = (23f * unit.value).sp,
                    fontWeight = FontWeight.Light,
                )
                Spacer(Modifier.height(unit * 8))
                Box(Modifier.fillMaxWidth().height(1.dp).background(Look.line))
                val list = rememberLazyListState()
                LaunchedEffect(settingChoice) {
                    runCatching { list.animateScrollToItem(settingChoice.coerceIn(0, (settings.size - 1).coerceAtLeast(0))) }
                }
                LazyColumn(
                    state = list,
                    modifier = Modifier.fillMaxWidth().weight(1f).padding(horizontal = unit * 44, vertical = unit * 22),
                ) {
                    itemsIndexed(settings, key = { _, setting -> setting.key }) { index, setting ->
                        val chosen = index == settingChoice
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .height(unit * 36)
                                .background(
                                    if (chosen) Brush.horizontalGradient(
                                        listOf(Color.Transparent, Look.glow.copy(alpha = 0.18f), Color.Transparent)
                                    ) else Brush.horizontalGradient(listOf(Color.Transparent, Color.Transparent))
                                )
                                .testTag("ps2-setting-${setting.key}")
                                .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {
                                    settingChoice = index
                                    setting.choose()
                                },
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(setting.name, style = entryStyle(chosen, 18f * unit.value, seconds), modifier = Modifier.weight(1f))
                            Text(
                                setting.value(),
                                color = if (chosen) Look.text else Look.faint,
                                fontSize = (18f * unit.value).sp,
                                fontWeight = FontWeight.Light,
                                maxLines = 1,
                            )
                        }
                    }
                }
                Spacer(Modifier.height(unit * 40))
            }
            Hints(unit, listOf("cross" to "Enter", "circle" to "Back"), Modifier.align(Alignment.BottomEnd),
                onHint = { mark -> if (mark == "circle") back() else settings.getOrNull(settingChoice)?.choose?.invoke() })
        }
    }

    /** What the buttons do, in the bottom right corner: a mark in its own colour and a word. */
    @Composable
    private fun Hints(unit: Dp, hints: List<Pair<String, String>>, modifier: Modifier, onHint: (String) -> Unit = {}) {
        Row(
            modifier.padding(end = unit * 46, bottom = unit * 24),
            horizontalArrangement = Arrangement.spacedBy(unit * 20),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            hints.forEach { (mark, word) ->
                Row(
                    Modifier.clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { onHint(mark) },
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Canvas(Modifier.size(unit * 20)) { drawMark(mark) }
                    Spacer(Modifier.width(unit * 5))
                    Text(word, color = Look.text, fontSize = (15f * unit.value).sp)
                }
            }
        }
    }

    // ---- Drawing ----

    /** The four marks of the console's buttons, each in its own colour. */
    private fun DrawScope.drawMark(mark: String) {
        val inset = size.width * 0.2f
        val stroke = Stroke(width = (size.width * 0.11f).coerceAtLeast(2f), cap = StrokeCap.Round)
        val a = Offset(inset, inset)
        val b = Offset(size.width - inset, size.height - inset)
        when (mark) {
            "cross" -> {
                drawLine(Color(0xFF7CA8FF), a, b, stroke.width, StrokeCap.Round)
                drawLine(Color(0xFF7CA8FF), Offset(b.x, a.y), Offset(a.x, b.y), stroke.width, StrokeCap.Round)
            }
            "circle" -> drawCircle(Color(0xFFFF6E6E), (size.width - 2 * inset) / 2, style = stroke)
            "triangle" -> {
                val path = androidx.compose.ui.graphics.Path().apply {
                    moveTo(size.width / 2, a.y); lineTo(b.x, b.y); lineTo(a.x, b.y); close()
                }
                drawPath(path, Color(0xFF5FE0A8), style = stroke)
            }
            else -> drawRect(Color(0xFFF09AE0), a, Size(b.x - a.x, b.y - a.y), style = stroke)
        }
    }

    private fun DrawScope.drawBook(colour: Color) {
        val k = size.width / 40f
        val m = Offset(size.width / 2, size.height / 2)
        val path = androidx.compose.ui.graphics.Path().apply {
            moveTo(m.x, m.y - 8 * k)
            cubicTo(m.x - 5 * k, m.y - 12 * k, m.x - 11 * k, m.y - 12 * k, m.x - 14 * k, m.y - 10 * k)
            lineTo(m.x - 14 * k, m.y + 9 * k)
            cubicTo(m.x - 11 * k, m.y + 7 * k, m.x - 5 * k, m.y + 7 * k, m.x, m.y + 11 * k)
            cubicTo(m.x + 5 * k, m.y + 7 * k, m.x + 11 * k, m.y + 7 * k, m.x + 14 * k, m.y + 9 * k)
            lineTo(m.x + 14 * k, m.y - 10 * k)
            cubicTo(m.x + 11 * k, m.y - 12 * k, m.x + 5 * k, m.y - 12 * k, m.x, m.y - 8 * k)
            lineTo(m.x, m.y + 11 * k)
        }
        drawPath(path, colour, style = Stroke(width = (2.4f * k).coerceAtLeast(1.5f), cap = StrokeCap.Round))
    }

    /** The console's seven lights, each on a ring of its own, turning slowly about the middle. */
    private fun DrawScope.drawLights(centre: Offset, radius: Float, seconds: Float, alpha: Float) {
        drawCircle(
            Brush.radialGradient(
                listOf(Look.glow.copy(alpha = 0.27f * alpha), Color(0xFF1C3CB0).copy(alpha = 0.10f * alpha), Color.Transparent),
                centre, radius * 1.9f,
            ),
            radius * 1.9f, centre,
        )
        data class Light(val depth: Float, val at: Offset, val size: Float)
        val lights = (0 until 7).map { index ->
            val tilt = 0.5f + index * 0.37f
            val angle = seconds * (0.22f + index * 0.031f) + index * 2f * PI.toFloat() / 7f
            val x = cos(angle) * radius
            val y = sin(angle) * radius * cos(tilt)
            val z = sin(angle) * sin(tilt)
            val turn = index * 0.9f
            Light(z, centre + Offset(x * cos(turn) - y * sin(turn), x * sin(turn) + y * cos(turn)), radius * (0.2f + 0.06f * z))
        }.sortedBy { it.depth }
        for (light in lights) {
            val near = 0.55f + 0.45f * (light.depth + 1f) / 2f
            drawCircle(
                Brush.radialGradient(listOf(Color(0xFF6EB4FF).copy(alpha = 0.35f * alpha * near), Color.Transparent), light.at, light.size * 2.6f),
                light.size * 2.6f, light.at,
            )
            drawCircle(
                Brush.radialGradient(
                    listOf(
                        Color.White.copy(alpha = 0.92f * alpha * near),
                        Look.accent.copy(alpha = 0.78f * alpha * near),
                        Color(0xFF1E50E0).copy(alpha = 0.47f * alpha * near),
                    ),
                    light.at - Offset(light.size * 0.3f, light.size * 0.35f), light.size * 1.25f,
                ),
                light.size, light.at,
            )
        }
    }

    private fun DrawScope.drawGround(which: Screen, seconds: Float) {
        if (which == Screen.Browser) {
            // The Browser's haze: grey-blue, lighter towards the top, with slow clouds in it.
            drawRect(Brush.verticalGradient(listOf(Color(0xFF6F7C93), Color(0xFF39445C), Color(0xFF121726))))
            for (cloud in 0 until 4) {
                val phase = seconds * (0.035f + cloud * 0.011f) + cloud * 0.31f
                val at = Offset(
                    size.width * (0.5f + 0.55f * sin(phase * 2f * PI.toFloat())),
                    size.height * (0.3f + 0.16f * cloud + 0.05f * cos(phase * 5f)),
                )
                val reach = size.width * (0.34f + 0.05f * cloud)
                drawCircle(Brush.radialGradient(listOf(Color(0x22C8D6F0), Color.Transparent), at, reach), reach, at)
            }
            return
        }
        drawRect(
            Brush.radialGradient(
                listOf(lerp(Look.ground, Look.glow, 0.10f), Look.ground, Look.groundLow),
                Offset(size.width * 0.62f, size.height * 0.5f), maxOf(size.width, size.height) * 0.75f,
            )
        )
        // Motes of light adrift in the dark.
        for (mote in 0 until 26) {
            val seed = mote * 12.9898f
            val fx = abs(sin(seed) * 43758.547f) % 1f
            val fy = abs(sin(seed * 1.7f) * 24634.635f) % 1f
            val drift = seconds * (0.006f + 0.004f * (mote % 5))
            val at = Offset(size.width * ((fx + drift) % 1f), size.height * (fy + 0.02f * sin(seconds * 0.4f + mote)))
            val twinkle = 0.4f + 0.6f * abs(sin(seconds * 0.5f + mote * 0.7f))
            drawCircle(Color(0xFF8CBEFF).copy(alpha = 0.27f * twinkle), (0.7f + (mote % 3) * 0.5f) * density, at)
        }
    }
}
