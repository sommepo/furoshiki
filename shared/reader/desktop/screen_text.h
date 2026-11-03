// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include "reader/core/console.h"
#include "reader/desktop/stats.h"
#include "reader/desktop/text_line.h"

namespace ReaderApp {

/// A line after it was counted and stored in the backlog.
struct PresentedLine {
    TextLine line;
    LineAccounting accounting;
    qint64 backlog_id{-1};
};

/**
 * Where the text of a line is in the game's picture, as reported by the game's profile. This
 * is what makes the game's own text something the player can point at.
 */
struct ScreenLayout {
    /// The console's screen, which is what a layout is on unless it says otherwise, and a second
    /// screen's width where the console has one (reader/core/console.h). Asked each time: in a
    /// program that plays several consoles (Reader Home) they are said at run time.
    static float DefaultWidth() { return Reader::ConsoleTraits::ScreenWidth; }
    static float DefaultHeight() { return Reader::ConsoleTraits::ScreenHeight; }
    static float LowerWidth() { return Reader::ConsoleTraits::LowerScreenWidth; }

    /// The TextLine::window this belongs to.
    QString window;
    /// The TextLine::sequence of the line this was reported for.
    qint64 sequence{};
    /// The text is no longer on screen; the other fields are empty.
    bool closed{};
    /// Which of the console's screens the text is on: 0 the (upper) one, 1 the lower one of a
    /// console that has two.
    int screen{};
    /// The size of the picture (of that screen) in the units of the rectangles: a game lays its
    /// screen out in units of its own (often 1280x720 or 1920x1080), whatever it is drawn at.
    float space_width{DefaultWidth()};
    float space_height{DefaultHeight()};
    /// The text box as a whole, when the profile knows it.
    std::optional<QRectF> box;
    /// One entry per Unicode code point of the line's text: the cell the character occupies,
    /// or nothing where nothing is drawn (a line break, a character not yet typed out).
    QList<std::optional<QRectF>> cells;

    /// Parses the engine's layout JSON. Nothing when it is not a layout.
    [[nodiscard]] static std::optional<ScreenLayout> FromJson(const QByteArray& json);
};

/// A line together with where its text is in the game's picture right now.
struct ScreenText {
    PresentedLine presented;
    ScreenLayout layout;
};

/**
 * Keeps track of which lines are on screen and where. A line and the layout reported for it
 * arrive separately and in either order (the line passes through the session's worker first),
 * so whichever comes first waits for the other. One thread only.
 */
class ScreenTexts {
public:
    /// The lines whose text the game is showing, with where each character is, in the order
    /// their windows first showed text.
    [[nodiscard]] QList<ScreenText> OnScreen() const;
    /// Where `presented` is on screen, or nothing when its text is not shown (any more).
    [[nodiscard]] std::optional<ScreenText> PlacementOf(const PresentedLine& presented) const;

    /// `presented` is now the text of its window. True when OnScreen changed.
    bool AdoptLine(const PresentedLine& presented);
    /// A layout arrived from the game. True when OnScreen changed.
    bool ApplyLayout(const ScreenLayout& layout);
    /// Forgets everything. True when OnScreen changed.
    bool Clear();

private:
    QHash<QString, PresentedLine> lines;
    QHash<QString, ScreenLayout> waiting;
    QHash<QString, ScreenText> shown;
    QStringList shown_order;
};

/**
 * The text of one of the game's text boxes as it sits in a view: answers what is under a
 * pointer, and where a word is so that it can be marked. Coordinates are those of the view the
 * game's picture is drawn in.
 */
class PlacedText {
public:
    /// One character of the line.
    struct Glyph {
        /// Index into the line's text (UTF-16), where a lookup would start.
        int offset{};
        int row{};
        /// The cell the game drew the character in.
        QRectF drawn;
        /// What a pointer has to hit: the cell grown to meet its neighbours, the gap above its
        /// row (where the game puts furigana) and some room at the ends of a row.
        QRectF touch;
    };

    enum class HitKind {
        Outside,  ///< Not ours: the game gets the click.
        Box,      ///< In the text box, but on no character.
        Character ///< On a character: `glyph` says which.
    };
    struct Hit {
        HitKind kind{HitKind::Outside};
        const PlacedText* text{};
        const Glyph* glyph{};
    };

    QString window;
    PresentedLine presented;
    /// The text box as far as clicks go: nothing inside it reaches the game.
    QRectF box;
    QList<Glyph> characters;
    /// The rows of text with their margins: what a popup about a word should not cover.
    QRectF rows;

    [[nodiscard]] Hit HitTest(const QPointF& point) const;
    /// One rectangle per row for the characters at [first, last] of the line's text.
    [[nodiscard]] QList<QRectF> Marks(int first, int last) const;

    /**
     * Places `presented`, whose text is laid out as `layout`, where the game's picture is
     * `area`. Nothing when nothing is drawn yet.
     */
    /// The picture may be bent on its way to the screen (the glass of the CRT picture,
    /// crt.h in the reader's core): `bend` moves a place of the picture, measured 0 to 1 from its top
    /// left corner, to where it is seen. Every cell placed from then on is moved with its
    /// middle. Nothing bends until this is set.
    static void SetBend(std::function<void(float& x, float& y)> bend);

    [[nodiscard]] static std::optional<PlacedText> Place(const PresentedLine& presented,
                                                         const ScreenLayout& layout,
                                                         const QRectF& area);
};

/// A rectangle given in the units of `layout`, as drawn where the picture is `area`.
[[nodiscard]] QRectF PlaceCell(const QRectF& cell, const ScreenLayout& layout, const QRectF& area);

/**
 * Hover lookups while a popup is open, for a pointer at `point` that is not on the popup
 * itself (`popup`, in the same coordinates; `word_row` is the row of the word it is about,
 * `hit` what is under the pointer, `popups_word` whether that is the popup's own word). True
 * when the pointer is still the popup's: on its own word, on a character the popup covers (its
 * middle is under the popup: one that only peeks under its edge is not), or in the narrow gap
 * between that word's row and the popup. Nothing there is looked up. Any other word
 * is looked up at once, without waiting for the pointer to rest (the maintainer, 2026-10-22:
 * "hovering on any word should be instant unless it is behind the popup directly").
 * The same rule as PlacedText.keptByPopup in the Android app.
 */
[[nodiscard]] bool KeptByPopup(const QPointF& point, const PlacedText::Hit& hit, bool popups_word,
                               const QRectF& popup, const QRectF& word_row);

} // namespace ReaderApp
