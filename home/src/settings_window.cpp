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
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#ifndef HOME_PUBLIC
#include "feedback_dialog.h"
#endif
#include "home_window.h"
#include "popup_controls.h"
#include "popup_style.h"
#include "reader/desktop/popup_model.h"
#include "reader/qt/lookup_popup.h"
#include "reader/qt/popup_web.h"
#include "reader/qt/reader_session.h"
#include "settings_window.h"

namespace Home {

namespace {

/// What the preview shows: a word not in Anki, with a frequency and the count of lookups, and one
/// Anki has.
QJsonArray SampleEntries() {
    ReaderApp::LookupResult unknown;
    unknown.expression = QStringLiteral("死ぬほど");
    unknown.reading = QStringLiteral("しぬほど");
    unknown.frequencies = {ReaderApp::Frequency{QStringLiteral("JPDB"), QStringLiteral("7621"), 7621.0, true}};
    ReaderApp::TermEntry sense;
    sense.dictionary = QStringLiteral("JMdict");
    sense.definition_tags = QStringLiteral("exp");
    sense.glossary_json = QString::fromUtf8(R"j(["to death (bored, scared, etc.)","to distraction","like crazy","so much"])j");
    ReaderApp::TermEntry forms;
    forms.dictionary = QStringLiteral("JMdict");
    forms.definition_tags = QStringLiteral("forms");
    forms.glossary_json = QString::fromUtf8(R"j(["死ぬほど | 死ぬ程"])j");
    unknown.entries = {sense, forms};
    QJsonObject first = ReaderApp::PopupModel::Entry(unknown, QStringLiteral("Looked up 7 times · first 3 Oct in STEINS;GATE"));
    first.insert(QStringLiteral("unknown"), true);

    ReaderApp::LookupResult known;
    known.expression = QStringLiteral("死ぬ");
    known.reading = QStringLiteral("しぬ");
    known.reasons = {QStringLiteral("masu")};
    known.pitches = {ReaderApp::PitchAccent{QStringLiteral("NHK"), QStringLiteral("しぬ"), {0}}};
    ReaderApp::TermEntry die;
    die.dictionary = QStringLiteral("JMdict");
    die.definition_tags = QStringLiteral("v5n vi");
    die.glossary_json = QString::fromUtf8(R"j(["to die","to pass away"])j");
    known.entries = {die};
    QJsonObject second = ReaderApp::PopupModel::Entry(known, {});
    second.insert(QStringLiteral("state"), QStringLiteral("inDeck"));
    return {first, second};
}

/// The CSS chooser's entry for a built-in look: this before its name.
const QString BuiltIn = QStringLiteral("builtin:");

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
    tabs->addTab(ControllerTab(), QStringLiteral("Controller"));
#ifndef READER_NO_SAVE_SYNC
    tabs->addTab(SyncTab(), QStringLiteral("Save sync"));
#endif
    tabs->addTab(AchievementsTab(), QStringLiteral("Achievements"));
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
        if (key.startsWith(QStringLiteral("popup."))) {
            ShowPreview();
            if (key == QStringLiteral("popup.css")) {
                const QString chosen = box->currentData().toString();
                const int at = css_file->findData(chosen);
                if (at >= 0) {
                    css_file->setCurrentIndex(at);
                }
            }
        }
    });
    Calm(box);
    form->addRow(label, box);
}

// ---- The popup ----

