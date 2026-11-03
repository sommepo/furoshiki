// SPDX-FileCopyrightText: Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The reading layer of the Vita apps (PS1 Reader, Adrenaline Reader), all but its drawing:
// where the game's text is on the screen (from the script's layouts), a word looked up where
// it is, and what that puts over the game (scene.h, drawn by scene_draw.c).
//
// - A tap on the front screen on a character looks up the word that begins there; a tap
//   anywhere else closes the popup.
// - The rear touch pad is a trackpad, as the Thor's second display is (the Android apps'
//   ReaderOverlay): a finger moves the pointer by as much as it moves, wherever it touches,
//   and the word under the pointer is looked up as it comes onto another character (the
//   desktop's hover); a short tap on the pad looks up where the pointer is. The pointer shows
//   while it moves and goes after a moment still. A short tap of two fingers on the pad
//   switches it off and on (on at first); an app may give a button for that too
//   (TogglePointer), as PS1 Reader does with L + R.
// - A double tap on the rear pad puts the current line as raw text on a filled box over the
//   game's own text, a size larger, the rest of the game showing (the plain view), as a manga
//   reader's overlay does; another double tap or ○ or × takes it away. It has the line as the game
//   last gave it, and its characters stand on a grid of this layer's own: tapped or pointed at, a
//   word is found there however far a script's layout is from the picture. With no place for the
//   text at all the box stands in the middle of the screen. Up and down go through the lines before
//   it, the last 200 the game gave: the reader's backlog, each line tappable and a card as any other. It is modal like the popup, the line
//   stays while the game goes on, and the pad's pointer works in it even if switched off.
// - While the popup is up it is modal: the app keeps the controller from the game (Modal), the
//   left stick scrolls the popup, ○ or × closes it, △ asks for a card of the word (Pad, TakeCard).
//   A button held when the popup goes stays the popup's until it is let go.
// - Lookups run on a thread of their own (a read from the memory card costs milliseconds), so
//   the game never waits for the dictionary.
//
// Touches come in as plain points and times (Touch), so that all of it runs on a computer too
// (tests/lookup_layer_test.cpp). Every function may be called from any thread.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "reader/core/engine.h"
#include "reader/lookup/dictionary.h"
#include "reader/vita/scene.h"

namespace VitaReader {

/// Where the console's picture is drawn on the Vita's screen (960 by 544).
struct PictureRect {
    float x{};
    float y{};
    float width{};
    float height{};
};

/// One touch panel as it is now: how many fingers, and where the first one is. The front's in
/// the screen's pixels (960 by 544: the panel's own units halved), the rear's in the pad's own
/// units (PadArea).
struct Touch {
    int fingers{};
    float x{};
    float y{};
};

/// The controller as the popup takes it.
struct PadInput {
    float stick_y{};  ///< the left stick, -1 (up) to 1 (down)
    bool close{};     ///< ○ or ×
    bool add{};       ///< △
    bool any{};       ///< any button at all
    bool up{};        ///< the d-pad; the stick leaning as far does the same
    bool down{};
};

/// The rear pad's extent in its own units (sceTouchGetPanelInfo's active area).
struct PadArea {
    float min_x = 0;
    float min_y = 0;
    float max_x = 1919;
    float max_y = 889;
};

/// A card asked for with △: the word, and the line it is in as HTML with the word in bold.
struct CardRequest {
    std::string expression;
    std::string reading;
    std::string glossary; ///< HTML, as the desktop's {glossary}
    std::string sentence;
};

class LookupLayer {
public:
    explicit LookupLayer(PadArea rear = {});
    ~LookupLayer();
    LookupLayer(const LookupLayer&) = delete;
    LookupLayer& operator=(const LookupLayer&) = delete;

    /// Opens the dictionary (reader_dict.db); without one, nothing is looked up. On the Vita
    /// the system's SQLite is made ready first. `cache_kib`: SQLite's page cache (less inside
    /// another program, as in Adrenaline).
    bool OpenDictionary(const std::string& path, std::string& error,
                        int cache_kib = ReaderLookup::Dictionary::CacheKiB);
    /// The engine's text events and layouts: from its callbacks (PS1 Reader), or handed on by
    /// a FrameSession (FrameSession::SetListeners).
    void OnText(const Reader::TextEvent& event);
    void OnLayout(const Reader::TextLayout& layout);

