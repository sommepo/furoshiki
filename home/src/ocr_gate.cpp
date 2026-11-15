// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ocr_gate.h"

#include <algorithm>
#include <cstdlib>
#include <QPainter>

namespace Home::OcrGate {

namespace {
constexpr int Wide = 128, Tall = 32;  // each box, however large it is on the screen
constexpr int PixelThreshold = 40;    // as the helper's stabiliser has it
} // namespace

QImage Signature(const QImage& picture, const QList<QRectF>& boxes) {
    if (picture.isNull() || boxes.isEmpty()) {
        return {};
    }
    QImage out(Wide, Tall * static_cast<int>(boxes.size()), QImage::Format_Grayscale8);
    out.fill(0);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int i = 0; i < boxes.size(); ++i) {
        const QRectF& box = boxes[i];
        p.drawImage(QRectF(0, i * Tall, Wide, Tall), picture,
                    QRectF(box.x() * picture.width(), box.y() * picture.height(), box.width() * picture.width(), box.height() * picture.height()));
    }
    return out;
}

double ChangedShare(const QImage& before, const QImage& now) {
    if (before.isNull() || now.isNull() || before.size() != now.size() || before.format() != QImage::Format_Grayscale8 ||
        now.format() != QImage::Format_Grayscale8) {
        return 1.0;
    }
    qint64 unlike = 0;
    for (int y = 0; y < now.height(); ++y) {
        const uchar* a = before.constScanLine(y);
        const uchar* b = now.constScanLine(y);
        for (int x = 0; x < now.width(); ++x) {
            unlike += std::abs(static_cast<int>(a[x]) - static_cast<int>(b[x])) > PixelThreshold ? 1 : 0;
        }
    }
    return static_cast<double>(unlike) / (static_cast<double>(now.width()) * now.height());
}

bool Gate::Want(const QImage& signature, qint64 now_ms) {
    ++seen;
    if (signature.isNull()) {
        return false;
    }
    // Against the look it had when it last changed, so that a slow fade adds up to a change.
    if (ChangedShare(last, signature) >= MinChange) {
        last = signature;
        if (changed_at < 0 || now_ms - changed_at > tail_ms) {
            began_at = now_ms; // after it stood still: something new comes
        }
        changed_at = now_ms;
    }
    const bool want = sent_at < 0 || (changed_at >= 0 && now_ms - changed_at <= tail_ms) || now_ms - sent_at >= keep_ms;
    if (want) {
        sent_at = now_ms;
        ++passed;
    }
    return want;
}

OnScreen Place(const QRectF& box, const QRectF& picture, const QRectF& upper, const QRectF& lower) {
    if (!box.isValid() || !picture.isValid()) {
        return {};
    }
    const QRectF drawn(picture.x() + box.x() * picture.width(), picture.y() + box.y() * picture.height(), box.width() * picture.width(),
                       box.height() * picture.height());
    const QRectF screens[] = {upper.isValid() ? upper : picture, lower};
    for (int i = 0; i < 2; ++i) {
        const QRectF& screen = screens[i];
        if (screen.isValid() && screen.contains(drawn.center())) {
            return {i, QRectF((drawn.x() - screen.x()) / screen.width(), (drawn.y() - screen.y()) / screen.height(), drawn.width() / screen.width(),
                              drawn.height() / screen.height())};
        }
    }
    return {};
}

} // namespace Home::OcrGate
