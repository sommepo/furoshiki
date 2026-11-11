// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The settings window's pages that replace hand-edits of program/settings.json and the like:
// save sync (with a pairing link and a test of each address), the RetroAchievements login, and
// the text server. Plain widgets in the window's own style: no colours or fonts of their own.

#include <thread>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include "home_window.h"
#include "library.h"
#ifndef READER_NO_SAVE_SYNC
#include "pairing.h"
#include "services.h"
#endif
#include "reader/qt/install.h"
#include "reader/qt/reader_session.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#include "reader/save_sync/remote.h"
#endif
#include "settings_window.h"
#include "../../cores/reader_core.h"

namespace Home {

namespace {

QLabel* Note(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

#ifndef READER_NO_SAVE_SYNC
/// Where save sync's settings are written while no console's core has started it: the first
/// console's folder of the program's, which every console without settings of its own takes
/// (main_window.cpp, StartSaveSync).
QString IdleSyncFile() {
    return SharedFolder() + QStringLiteral("/program/save-sync/ps1/settings.json");
}

SaveSync::Host::Settings IdleSyncSettings() {
    SaveSync::Host::Settings settings;
    QFile file(IdleSyncFile());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        settings.enabled = json.value(QStringLiteral("enabled")).toBool();
        settings.addresses = json.value(QStringLiteral("addresses")).toString().toStdString();
        settings.code = json.value(QStringLiteral("code")).toString().toStdString();
        settings.device = json.value(QStringLiteral("device")).toString().toStdString();
    }
    return settings;
}

void SetIdleSyncSettings(const SaveSync::Host::Settings& settings) {
    // The file's other keys (the games' names) are kept.
    QJsonObject json;
    QFile in(IdleSyncFile());
    if (in.open(QIODevice::ReadOnly)) {
        json = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
    }
    json.insert(QStringLiteral("enabled"), settings.enabled);
    json.insert(QStringLiteral("addresses"), QString::fromStdString(settings.addresses));
    json.insert(QStringLiteral("code"), QString::fromStdString(settings.code));
    json.insert(QStringLiteral("device"), QString::fromStdString(settings.device));
    QDir().mkpath(QFileInfo(IdleSyncFile()).path());
    QSaveFile out(IdleSyncFile());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
        out.commit();
    }
}

SaveSync::Host::Settings CurrentSync() {
    return SaveSync::Host::Started() ? SaveSync::Host::Get() : IdleSyncSettings();
}
#endif

} // namespace

#ifndef READER_NO_SAVE_SYNC
void PairedService(QStringList* addresses, QString* code) {
    const SaveSync::Host::Settings settings = CurrentSync();
    addresses->clear();
    for (const SaveSync::Address& address : SaveSync::ParseAddresses(settings.addresses)) {
        addresses->append(QString::fromStdString(address.Text()));
    }
    *code = QString::fromStdString(settings.code);
}
#endif

// ---- Save sync ----