    void SetPicture(const PictureRect& picture);
    /// Once a frame: both panels, and the time (microseconds, any steady clock).
    void Input(const Touch& front, const Touch& rear, std::uint64_t now_us);
    /// The rear pad's pointer off or on.
    void TogglePointer(std::uint64_t now_us);
    /// How far the pointer goes for a stroke: how many screen widths a stroke across the whole
    /// pad's width takes it (Settings::pointer_speed).
    void SetPointerSpeed(float screens_per_pad);
    /// Once a frame: the controller (it does something only while the popup is modal).
    void Pad(const PadInput& pad, std::uint64_t now_us);
    /// Whether the controller is the popup's now (the app keeps it from the game).
    [[nodiscard]] bool Modal() const;
    /// The word a new popup shows, for an app that can ask whether a deck has it; once a popup.
    [[nodiscard]] std::optional<std::string> TakeWordShown();
    /// A deck has `expression`: the popup has a tick while it shows that word.
    void SetInDeck(const std::string& expression, bool in_deck);
    /// The card asked for since this was last called, if any.
    [[nodiscard]] std::optional<CardRequest> TakeCard();
    /// A short word for the player at the top of the screen, until the time given.
    void SetStatus(const std::string& text, std::uint64_t until_us);
    /// The reading so far for the upper right corner; empty: nothing there.
    void SetSessionLine(const std::string& text);
    /// Whether the picture has text to tap now: a game that takes touches is kept from them then.
    [[nodiscard]] bool TextOnScreen() const;
    /// How far the popup can scroll, in lines: what the drawing found (readerSceneDraw).
    void SetScrollLimit(int lines);

    /// What to draw now.
    void Scene(ReaderScene& scene, std::uint64_t now_us) const;
    /// Whether there is anything to draw (the pointer, the popup or the plain view).
    [[nodiscard]] bool Showing(std::uint64_t now_us) const;

    /// Something for the log (a lookup's result and its time), or empty.
    [[nodiscard]] std::string TakeNote();

    /// The character last asked about (its window and index), for tests.
    [[nodiscard]] std::optional<std::pair<std::string, std::size_t>> Asked() const;
    /// Whether a lookup is waiting or running, for tests.
    [[nodiscard]] bool Busy() const;

private:
    struct Window {
        std::u32string text; ///< the latest event's text, code point by code point
        Reader::TextLayout layout;
        std::uint64_t sequence{};
    };
    /// A character of a window under a point of the screen.
    struct Hit {
        std::string window;
        std::size_t index{};
    };
    struct Request {
        std::string window;
        std::uint64_t sequence{};
        std::size_t index{};
        std::string text; ///< UTF-8, from the character on
    };
    /// The plain view: the line on its grid.
    struct Plain {
        std::u32string text;
        int columns{};
        float left{}, top{}, cell{}, row{};
        ReaderSceneRect box{};
    };
    struct Answer {
        Request request;
        std::size_t length{}; ///< code points of the text the first word covers
        std::vector<std::pair<std::uint16_t, std::string>> entries; ///< the popup's (scene.h)
        CardRequest card; ///< the first word's, without its sentence
        bool in_deck = false;
    };

    [[nodiscard]] std::optional<Hit> HitTest(float x, float y) const;
    void LookUp(const Hit& hit);
    void Work();
    void Close();
    void Toggle(std::uint64_t now_us);
    void TogglePlain(std::uint64_t now_us);
    /// The plain view of `line`, in place of any before it.
    void ShowPlain(const std::string& line, std::uint64_t now_us);
    /// The plain view's grid and box over the game's own text; false where it has no place.
    [[nodiscard]] bool Over(const std::u32string& text, Plain& view) const;
    /// The text a window has, the plain view's under its own name.
    [[nodiscard]] const std::u32string& TextOf(const std::string& window);
    [[nodiscard]] bool PointerWorks() const { return pointer_on || plain.has_value(); }

    mutable std::mutex mutex; ///< everything below
    std::map<std::string, Window> windows;
    std::string latest;          ///< the last line the game gave, whatever showed it
    std::string latest_window;   ///< and what it was shown in
    std::deque<std::string> history; ///< the lines the game gave, the newest last
    std::size_t history_back = 0;    ///< how many lines back from the newest the plain view is
    bool up_down = false, down_down = false;
    std::optional<Plain> plain;
    PictureRect picture{};
    PadArea rear_area;

    ReaderLookup::Dictionary dictionary;
    std::thread worker;
    std::condition_variable wake;
    std::optional<Request> pending;
    bool working = false;
    std::optional<Answer> answer; ///< what the popup shows
    std::optional<Hit> asked;     ///< the character last asked about
    bool stopping = false;
    std::string note;
    std::optional<CardRequest> card_asked;
    std::optional<std::string> word_shown;
    std::string status;
    std::string session_line;
    std::uint64_t status_until_us = 0;

    // Input
    bool front_down = false;
    float front_x = 0, front_y = 0, front_start_x = 0, front_start_y = 0;
    bool pointer_on = true;
    // The pad
    bool holding = false; ///< buttons held from the popup's time, kept from the game
    bool close_down = false, add_down = false;
    float scroll = 0;     ///< in lines
    int scroll_limit = 0;
    std::uint64_t pad_us = 0;
    bool rear_down = false;
    bool rear_moved = false;
    int rear_most_fingers = 0;
    float rear_x = 0, rear_y = 0, rear_start_x = 0, rear_start_y = 0;
    std::uint64_t rear_start_us = 0;
    std::optional<std::uint64_t> rear_tap_us; ///< when the last tap ended, a double tap's first
    float rear_tap_x = 0, rear_tap_y = 0;
    float pointer_x = 480, pointer_y = 272;
    float pointer_speed = 2.0f;
    std::uint64_t pointer_seen_until_us = 0; ///< shown until then
};

} // namespace VitaReader
