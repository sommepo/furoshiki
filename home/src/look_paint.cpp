// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QRawFont>
#include <QSemaphore>
#include <QThreadPool>
#include "look_paint.h"

namespace Home::Paint {

namespace {

/// How many of the screen's pixels a sheet pixel is under this painter.
double ScaleOf(const QPainter& p) {
    const QTransform& t = p.worldTransform();
    return std::max(0.01, std::sqrt(std::fabs(t.m11() * t.m22() - t.m12() * t.m21())));
}

/// Work on an image's rows shared out among the processor's cores (a large picture only: the
/// page's fuzz is made on every change, and one core took a seventh of a second for it).
void ParallelRows(int rows, long long pixels, const std::function<void(int, int)>& work) {
    static QThreadPool* pool = [] {
        auto* made = new QThreadPool;
        made->setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 1, 1, 7));
        return made;
    }();
    const int parts = pixels < 160000 ? 1 : std::min(rows / 16, pool->maxThreadCount() + 1);
    if (parts <= 1) {
        work(0, rows);
        return;
    }
    const int chunk = (rows + parts - 1) / parts;
    QSemaphore done;
    int started = 0;
    for (int from = chunk; from < rows; from += chunk) {
        const int to = std::min(rows, from + chunk);
        pool->start([&work, &done, from, to] {
            work(from, to);
            done.release();
        });
        ++started;
    }
    work(0, std::min(rows, chunk));
    done.acquire(started);
}

// One pass of a box blur of radius r along rows (horizontal) or columns, on premultiplied
// ARGB, from `in` to `out`.
void BoxPass(const QImage& in, QImage& out, int r, bool horizontal) {
    const int w = in.width(), h = in.height();
    const int lines = horizontal ? h : w, length = horizontal ? w : h;
    const int window = 2 * r + 1;
    const auto at = [&](const QImage& image, int line, int i) -> const uchar* {
        return horizontal ? image.constScanLine(line) + i * 4 : image.constScanLine(i) + line * 4;
    };
    for (int line = 0; line < lines; ++line) {
        int sum[4] = {0, 0, 0, 0};
        // The edges stand for what is beyond them (clamped).
        for (int i = -r; i <= r; ++i) {
            const uchar* px = at(in, line, std::clamp(i, 0, length - 1));
            for (int c = 0; c < 4; ++c) {
                sum[c] += px[c];
            }
        }
        for (int i = 0; i < length; ++i) {
            uchar* dst = horizontal ? out.scanLine(line) + i * 4 : out.scanLine(i) + line * 4;
            for (int c = 0; c < 4; ++c) {
                dst[c] = static_cast<uchar>((sum[c] + window / 2) / window);
            }
            const uchar* add = at(in, line, std::min(i + r + 1, length - 1));
            const uchar* take = at(in, line, std::max(i - r, 0));
            for (int c = 0; c < 4; ++c) {
                sum[c] += add[c] - take[c];
            }
        }
    }
}

// A small Gaussian, straight, for a sigma under about two pixels (box blurs are too coarse).
void SmallGauss(QImage& image, double sigma) {
    const int r = std::max(1, static_cast<int>(std::ceil(sigma * 3)));
    std::vector<int> kernel(2 * r + 1);
    double total = 0;
    std::vector<double> raw(2 * r + 1);
    for (int i = -r; i <= r; ++i) {
        raw[i + r] = std::exp(-(i * i) / (2 * sigma * sigma));
        total += raw[i + r];
    }
    for (int i = 0; i <= 2 * r; ++i) {
        kernel[i] = static_cast<int>(std::lround(raw[i] / total * 4096));
    }
    const int w = image.width(), h = image.height();
    const long long pixels = static_cast<long long>(w) * h;
    QImage temp(image.size(), image.format());
    const auto put = [](uchar* dst, const int* sum) {
        for (int c = 0; c < 4; ++c) {
            dst[c] = static_cast<uchar>(std::clamp((sum[c] + 2048) >> 12, 0, 255));
        }
    };
    // Along the rows, into temp; the edges stand for what is beyond them.
    ParallelRows(h, pixels, [&](int from, int to) {
        for (int y = from; y < to; ++y) {
            const uchar* src = image.constScanLine(y);
            uchar* dst = temp.scanLine(y);
            for (int x = 0; x < w; ++x) {
                int sum[4] = {0, 0, 0, 0};
                if (x >= r && x + r < w) {
                    const uchar* px = src + (x - r) * 4;
                    for (int k = 0; k <= 2 * r; ++k, px += 4) {
                        for (int c = 0; c < 4; ++c) {
                            sum[c] += px[c] * kernel[k];
                        }
                    }
                } else {
                    for (int k = -r; k <= r; ++k) {
                        const uchar* px = src + std::clamp(x + k, 0, w - 1) * 4;
                        for (int c = 0; c < 4; ++c) {
                            sum[c] += px[c] * kernel[k + r];
                        }
                    }
                }
                put(dst + x * 4, sum);
            }
        }
    });
    // Down the columns, back into the image.
    ParallelRows(h, pixels, [&](int from, int to) {
        std::vector<const uchar*> lines(2 * r + 1);
        for (int y = from; y < to; ++y) {
            for (int k = -r; k <= r; ++k) {
                lines[k + r] = temp.constScanLine(std::clamp(y + k, 0, h - 1));
            }
            uchar* dst = image.scanLine(y);
            for (int x = 0; x < w * 4; x += 4) {
                int sum[4] = {0, 0, 0, 0};
                for (int k = 0; k <= 2 * r; ++k) {
                    const uchar* px = lines[k] + x;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += px[c] * kernel[k];
                    }
                }
                put(dst + x, sum);
            }
        }
    });
}

