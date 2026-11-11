// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QHash>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include "app_icon.h"
#ifndef HOME_PUBLIC
#include "feedback_dialog.h"
#endif
#include "home_window.h"
#include "popup_design.h"
#include "popup_style.h"
#include "reader/desktop/popup_model.h"
#include "reader/qt/lookup_popup.h"
#include "reader/qt/popup_web.h"
#include "reader/qt/reader_session.h"
#include "settings_window.h"

namespace Home {

namespace {

QString JsString(const QString& text) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

/// How a value of a setting reads in the window.
QString Shown(const PlaySetting& setting, const QString& value) {
    if (setting.kind == PlaySetting::Switch) {
        return value == QStringLiteral("1") ? QStringLiteral("On") : QStringLiteral("Off");
    }
    return setting.Shown(value).toLower();
}

} // namespace

SettingsWindow::SettingsWindow(QWidget* parent, PlaySettings& play_, const QList<PlayConsole>& consoles_,
                               QString popup_folder_, HomeWindow* home_, Hooks hooks_)
    : QDialog(parent), play{play_}, consoles{consoles_}, popup_folder{std::move(popup_folder_)}, home{home_},
      hooks{std::move(hooks_)} {
    setWindowTitle(QStringLiteral("Furoshiki settings"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(980, 680);
    tabs = new QTabWidget(this);
    tabs->addTab(PopupTab(), QStringLiteral("Popup"));
    tabs->addTab(ConsolesTab(), QStringLiteral("Consoles"));
    tabs->addTab(CrtTab(), QStringLiteral("CRT"));
    tabs->addTab(ControllerTab(), QStringLiteral("Controller"));
#ifndef READER_NO_SAVE_SYNC
    tabs->addTab(SyncTab(), QStringLiteral("Save sync"));
#endif
    tabs->addTab(AchievementsTab(), QStringLiteral("Achievements"));
    tabs->addTab(ReadingTab(), QStringLiteral("Reading"));
#ifndef READER_NO_TEXT_SERVER
    tabs->addTab(TextServerTab(), QStringLiteral("Text server"));
#endif
    tabs->addTab(LookTab(), QStringLiteral("Furoshiki"));
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    auto* foot = new QHBoxLayout;
    close->setToolTip(QStringLiteral("Changes count at once, as on the pages; F2 opens this window."));
#ifndef HOME_PUBLIC
    auto* feedback = new QPushButton(QStringLiteral("Send feedback…"), this);
    connect(feedback, &QPushButton::clicked, this, [this] {
        auto* box = new FeedbackDialog(this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    });
    foot->addWidget(feedback);
#endif
    foot->addStretch(1);
    foot->addWidget(close);
    layout->addLayout(foot);
    for (QComboBox* box : findChildren<QComboBox*>()) {
        Calm(box);
    }
    for (QSpinBox* box : findChildren<QSpinBox*>()) {
        Calm(box);
    }
}

void SettingsWindow::Calm(QWidget* box) {
    box->setFocusPolicy(Qt::StrongFocus);
    box->installEventFilter(this);
}

bool SettingsWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        const auto* box = qobject_cast<QWidget*>(watched);
        if (box && !box->hasFocus()) {
            event->ignore();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SettingsWindow::Refresh() {
    FillPopupRows();
    FillConsoleRows();
    FillCrtRows();
    pointer_button->setCurrentIndex(std::max(0, pointer_button->findData(ReaderUi::ReaderSession::Instance().Settings().CursorButton())));
#ifndef READER_NO_SAVE_SYNC
    LoadSync();
#endif
    ShowPreview();
}

void SettingsWindow::ShowTab(const QString& name) {
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->tabText(i) == name) {
            tabs->setCurrentIndex(i);
            // One editor, opened from its row of the settings pages: the other tabs are rows there.
            tabs->tabBar()->hide();
            setWindowTitle(name == QStringLiteral("Popup") ? QStringLiteral("The popup") : name);
        }
    }
}

// ---- Layers ----

void SettingsWindow::FillLayers(QComboBox* chooser) {
    chooser->clear();
    chooser->addItem(QStringLiteral("All consoles"), QStringList{});
    for (const PlayConsole& console : consoles) {
        chooser->addItem(QStringLiteral("Only %1").arg(console.name), QStringList{console.key});
    }
    QString console, game, title;
    if (hooks.playing) {
        hooks.playing(console, game, title);
    }
    if (!console.isEmpty() && !game.isEmpty()) {
        chooser->addItem(QStringLiteral("Only this game: %1").arg(title.isEmpty() ? game : title),
                         QStringList{console, game});
    }
}

SettingsWindow::Layer SettingsWindow::LayerOf(const QComboBox* chooser) const {
    const QStringList data = chooser->currentData().toStringList();
    return {data.value(0), data.value(1)};
}

QString SettingsWindow::Next(const PlaySetting& setting, const Layer& layer) const {
    // What shows where this layer has nothing: the game's console, all consoles, the default.
    if (!layer.game.isEmpty()) {
        return play.Value(setting, layer.console).value;
    }
    if (!layer.console.isEmpty() && setting.Common() && play.IsSet(setting.key)) {
        return play.Raw(setting.key); // all consoles'
    }
    return setting.default_value;
}

void SettingsWindow::Set(const QString& key, const QString& value, const Layer& layer) {
    play.Set(key, value, layer.console, layer.game);
    play.Save();
    if (home) {
        home->SettingsChanged();
    }
    if (!key.startsWith(QStringLiteral("popup.")) && hooks.applied) {
        hooks.applied();
    }
}

void SettingsWindow::AddSettingRow(QFormLayout* form, const PlaySetting& setting, const Layer& layer) {
    const QString label = setting.label.isEmpty() ? CommonLabel(setting.key) : setting.label;
    const bool own = play.IsSet(setting.key, layer.console, layer.game);
    const QString next = Next(setting, layer);
    const QString same = layer.console.isEmpty() ? QStringLiteral("As each console has it (%1)")
                         : layer.game.isEmpty()  ? QStringLiteral("Same as all consoles (%1)")
                                                 : QStringLiteral("Same as its console (%1)");
    const QString key = setting.key;
    if (setting.kind == PlaySetting::Number) {
        auto* box = new QSpinBox(this);
        const int step = static_cast<int>(setting.step > 0 ? setting.step : 1);
        box->setRange(static_cast<int>(setting.low) - step, static_cast<int>(setting.high));
        box->setSingleStep(step);
        box->setSpecialValueText(same.arg(next));
        box->setValue(own ? play.Raw(key, layer.console, layer.game).toInt() : box->minimum());
        connect(box, &QSpinBox::valueChanged, this, [this, box, key, layer](int value) {
            if (!filling) {
                Set(key, value == box->minimum() ? QString() : QString::number(value), layer);
            }
        });
        Calm(box);
        form->addRow(label, box);
        return;
    }
    auto* box = new QComboBox(this);
    box->addItem(same.arg(Shown(setting, next)), QString());
    const QStringList values = setting.kind == PlaySetting::Switch ? QStringList{QStringLiteral("1"), QStringLiteral("0")}
                                                                    : setting.choices;
    for (const QString& value : values) {
        box->addItem(Shown(setting, value), value);
    }
    box->setCurrentIndex(own ? std::max(0, box->findData(play.Raw(key, layer.console, layer.game))) : 0);
    connect(box, &QComboBox::currentIndexChanged, this, [this, box, key, layer](int) {
        if (filling) {
            return;
        }
        Set(key, box->currentData().toString(), layer);
        if (key == QStringLiteral("popup.theme")) {
            // Another look: the design over the one before is left, and the designs offered are
            // this look's (the maintainer: "when changing base popup that ... zeroes out").
            Set(QStringLiteral("popup.design"), QString(), layer);
            QTimer::singleShot(0, this, [this] {
                FillPopupRows();
                ShowPreview();
            });
            return;
        }
        if (key == QStringLiteral("popup.design")) {
            QTimer::singleShot(0, this, [this] {
                LoadDesign();
                ShowPreview();
            });
            return;
        }
        if (key.startsWith(QStringLiteral("popup."))) {
            ShowPreview();
        }
    });
    Calm(box);
    form->addRow(label, box);
}

// ---- The popup ----

QWidget* SettingsWindow::PopupTab() {
    auto* page = new QWidget(this);
    // At the head: for which consoles, the look, the design over it; the style as a file.
    auto* top = new QHBoxLayout;
    popup_layer = new QComboBox(page);
    FillLayers(popup_layer);
    connect(popup_layer, &QComboBox::currentIndexChanged, this, [this] {
        FillPopupRows();
        ShowPreview();
    });
    auto* layer_form = new QFormLayout;
    layer_form->addRow(QStringLiteral("For"), popup_layer);
    top->addLayout(layer_form, 2);
    look_form = new QFormLayout;
    design_form = new QFormLayout;
    top->addLayout(look_form, 2);
    top->addLayout(design_form, 2);
    auto* export_style = new QPushButton(QStringLiteral("Export…"), page);
    auto* import_style = new QPushButton(QStringLiteral("Import…"), page);
    export_style->setToolTip(QStringLiteral("The popup's style as one file, which the Android apps read too"));
    top->addWidget(export_style);
    top->addWidget(import_style);
    settings_page = new QWidget(page);
    popup_form = new QFormLayout(settings_page);

    // The customiser: the popup's elements, the one picked (here or by a click in the preview), its
    // settings; what is changed is a design over the look, drawn in the preview at once.
    auto* split = new QSplitter(Qt::Horizontal, page);
    CustomiserPanel(split);

    // The preview, drawn as the popup is.
    auto* right = new QWidget(split);
    auto* right_layout = new QVBoxLayout(right);
    right_layout->addWidget(new QLabel(QStringLiteral("Preview"), right));
    preview = ReaderUi::PopupWeb::Create(right, ReaderUi::ReaderSession::DataDirectory() + QStringLiteral("/webview2"));
    if (preview) {
        preview->setMinimumSize(380, 320);
        right_layout->addWidget(preview, 1);
    } else {
        auto* none = new QLabel(QStringLiteral("No preview here: this build or this computer has no web engine for the "
                                               "popup page (WebView2 on Windows, Qt WebEngine on Linux), so the popup is "
                                               "drawn classic."),
                                right);
        none->setWordWrap(true);
        right_layout->addWidget(none, 1);
    }
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 3);
    auto* layout = new QVBoxLayout(page);
    layout->addLayout(top);
    layout->addWidget(split, 1);

    preview_timer = new QTimer(this);
    preview_timer->setSingleShot(true);
    preview_timer->setInterval(250);
    connect(preview_timer, &QTimer::timeout, this, &SettingsWindow::ShowPreview);
    connect(export_style, &QPushButton::clicked, this, [this] {
        const Layer layer = LayerOf(popup_layer);
        auto& settings = ReaderUi::ReaderSession::Instance().Settings();
        const QJsonObject style = PopupStyle::Export(play, popup_folder, layer.console, layer.game,
                                                     [&settings](const QString& key) { return settings.Value(key).toString(); });
        const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Export the popup style"),
                                                          popup_folder + QStringLiteral("/popup-style.json"),
                                                          QStringLiteral("Popup style (*.json)"));
        if (file.isEmpty()) {
            return;
        }
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(style).toJson()) < 0 || !out.commit()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("The style could not be written."));
        }
    });
    connect(import_style, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Import a popup style"), popup_folder,
                                                          QStringLiteral("Popup style (*.json)"));
        if (file.isEmpty()) {
            return;
        }
        QFile in(file);
        const QJsonObject style = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
        const Layer layer = LayerOf(popup_layer);
        const QString failed = PopupStyle::Import(style, play, popup_folder,
                                                  QFileInfo(file).completeBaseName().remove(QStringLiteral("-popup-style")),
                                                  layer.console, layer.game);
        if (!failed.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Not imported: %1").arg(failed));
            return;
        }
        if (hooks.popup_files_changed) {
            hooks.popup_files_changed();
        }
        Refresh();
    });
    if (preview) {
        connect(preview, &ReaderUi::PopupWeb::Message, this, [this](const QJsonObject& message) {
            if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ready")) {
                ShowPreview();
            }
            PreviewMessage(message);
        });
    }
    FillPopupRows();
    return page;
}