QWidget* SettingsWindow::PopupTab() {
    auto* page = new QWidget(this);
    auto* split = new QSplitter(Qt::Horizontal, page);
    auto* left = new QWidget(split);
    auto* left_layout = new QVBoxLayout(left);
    popup_layer = new QComboBox(left);
    FillLayers(popup_layer);
    connect(popup_layer, &QComboBox::currentIndexChanged, this, [this] {
        FillPopupRows();
        ShowPreview();
    });
    auto* layer_row = new QFormLayout;
    layer_row->addRow(QStringLiteral("Settings for"), popup_layer);
    left_layout->addLayout(layer_row);
    auto* settings_box = new QWidget(left);
    popup_form = new QFormLayout(settings_box);
    left_layout->addWidget(settings_box);

    // The user's CSS, a file at a time; the built-in looks too, to read, copy and change in a copy.
    auto* css_head = new QHBoxLayout;
    css_head->addWidget(new QLabel(QStringLiteral("CSS"), left));
    css_file = new QComboBox(left);
    css_head->addWidget(css_file, 1);
    auto* add = new QPushButton(QStringLiteral("New…"), left);
    auto* save = new QPushButton(QStringLiteral("Save"), left);
    css_save = save;
    auto* copy = new QPushButton(QStringLiteral("Copy"), left);
    copy->setToolTip(QStringLiteral("Copies the CSS shown to the clipboard"));
    auto* folder = new QPushButton(QStringLiteral("Folder"), left);
    css_head->addWidget(add);
    css_head->addWidget(save);
    css_head->addWidget(copy);
    css_head->addWidget(folder);
    left_layout->addLayout(css_head);
    css_text = new QPlainTextEdit(left);
    css_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    css_text->setPlaceholderText(QStringLiteral("/* .popup { border-radius: 12px; }\n   .popup[data-console=\"ps2\"] .tag { … } */"));
    // The same CSS twice: as controls, and as text.
    editor_tabs = new QTabWidget(left);
    editor_tabs->addTab(ControlsPanel(editor_tabs), QStringLiteral("Controls"));
    editor_tabs->addTab(css_text, QStringLiteral("CSS"));
    left_layout->addWidget(editor_tabs, 1);
    css_text->setToolTip(QStringLiteral("The preview follows what you type; Save keeps it in its file.\n"
                                        "A built-in look is saved as a copy, which can then draw the popup by itself.\n"
                                        "Classes: .popup, .entry, .entry.unknown, .headword, .reading, .tag,\n"
                                        ".tag.frequency, .glosses .gloss, .lookups, .forms, .card.add, .card.book"));
    auto* transfer = new QHBoxLayout;
    auto* export_style = new QPushButton(QStringLiteral("Export popup style…"), left);
    auto* import_style = new QPushButton(QStringLiteral("Import popup style…"), left);
    transfer->addWidget(export_style);
    transfer->addWidget(import_style);
    transfer->addStretch(1);
    left_layout->addLayout(transfer);

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
    split->setStretchFactor(1, 2);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(split);

    preview_timer = new QTimer(this);
    preview_timer->setSingleShot(true);
    preview_timer->setInterval(250);
    connect(preview_timer, &QTimer::timeout, this, &SettingsWindow::ShowPreview);
    connect(css_text, &QPlainTextEdit::textChanged, this, [this] {
        if (!filling) {
            ControlsFromCss();
            preview_timer->start();
        }
    });
    connect(css_file, &QComboBox::currentIndexChanged, this, [this] { LoadCssFile(); });
    connect(save, &QPushButton::clicked, this, [this] {
        if (const QString theme = BuiltInShown(); !theme.isEmpty()) {
            SaveCopy(theme);
            return;
        }
        const QString name = css_file->currentData().toString();
        if (name.isEmpty()) {
            return;
        }
        QSaveFile file(popup_folder + QLatin1Char('/') + name + QStringLiteral(".css"));
        if (!file.open(QIODevice::WriteOnly) || file.write(css_text->toPlainText().toUtf8()) < 0 || !file.commit()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("The file could not be written."));
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        QString name = QInputDialog::getText(this, QStringLiteral("A new CSS file"), QStringLiteral("Its name:"),
                                             QLineEdit::Normal, QStringLiteral("mine"), &ok)
                           .trimmed();
        name.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("-"));
        if (!ok || name.isEmpty() || name == QStringLiteral("none")) {
            return;
        }
        QFile file(popup_folder + QLatin1Char('/') + name + QStringLiteral(".css"));
        if (!file.exists() && file.open(QIODevice::WriteOnly)) {
            file.write("/* Your popup style */\n");
        }
        file.close();
        if (hooks.popup_files_changed) {
            hooks.popup_files_changed();
        }
        FillPopupRows();
        css_file->setCurrentIndex(std::max(0, css_file->findData(name)));
    });
    connect(copy, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(css_text->toPlainText()); });
    connect(folder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(popup_folder)); });
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
        });
    }
    FillPopupRows();
    return page;
}

void SettingsWindow::FillPopupRows() {
    filling = true;
    while (popup_form->rowCount() > 0) {
        popup_form->removeRow(0);
    }
    const Layer layer = LayerOf(popup_layer);
    const QList<PlaySetting> settings = PopupStyle::Settings(popup_folder);
    for (const PlaySetting& setting : settings) {
        AddSettingRow(popup_form, setting, layer);
    }
    const QString chosen = css_file->currentData().toString();
    css_file->clear();
    for (const QString& theme : settings.first().choices) {
        if (!ReaderUi::LookupPopup::ThemeStyle(theme).isEmpty()) {
            css_file->addItem(QStringLiteral("%1 (built in)").arg(theme), BuiltIn + theme);
        }
    }
    css_file->insertSeparator(css_file->count());
    for (const QString& name : settings.last().choices) {
        if (name != QStringLiteral("none")) {
            css_file->addItem(name + QStringLiteral(".css"), name);
        }
    }
    // The file the layer uses, else the one shown before, else the layer's built-in look.
    const auto from = [&](const PlaySetting& setting) {
        return play.Value(setting, layer.console.isEmpty() ? QString() : layer.console, layer.game).value;
    };
    const QString used = from(settings.last());
    int at = css_file->findData(used != QStringLiteral("none") ? used : chosen);
    if (at < 0) {
        at = css_file->findData(BuiltIn + from(settings.first()));
    }
    css_file->setCurrentIndex(std::max(0, at));
    filling = false;
    LoadCssFile();
}

