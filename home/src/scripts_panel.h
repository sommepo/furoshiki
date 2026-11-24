// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QWidget>
#include "dictionary_panel.h"
#include "script_shelf.h"

class QLineEdit;

namespace Home {

/**
 * Settings › Game scripts (the maintainer, 2026-11-23: "top level game scripts in settings, in
 * the same style as the new dictionaries page, which also shows status of names pulled from vndb
 * AND box art/spine art ... and the ability to pull if not"). Every console's games in one list,
 * grouped by console: each game's script (built in, installed, an update, not installed, the
 * user's own), its names dictionary, its cover and spine; marks and the same mass actions as the
 * Dictionaries page (DictionaryPanel), its look and keys.
 */
class ScriptsPanel : public QWidget {
public:
    ScriptsPanel(QWidget* parent, ScriptShelf* shelf);

    void SetStyle(const DictionaryPanel::Style& style);
    void SetActive(bool active);
    void Reload();
    void RefreshArt();
    bool Key(int key);
    [[nodiscard]] QList<QPair<QString, int>> KeyHints() const;
    /// One console only (its key), or every console (empty).
    void ChooseConsole(const QString& key);
    void Search(const QString& text);
    void ClickAt(const QPointF& sheet, const QString& modifiers);

    std::function<void(const QString& title, const QStringList& said, int now, std::function<void(int)> take)> ask;
    std::function<void(int sound)> sound;
    std::function<void()> entered;
    std::function<void(const QString&)> say;
    /// Whether a game has its cover and its spine (built in or fetched).
    std::function<std::pair<bool, bool>(const QString& id)> art;
    /// Box art looked for, for these games (one after the other).
    std::function<void(const QList<ScriptGame>& games)> fetch_art;
    /// Stop queued box art lookups.
    std::function<void()> cancel_art;
    /// Whether box art is being looked for, and how far (done, total).
    std::function<std::pair<int, int>()> art_work;
    /// A game's English name from its title record, which the search finds too.
    std::function<QString(const QString& id)> english;
    /// Scripts of one's own from files (a file chooser).
    std::function<void()> files_ask;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum State { AnyState, InLibrary, NotInLibrary, NoScript, NoNames, NoArt, Updates };
    struct Hit {
        QRectF area;
        std::function<void()> act;
    };

    void Filter();
    [[nodiscard]] std::pair<bool, bool> ArtOf(const QString& id) const;
    [[nodiscard]] QStringList Target() const;
    [[nodiscard]] QList<ScriptGame> TargetGames() const;
    [[nodiscard]] const ScriptGame* GameOf(const QString& id) const;
    [[nodiscard]] const ScriptGame* DiscOf(const QString& id) const;
    void SetCurrent(int shown_index, bool extend);
    void ToggleMark(const QString& id);
    void MarkAll(bool on);
    void Install(const QStringList& ids);
    void Names(const QStringList& ids);
    void Art(const QList<ScriptGame>& games);
    void AskRemove(const QStringList& ids);
    void Actions();
    void Play(int which) const;
    void Keep();
    [[nodiscard]] int Rows() const;
    [[nodiscard]] int RowAt(double y) const;
    [[nodiscard]] QPointF Sheet(const QPointF& at) const;
    void PlaceSearch();
    [[nodiscard]] double Width() const;
    [[nodiscard]] double Height() const;

    ScriptShelf* shelf;
    DictionaryPanel::Style style;
    QLineEdit* search;
    bool active{};
    QList<ScriptGame> all; ///< A row a game: a game of several discs once (its disc in the library, else its first)
    QHash<QString, ScriptGame> by_id; ///< Every individual disc's metadata (so multi-disc actions don't drop Disc 2+)
    QHash<QString, QStringList> discs; ///< A row's game ID -> every disc's ID
    mutable QHash<QString, std::pair<bool, bool>> art_cache;
    QList<int> shown;
    QSet<QString> marked;
    QString current;
    QString anchor;
    QString console; ///< The console filter's key, empty for all
    State state{AnyState};
    int first{};
    QList<Hit> hits;
    int hover_hit{-1};
    int hover_row{-1};
    bool problems_open{};
    bool asked_lists{};
};

} // namespace Home
