// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QDate>
#include <QDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QSet>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTime>
#include <QWheelEvent>
#include "citra_qt/game_list.h"
#include "citra_qt/game_list_p.h"
#include <QDir>
#include <QJsonObject>
#include "reader/qt/reader_controller.h"
#include "reader/core/emulator.h"
#include "reader/qt/reader_games.h"
#include "reader/qt/reader_link.h"
#include "reader/qt/reader_session.h"
#include "reader/qt/threeds_home.h"

namespace ReaderUi {

namespace {

const QString KeyLook = QStringLiteral("reader_3ds_look");
const QString KeyTheme = QStringLiteral("reader_3ds_theme");
const QString KeyHome = QStringLiteral("reader_3ds_home");

// The menu is drawn in the pixels of the console's own screens: the upper one 400 by 240,
// the lower one 320 by 240, the hinge between them.
constexpr qreal UpperWidth = 400;
constexpr qreal LowerWidth = 320;
constexpr qreal ScreenHeight = 240;
constexpr qreal Hinge = 16;
constexpr qreal Margin = 14;
constexpr int Rows = 2;
constexpr int Columns = 4;
constexpr qreal Tile = 62;
constexpr qreal TileStep = 72;
constexpr qreal GridLeft = 21;
constexpr qreal GridTop = 58;
constexpr qreal BarHeight = 26;
constexpr int PressedOpen = -100;
constexpr double Pi = 3.14159265358979323846;

ReaderApp::ReaderSettings& Stored() {
    return ReaderSession::Instance().Settings();
}

QString Hex(const QColor& colour) {
    return colour.name(QColor::HexRgb);
}

QColor Mix(const QColor& a, const QColor& b, qreal t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
        a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QColor Alpha(QColor colour, qreal alpha) {
    colour.setAlphaF(std::clamp(alpha, 0.0, 1.0));
    return colour;
}

/// The strip along the top of a dressed window, with the window's name.
class Strip : public QWidget {
public:
    Strip(QWidget* parent, bool top_) : QWidget(parent), top{top_} {
        setAttribute(Qt::WA_StyledBackground, false);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto& colours = ThreeDsLook::Current();
        QLinearGradient shade(0, 0, 0, height());
        shade.setColorAt(0, Mix(colours.strip, Qt::white, 0.12));
        shade.setColorAt(1, colours.strip);
        painter.fillRect(rect(), shade);
        painter.setPen(QPen(colours.line, 1));
        const qreal y = top ? height() - 0.5 : 0.5;
        painter.drawLine(QPointF(0, y), QPointF(width(), y));
    }

private:
    bool top;
};

/// Keeps the two strips of a dressed window along its top and bottom edges.
class StripPlacer : public QObject {
public:
    StripPlacer(QWidget* window_, QWidget* top_, QWidget* bottom_, int top_height_,
                int bottom_height_)
        : QObject(window_), window{window_}, top{top_}, bottom{bottom_}, top_height{top_height_},
          bottom_height{bottom_height_} {
        Place();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == window &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            Place();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void Place() {
        top->setGeometry(0, 0, window->width(), top_height);
        bottom->setGeometry(0, window->height() - bottom_height, window->width(), bottom_height);
        top->raise();
        bottom->raise();
    }

    QWidget* window;
    QWidget* top;
    QWidget* bottom;
    int top_height;
    int bottom_height;
};

} // namespace

// ---- The look ----

namespace ThreeDsLook {

bool Wanted() {
    return Stored().Value(KeyLook, true).toBool();
}

void SetWanted(bool wanted) {
    Stored().SetValue(KeyLook, wanted);
    Stored().Sync();
}

QString Theme() {
    const QString theme = Stored().Value(KeyTheme, QStringLiteral("auto")).toString();
    return theme == QStringLiteral("light") || theme == QStringLiteral("dark")
               ? theme
               : QStringLiteral("auto");
}

void SetTheme(const QString& theme) {
    Stored().SetValue(KeyTheme, theme);
    Stored().Sync();
}

bool Dark() {
    const QString theme = Theme();
    if (theme != QStringLiteral("auto")) {
        return theme == QStringLiteral("dark");
    }
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

const Colours& Current() {
    static const Colours light{
        .body = QColor(0xC9, 0xCE, 0xD6),
        .screen_top = QColor(0xFB, 0xFC, 0xFD),
        .screen_bottom = QColor(0xDF, 0xE5, 0xEC),
        .ground = QColor(0xEE, 0xF0, 0xF3),
        .strip = QColor(0xD3, 0xD8, 0xDF),
        .text = QColor(0x32, 0x36, 0x40),
        .faint = QColor(0x7E, 0x85, 0x92),
        .line = QColor(0xB4, 0xBB, 0xC6),
        .accent = QColor(0x1C, 0x7F, 0xD6),
        .glow_a = QColor(0x28, 0xA0, 0xF0),
        .glow_b = QColor(0x9F, 0xE0, 0xFF),
        .raised = QColor(0xFF, 0xFF, 0xFF),
        .pressed = QColor(0xD2, 0xEA, 0xFB),
    };
    static const Colours dark{
        .body = QColor(0x17, 0x19, 0x1D),
        .screen_top = QColor(0x3A, 0x40, 0x4A),
        .screen_bottom = QColor(0x25, 0x29, 0x30),
        .ground = QColor(0x2A, 0x2D, 0x33),
        .strip = QColor(0x1F, 0x22, 0x27),
        .text = QColor(0xF1, 0xF3, 0xF6),
        .faint = QColor(0x9A, 0xA1, 0xAD),
        .line = QColor(0x55, 0x5C, 0x68),
        .accent = QColor(0x5C, 0xBC, 0xF7),
        .glow_a = QColor(0x3C, 0xAE, 0xF5),
        .glow_b = QColor(0xC2, 0xEC, 0xFF),
        .raised = QColor(0x41, 0x47, 0x52),
        .pressed = QColor(0x29, 0x4A, 0x63),
    };
    return Dark() ? dark : light;
}

QString StyleSheet(const Colours& c) {
    const bool dark = c.ground.lightness() < 128;
    const QString field = Hex(dark ? QColor(0x35, 0x39, 0x42) : QColor(0xFF, 0xFF, 0xFF));
    const QString hover = Hex(Mix(c.ground, c.glow_a, dark ? 0.22 : 0.16));
    QString sheet = QStringLiteral(R"(
QWidget { color: %text%; }
QMainWindow, QDialog, QMenuBar, QStatusBar, QTabWidget::pane, QStackedWidget, QScrollArea,
QScrollArea > QWidget > QWidget, QGroupBox, QDockWidget { background: %ground%; }
QMenuBar { background: %strip%; border-bottom: 1px solid %line%; }
QMenuBar::item { padding: 5px 11px; background: transparent; border-radius: 9px; }
QMenuBar::item:selected, QMenuBar::item:pressed { color: %accent%; background: %raised%; }
QMenu { background: %raised%; border: 1px solid %line%; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 22px; border-radius: 6px; }
QMenu::item:selected { color: %accent%; background: %hover%; }
QMenu::item:disabled { color: %faint%; }
QMenu::separator { height: 1px; background: %line%; margin: 5px 10px; }
QToolTip { color: %text%; background: %raised%; border: 1px solid %line%; border-radius: 6px;
    padding: 4px 8px; }
QLabel { background: transparent; }
QGroupBox { border: 1px solid %line%; border-radius: 10px; margin-top: 20px; padding-top: 8px;
    background: %panel%; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; top: 2px; color: %faint%; }
QPushButton, QToolButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
    stop:0 %raised%, stop:1 %button_low%); border: 1px solid %line%; border-radius: 13px;
    padding: 6px 18px; min-height: 18px; }
QToolButton { padding: 5px 9px; border-radius: 9px; }
QPushButton:hover, QToolButton:hover { border: 2px solid %glow%; padding: 5px 17px; }
QToolButton:hover { padding: 4px 8px; }
QPushButton:focus { border: 2px solid %glow%; padding: 5px 17px; color: %accent%; }
QPushButton:pressed, QToolButton:pressed { background: %pressed%; }
QPushButton:disabled, QToolButton:disabled { color: %faint%; background: %ground%; }
QLineEdit, QPlainTextEdit, QTextEdit, QKeySequenceEdit { background: %field%;
    border: 1px solid %line%; border-radius: 8px; padding: 5px 8px;
    selection-background-color: %glow%; selection-color: %field%; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border: 2px solid %glow%;
    padding: 4px 7px; }
QComboBox QAbstractItemView { background: %raised%; border: 1px solid %line%;
    selection-background-color: %hover%; selection-color: %accent%; outline: none; }
QCheckBox, QRadioButton { spacing: 9px; padding: 4px 0; background: transparent; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px;
    border: 2px solid %faint%; border-radius: 5px; background: %field%; }
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %glow%;
    border: 2px solid %glow%; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border: 2px solid %glow%; }
QCheckBox:hover, QRadioButton:hover, QCheckBox:focus, QRadioButton:focus { color: %accent%; }
QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget { background: %field%;
    alternate-background-color: %alt%; border: 1px solid %line%; border-radius: 8px;
    outline: none; }
QListView::item, QTreeView::item, QListWidget::item { padding: 6px 4px; }
QListView::item:hover, QTreeView::item:hover, QTableView::item:hover { background: %hover%; }
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {
    color: %accent%; background: %hover%; }
QHeaderView::section { background: %strip%; color: %faint%; border: none;
    border-bottom: 1px solid %line%; padding: 6px 8px; }
QTabBar::tab { background: %strip%; color: %faint%; padding: 7px 16px; border: 1px solid %line%;
    border-bottom: none; border-top-left-radius: 9px; border-top-right-radius: 9px;
    margin-right: 2px; }
QTabBar::tab:hover { color: %text%; }
QTabBar::tab:selected { color: %accent%; background: %ground%; }
QTabWidget::pane { border: none; border-top: 1px solid %line%; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: %line%; border-radius: 3px; min-height: 28px; min-width: 28px; }
QScrollBar::handle:hover { background: %glow%; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent; border: none; width: 0; height: 0; }
QSlider::groove:horizontal { height: 6px; background: %line%; border-radius: 3px; }
QSlider::sub-page:horizontal { background: %glow%; border-radius: 3px; }
QSlider::handle:horizontal { width: 18px; margin: -7px 0; border-radius: 9px;
    background: %raised%; border: 2px solid %glow%; }
QProgressBar { background: %field%; border: 1px solid %line%; border-radius: 7px;
    text-align: center; }
QProgressBar::chunk { background: %glow%; border-radius: 6px; }
QStatusBar { background: %strip%; border-top: 1px solid %line%; color: %faint%; }
QStatusBar::item { border: none; }
QStatusBar QPushButton, QStatusBar QToolButton { border: none; background: transparent;
    padding: 2px 8px; min-height: 0; }
QLabel#reader_3ds_title { font-size: 15pt; font-weight: 600; padding: 0 20px; }
QPushButton#reader_3ds_back { border-radius: 17px; min-width: 220px; min-height: 22px;
    font-weight: 600; }
)");
    const QList<QPair<QString, QString>> values{
        {QStringLiteral("%ground%"), Hex(c.ground)},
        {QStringLiteral("%strip%"), Hex(c.strip)},
        {QStringLiteral("%text%"), Hex(c.text)},
        {QStringLiteral("%faint%"), Hex(c.faint)},
        {QStringLiteral("%line%"), Hex(c.line)},
        {QStringLiteral("%accent%"), Hex(c.accent)},
        {QStringLiteral("%glow%"), Hex(c.glow_a)},
        {QStringLiteral("%raised%"), Hex(c.raised)},
        {QStringLiteral("%button_low%"), Hex(Mix(c.raised, c.strip, 0.55))},
        {QStringLiteral("%pressed%"), Hex(c.pressed)},
        {QStringLiteral("%field%"), field},
        {QStringLiteral("%hover%"), hover},
        {QStringLiteral("%panel%"), Hex(Mix(c.ground, c.raised, 0.35))},
        {QStringLiteral("%alt%"), Hex(Mix(QColor(field), c.text, 0.04))},
    };
    for (const auto& value : values) {
        sheet.replace(value.first, value.second);
    }
    return sheet;
}

bool Apply(QMainWindow* main_window) {
    // The system's own style and colours, to go back to when the look is switched off.
    static QString style_before;
    static QPalette palette_before;
    static bool applied = false;
    if (!Wanted()) {
        if (applied) {
            applied = false;
            if (QStyle* style = QStyleFactory::create(style_before)) {
                QApplication::setStyle(style);
            }
            QApplication::setPalette(palette_before);
        }
        return false;
    }
    const bool dark = Dark();
    // Setting the colours tells every window that they changed, and the main window sets its
    // theme anew when told: once is enough.
    static bool busy = false;
    static bool applied_dark = false;
    if (busy || (applied && applied_dark == dark)) {
        return true;
    }
    busy = true;
    if (!applied) {
        applied = true;
        style_before = QApplication::style()->objectName();
        palette_before = QApplication::palette();
    }
    applied_dark = dark;
    const Colours& c = Current();
    // One style on every system, so that the colours below mean the same everywhere.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }
    QPalette palette;
    palette.setColor(QPalette::Window, c.ground);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, dark ? QColor(0x35, 0x39, 0x42) : QColor(0xFF, 0xFF, 0xFF));
    palette.setColor(QPalette::AlternateBase, Mix(c.ground, c.text, 0.04));
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.raised);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::ToolTipBase, c.raised);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Highlight, c.glow_a);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::PlaceholderText, c.faint);
    for (const QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, c.faint);
    }
    QApplication::setPalette(palette);
    qApp->setStyleSheet(StyleSheet(c));
    if (main_window) {
        main_window->setStyleSheet({});
    }
    busy = false;
    return true;
}

