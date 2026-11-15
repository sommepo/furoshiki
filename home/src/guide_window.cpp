// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include "guide_window.h"
#include "services.h"

namespace Home {

GuidePlace GuidePlace::Read(const QByteArray& json) {
    GuidePlace place;
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    place.slug = object.value(QStringLiteral("slug")).toString();
    place.title = object.value(QStringLiteral("title")).toString();
    const QJsonObject section = object.value(QStringLiteral("section")).toObject();
    place.section = section.value(QStringLiteral("title")).toString();
    place.sub = section.value(QStringLiteral("sub")).toString();
    place.done = object.value(QStringLiteral("done")).toInt();
    place.total = object.value(QStringLiteral("total")).toInt();
    for (const QJsonValue& value : object.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject each = value.toObject();
        Step step{each.value(QStringLiteral("id")).toString(), each.value(QStringLiteral("text")).toString(),
                  each.value(QStringLiteral("note")).toString(), each.value(QStringLiteral("group")).toString(),
                  each.value(QStringLiteral("done")).toBool()};
        if (!step.id.isEmpty()) {
            place.steps.append(step);
        }
    }
    return place;
}

QString GuidePlace::Line(const Step& step) {
    QString text = step.text.simplified();
    if (!step.note.simplified().isEmpty()) {
        text += QStringLiteral("\n") + step.note.simplified();
    }
    return text;
}

GuideWindow::GuideWindow(QWidget* parent, const QString& service_, const QString& game_id_)
    : QDialog(parent), service(service_), game_id(game_id_) {
    setWindowTitle(QStringLiteral("Guide"));
    resize(720, 560);
    auto* layout = new QVBoxLayout(this);
    head = new QLabel(QStringLiteral("Asking…"));
    QFont large = head->font();
    large.setPointSizeF(large.pointSizeF() * 1.3);
    head->setFont(large);
    head->setWordWrap(true);
    count = new QLabel;
    list = new QListWidget;
    list->setWordWrap(true);
    list->setAlternatingRowColors(true);
    auto* close = new QPushButton(QStringLiteral("Back to the game"));
    layout->addWidget(head);
    layout->addWidget(count);
    layout->addWidget(list, 1);
    layout->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!filling) {
            Tick(list->row(item));
        }
    });
    // Enter or a controller's A on a step ticks it, as a click on its box does.
    connect(list, &QListWidget::itemActivated, this, [](QListWidgetItem* item) {
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });
    Ask();
}

void GuideWindow::Ask() {
    const QString address = service + QStringLiteral("/api/guide?game_id=") + QString::fromLatin1(QUrl::toPercentEncoding(game_id));
    Services::Ask(this, address, QByteArray(), QString(), 20, [this](const Services::Answer& answer) {
        const GuidePlace read = answer.Ok() ? GuidePlace::Read(answer.body) : GuidePlace{};
        if (read.slug.isEmpty()) {
            head->setText(answer.Ok() ? QStringLiteral("No guide for this game")
                                      : QStringLiteral("No guide for this game, or its service did not answer"));
            count->setText(answer.error);
            list->clear();
            return;
        }
        Show(read);
    });
}

