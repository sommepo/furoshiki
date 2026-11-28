// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QThreadPool>
#include <QWheelEvent>
#include "game_overlays.h"
#include "reader/qt/reader_dialogs.h"
#include "reader/qt/reader_overlay.h"

namespace Home {

namespace {

constexpr qint64 OpenTime = 190, CloseTime = 120;

double EaseOut(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return 1 - std::pow(1 - t, 3);
}

QColor WithAlpha(QColor colour, double alpha) {
    colour.setAlphaF(std::clamp(alpha, 0.0, 1.0) * colour.alphaF());
    return colour;
}

QPixmap CaptureBlurred(QWidget* over) {
    if (!over || over->width() <= 0 || over->height() <= 0) {
        return {};
    }
    const QPixmap shot = over->grab();
    if (shot.isNull()) {
        return {};
    }
    const QImage small = shot.toImage().scaled(std::max(1, shot.width() / 8), std::max(1, shot.height() / 8),
                                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(small.scaled(shot.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

} // namespace

// ============================================================================
// GameBacklogOverlay
// ============================================================================

GameBacklogOverlay* GameBacklogOverlay::Open(QWidget* over, const ChoicePicker::Colours& colours,
                                             std::function<void(const ReaderApp::PresentedLine&)> on_card,
                                             std::function<void(const ReaderApp::PresentedLine&)> on_explain) {
    auto* overlay = new GameBacklogOverlay(over, colours, std::move(on_card), std::move(on_explain));
    overlay->show();
    overlay->raise();
    overlay->setFocus(Qt::PopupFocusReason);
    return overlay;
}

GameBacklogOverlay::GameBacklogOverlay(QWidget* over, const ChoicePicker::Colours& colours_,
                                       std::function<void(const ReaderApp::PresentedLine&)> on_card_,
                                       std::function<void(const ReaderApp::PresentedLine&)> on_explain_)
    : QWidget(over), colours(colours_), on_card(std::move(on_card_)), on_explain(std::move(on_explain_)) {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    jp_font = QFont(QStringLiteral("Zen Kaku Gothic New"), static_cast<int>(std::lround(24 * s)), QFont::Medium);
    jp_font.setFamilies(ReaderUi::Style::JapaneseFamilies());
    jp_font.setPixelSize(std::max(18, static_cast<int>(std::lround(27 * s))));

    speaker_font = jp_font;
    speaker_font.setPixelSize(std::max(13, static_cast<int>(std::lround(15 * s))));
    speaker_font.setWeight(QFont::DemiBold);

    meta_font = QFont(QStringLiteral("Chakra Petch"), static_cast<int>(std::lround(11 * s)), QFont::Medium);
    meta_font.setPixelSize(std::max(10, static_cast<int>(std::lround(12 * s))));
    meta_font.setLetterSpacing(QFont::PercentageSpacing, 112);

    setObjectName(QStringLiteral("game-backlog-overlay"));
    setGeometry(over->rect());
    setMouseTracking(true);
    // Its own pointer, the reader's: before the game's first line the game's window shows none,
    // and the backlog was read without one (the maintainer, 2026-11-27).
    setCursor(ReaderUi::Style::PointerCursor(ReaderUi::Style::PointerSize(over), over->devicePixelRatioF()));
    setFocusPolicy(Qt::StrongFocus);
    lookups = std::make_unique<ReaderUi::LookupFlow>(over->window() ? over->window() : over);
    lookups->mark = [this](qint64 id, int first, int last) {
        mark_id = id;
        mark_first = first;
        mark_last = last;
        update();
    };
    lookups->notify = [this](const QString& text) { Notify(text); };
    Blur();
    LoadLines();
    frames.setInterval(16);
    connect(&frames, &QTimer::timeout, this, [this] {
        const qint64 t = clock.elapsed();
        if (leaving && t - left_at >= CloseTime) {
            frames.stop();
            if (lookups) {
                lookups->Close();
            }
            const bool had_focus = hasFocus();
            hide();
            if (had_focus) {
                if (QWidget* w = parentWidget()) {
                    w->setFocus();
                }
            }
            deleteLater();
            return;
        }
        if (std::fabs(scroll_y - scroll_target) > 0.5) {
            scroll_y += (scroll_target - scroll_y) * 0.28;
        } else {
            scroll_y = scroll_target;
        }
        if (!notice.isEmpty() && t >= notice_until) {
            notice.clear();
        }
        update();
    });
    clock.start();
    frames.start();
}

void GameBacklogOverlay::Blur() {
    behind = CaptureBlurred(parentWidget());
}

double GameBacklogOverlay::MetaColWidth() const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    return 172.0 * s;
}

double GameBacklogOverlay::TextWidth() const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    return std::max(160.0, ListRect().width() - MetaColWidth() - 16.0 * s);
}

void GameBacklogOverlay::LayoutText(QTextLayout& layout, int index, double text_width) const {
    layout.setFont(jp_font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setAlignment(Qt::AlignLeft);
    layout.setTextOption(opt);
    layout.setText(entries[index].line.text);
    const double leading = std::max(4.0, jp_font.pixelSize() * 0.22);
    double y = 0.0;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(text_width);
        line.setPosition(QPointF(0.0, y));
        y += line.height() + leading;
    }
    layout.endLayout();
}

void GameBacklogOverlay::LoadLines(qint64 keep_id) {
    auto& session = ReaderUi::ReaderSession::Instance();
    game_title = session.GameTitle();
    entries.clear();
    const QString game = session.GameKey();
    if (!game.isEmpty()) {
        try {
            QList<ReaderApp::BacklogEntry> recent = session.Database().RecentLines(game, 200);
            // Reverse so oldest is at top and newest is at bottom (like shelf Log).
            std::reverse(recent.begin(), recent.end());
            entries = std::move(recent);
        } catch (...) {
        }
    }
    chosen = std::max(0, static_cast<int>(entries.size()) - 1);
    if (keep_id > 0) {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].id == keep_id) {
                chosen = i;
                break;
            }
        }
    }
    words_row = -1;
    words.clear();
    word_at = -1;
    const double text_w = TextWidth();
    row_tops.clear();
    row_heights.clear();
    double y = 0;
    for (int i = 0; i < entries.size(); ++i) {
        row_tops.append(y);
        const double h = RowHeight(i, text_w);
        row_heights.append(h);
        y += h;
    }
    total_height = y;
    EnsureVisible(chosen);
    scroll_y = scroll_target;
}

