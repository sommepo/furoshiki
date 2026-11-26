// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A blurred copy of a picture, for the ground under a see-through popup where the system has no
// blur of its own (Linux: on Windows the compositor blurs what is behind the popup's window).

#include <algorithm>
#include <cmath>
#include <vector>
#include <QImage>

namespace ReaderApp {

/// `picture` blurred about as a Gaussian blur of `radius` pixels would (three box blurs, each
/// edge held at its own colour). The result is RGB32, as large as the picture; a picture with
/// nothing in it, or a radius under half a pixel, comes back as it is (in RGB32).
inline QImage Blurred(const QImage& picture, double radius) {
    QImage image = picture.convertToFormat(QImage::Format_RGB32);
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0 || radius < 0.5) {
        return image;
    }
    // Three boxes of this half-width have the Gaussian's spread (sigma = radius).
    const int half = std::max(1, static_cast<int>(std::lround(std::sqrt(radius * radius * 12.0 / 3.0 + 1.0) / 2.0 - 0.5)));
    std::vector<QRgb> line(static_cast<std::size_t>(std::max(width, height)));
    const auto box = [half, &line](QRgb* first, int count, std::ptrdiff_t step) {
        const int span = 2 * half + 1;
        int red = 0;
        int green = 0;
        int blue = 0;
        const auto at = [first, count, step](int index) { return first[std::clamp(index, 0, count - 1) * step]; };
        for (int i = -half; i <= half; ++i) {
            const QRgb pixel = at(i);
            red += qRed(pixel);
            green += qGreen(pixel);
            blue += qBlue(pixel);
        }
        for (int i = 0; i < count; ++i) {
            line[static_cast<std::size_t>(i)] = qRgb(red / span, green / span, blue / span);
            const QRgb out = at(i - half);
            const QRgb in = at(i + half + 1);
            red += qRed(in) - qRed(out);
            green += qGreen(in) - qGreen(out);
            blue += qBlue(in) - qBlue(out);
        }
        for (int i = 0; i < count; ++i) {
            first[i * step] = line[static_cast<std::size_t>(i)];
        }
    };
    const std::ptrdiff_t stride = image.bytesPerLine() / static_cast<std::ptrdiff_t>(sizeof(QRgb));
    auto* pixels = reinterpret_cast<QRgb*>(image.bits());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < height; ++y) {
            box(pixels + y * stride, width, 1);
        }
        for (int x = 0; x < width; ++x) {
            box(pixels + x, height, stride);
        }
    }
    return image;
}

} // namespace ReaderApp
