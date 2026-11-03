// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QDialog>
#include <QPainter>
#include "reader/qt/reader_overlay.h"
#include "reader/qt/threeds_home.h"

namespace ReaderUi {

// ---- The console's look: the 3DS's lower-screen panels: dark grey pinstripes, white type, light
// bevelled buttons ----

void Style::PaintPopupGround(QPainter& painter, const QRect& area) {
    // the lower screen's pinstripes
    painter.fillRect(area, QColor(0x4A, 0x4F, 0x55));
    for (int y = area.top() + 2; y <= area.bottom(); y += 4) {
        painter.fillRect(QRect(area.left(), y, area.width(), 2), QColor(0x42, 0x47, 0x4D));
    }
}

void Style::PaintPopupEdge(QPainter& painter, const QRect& area) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(PopupEdge, 2));
    painter.drawRect(QRectF(area).adjusted(1, 1, -1, -1));
    painter.fillRect(QRect(area.left() + 2, area.top() + 2, area.width() - 4, 1),
                     QColor(255, 255, 255, 64));
}

void Style::PaintPopupRule(QPainter& painter, const QRect& rule) {
    painter.fillRect(rule, QColor(0x23, 0x26, 0x2A));
    painter.fillRect(rule.translated(0, 1), QColor(255, 255, 255, 46));
}

void Style::PaintPopupEntry(QPainter&, const QRectF&, const QRectF&, bool) {}

void Style::DrawPointer(QPainter& painter, const QPointF& centre, qreal size) {
    // a touch of the stylus: a ring, and a fainter one spreading from it
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 110), size * 0.05));
    painter.drawEllipse(centre, size * 0.44, size * 0.44);
    painter.setPen(QPen(QColor(0x1D, 0x20, 0x24), size * 0.11));
    painter.drawEllipse(centre, size * 0.24, size * 0.24);
    painter.setPen(QPen(QColor(255, 255, 255), size * 0.05));
    painter.drawEllipse(centre, size * 0.24, size * 0.24);
    painter.setPen(QPen(QColor(0x1D, 0x20, 0x24), size * 0.02));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(centre, size * 0.06, size * 0.06);
}

void Style::DressDialog(QWidget* dialog) {
    // The reader's windows in the 3DS's colours, when they are wanted (threeds_home.h).
    if (auto* window = qobject_cast<QDialog*>(dialog)) {
        ThreeDsLook::Dress(window);
    }
}

} // namespace ReaderUi