QRectF GameBacklogOverlay::Card() const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    const double w = std::min(width() - 48.0 * s, std::max(720.0 * s, width() * 0.88));
    const double h = std::min(height() - 48.0 * s, std::max(480.0 * s, height() * 0.86));
    return QRectF((width() - w) * 0.5, (height() - h) * 0.5, w, h);
}

QRectF GameBacklogOverlay::ListRect() const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    const QRectF c = Card();
    return QRectF(c.left() + 20 * s, c.top() + 62 * s, c.width() - 40 * s, c.height() - 118 * s);
}

double GameBacklogOverlay::RowHeight(int index, double text_width) const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    QTextLayout layout;
    LayoutText(layout, index, text_width);
    const double text_h = layout.boundingRect().height();
    return std::max(76.0 * s, text_h + 28.0 * s);
}

int GameBacklogOverlay::CharOffsetAt(int index, const QPointF& pt, double row_y, double text_x, double text_w) const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    QTextLayout layout;
    LayoutText(layout, index, text_w);
    const double lx = pt.x() - text_x;
    const double ly = pt.y() - (row_y + 12.0 * s);
    for (int i = 0; i < layout.lineCount(); ++i) {
        const QTextLine line = layout.lineAt(i);
        const QRectF r = line.naturalTextRect();
        if (ly >= r.top() - 4.0 && ly <= r.bottom() + 6.0) {
            if (lx < r.left() - 6.0 || lx > r.right() + 6.0) {
                return -1;
            }
            const int cursor = line.xToCursor(lx, QTextLine::CursorOnCharacter);
            if (cursor >= 0 && cursor < entries[index].line.text.size()) {
                return cursor;
            }
            return -1;
        }
    }
    return -1;
}

QRectF GameBacklogOverlay::SpanRect(int index, int first, int last, double row_y, double text_x, double text_w) const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    QTextLayout layout;
    LayoutText(layout, index, text_w);
    const QString& text = entries[index].line.text;
    if (text.isEmpty() || first < 0 || first >= text.size()) {
        return QRectF(text_x, row_y + 12.0 * s, 40.0 * s, jp_font.pixelSize() + 6.0);
    }
    const int end = std::clamp(last, first + 1, static_cast<int>(text.size()));
    const QTextLine line = layout.lineForTextPosition(first);
    if (!line.isValid()) {
        return QRectF(text_x, row_y + 12.0 * s, 40.0 * s, jp_font.pixelSize() + 6.0);
    }
    const double x1 = line.cursorToX(first);
    const int line_end = std::min(end, line.textStart() + line.textLength());
    const double x2 = line.cursorToX(line_end);
    return QRectF(text_x + std::min(x1, x2), row_y + 12.0 * s + line.y(),
                  std::max(12.0, std::fabs(x2 - x1)), line.height());
}

void GameBacklogOverlay::BuildWordsForChosen() {
    if (chosen < 0 || chosen >= entries.size()) {
        words_row = -1;
        words.clear();
        word_at = -1;
        return;
    }
    if (words_row == chosen) {
        return;
    }
    words_row = chosen;
    word_at = -1;
    const QString& text = entries[chosen].line.text;
    auto& session = ReaderUi::ReaderSession::Instance();
    const bool any = session.Dictionaries().HasEnabledDictionaries();
    auto& lookup = session.Lookup();
    const QString game = session.GameKey();
    words = ReaderApp::WordWalk::Words(text, [any, &lookup, &text, &game](int offset) {
        if (!any) {
            return 0;
        }
        const auto res = lookup.Lookup(text.mid(offset, ReaderApp::DictionaryLookup::MaxScanLength), 1, game);
        return res.isEmpty() ? 0 : static_cast<int>(res.first().matched_text.size());
    });
}

void GameBacklogOverlay::LookupAt(int index, int offset, bool scroll) {
    if (!lookups || index < 0 || index >= entries.size()) {
        return;
    }
    chosen = index;
    if (scroll) {
        EnsureVisible(chosen);
        scroll_y = scroll_target;
    }
    const QRectF lr = ListRect();
    const double text_x = lr.left() + MetaColWidth();
    const double text_w = TextWidth();
    const double ry = lr.top() + row_tops[index] - scroll_y;
    const QRectF wr = SpanRect(index, offset, offset + 1, ry, text_x, text_w);
    ReaderUi::LookupFlow::Anchor anchor;
    anchor.bounds = rect();
    anchor.keep_clear = wr.toAlignedRect();
    anchor.near_x = static_cast<int>(std::lround(wr.left()));
    anchor.owner = this;
    lookups->Lookup(PresentedFor(index), offset, anchor);
    update();
}

void GameBacklogOverlay::ToggleCounted(int index) {
    if (index < 0 || index >= entries.size()) {
        return;
    }
    auto& session = ReaderUi::ReaderSession::Instance();
    auto& e = entries[index];
    const bool next = !e.counted;
    try {
        if (session.Database().ToggleLineCounted(e.id, next)) {
            e.counted = next;
            e.characters = next ? ReaderApp::ReadingRules::CountCharacters(e.line.text) : 0;
            Notify(next ? QStringLiteral("Line marked as counted for stats")
                        : QStringLiteral("Line marked as uncounted for stats"));
            update();
        } else {
            Notify(QStringLiteral("No countable characters in this line"));
        }
    } catch (...) {
        Notify(QStringLiteral("Could not update line stats"));
    }
}

void GameBacklogOverlay::ClearDuplicates() {
    auto& session = ReaderUi::ReaderSession::Instance();
    const QString game = session.GameKey();
    if (game.isEmpty()) {
        return;
    }
    if (lookups && lookups->IsOpen()) {
        lookups->Close();
    }
    const qint64 keep_id = (chosen >= 0 && chosen < entries.size()) ? entries[chosen].id : -1;
    try {
        const int removed = session.Database().ClearDuplicateLines(game);
        LoadLines(keep_id);
        if (removed > 0) {
            Notify(removed == 1 ? QStringLiteral("Removed 1 duplicate line")
                                : QStringLiteral("Removed %1 duplicate lines").arg(removed));
        } else {
            Notify(QStringLiteral("No duplicate lines found"));
        }
    } catch (...) {
        Notify(QStringLiteral("Could not clear duplicates"));
    }
}

