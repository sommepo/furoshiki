// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QDialog>
#include <QPointer>
#include <QTimer>
#include <QWidget>
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

/**
 * The guide's next step over the game, small, in a corner: the first step of the section the
 * checklist stands at that is not ticked yet, with its note and how far the section is. Shown and
 * hidden by a key (Ctrl+Shift+G, or Select and L1); Ctrl+Shift+N (Select and R1) ticks the step
 * shown and the next one comes. Asked again every half minute while shown, so a tick on the phone
 * or the guide's page is seen here. Takes no pointer and no keyboard: the game keeps both.
 */
class GuideOverlay : public QWidget {
public:
    GuideOverlay();

    /// Shows the guide of `game_id` over `over` (the game's widget), or hides it if shown.
    void Toggle(QWidget* over, const QString& service, const QString& game_id);
    /// Ticks the step shown, if one is.
    void Done();
    void Close();
    [[nodiscard]] bool Shown() const {
        return isVisible();
    }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void Ask();
    void Place();
    [[nodiscard]] int NextStep() const;

    QPointer<QWidget> over;
    QString service;
    QString game_id;
    GuidePlace place;
    QString said; ///< While nothing is known: what to say instead
    bool asking{};
    QTimer* again{};
    QTimer* follow{};
};

} // namespace Home