void Dress(QDialog* dialog) {
    if (!Wanted() || !dialog || dialog->property("reader_3ds_dressed").toBool()) {
        return;
    }
    auto* layout = qobject_cast<QBoxLayout*>(dialog->layout());
    if (!layout || layout->direction() != QBoxLayout::TopToBottom) {
        return;
    }
    dialog->setProperty("reader_3ds_dressed", true);
    // The window's own layout stays what it is, with room left above and below it; the two
    // strips lie in that room from edge to edge and follow the window's size.
    constexpr int TopHeight = 46;
    constexpr int BottomHeight = 52;
    auto* top = new Strip(dialog, true);
    auto* top_layout = new QHBoxLayout(top);
    top_layout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(dialog->windowTitle(), top);
    title->setObjectName(QStringLiteral("reader_3ds_title"));
    top_layout->addWidget(title);
    top_layout->addStretch(1);

    auto* bottom = new Strip(dialog, false);
    auto* bottom_layout = new QHBoxLayout(bottom);
    bottom_layout->setContentsMargins(0, 0, 0, 0);
    auto* back = new QPushButton(QObject::tr("Back"), bottom);
    back->setObjectName(QStringLiteral("reader_3ds_back"));
    back->setAutoDefault(false);
    back->setFocusPolicy(Qt::NoFocus);
    back->setCursor(Qt::PointingHandCursor);
    QObject::connect(back, &QPushButton::clicked, dialog, &QDialog::reject);
    bottom_layout->addStretch(1);
    bottom_layout->addWidget(back);
    bottom_layout->addStretch(1);

    layout->setContentsMargins(22, 0, 22, 0);
    layout->insertSpacing(0, TopHeight + 14);
    layout->addSpacing(BottomHeight + 12);
    dialog->installEventFilter(new StripPlacer(dialog, top, bottom, TopHeight, BottomHeight));
    dialog->resize(dialog->width() + 20, dialog->height() + TopHeight + BottomHeight + 20);
}

} // namespace ThreeDsLook