void GameBacklogOverlay::EnsureVisible(int index) {
    if (entries.isEmpty() || index < 0 || index >= row_tops.size()) {
        return;
    }
    const QRectF lr = ListRect();
    const double top = row_tops[index];
    const double bot = top + row_heights[index];
    const double max_scroll = std::max(0.0, total_height - lr.height());
    if (top < scroll_target + 16) {
        scroll_target = std::clamp(top - 16, 0.0, max_scroll);
    } else if (bot > scroll_target + lr.height() - 16) {
        scroll_target = std::clamp(bot - lr.height() + 16, 0.0, max_scroll);
    }
}

void GameBacklogOverlay::Notify(const QString& text) {
    notice = text;
    notice_until = clock.elapsed() + 2400;
    update();
}

ReaderApp::PresentedLine GameBacklogOverlay::PresentedFor(int index) const {
    ReaderApp::PresentedLine out;
    if (index >= 0 && index < entries.size()) {
        const auto& e = entries[index];
        out.line = e.line;
        out.accounting.characters = e.characters;
        out.accounting.reread = e.reread;
        out.accounting.counted = e.counted;
        out.backlog_id = e.id;
        out.first_backlog_id = e.id;
    }
    return out;
}

ReaderApp::PresentedLine GameBacklogOverlay::SelectedPresented() const {
    return PresentedFor(chosen);
}

void GameBacklogOverlay::Leave() {
    if (lookups && lookups->IsOpen()) {
        lookups->Close();
    }
    if (!leaving) {
        leaving = true;
        left_at = clock.elapsed();
    }
}

