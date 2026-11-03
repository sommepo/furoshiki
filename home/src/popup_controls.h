// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The popup's look set with controls instead of typed CSS (the maintainer: "options to change
// text size, colours, border widths with settings instead of typing CSS"). What the controls
// say is one block of the user's CSS file, made here and read back from it:
//
//   /* furoshiki-controls {"headword":"30","background":"#ff2b2119"} */
//   ... the rules, !important so that they win over the theme's ...
//   /* furoshiki-controls end */
//
// The rest of the file is left as it is; a control left at "the look's own" writes nothing.

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Home::PopupControls {

struct Control {
    enum Kind { Size, Colour, Font };
    QString key;
    QString label;
    QString group; ///< "Text", "Colours", "Box"
    Kind kind;
    int low = 0; ///< A size's range, in the popup's pixels at 100 %
    int high = 0;
};

/// Every control, in the order they are shown.
[[nodiscard]] const QList<Control>& Controls();
/// The font choices (Font): key and name.
[[nodiscard]] const QList<QPair<QString, QString>>& Fonts();

/// Key to value: a size as a number, a colour as #AARRGGBB, a font as its key.
using Values = QMap<QString, QString>;

/// The block for `values` (nothing when they are empty).
[[nodiscard]] QString Block(const Values& values);
/// What the block in `css` says, or nothing when it has none.
[[nodiscard]] Values Read(const QString& css);
/// `css` with its block made from `values`: replaced where it was, else added at the end;
/// taken out when `values` are empty.
[[nodiscard]] QString With(const QString& css, const Values& values);

} // namespace Home::PopupControls