#ifndef READER_NO_SAVE_SYNC
QWidget* SettingsWindow::SyncTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(Note(QStringLiteral("Every game's save follows you to your other devices through your save sync service "
                                          "(the one your other devices use). The quickest way in: the pairing link its page "
                                          "shows, pasted here."),
                           page));

    auto* pair_box = new QGroupBox(QStringLiteral("Pair with a link"), page);
    auto* pair_row = new QHBoxLayout(pair_box);
    pair_link = new QLineEdit(pair_box);
    pair_link->setPlaceholderText(QStringLiteral("readersync://pair?v=1&sync=…&code=…"));
    auto* paste = new QPushButton(QStringLiteral("Paste"), pair_box);
    auto* use = new QPushButton(QStringLiteral("Use this link"), pair_box);
    pair_row->addWidget(pair_link, 1);
    pair_row->addWidget(paste);
    pair_row->addWidget(use);
    layout->addWidget(pair_box);
    connect(paste, &QPushButton::clicked, this, [this] { pair_link->setText(QGuiApplication::clipboard()->text().trimmed()); });
    const auto use_link = [this] {
        QString error;
        if (!UsePairing(pair_link->text(), &error)) {
            sync_test->setText(QStringLiteral("Not used: %1.").arg(error));
        }
    };
    connect(use, &QPushButton::clicked, this, use_link);
    connect(pair_link, &QLineEdit::returnPressed, this, use_link);

    sync_on = new QCheckBox(QStringLiteral("Sync saves with my other devices"), page);
    layout->addWidget(sync_on);
    auto* form = new QFormLayout;
    auto* addresses_row = new QHBoxLayout;
    sync_addresses = new QListWidget(page);
    sync_addresses->setMaximumHeight(120);
    sync_addresses->setDragDropMode(QAbstractItemView::InternalMove);
    sync_addresses->setToolTip(QStringLiteral("Tried in this order: the home network's address first, then the Tailscale "
                                              "network's. Double-click to change one; drag to reorder."));
    addresses_row->addWidget(sync_addresses, 1);
    auto* address_buttons = new QVBoxLayout;
    auto* add = new QPushButton(QStringLiteral("Add"), page);
    auto* remove = new QPushButton(QStringLiteral("Remove"), page);
    address_buttons->addWidget(add);
    address_buttons->addWidget(remove);
    address_buttons->addStretch(1);
    addresses_row->addLayout(address_buttons);
    form->addRow(QStringLiteral("Addresses"), addresses_row);
    sync_code = new QLineEdit(page);
    sync_code->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    sync_code->setPlaceholderText(QStringLiteral("the code the service shows"));
    form->addRow(QStringLiteral("Code"), sync_code);
    sync_device = new QLineEdit(page);
    form->addRow(QStringLiteral("This device's name"), sync_device);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    auto* test = new QPushButton(QStringLiteral("Test connection"), page);
    auto* all = new QPushButton(QStringLiteral("Sync every game now"), page);
    auto* conflicts = new QPushButton(QStringLiteral("Conflicts and versions…"), page);
    buttons->addWidget(test);
    buttons->addWidget(all);
    buttons->addWidget(conflicts);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    sync_test = Note({}, page);
    layout->addWidget(sync_test);
    sync_status = Note({}, page);
    layout->addWidget(sync_status);
    layout->addStretch(1);

    connect(add, &QPushButton::clicked, this, [this] {
        auto* item = new QListWidgetItem(QStringLiteral("host:8642"), sync_addresses);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        sync_addresses->editItem(item);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        delete sync_addresses->currentItem();
        StoreSync();
    });
    connect(sync_addresses, &QListWidget::itemChanged, this, [this] { StoreSync(); });
    connect(sync_addresses->model(), &QAbstractItemModel::rowsMoved, this, [this] { StoreSync(); });
    connect(sync_on, &QCheckBox::toggled, this, [this] { StoreSync(); });
    for (QLineEdit* field : {sync_code, sync_device}) {
        connect(field, &QLineEdit::editingFinished, this, [this] { StoreSync(); });
    }
    connect(test, &QPushButton::clicked, this, [this] { TestSync(); });
    connect(all, &QPushButton::clicked, this, [this] {
        StoreSync();
        if (SaveSync::Host::Started()) {
            SaveSync::Host::SyncAll();
        } else {
            sync_test->setText(QStringLiteral("Save sync runs with the consoles' cores, and none has started here."));
        }
    });
    connect(conflicts, &QPushButton::clicked, this, [this] {
        StoreSync();
        if (hooks.open_sync_window) {
            hooks.open_sync_window();
        }
    });
    sync_timer = new QTimer(this);
    sync_timer->setInterval(500);
    connect(sync_timer, &QTimer::timeout, this, [this] { ShowSyncStatus(); });
    sync_timer->start();
    LoadSync();
    return page;
}