void SettingsWindow::FillPopupRows() {
    filling = true;
    for (QFormLayout* form : {popup_form, look_form, design_form}) {
        while (form->rowCount() > 0) {
            form->removeRow(0);
        }
    }
    const Layer layer = LayerOf(popup_layer);
    QList<PlaySetting> settings = PopupStyle::Settings(popup_folder);
    const QString base = PopupBase();
    for (PlaySetting& setting : settings) {
        if (setting.key == QStringLiteral("popup.design")) {
            // Only the designs over the look in use here: "yomitan-dark-mine" with Yomitan dark.
            setting.choices = {QStringLiteral("none")};
            for (const PopupDesign::Design& one : PopupDesign::InFolder(popup_folder, base)) {
                setting.choices.append(one.name);
            }
        }
        AddSettingRow(setting.key == QStringLiteral("popup.theme") ? look_form : setting.key == QStringLiteral("popup.design") ? design_form : popup_form, setting, layer);
    }
    filling = false;
    LoadDesign();
}

namespace {

/// A control left to the look: "—", the look's own value (its tooltip says so).
constexpr auto LooksOwn = "—";

/// A colour button's face: the colour, or the look's own.
void ShowColour(QPushButton* button, const QString& value) {
    button->setProperty("value", value);
    if (value.isEmpty()) {
        button->setText(QString::fromUtf8(LooksOwn));
        button->setToolTip(QStringLiteral("The look's own; click to choose a colour"));
        button->setStyleSheet({});
        return;
    }
    // The colour itself, its value in the tooltip: the button is narrow.
    const QColor colour = QColor::fromString(value);
    button->setText({});
    button->setToolTip(colour.alpha() == 255 ? colour.name()
                                             : QStringLiteral("%1, %2% seen").arg(colour.name()).arg(colour.alpha() * 100 / 255));
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid #888; border-radius: 3px; min-height: 1.4em; }")
                              .arg(colour.name(QColor::HexArgb)));
}

} // namespace

