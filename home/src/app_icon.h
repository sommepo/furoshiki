#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QString>

namespace Home {

/// One colouring of Furoshiki's icon (icon/icon.json, built in as :/icon/icon.json).
struct IconColours {
    QString key;
    QString name;
    QColor ground;
    QColor ink;
};

/// All of them, in the file's order.
[[nodiscard]] const QList<IconColours>& IconChoices();
/// The one kept in QSettings (`look/icon`), else the file's default.
[[nodiscard]] QString ChosenIcon();
/// Keeps a choice and shows it on the program's windows at once.
void ChooseIcon(const QString& key);
/// Gives the program's windows the chosen icon: at the start, and after a choice.
void ApplyChosenIcon();
/// What the choice is called: "30 Solarized".
[[nodiscard]] QString IconLabel(const QString& key);
/// The icon in a colouring at one size, and at all the sizes a window asks for.
[[nodiscard]] QImage IconImage(const QString& key, int size);
[[nodiscard]] QIcon IconPicture(const QString& key);

} // namespace Home