void SettingsWindow::LoadSync() {
    if (sync_on == nullptr) {
        return;
    }
    const SaveSync::Host::Settings now = CurrentSync();
    filling = true;
    sync_on->setChecked(now.enabled);
    sync_addresses->clear();
    for (const QString& address :
         QString::fromStdString(now.addresses).split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) {
        auto* item = new QListWidgetItem(address, sync_addresses);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    sync_code->setText(QString::fromStdString(now.code));
    sync_device->setText(QString::fromStdString(now.device));
    filling = false;
    sync_seen = ~0ull;
    ShowSyncStatus();
}

void SettingsWindow::StoreSync() {
    if (filling) {
        return;
    }
    QStringList addresses;
    for (int i = 0; i < sync_addresses->count(); ++i) {
        const QString address = sync_addresses->item(i)->text().trimmed();
        if (!address.isEmpty() && !addresses.contains(address)) {
            addresses.append(address);
        }
    }
    SaveSync::Host::Settings settings;
    settings.enabled = sync_on->isChecked();
    settings.addresses = addresses.join(QStringLiteral(", ")).toStdString();
    settings.code = sync_code->text().trimmed().toStdString();
    settings.device = sync_device->text().trimmed().toStdString();
    const SaveSync::Host::Settings before = CurrentSync();
    if (before.enabled == settings.enabled && before.addresses == settings.addresses && before.code == settings.code &&
        before.device == settings.device) {
        return;
    }
    if (SaveSync::Host::Started()) {
        SaveSync::Host::Set(settings);
    } else {
        SetIdleSyncSettings(settings);
    }
    if (home) {
        home->SettingsChanged();
    }
}

bool SettingsWindow::UsePairing(const QString& link, QString* error) {
    const std::optional<Pairing> pairing = ParsePairing(link, error);
    if (!pairing) {
        return false;
    }
    filling = true;
    sync_addresses->clear();
    for (const QString& address : pairing->sync) {
        auto* item = new QListWidgetItem(address, sync_addresses);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    sync_code->setText(pairing->code);
    if (sync_device->text().trimmed().isEmpty()) {
        sync_device->setText(QSysInfo::machineHostName());
    }
    sync_on->setChecked(true);
    filling = false;
    StoreSync();
    // The user's other services the link names (the tracker, Furochibi, the guides...).
    Home::Services::Keep(pairing->services);
    pair_link->clear(); // the code is in it
    ShowTab(QStringLiteral("Save sync"));
    sync_test->setText(QStringLiteral("Paired with %1: %2 addresses. Testing them…")
                           .arg(pairing->name.isEmpty() ? QStringLiteral("the service") : pairing->name)
                           .arg(pairing->sync.size()));
    TestSync();
    return true;
}

void SettingsWindow::TestSync() {
    StoreSync();
    const SaveSync::Host::Settings settings = CurrentSync();
    const std::vector<SaveSync::Address> addresses = SaveSync::ParseAddresses(settings.addresses);
    if (addresses.empty() || settings.code.empty()) {
        sync_test->setText(QStringLiteral("An address and the code are needed first."));
        return;
    }
    sync_test->setText(QStringLiteral("Asking %1 address(es)…").arg(addresses.size()));
    // Each address on its own, in the background: which of them answers here and now.
    QPointer<QLabel> out = sync_test;
    std::thread([out, addresses, settings] {
        QStringList lines;
        int good = 0;
        for (const SaveSync::Address& address : addresses) {
            SaveSync::Remote remote({address}, settings.code, settings.device);
            const qint64 started = QDateTime::currentMSecsSinceEpoch();
            const SaveSync::RemoteError failed = remote.Ping();
            const qint64 took = QDateTime::currentMSecsSinceEpoch() - started;
            const QString where = QString::fromStdString(address.Text());
            if (failed) {
                lines.append(QStringLiteral("✗ %1: %2").arg(where, QString::fromStdString(failed.message)));
            } else {
                ++good;
                lines.append(QStringLiteral("✓ %1 answered (%2 ms)").arg(where).arg(took));
            }
        }
        lines.prepend(good > 0 ? QStringLiteral("Tested %1: the service answered at %2 of %3 addresses.")
                                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                                     .arg(good)
                                     .arg(addresses.size())
                               : QStringLiteral("No address answered."));
        QMetaObject::invokeMethod(qApp, [out, text = lines.join(QLatin1Char('\n'))] {
            if (out) {
                out->setText(text);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void SettingsWindow::ShowSyncStatus() {
    if (sync_status == nullptr || !isVisible()) {
        return;
    }
    // When a save was last synced here: the newest of save sync's notes (state.json is written
    // each time a save goes up or comes down).
    QDateTime last;
    const QStringList folders = hooks.sync_folders ? hooks.sync_folders() : QStringList{};
    for (const QString& folder : folders) {
        const QFileInfo notes(folder + QStringLiteral("/state.json"));
        if (notes.exists() && (!last.isValid() || notes.lastModified() > last)) {
            last = notes.lastModified();
        }
    }
    const quint64 generation = SaveSync::Host::Started() ? SaveSync::Host::Generation() : 0;
    const QString when = last.isValid() ? QStringLiteral("Last save synced here: %1.").arg(last.toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                                        : QStringLiteral("No save synced here yet.");
    if (generation == sync_seen && sync_status->text().startsWith(when)) {
        return;
    }
    sync_seen = generation;
    QString status = SaveSync::Host::Started() ? QString::fromStdString(SaveSync::Host::Status()).trimmed()
                                               : QStringLiteral("Save sync starts with the consoles' cores; none has started here.");
    const int conflicts = SaveSync::Host::Started() ? static_cast<int>(SaveSync::Host::Conflicts().size()) : 0;
    if (conflicts > 0) {
        status += QStringLiteral("\n%1 game(s) have two different saves: Conflicts and versions… answers them.").arg(conflicts);
    }
    sync_status->setText(when + (status.isEmpty() ? QString() : QStringLiteral("\n") + status));
}
#endif

// ---- RetroAchievements ----

QWidget* SettingsWindow::AchievementsTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(Note(QStringLiteral("Your RetroAchievements login, handed to each core that has achievements (PCSX2's, "
                                          "Flycast's) when a game starts. The token is the site's \"Web API key\" or the one "
                                          "a login gives; your password is never kept."),
                           page));
    auto* form = new QFormLayout;
    ra_user = new QLineEdit(page);
    form->addRow(QStringLiteral("User name"), ra_user);
    auto* token_row = new QHBoxLayout;
    ra_token = new QLineEdit(page);
    ra_token->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    token_row->addWidget(ra_token, 1);
    auto* login = new QPushButton(QStringLiteral("Log in with password…"), page);
    token_row->addWidget(login);
    form->addRow(QStringLiteral("Token"), token_row);
    ra_hardcore = new QCheckBox(QStringLiteral("Hardcore: no savestates, no cheats, no slowing down"), page);
    form->addRow(QString(), ra_hardcore);
    layout->addLayout(form);
    auto* buttons = new QHBoxLayout;
    auto* forget = new QPushButton(QStringLiteral("Log out"), page);
    buttons->addWidget(forget);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    ra_note = Note({}, page);
    layout->addWidget(ra_note);
    layout->addStretch(1);

    filling = true;
    ra_user->setText(play.Raw(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_USER)));
    ra_token->setText(play.Raw(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_TOKEN)));
    const QString hardcore = play.Raw(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_HARDCORE));
    ra_hardcore->setChecked(hardcore.isEmpty() ? qEnvironmentVariable("READER_RA_HARDCORE") == QStringLiteral("1")
                                               : hardcore == QStringLiteral("1"));
    filling = false;
    if (ra_user->text().isEmpty() && !qEnvironmentVariable("READER_RA_USER").isEmpty()) {
        ra_note->setText(QStringLiteral("Not set here: the login %1 comes from the front end that started Furoshiki "
                                        "(Batocera's own). One set here comes first.")
                             .arg(qEnvironmentVariable("READER_RA_USER")));
    }
    for (QLineEdit* field : {ra_user, ra_token}) {
        connect(field, &QLineEdit::editingFinished, this, [this] { StoreAchievements(); });
    }
    connect(ra_hardcore, &QCheckBox::toggled, this, [this] { StoreAchievements(); });
    connect(login, &QPushButton::clicked, this, [this] { LogInAchievements(); });
    connect(forget, &QPushButton::clicked, this, [this] {
        filling = true;
        ra_user->clear();
        ra_token->clear();
        filling = false;
        StoreAchievements();
        ra_note->setText(QStringLiteral("Logged out here."));
    });
    return page;
}

void SettingsWindow::StoreAchievements() {
    if (filling) {
        return;
    }
    // All consoles' layer of program/settings.json, where main_window.cpp reads them for a core.
    play.Set(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_USER), ra_user->text().trimmed());
    play.Set(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_TOKEN), ra_token->text().trimmed());
    play.Set(QStringLiteral(READER_CORE_SETTING_ACHIEVEMENTS_HARDCORE), ra_hardcore->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    play.Save();
    if (home) {
        home->SettingsChanged();
    }
    ra_note->setText(ra_user->text().trimmed().isEmpty()
                         ? QStringLiteral("No login: the cores play without achievements.")
                         : QStringLiteral("Kept. It counts from the next game started."));
}

void SettingsWindow::LogInAchievements() {
    const QString user = ra_user->text().trimmed();
    if (user.isEmpty()) {
        ra_note->setText(QStringLiteral("Your user name first."));
        return;
    }
    bool ok = false;
    const QString password = QInputDialog::getText(this, QStringLiteral("RetroAchievements"),
                                                   QStringLiteral("The password of %1 (sent to retroachievements.org once, "
                                                                  "for a token; not kept):")
                                                       .arg(user),
                                                   QLineEdit::Password, {}, &ok);
    if (!ok || password.isEmpty()) {
        return;
    }
    auto* network = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("https://retroachievements.org/dorequest.php")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Furoshiki/1"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("r"), QStringLiteral("login2"));
    form.addQueryItem(QStringLiteral("u"), user);
    form.addQueryItem(QStringLiteral("p"), password);
    ra_note->setText(QStringLiteral("Logging in…"));
    QNetworkReply* reply = network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, network] {
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = answer.value(QStringLiteral("Token")).toString();
        if (answer.value(QStringLiteral("Success")).toBool() && !token.isEmpty()) {
            filling = true;
            ra_token->setText(token);
            filling = false;
            StoreAchievements();
            ra_note->setText(QStringLiteral("Logged in: the token is kept, the password is not."));
        } else {
            const QString why = answer.value(QStringLiteral("Error")).toString();
            ra_note->setText(QStringLiteral("Not logged in: %1").arg(!why.isEmpty() ? why : reply->errorString()));
        }
        reply->deleteLater();
        network->deleteLater();
    });
}

// ---- The controller ----

QWidget* SettingsWindow::ControllerTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(Note(QStringLiteral("The lookup pointer always follows the mouse and the Steam Controller's right pad. "
                                          "It can also be moved with the sticks while a button is held; that button then "
                                          "stops reaching the game. With None, every button is the game's."),
                           page));
    auto* form = new QFormLayout;
    pointer_button = new QComboBox(page);
    for (const int button : ReaderUi::PointerButtons()) {
        pointer_button->addItem(ReaderUi::PointerButtonName(button), button);
    }
    pointer_button->setCurrentIndex(std::max(0, pointer_button->findData(ReaderUi::ReaderSession::Instance().Settings().CursorButton())));
    connect(pointer_button, &QComboBox::activated, this, [this](int) {
        ReaderUi::SetPointerButton(pointer_button->currentData().toInt());
        if (home) {
            home->SettingsChanged();
        }
    });
    form->addRow(QStringLiteral("Button that moves the pointer with the sticks"), pointer_button);
    layout->addLayout(form);
    layout->addStretch(1);
    return page;
}

// ---- Reading time ----

QWidget* SettingsWindow::ReadingTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(Note(QStringLiteral("Reading time is counted while a game with a script is in front and a new line or a "
                                          "word looked up came not long ago. Past this, the clock stops until the next line: "
                                          "playing on without new text is not reading. Shared with the reader apps (reader.ini)."),
                           page));
    auto& settings = ReaderUi::ReaderSession::Instance().Settings();
    auto* form = new QFormLayout;
    idle_seconds = new QSpinBox(page);
    idle_seconds->setRange(ReaderApp::ReaderSettings::IdleSecondsMin,
                           static_cast<int>(ReaderApp::ReadingRules::MaxIdleTimeoutMs / 1000));
    idle_seconds->setSuffix(QStringLiteral(" s"));
    idle_seconds->setSingleStep(5);
    idle_seconds->setValue(static_cast<int>(settings.IdleTimeoutMs() / 1000));
    form->addRow(QStringLiteral("Reading time stops after, without a new line"), idle_seconds);
    layout->addLayout(form);
    connect(idle_seconds, &QSpinBox::valueChanged, this, [this](int value) {
        auto& session = ReaderUi::ReaderSession::Instance();
        session.Settings().SetValue(ReaderApp::ReaderSettings::KeyIdleSeconds, value);
        session.Settings().Sync();
        session.OnSettingsChanged();
    });

    layout->addSpacing(12);
    layout->addWidget(Note(QStringLiteral("Tidy reading time works every session's time out again from when its lines came: "
                                          "your own pace in that session (or game) gives how long each line can take at most, "
                                          "a word looked up shows you were there, and the rest, play without new text, is "
                                          "taken out. It never adds time. It runs by itself on the session just read when a "
                                          "game stops; this does every earlier one, with the time above as the longest a "
                                          "line is read. The time as counted is kept, so it can be put back."),
                           page));
    auto* buttons = new QHBoxLayout;
    tidy_button = new QPushButton(QStringLiteral("Tidy reading time"), page);
    untidy_button = new QPushButton(QStringLiteral("Put back the time as counted"), page);
    buttons->addWidget(tidy_button);
    buttons->addWidget(untidy_button);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    tidy_note = Note({}, page);
    layout->addWidget(tidy_note);
    layout->addStretch(1);
    connect(tidy_button, &QPushButton::clicked, this, [this] { TidyReading(false); });
    connect(untidy_button, &QPushButton::clicked, this, [this] { TidyReading(true); });
    return page;
}

