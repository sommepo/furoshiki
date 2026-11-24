// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QTimer>
#include <QWidget>
#include "dictionary_panel.h"
#include "dictionary_shelf.h"

namespace Home {

/**
 * The old dictionary file's dictionaries moved into the dictionary engine, said where it cannot
 * be missed (the maintainer, 2026-11-24: "it needs to be a prompt so they are aware it's
 * happening, with a progress bar"): a box over the whole window on the first start that has
 * dictionaries to move. It says what is happening and why, the dictionary being moved and how
 * many of all, with a bar for the whole move; "Hide" puts it away to the strip at the window's
 * foot (DictionaryConversionStrip, which brings it back), and the reading goes on meanwhile
 * (lookups use the old file until the move is done). At the end it says how it went, until OK:
 * all moved, or which did not come and what to do.
 */
class DictionaryConversionBox : public QWidget {
public:
    DictionaryConversionBox(QWidget* parent, DictionaryShelf* shelf);
    /// The page's look (its colours and faces); its size follows `scale`.
    void SetStyle(const DictionaryPanel::Style& style);
    /// Starts looking: shown as soon as dictionaries are being moved.
    void Watch();
    /// Shown again (the strip at the foot was clicked).
    void Reopen();
    /// A key while it is shown (the program's keys and a controller's): true when it was its own.
    bool Key(int key);
    /// Told when it is put away or comes back (the strip shows while it is away).
    std::function<void(bool shown)> shown_changed;
    /// Whether it may come up now (not over a game playing); it waits till then.
    std::function<bool()> may_show;
    /// Asked to import dictionaries (after the user gave the move up for that: Files or Folders).
    std::function<void()> import_ask;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void Place();
    void Close();
    [[nodiscard]] QRectF Panel() const;
    /// The box's two buttons in the window (0: left, 1: right), and in the sheet's units.
    [[nodiscard]] QRectF Button(int which) const;
    [[nodiscard]] static QRectF SheetButton(int which);
    /// The buttons' words now; an empty one is not there.
    [[nodiscard]] QString Label(int which) const;
    void Press(int which);

    /// What the box says: the move going on, the question whether to give it up for imports,
    /// that it was given up, or its result.
    enum class Mode { Running, Confirm, Removed, Finished };
    Mode mode{Mode::Running};
    int chosen{1}; ///< The button the keys are on
    int removed{};

    DictionaryShelf* shelf;
    DictionaryPanel::Style style;
    DictionaryConversion now;
    QTimer ticks;
    bool put_away{};  ///< Hidden by the user while the move goes on
    bool finished{};  ///< The move is over: the result is shown until OK
};

} // namespace Home
