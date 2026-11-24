// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include "dictionary_shelf.h"

class QLineEdit;

namespace Home {

/**
 * Settings › Dictionaries (the maintainer, 2026-11-23: built for a hundred dictionaries and more,
 * "a hybrid approach between something winui feeling and furoshiki feeling which allows for
 * powerful mass edits"). A dense list in the page's colours and type, over the page's place for
 * rows: a search that filters as it is typed, the kinds and on or off as filters, a row a
 * dictionary (marked or not, its place in the lookup order, title, kind, entries, pictures, on
 * or off), and what can be done to the marked ones at once. The mouse works it as a list on a
 * computer does (Ctrl and Shift with a click, dragging to change the order); the page hands it its
 * keys, so a keyboard and a controller work it too (Key).
 */
class DictionaryPanel : public QWidget {
public:
    /// The page's look: colours, and faces at the sheet's sizes (painted scaled).
    struct Style {
        QColor ground, raised, ink, muted, accent, rule;
        QFont row, mono, mono_small, head;
        double scale{1}; ///< The window's pixels for one of the sheet's
    };

    DictionaryPanel(QWidget* parent, DictionaryShelf* shelf);

    void SetStyle(const Style& style);
    /// Whether the rows have the page's keys (the sections at the left have them otherwise).
    void SetActive(bool active);
    /// The shelf's list changed: read again, the marks and the row in hand kept where they still are.
    void Reload();
    /// A key the page hands on (a keyboard's or a controller's); false where it is not this list's.
    bool Key(int key);
    /// The line of keys the page's foot shows while the rows have the keys.
    [[nodiscard]] QList<QPair<QString, int>> KeyHints() const;

    /// For page scripts: the search typed, a kind chosen by its name, a click at a place of the
    /// sheet (its modifiers "ctrl" or "shift").
    void Search(const QString& text);
    void ChooseKind(const QString& kind);
    void ClickAt(const QPointF& sheet, const QString& modifiers);
    void DragAt(const QPointF& from, const QPointF& to);

    /// The page's choices over the window (HomeWindow::OpenChoices).
    std::function<void(const QString& title, const QStringList& said, int now, std::function<void(int)> take)> ask;
    /// 0 a move, 1 a choice taken, 2 back, 3 refused.
    std::function<void(int sound)> sound;
    /// The import's chooser (files or a folder).
    std::function<void()> import_ask;
    /// A click on the list: it has the page's keys now.
    std::function<void()> entered;
    std::function<void(const QString&)> say;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Kind { Words, Grammar, Kanji, Frequency, Pitch, Names, KindCount };
    enum State { AnyState, OnOnly, OffOnly };
    struct Hit {
        QRectF area; ///< In the sheet's units, from the panel's corner
        std::function<void()> act;
    };

    [[nodiscard]] static Kind KindOf(const DictionaryEntry& entry);
    [[nodiscard]] static QString KindName(Kind kind);
    void Filter();
    [[nodiscard]] QList<qint64> Target() const;
    [[nodiscard]] QList<qint64> Order() const; ///< The others' order, names dictionaries left out
    [[nodiscard]] const DictionaryEntry* EntryOf(qint64 id) const;
    void SetCurrent(int shown_index, bool extend);
    void ToggleMark(qint64 id);
    void MarkAll(bool on);
    void Switch(const QList<qint64>& ids, bool on);
    void Move(const QList<qint64>& ids, int how, int steps = 1); ///< -1 up, 1 down, -2 to the top, 2 to the bottom
    void DropAt(const QList<qint64>& ids, int gap); ///< Before the shown row `gap` (its count: at the end)
    void AskDelete(const QList<qint64>& ids);
    void Actions();
    void Play(int which) const;
    void Keep(); ///< The row in hand inside the rows shown
    [[nodiscard]] int Rows() const;    ///< How many rows the list shows at once
    /// The import box over the list (an import running, or its result not yet put away), and
    /// where the column heads and the rows stand under it.
    [[nodiscard]] double Banner() const;
    [[nodiscard]] double HeadTop() const;
    [[nodiscard]] double ListTop() const;
    [[nodiscard]] int RowAt(double y) const;
    [[nodiscard]] QPointF Sheet(const QPointF& at) const;
    void PlaceSearch();
    [[nodiscard]] double Width() const;
    [[nodiscard]] double Height() const;

    DictionaryShelf* shelf;
    Style style;
    QLineEdit* search;
    bool active{};
    QList<DictionaryEntry> all;
    QHash<qint64, Kind> kinds;
    QHash<qint64, int> places; ///< Place in the lookup order, from 1 (names dictionaries none)
    QList<int> shown;          ///< Indexes into `all`
    QSet<qint64> marked;
    qint64 current{-1};
    qint64 anchor{-1}; ///< Where a Shift range starts
    int kind{-1};      ///< A Kind, or -1: all but the names dictionaries
    State state{AnyState};
    int first{};       ///< The first row shown
    QList<Hit> hits;
    int hover_hit{-1};
    int hover_row{-1};
    // A drag of rows to another place in the order.
    QPointF press_at;
    qint64 press_id{-1};
    bool dragging{};
    int drop_gap{-1};
    bool drag_said{};
    QTimer converting_check;
    bool problems_open{}; ///< The last import's problems, in full, over the list
    // Each dictionary shown as it comes in (the maintainer's recording, 2026-11-23): what was there
    // when the import began, and what came since, marked NEW until the result is put away.
    bool importing{};
    QSet<qint64> before_import;
    QSet<qint64> arrived;
    // Changing the order (the maintainer, 2026-11-23: "a brilliant way of reordering dictionaries
    // on all platforms"): a move mode for keys and a controller, dragging (with a filter too), a
    // sort by kind, and undo.
    void Reordered(const QList<qint64>& order); ///< Writes the order, the one before kept for undo
    void BeginMove();
    void EndMove(bool keep);
    bool MoveKey(int key);
    void SortByKind();
    void Undo();
    bool moving{};
    QList<qint64> moving_ids;  ///< The rows being moved
    QList<qint64> moving_from; ///< The order when the move began, put back by Esc
    QList<qint64> undo_order;  ///< The order before the last change; empty: nothing to undo
    bool undone{};             ///< The last change was undone: the link puts it back
};

/**
 * The old dictionary file's dictionaries moved into the dictionary engine, as a strip pinned at
 * the foot of the program's window while it lasts (the maintainer, 2026-11-23: "a converting
 * dictionaries progress bar pinned at the bottom while this happens"): how many of all, the one
 * being moved, a rule that fills. It looks every half second and goes when the move is done.
 */
class DictionaryConversionStrip : public QWidget {
public:
    DictionaryConversionStrip(QWidget* parent, DictionaryShelf* shelf);
    /// The page's look (its colours, and faces at the sheet's sizes); its height follows `scale`.
    void SetStyle(const DictionaryPanel::Style& style);
    /// Starts looking, and shows itself while dictionaries are being moved.
    void Watch();
    /// Hidden while the modal conversion box is up so the strip never raises over it.
    void SetSuppressed(bool suppressed);
    /// Its place at the foot of its parent.
    void Place();
    /// A click on it (the box that says the move brought back).
    std::function<void()> clicked;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    DictionaryShelf* shelf;
    DictionaryPanel::Style style;
    DictionaryConversion now;
    QTimer ticks;
    bool suppressed{};
};

} // namespace Home
