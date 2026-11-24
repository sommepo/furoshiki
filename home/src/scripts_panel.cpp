// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTimer>
#include <QWheelEvent>
#include "scripts_panel.h"

namespace Home {
namespace {

// In the sheet's units, as the Dictionaries page's (dictionary_panel.cpp).
constexpr double ToolbarTall = 30;
constexpr double ChipsTop = 40;   ///< The consoles, a row of their own
constexpr double FiltersTop = 70; ///< What the games want or have
constexpr double HeadTop = 98;
constexpr double ListTop = 116;
constexpr double RowTall = 25;
constexpr double DetailTall = 44; ///< Full title and description of the selected game
constexpr double FootTall = 54 + DetailTall;
constexpr double Bar = 10;
constexpr double CheckWide = 26;
constexpr double TitleLeft = 34;

QColor Faded(QColor colour, double alpha) {
    colour.setAlphaF(colour.alphaF() * alpha);
    return colour;
}

double Put(QPainter& p, const QFont& font, double x, double y, const QString& text, const QColor& colour, int align = 0) {
    p.setFont(font);
    p.setPen(colour);
    const double wide = QFontMetricsF(font).horizontalAdvance(text);
    p.drawText(QPointF(align == 1 ? x - wide : align == 2 ? x - wide / 2 : x, y), text);
    return wide;
}

QString Fit(const QString& text, const QFont& font, double room) {
    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, room);
}

using Script = ScriptGame::Script;
using Names = ScriptGame::Names;

QString ScriptSaid(Script script) {
    switch (script) {
    case Script::None: return QStringLiteral("NO SCRIPT");
    case Script::NotInstalled: return QStringLiteral("NOT INSTALLED");
    case Script::Installed: return QStringLiteral("INSTALLED");
    case Script::BuiltIn: return QStringLiteral("BUILT-IN");
    case Script::Update: return QStringLiteral("UPDATE");
    case Script::Own: return QStringLiteral("CUSTOM");
    case Script::TooNew: return QStringLiteral("UPDATE APP");
    }
    return {};
}

/// A console as the list's narrow column and its strip of consoles say it.
QString ShortConsole(const QString& key, const QString& name) {
    static const QHash<QString, QString> short_names{
        {QStringLiteral("dc"), QStringLiteral("DC")}, {QStringLiteral("psvita"), QStringLiteral("VITA")}, {QStringLiteral("switch"), QStringLiteral("SWITCH")}};
    return short_names.value(key, name.isEmpty() ? (key.isEmpty() ? QStringLiteral("OTHER") : key.toUpper()) : name.toUpper());
}

QString NamesSaid(Names names) {
    switch (names) {
    case Names::None: return QStringLiteral("NONE");
    case Names::Available: return QStringLiteral("AVAILABLE");
    case Names::Ready: return QStringLiteral("READY");
    case Names::Installed: return QStringLiteral("INSTALLED");
    }
    return {};
}

bool Installable(const ScriptGame& game) {
    return game.script == Script::NotInstalled || game.script == Script::Update || (game.in_list && game.script == Script::Own);
}

bool NamesWanted(const ScriptGame& game) {
    return game.names == Names::Available || game.names == Names::Ready;
}

} // namespace

ScriptsPanel::ScriptsPanel(QWidget* parent, ScriptShelf* shelf_) : QWidget(parent), shelf(shelf_) {
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    search = new QLineEdit(this);
    search->setPlaceholderText(QStringLiteral("Search games"));
    search->setClearButtonEnabled(true);
    search->setFrame(false);
    search->installEventFilter(this);
    connect(search, &QLineEdit::textChanged, this, [this] {
        first = 0;
        Filter();
        update();
    });
}

void ScriptsPanel::SetStyle(const DictionaryPanel::Style& style_) {
    style = style_;
    QFont font = style.row;
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(style.row.pixelSize() * style.scale))));
    search->setFont(font);
    search->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; padding: 0 %4px; "
                                         "selection-background-color: %5; selection-color: %6; }")
                              .arg(style.raised.alpha() > 0 ? style.raised.name(QColor::HexArgb) : style.ground.name(QColor::HexArgb),
                                   style.ink.name(), style.rule.name(), QString::number(std::lround(8 * style.scale)),
                                   style.accent.name(), style.ground.name()));
    QPalette palette = search->palette();
    palette.setColor(QPalette::PlaceholderText, style.muted);
    search->setPalette(palette);
    PlaceSearch();
    update();
}

void ScriptsPanel::SetActive(bool on) {
    if (active != on) {
        active = on;
        update();
    }
}

double ScriptsPanel::Width() const {
    return width() / std::max(0.01, style.scale);
}

double ScriptsPanel::Height() const {
    return height() / std::max(0.01, style.scale);
}

void ScriptsPanel::resizeEvent(QResizeEvent*) {
    PlaceSearch();
}

void ScriptsPanel::PlaceSearch() {
    search->setGeometry(QRectF(0, 0, 320 * style.scale, ToolbarTall * style.scale).toRect());
}

