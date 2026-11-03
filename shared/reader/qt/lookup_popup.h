// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QList>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>
#include "reader/desktop/dictionary.h"
#include "reader/desktop/reader_database.h"

class QTextDocument;

namespace ReaderUi {
class PopupWeb;
}

namespace ReaderUi {

/**
 * The dictionary popup: always as wide as it may be for words found (a message is only as
 * wide as what it says), and only as tall as what it says. Per headword there is one line with the word, its reading and how it was inflected,
 * then one line per sense with its glosses run together. In the bottom right corner of each
 * headword's part sits a boxed button: a plus to make a card, or an open book when Anki has
 * the word already; a turning ring while that is not known yet.
 *
 * A window of its own, without a frame, that never takes the keyboard from the game.
 */
class LookupPopup : public QWidget {
    Q_OBJECT

public:
    /// What the box at the foot of a headword offers.
    enum class CardState {
        Add,      ///< Not in Anki, as far as is known: a plus that makes a card.
        Working,  ///< A card is being made.
        InDeck,   ///< Anki has the word: an open book that shows its notes.
        Checking, ///< Not known yet: a turning ring, which makes a card all the same.
        Away      ///< Anki did not answer: a plus with a grey dot, which tries all the same.
    };

    explicit LookupPopup(QWidget* parent);
    ~LookupPopup() override;

    /// Size of the popup's text relative to its normal size, in percent.
    void SetTextScale(int percent);
    /// Keep offering the plus beside the book for a word Anki already has.
    void SetAllowDuplicates(bool allow);
    /// The corner of each headword's part where its boxes stand.
    enum class Corner { BottomLeft, BottomRight, TopLeft, TopRight };
    void SetCardCorner(Corner corner);
    /// From the setting's value ("bottom_left" and so on); the bottom left otherwise.
    [[nodiscard]] static Corner CornerOf(const QString& value);

    /**
     * Shows the words found. With `history` (one per result, from the reader database), a
     * quiet line under each headword says how often it was looked up before; `with_where`
     * adds when and in which game first.
     */
    void ShowResults(const QList<ReaderApp::LookupResult>& results,
                     const QList<ReaderApp::LookupHistory>& history = {}, bool with_where = false);
    /**
     * The popup's look: "classic" (drawn here, as before) or a theme of the popup page
     * ("console": the console's colours, "furoshiki", "yomitan-dark", "paper", "paper-dark"), drawn by the
     * web engine where this build and system have one (PopupWeb); else classic. Call before
     * ShowResults.
     */
    void SetTheme(const QString& theme);
    /// A theme's CSS for the popup page ("console": none, the page's own), and the console's
    /// colours as the page's CSS variables: for a preview of the page elsewhere.
    [[nodiscard]] static QString ThemeStyle(const QString& theme);
    [[nodiscard]] static QString ConsoleStyle();
    /// The user's own CSS, applied after the theme (the page's "user" stylesheet).
    void SetUserCss(const QString& css);
    /// The console the popup is for, as the page's `data-console` says it (a theme or the
    /// user's CSS can tell consoles apart by it): a program that plays several consoles sets it
    /// for each game; else the app's own key.
    static void SetConsoleKey(const QString& key);
    /// The question about a card's audio in the page, in the place of result `index`'s
    /// buttons (ListenRequested, KeepRequested, DropRequested); stop instead of listen while the
    /// clip plays; the buttons back.
    void AskOnPage(int index);
    void AskPlaying(bool playing);
    void AskDone();
    /// The page draws the popup (a theme is in use and its engine runs).
    [[nodiscard]] bool IsWeb() const {
        return web != nullptr;
    }
    /// The page has said how tall the popup is since the last ShowResults or ShowMessage.
    [[nodiscard]] bool HasLayout() const {
        return web == nullptr || web_layout;
    }
    /// A word not in Anki gets a soft tint behind its headword, when that is switched on.
    void SetMarkUnknown(bool mark);
    void SetUnknown(int index, bool unknown);
    [[nodiscard]] bool IsUnknown(int index) const;
    void ShowMessage(const QString& message, bool offer_dictionaries);