// ---- The popup customiser (popup_design.h) ----

QWidget* SettingsWindow::CustomiserPanel(QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* head = new QHBoxLayout;
    design_title = new QLabel(box);
    QFont strong = design_title->font();
    strong.setBold(true);
    design_title->setFont(strong);
    head->addWidget(design_title, 1);
    design_rename = new QPushButton(QStringLiteral("Rename…"), box);
    design_remove = new QPushButton(QStringLiteral("Delete"), box);
    head->addWidget(design_rename);
    head->addWidget(design_remove);
    layout->addLayout(head);
    auto* middle = new QHBoxLayout;
    design_elements = new QListWidget(box);
    design_elements->setFixedWidth(180);
    auto* shows = new QListWidgetItem(QStringLiteral("What it shows"), design_elements);
    shows->setData(Qt::UserRole, QStringLiteral("settings"));
    for (const PopupDesign::Element& element : PopupDesign::Elements()) {
        auto* item = new QListWidgetItem(element.label, design_elements);
        item->setData(Qt::UserRole, element.id);
    }
    auto* order = new QListWidgetItem(QStringLiteral("Order of things"), design_elements);
    order->setData(Qt::UserRole, QStringLiteral("order"));
    middle->addWidget(design_elements);
    auto* scroll = new QScrollArea(box);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspector = new QWidget(scroll);
    new QFormLayout(inspector);
    scroll->setWidget(inspector);
    auto* settings_scroll = new QScrollArea(box);
    settings_scroll->setWidgetResizable(true);
    settings_scroll->setFrameShape(QFrame::NoFrame);
    settings_scroll->setWidget(settings_page);
    inspector_stack = new QStackedWidget(box);
    inspector_stack->addWidget(scroll);
    inspector_stack->addWidget(settings_scroll);
    middle->addWidget(inspector_stack, 1);
    layout->addLayout(middle, 1);
    auto* foot = new QHBoxLayout;
    design_reset_element = new QPushButton(QStringLiteral("Reset this"), box);
    design_reset_all = new QPushButton(QStringLiteral("Reset all"), box);
    foot->addWidget(design_reset_element);
    foot->addWidget(design_reset_all);
    foot->addStretch(1);
    layout->addLayout(foot);

    design_save_timer = new QTimer(this);
    design_save_timer->setSingleShot(true);
    design_save_timer->setInterval(300);
    connect(design_save_timer, &QTimer::timeout, this, [this] {
        if (!design.name.isEmpty() && PopupDesign::Save(popup_folder, design) && hooks.popup_files_changed) {
            hooks.popup_files_changed(); // the popup over a game draws it from now on
        }
    });
    connect(design_elements, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item != nullptr && !filling) {
            PickElement(item->data(Qt::UserRole).toString());
        }
    });
    connect(design_reset_element, &QPushButton::clicked, this, [this] {
        PopupDesign::Design& edited = EditedDesign();
        if (picked_element == QStringLiteral("order")) {
            edited.entry_order.clear();
            edited.head_order.clear();
        }
        for (const PopupDesign::Element& element : PopupDesign::Elements()) {
            if (element.id == picked_element) {
                for (const PopupDesign::Property& property : element.properties) {
                    edited.values.remove(property.key);
                }
            }
        }
        DesignChanged(true);
    });
    connect(design_reset_all, &QPushButton::clicked, this, [this] {
        PopupDesign::Design& edited = EditedDesign();
        edited.values = {};
        edited.entry_order.clear();
        edited.head_order.clear();
        DesignChanged(true);
    });
    connect(design_rename, &QPushButton::clicked, this, [this] {
        if (design.name.isEmpty()) {
            return;
        }
        bool ok = false;
        QString name = QInputDialog::getText(this, QStringLiteral("Rename the design"), QStringLiteral("Its name:"), QLineEdit::Normal, design.name, &ok).trimmed();
        name.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("-"));
        if (!ok || name.isEmpty() || name == design.name || name == QStringLiteral("none")) {
            return;
        }
        if (PopupDesign::Load(popup_folder, name)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("A design is called %1 already.").arg(name));
            return;
        }
        const QString was = design.name;
        design.name = name;
        if (!PopupDesign::Save(popup_folder, design)) {
            design.name = was;
            return;
        }
        PopupDesign::Remove(popup_folder, was);
        Set(QStringLiteral("popup.design"), name, LayerOf(popup_layer));
        if (hooks.popup_files_changed) {
            hooks.popup_files_changed();
        }
        FillPopupRows();
    });
    connect(design_remove, &QPushButton::clicked, this, [this] {
        if (design.name.isEmpty() ||
            QMessageBox::question(this, windowTitle(), QStringLiteral("Delete the design %1?").arg(PopupDesign::Shown(design.name, design.base))) != QMessageBox::Yes) {
            return;
        }
        PopupDesign::Remove(popup_folder, design.name);
        Set(QStringLiteral("popup.design"), QString(), LayerOf(popup_layer));
        if (hooks.popup_files_changed) {
            hooks.popup_files_changed();
        }
        FillPopupRows();
        ShowPreview();
    });
    return box;
}