/// An image the size of a box (with a margin) in the screen's pixels, transparent.
QImage Canvas(const QSizeF& size, double scale) {
    QImage image(std::max(1, static_cast<int>(std::ceil(size.width() * scale))),
                 std::max(1, static_cast<int>(std::ceil(size.height() * scale))), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

/// Everything drawn in an image takes one colour, keeping its coverage.
void Tint(QImage& image, const QColor& colour) {
    QPainter q(&image);
    q.setCompositionMode(QPainter::CompositionMode_SourceIn);
    q.fillRect(image.rect(), colour);
}

/// A gradient's stops in shares of its length, CSS's way: a stop without a place between its
/// neighbours, none before the one before it, a hard edge where two meet.
QGradientStops Resolve(const QList<Stop>& stops, double length, double* first = nullptr, double* last = nullptr) {
    const int n = static_cast<int>(stops.size());
    std::vector<double> at(n, -1);
    for (int i = 0; i < n; ++i) {
        if (stops[i].place >= 0 || !stops[i].percent) {
            at[i] = stops[i].percent ? stops[i].place : (length > 0 ? stops[i].place / length : 0);
        }
    }
    if (n > 0 && at[0] < 0) {
        at[0] = 0;
    }
    if (n > 1 && at[n - 1] < 0) {
        at[n - 1] = 1;
    }
    for (int i = 1; i < n; ++i) {
        if (at[i] >= 0) {
            at[i] = std::max(at[i], at[i - 1]);
        }
    }
    for (int i = 1; i < n; ++i) {
        if (at[i] < 0) {
            int j = i;
            while (at[j] < 0) {
                ++j;
            }
            for (int k = i; k < j; ++k) {
                at[k] = at[i - 1] + (at[j] - at[i - 1]) * (k - i + 1) / (j - i + 1);
            }
        }
    }
    const double from = n > 0 ? at[0] : 0, to = n > 0 ? at[n - 1] : 1;
    if (first != nullptr) {
        *first = from;
    }
    if (last != nullptr) {
        *last = to;
    }
    QGradientStops out;
    double previous = -1;
    for (int i = 0; i < n; ++i) {
        // Qt keeps one colour a place: a hard edge is two places a hair apart.
        double place = std::clamp(at[i], 0.0, 1.0);
        if (place <= previous) {
            place = std::min(1.0, previous + 1e-5);
        }
        previous = place;
        out.append({place, stops[i].colour});
    }
    return out;
}

/// The stops of a repeating gradient as shares of one repeat (from its first stop to its last).
QGradientStops Repeat(const QList<Stop>& stops, double length, double& from, double& to) {
    QGradientStops plain = Resolve(stops, length, &from, &to);
    const double span = std::max(1e-6, to - from);
    for (auto& stop : plain) {
        stop.first = std::clamp((stop.first - from) / span, 0.0, 1.0);
    }
    return plain;
}

QColor ConicAt(const QGradientStops& stops, double t) {
    if (stops.isEmpty()) {
        return Qt::transparent;
    }
    if (t <= stops.first().first) {
        return stops.first().second;
    }
    for (int i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].first) {
            const double span = stops[i].first - stops[i - 1].first;
            const double f = span <= 0 ? 1 : (t - stops[i - 1].first) / span;
            const QColor a = stops[i - 1].second, b = stops[i].second;
            return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                                    a.blueF() + (b.blueF() - a.blueF()) * f, a.alphaF() + (b.alphaF() - a.alphaF()) * f);
        }
    }
    return stops.last().second;
}

