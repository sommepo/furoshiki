// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
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

} // namespace Home
