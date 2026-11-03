// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <functional>
#include <QColor>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QWidget>

class QDialog;
class QMainWindow;
class QPainter;

namespace ReaderUi {

/**
 * The look of the PlayStation 2's own system menu in the desktop app: its deep blue dark, its
 * pale blue type and the glow of what is chosen in every window, and the reader's windows laid
 * out as pages of its System Configuration.
 */
namespace Ps2Look {
struct Colours {
    QColor ground;
    QColor ground_low;
    QColor text;
    QColor faint;
    QColor line;
    /// What is chosen, and the light it throws.
    QColor accent;
    QColor glow;
    QColor raised;
    QColor pressed;
};

/// The app has the console's colours (a setting of the reader's; on unless switched off).
[[nodiscard]] bool Wanted();
void SetWanted(bool wanted);
[[nodiscard]] const Colours& Current();
[[nodiscard]] QString StyleSheet(const Colours& colours);
/**
 * Gives the app the look when it is wanted; true when it did. Called by the main window
 * after it has set its own theme, which stands when this returns false.
 */
bool Apply(QMainWindow* main_window);
/**
 * Lays a window of the reader's out as a page of the console's System Configuration: its name
 * in the top left corner, the window's own content, and along the bottom what the buttons
 * do. Does nothing when the look is off.
 */
void Dress(QDialog* dialog);
} // namespace Ps2Look

/**
 * The game list in the manner of the PlayStation 2's system menu: its main menu (Browser and
 * System Configuration beside the slowly turning lights), the Browser with the games as
 * pictures afloat in the haze and the chosen one's name in the top right corner, and System
 * Configuration as a list of names and values. The same screens as the Android app's.
 *
 * It lies over the main window's game list and shows whenever that would: the list stays what
 * it is underneath, and is what "The emulator's game list" brings back. What it does to the
 * rest of the app it does through the functions it is given.
 */
class Ps2Home : public QWidget {
    Q_OBJECT

public:
    struct Game {
        QString path;
        QString name;
        QString serial;
        QPixmap picture;
        /// The game has a text profile.
        bool readable{};
    };
    /// A line of System Configuration: a name, what it is set to (may be empty), what
    /// choosing it does.
    struct Setting {
        QString key;
        QString name;
        std::function<QString()> value;
        std::function<void()> choose;
    };
    enum class Screen { Menu, Browser, Configuration };

    Ps2Home(QMainWindow* main_window_, QWidget* list_);

    /// The menu is the game list (a setting of the reader's; on unless switched off).
    [[nodiscard]] static bool Wanted();
    void SetWanted(bool wanted);

    void SetGames(const QList<Game>& games_);
    void SetSettings(const QList<Setting>& settings_);
    std::function<void(const QString& path)> start_game;
    std::function<void()> add_games;
    std::function<void(const QString& path, const QPoint& global)> game_options;

    // ---- For the development control channel ----
    [[nodiscard]] QJsonObject Describe() const;
    [[nodiscard]] QRectF TileRect(int index) const;

signals:
    void WantedChanged(bool wanted);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void Follow();
    void Go(Screen next);
    void Back();
    void Enter();
    void Move(int dx, int dy);
    void ApplyFilter();
    void Tick();
    [[nodiscard]] qreal Unit() const;
    [[nodiscard]] int Columns() const;
    [[nodiscard]] QRectF MenuRect(int index) const;
    [[nodiscard]] QRectF SettingRect(int index) const;
    [[nodiscard]] int HitTest(const QPointF& at) const;
    /// How many lines of System Configuration fit.
    [[nodiscard]] int SettingRows() const;
    void DrawGround(QPainter& painter, Screen which, qreal alpha);
    void DrawLights(QPainter& painter, const QPointF& centre, qreal radius, qreal alpha);
    void DrawMenu(QPainter& painter, qreal alpha);
    void DrawBrowser(QPainter& painter, qreal alpha);
    void DrawConfiguration(QPainter& painter, qreal alpha);
    void DrawHints(QPainter& painter, const QStringList& hints, qreal alpha);
    void DrawGlowText(QPainter& painter, const QRectF& rect, int flags, const QString& text,
                      bool chosen, qreal alpha);

    QMainWindow* main_window;
    QWidget* list;

    QList<Game> all;
    QList<Game> shown;
    QList<Setting> settings;
    QString filter;

    Screen screen{Screen::Menu};
    Screen screen_before{Screen::Menu};
    /// 0 when the screen was just changed, 1 once the new one is all there.
    qreal arrival{1.0};
    int menu_choice{};
    int game_choice{};
    int setting_choice{};
    /// The first line of System Configuration that is shown.
    int setting_scroll{};
    /// 0 when the chosen thing was just chosen, 1 once it has come to rest.
    qreal choice_arrival{1.0};
    /// How far the Browser's rows are moved up, in rows.
    qreal scroll{};

    QTimer frame_timer;
    QElapsedTimer clock;
    qint64 last_tick_ms{};
};

/// The menu of the running app, once the reader was installed (reader/qt/host.cpp).
[[nodiscard]] Ps2Home* Home();

} // namespace ReaderUi