void SettingsWindow::TidyReading(bool undo) {
    tidy_button->setEnabled(false);
    untidy_button->setEnabled(false);
    tidy_note->setText(QStringLiteral("Working…"));
    QPointer<SettingsWindow> self(this);
    ReaderUi::ReaderSession::Instance().TidyReadingTime([self, undo](const ReaderApp::TidyReport& report) {
        if (!self) {
            return;
        }
        self->tidy_button->setEnabled(true);
        self->untidy_button->setEnabled(true);
        if (undo) {
            self->tidy_note->setText(QStringLiteral("The time as counted is back in %1 sessions.").arg(report.sessions));
        } else {
            self->tidy_note->setText(Home::TidyReportText(report));
        }
        if (self->home) {
            self->home->SettingsChanged();
        }
        if (self->hooks.stats_changed) {
            self->hooks.stats_changed();
        }
    }, undo);
}

QString TidyReportText(const ReaderApp::TidyReport& report) {
    const QLocale locale;
    const auto minutes = [&](qint64 ms) { return locale.toString(static_cast<double>(ms) / 60000.0, 'f', 0) + QStringLiteral(" min"); };
    const auto speed = [&](qint64 ms) {
        const std::optional<double> per_hour = ReaderApp::ReadingRules::CharactersPerHour(report.characters, ms);
        return per_hour ? locale.toString(static_cast<qint64>(*per_hour)) + QStringLiteral("/h") : QStringLiteral("–");
    };
    if (report.sessions == 0) {
        return QStringLiteral("No session to tidy.");
    }
    return QStringLiteral("%1 sessions looked at, %2 shortened: %3 → %4 (%5 → %6 characters an hour).")
        .arg(report.sessions)
        .arg(report.changed)
        .arg(minutes(report.before_ms), minutes(report.after_ms), speed(report.before_ms), speed(report.after_ms));
}

