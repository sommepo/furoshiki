// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QWidget>

namespace Home {

/**
 * Every choice of a setting at once, over the window: the window behind blurred and dimmed, a
 * card in the middle with the choices as rows, the one in use marked. A click or Enter takes
 * one, a click outside or Escape leaves it as it was; the wheel and the arrows go along a long
 * list. It covers the window it is opened over and takes its keys until it closes.
 */
class ChoicePicker : public QWidget {
public:
    struct Colours {
        QColor card, txt, sel, dim, accent, rule;
        QFont title, row; ///< At the window's size already
        double scale{1}; ///< The window's pixels for one of the page's
    };

    /// Opens over `over` (its whole area); `picked` hears the index taken, never a cancel.
    /// `focused` (optional) hears the index highlighted as the pointer or keys move over the list,
    /// and -1 when the picker closes without picking. `keep_clear` (optional) keeps a bottom band
    /// (such as the live popup preview) uncovered by placing the card above it.
    static ChoicePicker* Open(QWidget* over, const QString& title, const QStringList& choices, int now,
                              const Colours& colours, std::function<void(int)> picked,
                              std::function<void(int)> focused = {}, const QRect& keep_clear = {});

    /// A key handed in by the window (a controller's, through its own key handling): true when
    /// the picker took it.
    bool Key(int key);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    ChoicePicker(QWidget* over, const QString& title, const QStringList& choices, int now, const Colours& colours,
                 std::function<void(int)> picked, std::function<void(int)> focused, const QRect& keep_clear);
    void Blur();
    [[nodiscard]] QRectF Card() const;
    [[nodiscard]] int Visible() const;
    [[nodiscard]] int RowAt(const QPointF& at) const;
    void Show(int index);
    void Take(int index);
    void Leave();

    QString title;
    QStringList choices;
    int now{};
    int chosen{};
    int hover{-1};
    double top{};    ///< The first row shown, in rows (the wheel moves it smoothly)
    double target{}; ///< Where `top` glides to
    Colours colours;
    std::function<void(int)> picked;
    std::function<void(int)> focused;
    QRect keep_clear;
    QPixmap behind;
    QElapsedTimer clock;
    QTimer frames;
    bool leaving{};
    qint64 left_at{};
};

} // namespace Home
