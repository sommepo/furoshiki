// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The window's look: its themes, each a file of values in `themes/` (built into the program
// as ":/themes/<name>.json"), and the few CSS forms those values are written in, so that a
// theme can take a Design board's values as they stand (AGENTS.md, "The look and its themes").
//
// What is read of CSS: colours (#rgb, #rrggbb, rgb(), rgba(), transparent); lengths in px, %
// and deg; linear-gradient, repeating-linear-gradient, radial-gradient (an ellipse at a point,
// to its farthest corner) and repeating-conic-gradient with a tile size ("0 0/2px 2px"), one
// layer or several after one another (the first on top); box-shadow and text-shadow lists
// ("inset dx dy blur spread colour"); and var(--name), a theme's own value by its name.

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

namespace Home {

/// One stop of a gradient: a colour and where it stands, in px or as a share of the
/// gradient's length (`percent`); a stop without a place has `place` < 0 (spread between its
/// neighbours, as CSS does).
struct Stop {
    QColor colour;
    double place{-1};
    bool percent{true};
};

/// One layer of a background.
struct Layer {
    enum Kind { Colour, Linear, Radial, Conic } kind{Colour};
    QColor colour;           ///< Colour
    double angle{180};       ///< Linear, conic: in degrees, CSS's (0 up, 90 right)
    QPointF at{0.5, 0.5};    ///< Radial: the centre as shares of the box
    bool repeating{};
    double tile_w{}, tile_h{}; ///< Conic: the tile's size in px (0: the box)
    QList<Stop> stops;
};

/// A background: layers, the first on top.
struct Fill {
    QList<Layer> layers;
    [[nodiscard]] bool Empty() const {
        return layers.isEmpty();
    }
};

/// A box-shadow or a text-shadow (a text-shadow has no inset or spread).
struct Shadow {
    bool inset{};
    double dx{}, dy{}, blur{}, spread{};
    QColor colour;
};

/// A kind of text: its font file (in `fonts/`), its size in px, its letter spacing in em, the
/// height of its line in px (0: the font's own), and whether a bold is made from a face that
/// has none (as a browser does when a weight of 700 meets a font of one weight).
struct TypeSpec {
    QString font;
    double size{16};
    double spacing{};
    double line{};
    bool bold{};
};

/// The fuzz at one strength: the red and blue taken so many px left and right, a softness
/// (a blur's radius in px) and a contrast.
struct FuzzLevel {
    double shift{};
    double blur{};
    double contrast{1};
};

[[nodiscard]] QColor ParseColour(const QString& text, bool* ok = nullptr);
/// A length in px ("12px", "12", ".5"), or a share for "%" when `percent` is given.
[[nodiscard]] double ParseLength(const QString& text, bool* percent = nullptr);
[[nodiscard]] Fill ParseFill(const QString& text);
[[nodiscard]] QList<Shadow> ParseShadows(const QString& text);
/// Splits at the commas that are not inside brackets.
[[nodiscard]] QStringList SplitTop(const QString& text, QChar separator = QLatin1Char(','));

/// A theme, as its file says it.
class Theme {
public:
    Theme() = default;
    Theme(QString key, QJsonObject values);

    [[nodiscard]] const QString& Key() const {
        return key;
    }
    [[nodiscard]] QString Name() const;
    [[nodiscard]] int Order() const;
    /// A value's text with every var(--name) in it replaced by that value.
    [[nodiscard]] QString Text(const QString& name) const;
    [[nodiscard]] bool Has(const QString& name) const;
    [[nodiscard]] QColor Colour(const QString& name, const QColor& otherwise = Qt::transparent) const;
    [[nodiscard]] Fill FillOf(const QString& name) const;
    [[nodiscard]] QList<Shadow> Shadows(const QString& name) const;
    [[nodiscard]] double Number(const QString& name, double otherwise = 0) const;
    /// An object's value ("clock", "layout", "fuzz"), and a number or text in one.
    [[nodiscard]] QJsonObject Object(const QString& name) const;
    [[nodiscard]] double Number(const QString& object, const QString& name, double otherwise) const;
    [[nodiscard]] QString Text(const QString& object, const QString& name) const;
    [[nodiscard]] TypeSpec Type(const QString& role) const;
    [[nodiscard]] FuzzLevel Fuzz(const QString& level) const;
    /// The fonts its types name (each once).
    [[nodiscard]] QStringList Fonts() const;
    /// What a theme's file must have and does not (empty: none missing); for the tests.
    [[nodiscard]] QStringList Missing() const;

private:
    QString key;
    QJsonObject values;
};

/// The themes in a folder (the program's own: ":/themes"), in their order.
[[nodiscard]] QList<Theme> LoadThemes(const QString& folder = QStringLiteral(":/themes"));

/// The roles a theme's "type" names, and the values every theme must have.
[[nodiscard]] QStringList TypeRoles();
[[nodiscard]] QStringList RequiredValues();

} // namespace Home
