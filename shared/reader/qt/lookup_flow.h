// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QRect>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include "reader/desktop/dictionary.h"
#include "reader/desktop/reader_database.h"
#include "reader/desktop/screen_text.h"
#include "reader/qt/audio_prompt.h"
#include "reader/qt/lookup_popup.h"
#include "reader/qt/reader_media.h"

namespace ReaderUi {

/**
 * Dictionary lookups over some text: runs the lookup for a character, shows the popup beside
 * the text, says which word was matched so that it can be marked, and makes cards from the
 * results. Used for the game's own text and for the backlog; each has one of its own.
 *
 * Main thread. The dictionary is searched on a thread of this object's.
 */
class LookupFlow : public QObject {
    Q_OBJECT

public:
    /**
     * Where the popup belongs, in the coordinates of `bounds`' owner: above or below
     * `keep_clear` (the text the word is in, which the popup must not cover), starting near
     * `near_x` (the word), and inside `bounds`.
     */
    struct Anchor {
        QRect bounds;
        QRect keep_clear;
        int near_x{};
        /// The widget those coordinates are in: the popup is placed on screen from them.
        QWidget* owner{};
    };

    /// `popup_parent` is the window the popup belongs to and stays above.
    explicit LookupFlow(QWidget* popup_parent);
    ~LookupFlow() override;

    /// Marks [first, last] of the line `backlog_id`'s text, or takes the mark away (-1).
    std::function<void(qint64 backlog_id, int first, int last)> mark;
    /// Says a few words to the user: how a card request ended.
    std::function<void(const QString&)> notify;
    /// The popup's "open the dictionaries" was pressed.
    std::function<void()> open_dictionaries;

    /**
     * Looks up the word starting at `offset` of `presented`'s text and shows what was found
     * at `beside`. A `quiet` lookup that finds no word opens nothing, and closes a popup that
     * is open (the pointer went from its word to something else): for lookups nobody asked for
     * by hand, such as under a pointer passing over punctuation.
     */
    /// `longest` (when above 0): the word is looked for in that many UTF-16 units from `offset`
    /// and no further (a match made shorter or longer by hand: reading with a controller).
    void Lookup(const ReaderApp::PresentedLine& presented, int offset, const Anchor& beside,
                bool quiet = false, int longest = -1);
    /**
     * Whether `word`, found at [first, last] of `presented`, is in Anki, as this computer's copy
     * of the deck says at once (the popup's "not in Anki" mark asks the same): 1 it is not, 0 it
     * is or the copy cannot tell yet, -1 no card can be made (Anki is not set up). Main thread.
     */
    [[nodiscard]] int NotInAnki(const ReaderApp::PresentedLine& presented,
                                const ReaderApp::LookupResult& word, int first, int last);
    void Close();
    /// Makes a card of `presented` alone, without a word.
    void MineSentence(const ReaderApp::PresentedLine& presented);
    /// A card of the word the open popup shows first; false when no popup is open.
    bool MineShown();
    /// Takes the card made last back out of Anki, and says so.
    void UndoLast();

    [[nodiscard]] bool IsOpen() const;

    /// A controller's button, while the popup is open and has the controller (it is modal).
    enum class PadKey {
        EntryBefore,   ///< d-pad up
        EntryNext,     ///< d-pad down
        TabBefore,     ///< L1
        TabNext,       ///< R1
        Card,          ///< north: a card without the line's audio
        CardWithAudio, ///< east: a card with it
        Close,         ///< south
    };
    /// Does what `key` does in the open popup; false when no popup is open.
    bool OnPad(PadKey key);
    /// The left stick's up and down while the popup is open: -1 (up) to 1 (down), once a frame.
    void PadScroll(float y);
    /// True when the open popup is about the character at `offset` of the line `backlog_id`.
    [[nodiscard]] bool IsAbout(qint64 backlog_id, int offset) const;
    /**
     * The pointer moved: the mouse, or the controller's cursor. `inside`: it is on the popup
     * or on the popup's word. Inside, the popup's time starts over; outside, the popup goes
     * away after a moment (long enough to cross from the word to the popup) when the settings
     * say so. The mouse on the popup itself is seen here.
     */
    void PointerMoved(bool inside);
    /// Asked when the popup comes up and when its time is up: whether the pointer is on the
    /// popup or on its word right now (it may have been resting there without moving).
    std::function<bool()> pointer_inside_now;
    /// How long the pointer may be off the word and the popup before the popup goes (a slip
    /// off the word; the way to the popup is the popup's anyway, ReaderApp::KeptByPopup).
    static constexpr int ExitGraceMs = 150;
    /// A popup up this long counts its word as looked up (CountLookup).
    static constexpr int CountAfterMs = 1000;
    /// How long a popup drawn by the page waits for the page's layout at most.
    static constexpr int LayoutWaitMs = 1500;
    [[nodiscard]] LookupPopup* Popup() const {
        return popup.data();
    }
    /// True while a card's question about its audio is up.
    [[nodiscard]] bool IsAsking() const {
        return !asking.isEmpty();
    }
    /// Calls off every card that is waiting for an answer about its audio.
    void CancelQuestions();
    [[nodiscard]] AudioPrompt* Prompt() const {
        return prompt.data();
    }
    /// Where a question goes that belongs to no popup (a card of the sentence): the middle
    /// of this widget.
    void SetHome(QWidget* widget) {
        home = widget;
    }