QString SettingsWindow::PopupBase() const {
    const Layer layer = LayerOf(popup_layer);
    const QList<PlaySetting> settings = PopupStyle::Settings(popup_folder);
    const QString theme = play.Value(settings.first(), layer.console, layer.game).value;
    return theme == QStringLiteral("classic") ? QStringLiteral("console") : theme;
}

QString SettingsWindow::PopupDesignName() const {
    const Layer layer = LayerOf(popup_layer);
    for (const PlaySetting& setting : PopupStyle::Settings(popup_folder)) {
        if (setting.key == QStringLiteral("popup.design")) {
            const QString name = play.Value(setting, layer.console, layer.game).value;
            const auto found = PopupDesign::Load(popup_folder, name);
            return found && found->base == PopupBase() ? name : QString();
        }
    }
    return {};
}

void SettingsWindow::LoadDesign() {
    const QString base = PopupBase(), name = PopupDesignName();
    const auto found = PopupDesign::Load(popup_folder, name);
    design = found ? *found : PopupDesign::Design{QString(), base, {}, {}, {}};
    design_loaded = true;
    if (design_title) {
        design_title->setText(design.name.isEmpty() ? QStringLiteral("%1, as it is: change anything to make it yours").arg(PopupDesign::Shown(base, {}))
                                                    : PopupDesign::Shown(design.name, base));
        design_rename->setEnabled(!design.name.isEmpty());
        design_remove->setEnabled(!design.name.isEmpty());
    }
    FillInspector();
}

