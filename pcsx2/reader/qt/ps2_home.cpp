// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QDialog>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QWheelEvent>
#include "reader/qt/ps2_home.h"
#include "reader/qt/reader_session.h"

namespace ReaderUi {

namespace {

const QString KeyLook = QStringLiteral("reader_ps2_look");
const QString KeyHome = QStringLiteral("reader_ps2_home");

constexpr double Pi = 3.14159265358979323846;
// The menu is laid out in the units of the console's own picture, 640 by 448.
constexpr qreal TileWidth = 92;
constexpr qreal TileHeight = 130;
constexpr qreal TileGapX = 30;
constexpr qreal TileGapY = 34;
constexpr qreal GridTop = 112;
constexpr qreal GridSide = 64;
constexpr qreal RowStep = 36;
constexpr qreal RowsTop = 118;

ReaderApp::ReaderSettings& Stored() {
    return ReaderSession::Instance().Settings();
}

QString Hex(const QColor& colour) {
    return colour.name(QColor::HexRgb);
}

QColor Mix(const QColor& a, const QColor& b, qreal t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QColor Alpha(QColor colour, qreal alpha) {
    colour.setAlphaF(std::clamp(colour.alphaF() * alpha, 0.0, 1.0));
    return colour;
}

/// The four marks of the console's buttons, each in its own colour.
void DrawMark(QPainter& painter, const QString& mark, const QRectF& box, qreal alpha) {
    const qreal inset = box.width() * 0.2;
    const QRectF in = box.adjusted(inset, inset, -inset, -inset);
    const qreal width = std::max(1.2, box.width() * 0.11);
    painter.setBrush(Qt::NoBrush);
    if (mark == QStringLiteral("cross")) {
        painter.setPen(QPen(Alpha(QColor(0x7C, 0xA8, 0xFF), alpha), width, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(in.topLeft(), in.bottomRight());
        painter.drawLine(in.topRight(), in.bottomLeft());
    } else if (mark == QStringLiteral("circle")) {
        painter.setPen(QPen(Alpha(QColor(0xFF, 0x6E, 0x6E), alpha), width));
        painter.drawEllipse(in);
    } else if (mark == QStringLiteral("triangle")) {
        painter.setPen(QPen(Alpha(QColor(0x5F, 0xE0, 0xA8), alpha), width, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawPolygon(QPolygonF{QPointF(in.center().x(), in.top()), in.bottomRight(), in.bottomLeft()});
    } else {
        painter.setPen(QPen(Alpha(QColor(0xF0, 0x9A, 0xE0), alpha), width, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawRect(in);
    }
}

/// What the buttons do, along the foot of a dressed window.
class HintBar : public QWidget {
public:
    explicit HintBar(QWidget* parent) : QWidget(parent) {
        setFixedHeight(40);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto& colours = Ps2Look::Current();
        QFont font = painter.font();
        font.setPixelSize(15);
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        qreal x = width() - 14;
        const QList<QPair<QString, QString>> hints{{QStringLiteral("circle"), QObject::tr("Back")},
                                                   {QStringLiteral("cross"), QObject::tr("Enter")}};
        for (const auto& hint : hints) {
            const qreal text_width = metrics.horizontalAdvance(hint.second);
            x -= text_width;
            painter.setPen(colours.text);
            painter.drawText(QRectF(x, 8, text_width + 2, 24), Qt::AlignVCenter | Qt::AlignLeft, hint.second);
            x -= 26;
            DrawMark(painter, hint.first, QRectF(x, 9, 22, 22), 1.0);
            x -= 20;
        }
    }
};

} // namespace

// ---- The look ----

namespace Ps2Look {

bool Wanted() {
    return Stored().Value(KeyLook, true).toBool();
}

void SetWanted(bool wanted) {
    Stored().SetValue(KeyLook, wanted);
    Stored().Sync();
}

const Colours& Current() {
    // The system menu has one face: a blue so deep that it is nearly black.
    static const Colours colours{
        .ground = QColor(0x06, 0x0A, 0x1E),
        .ground_low = QColor(0x01, 0x02, 0x08),
        .text = QColor(0xC9, 0xD8, 0xF6),
        .faint = QColor(0x70, 0x82, 0xAE),
        .line = QColor(0x2B, 0x3A, 0x68),
        .accent = QColor(0x9C, 0xD4, 0xFF),
        .glow = QColor(0x3E, 0x8C, 0xFF),
        .raised = QColor(0x11, 0x1A, 0x3A),
        .pressed = QColor(0x1D, 0x35, 0x70),
    };
    return colours;
}

QString StyleSheet(const Colours& c) {
    QString sheet = QStringLiteral(R"(
QWidget { color: %text%; }
QMainWindow, QDialog, QMenuBar, QStatusBar, QStackedWidget, QScrollArea,
QScrollArea > QWidget > QWidget, QDockWidget, QToolBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %ground%, stop:1 %low%); }
QTabWidget::pane, QGroupBox { background: transparent; }
QMenuBar { background: %low%; border-bottom: 1px solid %line%; }
QMenuBar::item { padding: 5px 11px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { color: %accent%; background: %raised%; }
QMenu { background: %ground%; border: 1px solid %line%; padding: 6px 0; }
QMenu::item { padding: 6px 30px 6px 24px; }
QMenu::item:selected { color: %accent%; background: %raised%; }
QMenu::item:disabled { color: %faint%; }
QMenu::separator { height: 1px; background: %line%; margin: 5px 14px; }
QToolTip { color: %text%; background: %raised%; border: 1px solid %line%; padding: 4px 7px; }
QLabel { background: transparent; }
QGroupBox { border: none; border-top: 1px solid %line%; margin-top: 22px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 2px; top: 2px; color: %faint%; }
QPushButton, QToolButton { background: %raised%; border: 1px solid %line%; border-radius: 2px;
    padding: 6px 18px; min-height: 18px; }
QToolButton { padding: 5px 9px; }
QPushButton:hover, QToolButton:hover, QPushButton:focus { border: 1px solid %accent%;
    color: %accent%; }
QPushButton:pressed, QToolButton:pressed { background: %pressed%; }
QPushButton:disabled, QToolButton:disabled { color: %faint%; background: transparent; }
QLineEdit, QPlainTextEdit, QTextEdit, QKeySequenceEdit { background: %field%;
    border: 1px solid %line%; border-radius: 2px; padding: 5px 8px;
    selection-background-color: %glow%; selection-color: white; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border: 1px solid %accent%; }
QComboBox QAbstractItemView { background: %ground%; border: 1px solid %line%;
    selection-background-color: %raised%; selection-color: %accent%; outline: none; }
QCheckBox, QRadioButton { spacing: 9px; padding: 4px 0; background: transparent; }
QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px;
    border: 1px solid %faint%; border-radius: 2px; background: %field%; }
QRadioButton::indicator { border-radius: 7px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %accent%;
    border: 1px solid %accent%; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border: 1px solid %accent%; }
QCheckBox:hover, QRadioButton:hover, QCheckBox:focus, QRadioButton:focus { color: %accent%; }
QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget { background: %field%;
    alternate-background-color: %alt%; border: 1px solid %line%; outline: none; }
QListView::item, QTreeView::item, QListWidget::item { padding: 6px 4px; }
QListView::item:hover, QTreeView::item:hover, QTableView::item:hover { background: %raised%; }
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {
    color: %accent%; background: %pressed%; }
QHeaderView::section { background: %low%; color: %faint%; border: none;
    border-bottom: 1px solid %line%; padding: 6px 8px; }
QTabBar::tab { background: transparent; color: %faint%; padding: 8px 16px; border: none;
    border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: %text%; }
QTabBar::tab:selected { color: %accent%; border-bottom: 2px solid %accent%; }
QTabWidget::pane { border: none; border-top: 1px solid %line%; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }
QScrollBar::handle { background: %line%; border-radius: 2px; min-height: 28px; min-width: 28px; }
QScrollBar::handle:hover { background: %glow%; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent; border: none; width: 0; height: 0; }
QSlider::groove:horizontal { height: 3px; background: %line%; }
QSlider::sub-page:horizontal { background: %accent%; }
QSlider::handle:horizontal { width: 12px; margin: -6px 0; border-radius: 6px;
    background: %accent%; }
QProgressBar { background: %field%; border: 1px solid %line%; text-align: center; }
QProgressBar::chunk { background: %glow%; }
QStatusBar { background: %low%; border-top: 1px solid %line%; color: %faint%; }
QStatusBar::item { border: none; }
QLabel#reader_ps2_title { font-size: 19pt; font-weight: 300; color: %accent%;
    padding: 2px 2px 12px 2px; }
)");
    const QList<QPair<QString, QString>> values{
        {QStringLiteral("%ground%"), Hex(c.ground)},
        {QStringLiteral("%low%"), Hex(c.ground_low)},
        {QStringLiteral("%text%"), Hex(c.text)},
        {QStringLiteral("%faint%"), Hex(c.faint)},
        {QStringLiteral("%line%"), Hex(c.line)},
        {QStringLiteral("%accent%"), Hex(c.accent)},
        {QStringLiteral("%glow%"), Hex(c.glow)},
        {QStringLiteral("%raised%"), Hex(c.raised)},
        {QStringLiteral("%pressed%"), Hex(c.pressed)},
        {QStringLiteral("%field%"), Hex(Mix(c.ground, Qt::black, 0.35))},
        {QStringLiteral("%alt%"), Hex(Mix(c.ground, c.text, 0.05))},
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
    // Setting the colours tells every window that they changed, and a main window that sets
    // its theme anew when told would come back here: once is enough.
    static bool busy = false;
    if (busy) {
        return applied;
    }
    if (!Wanted()) {
        if (applied) {
            busy = true;
            applied = false;
            if (QStyle* style = QStyleFactory::create(style_before)) {
                QApplication::setStyle(style);
            }
            QApplication::setPalette(palette_before);
            busy = false;
        }
        return false;
    }
    busy = true;
    if (!applied) {
        applied = true;
        style_before = QApplication::style()->objectName();
        palette_before = QApplication::palette();
    }
    const Colours& c = Current();
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }
    QPalette palette;
    palette.setColor(QPalette::Window, c.ground);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, Mix(c.ground, Qt::black, 0.35));
    palette.setColor(QPalette::AlternateBase, Mix(c.ground, c.text, 0.05));
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.raised);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::ToolTipBase, c.raised);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Highlight, c.glow);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::PlaceholderText, c.faint);
    palette.setColor(QPalette::Light, Mix(c.raised, c.text, 0.2));
    palette.setColor(QPalette::Mid, c.line);
    palette.setColor(QPalette::Dark, c.ground_low);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
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
    if (!Wanted() || !dialog || dialog->property("reader_ps2_dressed").toBool()) {
        return;
    }
    auto* layout = qobject_cast<QBoxLayout*>(dialog->layout());
    if (!layout || layout->direction() != QBoxLayout::TopToBottom) {
        return;
    }
    dialog->setProperty("reader_ps2_dressed", true);
    auto* title = new QLabel(dialog->windowTitle(), dialog);
    title->setObjectName(QStringLiteral("reader_ps2_title"));
    layout->insertWidget(0, title);
    layout->addSpacing(4);
    layout->addWidget(new HintBar(dialog));
    layout->setContentsMargins(34, 22, 34, 4);
    dialog->resize(dialog->width() + 44, dialog->height() + 92);
}

} // namespace Ps2Look