    /// The anchor has moved (the window was moved or resized): the popup follows.
    void Reposition(const Anchor& beside);
    /// The window the popup belongs to changed (the game went to full screen).
    void SetPopupParent(QWidget* parent);

signals:
    /// The popup went away, however that came about.
    void Closed();
    /// A lookup's popup came up.
    void Opened();

private:
    struct Pinned {
        ReaderApp::PresentedLine presented;
        int offset{};
        QList<ReaderApp::LookupResult> results;
        /// The notes Anki has for the words of the open popup, as far as it has answered.
        QHash<int, QList<qint64>> notes;
        /// Its word was counted as looked up (CountLookup).
        bool counted{};
    };

    void OnLookupDone(int request, const ReaderApp::PresentedLine& presented, int offset,
                      bool quiet, bool has_dictionaries,
                      const QList<ReaderApp::LookupResult>& results,
                      const QList<ReaderApp::LookupHistory>& history);
    void AskAnki();
    /// Places and shows the popup with what it has been given.
    void ShowNow();
    /// How wide the popup is: as wide as it may be beside its anchor.
    [[nodiscard]] int PopupWidth() const;
    /**
     * Counts the open popup's word as looked up, once: when the popup stayed up for a moment
     * (CountAfterMs) or something was done in it. Passing over a word, which looks it up at
     * once, does not count; nor does the same word in the same line again.
     */
    void CountLookup();
    /// A card of the popup's `index`th word: `audio` with the line's audio or without, or as the settings say.
    void Mine(int index, std::optional<bool> audio = std::nullopt);
    void ShowNotes(int index);
    void Report(const Miner::Outcome& outcome);
    void Position();
    void Ask(const MediaCapture::Clip& clip, Miner::AudioAnswer answer);
    void ShowQuestion();
    void PlaceQuestion();
    void AnswerQuestion(std::optional<bool> keep);

    /// Its window's, and gone with it: the pointer says whether it is still there.
    QPointer<LookupPopup> popup;
    Miner miner;
    QThreadPool worker;
    std::optional<Pinned> pinned;
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// Starts the popup's time over: it closes by itself when left alone for the time in the
    /// settings.
    void KeepOpen();
    /// The word the open popup is about, also when nothing was found for it.
    qint64 word_line{-1};
    int word_first{-1};
    int word_last{-1};
    bool pointer_inside{};
    QTimer hide_timer;
    QTimer count_timer;
    /// The result whose plus asked about a card's audio (its question comes up over it), or -1.
    int asking_index{-1};
    /// The question stands in the popup page (not in its own window).
    bool asking_on_page{};
    /// A popup drawn by the page waits for the page's layout before it is shown.
    bool show_pending{};
    QTimer exit_timer;
    Anchor anchor;
    /// Counts what the popup has shown: answers about an earlier content are dropped.
    int token{};
    /// Counts lookups: only the latest one's result is shown, and one that is no longer the
    /// latest when its turn on the worker comes is not made (read there).
    std::atomic<int> asked{};

    /// A card that waits for the answer about its audio.
    struct Asked {
        MediaCapture::Clip clip;
        Miner::AudioAnswer answer;
    };
    /// One question is shown at a time; the others wait their turn.
    QList<Asked> asking;
    QPointer<AudioPrompt> prompt;
    ClipPlayer player;
    QPointer<QWidget> home;
};

} // namespace ReaderUi