PopupDesign::Design& SettingsWindow::EditedDesign() {
    const QString base = PopupBase();
    if (design.name.isEmpty() || design.base != base) {
        // The look as it is is changed: a design of its own over it, "yomitan-dark-mine", in use here.
        design = PopupDesign::Design{PopupDesign::FreeName(popup_folder, base), base, design.base == base ? design.values : QJsonObject(), {}, {}};
        PopupDesign::Save(popup_folder, design);
        Set(QStringLiteral("popup.design"), design.name, LayerOf(popup_layer));
        if (hooks.popup_files_changed) {
            hooks.popup_files_changed();
        }
        const QString keep = picked_element;
        QTimer::singleShot(0, this, [this, keep] {
            // The design row offers it now; the customiser stays where it was.
            const PopupDesign::Design edited = design;
            FillPopupRows();
            design = edited;
            picked_element = keep;
            FillInspector();
        });
    }
    return design;
}

void SettingsWindow::DesignChanged(bool refill) {
    if (preview) {
        preview->Call(QStringLiteral("Reader.setStyle(\"user\", %1)").arg(JsString(PopupDesign::Css(design))));
    }
    design_save_timer->start();
    if (refill) {
        FillInspector();
    }
}

void SettingsWindow::PickElement(const QString& id) {
    picked_element = id;
    filling = true;
    for (int i = 0; i < design_elements->count(); ++i) {
        if (design_elements->item(i)->data(Qt::UserRole).toString() == id) {
            design_elements->setCurrentRow(i);
        }
    }
    filling = false;
    if (preview) {
        preview->Call(QStringLiteral("Reader.pick(%1)").arg(JsString(id)));
    }
    FillInspector();
}