    /// What the box of result `index` offers.
    [[nodiscard]] CardState GetCardState(int index) const;
    void SetCardState(int index, CardState state);
    [[nodiscard]] int ResultCount() const {
        return static_cast<int>(entries.size());
    }

    /// Lays the content out for a popup no larger than this, and returns the size it takes.
    [[nodiscard]] QSize Fit(int max_width, int max_height);
    /// How far the content is scrolled, and how far it can be.
    [[nodiscard]] int Scrolled() const {
        return web ? web_scrolled : scroll;
    }
    void ScrollTo(int y);
    /// Where the boxes of result `index` are, in the popup's coordinates. For tests and for
    /// the cursor's clicks.
    [[nodiscard]] QRect AddBox(int index) const;
    [[nodiscard]] QRect BookBox(int index) const;
    /// What the popup says, as plain text. For tests.
    [[nodiscard]] QString PlainText() const;

    /// The largest width the popup takes at the current text scale.
    [[nodiscard]] int MaxWidth() const;

signals:
    void AddRequested(int index);
    void NotesRequested(int index);
    void DictionariesRequested();
    /// The page laid the popup out anew (its height may have changed), or its engine failed
    /// and the popup is drawn here from now on.
    void LayoutChanged();
    void ListenRequested();
    void KeepRequested();
    void DropRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool pointer_set{};

    struct Entry {
        std::unique_ptr<QTextDocument> document;
        CardState state{CardState::Add};
        /// In content coordinates (before scrolling).
        int top{};
        int height{};
        /// Where its text begins, below its top.
        int text_top{};
        QRect add_box;
        QRect book_box;
        bool is_result{};
        bool is_link{};
        /// Not in Anki, as far as is known: the headword is marked.
        bool unknown{};
        /// How many characters the headword has: what the mark covers.
        int head_length{};
    };

    void Clear();
    [[nodiscard]] QFont BaseFont() const;
    [[nodiscard]] int Scaled(double value) const;
    [[nodiscard]] QString Headline(const ReaderApp::LookupResult& result) const;
    [[nodiscard]] static QString HistoryLine(const ReaderApp::LookupHistory& history,
                                             bool with_where);
    [[nodiscard]] QString Senses(const ReaderApp::LookupResult& result) const;
    [[nodiscard]] int BoxCount(const Entry& entry) const;
    void Layout(int max_width, int max_height);
    /// Which box of which entry is at a point of the popup: 1 add, 2 book, 3 link.
    [[nodiscard]] int HitTest(const QPoint& point, int& index) const;
    [[nodiscard]] int MaxScroll() const;
    enum class Sign { Plus, Book, Ring, PlusAway };
    void DrawBox(QPainter& painter, const QRect& box, Sign sign, bool pressed_, bool enabled) const;
    /// Turns the rings while a box shows one.
    void UpdateSpin();

    std::vector<Entry> entries;
    int text_scale{100};
    bool allow_duplicates{};
    bool mark_unknown{true};
    Corner card_corner{Corner::BottomLeft};
    int content_width{};
    int content_height{};
    int scroll{};
    int last_max_width{};
    int last_max_height{};
    int pressed_index{-1};
    int pressed_kind{};
    QTimer spin;
    QElapsedTimer spun;

    // ---- Drawn by the popup page ----
    void OnPageMessage(const QJsonObject& message);
    /// Hands the page the popup's content, as the model popup.js draws.
    void ShowOnPage(const QJsonArray& entries, const QJsonObject& message);
    /// The console's colours as the page's CSS variables.
    [[nodiscard]] QString ConsoleCss() const;
    PopupWeb* web{};
    QString theme_name{QStringLiteral("classic")};
    QString user_css;
    bool web_failed{};
    bool web_layout{};
    int web_height{};
    int web_scrolled{};
    QString web_text;
    QHash<int, QRect> web_add;
    QHash<int, QRect> web_book;
};

} // namespace ReaderUi