void PaintLayer(QPainter& p, const QRectF& box, const Layer& layer) {
    switch (layer.kind) {
    case Layer::Colour:
        p.fillRect(box, layer.colour);
        return;
    case Layer::Linear: {
        const double rad = layer.angle * M_PI / 180;
        const QPointF dir(std::sin(rad), -std::cos(rad));
        const double length = std::fabs(box.width() * std::sin(rad)) + std::fabs(box.height() * std::cos(rad));
        const QPointF start = box.center() - dir * (length / 2);
        QLinearGradient gradient;
        if (layer.repeating) {
            double from = 0, to = 1;
            gradient.setStops(Repeat(layer.stops, length, from, to));
            gradient.setStart(start + dir * (from * length));
            gradient.setFinalStop(start + dir * (to * length));
            gradient.setSpread(QGradient::RepeatSpread);
        } else {
            gradient.setStops(Resolve(layer.stops, length));
            gradient.setStart(start);
            gradient.setFinalStop(start + dir * length);
        }
        p.fillRect(box, gradient);
        return;
    }
    case Layer::Radial: {
        // An ellipse to the farthest corner: the farthest sides' ratio, through the corner.
        const double cx = layer.at.x() * box.width(), cy = layer.at.y() * box.height();
        const double rx = std::max(cx, box.width() - cx) * M_SQRT2, ry = std::max(cy, box.height() - cy) * M_SQRT2;
        if (rx <= 0 || ry <= 0) {
            return;
        }
        QRadialGradient gradient(QPointF(0, 0), rx);
        gradient.setStops(Resolve(layer.stops, rx));
        QBrush brush(gradient);
        brush.setTransform(QTransform::fromTranslate(box.left() + cx, box.top() + cy).scale(1, ry / rx));
        p.fillRect(box, brush);
        return;
    }
    case Layer::Conic: {
        // A tile, each sheet pixel of it one colour, repeated over the box.
        const int tw = std::max(1, static_cast<int>(std::lround(layer.tile_w > 0 ? layer.tile_w : box.width())));
        const int th = std::max(1, static_cast<int>(std::lround(layer.tile_h > 0 ? layer.tile_h : box.height())));
        double from = 0, to = 1;
        const QGradientStops stops = layer.repeating ? Repeat(layer.stops, 1, from, to) : Resolve(layer.stops, 1, &from, &to);
        QImage tile(tw, th, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < th; ++y) {
            for (int x = 0; x < tw; ++x) {
                const double dx = x + 0.5 - tw / 2.0, dy = y + 0.5 - th / 2.0;
                double t = std::atan2(dx, -dy) / (2 * M_PI) - layer.angle / 360;
                t -= std::floor(t);
                if (layer.repeating) {
                    const double span = std::max(1e-6, to - from);
                    t = std::fmod(t - from, span) / span;
                    t = t < 0 ? t + 1 : t;
                }
                tile.setPixelColor(x, y, ConicAt(stops, t));
            }
        }
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        QBrush brush(tile);
        brush.setTransform(QTransform::fromTranslate(box.left(), box.top()));
        p.fillRect(box, brush);
        p.restore();
        return;
    }
    }
}

QHash<QString, QImage>& ShadowStore() {
    static QHash<QString, QImage> store;
    return store;
}

struct Face {
    QString family;
    int weight{400};
    QFont::Style style{QFont::StyleNormal};
};

} // namespace