void SettingsWindow::FillInspector() {
    if (inspector == nullptr) {
        return;
    }
    inspector_sync.clear();
    inspector_stack->setCurrentIndex(picked_element == QStringLiteral("settings") ? 1 : 0);
    design_reset_element->setEnabled(picked_element != QStringLiteral("settings"));
    auto* form = static_cast<QFormLayout*>(inspector->layout());
    while (form->rowCount() > 0) {
        form->removeRow(0);
    }
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const QString look_own = QStringLiteral("As the look has it");
    if (picked_element == QStringLiteral("order")) {
        // The order of an entry's blocks and of its headline's parts: dragged here, or in the preview.
        const auto list = [&](const QList<QPair<QString, QString>>& all, QStringList order, bool head) {
            for (const auto& [id, label] : all) {
                if (!order.contains(id)) {
                    order.append(id);
                }
            }
            auto* widget = new QListWidget(inspector);
            widget->setDragDropMode(QAbstractItemView::InternalMove);
            widget->setDefaultDropAction(Qt::MoveAction);
            for (const QString& id : order) {
                for (const auto& [one, label] : all) {
                    if (one == id) {
                        auto* item = new QListWidgetItem(label, widget);
                        item->setData(Qt::UserRole, id);
                    }
                }
            }
            widget->setFixedHeight(widget->sizeHintForRow(0) * static_cast<int>(order.size()) + 8);
            connect(widget->model(), &QAbstractItemModel::rowsMoved, this, [this, widget, head] {
                QStringList now;
                for (int i = 0; i < widget->count(); ++i) {
                    now.append(widget->item(i)->data(Qt::UserRole).toString());
                }
                PopupDesign::Design& edited = EditedDesign();
                (head ? edited.head_order : edited.entry_order) = now;
                DesignChanged(false);
            });
            return widget;
        };
        form->addRow(QStringLiteral("A word's blocks"), list(PopupDesign::Blocks(), design.entry_order, false));
        form->addRow(QStringLiteral("Its headline"), list(PopupDesign::HeadParts(), design.head_order, true));
        auto* note = new QLabel(QStringLiteral("Or drag them in the preview."), inspector);
        note->setEnabled(false);
        form->addRow(note);
        return;
    }
    for (const PopupDesign::Element& element : PopupDesign::Elements()) {
        if (element.id != picked_element) {
            continue;
        }
        for (const PopupDesign::Property& property : element.properties) {
            const QString key = property.key;
            auto* row = new QHBoxLayout;
            auto* reset = new QToolButton(inspector);
            reset->setText(QStringLiteral("↺"));
            reset->setToolTip(QStringLiteral("Back to the look's own"));
            const auto set = [this, key](const QJsonValue& value) {
                PopupDesign::Design& edited = EditedDesign();
                if (value.isNull() || value.isUndefined()) {
                    edited.values.remove(key);
                } else {
                    edited.values.insert(key, value);
                }
                DesignChanged(false);
                if (const auto sync = inspector_sync.value(key)) {
                    sync();
                }
            };
            connect(reset, &QToolButton::clicked, this, [set] { set(QJsonValue()); });
            if (property.kind == PopupDesign::Property::Size || property.kind == PopupDesign::Property::Number) {
                auto* slider = new QSlider(Qt::Horizontal, inspector);
                const double step = property.step > 0 ? property.step : 1;
                slider->setRange(static_cast<int>(std::lround(property.low / step)), static_cast<int>(std::lround(property.high / step)));
                auto* shown = new QLabel(inspector);
                shown->setMinimumWidth(92);
                row->addWidget(slider, 1);
                row->addWidget(shown);
                const auto sync = [this, slider, shown, reset, key, step, property] {
                    const QJsonValue value = design.values.value(key);
                    const bool own = value.isUndefined();
                    const QSignalBlocker quiet(slider);
                    slider->setValue(static_cast<int>(std::lround((own ? property.start : value.toDouble()) / step)));
                    shown->setText(own ? QStringLiteral("look's") : QStringLiteral("%1 %2").arg(value.toDouble()).arg(property.unit).trimmed());
                    shown->setEnabled(!own);
                    reset->setEnabled(!own);
                };
                connect(slider, &QSlider::valueChanged, this, [set, step](int at) { set(at * step); });
                inspector_sync.insert(key, sync);
                sync();
            } else if (property.kind == PopupDesign::Property::Colour) {
                auto* button = new QPushButton(inspector);
                button->setMinimumWidth(80);
                row->addWidget(button, 1);
                const auto sync = [this, button, reset, key] {
                    const QString value = design.values.value(key).toString();
                    ShowColour(button, value);
                    reset->setEnabled(!value.isEmpty());
                };
                const QString label = property.label;
                connect(button, &QPushButton::clicked, this, [this, button, set, label] {
                    const QString was = button->property("value").toString();
                    QColorDialog dialog(was.isEmpty() ? QColor(Qt::white) : QColor::fromString(was), this);
                    dialog.setWindowTitle(label);
                    dialog.setOption(QColorDialog::ShowAlphaChannel);
                    // The preview follows the colour as it is chosen.
                    connect(&dialog, &QColorDialog::currentColorChanged, this, [set](const QColor& colour) { set(colour.name(QColor::HexArgb)); });
                    if (dialog.exec() != QDialog::Accepted) {
                        set(was.isEmpty() ? QJsonValue() : QJsonValue(was));
                    } else {
                        set(dialog.selectedColor().name(QColor::HexArgb));
                    }
                });
                inspector_sync.insert(key, sync);
                sync();
            } else {
                auto* choice = new QComboBox(inspector);
                choice->addItem(look_own, QString());
                if (property.kind == PopupDesign::Property::Toggle) {
                    choice->addItem(QStringLiteral("On"), QStringLiteral("1"));
                    choice->addItem(QStringLiteral("Off"), QStringLiteral("0"));
                } else {
                    for (const auto& [value, label] : property.choices) {
                        choice->addItem(label, value);
                    }
                }
                Calm(choice);
                row->addWidget(choice, 1);
                const bool toggle = property.kind == PopupDesign::Property::Toggle;
                const auto sync = [this, choice, reset, key, toggle] {
                    const QJsonValue value = design.values.value(key);
                    const QSignalBlocker quiet(choice);
                    const QString data = value.isUndefined() ? QString() : toggle ? (value.toBool() ? QStringLiteral("1") : QStringLiteral("0")) : value.toString();
                    choice->setCurrentIndex(std::max(0, choice->findData(data)));
                    reset->setEnabled(!value.isUndefined());
                };
                connect(choice, &QComboBox::currentIndexChanged, this, [choice, set, toggle] {
                    const QString data = choice->currentData().toString();
                    set(data.isEmpty() ? QJsonValue() : toggle ? QJsonValue(data == QStringLiteral("1")) : QJsonValue(data));
                });
                inspector_sync.insert(key, sync);
                sync();
            }
            row->addWidget(reset);
            form->addRow(property.label, row);
        }
        if (!element.size.isEmpty()) {
            auto* note = new QLabel(QStringLiteral("Drag its corner in the preview to size it."), inspector);
            note->setEnabled(false);
            form->addRow(note);
        }
    }
}

void SettingsWindow::PreviewMessage(const QJsonObject& message) {
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("pick")) {
        const QString id = message.value(QStringLiteral("element")).toString();
        if (!id.isEmpty() && id != picked_element) {
            PickElement(id);
        }
    } else if (type == QStringLiteral("value")) {
        const QString key = message.value(QStringLiteral("key")).toString();
        const PopupDesign::Property* property = PopupDesign::PropertyOf(key);
        if (property == nullptr) {
            return;
        }
        PopupDesign::Design& edited = EditedDesign();
        edited.values.insert(key, PopupDesign::Clean(*property, message.value(QStringLiteral("value"))));
        DesignChanged(false);
        if (const auto sync = inspector_sync.value(key)) {
            sync();
        }
    } else if (type == QStringLiteral("move")) {
        // A block or a part dragged in the preview: before another, or last.
        const bool head = message.value(QStringLiteral("which")).toString() == QStringLiteral("head");
        const QString id = message.value(QStringLiteral("id")).toString(), before = message.value(QStringLiteral("before")).toString();
        PopupDesign::Design& edited = EditedDesign();
        QStringList& order = head ? edited.head_order : edited.entry_order;
        for (const auto& [one, label] : head ? PopupDesign::HeadParts() : PopupDesign::Blocks()) {
            if (!order.contains(one)) {
                order.append(one);
            }
        }
        if (!order.contains(id)) {
            return;
        }
        order.removeAll(id);
        const qsizetype at = before.isEmpty() ? -1 : order.indexOf(before);
        order.insert(at < 0 ? order.size() : at, id);
        DesignChanged(picked_element == QStringLiteral("order"));
    }
}

