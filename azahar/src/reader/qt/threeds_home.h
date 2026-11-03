// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QWidget>
#include "common/common_types.h"

class GameList;
class QDialog;
class QMainWindow;
class QPainter;

namespace ReaderUi {

class ReaderController;

/**
 * The 3DS's look in the desktop app, as the Android app has it: the console's pale grey and
 * its blue in every window (dark with a dark system), and the reader's windows laid out as
 * the console's settings pages.
 */
namespace ThreeDsLook {
struct Colours {
    /// The console's body, around the two screens.
    QColor body;
    /// A screen's ground, from its top to its bottom.
    QColor screen_top;
    QColor screen_bottom;
    /// A window's ground, and the strips along a settings page's top and bottom.
    QColor ground;
    QColor strip;
    QColor text;
    QColor faint;
    QColor line;
    QColor accent;
    /// The two colours a chosen frame glows between.
    QColor glow_a;
    QColor glow_b;
    QColor raised;
    QColor pressed;
};

/// The app has the console's colours (a setting of the reader's; on unless switched off).
[[nodiscard]] bool Wanted();
void SetWanted(bool wanted);
/// "auto" (the system's light or dark), "light" or "dark".
[[nodiscard]] QString Theme();
void SetTheme(const QString& theme);
[[nodiscard]] bool Dark();
[[nodiscard]] const Colours& Current();
[[nodiscard]] QString StyleSheet(const Colours& colours);
/**
 * Gives the app the look when it is wanted; true when it did. Called by the main window
 * after it has set its own theme, which stands when this returns false.
 */
bool Apply(QMainWindow* main_window);
/**
 * Lays a window of the reader's out as a page of the console's settings: a strip with its
 * name, the window's own content, a strip with a wide round Back button. Does nothing when
 * the look is off.
 */
void Dress(QDialog* dialog);
} // namespace ThreeDsLook

/**
 * The game list in the manner of the 3DS's HOME menu, on two screens as the console has them:
 * the upper one shows the chosen game (a strip with the date and the time, the game's picture
 * slowly bobbing, its name, its maker and whether its text can be read), the lower one the
 * games' icons as tiles in a grid, the chosen one in a frame that glows, small buttons along
 * its top and a wide "Open" button along its bottom. The desktop sibling of the Android app's
 * ThreeDsHome.
 *
 * It lies over the main window's game list and shows whenever that (or its "no games yet"
 * stand-in) would: the list stays what it is underneath, and is what "The emulator's game
 * list" brings back.
 */
namespace HomeLink {
struct About;
}
/// What Reader Home is told of this app (reader_link.h), and the reading statistics for its
/// sums: once at the start and whenever a game has stopped.
const HomeLink::About& HomeLinkAbout();
void TellHomeStats();

class ThreeDsHome : public QWidget {
    Q_OBJECT

public:
    struct Game {
        QString path;
        QString name;
        QString maker;
        u64 program_id{};
        QPixmap picture;
        bool favourite{};
        /// The game has a text profile.
        bool readable{};
    };

    ThreeDsHome(QMainWindow* main_window_, QWidget* area_, GameList* games_, QWidget* placeholder_,
                ReaderController* controller_);

    /// The HOME menu is the game list (a setting of the reader's; on unless switched off).
    [[nodiscard]] static bool Wanted();
    void SetWanted(bool wanted);

    // ---- For the development control channel ----
    [[nodiscard]] const QList<Game>& Games() const {
        return shown;
    }
    [[nodiscard]] int Chosen() const {
        return chosen;
    }
    /// What the two screens show and what is chosen, with where things are in the widget.
    [[nodiscard]] QJsonObject Describe() const;
    /// Where a game's tile, a small button or the Open button is right now.
    [[nodiscard]] QRectF TileRect(int index) const;
    [[nodiscard]] QRectF ButtonRect(int index) const;
    [[nodiscard]] QRectF OpenRect() const;
    /// The two screens.
    [[nodiscard]] QRectF UpperRect() const;
    [[nodiscard]] QRectF LowerRect() const;

signals:
    void WantedChanged(bool wanted);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Button {
        QString key;
        QString name;
    };

    void Reload();
    void ApplyFilter();
    void Follow();
    void Choose(int index);
    void ChooseButton(int index);
    void Start(int index);
    void Activate(int index);
    void Options(int index, const QPoint& global);
    void Tick();
    void Animate();
    [[nodiscard]] qreal Unit() const;
    [[nodiscard]] qreal ScrollTarget() const;
    void DrawUpper(QPainter& painter, const QRectF& screen, qreal unit);
    void DrawLower(QPainter& painter, const QRectF& screen, qreal unit);
    void DrawIcon(QPainter& painter, const QString& key, const QRectF& box,
                  const QColor& colour) const;

    QMainWindow* main_window;
    QWidget* area;
    GameList* games;
    QWidget* placeholder;
    ReaderController* controller;

    QList<Game> all;
    QList<Game> shown;
    QList<Button> buttons;
    QString filter;

    int chosen{};
    int button{};
    bool on_buttons{};
    /// What a press of the mouse began on: a game (0…), a small button (-2 - index), the
    /// Open button (-100) or nothing (-1).
    int pressed{-1};

    QTimer reload_timer;
    QTimer frame_timer;
    QElapsedTimer clock;
    qint64 last_tick_ms{};
    /// How far the grid is moved to the left, in columns.
    qreal scroll{};
    /// 0 when the chosen one was just chosen, 1 once it has come to rest: it hops.
    qreal arrival{1.0};
    /// 1 while something is held down, falling back to 0.
    qreal press{};
};

} // namespace ReaderUi
