// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader.mining

import org.readers.app.reader.Console
import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import org.readers.app.R
import org.readers.app.reader.PresentedLine
import org.readers.app.reader.ReaderCatalog
import org.readers.app.reader.ReaderSession
import org.readers.app.reader.ReaderSettings
import org.readers.app.reader.anki.AnkiClient
import org.readers.app.reader.anki.AnkiConnectClient
import org.readers.app.reader.anki.AnkiDroidClient
import org.readers.app.reader.anki.AnkiException
import org.readers.app.reader.anki.AnkiIndex
import org.readers.app.reader.anki.CardBuilder
import org.readers.app.reader.anki.CardContext
import org.readers.app.reader.anki.Duplicates
import org.readers.app.reader.anki.SourceAddress
import org.readers.app.reader.dict.LookupResult
import org.readers.app.reader.ReaderLog as Log

/**
 * Makes an Anki card from a line: gathers the screenshot and audio the note's fields ask for,
 * stores them in Anki, then adds the note. Reading statistics and the game are not affected.
 */
class Miner(context: Context) {
    sealed class Outcome {
        data class Added(val noteId: Long) : Outcome()

        /** Nothing was added: Anki has the word already, as [notes]. */
        data class Exists(val notes: List<Long>) : Outcome()
        data class Failed(val message: String) : Outcome()

        /** Nothing was added: the user was asked about the audio and called the card off. */
        object Cancelled : Outcome()
    }

    /**
     * Puts the question of a game whose cards ask about their audio: does this card get [clip],
     * what the game played during its line? [answer] takes true to keep it, false to make the
     * card without it, null to make no card. Main thread.
     */
    fun interface AudioQuestion {
        fun ask(clip: File, answer: (keep: Boolean?) -> Unit)
    }

    private val context = context.applicationContext
    private val settings = ReaderSettings(this.context)
    private val main = Handler(Looper.getMainLooper())

    /**
     * The configured client, or null with the reason it cannot be used. A [quick] one gives up
     * on an Anki that does not answer soon: for questions the user is not waiting on.
     */
    fun client(quick: Boolean = false): Pair<AnkiClient?, String?> = when (settings.ankiTransport) {
        ReaderSettings.ANKI_ANKIDROID -> when {
            !AnkiDroidClient.isInstalled(context) ->
                null to context.getString(R.string.reader_ankidroid_missing)

            !AnkiDroidClient.hasPermission(context) ->
                null to context.getString(R.string.reader_ankidroid_permission)

            else -> AnkiDroidClient(context) to null
        }

        ReaderSettings.ANKI_ANKICONNECT -> if (settings.ankiConnectUrl.isBlank()) {
            null to context.getString(R.string.reader_anki_not_configured)
        } else if (quick) {
            AnkiConnectClient(
                settings.ankiConnectUrl.trim(),
                settings.ankiConnectKey,
                QUICK_CONNECT_MS,
                QUICK_READ_MS
            ) to null
        } else {
            AnkiConnectClient(settings.ankiConnectUrl.trim(), settings.ankiConnectKey) to null
        }

        else -> null to context.getString(R.string.reader_anki_disabled)
    }