void SettingsWindow::ShowPreview() {
    if (!preview) {
        return;
    }
    const Layer layer = LayerOf(popup_layer);
    auto& settings = ReaderUi::ReaderSession::Instance().Settings();
    const auto value = [&](const char* key) {
        if (const auto layered = PopupStyle::Layered(play, popup_folder, QString::fromLatin1(key), layer.console, layer.game)) {
            return *layered;
        }
        return settings.Value(QString::fromLatin1(key)).toString();
    };
    QString theme = value("reader_popup_theme");
    if (theme.isEmpty() || theme == QStringLiteral("classic")) {
        theme = QStringLiteral("console"); // the classic popup is drawn without the page: the page in its colours
    }
    const QString mark = value("reader_popup_mark_unknown");
    // The page is see-through where the popup is not: here the window's colour under it, or the
    // engine's window keeps showing what was drawn there before.
    preview->Call(QStringLiteral("document.documentElement.style.background = %1")
                      .arg(JsString(palette().color(QPalette::Window).name())));
    preview->Call(QStringLiteral("Reader.setStyle(\"console\", %1)").arg(JsString(ReaderUi::LookupPopup::ConsoleStyle())));
    // The look, and the design being changed over it (what is moved shows before it is saved).
    preview->Call(QStringLiteral("Reader.setStyle(\"theme\", %1)").arg(JsString(ReaderUi::LookupPopup::ThemeStyle(theme))));
    preview->Call(QStringLiteral("Reader.setStyle(\"user\", %1)").arg(JsString(design.base == theme ? PopupDesign::Css(design) : QString())));
    preview->Call(QStringLiteral("Reader.design(%1)").arg(QString::fromUtf8(QJsonDocument(PopupDesign::PageConfig()).toJson(QJsonDocument::Compact))));
    preview->Call(QStringLiteral("Reader.pick(%1)").arg(JsString(picked_element)));
    QJsonArray entries = ReaderApp::PopupModel::Sample();
    if (value("reader_popup_lookups") == QStringLiteral("off")) {
        QJsonObject first = entries[0].toObject();
        first.remove(QStringLiteral("history"));
        entries[0] = first;
    } else if (value("reader_popup_lookups") != QStringLiteral("where")) {
        QJsonObject first = entries[0].toObject();
        first.insert(QStringLiteral("history"), QStringLiteral("Looked up 7 times"));
        entries[0] = first;
    }
    const QJsonObject model{{QStringLiteral("entries"), entries},
                            {QStringLiteral("corner"), settings.CardCorner()},
                            {QStringLiteral("duplicates"), false},
                            {QStringLiteral("markUnknown"), mark != QStringLiteral("false") && mark != QStringLiteral("0")},
                            {QStringLiteral("scale"), settings.PopupScale()},
                            {QStringLiteral("look"), PopupStyle::LookOf([&](const QString& key) { return value(key.toLatin1().constData()); })},
                            {QStringLiteral("console"), layer.console},
                            {QStringLiteral("game"), layer.game},
                            {QStringLiteral("labels"), QJsonObject{{QStringLiteral("forms"), QStringLiteral("forms")},
                                                                   {QStringLiteral("more"), QStringLiteral("more")}}}};
    preview->Call(QStringLiteral("Reader.show(%1)").arg(QString::fromUtf8(QJsonDocument(model).toJson(QJsonDocument::Compact))));
}

// ---- The consoles ----

QWidget* SettingsWindow::ConsolesTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    console_layer = new QComboBox(page);
    FillLayers(console_layer);
    auto* head = new QFormLayout;
    head->addRow(QStringLiteral("Settings for"), console_layer);
    layout->addLayout(head);
    // The folders a console's games are looked for in (program/settings.json's "folders"), for
    // one console: the CONSOLES page's Game folders row, as a list.
    folders_box = new QGroupBox(QStringLiteral("Game folders"), page);
    auto* folders_layout = new QHBoxLayout(folders_box);
    folders_list = new QListWidget(folders_box);
    folders_list->setMaximumHeight(110);
    folders_layout->addWidget(folders_list, 1);
    auto* folder_buttons = new QVBoxLayout;
    auto* add_folder = new QPushButton(QStringLiteral("Add…"), folders_box);
    auto* forget_folder = new QPushButton(QStringLiteral("Forget"), folders_box);
    folder_buttons->addWidget(add_folder);
    folder_buttons->addWidget(forget_folder);
    folder_buttons->addStretch(1);
    folders_layout->addLayout(folder_buttons);
    layout->addWidget(folders_box);
    const auto change_folders = [this](const std::function<void(QStringList&)>& change) {
        const QString console = LayerOf(console_layer).console;
        QStringList folders = play.Folders(console);
        change(folders);
        play.SetFolders(console, folders);
        play.Save();
        FillFolders();
        if (home) {
            home->SettingsChanged();
        }
        if (hooks.folders_changed) {
            hooks.folders_changed();
        }
    };
    connect(add_folder, &QPushButton::clicked, this, [this, change_folders] {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("A folder of %1 games").arg(console_layer->currentText().remove(QStringLiteral("Only "))));
        if (!folder.isEmpty()) {
            change_folders([&folder](QStringList& folders) {
                if (!folders.contains(folder)) {
                    folders.append(folder);
                }
            });
        }
    });
    connect(forget_folder, &QPushButton::clicked, this, [this, change_folders] {
        const int row = folders_list->currentRow();
        if (row >= 0) {
            change_folders([row](QStringList& folders) { folders.removeAt(row); });
        }
    });
    auto* body = new QWidget(page);
    console_form = new QFormLayout(body);
    layout->addWidget(body);
    layout->addStretch(1);
    connect(console_layer, &QComboBox::currentIndexChanged, this, [this] { FillConsoleRows(); });
    FillConsoleRows();
    return page;
}

