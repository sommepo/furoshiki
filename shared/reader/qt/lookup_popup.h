// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QImage>
#include <QSet>

#include <functional>
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
#include <QVariantAnimation>
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
    /// From the setting's value ("top_right" and so on); the top right otherwise.
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
    /// The look's own settings as the page takes them (ReaderSettings::PopupLook): how solid,
    /// how round, how many senses, the tags, a name's picture. The classic popup has none of it.
    void SetLook(const QJsonObject& look_);
    /**
     * Whether the popup wants a blurred picture of what is behind it drawn under its page: a
     * page popup that is see-through (the look's opacity under 100, or a glass theme), where
     * the system does not blur behind a window itself (not on Windows: UpdateBackdropBlur).
     */
    [[nodiscard]] bool WantsBackdrop() const;
    /// That picture, drawn over the whole popup inside its corners; none takes it away.
    void SetBackdrop(const QImage& picture);
    /// How round the popup's corners are, in its own pixels: the look's, else its theme's.
    [[nodiscard]] int CornerRadius() const;
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
    /// As tall as the entry in hand (the page's "fit") instead of all its content.
    void SetFitEntry(bool on) {
        fit_entry = on;
    }
    /// The least height that still shows where the definition begins (the page's "need"; 0 unknown).
    [[nodiscard]] bool IsPage() const {
        return web != nullptr;
    }
    [[nodiscard]] int Need() const {
        return web ? web_need : 0;
    }
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

    // ---- A controller (Furoshiki's on a computer, as PopupKeys on Android) ----

    /// A controller is the input: every popup's page marks the buttons that work it (SetPadKeys)
    /// and the entry the d-pad is on. Set by the program as a controller's button is pressed.
    static void SetPadInUse(bool on);
    [[nodiscard]] static bool PadInUse();
    /// How the controller names those buttons, for the marks: {"add": "Y", "addAudio": "B",
    /// "left": "L", "right": "R"} (the page's own △ and ○ where one is not given).
    static void SetPadKeys(const QJsonObject& keys);
    /// What the program does when the page says something happened that may be heard or felt
    /// ({"type":"feedback"}: "land", a card's book landing; "step", the d-pad on another entry).
    /// None set (the forks' own apps): nothing.
    static void SetFeedback(std::function<void(const QString& kind)> feedback);
    /// The tab before (-1) or the next (1): words, grammar, kanji, names.
    void PadTab(int step);
    /// The kanji before (-1) or the next (1) on the Kanji tab.
    void PadKanji(int step);
    void PadToggleMore();
    /// How many tabs the popup shows (words, grammar, kanji, names): 0 or 1 is no row of tabs.
    [[nodiscard]] int Tabs() const {
        return web ? web_tabs : 0;
    }
    /// The controller's marks as they now are, on the page (after SetPadKeys changed them).
    void RefreshPad() {
        ShowPad();
    }
    /// The entry before (-1) or the next (1), brought into view.
    void PadEntry(int step);
    /// The chosen entry's card, with the line's audio or without, as its own button makes it.
    void PadAdd(bool audio);
    /// Tells the page a controller is the input now, with its letters (once it has changed).
    void ShowPad();
    /// Scrolls by `dy` pixels (the left stick).
    void ScrollBy(int dy);

    /// The largest width the popup takes at the current text scale.
    [[nodiscard]] int MaxWidth() const;

signals:
    /// A card of the `index`th word: `audio` 1 with the line's audio, 0 without, -1 as the settings say.
    void AddRequested(int index, int audio);
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
    void showEvent(QShowEvent* event) override;

private:
    void UpdateBackdropBlur();
    /// See-through: the look's opacity under 100, or a theme that is glass by itself.
    [[nodiscard]] bool Translucent() const;
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
        /// For a web popup: the text, made only when asked for (PlainText), as the page draws it.
        std::function<QString()> html;
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
    QJsonObject look;
    /// What is behind the popup, blurred (SetBackdrop).
    QImage backdrop;
    /// Until when the backdrop is not drawn: the page takes a moment to draw itself at a new
    /// size, and the blur alone, with no page over it, would flash as an empty box.
    qint64 backdrop_held_until{};
    void HoldBackdrop();
    Corner card_corner{Corner::TopRight};
    int content_width{};
    int content_height{};
    int scroll{};
    int last_max_width{};
    int last_max_height{};
    int pressed_index{-1};
    /// The entry the controller is on (PadEntry) in the popup drawn here.
    int pad_chosen{};
    /// What the page was last told of the controller (Reader.setPad).
    bool pad_shown{};
    /// Which set of the controller's marks the page has (SetPadKeys counts them).
    int pad_keys_shown{};
    int pressed_kind{};
    QTimer spin;
    QElapsedTimer spun;

    // ---- Drawn by the popup page ----
    void OnPageMessage(const QJsonObject& message);
    /// Hands the page the popup's content, as the model popup.js draws.
    void ShowOnPage(const QJsonArray& entries, const QJsonObject& message);
    /// The same with the entries as JSON text already, and the pictures they name not sent yet.
    void ShowOnPage(const QByteArray& entries_json, const QJsonObject& pictures, const QJsonObject& message = {});
    /// The console's colours as the page's CSS variables.
    [[nodiscard]] QString ConsoleCss() const;
    PopupWeb* web{};
    QString theme_name{QStringLiteral("classic")};
    QString user_css;
    bool web_failed{};
    bool web_layout{};
    int web_height{};
    /// From the page's "size": the height that shows the entry in hand whole, and the least that
    /// still shows where its definition begins.
    int web_fit{};
    /// A step to another entry (the d-pad, the height following the scroll) eases web_fit to it.
    QVariantAnimation* fit_ease{};
    int fit_target{};
    int web_need{};
    bool fit_entry{};
    int web_scrolled{};
    int web_tabs{};
    QString web_text;
    QHash<int, QRect> web_add;
    QHash<int, QRect> web_book;
    /// What the page has been given since it last loaded: pictures (SharePictures' keys) and
    /// the dictionaries' stylesheets, which are not sent again.
    QSet<QString> web_pictures;
    QHash<QString, QString> web_styles;
    bool web_styles_sent{};
    bool web_forget_pictures{};
};

} // namespace ReaderUi
