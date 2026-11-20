// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QHash>
#include "pad_shortcuts.h"

namespace Home::PadShortcuts {

QStringList Rows() {
    return {QStringLiteral("furoshiki_read"),       QStringLiteral("reader_backlog"),      QStringLiteral("reader_pause_stats"),
            QStringLiteral("reader_pause_game"),    QStringLiteral("furoshiki_text_panel"), QStringLiteral("reader_menu"),
            QStringLiteral("reader_sentence_card"), QStringLiteral("furoshiki_ocr_read")};
}

QString Conflict(const QString& input) {
    // As PollPads has them while reading with the d-pad and in the popup (main_window.cpp).
    static const QHash<QString, QString> uses{
        {QStringLiteral("up"), QStringLiteral("moves the reading cursor")},
        {QStringLiteral("down"), QStringLiteral("moves the reading cursor")},
        {QStringLiteral("left"), QStringLiteral("moves the reading cursor")},
        {QStringLiteral("right"), QStringLiteral("moves the reading cursor")},
        {QStringLiteral("l1"), QStringLiteral("goes to the next word not in Anki while reading")},
        {QStringLiteral("r1"), QStringLiteral("goes to the next word not in Anki while reading")},
        {QStringLiteral("north"), QStringLiteral("makes a card while reading")},
        {QStringLiteral("east"), QStringLiteral("makes a card with audio while reading")},
        {QStringLiteral("west"), QStringLiteral("opens the popup while reading")},
        {QStringLiteral("south"), QStringLiteral("goes on in the game while reading")},
        {QStringLiteral("start"), QStringLiteral("is the game's Start")},
    };
    return input.contains(QLatin1Char('+')) ? QString() : uses.value(input);
}

QString ReadStartOf(const QString& input) {
    QStringList parts = input.split(QLatin1Char('+'));
    std::sort(parts.begin(), parts.end());
    const QString sorted = parts.join(QLatin1Char('+'));
    static const QHash<QString, QString> starts{
        {QStringLiteral("select"), QStringLiteral("select")},       {QStringLiteral("l3"), QStringLiteral("l3")},
        {QStringLiteral("r3"), QStringLiteral("r3")},               {QStringLiteral("select+south"), QStringLiteral("select+a")},
        {QStringLiteral("l3+r3"), QStringLiteral("l3+r3")},         {QStringLiteral("l1+r1"), QStringLiteral("lb+rb")},
    };
    return starts.value(sorted);
}

bool Assign(const ControllerMap::Store& store, const QString& action, const QString& input,
            const std::function<void(const QString&)>& set_read_start) {
    const ControllerMap::Action* found = nullptr;
    for (const ControllerMap::Action& one : ControllerMap::Load().actions) {
        if (one.id == action) {
            found = &one;
        }
    }
    if (found == nullptr) {
        return false;
    }
    if (action == QStringLiteral("furoshiki_read")) {
        if (const QString start = ReadStartOf(input); !start.isEmpty()) {
            ControllerMap::Clear(store, *found, {});
            if (store.save) {
                store.save();
            }
            if (set_read_start) {
                set_read_start(start);
            }
            return true;
        }
    }
    return ControllerMap::Assign(store, *found, input, {});
}

} // namespace Home::PadShortcuts
