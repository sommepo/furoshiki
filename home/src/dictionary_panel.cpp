// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QWheelEvent>
#include "dictionary_panel.h"

namespace Home {
namespace {

// The panel's own measures, in the sheet's units (the page is 1280×720; the panel's place on it
// is HomeWindow's, some 800×548).
constexpr double ToolbarTall = 30;
constexpr double ChipsTop = 46;
constexpr double HeadTop0 = 78;
constexpr double ListTop0 = 96;
constexpr double BannerTall = 60;
constexpr double RowTall = 25;
constexpr double FootTall = 54;
constexpr double Bar = 10; ///< The scroll bar's room at the right
constexpr double CheckWide = 26;
constexpr double PlaceRight = 62;
constexpr double TitleLeft = 74;

QColor Faded(QColor colour, double alpha) {
    colour.setAlphaF(colour.alphaF() * alpha);
    return colour;
}

QString Count(qint64 n) {
    return n <= 0 ? QString() : QLocale(QLocale::English).toString(n);
}

/// Text at a baseline, left (0), right (1) or centred (2) on x; its width.
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

} // namespace

DictionaryPanel::DictionaryPanel(QWidget* parent, DictionaryShelf* shelf_) : QWidget(parent), shelf(shelf_) {
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus); // the page keeps the keys and hands them on (Key)
    search = new QLineEdit(this);
    search->setPlaceholderText(QStringLiteral("Search dictionaries"));
    search->setClearButtonEnabled(true);
    search->setFrame(false);
    search->installEventFilter(this);
    connect(search, &QLineEdit::textChanged, this, [this] {
        first = 0;
        Filter();
        update();
    });
    // While the old file's dictionaries are moved into the engine the list is the old file's:
    // looked at again every few seconds until that is done.
    converting_check.setInterval(2000);
    connect(&converting_check, &QTimer::timeout, this, [this] {
        if (!shelf->Converting()) {
            converting_check.stop();
            shelf->Refresh();
        }
    });
    Reload();
}