// ---- The HOME menu ----

ThreeDsHome::ThreeDsHome(QMainWindow* main_window_, QWidget* area_, GameList* games_,
                         QWidget* placeholder_, ReaderController* controller_)
    : QWidget(area_), main_window{main_window_}, area{area_}, games{games_},
      placeholder{placeholder_}, controller{controller_} {
    setObjectName(QStringLiteral("reader_3ds_home"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    buttons = {
        {QStringLiteral("settings"), tr("System Settings")},
        {QStringLiteral("reader"), tr("Reader")},
        {QStringLiteral("statistics"), tr("Statistics")},
        {QStringLiteral("games"), tr("Supported games")},
        {QStringLiteral("folders"), tr("Game folders")},
        {QStringLiteral("view"), tr("View")},
    };

    reload_timer.setSingleShot(true);
    reload_timer.setInterval(120);
    connect(&reload_timer, &QTimer::timeout, this, &ThreeDsHome::Reload);
    QStandardItemModel* model = games->GetModel();
    const auto later = [this] { reload_timer.start(); };
    connect(model, &QStandardItemModel::rowsInserted, this, later);
    connect(model, &QStandardItemModel::rowsRemoved, this, later);
    connect(model, &QStandardItemModel::modelReset, this, later);
    connect(model, &QStandardItemModel::itemChanged, this, later);
    connect(games, &GameList::PopulatingCompleted, this, later);

    // The picture bobs and the frame glows for as long as the menu is in front.
    frame_timer.setInterval(16);
    connect(&frame_timer, &QTimer::timeout, this, &ThreeDsHome::Tick);
    clock.start();

    // The list underneath says when a game list is to be seen at all.
    for (QWidget* watched :
         {static_cast<QWidget*>(games), placeholder, area, static_cast<QWidget*>(main_window)}) {
        watched->installEventFilter(this);
    }
    for (QWidget* child : games->findChildren<QWidget*>()) {
        child->installEventFilter(this);
    }
    Reload();
    Follow();
}

bool ThreeDsHome::Wanted() {
    return Stored().Value(KeyHome, true).toBool();
}

void ThreeDsHome::SetWanted(bool wanted) {
    if (wanted != Wanted()) {
        Stored().SetValue(KeyHome, wanted);
        Stored().Sync();
    }
    Follow();
    emit WantedChanged(wanted);
}

void ThreeDsHome::Follow() {
    const bool list_wanted = !games->isHidden() || !placeholder->isHidden();
    const bool show = Wanted() && list_wanted;
    setGeometry(area->rect());
    setEnabled(games->isEnabled());
    if (show != !isHidden()) {
        setVisible(show);
    }
    if (show) {
        raise();
        if (!QApplication::activeModalWidget() && window()->isActiveWindow()) {
            setFocus();
        }
    }
}

bool ThreeDsHome::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::EnabledChange:
    case QEvent::Resize:
        if (watched == games || watched == placeholder || watched == area) {
            QTimer::singleShot(0, this, &ThreeDsHome::Follow);
        }
        break;
    case QEvent::WindowActivate:
        // Back from one of the app's other windows: the keys are the menu's again.
        if (watched == main_window) {
            QTimer::singleShot(0, this, &ThreeDsHome::Follow);
        }
        break;
    case QEvent::KeyPress:
        if (watched == main_window) {
            break;
        }
        // Keys that arrive at the list underneath are the menu's while it is in front.
        if (isVisible() && watched != this) {
            keyPressEvent(static_cast<QKeyEvent*>(event));
            return true;
        }
        break;
    case QEvent::FocusIn:
        if (isVisible() && watched != this && watched != area && watched != main_window) {
            QTimer::singleShot(0, this, [this] {
                if (isVisible()) {
                    setFocus();
                }
            });
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

const HomeLink::About& HomeLinkAbout() {
    static const HomeLink::About about{QStringLiteral("azahar-reader"), QStringLiteral("Azahar Reader"),
                                       QStringLiteral("Nintendo 3DS"),
                                       {QStringLiteral("-f"), QStringLiteral("%ROM%")}, {}};
    return about;
}

void TellHomeStats() {
    auto& session = ReaderSession::Instance();
    ReaderApp::ReaderDatabase* database = &session.Database();
    session.AfterWrites([database] {
        const QJsonObject record = database->StatsRecord();
        HomeLink::Write(HomeLinkAbout(), nullptr, &record);
    });
}

void ThreeDsHome::Reload() {
    const QString chosen_path = shown.value(chosen).path;
    all.clear();
    QSet<QString> seen;
    const QStandardItemModel* model = games->GetModel();
    const auto take = [this, &seen](const QStandardItem* item, bool favourite) {
        if (!item || item->data(GameListItem::TypeRole).toInt() !=
                         static_cast<int>(GameListItemType::Game)) {
            return;
        }
        const QString path = item->data(GameListItemPath::FullPathRole).toString();
        if (path.isEmpty() || seen.contains(path)) {
            return;
        }
        seen.insert(path);
        Game game;
        game.path = path;
        // A game says its name in Japanese where it has one: that is the one to read.
        game.name = item->data(GameListItemPath::ReaderJapaneseTitleRole).toString().trimmed();
        if (game.name.isEmpty()) {
            game.name = item->data(GameListItemPath::TitleRole).toString().trimmed();
        }
        if (game.name.isEmpty()) {
            game.name = QFileInfo(path).completeBaseName();
        }
        game.name.replace(u'\n', u' ');
        game.maker = item->data(GameListItemPath::ReaderPublisherRole).toString().trimmed();
        game.program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
        game.picture = item->data(GameListItemPath::ReaderPictureRole).value<QPixmap>();
        if (game.picture.isNull()) {
            game.picture = item->data(Qt::DecorationRole).value<QPixmap>();
        }
        game.favourite = favourite;
        game.readable = Games::Covers(QString::fromStdString(Reader::Emu::GameId(game.program_id)));
        all.append(game);
    };
    // Favourites are the list's first folder, and come first here too.
    for (int row = 0; row < model->rowCount(); ++row) {
        const QStandardItem* top = model->item(row, 0);
        if (!top) {
            continue;
        }
        const bool favourites = top->data(GameListItem::TypeRole).toInt() ==
                                static_cast<int>(GameListItemType::Favorites);
        take(top, false);
        for (int child = 0; child < top->rowCount(); ++child) {
            take(top->child(child, 0), favourites);
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const Game& a, const Game& b) {
        if (a.favourite != b.favourite) {
            return a.favourite;
        }
        return QString::localeAwareCompare(a.name.toLower(), b.name.toLower()) < 0;
    });
    // Reader Home hears of the games whose text can be read, each with its picture as a file.
    QList<HomeLink::Game> readable;
    const QString pictures = HomeLink::Folder() + QStringLiteral("/covers");
    for (const Game& game : all) {
        if (!game.readable) {
            continue;
        }
        const QString id = QStringLiteral("%1").arg(game.program_id, 16, 16, QLatin1Char('0')).toUpper();
        QString cover;
        if (!game.picture.isNull() && QDir().mkpath(pictures)) {
            cover = pictures + QStringLiteral("/azahar-reader-") + id + QStringLiteral(".png");
            if (!QFileInfo::exists(cover) && !game.picture.save(cover, "PNG")) {
                cover.clear();
            }
        }
        readable.append({id, game.name, QDir::toNativeSeparators(game.path), QDir::toNativeSeparators(cover)});
    }
    HomeLink::Write(HomeLinkAbout(), &readable, nullptr);
    static bool stats_told = false;
    if (!stats_told) {
        stats_told = true;
        TellHomeStats();
    }
    ApplyFilter();
    for (int index = 0; index < shown.size(); ++index) {
        if (shown.at(index).path == chosen_path) {
            chosen = index;
        }
    }
    scroll = ScrollTarget();
    update();
}

void ThreeDsHome::ApplyFilter() {
    shown.clear();
    for (const Game& game : all) {
        if (filter.isEmpty() || game.name.contains(filter, Qt::CaseInsensitive) ||
            QFileInfo(game.path).fileName().contains(filter, Qt::CaseInsensitive)) {
            shown.append(game);
        }
    }
    chosen = std::clamp(chosen, 0, std::max(0, static_cast<int>(shown.size()) - 1));
}

// ---- Geometry ----

qreal ThreeDsHome::Unit() const {
    const qreal wide = width() / (UpperWidth + 2 * Margin);
    const qreal tall = height() / (2 * ScreenHeight + Hinge + 2 * Margin);
    return std::max(0.4, std::min(wide, tall));
}

QRectF ThreeDsHome::UpperRect() const {
    const qreal unit = Unit();
    const qreal total = (2 * ScreenHeight + Hinge) * unit;
    return QRectF((width() - UpperWidth * unit) / 2, (height() - total) / 2, UpperWidth * unit,
                  ScreenHeight * unit);
}

QRectF ThreeDsHome::LowerRect() const {
    const qreal unit = Unit();
    const QRectF upper = UpperRect();
    return QRectF((width() - LowerWidth * unit) / 2, upper.bottom() + Hinge * unit,
                  LowerWidth * unit, ScreenHeight * unit);
}

qreal ThreeDsHome::ScrollTarget() const {
    const int column = chosen / Rows;
    const int columns = (static_cast<int>(shown.size()) + Rows - 1) / Rows;
    qreal target = std::round(scroll);
    if (column < target) {
        target = column;
    }
    if (column > target + Columns - 1) {
        target = column - (Columns - 1);
    }
    return std::clamp(target, 0.0, static_cast<qreal>(std::max(0, columns - Columns)));
}

QRectF ThreeDsHome::TileRect(int index) const {
    const qreal unit = Unit();
    const QRectF lower = LowerRect();
    const int column = index / Rows;
    const int row = index % Rows;
    return QRectF(lower.left() + (GridLeft + (column - scroll) * TileStep) * unit,
                  lower.top() + (GridTop + row * TileStep) * unit, Tile * unit, Tile * unit);
}

QRectF ThreeDsHome::ButtonRect(int index) const {
    const qreal unit = Unit();
    const QRectF lower = LowerRect();
    return QRectF(lower.left() + (8 + index * 31) * unit, lower.top() + 3 * unit, 27 * unit,
                  20 * unit);
}

QRectF ThreeDsHome::OpenRect() const {
    const qreal unit = Unit();
    const QRectF lower = LowerRect();
    return QRectF(lower.center().x() - 80 * unit, lower.bottom() - 37 * unit, 160 * unit,
                  30 * unit);
}

QJsonObject ThreeDsHome::Describe() const {
    const auto rect = [](const QRectF& r) {
        return QJsonArray{std::round(r.x()), std::round(r.y()), std::round(r.width()),
                          std::round(r.height())};
    };
    QJsonArray names;
    for (const Game& game : shown) {
        names.append(game.name);
    }
    const Game here = shown.value(chosen);
    return QJsonObject{
        {QStringLiteral("visible"), isVisible()},
        {QStringLiteral("focus"), hasFocus()},
        {QStringLiteral("games"), names},
        {QStringLiteral("chosen"), chosen},
        {QStringLiteral("name"), here.name},
        {QStringLiteral("maker"), here.maker},
        {QStringLiteral("readable"), here.readable},
        {QStringLiteral("button"), on_buttons ? buttons.value(button).key : QString()},
        {QStringLiteral("filter"), filter},
        {QStringLiteral("upper"), rect(UpperRect())},
        {QStringLiteral("lower"), rect(LowerRect())},
        {QStringLiteral("tile"), rect(TileRect(chosen))},
        {QStringLiteral("open"), rect(OpenRect())},
        {QStringLiteral("size"), QJsonArray{width(), height()}},
    };
}

// ---- What moves ----

void ThreeDsHome::Animate() {
    last_tick_ms = clock.elapsed();
    if (!frame_timer.isActive() && isVisible()) {
        frame_timer.start();
    }
}

void ThreeDsHome::Tick() {
    const qint64 now = clock.elapsed();
    const qreal seconds = std::clamp((now - last_tick_ms) / 1000.0, 0.0, 0.1);
    last_tick_ms = now;
    const qreal target = ScrollTarget();
    scroll += (target - scroll) * (1.0 - std::exp(-seconds * 14.0));
    if (std::abs(target - scroll) < 0.004) {
        scroll = target;
    }
    arrival = std::min(1.0, arrival + seconds / 0.32);
    if (pressed == -1) {
        press = std::max(0.0, press - seconds / 0.14);
    }
    update();
}

void ThreeDsHome::Choose(int index) {
    if (shown.isEmpty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(shown.size()) - 1);
    if (index != chosen || on_buttons) {
        chosen = index;
        on_buttons = false;
        arrival = 0.0;
    }
    Animate();
    update();
}

void ThreeDsHome::ChooseButton(int index) {
    index = std::clamp(index, 0, static_cast<int>(buttons.size()) - 1);
    if (index != button || !on_buttons) {
        button = index;
        on_buttons = true;
        arrival = 0.0;
    }
    Animate();
    update();
}

void ThreeDsHome::Start(int index) {
    if (!isEnabled()) {
        return;
    }
    if (shown.isEmpty()) {
        emit games->AddDirectory();
        return;
    }
    if (index < 0 || index >= shown.size()) {
        return;
    }
    emit games->GameChosen(shown.at(index).path);
}

void ThreeDsHome::Activate(int index) {
    if (index < 0 || index >= buttons.size()) {
        return;
    }
    const QString key = buttons.at(index).key;
    const QPoint below = mapToGlobal(ButtonRect(index).bottomLeft().toPoint() + QPoint(0, 4));
    if (key == QStringLiteral("reader")) {
        controller->TriggerAction(QStringLiteral("reader_refresh_menu"));
        controller->Menu()->popup(below);
    } else if (key == QStringLiteral("statistics")) {
        controller->TriggerAction(QStringLiteral("reader_statistics"));
    } else if (key == QStringLiteral("games")) {
        controller->TriggerAction(QStringLiteral("reader_games"));
    } else if (key == QStringLiteral("view")) {
        QMenu menu(this);
        QAction* home = menu.addAction(tr("3DS HOME menu"));
        home->setCheckable(true);
        home->setChecked(true);
        QAction* list = menu.addAction(tr("The emulator's game list"));
        menu.addSeparator();
        QMenu* themes = menu.addMenu(tr("Theme"));
        const QString current = ThreeDsLook::Theme();
        const QList<QPair<QString, QString>> names{{QStringLiteral("auto"), tr("As the system")},
                                                   {QStringLiteral("light"), tr("Light")},
                                                   {QStringLiteral("dark"), tr("Dark")}};
        for (const auto& name : names) {
            QAction* action = themes->addAction(name.second);
            action->setCheckable(true);
            action->setChecked(name.first == current);
            action->setData(name.first);
        }
        const QAction* picked = menu.exec(below);
        if (picked == list) {
            SetWanted(false);
        } else if (picked && picked->data().isValid()) {
            ThreeDsLook::SetTheme(picked->data().toString());
            ThreeDsLook::Apply(main_window);
            update();
        }
    } else if (key == QStringLiteral("folders")) {
        emit games->AddDirectory();
    } else if (key == QStringLiteral("settings")) {
        if (QAction* action =
                main_window->findChild<QAction*>(QStringLiteral("action_Configure"))) {
            action->trigger();
        }
    }
}

void ThreeDsHome::Options(int index, const QPoint& global) {
    if (index < 0 || index >= shown.size()) {
        return;
    }
    const Game game = shown.at(index);
    QMenu menu(this);
    QAction* title = menu.addAction(game.name);
    title->setEnabled(false);
    menu.addSeparator();
    const QAction* start = menu.addAction(tr("Open"));
    const QAction* properties = menu.addAction(tr("Settings for this game…"));
    const QAction* saves = menu.addAction(tr("Open the save data folder"));
    menu.addSeparator();
    const QAction* list = menu.addAction(tr("More, in the emulator's game list"));
    const QAction* picked = menu.exec(global);
    if (!picked) {
        return;
    }
    if (picked == start) {
        Start(index);
    } else if (picked == properties) {
        emit games->OpenPerGameGeneralRequested(game.path);
    } else if (picked == saves) {
        emit games->OpenFolderRequested(game.program_id, GameListOpenTarget::SAVE_DATA);
    } else if (picked == list) {
        SetWanted(false);
    }
}

// ---- Input ----

void ThreeDsHome::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    const bool plain =
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const int count = static_cast<int>(shown.size());
    switch (key) {
    case Qt::Key_Left:
        if (on_buttons) {
            ChooseButton(button - 1);
        } else if (chosen - Rows >= 0) {
            Choose(chosen - Rows);
        }
        return;
    case Qt::Key_Right:
        if (on_buttons) {
            ChooseButton(button + 1);
        } else if (chosen + Rows < count) {
            Choose(chosen + Rows);
        } else if (chosen / Rows < (count - 1) / Rows) {
            Choose(count - 1);
        }
        return;
    case Qt::Key_Down:
        if (on_buttons) {
            if (count > 0) {
                Choose(chosen - chosen % Rows);
            }
        } else if (chosen % Rows < Rows - 1 && chosen + 1 < count) {
            Choose(chosen + 1);
        }
        return;
    case Qt::Key_Up:
        if (!on_buttons) {
            if (chosen % Rows > 0) {
                Choose(chosen - 1);
            } else {
                ChooseButton(button);
            }
        }
        return;
    case Qt::Key_Home:
        Choose(0);
        return;
    case Qt::Key_End:
        Choose(count - 1);
        return;
    case Qt::Key_PageUp:
        Choose(chosen - Rows * Columns);
        return;
    case Qt::Key_PageDown:
        Choose(chosen + Rows * Columns);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        press = 1.0;
        Animate();
        if (on_buttons) {
            Activate(button);
        } else {
            Start(chosen);
        }
        return;
    case Qt::Key_Menu:
        if (!on_buttons) {
            Options(chosen, mapToGlobal(TileRect(chosen).center().toPoint()));
        }
        return;
    case Qt::Key_Escape:
        if (!filter.isEmpty()) {
            filter.clear();
            ApplyFilter();
            scroll = ScrollTarget();
            update();
        }
        return;
    case Qt::Key_Backspace:
        if (!filter.isEmpty()) {
            filter.chop(1);
            ApplyFilter();
            scroll = ScrollTarget();
            update();
        }
        return;
    default:
        break;
    }
    // Typing narrows the grid to the games whose name has what was typed.
    const QString text = event->text();
    if (plain && !text.isEmpty() && text.at(0).isPrint() && !(text == u' ' && filter.isEmpty())) {
        filter += text;
        chosen = 0;
        on_buttons = false;
        ApplyFilter();
        scroll = ScrollTarget();
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ThreeDsHome::inputMethodEvent(QInputMethodEvent* event) {
    if (!event->commitString().isEmpty()) {
        filter += event->commitString();
        chosen = 0;
        on_buttons = false;
        ApplyFilter();
        scroll = ScrollTarget();
        update();
    }
    event->accept();
}

void ThreeDsHome::mousePressEvent(QMouseEvent* event) {
    setFocus();
    pressed = -1;
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const QPointF at = event->position();
    if (LowerRect().contains(at)) {
        for (int index = 0; index < shown.size(); ++index) {
            if (TileRect(index).contains(at)) {
                pressed = index;
            }
        }
        if (shown.isEmpty() && TileRect(0).contains(at)) {
            pressed = 0;
        }
    }
    for (int index = 0; index < buttons.size(); ++index) {
        if (ButtonRect(index).contains(at)) {
            pressed = -2 - index;
        }
    }
    if (OpenRect().contains(at)) {
        pressed = PressedOpen;
    }
    if (pressed != -1) {
        press = 1.0;
        Animate();
        update();
    }
}

void ThreeDsHome::mouseReleaseEvent(QMouseEvent* event) {
    const int was = pressed;
    pressed = -1;
    Animate();
    if (event->button() != Qt::LeftButton || was == -1) {
        return;
    }
    const QPointF at = event->position();
    if (was == PressedOpen) {
        if (OpenRect().contains(at)) {
            Start(chosen);
        }
        return;
    }
    if (was <= -2) {
        const int index = -2 - was;
        if (ButtonRect(index).contains(at)) {
            ChooseButton(index);
            Activate(index);
        }
        return;
    }
    if (!TileRect(was).contains(at)) {
        return;
    }
    if (shown.isEmpty() || (was == chosen && !on_buttons)) {
        // As on the console: a touch of the chosen game opens it, a touch of another chooses
        // that one.
        Start(was);
    } else {
        Choose(was);
    }
}

void ThreeDsHome::mouseDoubleClickEvent(QMouseEvent* event) {
    mousePressEvent(event);
}

void ThreeDsHome::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    const int amount = std::abs(delta.x()) > std::abs(delta.y()) ? delta.x() : delta.y();
    if (amount != 0 && !on_buttons && !shown.isEmpty()) {
        Choose(chosen + (amount < 0 ? Rows : -Rows));
    }
    event->accept();
}

void ThreeDsHome::contextMenuEvent(QContextMenuEvent* event) {
    for (int index = 0; index < shown.size(); ++index) {
        if (TileRect(index).contains(event->pos())) {
            Choose(index);
            Options(index, event->globalPos());
            return;
        }
    }
}

void ThreeDsHome::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    arrival = 0.0;
    Animate();
}

void ThreeDsHome::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    frame_timer.stop();
    pressed = -1;
}

// ---- Drawing ----

void ThreeDsHome::DrawIcon(QPainter& painter, const QString& key, const QRectF& box,
                           const QColor& colour) const {
    painter.save();
    painter.translate(box.center());
    const qreal u = std::min(box.width(), box.height()) / 48.0;
    painter.scale(u, u);
    QPen pen(colour, 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (key == QStringLiteral("reader")) {
        QPainterPath book;
        book.moveTo(0, -9);
        book.cubicTo(-5, -13, -11, -13, -15, -11);
        book.lineTo(-15, 10);
        book.cubicTo(-11, 8, -5, 8, 0, 12);
        book.cubicTo(5, 8, 11, 8, 15, 10);
        book.lineTo(15, -11);
        book.cubicTo(11, -13, 5, -13, 0, -9);
        book.lineTo(0, 12);
        painter.drawPath(book);
    } else if (key == QStringLiteral("statistics")) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawRoundedRect(QRectF(-13, 1, 6, 11), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(-3, -6, 6, 18), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(7, -13, 6, 25), 1.5, 1.5);
    } else if (key == QStringLiteral("games")) {
        // A game card.
        painter.drawRoundedRect(QRectF(-12, -14, 24, 28), 3, 3);
        painter.drawLine(QPointF(-6, -5), QPointF(6, -5));
        painter.drawLine(QPointF(-6, 2), QPointF(6, 2));
    } else if (key == QStringLiteral("view")) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        for (const QPointF& corner :
             {QPointF(-13, -13), QPointF(2, -13), QPointF(-13, 2), QPointF(2, 2)}) {
            painter.drawRoundedRect(QRectF(corner, QSizeF(11, 11)), 2, 2);
        }
    } else if (key == QStringLiteral("folders")) {
        QPainterPath folder;
        folder.moveTo(-14, -10);
        folder.lineTo(-4, -10);
        folder.lineTo(0, -6);
        folder.lineTo(14, -6);
        folder.lineTo(14, 11);
        folder.lineTo(-14, 11);
        folder.closeSubpath();
        painter.drawPath(folder);
    } else {
        // The console's spanner.
        pen.setWidthF(6.5);
        painter.setPen(pen);
        painter.drawLine(QPointF(-11, 11), QPointF(4, -4));
        pen.setWidthF(3.6);
        painter.setPen(pen);
        painter.drawArc(QRectF(1, -15, 14, 14), 100 * 16, 250 * 16);
    }
    painter.restore();
}