// ---- The menu ----

Ps2Home::Ps2Home(QMainWindow* main_window_, QWidget* list_)
    : QWidget(list_), main_window{main_window_}, list{list_} {
    setObjectName(QStringLiteral("reader_ps2_home"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);

    // The lights turn for as long as the menu is in front.
    frame_timer.setInterval(16);
    connect(&frame_timer, &QTimer::timeout, this, &Ps2Home::Tick);
    clock.start();

    list->installEventFilter(this);
    main_window->installEventFilter(this);
    for (QWidget* child : list->findChildren<QWidget*>()) {
        if (child != this) {
            child->installEventFilter(this);
        }
    }
    Follow();
}

bool Ps2Home::Wanted() {
    return Stored().Value(KeyHome, true).toBool();
}

void Ps2Home::SetWanted(bool wanted) {
    if (wanted != Wanted()) {
        Stored().SetValue(KeyHome, wanted);
        Stored().Sync();
    }
    Follow();
    emit WantedChanged(wanted);
}

void Ps2Home::SetGames(const QList<Game>& games_) {
    const QString chosen_path = shown.value(game_choice).path;
    all = games_;
    ApplyFilter();
    for (int index = 0; index < shown.size(); ++index) {
        if (shown.at(index).path == chosen_path) {
            game_choice = index;
        }
    }
    update();
}

void Ps2Home::SetSettings(const QList<Setting>& settings_) {
    settings = settings_;
    setting_choice = std::clamp(setting_choice, 0, std::max(0, static_cast<int>(settings.size()) - 1));
    update();
}

void Ps2Home::ApplyFilter() {
    shown.clear();
    for (const Game& game : all) {
        if (filter.isEmpty() || game.name.contains(filter, Qt::CaseInsensitive) ||
            game.serial.contains(filter, Qt::CaseInsensitive)) {
            shown.append(game);
        }
    }
    game_choice = std::clamp(game_choice, 0, std::max(0, static_cast<int>(shown.size()) - 1));
}

void Ps2Home::Follow() {
    const bool show = Wanted();
    setGeometry(list->rect());
    if (show != !isHidden()) {
        setVisible(show);
    }
    if (show) {
        raise();
        if (isVisible() && !QApplication::activeModalWidget() && window()->isActiveWindow()) {
            setFocus();
        }
    }
}

bool Ps2Home::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
        if (watched == list) {
            QTimer::singleShot(0, this, &Ps2Home::Follow);
        }
        break;
    case QEvent::WindowActivate:
        if (watched == main_window) {
            QTimer::singleShot(0, this, &Ps2Home::Follow);
        }
        break;
    case QEvent::KeyPress:
        // Keys that arrive at the list underneath are the menu's while it is in front.
        if (watched != main_window && watched != this && isVisible()) {
            keyPressEvent(static_cast<QKeyEvent*>(event));
            return true;
        }
        break;
    case QEvent::FocusIn:
        if (watched != main_window && watched != this && watched != list && isVisible()) {
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

// ---- Geometry ----

qreal Ps2Home::Unit() const {
    return std::max(0.4, std::min(width() / 640.0, height() / 448.0));
}

int Ps2Home::Columns() const {
    const qreal unit = Unit();
    const qreal room = width() - 2 * GridSide * unit + TileGapX * unit;
    return std::max(1, static_cast<int>(room / ((TileWidth + TileGapX) * unit)));
}

QRectF Ps2Home::MenuRect(int index) const {
    const qreal unit = Unit();
    return QRectF(width() * 0.14, height() / 2.0 - 46 * unit + index * 48 * unit, 300 * unit, 40 * unit);
}

QRectF Ps2Home::SettingRect(int index) const {
    const qreal unit = Unit();
    const qreal side = std::max(70 * unit, width() * 0.14);
    return QRectF(side, RowsTop * unit + (index - setting_scroll) * RowStep * unit, width() - 2 * side,
                  RowStep * unit);
}

QRectF Ps2Home::TileRect(int index) const {
    const qreal unit = Unit();
    const int columns = Columns();
    const qreal used = columns * TileWidth * unit + (columns - 1) * TileGapX * unit;
    const qreal left = (width() - used) / 2;
    const int column = index % columns;
    const int row = index / columns;
    return QRectF(left + column * (TileWidth + TileGapX) * unit,
                  GridTop * unit + (row - scroll) * (TileHeight + TileGapY) * unit, TileWidth * unit,
                  TileHeight * unit);
}

int Ps2Home::SettingRows() const {
    const qreal unit = Unit();
    return std::max(1, static_cast<int>((height() - (RowsTop + 62) * unit) / (RowStep * unit)));
}

int Ps2Home::HitTest(const QPointF& at) const {
    if (screen == Screen::Menu) {
        for (int index = 0; index < 2; ++index) {
            if (MenuRect(index).contains(at)) {
                return index;
            }
        }
    } else if (screen == Screen::Configuration) {
        for (int index = setting_scroll; index < settings.size() && index < setting_scroll + SettingRows(); ++index) {
            if (SettingRect(index).contains(at)) {
                return index;
            }
        }
    } else {
        for (int index = 0; index < shown.size(); ++index) {
            if (TileRect(index).contains(at)) {
                return index;
            }
        }
        if (shown.isEmpty() && TileRect(0).contains(at)) {
            return 0;
        }
    }
    return -1;
}

QJsonObject Ps2Home::Describe() const {
    const auto rect = [](const QRectF& r) {
        return QJsonArray{std::round(r.x()), std::round(r.y()), std::round(r.width()), std::round(r.height())};
    };
    QJsonArray names;
    for (const Game& game : shown) {
        names.append(game.name);
    }
    QJsonArray rows;
    for (const Setting& setting : settings) {
        rows.append(setting.name + (setting.value ? QStringLiteral(" = ") + setting.value() : QString()));
    }
    return QJsonObject{
        {QStringLiteral("visible"), isVisible()},
        {QStringLiteral("focus"), hasFocus()},
        {QStringLiteral("screen"), screen == Screen::Menu      ? QStringLiteral("menu")
                                   : screen == Screen::Browser ? QStringLiteral("browser")
                                                               : QStringLiteral("configuration")},
        {QStringLiteral("menu_choice"), menu_choice},
        {QStringLiteral("games"), names},
        {QStringLiteral("game_choice"), game_choice},
        {QStringLiteral("settings"), rows},
        {QStringLiteral("setting_choice"), setting_choice},
        {QStringLiteral("filter"), filter},
        {QStringLiteral("tile"), rect(TileRect(game_choice))},
        {QStringLiteral("size"), QJsonArray{width(), height()}},
    };
}

// ---- What moves ----

void Ps2Home::Tick() {
    const qint64 now = clock.elapsed();
    const qreal seconds = std::clamp((now - last_tick_ms) / 1000.0, 0.0, 0.1);
    last_tick_ms = now;
    arrival = std::min(1.0, arrival + seconds / 0.38);
    choice_arrival = std::min(1.0, choice_arrival + seconds / 0.25);
    // System Configuration's lines follow the chosen one.
    const int rows = SettingRows();
    if (setting_choice < setting_scroll) {
        setting_scroll = setting_choice;
    } else if (setting_choice >= setting_scroll + rows) {
        setting_scroll = setting_choice - rows + 1;
    }
    setting_scroll = std::clamp(setting_scroll, 0, std::max(0, static_cast<int>(settings.size()) - rows));
    // The Browser's rows follow the chosen game.
    const int row = game_choice / Columns();
    const qreal unit = Unit();
    const int visible_rows =
        std::max(1, static_cast<int>((height() - (GridTop + 62) * unit + TileGapY * unit) /
                                     ((TileHeight + TileGapY) * unit)));
    qreal target = std::round(scroll);
    if (row < target) {
        target = row;
    }
    if (row > target + visible_rows - 1) {
        target = row - (visible_rows - 1);
    }
    target = std::max(0.0, target);
    scroll += (target - scroll) * (1.0 - std::exp(-seconds * 12.0));
    if (std::abs(target - scroll) < 0.003) {
        scroll = target;
    }
    update();
}

void Ps2Home::Go(Screen next) {
    if (next == screen) {
        return;
    }
    screen_before = screen;
    screen = next;
    arrival = 0.0;
    choice_arrival = 0.0;
    update();
}

void Ps2Home::Back() {
    if (screen == Screen::Browser && !filter.isEmpty()) {
        filter.clear();
        ApplyFilter();
        update();
        return;
    }
    if (screen != Screen::Menu) {
        Go(Screen::Menu);
    }
}

void Ps2Home::Enter() {
    if (!isEnabled()) {
        return;
    }
    if (screen == Screen::Menu) {
        Go(menu_choice == 0 ? Screen::Browser : Screen::Configuration);
    } else if (screen == Screen::Browser) {
        if (shown.isEmpty()) {
            if (add_games) {
                add_games();
            }
        } else if (start_game) {
            start_game(shown.at(game_choice).path);
        }
    } else if (setting_choice >= 0 && setting_choice < settings.size()) {
        if (settings.at(setting_choice).choose) {
            settings.at(setting_choice).choose();
        }
        update();
    }
}

void Ps2Home::Move(int dx, int dy) {
    if (screen == Screen::Menu) {
        const int next = std::clamp(menu_choice + dy, 0, 1);
        if (next != menu_choice) {
            menu_choice = next;
            choice_arrival = 0.0;
        }
        if (dx > 0) {
            Enter();
        }
    } else if (screen == Screen::Configuration) {
        const int next = std::clamp(setting_choice + dy, 0, std::max(0, static_cast<int>(settings.size()) - 1));
        if (next != setting_choice) {
            setting_choice = next;
            choice_arrival = 0.0;
        }
        if (dx < 0) {
            Back();
        }
    } else if (!shown.isEmpty()) {
        const int columns = Columns();
        const int count = static_cast<int>(shown.size());
        int next = game_choice;
        if (dx < 0 && game_choice % columns > 0) {
            --next;
        } else if (dx > 0 && game_choice % columns < columns - 1 && game_choice + 1 < count) {
            ++next;
        }
        if (dy < 0 && game_choice - columns >= 0) {
            next -= columns;
        } else if (dy > 0) {
            if (game_choice + columns < count) {
                next += columns;
            } else if (game_choice / columns < (count - 1) / columns) {
                next = count - 1;
            }
        }
        if (next >= 0 && next < shown.size() && next != game_choice) {
            game_choice = next;
            choice_arrival = 0.0;
        }
    }
    update();
}

// ---- Input ----

void Ps2Home::keyPressEvent(QKeyEvent* event) {
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    switch (event->key()) {
    case Qt::Key_Left:
        Move(-1, 0);
        return;
    case Qt::Key_Right:
        Move(1, 0);
        return;
    case Qt::Key_Up:
        Move(0, -1);
        return;
    case Qt::Key_Down:
        Move(0, 1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        Enter();
        return;
    case Qt::Key_Escape:
        Back();
        return;
    case Qt::Key_Backspace:
        if (screen == Screen::Browser && !filter.isEmpty()) {
            filter.chop(1);
            ApplyFilter();
            update();
        } else {
            Back();
        }
        return;
    case Qt::Key_Menu:
        if (screen == Screen::Browser && !shown.isEmpty() && game_options) {
            game_options(shown.at(game_choice).path, mapToGlobal(TileRect(game_choice).center().toPoint()));
        }
        return;
    default:
        break;
    }
    // In the Browser, typing narrows the games to those whose name has what was typed.
    const QString text = event->text();
    if (screen == Screen::Browser && plain && !text.isEmpty() && text.at(0).isPrint() &&
        !(text == u' ' && filter.isEmpty())) {
        filter += text;
        game_choice = 0;
        ApplyFilter();
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void Ps2Home::inputMethodEvent(QInputMethodEvent* event) {
    if (screen == Screen::Browser && !event->commitString().isEmpty()) {
        filter += event->commitString();
        game_choice = 0;
        ApplyFilter();
        update();
    }
    event->accept();
}

void Ps2Home::mouseMoveEvent(QMouseEvent* event) {
    // In the two lists the pointer chooses what it is on.
    const int hit = HitTest(event->position());
    if (hit < 0) {
        return;
    }
    if (screen == Screen::Menu && hit != menu_choice) {
        menu_choice = hit;
        choice_arrival = 0.0;
        update();
    } else if (screen == Screen::Configuration && hit != setting_choice) {
        setting_choice = hit;
        choice_arrival = 0.0;
        update();
    }
}

void Ps2Home::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (event->button() == Qt::BackButton || event->button() == Qt::RightButton) {
        if (event->button() == Qt::BackButton || screen != Screen::Browser) {
            Back();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int hit = HitTest(event->position());
    if (hit < 0) {
        return;
    }
    if (screen == Screen::Menu) {
        menu_choice = hit;
        Enter();
    } else if (screen == Screen::Configuration) {
        setting_choice = hit;
        Enter();
    } else if (shown.isEmpty() || hit == game_choice) {
        // As a controller's Enter: the chosen game starts; another one is chosen first.
        Enter();
    } else {
        game_choice = hit;
        choice_arrival = 0.0;
        update();
    }
}

void Ps2Home::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    const int amount = std::abs(delta.x()) > std::abs(delta.y()) ? delta.x() : delta.y();
    if (amount != 0) {
        Move(0, amount < 0 ? 1 : -1);
    }
    event->accept();
}

void Ps2Home::contextMenuEvent(QContextMenuEvent* event) {
    if (screen != Screen::Browser || !game_options) {
        return;
    }
    const int hit = HitTest(event->pos());
    if (hit >= 0 && hit < shown.size()) {
        game_choice = hit;
        update();
        game_options(shown.at(hit).path, event->globalPos());
    }
}

void Ps2Home::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    arrival = 0.0;
    screen_before = screen;
    last_tick_ms = clock.elapsed();
    frame_timer.start();
}

void Ps2Home::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    frame_timer.stop();
}

// ---- Drawing ----

void Ps2Home::DrawGlowText(QPainter& painter, const QRectF& rect, int flags, const QString& text,
                           bool chosen, qreal alpha) {
    const auto& c = Ps2Look::Current();
    if (!chosen) {
        painter.setPen(Alpha(c.faint, alpha));
        painter.drawText(rect, flags, text);
        return;
    }
    // What is chosen is lit from within: the light breathes.
    const qreal breath = 0.75 + 0.25 * std::sin(clock.elapsed() / 1000.0 * 2.0 * Pi / 1.8);
    // A halo: the same words many times over, faint, a little way off in every direction.
    const qreal spread = std::max(0.8, rect.height() * 0.035);
    painter.setPen(Alpha(c.glow, alpha * 0.085 * breath));
    for (int ring = 1; ring <= 3; ++ring) {
        const int steps = 8 * ring;
        for (int step = 0; step < steps; ++step) {
            const qreal angle = step * 2 * Pi / steps;
            painter.drawText(rect.translated(std::cos(angle) * spread * ring, std::sin(angle) * spread * ring),
                             flags, text);
        }
    }
    painter.setPen(Alpha(Mix(c.accent, Qt::white, 0.35 * breath), alpha));
    painter.drawText(rect, flags, text);
}

void Ps2Home::DrawLights(QPainter& painter, const QPointF& centre, qreal radius, qreal alpha) {
    // The console's seven lights, each on a ring of its own, turning slowly about the middle.
    const qreal seconds = clock.elapsed() / 1000.0;
    painter.setPen(Qt::NoPen);
    QRadialGradient heart(centre, radius * 1.9);
    heart.setColorAt(0, QColor(0x3E, 0x8C, 0xFF, static_cast<int>(70 * alpha)));
    heart.setColorAt(0.5, QColor(0x1C, 0x3C, 0xB0, static_cast<int>(26 * alpha)));
    heart.setColorAt(1, QColor(0, 0, 0, 0));
    painter.setBrush(heart);
    painter.drawEllipse(centre, radius * 1.9, radius * 1.9);
    struct Light {
        qreal depth;
        QPointF at;
        qreal size;
        int index;
    };
    QList<Light> lights;
    for (int index = 0; index < 7; ++index) {
        const qreal tilt = 0.5 + index * 0.37;
        const qreal angle = seconds * (0.22 + index * 0.031) + index * 2.0 * Pi / 7;
        const qreal x = std::cos(angle) * radius;
        const qreal y = std::sin(angle) * radius * std::cos(tilt);
        const qreal z = std::sin(angle) * std::sin(tilt);
        lights.append({z, centre + QPointF(x * std::cos(index * 0.9) - y * std::sin(index * 0.9),
                                           x * std::sin(index * 0.9) + y * std::cos(index * 0.9)),
                       radius * (0.2 + 0.06 * z), index});
    }
    std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b) { return a.depth < b.depth; });
    for (const Light& light : lights) {
        const qreal near = 0.55 + 0.45 * (light.depth + 1) / 2;
        QRadialGradient glow(light.at, light.size * 2.6);
        glow.setColorAt(0, QColor(0x6E, 0xB4, 0xFF, static_cast<int>(90 * alpha * near)));
        glow.setColorAt(1, QColor(0x1C, 0x4C, 0xFF, 0));
        painter.setBrush(glow);
        painter.drawEllipse(light.at, light.size * 2.6, light.size * 2.6);
        QRadialGradient glass(light.at - QPointF(light.size * 0.3, light.size * 0.35), light.size * 1.25);
        glass.setColorAt(0, QColor(0xFF, 0xFF, 0xFF, static_cast<int>(235 * alpha * near)));
        glass.setColorAt(0.35, QColor(0x9C, 0xD4, 0xFF, static_cast<int>(200 * alpha * near)));
        glass.setColorAt(1, QColor(0x1E, 0x50, 0xE0, static_cast<int>(120 * alpha * near)));
        painter.setBrush(glass);
        painter.drawEllipse(light.at, light.size, light.size);
    }
}

void Ps2Home::DrawGround(QPainter& painter, Screen which, qreal alpha) {
    const auto& c = Ps2Look::Current();
    const qreal seconds = clock.elapsed() / 1000.0;
    painter.setPen(Qt::NoPen);
    if (which == Screen::Browser) {
        // The Browser's haze: grey-blue, lighter towards the top, with slow clouds in it.
        QLinearGradient haze(0, 0, 0, height());
        haze.setColorAt(0, QColor(0x6F, 0x7C, 0x93, static_cast<int>(255 * alpha)));
        haze.setColorAt(0.55, QColor(0x39, 0x44, 0x5C, static_cast<int>(255 * alpha)));
        haze.setColorAt(1, QColor(0x12, 0x17, 0x26, static_cast<int>(255 * alpha)));
        painter.fillRect(rect(), haze);
        for (int cloud = 0; cloud < 4; ++cloud) {
            const qreal phase = seconds * (0.035 + cloud * 0.011) + cloud * 0.31;
            const QPointF at(width() * (0.5 + 0.55 * std::sin(phase * 2 * Pi)),
                             height() * (0.3 + 0.16 * cloud + 0.05 * std::cos(phase * 5)));
            const qreal reach = width() * (0.34 + 0.05 * cloud);
            QRadialGradient fog(at, reach);
            fog.setColorAt(0, QColor(0xC8, 0xD6, 0xF0, static_cast<int>(34 * alpha)));
            fog.setColorAt(1, QColor(0xC8, 0xD6, 0xF0, 0));
            painter.setBrush(fog);
            painter.drawEllipse(at, reach, reach * 0.55);
        }
        return;
    }
    QRadialGradient deep(QPointF(width() * 0.62, height() * 0.5), std::max(width(), height()) * 0.75);
    deep.setColorAt(0, Alpha(Mix(c.ground, c.glow, 0.10), alpha));
    deep.setColorAt(0.55, Alpha(c.ground, alpha));
    deep.setColorAt(1, Alpha(c.ground_low, alpha));
    painter.fillRect(rect(), deep);
    // Motes of light adrift in the dark.
    for (int mote = 0; mote < 26; ++mote) {
        const qreal seed = mote * 12.9898;
        const qreal fx = std::fmod(std::abs(std::sin(seed) * 43758.5453), 1.0);
        const qreal fy = std::fmod(std::abs(std::sin(seed * 1.7) * 24634.6345), 1.0);
        const qreal drift = seconds * (0.006 + 0.004 * (mote % 5));
        const QPointF at(width() * std::fmod(fx + drift, 1.0),
                         height() * (fy + 0.02 * std::sin(seconds * 0.4 + mote)));
        const qreal twinkle = 0.4 + 0.6 * std::abs(std::sin(seconds * 0.5 + mote * 0.7));
        painter.setBrush(QColor(0x8C, 0xBE, 0xFF, static_cast<int>(70 * alpha * twinkle)));
        const qreal size = (0.7 + (mote % 3) * 0.5) * Unit();
        painter.drawEllipse(at, size, size);
    }
}

void Ps2Home::DrawHints(QPainter& painter, const QStringList& hints, qreal alpha) {
    // Pairs of a mark and what its button does, from the right.
    const auto& c = Ps2Look::Current();
    const qreal unit = Unit();
    QFont font = QApplication::font();
    font.setPixelSize(std::max(9, static_cast<int>(15 * unit)));
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    qreal x = width() - 46 * unit;
    const qreal y = height() - 44 * unit;
    for (int index = static_cast<int>(hints.size()) - 2; index >= 0; index -= 2) {
        const qreal text_width = metrics.horizontalAdvance(hints.at(index + 1));
        x -= text_width;
        painter.setPen(Alpha(c.text, alpha));
        painter.drawText(QRectF(x, y, text_width + 2, 22 * unit), Qt::AlignVCenter | Qt::AlignLeft,
                         hints.at(index + 1));
        x -= 24 * unit;
        DrawMark(painter, hints.at(index), QRectF(x, y + 1 * unit, 20 * unit, 20 * unit), alpha);
        x -= 22 * unit;
    }
}

void Ps2Home::DrawMenu(QPainter& painter, qreal alpha) {
    const qreal unit = Unit();
    DrawLights(painter, QPointF(width() * 0.68, height() * 0.48), 78 * unit, alpha);
    QFont font = QApplication::font();
    font.setPixelSize(std::max(11, static_cast<int>(27 * unit)));
    font.setWeight(QFont::Light);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.6 * unit);
    painter.setFont(font);
    const QStringList names{tr("Browser"), tr("System Configuration")};
    for (int index = 0; index < names.size(); ++index) {
        DrawGlowText(painter, MenuRect(index), Qt::AlignVCenter | Qt::AlignLeft, names.at(index),
                     index == menu_choice, alpha);
    }
    DrawHints(painter, {QStringLiteral("cross"), tr("Enter")}, alpha);
}