    /**
     * Adds a card for [presented], about [word] when one was looked up. Whether it gets its
     * line's audio is the game's setting; where that is to ask, [question] is asked, and a card
     * made where nobody can be asked gets the audio. [callback] runs on the main thread. Call
     * from the main thread.
     */
    fun mine(
        presented: PresentedLine,
        word: LookupResult?,
        wordRange: IntRange?,
        question: AudioQuestion? = null,
        callback: (Outcome) -> Unit
    ) {
        val (client, problem) = client()
        val templates = CardBuilder.parseMapping(settings.ankiFieldMap).filterValues { it.isNotBlank() }
        if (client == null) {
            callback(Outcome.Failed(problem ?: ""))
            return
        }
        if (settings.ankiDeck.isBlank() || settings.ankiModel.isBlank() || templates.isEmpty()) {
            callback(Outcome.Failed(context.getString(R.string.reader_anki_not_configured)))
            return
        }

        val media = mediaOverride ?: ReaderSession.media
        val isCurrent = ReaderSession.current?.backlogId == presented.backlogId
        val audio = settings.cardAudioFor(
            ReaderSession.gameKey,
            ReaderCatalog.cardAudio(context, ReaderSession.gameKey)
        )
        val wantsPicture = CardBuilder.needsScreenshot(templates.values) && media != null
        val wantsAudio = audio != ReaderSettings.CardAudio.NEVER &&
            CardBuilder.needsAudio(templates.values) && media != null &&
            presented.line.audioPosition >= 0

        // Gather media (each step may finish asynchronously), then hand over to the worker.
        fun withAudio(picture: File?) {
            if (!wantsAudio) {
                send(client, templates, presented, word, wordRange, picture, null, callback)
                return
            }
            media!!.audioClip(
                presented.backlogId,
                presented.line.audioPosition,
                ReaderSession.audioEndFor(presented.backlogId)
            ) { clip ->
                if (clip == null || audio != ReaderSettings.CardAudio.ASK || question == null) {
                    send(client, templates, presented, word, wordRange, picture, clip, callback)
                    return@audioClip
                }
                question.ask(clip) { keep ->
                    when (keep) {
                        true -> send(client, templates, presented, word, wordRange, picture, clip, callback)

                        false -> {
                            clip.delete()
                            send(client, templates, presented, word, wordRange, picture, null, callback)
                        }

                        null -> {
                            discard(picture, clip)
                            callback(Outcome.Cancelled)
                        }
                    }
                }
            }
        }
        when {
            !wantsPicture -> withAudio(null)

            // The scene on screen belongs to the current line; older lines use what was kept.
            isCurrent -> media!!.captureNow { withAudio(it ?: media.screenshotFor(presented.backlogId)) }

            else -> withAudio(media!!.screenshotFor(presented.backlogId))
        }
    }

    private fun send(
        client: AnkiClient,
        templates: Map<String, String>,
        presented: PresentedLine,
        word: LookupResult?,
        wordRange: IntRange?,
        picture: File?,
        clip: File?,
        callback: (Outcome) -> Unit
    ) {
        val deck = settings.ankiDeck
        val model = settings.ankiModel
        val tags = settings.ankiTags.split(' ').filter { it.isNotBlank() }
        val duplicates = when {
            settings.ankiAllowDuplicates -> Duplicates.ALLOW
            settings.ankiDuplicatesAnywhere -> Duplicates.REFUSE_ANYWHERE
            else -> Duplicates.REFUSE_IN_DECK
        }
        val scope = scopeDeck()
        val gameTitle = ReaderSession.gameTitle
        val gameUrl = SourceAddress.of(ReaderSession.gameKey)
        // The field VN Library's media add-on asked an address for, if cards go through it.
        val addressField = (client as? AnkiConnectClient)?.let { settings.ankiAddressField(settings.ankiConnectUrl) }
        worker.execute {
            val outcome = try {
                val card = CardContext(
                    presented.line, gameTitle, word, wordRange, gameUrl = gameUrl,
                    before = linesBefore(templates.values, presented)
                )
                // Not every Anki refuses a duplicate when asked to (AnkiConnect Android and
                // AnkiDroid add whatever they are given), so look before sending anything.
                val key = if (duplicates == Duplicates.ALLOW) null else keyOf(client, model, templates, card)
                val existing = key?.let { client.findNotes(scope, it.first, it.second) }.orEmpty()
                if (existing.isNotEmpty()) {
                    index.learn(sourceOf(key!!.first, scope), key.second, existing)
                    Outcome.Exists(existing)
                } else {
                    val stamp = System.currentTimeMillis()
                    val pictureName = picture?.let { client.storeMedia(it, "${Console.MEDIA_PREFIX}$stamp.jpg") }
                    val audioName = clip?.let { client.storeMedia(it, "${Console.MEDIA_PREFIX}$stamp.m4a") }
                    // The word as somebody says it, when a field asks for it and the card has a
                    // word: fetched now, and left out when it cannot be had.
                    val wordAudioName = word?.takeIf { CardBuilder.needsWordAudio(templates.values) }?.let {
                        val file = File.createTempFile("word", ".mp3")
                        try {
                            if (WordAudio.fetch(it.expression, it.reading, file)) {
                                client.storeMedia(file, WordAudio.fileName(it.expression, it.reading))
                            } else {
                                null
                            }
                        } catch (e: Exception) {
                            null
                        } finally {
                            file.delete()
                        }
                    }
                    val fields = CardBuilder.build(
                        templates,
                        card.copy(screenshotName = pictureName, audioName = audioName, wordAudioName = wordAudioName)
                    )
                    val added = SourceAddress.addNote(
                        client, deck, model, fields, tags, duplicates, gameUrl, gameTitle, addressField
                    )
                    if (added.field != null && added.field != addressField) {
                        settings.rememberAnkiAddressField(settings.ankiConnectUrl, added.field)
                    }
                    val id = added.id
                    (key ?: runCatching { keyOf(client, model, templates, card) }.getOrNull())
                        ?.let { index.add(sourceOf(it.first, scope), it.second, id) }
                    Outcome.Added(id)
                }
            } catch (e: AnkiException) {
                Outcome.Failed(e.message ?: "Anki error")
            } catch (e: Exception) {
                Log.error("[Reader] Card failed: $e")
                Outcome.Failed(e.message ?: e.javaClass.simpleName)
            } finally {
                discard(picture, clip)
            }
            main.post {
                if (outcome is Outcome.Added) onAdded?.invoke(presented, word, outcome.noteId)
                callback(outcome)
            }
        }
    }