void GuideWindow::Show(const GuidePlace& read) {
    place = read;
    head->setText(place.sub.isEmpty() ? place.section : place.section + QStringLiteral("  ·  ") + place.sub);
    count->setText(QStringLiteral("%1  ·  %2 of %3 steps done").arg(place.title).arg(place.done).arg(place.total));
    filling = true;
    list->clear();
    int first_open = -1;
    for (int i = 0; i < place.steps.size(); ++i) {
        const GuidePlace::Step& step = place.steps[i];
        auto* item = new QListWidgetItem(GuidePlace::Line(step), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(step.done ? Qt::Checked : Qt::Unchecked);
        if (!step.done && first_open < 0) {
            first_open = i;
        }
    }
    filling = false;
    // At the next step to do.
    list->setCurrentRow(std::max(first_open, 0));
    list->setFocus();
}

void GuideWindow::Tick(int row) {
    if (row < 0 || row >= place.steps.size()) {
        return;
    }
    const bool done = list->item(row)->checkState() == Qt::Checked;
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("id"), place.steps[row].id}, {QStringLiteral("done"), done}})
                                .toJson(QJsonDocument::Compact);
    const QString address = service + QStringLiteral("/api/guide/") + QString::fromLatin1(QUrl::toPercentEncoding(place.slug)) +
                            QStringLiteral("/tick");
    Services::Ask(this, address, body, QString(), 20, [this](const Services::Answer& answer) {
        if (!answer.Ok()) {
            count->setText(QStringLiteral("The tick was not kept: %1").arg(answer.error));
        }
        // Where the checklist stands now (the section may be done, and the next one comes).
        Ask();
    });
}

GuideOverlay::GuideOverlay() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    again = new QTimer(this);
    again->setInterval(30000);
    connect(again, &QTimer::timeout, this, [this] { Ask(); });
    // Where the game's window is now: it may move, or go full screen.
    follow = new QTimer(this);
    follow->setInterval(400);
    connect(follow, &QTimer::timeout, this, [this] { Place(); });
}

void GuideOverlay::Toggle(QWidget* over_, const QString& service_, const QString& game_id_) {
    if (isVisible() && game_id_ == game_id) {
        Close();
        return;
    }
    over = over_;
    service = service_;
    game_id = game_id_;
    if (service.isEmpty()) {
        place = {};
        said = QStringLiteral("No guide service is known to this device (Settings, Furoshiki, Home server services)");
    } else if (place.slug.isEmpty()) {
        said = QStringLiteral("Asking for the guide…");
    }
    Place();
    show();
    raise();
    again->start();
    follow->start();
    Ask();
}

void GuideOverlay::Close() {
    again->stop();
    follow->stop();
    hide();
}

void GuideOverlay::Ask() {
    if (service.isEmpty() || game_id.isEmpty() || asking) {
        return;
    }
    asking = true;
    const QString address = service + QStringLiteral("/api/guide?game_id=") + QString::fromLatin1(QUrl::toPercentEncoding(game_id));
    Services::Ask(this, address, QByteArray(), QString(), 20, [this](const Services::Answer& answer) {
        asking = false;
        const GuidePlace read = answer.Ok() ? GuidePlace::Read(answer.body) : GuidePlace{};
        place = read;
        said = !read.slug.isEmpty() ? QString()
               : answer.Ok()        ? QStringLiteral("No guide for this game")
                                    : QStringLiteral("The guide service did not answer");
        Place();
        update();
    });
}

int GuideOverlay::NextStep() const {
    for (int i = 0; i < place.steps.size(); ++i) {
        if (!place.steps[i].done) {
            return i;
        }
    }
    return -1;
}

void GuideOverlay::Done() {
    const int at = NextStep();
    if (!isVisible() || at < 0 || place.slug.isEmpty()) {
        return;
    }
    // Ticked here at once (the next step shows), then kept by the service and asked again.
    place.steps[at].done = true;
    ++place.done;
    update();
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("id"), place.steps[at].id}, {QStringLiteral("done"), true}})
                                .toJson(QJsonDocument::Compact);
    const QString address = service + QStringLiteral("/api/guide/") + QString::fromLatin1(QUrl::toPercentEncoding(place.slug)) +
                            QStringLiteral("/tick");
    Services::Ask(this, address, body, QString(), 20, [this](const Services::Answer& answer) {
        if (!answer.Ok()) {
            said = QStringLiteral("The tick was not kept: %1").arg(answer.error);
        }
        Ask();
    });
}