bool GameBacklogOverlay::Key(int key) {
    if (leaving) {
        return true;
    }
    if (lookups && lookups->IsOpen()) {
        using PK = ReaderUi::LookupFlow::PadKey;
        switch (key) {
        case Qt::Key_Escape:
        case Qt::Key_Backspace:
            lookups->Close();
            update();
            return true;
        case Qt::Key_Up:
            lookups->OnPad(PK::EntryBefore);
            return true;
        case Qt::Key_Down:
            lookups->OnPad(PK::EntryNext);
            return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
            // On the Kanji tab of a word of several characters they go along its characters, as
            // over the game (the maintainer, 2026-11-27); else to the word before or after.
            if (lookups->Popup() != nullptr && lookups->Popup()->KanjiRail()) {
                lookups->OnPad(key == Qt::Key_Right ? PK::KanjiNext : PK::KanjiBefore);
                return true;
            }
            if (chosen >= 0 && chosen < entries.size()) {
                BuildWordsForChosen();
                if (!words.isEmpty()) {
                    if (word_at < 0) {
                        word_at = (key == Qt::Key_Right) ? 0 : static_cast<int>(words.size()) - 1;
                    } else {
                        word_at = std::clamp(word_at + (key == Qt::Key_Right ? 1 : -1),
                                             0, static_cast<int>(words.size()) - 1);
                    }
                    LookupAt(chosen, words[word_at].first);
                }
            }
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            lookups->OnPad(PK::ToggleMore);
            return true;
        case Qt::Key_PageUp:
            lookups->OnPad(PK::TabBefore);
            return true;
        case Qt::Key_PageDown:
            lookups->OnPad(PK::TabNext);
            return true;
        case Qt::Key_Y:
            lookups->OnPad(PK::Card);
            return true;
        case Qt::Key_X:
            lookups->OnPad(PK::CardWithAudio);
            return true;
        default:
            return true;
        }
    }
    switch (key) {
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
    case Qt::Key_H:
        Leave();
        return true;
    case Qt::Key_Up:
        if (!entries.isEmpty()) {
            chosen = std::max(0, chosen - 1);
            words_row = -1;
            word_at = -1;
            EnsureVisible(chosen);
            update();
        }
        return true;
    case Qt::Key_Down:
        if (!entries.isEmpty()) {
            chosen = std::min(static_cast<int>(entries.size()) - 1, chosen + 1);
            words_row = -1;
            word_at = -1;
            EnsureVisible(chosen);
            update();
        }
        return true;
    case Qt::Key_PageUp:
        if (!entries.isEmpty()) {
            chosen = std::max(0, chosen - 6);
            words_row = -1;
            word_at = -1;
            EnsureVisible(chosen);
            update();
        }
        return true;
    case Qt::Key_PageDown:
        if (!entries.isEmpty()) {
            chosen = std::min(static_cast<int>(entries.size()) - 1, chosen + 6);
            words_row = -1;
            word_at = -1;
            EnsureVisible(chosen);
            update();
        }
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (chosen >= 0 && chosen < entries.size()) {
            BuildWordsForChosen();
            if (!words.isEmpty()) {
                if (word_at < 0) {
                    word_at = (key == Qt::Key_Right) ? 0 : static_cast<int>(words.size()) - 1;
                } else {
                    word_at = std::clamp(word_at + (key == Qt::Key_Right ? 1 : -1),
                                         0, static_cast<int>(words.size()) - 1);
                }
                LookupAt(chosen, words[word_at].first);
            }
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (chosen >= 0 && chosen < entries.size()) {
            BuildWordsForChosen();
            if (!words.isEmpty()) {
                if (word_at < 0 || word_at >= words.size()) {
                    word_at = 0;
                }
                LookupAt(chosen, words[word_at].first);
            } else {
                QApplication::clipboard()->setText(entries[chosen].line.text);
                Notify(QStringLiteral("Copied line to clipboard"));
            }
        }
        return true;
    case Qt::Key_Y:
    case Qt::Key_C:
        ToggleCounted(chosen);
        return true;
    case Qt::Key_Delete:
        ClearDuplicates();
        return true;
    case Qt::Key_D:
        if (chosen >= 0 && chosen < entries.size() && on_card) {
            on_card(SelectedPresented());
            Notify(QStringLiteral("Sending card to Anki…"));
        }
        return true;
    case Qt::Key_X:
    case Qt::Key_E:
        if (chosen >= 0 && chosen < entries.size() && on_explain) {
            const auto presented = SelectedPresented();
            on_explain(presented);
        }
        return true;
    default:
        return false;
    }
}

void GameBacklogOverlay::paintEvent(QPaintEvent*) {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    const qint64 elapsed = clock.elapsed();
    const double alpha = leaving ? 1.0 - EaseOut(static_cast<double>(elapsed - left_at) / CloseTime)
                                 : EaseOut(static_cast<double>(elapsed) / OpenTime);
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setOpacity(alpha);
    if (colours.blur && !behind.isNull()) {
        p.drawPixmap(rect(), behind);
    }
    p.fillRect(rect(), QColor(0, 0, 0, colours.blur ? 120 : 155));

    const QRectF c = Card();
    const double rad = 12.0 * s;
    p.setPen(QPen(colours.rule, 1));
    p.setBrush(colours.card);
    p.drawRoundedRect(c.adjusted(0.5, 0.5, -0.5, -0.5), rad, rad);
    p.setBrush(Qt::NoBrush);

    btn_hits.clear();
    count_hits.clear();

    // Header
    p.setFont(colours.title);
    p.setPen(colours.txt);
    const QString head = game_title.isEmpty() ? QStringLiteral("BACKLOG")
                                              : QStringLiteral("BACKLOG  ·  ") + game_title.toUpper();
    p.drawText(QRectF(c.left() + 24 * s, c.top() + 14 * s, c.width() - 340 * s, 34 * s),
               Qt::AlignLeft | Qt::AlignVCenter, head);

    p.setFont(meta_font);
    const QFontMetricsF mfm(meta_font);
    // Clear duplicates button in header
    const QString clear_label = QStringLiteral("SELECT  CLEAR DUPLICATES");
    const double clear_w = mfm.horizontalAdvance(clear_label) + 18 * s;
    const QRectF clear_r(c.right() - 24 * s - clear_w, c.top() + 17 * s, clear_w, 28 * s);
    p.setPen(QPen(colours.rule, 1));
    p.drawRoundedRect(clear_r, 6.0 * s, 6.0 * s);
    p.setPen(colours.txt);
    p.drawText(clear_r, Qt::AlignCenter, clear_label);
    btn_hits.append({clear_r, Qt::Key_Delete});

    p.setPen(colours.dim);
    const QString count_txt = entries.isEmpty()
                                  ? QStringLiteral("EMPTY")
                                  : QStringLiteral("%1 / %2").arg(chosen + 1).arg(entries.size());
    p.drawText(QRectF(clear_r.left() - 120 * s, c.top() + 14 * s, 106 * s, 34 * s),
               Qt::AlignRight | Qt::AlignVCenter, count_txt);
    p.fillRect(QRectF(c.left() + 20 * s, c.top() + 54 * s, c.width() - 40 * s, 1), colours.rule);

    // Scrollable lines list
    const QRectF lr = ListRect();
    const double meta_col = MetaColWidth();
    const double text_w = TextWidth();
    p.save();
    p.setClipRect(lr);
    if (entries.isEmpty()) {
        p.setFont(jp_font);
        p.setPen(colours.dim);
        p.drawText(lr, Qt::AlignCenter, QStringLiteral("No lines recorded yet"));
    } else {
        for (int i = 0; i < entries.size(); ++i) {
            const double ry = lr.top() + row_tops[i] - scroll_y;
            const double rh = row_heights[i];
            if (ry + rh < lr.top() || ry > lr.bottom()) {
                continue;
            }
            const bool is_chosen = (i == chosen);
            const bool is_hover = (i == hover);
            if (is_chosen) {
                QLinearGradient wash(lr.left(), ry, lr.right(), ry);
                wash.setColorAt(0.0, WithAlpha(colours.accent, 0.16));
                wash.setColorAt(0.7, WithAlpha(colours.accent, 0.05));
                wash.setColorAt(1.0, WithAlpha(colours.accent, 0.0));
                p.fillRect(QRectF(lr.left(), ry, lr.width(), rh - 1), wash);
                p.fillRect(QRectF(lr.left(), ry + 4 * s, 3.0 * s, rh - 8 * s), colours.accent);
            } else if (is_hover) {
                p.fillRect(QRectF(lr.left(), ry, lr.width(), rh - 1), WithAlpha(colours.txt, 0.04));
            }
            const auto& e = entries[i];
            const QString when = QDateTime::fromMSecsSinceEpoch(e.line.timestamp).toString(QStringLiteral("HH:mm"));
            p.setFont(meta_font);
            p.setPen(is_chosen ? colours.txt : colours.dim);
            p.drawText(QRectF(lr.left() + 14 * s, ry + 10 * s, meta_col - 22 * s, 18 * s),
                       Qt::AlignLeft | Qt::AlignTop, when);
            double badge_y = ry + 30 * s;
            if (!e.line.speaker.isEmpty()) {
                p.setFont(speaker_font);
                p.setPen(is_chosen ? colours.accent : WithAlpha(colours.accent, 0.82));
                const QString sp = QFontMetricsF(speaker_font).elidedText(e.line.speaker, Qt::ElideRight, meta_col - 22 * s);
                p.drawText(QRectF(lr.left() + 14 * s, ry + 28 * s, meta_col - 22 * s, 22 * s),
                           Qt::AlignLeft | Qt::AlignTop, sp);
                badge_y = ry + 52 * s;
            }
            // Counted / Uncounted status badge (clickable + toggleable with Y)
            const QString badge = e.counted ? QStringLiteral("COUNTED") : QStringLiteral("UNCOUNTED");
            p.setFont(meta_font);
            const double bw = mfm.horizontalAdvance(badge) + 12 * s;
            const QRectF br(lr.left() + 14 * s, badge_y, bw, 19 * s);
            if (e.counted) {
                p.setBrush(WithAlpha(colours.accent, 0.18));
                p.setPen(QPen(WithAlpha(colours.accent, 0.65), 1));
                p.drawRoundedRect(br, 4.0 * s, 4.0 * s);
                p.setBrush(Qt::NoBrush);
                p.setPen(colours.accent);
            } else {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(WithAlpha(colours.dim, 0.5), 1));
                p.drawRoundedRect(br, 4.0 * s, 4.0 * s);
                p.setPen(colours.dim);
            }
            p.drawText(br, Qt::AlignCenter, badge);
            count_hits.append({br, i});

            // Japanese text via QTextLayout with optional word highlight
            QTextLayout layout;
            LayoutText(layout, i, text_w);
            const double tx = lr.left() + meta_col;
            const double ty = ry + 12.0 * s;
            if (mark_id == e.id && mark_first >= 0 && mark_last > mark_first) {
                const QRectF hr = SpanRect(i, mark_first, mark_last, ry, tx, text_w);
                p.setBrush(WithAlpha(colours.accent, 0.28));
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(hr.adjusted(-2, -1, 2, 1), 4.0 * s, 4.0 * s);
                p.setBrush(Qt::NoBrush);
            } else if (is_chosen && words_row == i && word_at >= 0 && word_at < words.size()) {
                const auto& sp = words[word_at];
                const QRectF hr = SpanRect(i, sp.first, sp.last, ry, tx, text_w);
                p.setBrush(WithAlpha(colours.accent, 0.22));
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(hr.adjusted(-2, -1, 2, 1), 4.0 * s, 4.0 * s);
                p.setBrush(Qt::NoBrush);
            }
            p.setPen(is_chosen ? colours.txt : WithAlpha(colours.txt, 0.78));
            layout.draw(&p, QPointF(tx, ty));
            p.fillRect(QRectF(lr.left(), ry + rh - 1, lr.width(), 1), WithAlpha(colours.rule, 0.5));
        }
    }
    p.restore();

    // Scrollbar indicator
    if (total_height > lr.height() + 1) {
        const double track_x = lr.right() + 6 * s;
        const double frac = scroll_y / std::max(1.0, total_height - lr.height());
        const double thumb_h = std::max(24.0 * s, lr.height() * (lr.height() / total_height));
        const double thumb_y = lr.top() + frac * (lr.height() - thumb_h);
        p.fillRect(QRectF(track_x, lr.top(), 2.0 * s, lr.height()), WithAlpha(colours.rule, 0.6));
        p.fillRect(QRectF(track_x, thumb_y, 2.5 * s, thumb_h), colours.accent);
    }

    // Footer bar + clickable/controller hints
    const double foot_y = c.bottom() - 48 * s;
    p.fillRect(QRectF(c.left() + 20 * s, foot_y, c.width() - 40 * s, 1), colours.rule);
    if (!notice.isEmpty()) {
        p.setFont(meta_font);
        p.setPen(colours.accent);
        p.drawText(QRectF(c.left() + 24 * s, foot_y + 8 * s, c.width() - 48 * s, 32 * s),
                   Qt::AlignLeft | Qt::AlignVCenter, notice);
    } else if (lookups && lookups->IsOpen()) {
        p.setFont(meta_font);
        p.setPen(colours.dim);
        p.drawText(QRectF(c.left() + 24 * s, foot_y + 8 * s, 420 * s, 32 * s),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("↑ / ↓ ENTRY  ·  L1 / R1 TAB  ·  Y CARD  ·  B BACK"));
    } else {
        p.setFont(meta_font);
        p.setPen(colours.dim);
        p.drawText(QRectF(c.left() + 24 * s, foot_y + 8 * s, 360 * s, 32 * s),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("↑ / ↓ LINE  ·  ← / → WORD  ·  L1 / R1 PAGE"));
    }

    struct Hint {
        QString label;
        int key;
    };
    QList<Hint> hints;
    hints.append({QStringLiteral("A  LOOK UP"), Qt::Key_Return});
    const bool sel_counted = (chosen >= 0 && chosen < entries.size()) && entries[chosen].counted;
    hints.append({sel_counted ? QStringLiteral("Y  UNCOUNT") : QStringLiteral("Y  COUNT"), Qt::Key_Y});
    if (on_explain) {
        hints.append({QStringLiteral("X  EXPLAIN"), Qt::Key_X});
    }
    hints.append({QStringLiteral("B  CLOSE"), Qt::Key_Escape});
    p.setFont(meta_font);
    double rx = c.right() - 24 * s;
    for (int i = static_cast<int>(hints.size()) - 1; i >= 0; --i) {
        const double bw = mfm.horizontalAdvance(hints[i].label) + 18 * s;
        const QRectF br(rx - bw, foot_y + 10 * s, bw, 28 * s);
        p.setPen(QPen(colours.rule, 1));
        p.drawRoundedRect(br, 6.0 * s, 6.0 * s);
        p.setPen(colours.txt);
        p.drawText(br, Qt::AlignCenter, hints[i].label);
        btn_hits.append({br, hints[i].key});
        rx -= bw + 10 * s;
    }
}

