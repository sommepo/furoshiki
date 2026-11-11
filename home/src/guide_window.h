// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QListWidget;

namespace Home {

/// Where a playthrough guide stands, as Furochibi's service says it (GET /api/guide).
struct GuidePlace {
    struct Step {
        QString id;
        QString text;
        QString note;
        QString group;
        bool done{};
    };
    QString slug;
    QString title;
    QString section;
    QString sub;
    QList<Step> steps; ///< The section's, in order
    int done{};
    int total{};

    /// The service's answer read; an empty slug when it is none.
    [[nodiscard]] static GuidePlace Read(const QByteArray& json);
    /// A step as a row says it: its text on one line, its note after it.
    [[nodiscard]] static QString Line(const Step& step);
};

/**
 * The guide of the game being played, over the game: the section its checklist stands at (the
 * one open last on any device) with its steps, each ticked here as on the guide's own page and
 * in Furochibi, since all three keep their ticks in the guides' sync service. Asked of
 * Furochibi's service (services.h, "furochibi"), by the game's ID: the title's record names its
 * guide there. The dev line's.
 */
class GuideWindow : public QDialog {
public:
    GuideWindow(QWidget* parent, const QString& service, const QString& game_id);

private:
    void Ask();
    void Show(const GuidePlace& place);
    void Tick(int row);

    QString service;
    QString game_id;
    GuidePlace place;
    QLabel* head{};
    QLabel* count{};
    QListWidget* list{};
    bool filling{};
};

} // namespace Home