    /** Removes what was gathered for one card. */
    private fun discard(picture: File?, clip: File?) {
        clip?.delete()
        // Kept line screenshots stay for other cards; one-off captures are removed.
        if (picture != null && picture.name.startsWith("now-")) picture.delete()
    }

    // ---- What Anki already has ----

    /**
     * What this device's copy of the deck ([AnkiIndex]) says about [word] at once, without
     * asking Anki: whether a card made from it now would be one Anki has, by the note type's
     * first field in the deck cards go to (or anywhere, when the user chose that). [Known.setUp]
     * is false when no card can be made (cards off, Anki not set up): there is then nothing to
     * ask. Brings the copy up to date in the background when that is due. Main thread.
     */
    data class Known(
        val setUp: Boolean,
        val answer: AnkiIndex.Answer = AnkiIndex.Answer.NOT_READY,
        val notes: List<Long> = emptyList()
    )

    fun knownNow(presented: PresentedLine, word: LookupResult, wordRange: IntRange?): Known {
        val (client, _) = client(quick = true)
        val model = settings.ankiModel
        val templates = CardBuilder.parseMapping(settings.ankiFieldMap).filterValues { it.isNotBlank() }
        if (client == null || settings.ankiDeck.isBlank() || model.isBlank() || templates.isEmpty()) {
            return Known(setUp = false)
        }
        // Anki is asked for the field first (confirm); the copy is made after that.
        val field = fieldOf(model) ?: return Known(setUp = true)
        val source = sourceOf(field, scopeDeck())
        keepIndex(client, source)
        val card = cardOf(presented, word, wordRange)
        val value = keyIn(field, templates, card)?.second ?: return Known(setUp = true)
        val found = index.find(source, value)
        return Known(setUp = true, answer = found.answer, notes = found.notes)
    }

    /**
     * What Anki says about each of [words] (all found at [offset] in the line), asked in one
     * request: [notes] has one entry per word, its notes (empty when there are none) or null
     * when that cannot be told for it; [reached] is false when Anki did not answer.
     */
    data class Confirmed(val reached: Boolean, val notes: List<List<Long>?>)