void SettingsWindow::FillFolders() {
    const Layer layer = LayerOf(console_layer);
    // Folders are a console's, not every console's or one game's.
    folders_box->setVisible(!layer.console.isEmpty() && layer.game.isEmpty());
    folders_list->clear();
    if (!layer.console.isEmpty()) {
        for (const QString& folder : play.Folders(layer.console)) {
            folders_list->addItem(QDir::toNativeSeparators(folder));
        }
    }
}

void SettingsWindow::FillConsoleRows() {
    FillFolders();
    filling = true;
    while (console_form->rowCount() > 0) {
        console_form->removeRow(0);
    }
    const Layer layer = LayerOf(console_layer);
    QStringList keys;
    for (const PlayConsole& console : consoles) {
        if (!layer.console.isEmpty() && console.key != layer.console) {
            continue;
        }
        for (const PlaySetting& setting : console.settings) {
            // All consoles: the common settings once; one console: all of its own. The popup's
            // are on the Popup tab.
            if (setting.Hidden() || setting.key.startsWith(QStringLiteral("popup.")) || keys.contains(setting.key) ||
                (layer.console.isEmpty() && !setting.Common())) {
                continue;
            }
            keys.append(setting.key);
            AddSettingRow(console_form, setting, layer);
        }
    }
    if (keys.isEmpty()) {
        console_form->addRow(new QLabel(QStringLiteral("No console's core is here: their settings come with them."), this));
    }
    filling = false;
}

// ---- This window's own look ----

QWidget* SettingsWindow::LookTab() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    if (!home) {
        return page;
    }
    auto* theme = new QComboBox(page);
    theme->addItems(home->ThemeNames());
    theme->setCurrentIndex(home->ThemeIndex());
    connect(theme, &QComboBox::currentIndexChanged, this, [this](int index) { home->ChooseTheme(index); });
    form->addRow(QStringLiteral("Look"), theme);
    auto* icon = new QComboBox(page);
    icon->setIconSize(QSize(24, 24));
    for (const IconColours& colours : IconChoices()) {
        icon->addItem(IconPicture(colours.key), IconLabel(colours.key), colours.key);
    }
    icon->setCurrentIndex(std::max(0, icon->findData(ChosenIcon())));
    connect(icon, &QComboBox::currentIndexChanged, this, [icon](int) { ChooseIcon(icon->currentData().toString()); });
    form->addRow(QStringLiteral("Icon"), icon);
    auto* fuzz = new QComboBox(page);
    fuzz->addItem(QStringLiteral("As the look has it"), QString());
    fuzz->addItem(QStringLiteral("Off"), QStringLiteral("off"));
    fuzz->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    fuzz->addItem(QStringLiteral("Heavy"), QStringLiteral("heavy"));
    fuzz->setCurrentIndex(std::max(0, fuzz->findData(home->ChosenFuzz())));
    connect(fuzz, &QComboBox::currentIndexChanged, this, [this, fuzz](int) { home->ChooseFuzz(fuzz->currentData().toString()); });
    form->addRow(QStringLiteral("Fuzz"), fuzz);
    auto* motion = new QCheckBox(QStringLiteral("The orbs turn and spring, the glow breathes"), page);
    motion->setChecked(home->ChosenMotion());
    connect(motion, &QCheckBox::toggled, this, [this](bool on) { home->ChooseMotion(on); });
    form->addRow(QStringLiteral("Moving cursor"), motion);
    // Furoshiki 3's shelf asks the study tracker what was read of each title (docs/furoshiki-3.md).
    auto* tracker = new QLineEdit(home->TrackerUrl(), page);
    tracker->setPlaceholderText(QStringLiteral("http://…:8788 (empty: this device's own figures)"));
    connect(tracker, &QLineEdit::editingFinished, this, [this, tracker] { home->SetTrackerUrl(tracker->text().trimmed()); });
    form->addRow(QStringLiteral("Study tracker"), tracker);
    form->addRow(new QLabel(QStringLiteral("The reader's own settings (dictionaries, Anki, the text size of the popup…) are "
                                           "in the Reader menu over a game."),
                            page));
    return page;
}

} // namespace Home