void GuideOverlay::Place() {
    if (over.isNull() || !over->isVisible() || over->window()->isMinimized()) {
        hide();
        return;
    }
    // In the top left corner of the game, a third of its width (at least 340 px, at most 560).
    const QRect game(over->mapToGlobal(QPoint(0, 0)), over->size());
    const int width = std::clamp(game.width() / 3, 340, 560);
    QFont text = font();
    text.setPointSizeF(13.5);
    const int step = NextStep();
    const QString shown = step >= 0 ? GuidePlace::Line(place.steps[step]) : said;
    const QString first = step >= 0 ? place.steps[step].text.simplified() : shown;
    const QRect room = QFontMetrics(text).boundingRect(QRect(0, 0, width - 36, 2000), Qt::TextWordWrap, first.isEmpty() ? QStringLiteral(" ") : first);
    QFont quiet = font();
    quiet.setPointSizeF(11);
    const QString note = step >= 0 ? place.steps[step].note.simplified() : QString();
    const int note_height = note.isEmpty() ? 0 : QFontMetrics(quiet).boundingRect(QRect(0, 0, width - 36, 2000), Qt::TextWordWrap, note).height() + 4;
    const int height = std::min(room.height() + note_height + 74, game.height() / 2);
    setGeometry(game.left() + 18, game.top() + 18, width, height);
    if (!isVisible() && again->isActive()) {
        show();
    }
}

void GuideOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // Furoshiki's Kon: indigo glass, cream type, a vermilion rule.
    const QColor ground(16, 26, 58, 225), ink(236, 229, 211), muted(138, 147, 181), accent(217, 71, 43);
    QPainterPath card;
    card.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    p.fillPath(card, ground);
    p.fillRect(QRectF(0, 12, 3, height() - 24), accent);
    QFont small = font();
    small.setPointSizeF(9);
    small.setLetterSpacing(QFont::PercentageSpacing, 106);
    QFont text = font();
    text.setPointSizeF(13.5);
    const int step = NextStep();
    const QRect inner = rect().adjusted(18, 12, -18, -10);
    // The guide and where it stands.
    QString head = place.slug.isEmpty() ? QStringLiteral("GUIDE") : (place.sub.isEmpty() ? place.section : place.section + QStringLiteral(" · ") + place.sub);
    p.setFont(small);
    p.setPen(muted);
    p.drawText(inner.left(), inner.top() + 12, QFontMetrics(small).elidedText(head.toUpper(), Qt::ElideRight, inner.width()));
    // The step to do, or what is known instead; its note under it, quieter.
    p.setFont(text);
    p.setPen(step >= 0 ? ink : muted);
    const QString shown = step >= 0 ? place.steps[step].text.simplified()
                          : !said.isEmpty() ? said
                                            : QStringLiteral("Every step of this section is done");
    QRect drawn;
    p.drawText(QRect(inner.left(), inner.top() + 22, inner.width(), inner.height() - 44), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, shown, &drawn);
    if (step >= 0 && !place.steps[step].note.simplified().isEmpty()) {
        QFont quiet = font();
        quiet.setPointSizeF(11);
        p.setFont(quiet);
        p.setPen(muted);
        p.drawText(QRect(inner.left(), drawn.bottom() + 4, inner.width(), inner.bottom() - drawn.bottom() - 22), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                   place.steps[step].note.simplified());
    }
    // How far, and the keys.
    p.setFont(small);
    p.setPen(muted);
    if (place.total > 0) {
        p.drawText(inner.left(), inner.bottom() - 2, QStringLiteral("%1 / %2").arg(place.done).arg(place.total));
    }
    const QString keys = step >= 0 ? QStringLiteral("CTRL+SHIFT+N DONE  ·  CTRL+SHIFT+G HIDE") : QStringLiteral("CTRL+SHIFT+G HIDE");
    p.drawText(QRect(inner.left(), inner.bottom() - 14, inner.width(), 16), Qt::AlignRight | Qt::AlignBottom, keys);
}

} // namespace Home