void ThreeDsHome::DrawUpper(QPainter& painter, const QRectF& screen, qreal unit) {
    const auto& c = ThreeDsLook::Current();
    const bool dark = ThreeDsLook::Dark();
    const auto at = [&screen, unit](qreal x, qreal y) {
        return QPointF(screen.left() + x * unit, screen.top() + y * unit);
    };
    const auto box = [&screen, unit](qreal x, qreal y, qreal w, qreal h) {
        return QRectF(screen.left() + x * unit, screen.top() + y * unit, w * unit, h * unit);
    };
    const auto px = [unit](qreal value) {
        return std::max(1, static_cast<int>(std::round(value * unit)));
    };
    QFont font = QApplication::font();
    const qreal seconds = clock.elapsed() / 1000.0;

    QLinearGradient ground(screen.topLeft(), screen.bottomLeft());
    ground.setColorAt(0, c.screen_top);
    ground.setColorAt(1, c.screen_bottom);
    painter.fillRect(screen, ground);
    // A slow shimmer of pale rings, as the console's menu has behind its pictures.
    painter.setPen(Qt::NoPen);
    for (int ring = 0; ring < 5; ++ring) {
        const qreal phase = seconds * 0.11 + ring * 0.2;
        const qreal t = phase - std::floor(phase);
        const qreal radius = (30 + t * 190) * unit;
        painter.setBrush(
            Alpha(dark ? QColor(Qt::white) : c.glow_a, (1.0 - t) * (dark ? 0.035 : 0.05)));
        painter.drawEllipse(at(200, 112), radius, radius * 0.62);
    }

    // The strip: the date, what is being searched for, the time.
    const QRectF strip = box(0, 0, UpperWidth, 22);
    painter.fillRect(strip, Alpha(c.strip, 0.9));
    painter.setPen(QPen(c.line, 1));
    painter.drawLine(strip.bottomLeft(), strip.bottomRight());
    font.setPixelSize(px(12));
    painter.setFont(font);
    painter.setPen(c.text);
    const QLocale locale;
    painter.drawText(strip.adjusted(10 * unit, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                     locale.toString(QDate::currentDate(), QStringLiteral("M/d (ddd)")));
    painter.drawText(strip.adjusted(0, 0, -10 * unit, 0), Qt::AlignRight | Qt::AlignVCenter,
                     locale.toString(QTime::currentTime(), QLocale::ShortFormat));
    if (!filter.isEmpty()) {
        painter.setPen(c.accent);
        painter.drawText(strip, Qt::AlignCenter, tr("Search: %1").arg(filter));
    }

    if (shown.isEmpty()) {
        font.setPixelSize(px(16));
        painter.setFont(font);
        painter.setPen(c.faint);
        painter.drawText(box(20, 60, 360, 120), Qt::AlignCenter | Qt::TextWordWrap,
                         all.isEmpty() ? tr("No games yet.\nAdd the folder your games are in.")
                                       : tr("No game is called that."));
        return;
    }
    const Game& game = shown.at(chosen);

    // The game's picture, bobbing; it drops into place when the game is chosen.
    const qreal eased = 1.0 - std::pow(1.0 - arrival, 3.0);
    const qreal bob = std::sin(seconds * 2.0 * Pi / 3.2) * 3.5 - (1.0 - eased) * 14;
    const qreal side = 92;
    const QRectF picture = box(200 - side / 2, 46 + bob, side, side);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, dark ? 90 : 46));
    const qreal shadow = (side * 0.42 - bob * 0.9) * unit;
    painter.drawEllipse(at(200, 151), shadow, 5.5 * unit);
    QPainterPath rounded;
    rounded.addRoundedRect(picture, 9 * unit, 9 * unit);
    painter.save();
    painter.setClipPath(rounded);
    painter.setOpacity(0.35 + 0.65 * eased);
    if (game.picture.isNull()) {
        painter.fillRect(picture, c.raised);
    } else {
        painter.drawPixmap(picture, game.picture, QRectF(game.picture.rect()));
    }
    painter.restore();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Alpha(c.raised, 0.9), px(2)));
    painter.drawPath(rounded);

    // Its name, its maker and whether its text can be read, in a bubble.
    const QRectF bubble = box(40, 166, 320, game.readable || !game.maker.isEmpty() ? 60 : 44);
    painter.setPen(QPen(c.line, 1));
    painter.setBrush(Alpha(c.raised, 0.94));
    painter.drawRoundedRect(bubble, 10 * unit, 10 * unit);
    font.setPixelSize(px(15));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Alpha(c.text, 0.25 + 0.75 * eased));
    const QFontMetricsF name_metrics(font);
    painter.drawText(
        QRectF(bubble.left() + 12 * unit, bubble.top() + 6 * unit, bubble.width() - 24 * unit,
               22 * unit),
        Qt::AlignCenter,
        name_metrics.elidedText(game.name, Qt::ElideRight, bubble.width() - 24 * unit));
    font.setBold(false);
    font.setPixelSize(px(11));
    painter.setFont(font);
    const QFontMetricsF small(font);
    if (!game.maker.isEmpty()) {
        painter.setPen(c.faint);
        painter.drawText(QRectF(bubble.left() + 12 * unit, bubble.top() + 27 * unit,
                                bubble.width() - 24 * unit, 14 * unit),
                         Qt::AlignCenter,
                         small.elidedText(game.maker, Qt::ElideRight, bubble.width() - 24 * unit));
    }
    if (game.readable) {
        const QString text = tr("Its text can be read");
        const qreal width = small.horizontalAdvance(text) + 18 * unit;
        const QRectF line(bubble.center().x() - width / 2, bubble.top() + 42 * unit, width,
                          14 * unit);
        DrawIcon(painter, QStringLiteral("reader"),
                 QRectF(line.left(), line.top(), 14 * unit, 14 * unit), c.accent);
        painter.setPen(c.accent);
        painter.drawText(line.adjusted(18 * unit, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}

void ThreeDsHome::DrawLower(QPainter& painter, const QRectF& screen, qreal unit) {
    const auto& c = ThreeDsLook::Current();
    const bool dark = ThreeDsLook::Dark();
    const auto px = [unit](qreal value) {
        return std::max(1, static_cast<int>(std::round(value * unit)));
    };
    QFont font = QApplication::font();
    const qreal seconds = clock.elapsed() / 1000.0;
    const qreal glow = 0.5 + 0.5 * std::sin(seconds * 2.0 * Pi / 1.4);
    const QColor frame_colour = Mix(c.glow_a, c.glow_b, glow);

    QLinearGradient ground(screen.topLeft(), screen.bottomLeft());
    ground.setColorAt(0, c.screen_top);
    ground.setColorAt(1, c.screen_bottom);
    painter.fillRect(screen, ground);

    // ---- The games ----
    const int count = static_cast<int>(shown.size());
    if (count == 0) {
        const QRectF tile = TileRect(0);
        painter.setPen(QPen(frame_colour, px(2.5)));
        painter.setBrush(c.raised);
        painter.drawRoundedRect(tile, 8 * unit, 8 * unit);
        painter.setPen(QPen(c.faint, px(3), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(tile.center() - QPointF(11 * unit, 0),
                         tile.center() + QPointF(11 * unit, 0));
        painter.drawLine(tile.center() - QPointF(0, 11 * unit),
                         tile.center() + QPointF(0, 11 * unit));
    }
    // Empty places, as the console's grid has them.
    const int first_column = static_cast<int>(std::floor(scroll));
    for (int column = first_column; column <= first_column + Columns; ++column) {
        for (int row = 0; row < Rows; ++row) {
            const int index = column * Rows + row;
            if (index < count || (count == 0 && index == 0)) {
                continue;
            }
            const QRectF place = TileRect(index).adjusted(6 * unit, 6 * unit, -6 * unit, -6 * unit);
            painter.setPen(QPen(Alpha(c.line, 0.7), px(1), Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(place, 7 * unit, 7 * unit);
        }
    }
    for (int index = 0; index < count; ++index) {
        QRectF tile = TileRect(index);
        if (tile.right() < screen.left() || tile.left() > screen.right()) {
            continue;
        }
        const bool is_chosen = index == chosen && !on_buttons;
        if (is_chosen) {
            // It hops when it is chosen.
            tile.translate(0, -std::sin(std::min(arrival, 1.0) * Pi) * 9 * unit);
        }
        if (pressed == index || (is_chosen && pressed == -1 && press > 0)) {
            const qreal inset = tile.width() * 0.05 * press;
            tile.adjust(inset, inset, -inset, -inset);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, dark ? 80 : 40));
        painter.drawRoundedRect(tile.translated(0, 2.5 * unit), 9 * unit, 9 * unit);
        painter.setBrush(c.raised);
        painter.drawRoundedRect(tile, 9 * unit, 9 * unit);
        const QRectF picture = tile.adjusted(5 * unit, 5 * unit, -5 * unit, -5 * unit);
        const Game& game = shown.at(index);
        QPainterPath rounded;
        rounded.addRoundedRect(picture, 6 * unit, 6 * unit);
        painter.save();
        painter.setClipPath(rounded);
        if (game.picture.isNull()) {
            painter.fillRect(picture, c.strip);
        } else {
            painter.drawPixmap(picture, game.picture, QRectF(game.picture.rect()));
        }
        painter.restore();
        if (game.readable) {
            // A game whose text can be read carries a boxed book in its corner.
            const qreal side = 17 * unit;
            const QRectF badge(tile.right() - side - 2 * unit, tile.top() + 2 * unit, side, side);
            painter.setPen(QPen(c.raised, px(1.2)));
            painter.setBrush(c.accent);
            painter.drawRoundedRect(badge, 4 * unit, 4 * unit);
            DrawIcon(painter, QStringLiteral("reader"),
                     badge.adjusted(2 * unit, 2 * unit, -2 * unit, -2 * unit), Qt::white);
        }
        if (is_chosen) {
            const qreal off = 3 * unit;
            const QRectF frame = tile.adjusted(-off, -off, off, off);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Alpha(frame_colour, 0.28), px(7)));
            painter.drawRoundedRect(frame, 11 * unit, 11 * unit);
            painter.setPen(QPen(frame_colour, px(3)));
            painter.drawRoundedRect(frame, 11 * unit, 11 * unit);
        }
    }

    // ---- Along the top: small buttons, and the name of what is chosen ----
    const QRectF bar(screen.left(), screen.top(), screen.width(), BarHeight * unit);
    painter.fillRect(bar, Alpha(c.strip, 0.96));
    painter.setPen(QPen(c.line, 1));
    painter.drawLine(bar.bottomLeft(), bar.bottomRight());
    for (int index = 0; index < buttons.size(); ++index) {
        QRectF small = ButtonRect(index);
        const bool is_chosen = on_buttons && index == button;
        const bool held = pressed == -2 - index;
        painter.setPen(QPen(is_chosen ? frame_colour : c.line, px(is_chosen ? 2.2 : 1)));
        painter.setBrush(held ? c.pressed : c.raised);
        painter.drawRoundedRect(small, 7 * unit, 7 * unit);
        DrawIcon(painter, buttons.at(index).key,
                 small.adjusted(3 * unit, 3 * unit, -3 * unit, -3 * unit),
                 is_chosen ? c.accent : c.faint);
    }
    QString name;
    qreal name_centre = screen.center().x();
    if (on_buttons) {
        name = buttons.at(button).name;
        if (buttons.at(button).key == QStringLiteral("statistics")) {
            // What was read today is said with the name, as in the Reader menu.
            if (const QAction* action =
                    controller->Menu()->findChild<QAction*>(QStringLiteral("reader_statistics"))) {
                name = action->text().remove(u'&').remove(QChar(0x2026));
            }
        }
    } else if (count > 0) {
        name = shown.at(chosen).name;
        name_centre = TileRect(chosen).center().x();
    } else {
        name = all.isEmpty() ? tr("Add a folder with your games") : tr("No game is called that");
        name_centre = TileRect(0).center().x();
    }
    font.setPixelSize(px(12));
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    name = metrics.elidedText(name, Qt::ElideRight, screen.width() - 40 * unit);
    const qreal bubble_width = metrics.horizontalAdvance(name) + 22 * unit;
    const qreal left =
        std::clamp(name_centre - bubble_width / 2, screen.left() + 6 * unit,
                   std::max(screen.left() + 6 * unit, screen.right() - 6 * unit - bubble_width));
    const QRectF bubble(left, screen.top() + 31 * unit, bubble_width, 20 * unit);
    painter.setPen(QPen(c.line, 1));
    painter.setBrush(c.raised);
    painter.drawRoundedRect(bubble, 10 * unit, 10 * unit);
    painter.setPen(c.text);
    painter.drawText(bubble, Qt::AlignCenter, name);

    // ---- Along the bottom: the wide Open button ----
    const QRectF foot(screen.left(), screen.bottom() - 44 * unit, screen.width(), 44 * unit);
    painter.fillRect(foot, Alpha(c.strip, 0.96));
    painter.setPen(QPen(c.line, 1));
    painter.drawLine(foot.topLeft(), foot.topRight());
    QRectF open = OpenRect();
    if (pressed == PressedOpen) {
        open.adjust(2 * unit, 1 * unit, -2 * unit, -1 * unit);
    }
    QLinearGradient shade(open.topLeft(), open.bottomLeft());
    shade.setColorAt(0, pressed == PressedOpen ? c.pressed : c.raised);
    shade.setColorAt(1, pressed == PressedOpen ? c.pressed : Mix(c.raised, c.strip, 0.7));
    painter.setPen(
        QPen(on_buttons ? c.line : Mix(c.line, frame_colour, 0.8), px(on_buttons ? 1 : 2)));
    painter.setBrush(shade);
    painter.drawRoundedRect(open, open.height() / 2, open.height() / 2);
    font.setPixelSize(px(14));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(isEnabled() ? c.text : c.faint);
    painter.drawText(open, Qt::AlignCenter, count == 0 ? tr("Add games") : tr("Open"));
}

void ThreeDsHome::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto& c = ThreeDsLook::Current();
    painter.fillRect(rect(), c.body);
    const qreal unit = Unit();
    const QRectF upper = UpperRect();
    const QRectF lower = LowerRect();

    // The console's two halves behind the screens, with the hinge between them.
    painter.setPen(QPen(Mix(c.body, Qt::black, 0.18), 1));
    painter.setBrush(Mix(c.body, Qt::black, ThreeDsLook::Dark() ? 0.35 : 0.72));
    const qreal rim = 7 * unit;
    painter.drawRoundedRect(upper.adjusted(-rim, -rim, rim, rim), 8 * unit, 8 * unit);
    painter.drawRoundedRect(QRectF(upper.left() - rim, lower.top() - rim, upper.width() + 2 * rim,
                                   lower.height() + 2 * rim),
                            8 * unit, 8 * unit);

    painter.save();
    painter.setClipRect(upper);
    DrawUpper(painter, upper, unit);
    painter.restore();
    painter.save();
    painter.setClipRect(lower);
    DrawLower(painter, lower, unit);
    painter.restore();

    if (!isEnabled()) {
        painter.fillRect(rect(), Alpha(c.body, 0.55));
    }
}

} // namespace ReaderUi
