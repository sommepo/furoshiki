// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Drawing what a theme says (look.h) as a browser draws its CSS: backgrounds of gradient
// layers, boxes with their borders and shadows (outer ones only outside the box, inset ones
// inside its borders), text with its shadows (the first on top), the cursor's orb, and the
// fuzz over a finished picture. Lengths are on the 1280x720 sheet; a painter scaled to the
// screen draws them at the screen's own pixels.

#include <QFont>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include "look.h"

namespace Home::Paint {

/// A Gaussian blur of an image in place (three box blurs), `sigma` in its pixels.
void Blur(QImage& image, double sigma);

/// A background in a box.
void PaintFill(QPainter& p, const QRectF& box, const Fill& fill);

/// The borders of a box: their widths (top, right, bottom, left) and colour.
struct Borders {
    double top{}, right{}, bottom{}, left{};
    QColor colour;
};

/// A box as CSS draws one: its outer shadows outside it, its background, its inset shadows
/// inside its borders, its borders.
void PaintBox(QPainter& p, const QRectF& box, const Fill& background, const QList<Shadow>& shadows,
              const Borders& borders);

/// A shape of any outline (rounded corners) as CSS draws a box without borders: its outer
/// shadows outside it, its background, its inset shadows inside it.
void PaintShape(QPainter& p, const QPainterPath& shape, const Fill& background, const QList<Shadow>& shadows);

/// A font from the program's fonts (":/fonts/<file>") at a type's size and spacing.
[[nodiscard]] QFont FontOf(const TypeSpec& type);
/// The height of a font's line as a browser's "normal".
[[nodiscard]] double NormalLine(const QFont& font);
/// Where the baseline goes for a line of this font centred on `centre_y`.
[[nodiscard]] double CentredBaseline(const QFont& font, double centre_y);
/// Where the baseline goes in a line box of a height that starts at `top` (0: normal).
[[nodiscard]] double BoxBaseline(const QFont& font, double top, double line = 0);

/// Text on a baseline with its shadows under it (the first on top); returns its width.
/// `align` 0 from x, 1 ending at x, 2 centred on x. Blurred shadows are kept for reuse.
double PutText(QPainter& p, const QFont& font, QPointF at, const QString& text, const QColor& colour,
                const QList<Shadow>& shadows, int align = 0);
/// Text of a font family drawn with its pixel steps rounded off (a pixel font made larger):
/// `strength` is the blur in dots of the font (its size / 16) before the edge is sharpened
/// again; 0 draws it as it is.
void SetSmoothing(const QString& family, double strength);
/// The text cut to a width with "…".
[[nodiscard]] QString Fit(const QString& text, const QFont& font, double width);
/// Lets go of the shadows kept (a theme was changed).
void ForgetShadows();

/// The cursor's orb (three rings, red, green and blue, round a white dot, each turning in
/// depth) with a theme's glow, at `phase` of its loop of `OrbPhases`, at `scale` of the
/// screen; kept once made.
[[nodiscard]] QImage OrbSprite(const QList<Shadow>& glow, double device_scale, int phase = 0);
/// The pictures in one loop of the orb's turning.
inline constexpr int OrbPhases = 48;
/// The orb sprite's size on the sheet at a scale of 1 (its glow's margin included).
inline constexpr double OrbBox = 40;

/// The fuzz of a picture made at `device_scale` pixels to a sheet pixel: red and blue shifted
/// apart, a softness, a contrast, grain and fine interlace lines. `origin` is where the
/// picture's top left stands in the whole frame (the grain and the lines line up across pieces).
struct FuzzSpec {
    FuzzLevel level;
    double grain{};
    double grain_frequency{0.9};
    double interlace{};
};
void ApplyFuzz(QImage& image, const FuzzSpec& spec, double device_scale, QPoint origin);

} // namespace Home::Paint