void Blur(QImage& image, double sigma) {
    if (sigma < 0.15 || image.isNull()) {
        return;
    }
    if (image.format() != QImage::Format_ARGB32_Premultiplied) {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (sigma < 2.0) {
        SmallGauss(image, sigma);
        return;
    }
    // Three boxes make a Gaussian (their sizes after W. Jarosz and I. Kutsko).
    const int n = 3;
    const double ideal = std::sqrt(12 * sigma * sigma / n + 1);
    int lower = static_cast<int>(std::floor(ideal));
    if (lower % 2 == 0) {
        --lower;
    }
    const int upper = lower + 2;
    const int m = static_cast<int>(std::lround((12 * sigma * sigma - n * lower * lower - 4 * n * lower - 3 * n) / (-4.0 * lower - 4)));
    QImage temp(image.size(), image.format());
    for (int i = 0; i < n; ++i) {
        const int r = ((i < m ? lower : upper) - 1) / 2;
        BoxPass(image, temp, r, true);
        BoxPass(temp, image, r, false);
    }
}

void PaintFill(QPainter& p, const QRectF& box, const Fill& fill) {
    for (int i = static_cast<int>(fill.layers.size()) - 1; i >= 0; --i) {
        PaintLayer(p, box, fill.layers[i]);
    }
}

void PaintBox(QPainter& p, const QRectF& box, const Fill& background, const QList<Shadow>& shadows, const Borders& borders) {
    const double scale = ScaleOf(p);
    // Outer shadows, the last first; never under the box itself.
    for (int i = static_cast<int>(shadows.size()) - 1; i >= 0; --i) {
        const Shadow& shadow = shadows[i];
        if (shadow.inset || shadow.colour.alpha() == 0) {
            continue;
        }
        const QRectF shape = box.adjusted(-shadow.spread, -shadow.spread, shadow.spread, shadow.spread)
                                 .translated(shadow.dx, shadow.dy);
        p.save();
        QPainterPath outside;
        outside.setFillRule(Qt::OddEvenFill);
        outside.addRect(shape.adjusted(-shadow.blur * 2 - 2, -shadow.blur * 2 - 2, shadow.blur * 2 + 2, shadow.blur * 2 + 2));
        outside.addRect(box);
        p.setClipPath(outside, Qt::IntersectClip);
        if (shadow.blur <= 0) {
            p.fillRect(shape, shadow.colour);
        } else {
            const double margin = shadow.blur * 1.6 + 2;
            QImage image = Canvas(shape.size() + QSizeF(margin * 2, margin * 2), scale);
            {
                QPainter q(&image);
                q.scale(scale, scale);
                q.fillRect(QRectF(margin, margin, shape.width(), shape.height()), shadow.colour);
            }
            Blur(image, shadow.blur / 2 * scale);
            p.drawImage(QRectF(shape.left() - margin, shape.top() - margin, image.width() / scale, image.height() / scale), image);
        }
        p.restore();
    }
    PaintFill(p, box, background);
    // Inset shadows inside the borders, the last first.
    const QRectF padding = box.adjusted(borders.left, borders.top, -borders.right, -borders.bottom);
    for (int i = static_cast<int>(shadows.size()) - 1; i >= 0; --i) {
        const Shadow& shadow = shadows[i];
        if (!shadow.inset || shadow.colour.alpha() == 0) {
            continue;
        }
        const QRectF hole = padding.translated(shadow.dx, shadow.dy)
                                .adjusted(shadow.spread, shadow.spread, -shadow.spread, -shadow.spread);
        p.save();
        p.setClipRect(padding, Qt::IntersectClip);
        if (shadow.blur <= 0) {
            QPainterPath ring;
            ring.setFillRule(Qt::OddEvenFill);
            ring.addRect(padding);
            ring.addRect(hole.intersected(padding));
            p.fillPath(ring, shadow.colour);
        } else {
            const double margin = shadow.blur * 1.6 + 2;
            QImage image = Canvas(padding.size() + QSizeF(margin * 2, margin * 2), scale);
            {
                QPainter q(&image);
                q.scale(scale, scale);
                q.fillRect(QRectF(0, 0, padding.width() + margin * 2, padding.height() + margin * 2), shadow.colour);
                q.setCompositionMode(QPainter::CompositionMode_Clear);
                q.fillRect(hole.translated(-padding.left() + margin, -padding.top() + margin), Qt::transparent);
            }
            Blur(image, shadow.blur / 2 * scale);
            p.drawImage(QRectF(padding.left() - margin, padding.top() - margin, image.width() / scale, image.height() / scale),
                        image);
        }
        p.restore();
    }
    if (borders.colour.alpha() > 0) {
        if (borders.top > 0) {
            p.fillRect(QRectF(box.left(), box.top(), box.width(), borders.top), borders.colour);
        }
        if (borders.bottom > 0) {
            p.fillRect(QRectF(box.left(), box.bottom() - borders.bottom, box.width(), borders.bottom), borders.colour);
        }
        if (borders.left > 0) {
            p.fillRect(QRectF(box.left(), box.top() + borders.top, borders.left, box.height() - borders.top - borders.bottom),
                       borders.colour);
        }
        if (borders.right > 0) {
            p.fillRect(QRectF(box.right() - borders.right, box.top() + borders.top, borders.right,
                              box.height() - borders.top - borders.bottom),
                       borders.colour);
        }
    }
}

void PaintShape(QPainter& p, const QPainterPath& shape, const Fill& background, const QList<Shadow>& shadows) {
    const double scale = ScaleOf(p);
    const QRectF bounds = shape.boundingRect();
    // A shadow's own shape, blurred where it says so, drawn through a clip.
    const auto shade = [&](const QPainterPath& area, const QPainterPath& clip, const Shadow& shadow) {
        p.save();
        p.setClipPath(clip, Qt::IntersectClip);
        if (shadow.blur <= 0) {
            p.fillPath(area, shadow.colour);
        } else {
            const double margin = shadow.blur * 1.6 + 2;
            const QRectF frame = area.boundingRect().adjusted(-margin, -margin, margin, margin);
            QImage image = Canvas(frame.size(), scale);
            {
                QPainter q(&image);
                q.setRenderHint(QPainter::Antialiasing);
                q.scale(scale, scale);
                q.translate(-frame.topLeft());
                q.fillPath(area, shadow.colour);
            }
            Blur(image, shadow.blur / 2 * scale);
            p.drawImage(QRectF(frame.topLeft(), QSizeF(image.width() / scale, image.height() / scale)), image);
        }
        p.restore();
    };
    for (int i = static_cast<int>(shadows.size()) - 1; i >= 0; --i) {
        const Shadow& shadow = shadows[i];
        if (shadow.inset || shadow.colour.alpha() == 0) {
            continue;
        }
        QPainterPath outside;
        outside.addRect(bounds.adjusted(-shadow.blur * 2 - std::fabs(shadow.dx) - shadow.spread - 4,
                                        -shadow.blur * 2 - std::fabs(shadow.dy) - shadow.spread - 4,
                                        shadow.blur * 2 + std::fabs(shadow.dx) + shadow.spread + 4,
                                        shadow.blur * 2 + std::fabs(shadow.dy) + shadow.spread + 4));
        outside = outside.subtracted(shape);
        shade(shape.translated(shadow.dx, shadow.dy), outside, shadow);
    }
    p.save();
    p.setClipPath(shape, Qt::IntersectClip);
    PaintFill(p, bounds, background);
    p.restore();
    for (int i = static_cast<int>(shadows.size()) - 1; i >= 0; --i) {
        const Shadow& shadow = shadows[i];
        if (!shadow.inset || shadow.colour.alpha() == 0) {
            continue;
        }
        QPainterPath ring;
        ring.addRect(bounds.adjusted(-shadow.blur * 2 - 4, -shadow.blur * 2 - 4, shadow.blur * 2 + 4, shadow.blur * 2 + 4));
        ring = ring.subtracted(shape.translated(shadow.dx, shadow.dy));
        shade(ring, shape, shadow);
    }
}

QFont FontOf(const TypeSpec& type) {
    static QHash<QString, Face> faces;
    auto found = faces.find(type.font);
    if (found == faces.end()) {
        Face face;
        QFile file(QStringLiteral(":/fonts/") + type.font);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            const QStringList families = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFontFromData(data));
            // The face of this file (one family may have several files: a bold and an italic).
            const QRawFont raw(data, 12);
            face.family = families.isEmpty() ? QGuiApplication::font().family() : families.first();
            face.weight = raw.isValid() ? raw.weight() : 400;
            face.style = raw.isValid() ? raw.style() : QFont::StyleNormal;
        } else {
            face.family = QGuiApplication::font().family();
        }
        found = faces.insert(type.font, face);
    }
    QFont font(found->family);
    font.setWeight(static_cast<QFont::Weight>(found->weight));
    font.setStyle(found->style);
    if (type.bold && found->weight < 600) {
        font.setWeight(QFont::Bold); // made bold from the one weight there is
    }
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(type.size))));
    font.setLetterSpacing(QFont::AbsoluteSpacing, type.spacing * type.size);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(true);
    return font;
}

