// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QList>
#include <QString>
#include "controller_map.h"

/**
 * The reading shortcuts a controller's setup offers (pad_setup.h, its third step): one row each,
 * kept where the controller map keeps its Shortcuts (controller_map.h), for all consoles, so both
 * screens show the same. A row is learnt from one button or two held together.
 */
namespace Home::PadShortcuts {

/// The actions the setup lists, in order (their ids in controllers.json).
[[nodiscard]] QStringList Rows();

/// What controller reading or the popup already does with an input ("south", "l1"), in words
/// that follow "B " ("makes a card while reading"); empty where it is free. A chord is free.
[[nodiscard]] QString Conflict(const QString& input);

/// The "Start reading with" value an input or chord is ("select", "select+a", "lb+rb"), or empty
/// where it is none of those.
[[nodiscard]] QString ReadStartOf(const QString& input);

/// Puts a row's action on an input or a chord ("select+l3") for all consoles. "Start or stop
/// reading" on one of "Start reading with"'s own buttons sets that (set_read_start) and takes the
/// action off any other button. False where it cannot be (a held action on two buttons).
bool Assign(const ControllerMap::Store& store, const QString& action, const QString& input,
            const std::function<void(const QString&)>& set_read_start);

} // namespace Home::PadShortcuts