#ifndef READER_NO_TEXT_SERVER
// ---- The text server ----

QWidget* SettingsWindow::TextServerTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(Note(QStringLiteral("Every line read, sent as it comes to a WebSocket, as texthooker pages "
                                          "and sentence-mining tools expect (ws://127.0.0.1:6677 by default). It runs while a "
                                          "game with a script plays; nothing a page sends is acted on."),
                           page));
    auto& settings = ReaderUi::ReaderSession::Instance().Settings();
    auto* form = new QFormLayout;
    hook_on = new QCheckBox(QStringLiteral("Send the text to a WebSocket"), page);
    hook_on->setChecked(settings.TextHookEnabled());
    form->addRow(QString(), hook_on);
    hook_port = new QSpinBox(page);
    hook_port->setRange(1024, 65535);
    hook_port->setValue(settings.TextHookPort());
    form->addRow(QStringLiteral("Port"), hook_port);
    hook_lan = new QCheckBox(QStringLiteral("Other devices on the network too (no password: anyone there can read it)"), page);
    hook_lan->setChecked(settings.TextHookLan());
    form->addRow(QString(), hook_lan);
    layout->addLayout(form);
    hook_note = Note({}, page);
    layout->addWidget(hook_note);
    layout->addStretch(1);
    connect(hook_on, &QCheckBox::toggled, this, [this] { StoreTextServer(); });
    connect(hook_lan, &QCheckBox::toggled, this, [this] { StoreTextServer(); });
    connect(hook_port, &QSpinBox::editingFinished, this, [this] { StoreTextServer(); });
    StoreTextServer();
    return page;
}