void ScriptsPanel::Reload() {
    if (!asked_lists && !shelf->ListsFetched()) {
        asked_lists = true; // the lists, once, the first time the page is shown
        shelf->FetchLists();
    }
    // A game of several discs is one row (the maintainer, 2026-11-23: "the multidisc games
    // shouldnt show up once per disc"): its discs are the same title on the same console once a
    // "(Disc 2)" or "ディスク2" at its end is set aside; the row stands for every disc's ID.
    static const QRegularExpression disc_end(QStringLiteral("\\s*[(（\\[]?\\s*(?:disc|disk|ディスク)\\s*\\d+\\s*[)）\\]]?\\s*$"),
                                             QRegularExpression::CaseInsensitiveOption);
    const auto rank = [](Script script) {
        // Which of its discs' states the row says: what wants doing first.
        switch (script) {
        case Script::Update: return 0;
        case Script::NotInstalled: return 1;
        case Script::Own: return 2;
        case Script::Installed: return 3;
        case Script::BuiltIn: return 4;
        case Script::TooNew: return 5;
        case Script::None: return 6;
        }
        return 6;
    };
    const auto names_rank = [](ScriptGame::Names names) {
        return names == ScriptGame::Names::Available ? 0 : names == ScriptGame::Names::Ready ? 1 : names == ScriptGame::Names::Installed ? 2 : 3;
    };
    all.clear();
    by_id.clear();
    discs.clear();
    art_cache.clear();
    QHash<QString, int> row_of;
    for (ScriptGame game : shelf->Games()) {
        by_id.insert(game.id, game);
        const QString title = QString(game.title).remove(disc_end).trimmed();
        const QString key = game.console + QLatin1Char('|') + title.toCaseFolded();
        const auto found = row_of.constFind(key);
        if (title.isEmpty() || found == row_of.constEnd()) {
            row_of.insert(key, static_cast<int>(all.size()));
            discs.insert(game.id, {game.id});
            if (!title.isEmpty()) {
                game.title = title;
            }
            all.append(game);
            continue;
        }
        ScriptGame& row = all[*found];
        QStringList ids = discs.take(row.id);
        ids.append(game.id);
        if (game.in_library && !row.in_library) {
            row.id = game.id; // the disc on the shelf stands for the game: its box art is that one's
        }
        row.in_library = row.in_library || game.in_library;
        row.in_list = row.in_list || game.in_list;
        if (rank(game.script) < rank(row.script)) {
            row.script = game.script;
        }
        if (names_rank(game.names) < names_rank(row.names)) {
            row.names = game.names;
        }
        discs.insert(row.id, ids);
    }
    // A row not on the shelf stands for its discs by the first (in ID order) that has box art: the
    // discs of one game share it, but only some carry it.
    for (ScriptGame& row : all) {
        QStringList ids = discs.take(row.id);
        ids.sort();
        if (!row.in_library && ids.size() > 1 && art) {
            for (const QString& id : ids) {
                if (ArtOf(id).first) {
                    row.id = id;
                    break;
                }
            }
        }
        discs.insert(row.id, ids);
    }
    QSet<QString> present;
    for (const ScriptGame& game : all) {
        present.insert(game.id);
    }
    marked.intersect(present);
    if (!present.contains(current)) {
        current.clear();
    }
    Filter();
    update();
}

void ScriptsPanel::RefreshArt() {
    art_cache.clear();
    if (state == NoArt) {
        Filter();
    }
    update();
}

std::pair<bool, bool> ScriptsPanel::ArtOf(const QString& id) const {
    if (!art) {
        return {false, false};
    }
    auto found = art_cache.constFind(id);
    if (found == art_cache.constEnd()) {
        found = art_cache.insert(id, art(id));
    }
    return *found;
}

void ScriptsPanel::Filter() {
    shown.clear();
    const QString wanted = search->text().trimmed();
    for (int i = 0; i < all.size(); ++i) {
        const ScriptGame& game = all[i];
        if (!console.isEmpty() && game.console != console) {
            continue;
        }
        const auto [cover, spine] = art ? ArtOf(game.id) : std::pair<bool, bool>{true, true};
        const bool keep = state == AnyState || (state == InLibrary && game.in_library) || (state == NotInLibrary && !game.in_library) ||
                          (state == NoScript && (game.script == Script::None || game.script == Script::NotInstalled || game.script == Script::TooNew)) ||
                          (state == NoNames && NamesWanted(game)) || (state == NoArt && (!cover || !spine)) ||
                          (state == Updates && game.script == Script::Update);
        if (!keep) {
            continue;
        }
        if (!wanted.isEmpty()) {
            const QString said = game.title + QLatin1Char(' ') + game.id + QLatin1Char(' ') + game.reads + QLatin1Char(' ') + ScriptSaid(game.script) +
                                 QLatin1Char(' ') + (english ? english(game.id) : QString());
            bool every = true;
            for (const QString& word : wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                every = every && said.contains(word, Qt::CaseInsensitive);
            }
            if (!every) {
                continue;
            }
        }
        shown.append(i);
    }
    bool still = false;
    for (int i : shown) {
        still = still || all[i].id == current;
    }
    if (!still) {
        current = shown.isEmpty() ? QString() : all[shown.first()].id;
    }
    first = std::clamp(first, 0, std::max(0, static_cast<int>(shown.size()) - Rows()));
}

const ScriptGame* ScriptsPanel::GameOf(const QString& id) const {
    for (const ScriptGame& game : all) {
        if (game.id == id) {
            return &game;
        }
    }
    return nullptr;
}