double NormalLine(const QFont& font) {
    const QFontMetricsF metrics(font);
    return std::max(metrics.lineSpacing(), metrics.ascent() + metrics.descent());
}

double CentredBaseline(const QFont& font, double centre_y) {
    const QFontMetricsF metrics(font);
    return centre_y + (metrics.ascent() - metrics.descent()) / 2;
}

double BoxBaseline(const QFont& font, double top, double line) {
    const QFontMetricsF metrics(font);
    const double height = line > 0 ? line : NormalLine(font);
    return top + (height - (metrics.ascent() + metrics.descent())) / 2 + metrics.ascent();
}

QString Fit(const QString& text, const QFont& font, double width) {
    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, width);
}

void ForgetShadows() {
    ShadowStore().clear();
}

namespace {

QHash<QString, double>& Smoothing() {
    static QHash<QString, double> by_family;
    return by_family;
}

/// Text in white on clear, its pixel steps rounded off: blurred by a share of the font's dot
/// (a sixteenth of its size: DotGothic16 is drawn on a grid of 16) and made sharp again with
/// a curve on its coverage, so a stair of dots becomes a smooth stroke with a clean edge.
QImage SmoothedText(const QFont& font, const QString& text, double strength, double scale, double margin) {
    const QFontMetricsF metrics(font);
    QImage image = Canvas(QSizeF(metrics.horizontalAdvance(text) + margin * 2, metrics.ascent() + metrics.descent() + margin * 2), scale);
    {
        QPainter q(&image);
        q.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        q.scale(scale, scale);
        q.setFont(font);
        q.setPen(Qt::white);
        q.drawText(QPointF(margin, margin + metrics.ascent()), text);
    }
    Blur(image, strength * font.pixelSize() / 16.0 * scale);
    // A smoothstep cut at the coverage a stroke one dot wide has at its old edge once blurred
    // (so strokes keep their width), over a narrow band (so the edge stays clean).
    const double cut = 0.5 * std::erf(1.0 / (strength * M_SQRT2)), band = 0.13;
    uchar curve[256];
    for (int v = 0; v < 256; ++v) {
        const double t = std::clamp((v / 255.0 - (cut - band)) / (2 * band), 0.0, 1.0);
        curve[v] = static_cast<uchar>(std::lround(t * t * (3 - 2 * t) * 255));
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int a = curve[qAlpha(line[x])];
            line[x] = qRgba(a, a, a, a);
        }
    }
    return image;
}

} // namespace