void GameBacklogOverlay::mouseMoveEvent(QMouseEvent* event) {
    const QRectF lr = ListRect();
    hover = -1;
    int offset = -1;
    if (lr.contains(event->position())) {
        const double y = event->position().y() - lr.top() + scroll_y;
        for (int i = 0; i < row_tops.size(); ++i) {
            if (y >= row_tops[i] && y < row_tops[i] + row_heights[i]) {
                hover = i;
                offset = CharOffsetAt(i, event->position(), lr.top() + row_tops[i] - scroll_y, lr.left() + MetaColWidth(), TextWidth());
                break;
            }
        }
    }
    // A word under the pointer is looked up at once, as over the game (the maintainer,
    // 2026-11-27: "Backlog shouldnt need click only hover"), as the reader's hover setting says:
    // always, with Shift held, or never (a click still does).
    const QString hover_lookup = ReaderUi::ReaderSession::Instance().Settings().HoverLookup();
    const bool wanted = hover_lookup == QStringLiteral("always") ||
                        (hover_lookup == QStringLiteral("shift") && (event->modifiers() & Qt::ShiftModifier) != 0);
    if (lookups && wanted && hover >= 0 && offset >= 0 && event->buttons() == Qt::NoButton) {
        if (!lookups->IsAbout(entries[hover].id, offset)) {
            words_row = -1;
            word_at = -1;
            LookupAt(hover, offset, false);
        }
        lookups->PointerMoved(true);
    } else if (lookups && lookups->IsOpen()) {
        // Off the words a popup that is open stays, unless the reader's settings close it then.
        lookups->PointerMoved(false);
    }
    update();
}

void GameBacklogOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPointF pt = event->position();
    if (!Card().contains(pt)) {
        if (lookups && lookups->IsOpen()) {
            lookups->Close();
            update();
            return;
        }
        Leave();
        return;
    }
    for (const auto& b : btn_hits) {
        if (b.rect.contains(pt)) {
            Key(b.key);
            return;
        }
    }
    for (const auto& ch : count_hits) {
        if (ch.rect.contains(pt)) {
            chosen = ch.row;
            ToggleCounted(ch.row);
            return;
        }
    }
    const QRectF lr = ListRect();
    if (lr.contains(pt)) {
        const double y = pt.y() - lr.top() + scroll_y;
        const double text_x = lr.left() + MetaColWidth();
        const double text_w = TextWidth();
        for (int i = 0; i < row_tops.size(); ++i) {
            if (y >= row_tops[i] && y < row_tops[i] + row_heights[i]) {
                const double ry = lr.top() + row_tops[i] - scroll_y;
                const int offset = CharOffsetAt(i, pt, ry, text_x, text_w);
                chosen = i;
                words_row = -1;
                word_at = -1;
                EnsureVisible(chosen);
                if (offset >= 0) {
                    LookupAt(i, offset);
                } else {
                    if (lookups && lookups->IsOpen()) {
                        lookups->Close();
                    }
                    update();
                }
                return;
            }
        }
    }
}

