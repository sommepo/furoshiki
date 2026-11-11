// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The reader's layer over another program's window (docs/furoshiki-3.md, "A window read by
// OCR"): where the OCR helper says where each character of the line is, the game's own text is
// what is pointed at, as in a game a core plays. A window of its own exactly over the game's
// picture, see-through, always on top, and it takes no clicks or keys: they are the game's.
// So the pointer is asked for, not listened to: the character under it is looked up at once
// with the reader's own lookup (ReaderUi::LookupFlow), the word marked by a rule in the accent.
// Nothing else is drawn: the game draws its text.

#include <memory>
#include <QColor>
#include <QTimer>
#include <QWidget>
#include "reader/desktop/screen_text.h"

namespace ReaderUi {
class LookupFlow;
}

namespace Home {

class OcrOverlay : public QWidget {
    Q_OBJECT

public:
    explicit OcrOverlay(QWidget* parent = nullptr);
    ~OcrOverlay() override;

    void SetAccent(const QColor& colour);
    /// The line on the screen and where each of its characters is (one a code point, 0-1 of
    /// the window's picture).
    void SetLine(const ReaderApp::PresentedLine& presented, const QList<QRectF>& boxes);
    /// Where the window's picture is on the screen, and whether the line stands in it now:
    /// shown only with both.
    void SetPlace(const QRect& place, bool on_screen);
    [[nodiscard]] const ReaderApp::PresentedLine& Line() const {
        return line;
    }
    [[nodiscard]] ReaderUi::LookupFlow& Lookups() {
        return *lookups;
    }
    /// Looks up the word at `offset` (UTF-16) of the line: what the pointer over that character
    /// does. False when that character has no place.
    bool LookUpAt(int offset, bool quiet = false);
    /// The character under a point of the screen: its offset in the line, or -1.
    [[nodiscard]] int OffsetAt(const QPoint& global) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void Hover();
    /// Where the character at `offset` is drawn, in this window; empty when it has no place.
    [[nodiscard]] QRectF BoxOf(int offset) const;

    QColor accent{0xff, 0x4d, 0x2e};
    ReaderApp::PresentedLine line;
    QList<QRectF> boxes;
    QList<int> box_of; ///< For each UTF-16 unit of the text, its code point's box
    int mark_first{-1}, mark_last{-1};
    int hovered{-1};
    QTimer pointer;
    std::unique_ptr<ReaderUi::LookupFlow> lookups;
};

} // namespace Home