void SettingsWindow::LoadCssFile() {
    if (filling) {
        return;
    }
    const QString theme = BuiltInShown();
    QFile file(popup_folder + QLatin1Char('/') + css_file->currentData().toString() + QStringLiteral(".css"));
    filling = true;
    QString text = !theme.isEmpty()                 ? ReaderUi::LookupPopup::ThemeStyle(theme)
                   : file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll())
                                                    : QString();
    // The controls' rules made again from what they kept: a file from an older Furoshiki gets
    // today's rules (in the editor; the file once saved).
    if (const PopupControls::Values kept = PopupControls::Read(text); !kept.isEmpty()) {
        text = PopupControls::With(text, kept);
    }
    css_text->setPlainText(text);
    css_save->setText(theme.isEmpty() ? QStringLiteral("Save") : QStringLiteral("Save as copy…"));
    filling = false;
    ControlsFromCss();
    ShowPreview();
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

QWidget* SettingsWindow::ControlsPanel(QWidget* parent) {
    namespace Controls = PopupControls;
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inside = new QWidget(scroll);
    auto* columns = new QHBoxLayout(inside);
    auto* first = new QVBoxLayout;
    auto* second = new QVBoxLayout;
    columns->addLayout(first, 1);
    columns->addLayout(second, 1);
    QMap<QString, QFormLayout*> groups;
    for (const QString& name : {QStringLiteral("Text"), QStringLiteral("Box"), QStringLiteral("Colours")}) {
        auto* box = new QGroupBox(name, inside);
        auto* form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        groups.insert(name, form);
        (name == QStringLiteral("Colours") ? second : first)->addWidget(box);
    }
    first->addStretch(1);
    second->addStretch(1);
    for (const Controls::Control& control : Controls::Controls()) {
        QFormLayout* form = groups.value(control.group);
        if (control.kind == Controls::Control::Size) {
            auto* box = new QSpinBox(inside);
            box->setRange(control.low - 1, control.high);
            box->setSpecialValueText(QString::fromUtf8(LooksOwn));
            box->setToolTip(QStringLiteral("— is the look's own"));
            box->setSuffix(QStringLiteral(" px"));
            box->setValue(box->minimum());
            box->setMinimumWidth(60);
            box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            connect(box, &QSpinBox::valueChanged, this, [this] { CssFromControls(); });
            form->addRow(control.label, box);
            controls.insert(control.key, box);
        } else if (control.kind == Controls::Control::Font) {
            auto* box = new QComboBox(inside);
            box->setMinimumWidth(60);
            box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            box->addItem(QString::fromUtf8(LooksOwn), QString());
            box->setToolTip(QStringLiteral("— is the look's own"));
            for (const auto& [key, name] : Controls::Fonts()) {
                box->addItem(name, key);
            }
            connect(box, &QComboBox::currentIndexChanged, this, [this] { CssFromControls(); });
            form->addRow(control.label, box);
            controls.insert(control.key, box);
        } else {
            auto* row = new QHBoxLayout;
            auto* button = new QPushButton(inside);
            button->setMinimumWidth(60);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            ShowColour(button, {});
            auto* clear = new QToolButton(inside);
            clear->setText(QStringLiteral("×"));
            clear->setToolTip(QStringLiteral("Back to the look's own"));
            row->addWidget(button, 1);
            row->addWidget(clear);
            const QString label = control.label;
            connect(button, &QPushButton::clicked, this, [this, button, label] {
                const QString was = button->property("value").toString();
                const QColor chosen = QColorDialog::getColor(was.isEmpty() ? QColor(Qt::white) : QColor::fromString(was), this,
                                                             label, QColorDialog::ShowAlphaChannel);
                if (chosen.isValid()) {
                    ShowColour(button, chosen.name(QColor::HexArgb));
                    CssFromControls();
                }
            });
            connect(clear, &QToolButton::clicked, this, [this, button] {
                ShowColour(button, {});
                CssFromControls();
            });
            form->addRow(control.label, row);
            controls.insert(control.key, button);
        }
    }
    scroll->setWidget(inside);
    return scroll;
}