const ScriptGame* ScriptsPanel::DiscOf(const QString& id) const {
    const auto found = by_id.constFind(id);
    return found == by_id.constEnd() ? GameOf(id) : &*found;
}

QStringList ScriptsPanel::Target() const {
    // Every disc of each row marked (else of the row in hand).
    QStringList out;
    for (const ScriptGame& game : all) {
        if (marked.contains(game.id)) {
            out += discs.value(game.id, {game.id});
        }
    }
    if (out.isEmpty() && !current.isEmpty()) {
        out += discs.value(current, {current});
    }
    return out;
}

QList<ScriptGame> ScriptsPanel::TargetGames() const {
    // A row each: box art is a game's, looked for once for all its discs.
    QList<ScriptGame> out;
    for (const ScriptGame& game : all) {
        if (marked.contains(game.id) || (marked.isEmpty() && game.id == current)) {
            out.append(game);
        }
    }
    return out;
}

int ScriptsPanel::Rows() const {
    return std::max(1, static_cast<int>((Height() - FootTall - ListTop) / RowTall));
}

int ScriptsPanel::RowAt(double y) const {
    if (y < ListTop) {
        return -1;
    }
    const int row = first + static_cast<int>((y - ListTop) / RowTall);
    return row < shown.size() && row < first + Rows() ? row : -1;
}

QPointF ScriptsPanel::Sheet(const QPointF& at) const {
    return at / std::max(0.01, style.scale);
}

void ScriptsPanel::Play(int which) const {
    if (sound) {
        sound(which);
    }
}

void ScriptsPanel::Keep() {
    int at = -1;
    for (int i = 0; i < shown.size(); ++i) {
        at = all[shown[i]].id == current ? i : at;
    }
    if (at >= 0) {
        first = std::clamp(first, at - Rows() + 1, at);
    }
    first = std::clamp(first, 0, std::max(0, static_cast<int>(shown.size()) - Rows()));
}

void ScriptsPanel::SetCurrent(int index, bool extend) {
    if (shown.isEmpty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(shown.size()) - 1);
    const QString id = all[shown[index]].id;
    if (extend) {
        int from = index;
        for (int i = 0; i < shown.size(); ++i) {
            from = all[shown[i]].id == (anchor.isEmpty() ? current : anchor) ? i : from;
        }
        for (int i = std::min(from, index); i <= std::max(from, index); ++i) {
            marked.insert(all[shown[i]].id);
        }
    } else {
        anchor = id;
    }
    current = id;
    Keep();
    update();
}

void ScriptsPanel::ToggleMark(const QString& id) {
    if (!marked.remove(id)) {
        marked.insert(id);
    }
    anchor = id;
    update();
}

void ScriptsPanel::MarkAll(bool on) {
    if (on) {
        for (int i : shown) {
            marked.insert(all[i].id);
        }
    } else {
        marked.clear();
    }
    update();
}

void ScriptsPanel::Install(const QStringList& ids) {
    QStringList wanted;
    for (const QString& id : ids) {
        if (const ScriptGame* game = DiscOf(id); game != nullptr && game->in_list && game->script != Script::TooNew) {
            wanted.append(id);
        }
    }
    if (wanted.isEmpty()) {
        Play(3);
        if (say) {
            say(QStringLiteral("No downloadable script is available for the selected games"));
        }
        return;
    }
    Play(1);
    shelf->Install(wanted);
}

void ScriptsPanel::Names(const QStringList& ids) {
    QStringList wanted;
    for (const QString& id : ids) {
        if (const ScriptGame* game = DiscOf(id); game != nullptr && NamesWanted(*game)) {
            wanted.append(id);
        }
    }
    if (wanted.isEmpty()) {
        Play(3);
        if (say) {
            say(QStringLiteral("No names dictionaries to install (already installed or unavailable)"));
        }
        return;
    }
    Play(1);
    shelf->InstallNames(wanted);
}

void ScriptsPanel::Art(const QList<ScriptGame>& games) {
    QList<ScriptGame> wanted;
    for (const ScriptGame& game : games) {
        if (art) {
            const auto [cover, spine] = ArtOf(game.id);
            if (cover && spine) {
                continue;
            }
        }
        wanted.append(game);
    }
    if (wanted.isEmpty() || !fetch_art) {
        Play(3);
        if (say) {
            say(games.size() == 1 ? QStringLiteral("This game already has box art")
                                  : QStringLiteral("Selected games already have box art"));
        }
        return;
    }
    Play(1);
    fetch_art(wanted);
}

void ScriptsPanel::AskRemove(const QStringList& ids) {
    QStringList own;
    for (const QString& id : ids) {
        if (const ScriptGame* game = DiscOf(id); game != nullptr &&
            (game->script == Script::Installed || game->script == Script::Own || game->script == Script::Update)) {
            own.append(id);
        }
    }
    if (own.isEmpty() || !ask) {
        Play(3);
        if (say) {
            say(QStringLiteral("Only downloaded or custom scripts can be removed"));
        }
        return;
    }
    const ScriptGame* one = DiscOf(own.first());
    ask(own.size() == 1 && one != nullptr ? QStringLiteral("REMOVE SCRIPT FOR %1?").arg(one->title.toUpper())
                                          : QStringLiteral("REMOVE %1 SCRIPTS?").arg(own.size()),
        {QStringLiteral("Cancel"), QStringLiteral("Remove")}, 0, [this, own](int index) {
            if (index == 1) {
                shelf->Remove(own);
            }
        });
}

