// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The text strip (docs/furoshiki-3.md, "Sources"): the line being read from a source that says
// nothing of where its text is on screen (lines sent by another program, text copied), in a
// window of its own that stays over the desktop, so that it can lie over the game. Its words
// are looked up with the reader's own lookup (ReaderUi::LookupFlow: the popup, names, cards),
// under the pointer as over a game's text. It is moved by its head; its ground is as solid as
// the popup (the popup's opacity setting). Type and hairlines, as the shelf: no box, no shadow.

#include <functional>
#include <memory>
#include <QColor>
#include <QFont>
#include <QTextLayout>
#include <QWidget>
#include "reader/desktop/screen_text.h"

namespace ReaderUi {
class LookupFlow;
}

namespace Home {

class TextStrip : public QWidget {
    Q_OBJECT

public:
    /// The shelf's theme, as far as the strip is drawn with it.
    struct Look {
        QColor ground, ink, muted, accent, rule;
        QFont text, mono;
    };

    explicit TextStrip(QWidget* parent = nullptr);
    ~TextStrip() override;

    void SetLook(const Look& look);
    /// What stands in its head: the title, and what its source says of itself.
    void SetHead(const QString& title, const QString& source, const QString& status);
    void SetLine(const ReaderApp::PresentedLine& presented);
    /// How solid its ground is, 20 to 100; 0: its own (92).
    void SetOpacity(int percent);
    /// Its place and width as kept (QSettings), or at the foot of the screen's middle.
    void Place();
    [[nodiscard]] const ReaderApp::PresentedLine& Line() const {
        return line;
    }
    [[nodiscard]] ReaderUi::LookupFlow& Lookups() {
        return *lookups;
    }
    /// Looks up the word at `offset` (UTF-16) of the line shown: what the pointer does, for a
    /// test and the development channel. False when there is no such character.
    bool LookUpAt(int offset, bool quiet = false);
    /// Asked to end the reading (its head's cross, Escape).
    std::function<void()> close_wanted;
    /// Says a few words in its head for a moment (how a card ended).
    void Say(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void LayOut();
    /// The character under `at`: its offset in the line's text, or -1.
    [[nodiscard]] int OffsetAt(const QPointF& at) const;
    /// Where the characters [first, last] are drawn, a rectangle a row.
    [[nodiscard]] QList<QRectF> RowsOf(int first, int last) const;
    void Keep();

    Look look;
    QString title, source, status, said;
    ReaderApp::PresentedLine line;
    std::unique_ptr<QTextLayout> layout;
    QPointF text_at;
    int opacity{92};
    int mark_first{-1}, mark_last{-1};
    int hovered{-1};
    bool dragging{}, sizing{};
    QPoint drag_from;
    QRect drag_geometry;
    std::unique_ptr<ReaderUi::LookupFlow> lookups;
};

} // namespace Home