void SetSmoothing(const QString& family, double strength) {
    Smoothing().insert(family, strength);
    ShadowStore().clear();
}

double PutText(QPainter& p, const QFont& font, QPointF at, const QString& text, const QColor& colour,
                const QList<Shadow>& shadows, int align) {
    const QFontMetricsF metrics(font);
    const double width = metrics.horizontalAdvance(text);
    if (align == 1) {
        at.rx() -= width;
    } else if (align == 2) {
        at.rx() -= width / 2;
    }
    p.setFont(font);
    const double scale = ScaleOf(p);
    const double strength = Smoothing().value(font.family());
    if (strength > 0 && !text.isEmpty()) {
        // Smoothed: the text and each shadow drawn from one rounded-off picture of it.
        double margin = 4;
        for (const Shadow& shadow : shadows) {
            margin = std::max(margin, std::ceil(shadow.blur * 1.6) + 4);
        }
        QHash<QString, QImage>& store = ShadowStore();
        if (store.size() > 1200) {
            store.clear();
        }
        const QString base = QStringLiteral("s|%1|%2|%3|%4|%5").arg(font.key(), text).arg(scale).arg(strength).arg(margin);
        if (!store.contains(base)) {
            store.insert(base, SmoothedText(font, text, strength, scale, margin));
        }
        const auto tinted = [&](const QColor& tone, double blur) {
            const QString key = base + QStringLiteral("|%1|%2").arg(tone.rgba()).arg(blur);
            const auto found = store.constFind(key);
            if (found != store.constEnd()) {
                return *found;
            }
            QImage image = store.value(base);
            Tint(image, tone);
            Blur(image, blur / 2 * scale);
            store.insert(key, image);
            return image;
        };
        const QPointF corner(at.x() - margin, at.y() - metrics.ascent() - margin);
        for (int i = static_cast<int>(shadows.size()) - 1; i >= 0; --i) {
            QColor tone = shadows[i].colour;
            tone.setAlphaF(tone.alphaF() * colour.alphaF());
            if (tone.alpha() == 0) {
                continue;
            }
            const QImage image = tinted(tone, shadows[i].blur);
            p.drawImage(QRectF(corner + QPointF(shadows[i].dx, shadows[i].dy), QSizeF(image.width() / scale, image.height() / scale)), image);
        }
        const QImage image = tinted(colour, 0);
        p.drawImage(QRectF(corner, QSizeF(image.width() / scale, image.height() / scale)), image);
        return width;
    }
    for (int i = static_cast<int>(shadows.size()) - 1; i >= 0; --i) {
        const Shadow& shadow = shadows[i];
        QColor tone = shadow.colour;
        tone.setAlphaF(tone.alphaF() * colour.alphaF());
        if (tone.alpha() == 0) {
            continue;
        }
        if (shadow.blur <= 0) {
            p.setPen(tone);
            p.drawText(at + QPointF(shadow.dx, shadow.dy), text);
            continue;
        }
        const double margin = std::ceil(shadow.blur * 1.6) + 2;
        const QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(font.key(), text).arg(tone.rgba()).arg(shadow.blur).arg(scale);
        QHash<QString, QImage>& store = ShadowStore();
        auto found = store.constFind(key);
        QImage image;
        if (found != store.constEnd()) {
            image = *found;
        } else {
            image = Canvas(QSizeF(width + margin * 2, metrics.ascent() + metrics.descent() + margin * 2), scale);
            {
                QPainter q(&image);
                q.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
                q.scale(scale, scale);
                q.setFont(font);
                q.setPen(tone);
                q.drawText(QPointF(margin, margin + metrics.ascent()), text);
            }
            Blur(image, shadow.blur / 2 * scale);
            if (store.size() > 800) {
                store.clear();
            }
            store.insert(key, image);
        }
        p.drawImage(QRectF(at.x() + shadow.dx - margin, at.y() - metrics.ascent() + shadow.dy - margin, image.width() / scale,
                           image.height() / scale),
                    image);
    }
    p.setPen(colour);
    p.drawText(at, text);
    return width;
}

