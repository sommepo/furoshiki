// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#include <algorithm>
#include <cmath>
#include <QDialog>
#include <QPainter>
#include <QPainterPath>
#include "reader/qt/ps2_home.h"
#include "reader/qt/reader_overlay.h"

namespace ReaderUi {

// ---- The console's look: the PS2's system menu: black-indigo glass, a thin light-blue edge, the headword lit from within ----

void Style::PaintPopupGround(QPainter& painter, const QRect& area) {
    QLinearGradient ground(area.topLeft(), area.bottomRight());
    ground.setColorAt(0.0, QColor(8, 12, 34));
    ground.setColorAt(1.0, QColor(2, 3, 12));
    painter.fillRect(area, ground);
    // the glass is lit a little from its upper edge
    QLinearGradient light(area.topLeft(), QPointF(area.left(), area.top() + area.height() * 0.45));
    light.setColorAt(0.0, QColor(60, 110, 255, 44));
    light.setColorAt(1.0, QColor(60, 110, 255, 0));
    painter.fillRect(area, light);
}

void Style::PaintPopupEdge(QPainter& painter, const QRect& area) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(PopupEdge, 1));
    painter.drawRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5));
}

void Style::PaintPopupRule(QPainter& painter, const QRect& rule) {
    QLinearGradient fade(rule.topLeft(), rule.topRight());
    fade.setColorAt(0.0, QColor(127, 196, 255, 180));
    fade.setColorAt(1.0, QColor(127, 196, 255, 0));
    painter.fillRect(rule, fade);
}

void Style::PaintPopupEntry(QPainter& painter, const QRectF&, const QRectF& head, bool) {
    // the headword, lit from within as the menu's chosen entry is
    const QPointF centre(head.left() + head.height() * 1.2, head.center().y());
    QRadialGradient glow(centre, head.height() * 2.4);
    glow.setColorAt(0.0, QColor(61, 125, 255, 96));
    glow.setColorAt(1.0, QColor(61, 125, 255, 0));
    painter.fillRect(head.adjusted(-8, -6, 0, 6), glow);
}

void Style::DrawPointer(QPainter& painter, const QPointF& centre, qreal size) {
    // one of the menu's lights: a white core in a blue halo
    QRadialGradient halo(centre, size / 2.0);
    halo.setColorAt(0.0, QColor(255, 255, 255, 255));
    halo.setColorAt(0.3, QColor(159, 212, 255, 220));
    halo.setColorAt(1.0, QColor(42, 107, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(centre, size / 2.0, size / 2.0);
    painter.setBrush(QColor(234, 246, 255));
    painter.drawEllipse(centre, size * 0.09, size * 0.09);
}

void Style::DressDialog(QWidget* dialog) {
    // The reader's windows in the PS2 menu's look (ps2_home.h).
    if (auto* window = qobject_cast<QDialog*>(dialog)) {
        Ps2Look::Dress(window);
    }
}

} // namespace ReaderUi
