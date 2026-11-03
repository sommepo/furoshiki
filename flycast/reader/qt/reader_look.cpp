// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QPainterPath>
#include "reader/qt/reader_overlay.h"

namespace ReaderUi {

// ---- The console's look: white, with a spiral (the maintainer's choice of three, 2026-10-10) ----

namespace {
/// A spiral of about one and three quarter turns around `centre`, `radius` at its outer end.
/// Drawn here from a formula: it is not the console maker's mark, though it is meant to
/// call it to mind.
QPainterPath Spiral(const QPointF& centre, qreal radius) {
    QPainterPath path;
    constexpr int Steps = 96;
    for (int i = 0; i <= Steps; ++i) {
        const qreal t = static_cast<qreal>(i) / Steps;
        const qreal angle = t * 3.5 * 3.14159265358979 + 0.6;
        const qreal r = radius * (0.11 + 0.89 * t);
        const QPointF point(centre.x() + std::cos(angle) * r, centre.y() + std::sin(angle) * r);
        if (i == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }
    return path;
}
} // namespace

void Style::PaintPopupGround(QPainter& painter, const QRect& area) {
    painter.fillRect(area, QColor(255, 255, 255));
    // the spiral, faint, half out of the bottom right corner
    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal radius = std::min<qreal>(area.height() * 0.62, 120.0);
    painter.setPen(QPen(QColor(0xF2, 0x7C, 0x1E, 34), radius * 0.26, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(Spiral(QPointF(area.right() - radius * 0.35, area.bottom() - radius * 0.25), radius));
    painter.restore();
}

void Style::PaintPopupEdge(QPainter& painter, const QRect& area) {
    painter.save();
    painter.setPen(QPen(PopupEdge, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(area.adjusted(0, 0, -1, -1));
    painter.restore();
}

void Style::PaintPopupRule(QPainter& painter, const QRect& rule) {
    painter.fillRect(rule, QColor(0xE3, 0xE9, 0xF5));
}

void Style::PaintPopupEntry(QPainter&, const QRectF&, const QRectF&, bool) {
    // nothing behind an entry: the white is the look
}

void Style::DrawPointer(QPainter& painter, const QPointF& centre, qreal size) {
    // an orange spiral on a white disc, with a thin dark rim so that it shows on a white picture
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal radius = size * 0.44;
    painter.setPen(QPen(QColor(0, 0, 0, 70), std::max(1.0, size * 0.03)));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(centre, radius, radius);
    painter.setPen(QPen(QColor(0xF2, 0x7C, 0x1E), radius * 0.22, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(Spiral(centre, radius * 0.72));
    painter.restore();
}

void Style::DressDialog(QWidget*) {
    // The reader's windows keep the app's own look here.
}

} // namespace ReaderUi