void ScriptsPanel::Actions() {
    const QStringList target = Target();
    if (target.isEmpty() || !ask) {
        Play(3);
        return;
    }
    QStringList said;
    QList<std::function<void()>> does;
    const auto add = [&](const QString& name, std::function<void()> act) {
        said.append(name);
        does.append(std::move(act));
    };
    if (!current.isEmpty()) {
        add(marked.contains(current) ? QStringLiteral("Unmark") : QStringLiteral("Mark"), [this] { ToggleMark(current); });
    }
    add(QStringLiteral("Install or update script"), [this, target] { Install(target); });
    add(QStringLiteral("Install names"), [this, target] { Names(target); });
    add(QStringLiteral("Find box art"), [this] { Art(TargetGames()); });
    add(QStringLiteral("Remove script…"), [this, target] { QTimer::singleShot(0, this, [this, target] { AskRemove(target); }); });
    add(QStringLiteral("Mark all shown"), [this] { MarkAll(true); });
    if (!marked.isEmpty()) {
        add(QStringLiteral("Unmark all"), [this] { MarkAll(false); });
    }
    const ScriptGame* here = GameOf(current);
    ask(!marked.isEmpty() ? QStringLiteral("%1 MARKED").arg(marked.size()) : here != nullptr ? here->title.toUpper() : QString(), said, -1,
        [does](int index) {
            if (index >= 0 && index < does.size()) {
                does[index]();
            }
        });
}

bool ScriptsPanel::Key(int key) {
    const Qt::KeyboardModifiers held = QGuiApplication::keyboardModifiers();
    const bool shift = held & Qt::ShiftModifier, ctrl = held & Qt::ControlModifier, alt = held & Qt::AltModifier;
    int at = -1;
    for (int i = 0; i < shown.size(); ++i) {
        at = all[shown[i]].id == current ? i : at;
    }
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
        if ((key == Qt::Key_Up && at <= 0) || (key == Qt::Key_Down && at >= shown.size() - 1)) {
            return true;
        }
        SetCurrent(at + (key == Qt::Key_Up ? -1 : 1), shift);
        Play(0);
        return true;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if ((ctrl || alt || shift) && !shown.isEmpty()) {
            SetCurrent(at + (key == Qt::Key_PageUp ? -Rows() : Rows()), shift);
            Play(0);
            return true;
        }
        return false;
    case Qt::Key_Home:
    case Qt::Key_End:
        if (!shown.isEmpty()) {
            SetCurrent(key == Qt::Key_Home ? 0 : static_cast<int>(shown.size()) - 1, shift);
            Play(0);
        }
        return true;
    case Qt::Key_Space:
        if (!current.isEmpty()) {
            ToggleMark(current);
            Play(1);
        }
        return true;
    case Qt::Key_A:
        if (ctrl) {
            MarkAll(true);
            Play(1);
        }
        return true;
    case Qt::Key_Right:
        Install(Target());
        return true;
    case Qt::Key_Left:
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        Actions();
        return true;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        AskRemove(Target());
        return true;
    case Qt::Key_Slash:
    case Qt::Key_F:
        search->setFocus();
        search->selectAll();
        Play(1);
        return true;
    case Qt::Key_Escape:
        if (problems_open) {
            problems_open = false;
            update();
            Play(2);
            return true;
        }
        if (!shelf->Work().busy && !shelf->LastAction().isEmpty()) {
            shelf->ClearProblems();
            Play(2);
            return true;
        }
        if (!marked.isEmpty()) {
            MarkAll(false);
            Play(2);
            return true;
        }
        return false;
    default:
        return false;
    }
}

QList<QPair<QString, int>> ScriptsPanel::KeyHints() const {
    return {{QStringLiteral("↑"), Qt::Key_Up},
            {QStringLiteral("↓ ROW"), Qt::Key_Down},
            {QStringLiteral("SPACE MARK"), Qt::Key_Space},
            {QStringLiteral("→ INSTALL"), Qt::Key_Right},
            {QStringLiteral("ENTER ACTIONS"), Qt::Key_Return},
            {QStringLiteral("DEL REMOVE"), Qt::Key_Delete},
            {QStringLiteral("/ SEARCH"), Qt::Key_Slash},
            {marked.isEmpty() ? QStringLiteral("ESC SECTIONS") : QStringLiteral("ESC UNMARK"), Qt::Key_Escape}};
}

void ScriptsPanel::ChooseConsole(const QString& key) {
    console = key;
    first = 0;
    Filter();
    update();
}

void ScriptsPanel::Search(const QString& text) {
    search->setText(text);
}

void ScriptsPanel::ClickAt(const QPointF& sheet, const QString& modifiers) {
    const Qt::KeyboardModifiers held = modifiers == QStringLiteral("ctrl")    ? Qt::ControlModifier
                                       : modifiers == QStringLiteral("shift") ? Qt::ShiftModifier
                                                                               : Qt::NoModifier;
    const QPointF at = sheet * style.scale;
    QMouseEvent press(QEvent::MouseButtonPress, at, mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, held);
    mousePressEvent(&press);
}