void GameBacklogOverlay::wheelEvent(QWheelEvent* event) {
    const QRectF lr = ListRect();
    const double max_scroll = std::max(0.0, total_height - lr.height());
    scroll_target = std::clamp(scroll_target - event->angleDelta().y() * 0.6, 0.0, max_scroll);
    update();
}

void GameBacklogOverlay::keyPressEvent(QKeyEvent* event) {
    if (!Key(event->key())) {
        QWidget::keyPressEvent(event);
    }
}

void GameBacklogOverlay::resizeEvent(QResizeEvent*) {
    const qint64 keep_id = (chosen >= 0 && chosen < entries.size()) ? entries[chosen].id : -1;
    LoadLines(keep_id);
    update();
}

// ============================================================================
// GameExplainOverlay
// ============================================================================

#ifndef READER_NO_EXPLAIN

GameExplainOverlay* GameExplainOverlay::Open(QWidget* over, const ChoicePicker::Colours& colours,
                                             const ReaderApp::PresentedLine& presented,
                                             std::function<void(const ReaderApp::PresentedLine&)> on_card) {
    auto* overlay = new GameExplainOverlay(over, colours, presented, std::move(on_card));
    overlay->show();
    overlay->raise();
    overlay->setFocus(Qt::PopupFocusReason);
    return overlay;
}

GameExplainOverlay::GameExplainOverlay(QWidget* over, const ChoicePicker::Colours& colours_,
                                       const ReaderApp::PresentedLine& presented_,
                                       std::function<void(const ReaderApp::PresentedLine&)> on_card_)
    : QWidget(over), colours(colours_), presented(presented_), on_card(std::move(on_card_)) {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    jp_font = QFont(QStringLiteral("Zen Kaku Gothic New"), static_cast<int>(std::lround(20 * s)), QFont::Medium);
    jp_font.setFamilies(ReaderUi::Style::JapaneseFamilies());
    jp_font.setPixelSize(std::max(15, static_cast<int>(std::lround(22 * s))));

    body_font = jp_font;
    body_font.setPixelSize(std::max(13, static_cast<int>(std::lround(16 * s))));
    body_font.setWeight(QFont::Normal);

    seg_font = jp_font;
    seg_font.setPixelSize(std::max(14, static_cast<int>(std::lround(18 * s))));
    seg_font.setWeight(QFont::DemiBold);

    meta_font = QFont(QStringLiteral("Chakra Petch"), static_cast<int>(std::lround(11 * s)), QFont::Medium);
    meta_font.setPixelSize(std::max(10, static_cast<int>(std::lround(12 * s))));
    meta_font.setLetterSpacing(QFont::PercentageSpacing, 112);

    setObjectName(QStringLiteral("game-explain-overlay"));
    setGeometry(over->rect());
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    Blur();
    Fetch(false);
    frames.setInterval(16);
    connect(&frames, &QTimer::timeout, this, [this] {
        const qint64 t = clock.elapsed();
        if (leaving && t - left_at >= CloseTime) {
            frames.stop();
            const bool had_focus = hasFocus();
            hide();
            if (had_focus) {
                if (QWidget* w = parentWidget()) {
                    w->setFocus();
                }
            }
            deleteLater();
            return;
        }
        if (std::fabs(scroll_y - scroll_target) > 0.5) {
            scroll_y += (scroll_target - scroll_y) * 0.28;
        } else {
            scroll_y = scroll_target;
        }
        if (!notice.isEmpty() && t >= notice_until) {
            notice.clear();
        }
        update();
    });
    clock.start();
    frames.start();
}

void GameExplainOverlay::Blur() {
    behind = CaptureBlurred(parentWidget());
}

void GameExplainOverlay::Fetch(bool force) {
    auto& session = ReaderUi::ReaderSession::Instance();
    const QString cache_file = ReaderUi::ReaderSession::DataDirectory() + QStringLiteral("/explanations.json");
    if (!force) {
        if (const auto kept = ReaderApp::Explain::Cache(cache_file).Of(presented.line.text)) {
            answer = *kept;
            has_answer = true;
            asking = false;
            update();
            return;
        }
    }
    asking = true;
    has_answer = false;
    scroll_y = scroll_target = 0;
    update();

    ReaderApp::Explain::Request request;
    request.line = presented.line.text;
    request.title = session.GameTitle();
    if (presented.backlog_id >= 0 && !session.GameKey().isEmpty()) {
        try {
            const qint64 before = presented.first_backlog_id >= 0 ? presented.first_backlog_id : presented.backlog_id;
            for (const ReaderApp::BacklogEntry& entry : session.Database().RecentLines(session.GameKey(), 3, before)) {
                request.before.prepend(entry.line.text);
            }
        } catch (...) {
        }
    }
    ReaderApp::Explain::Setup setup;
    setup.command = session.Settings().ExplainCommand();
    setup.model = session.Settings().ExplainModel();
    const QString service = ReaderUi::ExplainService() ? ReaderUi::ExplainService()() : QString();
    const QString game_id = session.GameKey();
    QPointer<GameExplainOverlay> alive(this);
    QThreadPool::globalInstance()->start([alive, request, setup, service, game_id, cache_file] {
        ReaderApp::Explain::Answer res;
        if (!service.isEmpty()) {
            res = ReaderApp::Explain::AskService(service, request, game_id);
        }
        if (service.isEmpty() || !res.error.isEmpty()) {
            const ReaderApp::Explain::Answer here = ReaderApp::Explain::Ask(setup, request);
            if (here.error.isEmpty() || service.isEmpty()) {
                res = here;
            }
        }
        ReaderApp::Explain::Cache(cache_file).Keep(request.line, res);
        if (alive) {
            QMetaObject::invokeMethod(
                alive.data(),
                [alive, res] {
                    if (!alive) {
                        return;
                    }
                    alive->answer = res;
                    alive->asking = false;
                    alive->has_answer = true;
                    alive->scroll_y = alive->scroll_target = 0;
                    alive->update();
                },
                Qt::QueuedConnection);
        }
    });
}

QRectF GameExplainOverlay::Card() const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    const double w = std::min(width() - 48.0 * s, std::max(680.0 * s, width() * 0.80));
    const double h = std::min(height() - 48.0 * s, std::max(460.0 * s, height() * 0.82));
    return QRectF((width() - w) * 0.5, (height() - h) * 0.5, w, h);
}