void Ps2Home::DrawBrowser(QPainter& painter, qreal alpha) {
    const auto& c = Ps2Look::Current();
    const qreal unit = Unit();
    const qreal seconds = clock.elapsed() / 1000.0;
    QFont font = QApplication::font();

    // The games, afloat.
    painter.save();
    // Between the names along the top and the buttons' meaning along the bottom.
    painter.setClipRect(QRectF(0, (GridTop - 26) * unit, width(), height() - (GridTop - 26 + 62) * unit));
    if (shown.isEmpty()) {
        font.setPixelSize(std::max(11, static_cast<int>(22 * unit)));
        font.setWeight(QFont::Light);
        painter.setFont(font);
        painter.setPen(Alpha(QColor(0xE6, 0xEE, 0xFF), alpha));
        painter.drawText(QRectF(0, height() * 0.4, width(), 40 * unit), Qt::AlignCenter,
                         all.isEmpty() ? tr("No data") : tr("No game is called that"));
    }
    for (int index = 0; index < shown.size(); ++index) {
        QRectF tile = TileRect(index);
        if (tile.bottom() < 0 || tile.top() > height()) {
            continue;
        }
        const bool chosen = index == game_choice;
        // Each floats a little, out of step with its neighbours; the chosen one comes forward.
        tile.translate(0, std::sin(seconds * 1.1 + index * 1.3) * 2.2 * unit);
        if (chosen) {
            const qreal grow = 0.11 * (1.0 - std::pow(1.0 - choice_arrival, 3.0));
            tile.adjust(-tile.width() * grow / 2, -tile.height() * grow / 2, tile.width() * grow / 2,
                        tile.height() * grow / 2);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, static_cast<int>(70 * alpha)));
        painter.drawEllipse(QPointF(tile.center().x(), tile.bottom() + 9 * unit), tile.width() * 0.46, 5 * unit);
        if (chosen) {
            const qreal breath = 0.6 + 0.4 * std::sin(seconds * 2.0 * Pi / 1.8);
            for (int ring = 3; ring >= 1; --ring) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(0xBE, 0xE2, 0xFF, static_cast<int>(alpha * breath * 60 / ring)),
                                    ring * 3.0 * unit));
                painter.drawRoundedRect(tile, 3 * unit, 3 * unit);
            }
            painter.setPen(Qt::NoPen);
        }
        const Game& game = shown.at(index);
        painter.setOpacity(alpha * (chosen ? 1.0 : 0.82));
        if (game.picture.isNull()) {
            // No picture: a plain case with the game's name on it.
            QLinearGradient plain(tile.topLeft(), tile.bottomRight());
            plain.setColorAt(0, QColor(0x2A, 0x3C, 0x74));
            plain.setColorAt(1, QColor(0x0E, 0x14, 0x30));
            painter.setBrush(plain);
            painter.drawRoundedRect(tile, 3 * unit, 3 * unit);
            painter.setBrush(QColor(0x08, 0x0C, 0x1C));
            painter.drawRect(QRectF(tile.left(), tile.top(), tile.width(), 12 * unit));
            font.setPixelSize(std::max(7, static_cast<int>(10.5 * unit)));
            font.setWeight(QFont::Normal);
            painter.setFont(font);
            painter.setPen(QColor(0xDC, 0xE8, 0xFF));
            painter.drawText(tile.adjusted(6 * unit, 18 * unit, -6 * unit, -6 * unit),
                             Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWrapAnywhere, game.name);
            painter.setPen(Qt::NoPen);
        } else {
            painter.drawPixmap(tile, game.picture, QRectF(game.picture.rect()));
        }
        painter.setOpacity(1.0);
        if (game.readable) {
            // A game whose text can be read carries a boxed book in its corner.
            const qreal side = 17 * unit;
            const QRectF box(tile.right() - side - 3 * unit, tile.top() + 3 * unit, side, side);
            painter.setPen(QPen(Alpha(QColor(0xF5, 0xC5, 0x18), alpha), std::max(1.0, 1.2 * unit)));
            painter.setBrush(QColor(0x0A, 0x10, 0x26, static_cast<int>(230 * alpha)));
            painter.drawRoundedRect(box, 3 * unit, 3 * unit);
            QPainterPath book;
            const QPointF m = box.center();
            const qreal k = side / 40.0;
            book.moveTo(m + QPointF(0, -8 * k));
            book.cubicTo(m + QPointF(-5 * k, -12 * k), m + QPointF(-11 * k, -12 * k), m + QPointF(-14 * k, -10 * k));
            book.lineTo(m + QPointF(-14 * k, 9 * k));
            book.cubicTo(m + QPointF(-11 * k, 7 * k), m + QPointF(-5 * k, 7 * k), m + QPointF(0, 11 * k));
            book.cubicTo(m + QPointF(5 * k, 7 * k), m + QPointF(11 * k, 7 * k), m + QPointF(14 * k, 9 * k));
            book.lineTo(m + QPointF(14 * k, -10 * k));
            book.cubicTo(m + QPointF(11 * k, -12 * k), m + QPointF(5 * k, -12 * k), m + QPointF(0, -8 * k));
            book.lineTo(m + QPointF(0, 11 * k));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(book);
            painter.setPen(Qt::NoPen);
        }
    }
    painter.restore();

    // In the top right corner, the chosen game's name, as the console names what is chosen.
    font.setPixelSize(std::max(10, static_cast<int>(21 * unit)));
    font.setWeight(QFont::Light);
    painter.setFont(font);
    const QRectF corner(width() * 0.3, 26 * unit, width() * 0.7 - 46 * unit, 30 * unit);
    if (!shown.isEmpty()) {
        const Game& game = shown.at(game_choice);
        const QFontMetricsF metrics(font);
        const qreal fade = 0.3 + 0.7 * choice_arrival;
        painter.setPen(Alpha(QColor(0, 0, 0, 120), alpha));
        painter.drawText(corner.translated(1.5 * unit, 1.5 * unit), Qt::AlignRight | Qt::AlignVCenter,
                         metrics.elidedText(game.name, Qt::ElideRight, corner.width()));
        painter.setPen(Alpha(QColor(0xF2, 0xF6, 0xFF), alpha * fade));
        painter.drawText(corner, Qt::AlignRight | Qt::AlignVCenter,
                         metrics.elidedText(game.name, Qt::ElideRight, corner.width()));
        font.setPixelSize(std::max(8, static_cast<int>(13 * unit)));
        painter.setFont(font);
        QString below = game.serial;
        if (game.readable) {
            below += (below.isEmpty() ? QString() : QStringLiteral("  ·  ")) + tr("Its text can be read");
        }
        painter.setPen(Alpha(QColor(0xD2, 0xDE, 0xF6), alpha * fade * 0.9));
        painter.drawText(corner.translated(0, 26 * unit), Qt::AlignRight | Qt::AlignVCenter, below);
    }
    font.setPixelSize(std::max(8, static_cast<int>(14 * unit)));
    painter.setFont(font);
    painter.setPen(Alpha(QColor(0xE6, 0xEE, 0xFF), alpha * 0.85));
    painter.drawText(QRectF(46 * unit, 26 * unit, width() * 0.4, 30 * unit), Qt::AlignLeft | Qt::AlignVCenter,
                     filter.isEmpty() ? tr("Browser") : tr("Search: %1").arg(filter));

    if (shown.isEmpty()) {
        DrawHints(painter, {QStringLiteral("cross"), all.isEmpty() ? tr("Add games") : tr("Enter"),
                            QStringLiteral("circle"), tr("Back")},
                  alpha);
    } else {
        DrawHints(painter, {QStringLiteral("cross"), tr("Enter"), QStringLiteral("circle"), tr("Back"),
                            QStringLiteral("triangle"), tr("Options")},
                  alpha);
    }
    Q_UNUSED(c);
}