bool ScriptsPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            if (!search->text().isEmpty()) {
                search->clear();
            } else {
                parentWidget()->setFocus();
            }
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Down || key->key() == Qt::Key_Tab) {
            parentWidget()->setFocus();
            if (entered) {
                entered();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ScriptsPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(style.scale, style.scale);
    const double W = Width(), H = Height();
    const double right = W - Bar;
    hits.clear();
    const auto button = [&](double x_right, double y, const QString& text, bool strong, bool enabled, std::function<void()> act) {
        const double wide = QFontMetricsF(style.mono).horizontalAdvance(text) + 20;
        const QRectF box(x_right - wide, y, wide, 24);
        const bool over = enabled && hover_hit == hits.size();
        if (strong && enabled) {
            p.fillRect(box, over ? style.ink : style.accent);
        } else {
            p.setPen(QPen(enabled ? (over ? style.ink : style.rule) : Faded(style.rule, 0.5), 1));
            p.setBrush(over ? QBrush(Faded(style.ink, 0.08)) : QBrush(Qt::NoBrush));
            p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        Put(p, style.mono, box.center().x(), y + 16, text, strong && enabled ? style.ground : enabled ? style.ink : Faded(style.muted, 0.6), 2);
        hits.append({box, enabled ? std::move(act) : std::function<void()>()});
        return box.left();
    };
    const auto chip = [&](double x, double y, const QString& text, qint64 n, bool on, std::function<void()> act) {
        const QString said = n >= 0 ? QStringLiteral("%1 %2").arg(text).arg(n) : text;
        const double wide = QFontMetricsF(style.mono_small).horizontalAdvance(said);
        const bool over = hover_hit == hits.size();
        Put(p, style.mono_small, x, y, said, on || over ? style.ink : style.muted);
        if (on) {
            p.fillRect(QRectF(x, y + 5, wide, 2), style.accent);
        }
        hits.append({QRectF(x - 6, y - 14, wide + 12, 24), std::move(act)});
        return wide + 16;
    };

    const ScriptWork work = shelf->Work();
    const std::pair<int, int> art_now = art_work ? art_work() : std::pair<int, int>{0, 0};
    int updates = 0, installed = 0;
    QHash<QString, int> per_console;
    for (const ScriptGame& game : all) {
        updates += game.script == Script::Update ? 1 : 0;
        installed += game.script == Script::Installed || game.script == Script::BuiltIn || game.script == Script::Own ? 1 : 0;
        ++per_console[game.console];
    }
    // The toolbar: the search, then every update at once, scripts from files, the lists again.
    double x = right;
    x = button(x, 3, QStringLiteral("REFRESH"), false, !work.busy, [this] {
            Play(1);
            shelf->FetchLists();
        }) - 6;
    x = button(x, 3, QStringLiteral("FROM FILE…"), false, !work.busy, [this] {
            Play(1);
            if (files_ask) {
                files_ask();
            }
        }) - 6;
    button(x, 3, updates > 0 ? QStringLiteral("UPDATE ALL %1").arg(updates) : QStringLiteral("UPDATE ALL"), updates > 0, updates > 0 && !work.busy,
           [this] {
               QStringList ids;
               for (const ScriptGame& row : all) {
                   for (const QString& disc_id : discs.value(row.id, {row.id})) {
                       if (const ScriptGame* disc = DiscOf(disc_id); disc != nullptr && disc->script == Script::Update) {
                           ids.append(disc_id);
                       }
                   }
               }
               if (!ids.isEmpty()) {
                   Play(1);
                   shelf->Install(ids);
               }
           });

    // The consoles, a row of their own as one strip of boxes (the maintainer, 2026-11-23: across
    // the top they took too much room and were not told apart from the other filters).
    {
        double cx = 0;
        const auto segment = [&](const QString& text, int n, bool on, std::function<void()> act) {
            const QString said = QStringLiteral("%1 %2").arg(text).arg(n);
            const double wide = QFontMetricsF(style.mono_small).horizontalAdvance(said) + 16;
            const QRectF box(cx, ChipsTop, wide, 22);
            const bool over = hover_hit == hits.size();
            if (on) {
                p.fillRect(box, style.ink);
            } else if (over) {
                p.fillRect(box, Faded(style.ink, 0.08));
            }
            p.setPen(QPen(style.rule, 1));
            p.setBrush(QBrush(Qt::NoBrush));
            p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
            Put(p, style.mono_small, box.center().x(), ChipsTop + 15, said, on ? style.ground : over ? style.ink : style.muted, 2);
            hits.append({box, std::move(act)});
            cx += wide - 1;
        };
        segment(QStringLiteral("ALL"), static_cast<int>(all.size()), console.isEmpty(), [this] { ChooseConsole({}); Play(0); });
        for (const auto& [key, name] : shelf->Consoles()) {
            if (per_console.value(key) > 0 || console == key) {
                segment(ShortConsole(key, name), per_console.value(key), console == key, [this, key] { ChooseConsole(key); Play(0); });
            }
        }
    }
    // What the games want or have: one at a time.
    {
        const auto set_state = [this](State to) {
            return [this, to] {
                state = to;
                first = 0;
                Filter();
                Play(0);
                update();
            };
        };
        double sx = 0;
        const std::pair<State, const char*> filters[] = {
            {AnyState, "ALL"}, {InLibrary, "IN LIBRARY"}, {NotInLibrary, "NOT IN LIBRARY"}, {NoScript, "SCRIPT MISSING"},
            {NoNames, "NAMES AVAILABLE"}, {NoArt, "BOX ART MISSING"}, {Updates, "UPDATES"},
        };
        for (const auto& [which, name] : filters) {
            sx += chip(sx, FiltersTop + 12, QString::fromLatin1(name), -1, state == which, set_state(which));
        }
    }

    // The heads of the columns.
    const double spine_right = right, cover_right = right - 50, library_right = right - 100, names_left = right - 214,
                 script_left = right - 330, console_left = right - 388, id_right = console_left - 12;
    const bool every = !shown.isEmpty() && std::all_of(shown.begin(), shown.end(), [this](int i) { return marked.contains(all[i].id); });
    {
        const QRectF box(7, HeadTop + 3, 11, 11);
        p.setPen(QPen(every ? style.accent : style.muted, 1.2));
        p.setBrush(every ? QBrush(style.accent) : QBrush(Qt::NoBrush));
        p.drawRect(box);
        hits.append({box.adjusted(-6, -6, 6, 6), [this, every] { MarkAll(!every); Play(1); }});
        Put(p, style.head, TitleLeft, HeadTop + 13, QStringLiteral("GAME"), style.muted);
        Put(p, style.head, id_right, HeadTop + 13, QStringLiteral("ID"), style.muted, 1);
        Put(p, style.head, console_left, HeadTop + 13, QStringLiteral("CONSOLE"), style.muted);
        Put(p, style.head, script_left, HeadTop + 13, QStringLiteral("SCRIPT"), style.muted);
        Put(p, style.head, names_left, HeadTop + 13, QStringLiteral("NAMES"), style.muted);
        Put(p, style.head, library_right, HeadTop + 13, QStringLiteral("LIBRARY"), style.muted, 1);
        Put(p, style.head, cover_right, HeadTop + 13, QStringLiteral("COVER"), style.muted, 1);
        Put(p, style.head, spine_right, HeadTop + 13, QStringLiteral("SPINE"), style.muted, 1);
    }
    p.fillRect(QRectF(0, ListTop - 1, right, 1), style.rule);

    // The rows, a console's together: its name at the head of its games.
    QHash<QString, QString> console_names;
    for (const auto& [key, name] : shelf->Consoles()) {
        console_names.insert(key, name);
    }
    const int rows = Rows();
    p.save();
    p.setClipRect(QRectF(0, ListTop, W, rows * RowTall));
    for (int i = first; i < std::min<int>(shown.size(), first + rows); ++i) {
        const ScriptGame& game = all[shown[i]];
        const double top = ListTop + (i - first) * RowTall, base = top + 17;
        const bool is_marked = marked.contains(game.id), here = game.id == current;
        const bool group = i == first || all[shown[i - 1]].console != game.console;
        if (is_marked) {
            p.fillRect(QRectF(0, top, right, RowTall - 1), Faded(style.accent, 0.16));
        } else if (i == hover_row) {
            p.fillRect(QRectF(0, top, right, RowTall - 1), Faded(style.ink, 0.05));
        }
        if (here) {
            p.fillRect(QRectF(0, top, 3, RowTall - 1), active ? style.accent : Faded(style.accent, 0.35));
        }
        const QRectF check(7, top + 7, 11, 11);
        p.setPen(QPen(is_marked ? style.accent : Faded(style.muted, 0.8), 1.2));
        p.setBrush(is_marked ? QBrush(style.accent) : QBrush(Qt::NoBrush));
        p.drawRect(check);
        if (is_marked) {
            QPainterPath tick;
            tick.moveTo(check.left() + 2.5, check.center().y());
            tick.lineTo(check.left() + 4.8, check.bottom() - 2.5);
            tick.lineTo(check.right() - 2, check.top() + 2.5);
            p.setPen(QPen(style.ground, 1.6));
            p.setBrush(QBrush(Qt::NoBrush));
            p.drawPath(tick);
        }
        const bool has_script = game.script != Script::None && game.script != Script::NotInstalled && game.script != Script::TooNew;
        // Its ID (with how many more discs), then its title in the room the ID leaves.
        const int more = static_cast<int>(discs.value(game.id).size()) - 1;
        const QString id_said = more > 0 ? QStringLiteral("%1 +%2").arg(game.id).arg(more) : game.id;
        const double id_wide = Put(p, style.mono_small, id_right, base, id_said, style.muted, 1);
        Put(p, style.row, TitleLeft, base, Fit(game.title, style.row, id_right - id_wide - TitleLeft - 14), has_script || here ? style.ink : Faded(style.ink, 0.62));
        if (group) {
            Put(p, style.mono_small, console_left, base, ShortConsole(game.console, console_names.value(game.console)), style.ink);
        }
        const QColor script_colour = game.script == Script::Update ? style.accent : has_script ? style.ink : style.muted;
        Put(p, style.mono_small, script_left, base, ScriptSaid(game.script), script_colour);
        const QColor names_colour = game.names == Names::Installed ? style.ink : NamesWanted(game) ? style.accent : Faded(style.muted, 0.7);
        Put(p, style.mono_small, names_left, base, NamesSaid(game.names), names_colour);
        const auto [cover, spine] = art ? ArtOf(game.id) : std::pair<bool, bool>{false, false};
        const auto dot = [&](double x_right, bool have) {
            // A full dot: it has it; a ring: it has none (only the library's games want it).
            const QPointF at(x_right - 14, top + RowTall / 2);
            p.setPen(QPen(have ? style.ink : game.in_library ? style.accent : Faded(style.muted, 0.6), 1.2));
            p.setBrush(have ? QBrush(style.ink) : QBrush(Qt::NoBrush));
            p.drawEllipse(at, 3.5, 3.5);
        };
        dot(cover_right, cover);
        dot(spine_right, spine);
        if (game.in_library) {
            // On the shelf: a dot of the accent, else nothing.
            p.setPen(Qt::NoPen);
            p.setBrush(QBrush(style.accent));
            p.drawEllipse(QPointF(library_right - 18, top + RowTall / 2), 3.5, 3.5);
        }
        p.fillRect(QRectF(0, top + RowTall - 1, right, 1), group && i > first ? style.rule : Faded(style.rule, 0.45));
        if (group && i > first) {
            p.fillRect(QRectF(0, top, right, 1), style.rule);
        }
    }
    p.restore();
    if (shown.isEmpty()) {
        Put(p, style.row, TitleLeft, ListTop + 30,
            work.busy ? QStringLiteral("Fetching script lists…") : all.isEmpty() ? QStringLiteral("No games yet") : QStringLiteral("No matching games"),
            style.muted);
    }
    if (shown.size() > rows) {
        const QRectF track(W - 3, ListTop, 2, rows * RowTall);
        p.fillRect(track, Faded(style.rule, 0.6));
        p.fillRect(QRectF(track.left() - 0.5, track.top() + track.height() * first / shown.size(), 3, track.height() * rows / shown.size()),
                   style.muted);
    }

    // The selected game's full title and description, full width so neither is cut off.
    const double detail = H - FootTall + 6;
    const double foot = H - (FootTall - DetailTall) + 10;
    p.fillRect(QRectF(0, detail - 6, right, 1), style.rule);
    if (const ScriptGame* game = GameOf(current)) {
        Put(p, style.row, 0, detail + 14, Fit(game->title, style.row, right), style.ink);
        if (!game->reads.isEmpty()) {
            Put(p, style.row, 0, detail + 32, Fit(game->reads, style.row, right), style.muted);
        } else if (game->script == Script::None) {
            Put(p, style.row, 0, detail + 32, QStringLiteral("No script"), Faded(style.muted, 0.7));
        }
    }

    // The foot: action buttons and counts.
    p.fillRect(QRectF(0, foot - 6, right, 1), Faded(style.rule, 0.55));
    const QString counts =
        QStringLiteral("%1 · %2 WITH SCRIPTS · %3 · %4 SHOWN")
            .arg(all.size() == 1 ? QStringLiteral("1 GAME") : QStringLiteral("%1 GAMES").arg(all.size()))
            .arg(installed)
            .arg(updates == 1 ? QStringLiteral("1 UPDATE") : QStringLiteral("%1 UPDATES").arg(updates))
            .arg(shown.size());
    const double counts_wide = Put(p, style.mono_small, right, foot + 36, counts, style.muted, 1);
    if (work.busy || art_now.second > 0) {
        QString said;
        double share = 0;
        if (work.busy) {
            said = work.doing.toUpper() + (work.total > 0 ? QStringLiteral(" · %1 / %2").arg(work.done).arg(work.total) : QString());
            share = work.total > 0 ? static_cast<double>(work.done) / work.total : 0;
        } else {
            said = QStringLiteral("FINDING BOX ART · %1 / %2").arg(art_now.first).arg(art_now.second);
            share = static_cast<double>(art_now.first) / art_now.second;
        }
        p.fillRect(QRectF(0, foot - 6, right * std::clamp(share, 0.0, 1.0), 2), style.accent);
        const bool can_cancel = work.busy || static_cast<bool>(cancel_art);
        const double cancel_left = button(right, foot, QStringLiteral("CANCEL"), false, can_cancel, [this, busy = work.busy] {
            Play(2);
            if (busy) {
                shelf->Cancel();
            } else if (cancel_art) {
                cancel_art();
            }
        });
        Put(p, style.mono_small, 0, foot + 16, Fit(said, style.mono_small, cancel_left - 16), style.ink);
    } else {
        const QStringList target = Target();
        const bool have = !target.isEmpty();
        double bx = right;
        bx = button(bx, foot, QStringLiteral("REMOVE SCRIPTS…"), false, have, [this] { AskRemove(Target()); }) - 6;
        bx = button(bx, foot, QStringLiteral("FIND BOX ART"), false, have, [this] { Art(TargetGames()); }) - 4;
        bx = button(bx, foot, QStringLiteral("INSTALL NAMES"), false, have, [this] { Names(Target()); }) - 4;
        bx = button(bx, foot, QStringLiteral("INSTALL SCRIPTS"), false, have, [this] { Install(Target()); }) - 12;
        if (!marked.isEmpty()) {
            int hidden = static_cast<int>(marked.size());
            for (int i : shown) {
                hidden -= marked.contains(all[i].id) ? 1 : 0;
            }
            const QString said =
                hidden > 0 ? QStringLiteral("%1 MARKED · %2 HIDDEN").arg(marked.size()).arg(hidden) : QStringLiteral("%1 MARKED").arg(marked.size());
            Put(p, style.mono_small, 0, foot + 16, Fit(said, style.mono_small, bx), style.accent);
        }
        const QString last = shelf->LastAction();
        if (!last.isEmpty()) {
            const QStringList problems = shelf->Problems();
            const QFontMetricsF fm(style.mono_small);
            const QString details_label = problems_open ? QStringLiteral("HIDE DETAILS") : QStringLiteral("DETAILS");
            const double tail_room = (problems.isEmpty() ? 0 : fm.horizontalAdvance(details_label) + 12) + fm.horizontalAdvance(QStringLiteral("×")) + 24;
            const double last_room = std::max(40.0, right - counts_wide - 20 - tail_room);
            double lx = Put(p, style.mono_small, 0, foot + 36, Fit(last.toUpper(), style.mono_small, last_room),
                            problems.isEmpty() ? style.muted : style.ink) +
                        12;
            if (!problems.isEmpty()) {
                const bool over = hover_hit == hits.size();
                const double wide = Put(p, style.mono_small, lx, foot + 36, details_label, over ? style.ink : style.accent);
                p.fillRect(QRectF(lx, foot + 39, wide, 1), over ? style.ink : style.accent);
                hits.append({QRectF(lx - 4, foot + 24, wide + 8, 16), [this] { Play(1); problems_open = !problems_open; update(); }});
                lx += wide + 12;
            }
            const bool over = hover_hit == hits.size();
            const double wide = Put(p, style.mono_small, lx, foot + 36, QStringLiteral("×"), over ? style.ink : style.muted);
            hits.append({QRectF(lx - 4, foot + 24, wide + 8, 16), [this] {
                             Play(2);
                             problems_open = false;
                             shelf->ClearProblems();
                         }});
        }
    }
    if (problems_open && !shelf->Problems().isEmpty()) {
        const QStringList problems = shelf->Problems();
        const QRectF box(0, ListTop - 2, right, Rows() * RowTall + 2);
        p.fillRect(box, style.raised.alpha() > 0 ? style.raised : style.ground);
        p.setPen(QPen(style.rule, 1));
        p.setBrush(QBrush(Qt::NoBrush));
        p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
        Put(p, style.head, 16, box.top() + 22,
            problems.size() == 1 ? QStringLiteral("1 ERROR") : QStringLiteral("%1 ERRORS").arg(problems.size()), style.muted);
        button(box.right() - 12, box.top() + 8, QStringLiteral("CLOSE"), false, true, [this] { Play(2); problems_open = false; update(); });
        p.setFont(style.mono_small);
        p.setPen(style.ink);
        double y = box.top() + 44;
        for (int idx = 0; idx < problems.size(); ++idx) {
            const double reserve = idx + 1 < problems.size() ? 24 : 8;
            const QRectF room(16, y, box.width() - 32, box.bottom() - y - reserve);
            if (room.height() < 14) {
                Put(p, style.mono_small, 16, box.bottom() - 10, QStringLiteral("+ %1 more").arg(problems.size() - idx), style.muted);
                break;
            }
            QRectF used;
            p.drawText(room, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, problems[idx], &used);
            y = used.bottom() + 10;
        }
    }
}

void ScriptsPanel::mousePressEvent(QMouseEvent* event) {
    if (search->hasFocus() && parentWidget() != nullptr) {
        parentWidget()->setFocus();
    }
    if (entered) {
        entered();
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPointF at = Sheet(event->position());
    for (const Hit& hit : hits) {
        if (hit.area.contains(at) && hit.act) {
            hit.act();
            update();
            return;
        }
    }
    if (problems_open && at.y() >= ListTop - 2 && at.y() < ListTop + Rows() * RowTall) {
        return;
    }
    const int row = RowAt(at.y());
    if (row < 0) {
        return;
    }
    const ScriptGame& game = all[shown[row]];
    const Qt::KeyboardModifiers held = event->modifiers();
    if (at.x() < CheckWide || (held & Qt::ControlModifier)) {
        ToggleMark(game.id);
        current = game.id;
        Play(1);
    } else if (held & Qt::ShiftModifier) {
        SetCurrent(row, true);
    } else {
        marked.clear();
        current = game.id;
        anchor = game.id;
        Play(0);
    }
    update();
}

void ScriptsPanel::mouseMoveEvent(QMouseEvent* event) {
    const QPointF at = Sheet(event->position());
    int over = -1;
    for (int i = 0; i < hits.size(); ++i) {
        over = hits[i].act && hits[i].area.contains(at) ? i : over;
    }
    const int row = RowAt(at.y());
    if (over != hover_hit || row != hover_row) {
        hover_hit = over;
        hover_row = row;
        setCursor(over >= 0 || row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void ScriptsPanel::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || problems_open) {
        return;
    }
    const QPointF at = Sheet(event->position());
    if (at.x() >= CheckWide && RowAt(at.y()) >= 0) {
        Actions();
    }
}

void ScriptsPanel::leaveEvent(QEvent*) {
    hover_hit = -1;
    hover_row = -1;
    update();
}

void ScriptsPanel::wheelEvent(QWheelEvent* event) {
    const int steps = -event->angleDelta().y() / 40;
    first = std::clamp(first + (steps == 0 ? (event->angleDelta().y() < 0 ? 1 : -1) : steps), 0, std::max(0, static_cast<int>(shown.size()) - Rows()));
    hover_row = RowAt(Sheet(event->position()).y());
    update();
}

} // namespace Home