QImage OrbSprite(const QList<Shadow>& glow, double device_scale, int phase) {
    static QHash<QString, QImage> made;
    phase = ((phase % OrbPhases) + OrbPhases) % OrbPhases;
    QString key = QStringLiteral("%1|%2").arg(device_scale).arg(phase);
    for (const Shadow& shadow : glow) {
        key += QStringLiteral("|%1,%2,%3,%4").arg(shadow.dx).arg(shadow.dy).arg(shadow.blur).arg(shadow.colour.rgba());
    }
    const auto found = made.constFind(key);
    if (found != made.constEnd()) {
        return *found;
    }
    // Made a little larger than it is drawn at its largest (it grows to 1.2 in front).
    const double scale = device_scale * 1.25;
    QImage image = Canvas(QSizeF(OrbBox, OrbBox), scale);
    {
        QPainter q(&image);
        q.setRenderHint(QPainter::Antialiasing);
        q.scale(scale, scale);
        q.translate(OrbBox / 2, OrbBox / 2);
        // The board's 28-unit picture drawn 24 px wide.
        const double unit = 24.0 / 28.0;
        q.scale(unit, unit);
        q.setBrush(Qt::NoBrush);
        // Each ring turns in depth as a hoop spun on a table: seen edge-on it narrows to an
        // ellipse, and the one nearer the eye is drawn over the others. Whole turns in a loop
        // (`turns`, `swing`), so the last phase meets the first.
        const double u = static_cast<double>(phase) / OrbPhases;
        constexpr double Tau = 6.283185307179586;
        const struct {
            double x, y;  ///< Its centre's offset from the orb's, which goes round once a loop
            QColor colour;
            double turns; ///< Turns in depth a loop
            double start; ///< Where in its turn at the loop's start
            double tilt;  ///< The ellipse's axis at the start, and how many half turns it swings
            double swing;
        } rings[] = {{-2.2, 0.4, QColor(0xff, 0x54, 0x66), 1, 0.08, 0.35, 1},
                     {1.8, -1.8, QColor(0x3f, 0xd8, 0x84), 2, 0.41, 1.4, -1},
                     {0.4, 2.2, QColor(0x3d, 0x8c, 0xff), 1, 0.74, 2.5, 2}};
        struct Drawn {
            QPointF at;
            double narrow, angle, depth;
            QColor colour;
        };
        QList<Drawn> drawn;
        for (const auto& ring : rings) {
            const double turn = Tau * (ring.turns * u + ring.start);
            const double round = Tau * u;
            const QPointF at(ring.x * std::cos(round) - ring.y * std::sin(round), ring.x * std::sin(round) + ring.y * std::cos(round));
            drawn.append({at, std::max(0.14, std::fabs(std::cos(turn))), (ring.tilt + ring.swing * u) * 180.0, std::sin(turn),
                          ring.colour});
        }
        std::sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) { return a.depth < b.depth; });
        for (const Drawn& ring : drawn) {
            // The far side a little fainter and finer, as a hoop's back is.
            QColor colour = ring.colour;
            colour.setAlphaF(0.78 + 0.22 * (ring.depth + 1) / 2);
            q.save();
            q.translate(ring.at);
            q.rotate(ring.angle);
            q.setPen(QPen(colour, 1.6 + 0.3 * (ring.depth + 1) / 2));
            q.drawEllipse(QPointF(0, 0), 8.5, 8.5 * ring.narrow);
            q.restore();
        }
        q.setPen(Qt::NoPen);
        q.setBrush(Qt::white);
        q.drawEllipse(QPointF(0, 0), 2.4, 2.4);
    }
    // Each drop shadow is made of what is there so far, and goes under it.
    for (const Shadow& shadow : glow) {
        QImage under = image;
        Tint(under, shadow.colour);
        Blur(under, shadow.blur / 2 * scale);
        QImage result = Canvas(QSizeF(OrbBox, OrbBox), scale);
        QPainter q(&result);
        q.drawImage(QPointF(shadow.dx * scale, shadow.dy * scale), under);
        q.drawImage(QPointF(0, 0), image);
        q.end();
        image = result;
    }
    made.insert(key, image);
    return image;
}

namespace {

/// Grey noise, about half a grey, of a fineness like SVG's fractal noise at that frequency.
const QImage& Grain(double frequency, double device_scale) {
    static QHash<QString, QImage> made;
    const QString key = QStringLiteral("%1|%2").arg(frequency).arg(device_scale);
    const auto found = made.constFind(key);
    if (found != made.constEnd()) {
        return *found;
    }
    QImage tile(256, 256, QImage::Format_ARGB32_Premultiplied);
    QRandomGenerator random(0x5a7e);
    for (int y = 0; y < 256; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(tile.scanLine(y));
        for (int x = 0; x < 256; ++x) {
            const int v = static_cast<int>(random.bounded(256));
            line[x] = qRgb(v, v, v);
        }
    }
    // A grain a sheet pixel and more across at a low frequency, finer at a high one.
    Blur(tile, std::max(0.0, 0.42 / std::max(0.1, frequency) * device_scale - 0.3));
    // Spread back to about the same strength, round a half grey.
    double mean = 0, square = 0;
    for (int y = 0; y < 256; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(tile.constScanLine(y));
        for (int x = 0; x < 256; ++x) {
            mean += qRed(line[x]);
            square += qRed(line[x]) * qRed(line[x]);
        }
    }
    mean /= 65536;
    const double deviation = std::sqrt(std::max(1.0, square / 65536 - mean * mean));
    for (int y = 0; y < 256; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(tile.scanLine(y));
        for (int x = 0; x < 256; ++x) {
            const int v = std::clamp(static_cast<int>(std::lround(128 + (qRed(line[x]) - mean) * 48 / deviation)), 0, 255);
            line[x] = qRgb(v, v, v);
        }
    }
    return *made.insert(key, tile);
}

} // namespace