void DictionaryPanel::SetStyle(const Style& style_) {
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

void DictionaryPanel::SetActive(bool on) {
    if (active != on) {
        active = on;
        update();
    }
}

double DictionaryPanel::Width() const {
    return width() / std::max(0.01, style.scale);
}

double DictionaryPanel::Height() const {
    return height() / std::max(0.01, style.scale);
}

void DictionaryPanel::resizeEvent(QResizeEvent*) {
    PlaceSearch();
}

void DictionaryPanel::hideEvent(QHideEvent*) {
    if (moving) {
        EndMove(true);
    }
}

void DictionaryPanel::PlaceSearch() {
    const double s = style.scale;
    search->setGeometry(QRectF(0, 0, 320 * s, ToolbarTall * s).toRect());
}

DictionaryPanel::Kind DictionaryPanel::KindOf(const DictionaryEntry& entry) {
    // A guess from what a dictionary says of itself: Yomitan's index has no kind.
    if (entry.names) {
        return Names;
    }
    const QString said = entry.title + QLatin1Char(' ') + entry.description;
    static const QRegularExpression grammar(QStringLiteral("grammar|文法|文型|dojg|表現文型"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pitch(QStringLiteral("pitch|accent|アクセント|高低"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kanji(QStringLiteral("kanji|漢字"), QRegularExpression::CaseInsensitiveOption);
    if (entry.terms == 0 && entry.metas > 0) {
        return !entry.frequency_mode.isEmpty() || !pitch.match(said).hasMatch() ? Frequency : Pitch;
    }
    if (entry.terms == 0 && entry.metas == 0) {
        return Kanji; // characters only (its kanji banks)
    }
    if (grammar.match(said).hasMatch()) {
        return Grammar;
    }
    if (kanji.match(entry.title).hasMatch() && entry.terms < 20000) {
        return Kanji;
    }
    return Words;
}

QString DictionaryPanel::KindName(Kind kind) {
    static const char* const names[] = {"Words", "Grammar", "Kanji", "Frequency", "Pitch", "Names"};
    return QString::fromLatin1(names[kind]);
}

void DictionaryPanel::Reload() {
    QSet<qint64> prev_order_set;
    for (const DictionaryEntry& entry : all) {
        if (!entry.names) {
            prev_order_set.insert(entry.id);
        }
    }
    all = shelf->List();
    kinds.clear();
    places.clear();
    QSet<qint64> present, next_order_set;
    int place = 0;
    for (const DictionaryEntry& entry : all) {
        kinds.insert(entry.id, KindOf(entry));
        if (!entry.names) {
            places.insert(entry.id, ++place);
            next_order_set.insert(entry.id);
        }
        present.insert(entry.id);
    }
    if (prev_order_set != next_order_set) {
        undo_order.clear();
        undone = false;
        if (moving) {
            moving = false;
            moving_ids.clear();
            moving_from.clear();
        }
    }
    marked.intersect(present);
    if (!present.contains(current)) {
        current = -1;
    }
    // An import: what was there when it began; each dictionary since then is NEW, the newest in
    // hand and in sight, until its result is put away.
    const bool busy = shelf->Work().busy;
    qint64 newest = -1;
    if (busy && !importing) {
        importing = true;
        before_import = present;
        arrived.clear();
    } else if (importing) {
        for (const DictionaryEntry& entry : all) {
            if (!before_import.contains(entry.id) && !arrived.contains(entry.id)) {
                arrived.insert(entry.id);
                newest = entry.id;
            }
        }
        arrived.intersect(present);
        if (!busy && shelf->LastImport().isEmpty()) {
            importing = false;
            before_import.clear();
            arrived.clear();
        }
    }
    if (shelf->Converting() && !converting_check.isActive()) {
        converting_check.start();
    }
    Filter();
    if (newest >= 0 && !moving && marked.isEmpty()) {
        current = newest;
        Keep();
    }
    if (moving) {
        Keep(); // the rows being moved stay in sight as they go
    }
    update();
}

void DictionaryPanel::Filter() {
    shown.clear();
    const QString wanted = search->text().trimmed();
    for (int i = 0; i < all.size(); ++i) {
        const DictionaryEntry& entry = all[i];
        const Kind its = kinds.value(entry.id);
        if (kind < 0 ? its == Names : its != kind) {
            continue; // the names dictionaries only where asked for: Furoshiki keeps them itself
        }
        if ((state == OnOnly && !entry.enabled) || (state == OffOnly && entry.enabled)) {
            continue;
        }
        if (!wanted.isEmpty()) {
            const QString said = entry.title + QLatin1Char(' ') + entry.author + QLatin1Char(' ') + entry.revision + QLatin1Char(' ') +
                                 entry.description + QLatin1Char(' ') + KindName(its);
            bool all_words = true;
            for (const QString& word : wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                all_words = all_words && said.contains(word, Qt::CaseInsensitive);
            }
            if (!all_words) {
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
        current = shown.isEmpty() ? -1 : all[shown.first()].id;
    }
    first = std::clamp(first, 0, std::max(0, static_cast<int>(shown.size()) - Rows()));
}

const DictionaryEntry* DictionaryPanel::EntryOf(qint64 id) const {
    for (const DictionaryEntry& entry : all) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

QList<qint64> DictionaryPanel::Target() const {
    // The marked ones, in their order; else the row in hand.
    QList<qint64> out;
    for (const DictionaryEntry& entry : all) {
        if (marked.contains(entry.id)) {
            out.append(entry.id);
        }
    }
    if (out.isEmpty() && current >= 0) {
        out.append(current);
    }
    return out;
}

QList<qint64> DictionaryPanel::Order() const {
    QList<qint64> out;
    for (const DictionaryEntry& entry : all) {
        if (!entry.names) {
            out.append(entry.id);
        }
    }
    return out;
}

int DictionaryPanel::Rows() const {
    return std::max(1, static_cast<int>((Height() - FootTall - ListTop()) / RowTall));
}

int DictionaryPanel::RowAt(double y) const {
    if (y < ListTop()) {
        return -1;
    }
    const int row = first + static_cast<int>((y - ListTop()) / RowTall);
    return row < shown.size() && row < first + Rows() ? row : -1;
}

QPointF DictionaryPanel::Sheet(const QPointF& at) const {
    return at / std::max(0.01, style.scale);
}

void DictionaryPanel::Play(int which) const {
    if (sound) {
        sound(which);
    }
}

double DictionaryPanel::Banner() const {
    return shelf->Work().busy || !shelf->LastImport().isEmpty() ? BannerTall : 0;
}

double DictionaryPanel::HeadTop() const {
    return HeadTop0 + Banner();
}

double DictionaryPanel::ListTop() const {
    return ListTop0 + Banner();
}

void DictionaryPanel::Keep() {
    int at = -1;
    for (int i = 0; i < shown.size(); ++i) {
        at = all[shown[i]].id == current ? i : at;
    }
    if (at >= 0) {
        first = std::clamp(first, at - Rows() + 1, at);
    }
    first = std::clamp(first, 0, std::max(0, static_cast<int>(shown.size()) - Rows()));
}

void DictionaryPanel::SetCurrent(int index, bool extend) {
    if (shown.isEmpty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(shown.size()) - 1);
    const qint64 id = all[shown[index]].id;
    if (extend) {
        // From where the range began to here, marked (as Shift does in a list on a computer).
        int from = index;
        for (int i = 0; i < shown.size(); ++i) {
            from = all[shown[i]].id == (anchor >= 0 ? anchor : current) ? i : from;
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

void DictionaryPanel::ToggleMark(qint64 id) {
    if (!marked.remove(id)) {
        marked.insert(id);
    }
    anchor = id;
    update();
}

void DictionaryPanel::MarkAll(bool on) {
    if (on) {
        for (int i : shown) {
            marked.insert(all[i].id);
        }
    } else {
        marked.clear();
    }
    update();
}

void DictionaryPanel::Switch(const QList<qint64>& ids, bool on) {
    if (ids.isEmpty() || shelf->Work().busy || shelf->Converting()) {
        Play(3);
        return;
    }
    Play(0);
    shelf->SetEnabled(ids, on);
}

void DictionaryPanel::Move(const QList<qint64>& ids, int how, int steps) {
    if (shelf->Work().busy || shelf->Converting()) {
        Play(3);
        return;
    }
    QList<qint64> order = Order();
    const QSet<qint64> chosen(ids.begin(), ids.end());
    if (how == -2 || how == 2) {
        QList<qint64> them, rest;
        for (qint64 id : order) {
            (chosen.contains(id) ? them : rest).append(id);
        }
        order = how == -2 ? them + rest : rest + them;
    } else if (how == -1) {
        for (int s = 0; s < std::max(1, steps); ++s) {
            for (int i = 1; i < order.size(); ++i) {
                if (chosen.contains(order[i]) && !chosen.contains(order[i - 1])) {
                    order.swapItemsAt(i, i - 1);
                }
            }
        }
    } else {
        for (int s = 0; s < std::max(1, steps); ++s) {
            for (int i = static_cast<int>(order.size()) - 2; i >= 0; --i) {
                if (chosen.contains(order[i]) && !chosen.contains(order[i + 1])) {
                    order.swapItemsAt(i, i + 1);
                }
            }
        }
    }
    if (order == Order()) {
        Play(3);
        return;
    }
    Play(0);
    if (moving) {
        shelf->Reorder(order); // a step of a move: the move as a whole is undone (EndMove)
    } else {
        Reordered(order);
    }
}

void DictionaryPanel::Reordered(const QList<qint64>& order) {
    if (shelf->Work().busy || shelf->Converting()) {
        return;
    }
    undo_order = Order();
    undone = false;
    shelf->Reorder(order);
}

void DictionaryPanel::Undo() {
    if (undo_order.isEmpty() || shelf->Work().busy || shelf->Converting()) {
        Play(3);
        return;
    }
    // Undo again puts the change back: the two orders change places.
    const QList<qint64> before = undo_order;
    undo_order = Order();
    undone = !undone;
    Play(2);
    shelf->Reorder(before);
}

void DictionaryPanel::BeginMove() {
    if (shelf->Work().busy || shelf->Converting()) {
        Play(3);
        return;
    }
    // What is marked (else the row in hand), but never a names dictionary: Furoshiki places those.
    moving_ids.clear();
    for (qint64 id : Target()) {
        if (const DictionaryEntry* entry = EntryOf(id); entry != nullptr && !entry->names) {
            moving_ids.append(id);
        }
    }
    if (moving_ids.isEmpty()) {
        Play(3);
        return;
    }
    moving = true;
    moving_from = Order();
    if (!moving_ids.contains(current)) {
        current = moving_ids.first();
    }
    Keep();
    Play(1);
    update();
}

void DictionaryPanel::EndMove(bool keep) {
    if (!moving) {
        return;
    }
    moving = false;
    if (!keep) {
        shelf->Reorder(moving_from);
        Play(2);
    } else {
        if (Order() != moving_from) {
            undo_order = moving_from;
            undone = false;
        }
        Play(1);
    }
    update();
}

bool DictionaryPanel::MoveKey(int key) {
    // While moving, the keys move the rows, live; Enter sets them down, Esc cancels.
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
        Move(moving_ids, key == Qt::Key_Up ? -1 : 1);
        break;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Q:
    case Qt::Key_E:
        Move(moving_ids, key == Qt::Key_PageUp || key == Qt::Key_Q ? -1 : 1, 10);
        break;
    case Qt::Key_Home:
    case Qt::Key_End:
        Move(moving_ids, key == Qt::Key_Home ? -2 : 2);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
    case Qt::Key_M:
        EndMove(true);
        break;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        EndMove(false);
        break;
    default:
        break;
    }
    return true;
}

void DictionaryPanel::SortByKind() {
    // Words, grammar, kanji, pitch, frequency: each type's own order kept.
    if (!ask || shelf->Work().busy || shelf->Converting()) {
        return;
    }
    ask(QStringLiteral("SORT BY TYPE"),
        {QStringLiteral("Words, grammar, kanji, pitch, frequency"), QStringLiteral("Words, grammar, frequency, pitch, kanji"), QStringLiteral("Cancel")}, -1,
        [this](int index) {
            if (index < 0 || index > 1) {
                return;
            }
            static const int first_rank[KindCount] = {0, 1, 2, 4, 3, 9};  // Words, Grammar, Kanji, Frequency, Pitch, Names
            static const int second_rank[KindCount] = {0, 1, 4, 2, 3, 9};
            const int* rank = index == 0 ? first_rank : second_rank;
            QList<qint64> order = Order();
            std::stable_sort(order.begin(), order.end(), [this, rank](qint64 a, qint64 b) { return rank[kinds.value(a)] < rank[kinds.value(b)]; });
            if (order == Order()) {
                if (say) {
                    say(QStringLiteral("Dictionaries are already in that order"));
                }
                return;
            }
            Reordered(order);
            if (say) {
                say(QStringLiteral("Sorted by type · Press Ctrl+Z to undo"));
            }
        });
}

void DictionaryPanel::DropAt(const QList<qint64>& ids, int gap) {
    if (shelf->Work().busy || shelf->Converting()) {
        return;
    }
    // The rows dragged go before the row shown at `gap` (after the last visible row at its end).
    const qint64 before = gap >= 0 && gap < shown.size() ? all[shown[gap]].id : -1;
    const QSet<qint64> chosen(ids.begin(), ids.end());
    QList<qint64> rest, them;
    for (qint64 id : Order()) {
        (chosen.contains(id) ? them : rest).append(id);
    }
    if (them.isEmpty() || chosen.contains(before)) {
        return;
    }
    int at = -1;
    if (before >= 0) {
        at = static_cast<int>(rest.indexOf(before));
        if (at < 0) {
            return;
        }
    } else {
        // Dropped after the last shown row: place right after the last shown non-dragged row.
        at = static_cast<int>(rest.size());
        for (int i = static_cast<int>(shown.size()) - 1; i >= 0; --i) {
            const int pos = static_cast<int>(rest.indexOf(all[shown[i]].id));
            if (pos >= 0) {
                at = pos + 1;
                break;
            }
        }
    }
    QList<qint64> order = rest.mid(0, at) + them + rest.mid(at);
    if (order != Order()) {
        Play(1);
        Reordered(order);
    }
}

void DictionaryPanel::AskDelete(const QList<qint64>& ids) {
    if (ids.isEmpty() || !ask || shelf->Work().busy || shelf->Converting()) {
        Play(3);
        return;
    }
    const DictionaryEntry* one = EntryOf(ids.first());
    const QString title = ids.size() == 1 && one != nullptr ? QStringLiteral("DELETE %1?").arg(one->title.toUpper())
                                                            : QStringLiteral("DELETE %1 DICTIONARIES?").arg(ids.size());
    ask(title, {QStringLiteral("Cancel"), QStringLiteral("Delete")}, 0,
        [this, ids](int index) {
            if (index == 1) {
                for (qint64 id : ids) {
                    marked.remove(id);
                }
                shelf->Delete(ids);
            }
        });
}

void DictionaryPanel::Actions() {
    // Every action on what is marked (else the row in hand) at once, for a controller's confirm.
    const QList<qint64> target = Target();
    if (target.isEmpty() || !ask || shelf->Work().busy || shelf->Converting()) {
        Play(3);
        return;
    }
    const DictionaryEntry* here = EntryOf(current);
    bool any_on = false, any_off = false, movable = false;
    for (qint64 id : target) {
        if (const DictionaryEntry* entry = EntryOf(id)) {
            any_on = any_on || entry->enabled;
            any_off = any_off || !entry->enabled;
            movable = movable || !entry->names;
        }
    }
    QStringList said;
    QList<std::function<void()>> does;
    const auto add = [&](const QString& name, std::function<void()> act) {
        said.append(name);
        does.append(std::move(act));
    };
    if (here != nullptr) {
        add(marked.contains(current) ? QStringLiteral("Unmark") : QStringLiteral("Mark"),
            [this] { ToggleMark(current); });
    }
    if (any_off) {
        add(QStringLiteral("Turn on"), [this, target] { Switch(target, true); });
    }
    if (any_on) {
        add(QStringLiteral("Turn off"), [this, target] { Switch(target, false); });
    }
    if (movable) {
        add(QStringLiteral("Move…"), [this] { BeginMove(); });
        add(QStringLiteral("Move to top"), [this, target] { Move(target, -2); });
        add(QStringLiteral("Move to bottom"), [this, target] { Move(target, 2); });
    }
    add(QStringLiteral("Delete…"), [this, target] { QTimer::singleShot(0, this, [this, target] { AskDelete(target); }); });
    add(QStringLiteral("Mark all shown"), [this] { MarkAll(true); });
    if (!marked.isEmpty()) {
        add(QStringLiteral("Unmark all"), [this] { MarkAll(false); });
    }
    const QString title = !marked.isEmpty() ? QStringLiteral("%1 MARKED").arg(marked.size())
                                            : here != nullptr ? here->title.toUpper() : QString();
    ask(title, said, -1, [does](int index) {
        if (index >= 0 && index < does.size()) {
            does[index]();
        }
    });
}

bool DictionaryPanel::Key(int key) {
    const Qt::KeyboardModifiers held = QGuiApplication::keyboardModifiers();
    const bool shift = held & Qt::ShiftModifier, ctrl = held & Qt::ControlModifier, alt = held & Qt::AltModifier;
    if (moving) {
        return MoveKey(key);
    }
    int at = -1;
    for (int i = 0; i < shown.size(); ++i) {
        at = all[shown[i]].id == current ? i : at;
    }
    switch (key) {
    case Qt::Key_M:
        BeginMove();
        return true;
    case Qt::Key_Z:
        if (ctrl) {
            Undo();
        }
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
        if (alt) {
            Move(Target(), key == Qt::Key_Up ? -1 : 1);
            return true;
        }
        if ((key == Qt::Key_Up && at <= 0) || (key == Qt::Key_Down && at >= shown.size() - 1)) {
            return true;
        }
        SetCurrent(at + (key == Qt::Key_Up ? -1 : 1), shift);
        Play(0);
        return true;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if (ctrl || alt || shift) {
            SetCurrent(at + (key == Qt::Key_PageUp ? -Rows() : Rows()), shift);
            Play(0);
            return true;
        }
        return false; // the page's: along the sections
    case Qt::Key_Home:
    case Qt::Key_End:
        if (!shown.isEmpty()) {
            SetCurrent(key == Qt::Key_Home ? 0 : static_cast<int>(shown.size()) - 1, shift);
            Play(0);
        }
        return true;
    case Qt::Key_Space:
        if (current >= 0) {
            ToggleMark(current);
            Play(1);
        }
        return true;
    case Qt::Key_A:
        if (ctrl) {
            MarkAll(true);
            Play(1);
            return true;
        }
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
        Switch(Target(), key == Qt::Key_Right);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        Actions();
        return true;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        AskDelete(Target());
        return true;
    case Qt::Key_Slash:
    case Qt::Key_F:
        search->setFocus();
        search->selectAll();
        Play(1);
        return true;
    case Qt::Key_I:
        if (!shelf->Work().busy && !shelf->Converting() && import_ask) {
            Play(1);
            import_ask();
        } else {
            Play(3);
        }
        return true;
    case Qt::Key_Escape:
        if (problems_open) {
            problems_open = false;
            update();
            Play(2);
            return true;
        }
        if (!shelf->Work().busy && !shelf->LastImport().isEmpty()) {
            shelf->ClearProblems(); // the import's result put away, as its OK
            Play(2);
            return true;
        }
        if (!marked.isEmpty()) {
            MarkAll(false); // the marks first; again, and out to the sections
            Play(2);
            return true;
        }
        return false;
    default:
        return false;
    }
}

QList<QPair<QString, int>> DictionaryPanel::KeyHints() const {
    if (moving) {
        return {{QStringLiteral("↑"), Qt::Key_Up},
                {QStringLiteral("↓ MOVE"), Qt::Key_Down},
                {QStringLiteral("PGUP/PGDN JUMP 10"), Qt::Key_PageDown},
                {QStringLiteral("HOME/END TOP/BOTTOM"), Qt::Key_Home},
                {QStringLiteral("ENTER DONE"), Qt::Key_Return},
                {QStringLiteral("ESC CANCEL"), Qt::Key_Escape}};
    }
    return {{QStringLiteral("↑"), Qt::Key_Up},
            {QStringLiteral("↓ ROW"), Qt::Key_Down},
            {QStringLiteral("SPACE MARK"), Qt::Key_Space},
            {QStringLiteral("←"), Qt::Key_Left},
            {QStringLiteral("→ OFF/ON"), Qt::Key_Right},
            {QStringLiteral("ENTER ACTIONS"), Qt::Key_Return},
            {QStringLiteral("M MOVE"), Qt::Key_M},
            {QStringLiteral("/ SEARCH"), Qt::Key_Slash},
            {QStringLiteral("I IMPORT"), Qt::Key_I},
            {marked.isEmpty() ? QStringLiteral("ESC SECTIONS") : QStringLiteral("ESC UNMARK"), Qt::Key_Escape}};
}

void DictionaryPanel::Search(const QString& text) {
    search->setText(text);
}

void DictionaryPanel::ChooseKind(const QString& name) {
    kind = -1;
    for (int k = 0; k < KindCount; ++k) {
        if (KindName(static_cast<Kind>(k)).compare(name, Qt::CaseInsensitive) == 0) {
            kind = k;
        }
    }
    first = 0;
    Filter();
    update();
}

void DictionaryPanel::ClickAt(const QPointF& sheet, const QString& modifiers) {
    const Qt::KeyboardModifiers held = modifiers == QStringLiteral("ctrl")    ? Qt::ControlModifier
                                       : modifiers == QStringLiteral("shift") ? Qt::ShiftModifier
                                                                               : Qt::NoModifier;
    const QPointF at = sheet * style.scale;
    QMouseEvent press(QEvent::MouseButtonPress, at, mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, held);
    mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, mapToGlobal(at), Qt::LeftButton, Qt::NoButton, held);
    mouseReleaseEvent(&release);
}

void DictionaryPanel::DragAt(const QPointF& from, const QPointF& to) {
    const QPointF a = from * style.scale;
    QMouseEvent press(QEvent::MouseButtonPress, a, mapToGlobal(a), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mousePressEvent(&press);
    for (int step = 1; step <= 8; ++step) {
        const QPointF at = (from + (to - from) * step / 8.0) * style.scale;
        QMouseEvent move(QEvent::MouseMove, at, mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        mouseMoveEvent(&move);
    }
    const QPointF b = to * style.scale;
    QMouseEvent release(QEvent::MouseButtonRelease, b, mapToGlobal(b), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    mouseReleaseEvent(&release);
}

bool DictionaryPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search && event->type() == QEvent::KeyPress) {
        // The search's own keys; leaving it gives the keys back to the page and the list.
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

void DictionaryPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(style.scale, style.scale);
    const double W = Width(), H = Height();
    const double right = W - Bar; // the columns' right edge
    hits.clear();
    const auto button = [&](double x_right, double y, const QString& text, bool strong, bool enabled, std::function<void()> act) {
        // A word in a box: the page's buttons (strong: the accent's). Returns its left edge.
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
        Put(p, style.mono, box.center().x(), y + 16, text,
            strong && enabled ? style.ground : enabled ? style.ink : Faded(style.muted, 0.6), 2);
        hits.append({box, enabled ? std::move(act) : std::function<void()>()});
        return box.left();
    };
    const auto chip = [&](double x, double y, const QString& text, qint64 n, bool on, std::function<void()> act) {
        // A filter: its word and how many, the chosen one in ink over a rule of the accent.
        const QString said = n >= 0 ? QStringLiteral("%1 %2").arg(text).arg(n) : text;
        const double wide = QFontMetricsF(style.mono_small).horizontalAdvance(said);
        const bool over = hover_hit == hits.size();
        Put(p, style.mono_small, x, y, said, on ? style.ink : over ? style.ink : style.muted);
        if (on) {
            p.fillRect(QRectF(x, y + 5, wide, 2), style.accent);
        }
        hits.append({QRectF(x - 6, y - 14, wide + 12, 24), std::move(act)});
        return wide + 18;
    };

    // The toolbar: the search (a line of its own, placed by PlaceSearch), import and Jitendex.
    const DictionaryWork work = shelf->Work();
    const bool converting = shelf->Converting();
    const bool editable = !work.busy && !converting && !moving;
    double x = right;
    x = button(x, 3, QStringLiteral("IMPORT…"), true, editable, [this] {
            Play(1);
            if (import_ask) {
                import_ask();
            }
        }) - 8;
    x = button(x, 3, QStringLiteral("SORT…"), false, editable, [this] {
            Play(1);
            SortByKind();
        }) - 8;
    bool jitendex = false;
    for (const DictionaryEntry& entry : all) {
        jitendex = jitendex || entry.title.startsWith(QStringLiteral("Jitendex"), Qt::CaseInsensitive);
    }
    if (!jitendex) {
        button(x, 3, QStringLiteral("DOWNLOAD JITENDEX"), false, editable, [this] {
            Play(1);
            shelf->DownloadJitendex();
        });
    }

    // The filters: what kind, then on or off; each with how many it has.
    int per_kind[KindCount] = {};
    int on_count = 0, others = 0;
    for (const DictionaryEntry& entry : all) {
        ++per_kind[kinds.value(entry.id)];
        if (!entry.names) {
            ++others;
            on_count += entry.enabled ? 1 : 0;
        }
    }
    x = 0;
    x += chip(x, ChipsTop + 12, QStringLiteral("ALL"), others, kind < 0, [this] { ChooseKind({}); Play(0); });
    for (int k = 0; k < KindCount; ++k) {
        if (per_kind[k] > 0 || kind == k) {
            x += chip(x, ChipsTop + 12, KindName(static_cast<Kind>(k)).toUpper(), per_kind[k], kind == k,
                      [this, k] { ChooseKind(KindName(static_cast<Kind>(k))); Play(0); });
        }
    }
    {
        const QFontMetricsF metrics(style.mono_small);
        double sx = right - metrics.horizontalAdvance(QStringLiteral("ANY ON OFF")) - 36;
        const auto set_state = [this](State to) {
            return [this, to] {
                state = to;
                first = 0;
                Filter();
                Play(0);
                update();
            };
        };
        sx += chip(sx, ChipsTop + 12, QStringLiteral("ANY"), -1, state == AnyState, set_state(AnyState));
        sx += chip(sx, ChipsTop + 12, QStringLiteral("ON"), -1, state == OnOnly, set_state(OnOnly));
        chip(sx, ChipsTop + 12, QStringLiteral("OFF"), -1, state == OffOnly, set_state(OffOnly));
    }

    // The import box: how far it is and the dictionary coming in, with a bar that fills; then the
    // result, kept until it is put away (OK, ×, Esc), so that an import is never missed.
    if (Banner() > 0) {
        const QRectF box(0, HeadTop0 - 4, right, BannerTall - 10);
        const QStringList problems = shelf->Problems();
        const bool trouble = !work.busy && !problems.isEmpty();
        p.fillRect(box, Faded(trouble ? style.ink : style.accent, 0.10));
        p.fillRect(QRectF(box.left(), box.top(), 3, box.height()), trouble ? style.ink : style.accent);
        double bx = right - 8;
        QString head, line;
        if (work.busy) {
            const bool deleting = work.doing.startsWith(QStringLiteral("Deleting"));
            const bool indexing = work.doing.startsWith(QStringLiteral("Indexing"));
            const bool downloading = work.doing.startsWith(QStringLiteral("Downloading"));
            const bool can_cancel = !deleting && !indexing;
            if (can_cancel) {
                bx = button(bx, box.top() + 8, QStringLiteral("CANCEL"), false, true, [this] {
                         Play(2);
                         shelf->Cancel();
                     }) - 12;
            }
            if (deleting) {
                head = QStringLiteral("DELETING");
            } else if (indexing) {
                head = QStringLiteral("INDEXING");
            } else if (downloading) {
                head = QStringLiteral("DOWNLOADING");
            } else {
                head = work.files > 1 ? QStringLiteral("IMPORTING · %1 OF %2").arg(work.file).arg(work.files) : QStringLiteral("IMPORTING");
                if (!arrived.isEmpty()) {
                    head += QStringLiteral(" · %1 IMPORTED").arg(arrived.size());
                }
            }
            if (work.total > 0) {
                head += work.unit.isEmpty() ? QStringLiteral(" · %1 / %2").arg(work.done).arg(work.total)
                                            : QStringLiteral(" · %1 / %2 %3").arg(work.done).arg(work.total).arg(work.unit);
            }
            line = work.doing;
            if (line.startsWith(QStringLiteral("Importing "))) {
                line = line.mid(10);
            }
            // The whole import's share done: the files before this one, and this one's part.
            const double part = work.total > 0 ? std::clamp(static_cast<double>(work.done) / work.total, 0.0, 1.0) : 0.0;
            const double share = std::clamp((std::max(0, work.file - 1) + part) / std::max(1, work.files), 0.0, 1.0);
            const QRectF track(box.left() + 14, box.bottom() - 9, box.width() - 28, 4);
            p.fillRect(track, Faded(style.rule, 0.7));
            p.fillRect(QRectF(track.left(), track.top(), track.width() * share, track.height()), style.accent);
        } else {
            bx = button(bx, box.top() + 13, QStringLiteral("OK"), true, true, [this] {
                     problems_open = false;
                     shelf->ClearProblems();
                     Play(2);
                 }) - 8;
            if (!problems.isEmpty()) {
                bx = button(bx, box.top() + 13, problems_open ? QStringLiteral("HIDE DETAILS") : QStringLiteral("DETAILS"), false, true, [this] {
                         Play(1);
                         problems_open = !problems_open;
                         update();
                     }) - 12;
            }
            head = trouble ? QStringLiteral("IMPORT FINISHED WITH ERRORS") : QStringLiteral("IMPORT FINISHED");
            if (!arrived.isEmpty()) {
                head += QStringLiteral(" · NEW ITEMS HIGHLIGHTED");
            }
            line = shelf->LastImport();
        }
        Put(p, style.mono_small, box.left() + 14, box.top() + 16, Fit(head, style.mono_small, bx - 28), trouble ? style.ink : style.accent);
        Put(p, style.row, box.left() + 14, box.top() + 36, Fit(line, style.row, bx - 28), style.ink);
    }

    // The columns' heads.
    const double kind_left = right - 300, entries_right = right - 120, pictures_right = right - 52, switch_left = right - 34;
    const bool every = !shown.isEmpty() && std::all_of(shown.begin(), shown.end(), [this](int i) { return marked.contains(all[i].id); });
    {
        const QRectF box(7, HeadTop() + 3, 11, 11);
        p.setPen(QPen(every ? style.accent : style.muted, 1.2));
        p.setBrush(every ? QBrush(style.accent) : QBrush(Qt::NoBrush));
        p.drawRect(box);
        hits.append({box.adjusted(-6, -6, 6, 6), [this, every] { MarkAll(!every); Play(1); }});
        Put(p, style.head, PlaceRight, HeadTop() + 13, QStringLiteral("#"), style.muted, 1);
        Put(p, style.head, TitleLeft, HeadTop() + 13, QStringLiteral("TITLE"), style.muted);
        Put(p, style.head, kind_left, HeadTop() + 13, QStringLiteral("TYPE"), style.muted);
        Put(p, style.head, entries_right, HeadTop() + 13, QStringLiteral("ENTRIES"), style.muted, 1);
        Put(p, style.head, pictures_right, HeadTop() + 13, QStringLiteral("PICTURES"), style.muted, 1);
        Put(p, style.head, right, HeadTop() + 13, QStringLiteral("ON"), style.muted, 1);
    }
    p.fillRect(QRectF(0, ListTop() - 1, right, 1), style.rule);

    // The rows.
    const int rows = Rows();
    p.save();
    p.setClipRect(QRectF(0, ListTop(), W, rows * RowTall));
    for (int i = first; i < std::min<int>(shown.size(), first + rows); ++i) {
        const DictionaryEntry& entry = all[shown[i]];
        const double top = ListTop() + (i - first) * RowTall, base = top + 17;
        const bool is_marked = marked.contains(entry.id), here = entry.id == current;
        if (is_marked) {
            p.fillRect(QRectF(0, top, right, RowTall - 1), Faded(style.accent, 0.16));
        } else if (i == hover_row) {
            p.fillRect(QRectF(0, top, right, RowTall - 1), Faded(style.ink, 0.05));
        }
        if (moving && moving_ids.contains(entry.id)) {
            // Being moved: lifted out of the list, an outline in the accent.
            p.setPen(QPen(style.accent, 1.5));
            p.setBrush(QBrush(Faded(style.accent, 0.12)));
            p.drawRect(QRectF(1, top + 1, right - 2, RowTall - 3));
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
            p.setBrush(Qt::NoBrush);
            p.drawPath(tick);
        }
        const QColor ink = entry.enabled ? (here || is_marked ? style.ink : Faded(style.ink, 0.86)) : Faded(style.muted, 0.75);
        if (places.contains(entry.id)) {
            Put(p, style.mono_small, PlaceRight, base, QString::number(places.value(entry.id)), style.muted, 1);
        }
        const bool fresh = arrived.contains(entry.id);
        Put(p, style.row, TitleLeft, base, Fit(entry.title, style.row, kind_left - TitleLeft - (fresh ? 54 : 14)), ink);
        if (fresh) {
            Put(p, style.mono_small, kind_left - 12, base, QStringLiteral("NEW"), style.accent, 1);
        }
        Put(p, style.mono_small, kind_left, base, KindName(kinds.value(entry.id)).toUpper(), entry.enabled ? style.muted : Faded(style.muted, 0.6));
        Put(p, style.mono_small, entries_right, base, Count(entry.terms + entry.metas + entry.kanji), entry.enabled ? style.ink : Faded(style.muted, 0.7), 1);
        Put(p, style.mono_small, pictures_right, base, Count(entry.media), entry.enabled ? style.ink : Faded(style.muted, 0.7), 1);
        // On or off: a small switch, which a click flips.
        const QRectF pill(switch_left, top + 6, 28, 13);
        p.setPen(Qt::NoPen);
        p.setBrush(entry.enabled ? style.accent : Faded(style.muted, 0.35));
        p.drawRoundedRect(pill, 6.5, 6.5);
        p.setBrush(entry.enabled ? style.ground : style.ink);
        p.drawEllipse(QPointF(entry.enabled ? pill.right() - 6.5 : pill.left() + 6.5, pill.center().y()), 4.5, 4.5);
        p.fillRect(QRectF(0, top + RowTall - 1, right, 1), Faded(style.rule, 0.45));
    }
    if (dragging && drop_gap >= 0) {
        const double y = ListTop() + (drop_gap - first) * RowTall - 1;
        p.fillRect(QRectF(0, y - 1, right, 3), style.accent);
    }
    p.restore();
    if (shown.isEmpty()) {
        const bool none_installed = others == 0 && kind < 0 && state == AnyState && search->text().trimmed().isEmpty();
        Put(p, style.row, TitleLeft, ListTop() + 30,
            none_installed ? (work.busy ? QStringLiteral("Dictionaries will appear here as they are imported.")
                                        : QStringLiteral("No dictionaries installed yet. Import Yomitan .zip files or download Jitendex."))
                           : QStringLiteral("No matching dictionaries"),
            style.muted);
    }
    // Where the list stands in its rows.
    if (shown.size() > rows) {
        const QRectF track(W - 3, ListTop(), 2, rows * RowTall);
        p.fillRect(track, Faded(style.rule, 0.6));
        p.fillRect(QRectF(track.left() - 0.5, track.top() + track.height() * first / shown.size(), 3,
                          track.height() * rows / shown.size()),
                   style.muted);
    }

    // The foot: what is marked and what can be done to it, or the work in progress; the counts.
    const double foot = H - FootTall + 10;
    p.fillRect(QRectF(0, foot - 6, right, 1), style.rule);
    {
        const QString counts = QStringLiteral("%1 · %2 ON · %3 SHOWN")
                                   .arg(others == 1 ? QStringLiteral("1 DICTIONARY") : QStringLiteral("%1 DICTIONARIES").arg(others))
                                   .arg(on_count)
                                   .arg(shown.size());
        Put(p, style.mono_small, right, foot + 36, counts, style.muted, 1);
    }
    if (work.busy) {
        // The banner above the list says it all; the foot waits.
        Put(p, style.mono_small, 0, foot + 16, QStringLiteral("EDITING IS DISABLED WHILE THIS FINISHES"), style.muted);
    } else if (converting) {
        const DictionaryConversion conv = shelf->Conversion();
        Put(p, style.mono_small, 0, foot + 16,
            conv.total > 0 ? QStringLiteral("UPDATING DICTIONARIES TO NEW FORMAT · %1 OF %2 · LIST WILL UPDATE WHEN DONE")
                                 .arg(conv.done)
                                 .arg(conv.total)
                           : QStringLiteral("UPDATING DICTIONARIES TO NEW FORMAT · LIST WILL UPDATE WHEN DONE"),
            style.muted);
    } else {
        const QList<qint64> target = Target();
        const int n = static_cast<int>(target.size());
        const bool have = n > 0;
        double bx = right;
        if (moving) {
            // Moving: what the keys do, and the two ways out.
            double mx = button(right, foot, QStringLiteral("CANCEL"), false, true, [this] { EndMove(false); }) - 4;
            mx = button(mx, foot, QStringLiteral("DONE"), true, true, [this] { EndMove(true); }) - 12;
            Put(p, style.mono_small, 0, foot + 16,
                Fit(QStringLiteral("MOVING %1 · ↑ ↓ MOVE · PGUP/PGDN JUMP 10 · HOME/END TOP/BOTTOM").arg(moving_ids.size()),
                    style.mono_small, mx),
                style.accent);
            return;
        }
        bx = button(bx, foot, QStringLiteral("DELETE…"), false, have, [this] { AskDelete(Target()); }) - 6;
        bx = button(bx, foot, QStringLiteral("BOTTOM"), false, have, [this] { Move(Target(), 2); }) - 4;
        bx = button(bx, foot, QStringLiteral("TOP"), false, have, [this] { Move(Target(), -2); }) - 4;
        bx = button(bx, foot, QStringLiteral("MOVE…"), false, have, [this] { BeginMove(); }) - 6;
        bx = button(bx, foot, QStringLiteral("OFF"), false, have, [this] { Switch(Target(), false); }) - 4;
        bx = button(bx, foot, QStringLiteral("ON"), false, have, [this] { Switch(Target(), true); }) - 12;
        QString said;
        if (!marked.isEmpty()) {
            // Marks a filter hides still count: said, so that nothing is changed unseen.
            int hidden = static_cast<int>(marked.size());
            for (int i : shown) {
                hidden -= marked.contains(all[i].id) ? 1 : 0;
            }
            said = hidden > 0 ? QStringLiteral("%1 MARKED · %2 HIDDEN").arg(marked.size()).arg(hidden)
                              : QStringLiteral("%1 MARKED").arg(marked.size());
        } else if (const DictionaryEntry* here = EntryOf(current)) {
            said = here->title;
        }
        Put(p, marked.isEmpty() ? style.row : style.mono_small, 0, foot + 16, Fit(said, marked.isEmpty() ? style.row : style.mono_small, bx),
            marked.isEmpty() ? style.muted : style.accent);
        // The order changed: its way back, until the next change (Ctrl+Z too).
        if (!undo_order.isEmpty()) {
            const bool over = hover_hit == hits.size();
            const double lx = Put(p, style.mono_small, 0, foot + 36, undone ? QStringLiteral("ORDER RESTORED · ") : QStringLiteral("ORDER CHANGED · "), style.muted);
            const double wide = Put(p, style.mono_small, lx, foot + 36, undone ? QStringLiteral("REDO") : QStringLiteral("UNDO"), over ? style.ink : style.accent);
            p.fillRect(QRectF(lx, foot + 39, wide, 1), over ? style.ink : style.accent);
            hits.append({QRectF(lx - 4, foot + 24, wide + 8, 16), [this] { Undo(); }});
        }
    }
    if (problems_open && !shelf->Problems().isEmpty()) {
        // What could not be imported, each in full: over the list, until put away.
        const QStringList problems = shelf->Problems();
        const QRectF box(0, ListTop() - 2, right, Rows() * RowTall + 2);
        p.fillRect(box, style.raised.alpha() > 0 ? style.raised : style.ground);
        p.setPen(QPen(style.rule, 1));
        p.setBrush(QBrush(Qt::NoBrush));
        p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
        Put(p, style.head, 16, box.top() + 22, problems.size() == 1 ? QStringLiteral("1 DICTIONARY COULDN'T BE IMPORTED")
                                                                    : QStringLiteral("%1 DICTIONARIES COULDN'T BE IMPORTED").arg(problems.size()),
            style.muted);
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

void DictionaryPanel::mousePressEvent(QMouseEvent* event) {
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
    if (moving) {
        // Moving: the foot's two buttons, or a click anywhere else sets the rows down.
        for (const Hit& hit : hits) {
            if (hit.area.contains(at) && hit.act) {
                hit.act();
                return;
            }
        }
        EndMove(true);
        return;
    }
    for (const Hit& hit : hits) {
        if (hit.area.contains(at) && hit.act) {
            hit.act();
            update();
            return;
        }
    }
    if (problems_open && at.y() >= ListTop() - 2 && at.y() < ListTop() + Rows() * RowTall) {
        return; // the details' box is over the rows
    }
    const int row = RowAt(at.y());
    if (row < 0) {
        return;
    }
    const DictionaryEntry& entry = all[shown[row]];
    const double right = Width() - Bar;
    if (at.x() >= right - 40) {
        // The switch: this one, or every marked one with it.
        const QList<qint64> ids = marked.contains(entry.id) ? Target() : QList<qint64>{entry.id};
        current = entry.id;
        Switch(ids, !entry.enabled);
        return;
    }
    const Qt::KeyboardModifiers held = event->modifiers();
    if (at.x() < CheckWide || (held & Qt::ControlModifier)) {
        ToggleMark(entry.id);
        current = entry.id;
        Play(1);
    } else if (held & Qt::ShiftModifier) {
        SetCurrent(row, true);
    } else {
        // A plain click: this row in hand, and the start of a drag (of the marked ones, if it is one).
        press_at = at;
        press_id = entry.id;
        if (!marked.contains(entry.id)) {
            marked.clear();
        }
        current = entry.id;
        anchor = entry.id;
        Play(0);
    }
    update();
}

void DictionaryPanel::mouseMoveEvent(QMouseEvent* event) {
    const QPointF at = Sheet(event->position());
    if (press_id >= 0 && (event->buttons() & Qt::LeftButton) && !shelf->Work().busy && !shelf->Converting()) {
        if (!dragging && std::abs(at.y() - press_at.y()) > 6) {
            // With a search or a filter too: the rows go before the row they are dropped on.
            if (EntryOf(press_id) == nullptr || EntryOf(press_id)->names) {
                if (!drag_said && say) {
                    say(QStringLiteral("Names dictionaries are managed automatically and can't be reordered"));
                    drag_said = true;
                }
            } else {
                dragging = true;
            }
        }
        if (dragging) {
            const double rel = (at.y() - ListTop()) / RowTall;
            drop_gap = std::clamp(first + static_cast<int>(std::lround(rel)), 0, static_cast<int>(shown.size()));
            // Near the list's ends it scrolls.
            if (at.y() < ListTop() + 8 && first > 0) {
                --first;
            } else if (at.y() > ListTop() + Rows() * RowTall - 8 && first + Rows() < shown.size()) {
                ++first;
            }
            update();
            return;
        }
    }
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

void DictionaryPanel::mouseReleaseEvent(QMouseEvent*) {
    if (dragging && drop_gap >= 0) {
        QList<qint64> ids;
        if (marked.contains(press_id)) {
            for (int i : shown) {
                if (marked.contains(all[i].id) && !all[i].names) {
                    ids.append(all[i].id);
                }
            }
        }
        if (ids.isEmpty()) {
            ids.append(press_id);
        }
        DropAt(ids, drop_gap);
    } else if (press_id >= 0 && !dragging && marked.contains(press_id) && marked.size() > 1) {
        // A plain click on one of several marked rows, not dragged: that row alone, as in a list.
        marked = {press_id};
    }
    press_id = -1;
    dragging = false;
    drop_gap = -1;
    drag_said = false;
    update();
}

void DictionaryPanel::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || problems_open || moving) {
        return;
    }
    const QPointF at = Sheet(event->position());
    const double right = Width() - Bar;
    if (at.x() >= CheckWide && at.x() < right - 40 && RowAt(at.y()) >= 0) {
        Actions();
    }
}

void DictionaryPanel::leaveEvent(QEvent*) {
    hover_hit = -1;
    hover_row = -1;
    update();
}

void DictionaryPanel::wheelEvent(QWheelEvent* event) {
    const int steps = -event->angleDelta().y() / 40;
    first = std::clamp(first + (steps == 0 ? (event->angleDelta().y() < 0 ? 1 : -1) : steps), 0,
                       std::max(0, static_cast<int>(shown.size()) - Rows()));
    hover_row = RowAt(Sheet(event->position()).y());
    update();
}

DictionaryConversionStrip::DictionaryConversionStrip(QWidget* parent, DictionaryShelf* shelf_) : QWidget(parent), shelf(shelf_) {
    setCursor(Qt::PointingHandCursor); // a click brings the box that says the move back
    hide();
    parent->installEventFilter(this); // its size: the strip keeps to its foot
    ticks.setInterval(500);
    connect(&ticks, &QTimer::timeout, this, [this] {
        if (!shelf->Converting()) {
            // Done: the list is the engine's now.
            ticks.stop();
            hide();
            shelf->Refresh();
            return;
        }
        now = shelf->Conversion();
        if (suppressed) {
            if (isVisible()) {
                hide();
            }
            return;
        }
        if (!isVisible()) {
            Place();
            show();
        }
        raise();
        update();
    });
}

void DictionaryConversionStrip::SetSuppressed(bool hidden) {
    suppressed = hidden;
    if (suppressed) {
        hide();
    } else if (shelf->Converting()) {
        now = shelf->Conversion();
        Place();
        show();
        raise();
        update();
    }
}

void DictionaryConversionStrip::SetStyle(const DictionaryPanel::Style& style_) {
    style = style_;
    Place();
    update();
}

bool DictionaryConversionStrip::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        Place();
    }
    return QWidget::eventFilter(watched, event);
}

void DictionaryConversionStrip::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && clicked) {
        clicked();
    }
    event->accept();
}

void DictionaryConversionStrip::Watch() {
    if (shelf->Converting() && !ticks.isActive()) {
        ticks.start();
    }
}

void DictionaryConversionStrip::Place() {
    if (parentWidget() == nullptr) {
        return;
    }
    const int tall = std::max(24, static_cast<int>(std::lround(30 * style.scale)));
    setGeometry(0, parentWidget()->height() - tall, parentWidget()->width(), tall);
}

void DictionaryConversionStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), style.raised.alpha() > 0 ? style.raised : style.ground);
    p.fillRect(QRectF(0, 0, width(), 1), style.rule);
    if (now.total > 0) {
        const double part = now.steps > 0 ? std::clamp(static_cast<double>(now.step) / now.steps, 0.0, 1.0) : 0.0;
        const double share = std::clamp((now.done + part) / now.total, 0.0, 1.0);
        p.fillRect(QRectF(0, 0, width() * share, 2), style.accent);
    }
    p.scale(style.scale, style.scale);
    const double wide = width() / std::max(0.01, style.scale);
    QString said = QStringLiteral("UPDATING DICTIONARIES");
    if (now.total > 0) {
        said += QStringLiteral(" · %1 OF %2").arg(now.done).arg(now.total);
    }
    const double left = Put(p, style.mono_small, 40, 19, said, style.ink) + 52;
    if (!now.title.isEmpty()) {
        Put(p, style.row, left, 20, Fit(now.title, style.row, wide - left - 260), style.muted);
    }
    Put(p, style.mono_small, wide - 40, 19, QStringLiteral("LOOKUPS KEEP WORKING MEANWHILE"), style.muted, 1);
}

} // namespace Home