QRectF GameExplainOverlay::BodyRect() const {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    const QRectF c = Card();
    const QFontMetricsF jfm(jp_font);
    const double line_h = jfm.boundingRect(QRectF(0, 0, c.width() - 56 * s, 2000), Qt::TextWordWrap | Qt::AlignLeft, presented.line.text).height();
    const double source_bottom = c.top() + 66 * s + (presented.line.speaker.isEmpty() ? 0.0 : 22.0 * s) + std::max(32.0 * s, line_h) + 18.0 * s;
    return QRectF(c.left() + 24 * s, source_bottom, c.width() - 48 * s, std::max(80.0 * s, c.bottom() - 56 * s - source_bottom));
}

void GameExplainOverlay::Notify(const QString& text) {
    notice = text;
    notice_until = clock.elapsed() + 2400;
    update();
}

void GameExplainOverlay::Leave() {
    if (!leaving) {
        leaving = true;
        left_at = clock.elapsed();
    }
}

bool GameExplainOverlay::Key(int key) {
    if (leaving) {
        return true;
    }
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    const QRectF br = BodyRect();
    const double max_scroll = std::max(0.0, content_height - br.height());
    switch (key) {
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
    case Qt::Key_E:
        Leave();
        return true;
    case Qt::Key_Up:
        scroll_target = std::clamp(scroll_target - 56.0 * s, 0.0, max_scroll);
        update();
        return true;
    case Qt::Key_Down:
        scroll_target = std::clamp(scroll_target + 56.0 * s, 0.0, max_scroll);
        update();
        return true;
    case Qt::Key_PageUp:
        scroll_target = std::clamp(scroll_target - br.height() * 0.75, 0.0, max_scroll);
        update();
        return true;
    case Qt::Key_PageDown:
        scroll_target = std::clamp(scroll_target + br.height() * 0.75, 0.0, max_scroll);
        update();
        return true;
    case Qt::Key_X:
    case Qt::Key_R:
        if (!asking) {
            Fetch(true);
        }
        return true;
    case Qt::Key_Y:
    case Qt::Key_D:
        if (has_answer && answer.error.isEmpty() && on_card) {
            on_card(presented);
            Notify(QStringLiteral("Sending card with translation…"));
        }
        return true;
    default:
        return false;
    }
}

