// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include "reader/qt/reader_look.h"
#include "reader/qt/reader_overlay.h"

namespace ReaderUi {

namespace {

enum class Look { Plain, PlayStation, Dreamcast, Psp, ThreeDs, Switch, Ps2 };
Look g_look = Look::Plain;

/// Flycast Reader's: a spiral of about one and three quarter turns around `centre`, `radius`
/// at its outer end, drawn from a formula (it is not the console maker's mark, though it is
/// meant to call it to mind).
QPainterPath Spiral(const QPointF& centre, qreal radius) {
    QPainterPath path;
    constexpr int Steps = 96;
    for (int i = 0; i <= Steps; ++i) {
        const qreal t = static_cast<qreal>(i) / Steps;
        const qreal angle = t * 3.5 * 3.14159265358979 + 0.6;
        const qreal r = radius * (0.11 + 0.89 * t);
        const QPointF point(centre.x() + std::cos(angle) * r, centre.y() + std::sin(angle) * r);
        i == 0 ? path.moveTo(point) : path.lineTo(point);
    }
    return path;
}

} // namespace

/// The popup's colours at once (reader_look.h).
static void Colours(QColor text, QColor dim, QColor accent, QColor sign, QColor book_sign, QColor box, QColor box_pressed,
             QColor edge) {
    Style::PopupText = text;
    Style::PopupDim = dim;
    Style::PopupAccent = accent;
    Style::PopupSign = sign;
    Style::PopupBookSign = book_sign;
    Style::PopupBox = box;
    Style::PopupBoxPressed = box_pressed;
    Style::PopupEdge = edge;
}

void Style::SetConsoleLook(const char* key) {
    const auto is = [key](const char* name) { return key != nullptr && std::strcmp(key, name) == 0; };
    PopupSign = QColor(0xFF, 0xFF, 0xFF);
    PopupBookSign = QColor(0xFF, 0xFF, 0xFF);
    if (is("psp")) {
        // PPSSPP Reader's: the PSP's cross-media bar, a blue band with the wave, white type
        g_look = Look::Psp;
        Colours({0xFF, 0xFF, 0xFF}, {0xD3, 0xE4, 0xF8}, {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
                {0x1E, 0x46, 0x96}, {0x3C, 0x78, 0xD2}, {0xFF, 0xFF, 0xFF, 0x99});
        return;
    }
    if (is("3ds")) {
        // Azahar Reader's: the 3DS's lower-screen panels, dark grey pinstripes, light buttons
        g_look = Look::ThreeDs;
        Colours({0xF3, 0xF5, 0xF7}, {0xC2, 0xC8, 0xCF}, {0x9F, 0xD8, 0xFF}, {0x2A, 0x2F, 0x35}, {0x2A, 0x2F, 0x35},
                {0xE4, 0xE8, 0xEC}, {0xB8, 0xBE, 0xC5}, {0x1D, 0x20, 0x24});
        return;
    }
    if (is("switch")) {
        // Eden Reader's: the Switch's HOME menu in Basic Black, the teal frame
        g_look = Look::Switch;
        Colours({0xFF, 0xFF, 0xFF}, {0xB4, 0xB4, 0xB4}, {0x1F, 0xE0, 0xC8}, {0x1F, 0xE0, 0xC8}, {0xFF, 0xFF, 0xFF},
                {0x3B, 0x3B, 0x3B}, {0x58, 0x58, 0x58}, {0x55, 0x55, 0x55});
        return;
    }
    if (is("ps2")) {
        // PCSX2 Reader's: the PS2's system menu, black-indigo glass with a light-blue edge
        g_look = Look::Ps2;
        Colours({0xFF, 0xFF, 0xFF}, {0x7F, 0xA6, 0xD6}, {0x9F, 0xD0, 0xFF}, {0x9F, 0xD0, 0xFF}, {0xFF, 0xFF, 0xFF},
                {0x0B, 0x12, 0x2E}, {0x1F, 0x33, 0x6B}, {0x7F, 0xC4, 0xFF, 0x8C});
        return;
    }
    if (is("ps1")) {
        // Station Reader's: the light grey of the console's case and of its system menu's
        // panels, dark ink, the deep blue of its menus for what stands out.
        g_look = Look::PlayStation;
        PopupText = QColor(0x23, 0x24, 0x2B);
        PopupDim = QColor(0x6B, 0x6C, 0x74);
        PopupAccent = QColor(0x2B, 0x3A, 0x8C);
        PopupBox = QColor(0x2B, 0x3A, 0x8C);
        PopupBoxPressed = QColor(0x1A, 0x24, 0x5C);
        PopupEdge = QColor(0x9C, 0x9B, 0x95);
        return;
    }
    if (key != nullptr && std::strcmp(key, "dc") == 0) {
        // Flycast Reader's: plain white with a faint orange spiral in its corner, dark blue
        // type, orange for the plus and the book.
        g_look = Look::Dreamcast;
        PopupText = QColor(0x1F, 0x3F, 0x8F);
        PopupDim = QColor(0x6B, 0x82, 0xB5);
        PopupAccent = QColor(0xD8, 0x62, 0x0A);
        PopupBox = QColor(0xF2, 0x7C, 0x1E);
        PopupBoxPressed = QColor(0xD8, 0x62, 0x0A);
        PopupEdge = QColor(0xC9, 0xD6, 0xEC);
        return;
    }
    g_look = Look::Plain;
    PopupText = QColor(0x1C, 0x22, 0x33);
    PopupDim = QColor(0x66, 0x6E, 0x82);
    PopupAccent = QColor(0x2F, 0x5B, 0xC8);
    PopupBox = QColor(0x2F, 0x5B, 0xC8);
    PopupBoxPressed = QColor(0x1E, 0x3C, 0x8A);
    PopupEdge = QColor(0xB4, 0xBC, 0xCC);
}

void Style::PaintPopupGround(QPainter& painter, const QRect& area) {
    if (g_look == Look::Psp) {
        QLinearGradient band(area.topLeft(), area.bottomRight());
        band.setColorAt(0.0, QColor(40, 110, 200));
        band.setColorAt(0.55, QColor(20, 50, 130));
        band.setColorAt(1.0, QColor(60, 140, 220));
        painter.fillRect(area, band);
        // the wave that runs through the menu
        painter.save();
        painter.setClipRect(area);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF wave(area.left() - area.width() * 0.1, area.top() + area.height() * 0.40, area.width() * 1.2,
                          area.height() * 1.3);
        painter.setBrush(QColor(160, 210, 255, 26));
        painter.setPen(QPen(QColor(255, 255, 255, 72), 1.5));
        painter.drawEllipse(wave);
        painter.restore();
        return;
    }
    if (g_look == Look::ThreeDs) {
        // the lower screen's pinstripes
        painter.fillRect(area, QColor(0x4A, 0x4F, 0x55));
        for (int y = area.top() + 2; y <= area.bottom(); y += 4) {
            painter.fillRect(QRect(area.left(), y, area.width(), 2), QColor(0x42, 0x47, 0x4D));
        }
        return;
    }
    if (g_look == Look::Switch) {
        painter.fillRect(area, QColor(0x2D, 0x2D, 0x2D));
        return;
    }
    if (g_look == Look::Ps2) {
        QLinearGradient ground(area.topLeft(), area.bottomRight());
        ground.setColorAt(0.0, QColor(8, 12, 34));
        ground.setColorAt(1.0, QColor(2, 3, 12));
        painter.fillRect(area, ground);
        // the glass is lit a little from its upper edge
        QLinearGradient light(area.topLeft(), QPointF(area.left(), area.top() + area.height() * 0.45));
        light.setColorAt(0.0, QColor(60, 110, 255, 44));
        light.setColorAt(1.0, QColor(60, 110, 255, 0));
        painter.fillRect(area, light);
        return;
    }
    if (g_look == Look::PlayStation) {
        // the case's warm light grey, a shade darker towards the bottom, with the thin ridges
        // of its lid along the top
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
        return;
    }
    if (g_look == Look::Dreamcast) {
        painter.fillRect(area, QColor(255, 255, 255));
        // the spiral, faint, half out of the bottom right corner
        painter.save();
        painter.setClipRect(area);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal radius = std::min<qreal>(area.height() * 0.62, 120.0);
        painter.setPen(QPen(QColor(0xF2, 0x7C, 0x1E, 34), radius * 0.26, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(Spiral(QPointF(area.right() - radius * 0.35, area.bottom() - radius * 0.25), radius));
        painter.restore();
        return;
    }
    painter.fillRect(area, QColor(0xF7, 0xF8, 0xFB));
}

void Style::PaintPopupEdge(QPainter& painter, const QRect& area) {
    if (g_look == Look::Psp) {
        painter.fillRect(QRect(area.left(), area.top(), area.width(), 1), QColor(255, 255, 255, 153));
        painter.fillRect(QRect(area.left(), area.bottom(), area.width(), 1), QColor(255, 255, 255, 77));
        return;
    }
    if (g_look == Look::ThreeDs) {
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(PopupEdge, 2));
        painter.drawRect(QRectF(area).adjusted(1, 1, -1, -1));
        painter.fillRect(QRect(area.left() + 2, area.top() + 2, area.width() - 4, 1), QColor(255, 255, 255, 64));
        painter.restore();
        return;
    }
    if (g_look == Look::Switch || g_look == Look::Ps2) {
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(PopupEdge, 1));
        painter.drawRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5));
        painter.restore();
        return;
    }
    painter.save();
    painter.setPen(QPen(PopupEdge, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(area.adjusted(0, 0, -1, -1));
    painter.restore();
}

void Style::PaintPopupRule(QPainter& painter, const QRect& rule) {
    if (g_look == Look::Psp || g_look == Look::Ps2) {
        QLinearGradient fade(rule.topLeft(), rule.topRight());
        if (g_look == Look::Psp) {
            fade.setColorAt(0.0, QColor(255, 255, 255, 0));
            fade.setColorAt(0.5, QColor(255, 255, 255, 180));
            fade.setColorAt(1.0, QColor(255, 255, 255, 0));
        } else {
            fade.setColorAt(0.0, QColor(127, 196, 255, 180));
            fade.setColorAt(1.0, QColor(127, 196, 255, 0));
        }
        painter.fillRect(rule, fade);
        return;
    }
    if (g_look == Look::ThreeDs) {
        painter.fillRect(rule, QColor(0x23, 0x26, 0x2A));
        painter.fillRect(rule.translated(0, 1), QColor(255, 255, 255, 46));
        return;
    }
    if (g_look == Look::Switch) {
        painter.fillRect(rule, PopupEdge);
        return;
    }
    painter.fillRect(rule, g_look == Look::PlayStation ? QColor(0xC8, 0xC7, 0xC1)
                           : g_look == Look::Dreamcast ? QColor(0xE3, 0xE9, 0xF5)
                                                       : QColor(0xD8, 0xDC, 0xE6));
}

void Style::PaintPopupEntry(QPainter& painter, const QRectF& entry, const QRectF& head, bool first) {
    if (g_look == Look::Psp || g_look == Look::Ps2) {
        // the headword glows as the chosen item of the console's menu does
        const bool psp = g_look == Look::Psp;
        const QPointF centre(head.left() + head.height() * 1.2, head.center().y());
        QRadialGradient glow(centre, head.height() * (psp ? 2.2 : 2.4));
        glow.setColorAt(0.0, psp ? QColor(255, 255, 255, 70) : QColor(61, 125, 255, 96));
        glow.setColorAt(1.0, psp ? QColor(255, 255, 255, 0) : QColor(61, 125, 255, 0));
        painter.fillRect(head.adjusted(-8, -6, 0, 6), glow);
    } else if (g_look == Look::Switch && first) {
        // the first entry is the chosen one: the HOME menu's frame around it
        painter.save();
        const QRectF frame = entry.adjusted(-3.5, 0.5, 1.5, -1.5);
        painter.setBrush(QColor(0x3B, 0x3B, 0x3B));
        painter.setPen(QPen(QColor(0x1F, 0xE0, 0xC8, 0x50), 5));
        painter.drawRoundedRect(frame, 3, 3);
        painter.setPen(QPen(QColor(0x1F, 0xE0, 0xC8), 2));
        painter.drawRoundedRect(frame, 3, 3);
        painter.restore();
    }
    // nothing behind an entry in the other looks
}

void Style::DrawPointer(QPainter& painter, const QPointF& centre, qreal size) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal radius = size * 0.44;
    if (g_look == Look::PlayStation) {
        // a disc as the console's discs are underneath: black, a ring of light grey round the
        // hole, a sheen across it
        painter.setPen(QPen(QColor(0xB8, 0xB8, 0xBE), std::max(1.0, size * 0.04)));
        QRadialGradient face(centre, radius);
        face.setColorAt(0.0, QColor(0x3A, 0x3A, 0x44));
        face.setColorAt(1.0, QColor(0x0C, 0x0C, 0x10));
        painter.setBrush(face);
        painter.drawEllipse(centre, radius, radius);
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
        painter.setBrush(QColor(0xC8, 0xC7, 0xC1));
        painter.drawEllipse(centre, radius * 0.34, radius * 0.34);
        painter.setBrush(QColor(0x23, 0x24, 0x2B));
        painter.drawEllipse(centre, radius * 0.12, radius * 0.12);
    } else if (g_look == Look::Psp) {
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
    } else if (g_look == Look::ThreeDs) {
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
    } else if (g_look == Look::Switch) {
        // the HOME menu's frame, small: a rounded square with the word seen through it
        const auto frame = [&](qreal half) { return QRectF(centre.x() - half, centre.y() - half, half * 2, half * 2); };
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0x1F, 0xE0, 0xC8, 0x5A), size * 0.12));
        painter.drawRoundedRect(frame(size * 0.36), size * 0.16, size * 0.16);
        painter.setPen(QPen(QColor(0x1F, 0xE0, 0xC8), size * 0.07));
        painter.drawRoundedRect(frame(size * 0.32), size * 0.13, size * 0.13);
        painter.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), size * 0.025));
        painter.drawRoundedRect(frame(size * 0.26), size * 0.09, size * 0.09);
    } else if (g_look == Look::Ps2) {
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
    } else if (g_look == Look::Dreamcast) {
        // an orange spiral on a white disc, with a thin dark rim so that it shows on white
        painter.setPen(QPen(QColor(0, 0, 0, 70), std::max(1.0, size * 0.03)));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(centre, radius, radius);
        painter.setPen(QPen(QColor(0xF2, 0x7C, 0x1E), radius * 0.22, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(Spiral(centre, radius * 0.72));
    } else {
        // the program's own: a ring with a dot, white on the program's blue
        painter.setPen(QPen(Qt::white, std::max(1.5, size * 0.08)));
        painter.setBrush(QColor(0x2F, 0x5B, 0xC8, 200));
        painter.drawEllipse(centre, radius, radius);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(centre, radius * 0.22, radius * 0.22);
    }
    painter.restore();
}

void Style::DressDialog(QWidget*) {
    // The reader's windows keep the program's own look.
}

} // namespace ReaderUi