void SettingsWindow::StoreTextServer() {
    auto& session = ReaderUi::ReaderSession::Instance();
    auto& settings = session.Settings();
    const bool changed = settings.TextHookEnabled() != hook_on->isChecked() || settings.TextHookPort() != hook_port->value() ||
                         settings.TextHookLan() != hook_lan->isChecked();
    if (changed) {
        // The keys of ReaderSettings (shared/reader/desktop/settings.cpp).
        settings.SetValue(QStringLiteral("reader_texthook_enabled"), hook_on->isChecked());
        settings.SetValue(QStringLiteral("reader_texthook_port"), hook_port->value());
        settings.SetValue(QStringLiteral("reader_texthook_lan"), hook_lan->isChecked());
        settings.Sync();
        session.OnSettingsChanged();
        if (home) {
            home->SettingsChanged();
        }
    }
    const QString address = session.TextHookAddress();
    hook_note->setText(!hook_on->isChecked()  ? QStringLiteral("Off.")
                       : !address.isEmpty() ? QStringLiteral("Listening: %1").arg(address)
                                             : QStringLiteral("On: ws://%1:%2 while a game with a script plays.")
                                                   .arg(hook_lan->isChecked() ? QStringLiteral("<this computer>") : QStringLiteral("127.0.0.1"))
                                                   .arg(hook_port->value()));
}
#endif

} // namespace Home
