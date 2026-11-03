// SPDX-FileCopyrightText: PPSSPP Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QPainterPath>
#include "reader/qt/reader_overlay.h"

namespace ReaderUi {

// ---- The console's look: the PSP's cross-media bar: a blue band with the wave through it, white type, the headword aglow ----

void Style::PaintPopupGround(QPainter& painter, const QRect& area) {
    QLinearGradient band(area.topLeft(), area.bottomRight());
    band.setColorAt(0.0, QColor(40, 110, 200));
    band.setColorAt(0.55, QColor(20, 50, 130));
    band.setColorAt(1.0, QColor(60, 140, 220));
    painter.fillRect(area, band);
    // the wave that runs through the menu
    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF wave(area.left() - area.width() * 0.1, area.top() + area.height() * 0.40,
                      area.width() * 1.2, area.height() * 1.3);
    painter.setBrush(QColor(160, 210, 255, 26));
    painter.setPen(QPen(QColor(255, 255, 255, 72), 1.5));
    painter.drawEllipse(wave);
    painter.restore();
}

void Style::PaintPopupEdge(QPainter& painter, const QRect& area) {
    painter.fillRect(QRect(area.left(), area.top(), area.width(), 1), QColor(255, 255, 255, 153));
    painter.fillRect(QRect(area.left(), area.bottom(), area.width(), 1), QColor(255, 255, 255, 77));
}

void Style::PaintPopupRule(QPainter& painter, const QRect& rule) {
    QLinearGradient fade(rule.topLeft(), rule.topRight());
    fade.setColorAt(0.0, QColor(255, 255, 255, 0));
    fade.setColorAt(0.5, QColor(255, 255, 255, 180));
    fade.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(rule, fade);
}

void Style::PaintPopupEntry(QPainter& painter, const QRectF&, const QRectF& head, bool) {
    // the headword glows as the chosen item of the bar does
    const QPointF centre(head.left() + head.height() * 1.2, head.center().y());
    QRadialGradient glow(centre, head.height() * 2.2);
    glow.setColorAt(0.0, QColor(255, 255, 255, 70));
    glow.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(head.adjusted(-8, -6, 0, 6), glow);
}

void Style::DrawPointer(QPainter& painter, const QPointF& centre, qreal size) {
    // one of the lights that drift along the wave: a four-point sparkle in a soft glow
    QRadialGradient glow(centre, size / 2.0);
    glow.setColorAt(0.0, QColor(255, 255, 255, 255));
    glow.setColorAt(0.35, QColor(207, 230, 255, 220));
    glow.setColorAt(1.0, QColor(111, 180, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(centre, size / 2.0, size / 2.0);
    const qreal arm = size * 0.38, waist = size * 0.06;
    QPainterPath star;
    star.moveTo(centre.x(), centre.y() - arm);
    star.lineTo(centre.x() + waist, centre.y() - waist);
    star.lineTo(centre.x() + arm, centre.y());
    star.lineTo(centre.x() + waist, centre.y() + waist);
    star.lineTo(centre.x(), centre.y() + arm);
    star.lineTo(centre.x() - waist, centre.y() + waist);
    star.lineTo(centre.x() - arm, centre.y());
    star.lineTo(centre.x() - waist, centre.y() - waist);
    star.closeSubpath();
    painter.setPen(QPen(QColor(20, 50, 130, 160), size * 0.02));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawPath(star);
}

void Style::DressDialog(QWidget*) {
    // The reader's windows keep the app's own look here.
}

} // namespace ReaderUi
