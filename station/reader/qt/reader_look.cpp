// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include "reader/qt/reader_overlay.h"

namespace ReaderUi {

// ---- The console's look: its grey case and the black underside of its discs ----

void Style::PaintPopupGround(QPainter& painter, const QRect& area) {
    // the case's warm light grey, a shade darker towards the bottom, with the thin ridges of
    // its lid along the top
    QLinearGradient ground(area.topLeft(), area.bottomLeft());
    ground.setColorAt(0.0, QColor(0xEE, 0xED, 0xE8));
    ground.setColorAt(1.0, QColor(0xDC, 0xDB, 0xD5));
    painter.fillRect(area, ground);
    painter.save();
    painter.setPen(QPen(QColor(0xC3, 0xC2, 0xBC), 1));
    for (int i = 0; i < 3; ++i) {
        const int y = area.top() + 3 + i * 3;
        painter.drawLine(area.left() + 8, y, area.right() - 8, y);
    }
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
    painter.fillRect(rule, QColor(0xC8, 0xC7, 0xC1));
}

void Style::PaintPopupEntry(QPainter&, const QRectF&, const QRectF&, bool) {
    // nothing behind an entry: the grey is the look
}

void Style::DrawPointer(QPainter& painter, const QPointF& centre, qreal size) {
    // a disc as the console's discs are underneath: black, a ring of light grey round the hole,
    // a sheen across it
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal radius = size * 0.44;
    painter.setPen(QPen(QColor(0xB8, 0xB8, 0xBE), std::max(1.0, size * 0.04)));
    QRadialGradient face(centre, radius);
    face.setColorAt(0.0, QColor(0x3A, 0x3A, 0x44));
    face.setColorAt(1.0, QColor(0x0C, 0x0C, 0x10));
    painter.setBrush(face);
    painter.drawEllipse(centre, radius, radius);
    // the sheen: a thin band of colour, as light catches the underside
    QPainterPath band;
    band.moveTo(centre);
    band.arcTo(QRectF(centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius), 30, 40);
    band.closeSubpath();
    QLinearGradient sheen(centre + QPointF(radius, -radius), centre);
    sheen.setColorAt(0.0, QColor(140, 120, 255, 120));
    sheen.setColorAt(0.5, QColor(120, 220, 255, 70));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(sheen);
    painter.drawPath(band);
    // the clamp ring and the hole
    painter.setBrush(QColor(0xC8, 0xC7, 0xC1));
    painter.drawEllipse(centre, radius * 0.34, radius * 0.34);
    painter.setBrush(QColor(0x23, 0x24, 0x2B));
    painter.drawEllipse(centre, radius * 0.12, radius * 0.12);
    painter.restore();
}

void Style::DressDialog(QWidget*) {
    // The reader's windows keep the app's own look (the palette host.cpp sets).
}

} // namespace ReaderUi