void ApplyFuzz(QImage& image, const FuzzSpec& spec, double device_scale, QPoint origin) {
    if (image.format() != QImage::Format_ARGB32_Premultiplied && image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    const int w = image.width(), h = image.height();
    const long long pixels = static_cast<long long>(w) * h;
    // Red taken from the right and blue from the left (red drawn to the left, blue to the
    // right), each by part of a pixel too.
    const double shift = spec.level.shift * device_scale;
    if (shift > 0.01) {
        const int whole = static_cast<int>(std::floor(shift));
        const int part = static_cast<int>(std::lround((shift - whole) * 256));
        ParallelRows(h, pixels, [&](int from, int to) {
            std::vector<QRgb> line(w);
            for (int y = from; y < to; ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
                std::copy(row, row + w, line.begin());
                for (int x = 0; x < w; ++x) {
                    const bool inside = x - whole - 1 >= 0 && x + whole + 1 < w;
                    const QRgb r0 = inside ? line[x + whole] : line[std::clamp(x + whole, 0, w - 1)];
                    const QRgb r1 = inside ? line[x + whole + 1] : line[std::clamp(x + whole + 1, 0, w - 1)];
                    const QRgb b0 = inside ? line[x - whole] : line[std::clamp(x - whole, 0, w - 1)];
                    const QRgb b1 = inside ? line[x - whole - 1] : line[std::clamp(x - whole - 1, 0, w - 1)];
                    const int red = (qRed(r0) * (256 - part) + qRed(r1) * part) >> 8;
                    const int blue = (qBlue(b0) * (256 - part) + qBlue(b1) * part) >> 8;
                    row[x] = qRgba(red, qGreen(line[x]), blue, qAlpha(line[x]));
                }
            }
        });
    }
    Blur(image, spec.level.blur * device_scale);
    // Contrast and grain (overlaid) as one table of a channel's value and the noise's, then the
    // interlace lines, a pixel at a time.
    const bool contrast = std::fabs(spec.level.contrast - 1) > 0.001;
    int table[256];
    for (int v = 0; v < 256; ++v) {
        table[v] = contrast ? std::clamp(static_cast<int>(std::lround((v - 127.5) * spec.level.contrast + 127.5)), 0, 255) : v;
    }
    const int strength = static_cast<int>(std::lround(spec.grain * 0.6 * 256));
    std::vector<uchar> mixed(256 * 256);
    for (int n = 0; n < 256; ++n) {
        for (int value = 0; value < 256; ++value) {
            const int v = table[value];
            const int over = v < 128 ? 2 * v * n / 255 : 255 - 2 * (255 - v) * (255 - n) / 255;
            mixed[n * 256 + value] = static_cast<uchar>(std::clamp(v + (((over - v) * strength) >> 8), 0, 255));
        }
    }
    const QImage& grain = Grain(spec.grain_frequency, device_scale);
    ParallelRows(h, pixels, [&](int from, int to) {
        for (int y = from; y < to; ++y) {
            // How much of this row of the screen lies on a dark line (one sheet pixel in two).
            const double top = (origin.y() + y) / device_scale, bottom = (origin.y() + y + 1) / device_scale;
            double dark = 0;
            for (double band = std::floor(top / 2) * 2; band < bottom; band += 2) {
                dark += std::max(0.0, std::min(bottom, band + 1) - std::max(top, band));
            }
            const int keep = 256 - static_cast<int>(std::lround(spec.interlace * dark / (bottom - top) * 256));
            QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
            const QRgb* noise = reinterpret_cast<const QRgb*>(grain.constScanLine((origin.y() + y) & 255));
            for (int x = 0; x < w; ++x) {
                const uchar* by = mixed.data() + qRed(noise[(origin.x() + x) & 255]) * 256;
                const QRgb px = row[x];
                row[x] = qRgba(std::min(255, (by[qRed(px)] * keep) >> 8), std::min(255, (by[qGreen(px)] * keep) >> 8),
                               std::min(255, (by[qBlue(px)] * keep) >> 8), qAlpha(px));
            }
        }
    });
}

} // namespace Home::Paint