void GameExplainOverlay::paintEvent(QPaintEvent*) {
    const double s = colours.scale > 0 ? colours.scale : 1.0;
    const qint64 elapsed = clock.elapsed();
    const double alpha = leaving ? 1.0 - EaseOut(static_cast<double>(elapsed - left_at) / CloseTime)
                                 : EaseOut(static_cast<double>(elapsed) / OpenTime);
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setOpacity(alpha);
    if (colours.blur && !behind.isNull()) {
        p.drawPixmap(rect(), behind);
    }
    p.fillRect(rect(), QColor(0, 0, 0, colours.blur ? 120 : 155));

    const QRectF c = Card();
    const double rad = 12.0 * s;
    p.setPen(QPen(colours.rule, 1));
    p.setBrush(colours.card);
    p.drawRoundedRect(c.adjusted(0.5, 0.5, -0.5, -0.5), rad, rad);
    p.setBrush(Qt::NoBrush);

    // Header
    p.setFont(colours.title);
    p.setPen(colours.txt);
    p.drawText(QRectF(c.left() + 24 * s, c.top() + 14 * s, c.width() - 48 * s, 34 * s),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("EXPLAIN LINE"));
    p.fillRect(QRectF(c.left() + 20 * s, c.top() + 54 * s, c.width() - 40 * s, 1), colours.rule);

    // Source line band
    double sy = c.top() + 66 * s;
    if (!presented.line.speaker.isEmpty()) {
        p.setFont(meta_font);
        p.setPen(colours.accent);
        p.drawText(QRectF(c.left() + 24 * s, sy, c.width() - 48 * s, 20 * s), Qt::AlignLeft | Qt::AlignTop, presented.line.speaker);
        sy += 22 * s;
    }
    p.setFont(jp_font);
    p.setPen(colours.txt);
    const QRectF line_br = QFontMetricsF(jp_font).boundingRect(QRectF(0, 0, c.width() - 56 * s, 2000), Qt::TextWordWrap | Qt::AlignLeft, presented.line.text);
    p.drawText(QRectF(c.left() + 24 * s, sy, c.width() - 56 * s, line_br.height() + 8 * s),
               Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, presented.line.text);

    const QRectF br = BodyRect();
    p.fillRect(QRectF(c.left() + 20 * s, br.top() - 10 * s, c.width() - 40 * s, 1), colours.rule);

    // Scrollable explanation body
    p.save();
    p.setClipRect(br);
    double y = 0;
    const double w = br.width() - 12 * s;
    if (asking) {
        p.setFont(body_font);
        p.setPen(colours.dim);
        p.drawText(br, Qt::AlignCenter, QStringLiteral("Asking…"));
        content_height = br.height();
    } else if (!answer.error.isEmpty()) {
        p.setFont(body_font);
        p.setPen(colours.accent);
        p.drawText(QRectF(br.left(), br.top() + 8 * s, w, br.height()), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, answer.error);
        content_height = br.height();
    } else if (has_answer) {
        const QFontMetricsF bfm(body_font);
        const QFontMetricsF sfm(seg_font);
        const QFontMetricsF mfm(meta_font);

        if (!answer.translation.isEmpty()) {
            p.setFont(meta_font);
            p.setPen(colours.accent);
            p.drawText(QRectF(br.left(), br.top() + y - scroll_y, w, 18 * s), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("TRANSLATION"));
            y += 22 * s;
            p.setFont(jp_font);
            p.setPen(colours.txt);
            const double th = QFontMetricsF(jp_font).boundingRect(QRectF(0, 0, w, 2000), Qt::TextWordWrap | Qt::AlignLeft, answer.translation).height();
            p.drawText(QRectF(br.left(), br.top() + y - scroll_y, w, th + 6 * s), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, answer.translation);
            y += th + 20 * s;
        }

        if (!answer.segments.isEmpty()) {
            p.setFont(meta_font);
            p.setPen(colours.accent);
            p.drawText(QRectF(br.left(), br.top() + y - scroll_y, w, 18 * s), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("BREAKDOWN"));
            y += 24 * s;
            for (const auto& seg : answer.segments) {
                const QString head_str = seg.reading.isEmpty() || seg.reading == seg.text
                                             ? seg.text
                                             : QStringLiteral("%1  (%2)").arg(seg.text, seg.reading);
                const double head_h = sfm.boundingRect(QRectF(0, 0, w - 16 * s, 2000), Qt::TextWordWrap | Qt::AlignLeft, head_str).height();
                const double gloss_h = seg.gloss.isEmpty() ? 0.0 : bfm.boundingRect(QRectF(0, 0, w - 16 * s, 2000), Qt::TextWordWrap | Qt::AlignLeft, seg.gloss).height();
                const QString gram_str = seg.grammar.join(QStringLiteral("  ·  "));
                const double gram_h = gram_str.isEmpty() ? 0.0 : mfm.boundingRect(QRectF(0, 0, w - 16 * s, 2000), Qt::TextWordWrap | Qt::AlignLeft, gram_str).height();
                const double box_h = 12 * s + head_h + (gloss_h > 0 ? gloss_h + 4 * s : 0) + (gram_h > 0 ? gram_h + 6 * s : 0) + 10 * s;

                const double draw_y = br.top() + y - scroll_y;
                if (draw_y + box_h >= br.top() && draw_y <= br.bottom()) {
                    p.fillRect(QRectF(br.left(), draw_y, w, box_h), WithAlpha(colours.txt, 0.04));
                    p.fillRect(QRectF(br.left(), draw_y, 2.5 * s, box_h), WithAlpha(colours.accent, 0.7));
                    double iy = draw_y + 8 * s;
                    p.setFont(seg_font);
                    p.setPen(colours.txt);
                    p.drawText(QRectF(br.left() + 12 * s, iy, w - 16 * s, head_h + 4 * s), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, head_str);
                    iy += head_h + 4 * s;
                    if (!seg.gloss.isEmpty()) {
                        p.setFont(body_font);
                        p.setPen(WithAlpha(colours.txt, 0.85));
                        p.drawText(QRectF(br.left() + 12 * s, iy, w - 16 * s, gloss_h + 4 * s), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, seg.gloss);
                        iy += gloss_h + 6 * s;
                    }
                    if (!gram_str.isEmpty()) {
                        p.setFont(meta_font);
                        p.setPen(colours.dim);
                        p.drawText(QRectF(br.left() + 12 * s, iy, w - 16 * s, gram_h + 4 * s), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, gram_str);
                    }
                }
                y += box_h + 8 * s;
            }
            y += 8 * s;
        }

        if (!answer.note.isEmpty()) {
            p.setFont(meta_font);
            p.setPen(colours.accent);
            p.drawText(QRectF(br.left(), br.top() + y - scroll_y, w, 18 * s), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("NOTE"));
            y += 22 * s;
            p.setFont(body_font);
            p.setPen(WithAlpha(colours.txt, 0.88));
            const double nh = bfm.boundingRect(QRectF(0, 0, w, 2000), Qt::TextWordWrap | Qt::AlignLeft, answer.note).height();
            p.drawText(QRectF(br.left(), br.top() + y - scroll_y, w, nh + 6 * s), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, answer.note);
            y += nh + 16 * s;
        }
        content_height = y;
    }
    p.restore();

    if (content_height > br.height() + 1) {
        const double track_x = br.right() + 6 * s;
        const double frac = scroll_y / std::max(1.0, content_height - br.height());
        const double thumb_h = std::max(24.0 * s, br.height() * (br.height() / content_height));
        const double thumb_y = br.top() + frac * (br.height() - thumb_h);
        p.fillRect(QRectF(track_x, br.top(), 2.0 * s, br.height()), WithAlpha(colours.rule, 0.6));
        p.fillRect(QRectF(track_x, thumb_y, 2.5 * s, thumb_h), colours.accent);
    }

    // Footer
    const double foot_y = c.bottom() - 48 * s;
    p.fillRect(QRectF(c.left() + 20 * s, foot_y, c.width() - 40 * s, 1), colours.rule);
    btn_hits.clear();
    if (!notice.isEmpty()) {
        p.setFont(meta_font);
        p.setPen(colours.accent);
        p.drawText(QRectF(c.left() + 24 * s, foot_y + 8 * s, c.width() - 48 * s, 32 * s),
                   Qt::AlignLeft | Qt::AlignVCenter, notice);
    } else {
        p.setFont(meta_font);
        p.setPen(colours.dim);
        p.drawText(QRectF(c.left() + 24 * s, foot_y + 8 * s, 260 * s, 32 * s),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("↑ / ↓ SCROLL  ·  L1 / R1 PAGE"));
    }

    struct Hint {
        QString label;
        int key;
    };
    QList<Hint> hints;
    if (!asking) {
        hints.append({QStringLiteral("X / R  ASK AGAIN"), Qt::Key_X});
    }
    if (has_answer && answer.error.isEmpty() && on_card) {
        hints.append({QStringLiteral("Y / D  ADD CARD"), Qt::Key_Y});
    }
    hints.append({QStringLiteral("B / ESC  CLOSE"), Qt::Key_Escape});
    p.setFont(meta_font);
    const QFontMetricsF mfm(meta_font);
    double rx = c.right() - 24 * s;
    for (int i = static_cast<int>(hints.size()) - 1; i >= 0; --i) {
        const double bw = mfm.horizontalAdvance(hints[i].label) + 18 * s;
        const QRectF br_hit(rx - bw, foot_y + 10 * s, bw, 28 * s);
        p.setPen(QPen(colours.rule, 1));
        p.drawRoundedRect(br_hit, 6.0 * s, 6.0 * s);
        p.setPen(colours.txt);
        p.drawText(br_hit, Qt::AlignCenter, hints[i].label);
        btn_hits.append({br_hit, hints[i].key});
        rx -= bw + 10 * s;
    }
}

void GameExplainOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPointF pt = event->position();
    if (!Card().contains(pt)) {
        Leave();
        return;
    }
    for (const auto& b : btn_hits) {
        if (b.rect.contains(pt)) {
            Key(b.key);
            return;
        }
    }
}

void GameExplainOverlay::wheelEvent(QWheelEvent* event) {
    const QRectF br = BodyRect();
    const double max_scroll = std::max(0.0, content_height - br.height());
    scroll_target = std::clamp(scroll_target - event->angleDelta().y() * 0.6, 0.0, max_scroll);
    update();
}

void GameExplainOverlay::keyPressEvent(QKeyEvent* event) {
    if (!Key(event->key())) {
        QWidget::keyPressEvent(event);
    }
}

void GameExplainOverlay::resizeEvent(QResizeEvent*) {
    update();
}

#endif // READER_NO_EXPLAIN

} // namespace Home