void SettingsWindow::ControlsFromCss() {
    const PopupControls::Values values = PopupControls::Read(css_text->toPlainText());
    const bool was = filling;
    filling = true;
    for (const PopupControls::Control& control : PopupControls::Controls()) {
        const QString value = values.value(control.key);
        QWidget* widget = controls.value(control.key);
        if (auto* box = qobject_cast<QSpinBox*>(widget)) {
            box->setValue(value.isEmpty() ? box->minimum() : value.toInt());
        } else if (auto* box = qobject_cast<QComboBox*>(widget)) {
            box->setCurrentIndex(std::max(0, box->findData(value)));
        } else if (auto* button = qobject_cast<QPushButton*>(widget)) {
            ShowColour(button, value);
        }
    }
    filling = was;
}

void SettingsWindow::CssFromControls() {
    if (filling) {
        return;
    }
    PopupControls::Values values;
    for (const PopupControls::Control& control : PopupControls::Controls()) {
        QWidget* widget = controls.value(control.key);
        QString value;
        if (auto* box = qobject_cast<QSpinBox*>(widget)) {
            value = box->value() == box->minimum() ? QString() : QString::number(box->value());
        } else if (auto* box = qobject_cast<QComboBox*>(widget)) {
            value = box->currentData().toString();
        } else if (auto* button = qobject_cast<QPushButton*>(widget)) {
            value = button->property("value").toString();
        }
        if (!value.isEmpty()) {
            values.insert(control.key, value);
        }
    }
    filling = true;
    css_text->setPlainText(PopupControls::With(css_text->toPlainText(), values));
    filling = false;
    preview_timer->start();
}

QString SettingsWindow::BuiltInShown() const {
    const QString data = css_file->currentData().toString();
    return data.startsWith(BuiltIn) ? data.mid(BuiltIn.size()) : QString();
}

void SettingsWindow::SaveCopy(const QString& theme) {
    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("A copy of \"%1\"").arg(theme),
                                         QStringLiteral("The copy's name:"), QLineEdit::Normal,
                                         theme + QStringLiteral("-mine"), &ok)
                       .trimmed();
    name.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("-"));
    if (!ok || name.isEmpty() || name == QStringLiteral("none")) {
        return;
    }
    const QString path = popup_folder + QLatin1Char('/') + name + QStringLiteral(".css");
    if (QFileInfo::exists(path) &&
        QMessageBox::question(this, windowTitle(), QStringLiteral("%1.css is there already. Replace it?").arg(name)) !=
            QMessageBox::Yes) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(css_text->toPlainText().toUtf8()) < 0 || !file.commit()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("The file could not be written."));
        return;
    }
    // The copy draws the popup by itself over the console's colours, as the built-in look did.
    const Layer layer = LayerOf(popup_layer);
    if (QMessageBox::question(this, windowTitle(),
                              QStringLiteral("Use %1 for the popup now (%2)?").arg(name, popup_layer->currentText())) ==
        QMessageBox::Yes) {
        Set(QStringLiteral("popup.theme"), QStringLiteral("console"), layer);
        Set(QStringLiteral("popup.css"), name, layer);
    }
    if (hooks.popup_files_changed) {
        hooks.popup_files_changed();
    }
    FillPopupRows();
    css_file->setCurrentIndex(std::max(0, css_file->findData(name)));
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
    // What is typed counts in the preview before it is saved: a built-in look shown in the editor
    // stands for the theme, a file for the user's own.
    const bool built_in = !BuiltInShown().isEmpty();
    preview->Call(QStringLiteral("Reader.setStyle(\"theme\", %1)")
                      .arg(JsString(built_in ? css_text->toPlainText() : ReaderUi::LookupPopup::ThemeStyle(theme))));
    preview->Call(
        QStringLiteral("Reader.setStyle(\"user\", %1)").arg(JsString(built_in ? QString() : css_text->toPlainText())));
    QJsonArray entries = SampleEntries();
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
                            {QStringLiteral("console"), layer.console},
                            {QStringLiteral("game"), layer.game},
                            {QStringLiteral("labels"), QJsonObject{{QStringLiteral("forms"), QStringLiteral("forms")}}}};
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
    form->addRow(new QLabel(QStringLiteral("The reader's own settings (dictionaries, Anki, the text size of the popup…) are "
                                           "in the Reader menu over a game."),
                            page));
    return page;
}

} // namespace Home