void Ps2Home::DrawConfiguration(QPainter& painter, qreal alpha) {
    const auto& c = Ps2Look::Current();
    const qreal unit = Unit();
    DrawLights(painter, QPointF(width() * 0.84, height() * 0.2), 34 * unit, alpha * 0.55);
    QFont font = QApplication::font();
    font.setPixelSize(std::max(10, static_cast<int>(23 * unit)));
    font.setWeight(QFont::Light);
    painter.setFont(font);
    painter.setPen(Alpha(c.accent, alpha));
    painter.drawText(QRectF(46 * unit, 34 * unit, width() - 92 * unit, 36 * unit), Qt::AlignLeft | Qt::AlignVCenter,
                     tr("System Configuration"));
    painter.setPen(QPen(Alpha(c.line, alpha), 1));
    painter.drawLine(QPointF(46 * unit, 78 * unit), QPointF(width() - 46 * unit, 78 * unit));

    font.setPixelSize(std::max(9, static_cast<int>(18 * unit)));
    painter.setFont(font);
    const int rows = SettingRows();
    // More lines above or below what is shown are said with a small arrowhead.
    painter.setPen(QPen(Alpha(c.faint, alpha), std::max(1.0, 1.4 * unit), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal arrow_x = width() / 2.0;
    if (setting_scroll > 0) {
        const qreal y = (RowsTop - 12) * unit;
        painter.drawPolyline(QPolygonF{QPointF(arrow_x - 7 * unit, y), QPointF(arrow_x, y - 6 * unit), QPointF(arrow_x + 7 * unit, y)});
    }
    if (setting_scroll + rows < settings.size()) {
        const qreal y = (RowsTop + rows * RowStep + 6) * unit;
        painter.drawPolyline(QPolygonF{QPointF(arrow_x - 7 * unit, y), QPointF(arrow_x, y + 6 * unit), QPointF(arrow_x + 7 * unit, y)});
    }
    for (int index = setting_scroll; index < settings.size() && index < setting_scroll + rows; ++index) {
        const QRectF row = SettingRect(index);
        const bool chosen = index == setting_choice;
        if (chosen) {
            QLinearGradient band(row.topLeft(), row.topRight());
            band.setColorAt(0, QColor(0x3E, 0x8C, 0xFF, 0));
            band.setColorAt(0.5, QColor(0x3E, 0x8C, 0xFF, static_cast<int>(46 * alpha * (0.4 + 0.6 * choice_arrival))));
            band.setColorAt(1, QColor(0x3E, 0x8C, 0xFF, 0));
            painter.fillRect(row.adjusted(-30 * unit, 2 * unit, 30 * unit, -2 * unit), band);
        }
        DrawGlowText(painter, row, Qt::AlignVCenter | Qt::AlignLeft, settings.at(index).name, chosen, alpha);
        if (settings.at(index).value) {
            painter.setPen(Alpha(chosen ? c.text : c.faint, alpha));
            painter.drawText(row, Qt::AlignVCenter | Qt::AlignRight, settings.at(index).value());
        }
    }
    DrawHints(painter, {QStringLiteral("cross"), tr("Enter"), QStringLiteral("circle"), tr("Back")}, alpha);
}

void Ps2Home::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), Ps2Look::Current().ground_low);
    const qreal eased = 1.0 - std::pow(1.0 - arrival, 2.0);
    const auto draw = [this, &painter](Screen which, qreal alpha) {
        if (alpha <= 0.004) {
            return;
        }
        DrawGround(painter, which, alpha);
        if (which == Screen::Menu) {
            DrawMenu(painter, alpha);
        } else if (which == Screen::Browser) {
            DrawBrowser(painter, alpha);
        } else {
            DrawConfiguration(painter, alpha);
        }
    };
    // One screen gives way to the next.
    if (arrival < 1.0 && screen_before != screen) {
        draw(screen_before, 1.0);
        draw(screen, eased);
    } else {
        draw(screen, 1.0);
    }
    if (!isEnabled()) {
        painter.fillRect(rect(), QColor(0, 0, 0, 130));
    }
}

} // namespace ReaderUi