    fun confirm(presented: PresentedLine, words: List<LookupResult>, offset: Int, callback: (Confirmed) -> Unit) {
        val (client, _) = client(quick = true)
        val model = settings.ankiModel
        val templates = CardBuilder.parseMapping(settings.ankiFieldMap).filterValues { it.isNotBlank() }
        if (client == null || settings.ankiDeck.isBlank() || model.isBlank() || templates.isEmpty() || words.isEmpty()) {
            callback(Confirmed(true, List(words.size) { null }))
            return
        }
        val scope = scopeDeck()
        val cards = words.map { cardOf(presented, it, offset until offset + it.matchedText.length) }
        checker.execute {
            val confirmed = try {
                if (System.currentTimeMillis() < quietUntil) {
                    Confirmed(false, List(cards.size) { null })
                } else {
                    val field = fieldOf(client, model)
                    val keys = cards.map { card -> field?.let { keyIn(it, templates, card)?.second } }
                    val asked = keys.filterNotNull()
                    val found = if (field == null) emptyList() else client.findNotesEach(scope, field, asked)
                    val source = field?.let { sourceOf(it, scope) }
                    // The field may have been learnt just now: the copy of the deck can be made.
                    source?.let { keepIndex(client, it) }
                    var next = 0
                    Confirmed(true, keys.map { key ->
                        if (key == null || source == null) {
                            null
                        } else {
                            found[next++].also { index.learn(source, key, it) }
                        }
                    })
                }
            } catch (e: Exception) {
                // An Anki that is not there is asked again later, not for every word tapped.
                quietUntil = System.currentTimeMillis() + QUIET_AFTER_FAILURE_MS
                Log.warning("[Reader] Could not ask Anki about a word: ${e.message}")
                Confirmed(false, List(cards.size) { null })
            }
            main.post { callback(confirmed) }
        }
    }

    /** Reads the deck into this device's copy when that is due, without holding anything up. */
    private fun keepIndex(client: AnkiClient, source: AnkiIndex.Source) {
        val load = !indexLoaded
        indexLoaded = true
        if (!load && (System.currentTimeMillis() < quietUntil ||
                index.whatIsDue(source, System.currentTimeMillis()) == AnkiIndex.Due.NOTHING)
        ) {
            return
        }
        if (indexBusy.getAndSet(true)) return
        val file = File(context.filesDir, INDEX_FILE)
        indexer.execute {
            try {
                // What was read before the app was closed answers until Anki is asked.
                if (load) index.load(file)
                val now = System.currentTimeMillis()
                val due = index.whatIsDue(source, now)
                if (due != AnkiIndex.Due.NOTHING && now >= quietUntil) {
                    // Not the quick client: a large deck takes a while to read.
                    val reader = client(quick = false).first ?: client
                    if (due == AnkiIndex.Due.FILL) index.fill(reader, source, now) else index.refresh(reader, source, now)
                    index.save(file)
                }
            } catch (e: Exception) {
                quietUntil = System.currentTimeMillis() + QUIET_AFTER_FAILURE_MS
                Log.warning("[Reader] Could not read the deck from Anki: ${e.message}")
            } finally {
                indexBusy.set(false)
            }
        }
    }

    /** The lines read before [presented], oldest first, where a field asks for them. */
    private fun linesBefore(templates: Collection<String>, presented: PresentedLine): List<String> {
        val markers = CardBuilder.markersIn(templates)
        val wanted = when {
            "context" in markers -> settings.cardContextLines
            "previous-sentence" in markers -> 1
            else -> 0
        }
        val game = ReaderSession.gameKey
        if (wanted == 0 || presented.backlogId < 0 || game.isNullOrEmpty()) return emptyList()
        return try {
            // A few more than wanted: choices and menus are passed over.
            ReaderSession.database(context).recentLines(game, wanted + 4, presented.backlogId)
                .filter { !it.line.isChoice && !it.line.isMenu }
                .take(wanted).map { it.line.text }.reversed()
        } catch (e: Exception) {
            emptyList()
        }
    }

    private fun cardOf(presented: PresentedLine, word: LookupResult, wordRange: IntRange?) = CardContext(
        presented.line, ReaderSession.gameTitle, word, wordRange,
        gameUrl = SourceAddress.of(ReaderSession.gameKey)
    )

    private fun sourceOf(field: String, scope: String?) = AnkiIndex.Source(
        settings.ankiTransport + "\u001f" + settings.ankiConnectUrl.trim(), scope, field
    )

    /** Opens Anki's browser on [notes]; [callback] gets null, or what went wrong. Main thread. */
    fun showNotes(notes: List<Long>, callback: (String?) -> Unit) {
        val (client, problem) = client()
        if (client == null) {
            callback(problem ?: "")
            return
        }
        worker.execute {
            val failure = try {
                client.showNotes(notes)
                null
            } catch (e: Exception) {
                e.message ?: e.javaClass.simpleName
            }
            main.post { callback(failure) }
        }
    }

    /** The deck existing notes are looked for in, or null for the whole collection. */
    private fun scopeDeck(): String? = if (settings.ankiDuplicatesAnywhere) null else settings.ankiDeck

    /**
     * The field Anki tells notes apart by (the note type's first), when it is known without
     * asking Anki: recorded when the setup was saved, or asked of Anki once since the app runs.
     */
    private fun fieldOf(model: String): String? =
        settings.ankiKeyField(model) ?: synchronized(keyFields) { keyFields[model] }

    /** The same, asked of Anki once for a setup saved before the field was recorded. */
    private fun fieldOf(client: AnkiClient, model: String): String? = fieldOf(model)
        ?: client.fieldNames(model).firstOrNull()?.also { synchronized(keyFields) { keyFields[model] = it } }

    /** The field and what a card made from [card] would put in it, or null when nothing. */
    private fun keyIn(field: String, templates: Map<String, String>, card: CardContext): Pair<String, String>? {
        val template = templates[field] ?: return null
        val value = CardBuilder.build(mapOf(field to template), card)[field].orEmpty()
        return if (value.isBlank()) null else field to value
    }

    /**
     * The field Anki tells notes apart by and what a card made from [card] would put in it, or
     * null when the mapping leaves that field empty.
     */
    private fun keyOf(
        client: AnkiClient,
        model: String,
        templates: Map<String, String>,
        card: CardContext
    ): Pair<String, String>? = fieldOf(client, model)?.let { keyIn(it, templates, card) }

    companion object {
        private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "ReaderAnki") }

        // Questions about what Anki has do not wait behind a card that is being sent.
        private val checker = Executors.newSingleThreadExecutor { r -> Thread(r, "ReaderAnkiCheck") }

        /** Reads the deck into [index], without holding up cards or questions. */
        private val indexer = Executors.newSingleThreadExecutor { r -> Thread(r, "ReaderAnkiIndex") }

        /** What the deck holds, on this device, and whether its file was read yet. */
        private val index = AnkiIndex()
        private val indexBusy = AtomicBoolean(false)

        @Volatile
        private var indexLoaded = false

        /** First fields of note types, as Anki named them. */
        private val keyFields = HashMap<String, String>()

        @Volatile
        private var quietUntil = 0L

        /**
         * Told of every card added, wherever it was made (the popup, an app's own screen), on the
         * main thread: an app that keeps a list of the day's cards (Furochibi's 今日) hears of
         * them here.
         */
        @Volatile
        var onAdded: ((presented: PresentedLine, word: LookupResult?, noteId: Long) -> Unit)? = null

        /** Stands in for the running game's pictures and audio, where there is no emulator. */
        @Volatile
        internal var mediaOverride: CardMedia? = null

        private const val INDEX_FILE = "reader/anki_index.json"
        private const val QUIET_AFTER_FAILURE_MS = 20_000L
        private const val QUICK_CONNECT_MS = 1_500
        private const val QUICK_READ_MS = 5_000

        /** Forgets what was learned from Anki, for tests and after the setup changed. */
        fun forget() {
            synchronized(keyFields) { keyFields.clear() }
            quietUntil = 0L
            // The copy of the deck is read again, from its file and then from Anki.
            indexLoaded = false
        }
    }
}
