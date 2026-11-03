// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Furoshiki's settings in a window of its own, for the mouse (the maintainer: "there should be a
// windows style mouse oriented settings window for furoshiki not just the controller friendly
// one"): the popup's look with a live preview and the user's own CSS, the consoles' settings in
// their layers (and the folders of each console's games), save sync (with a pairing link), the
// RetroAchievements login, the text server, and this window's own look. Everything it changes is what the pages change
// (program/settings.json, QSettings), so the two always agree.

#include <functional>
#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include "play_settings.h"

class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QPushButton;
class QFormLayout;
class QPlainTextEdit;
class QTabWidget;
class QTimer;

namespace ReaderUi {
class PopupWeb;
}

namespace Home {

class HomeWindow;

class SettingsWindow : public QDialog {
    Q_OBJECT

public:
    struct Hooks {
        /// The game playing now, if any: its console's key, its ID and title.
        std::function<void(QString& console, QString& game, QString& title)> playing;
        /// After a console setting changed: the running core is handed the values again.
        std::function<void()> applied;
        /// The popup's settings and CSS files changed (an imported style, a new file).
        std::function<void()> popup_files_changed;
#ifndef READER_NO_SAVE_SYNC
        /// Save sync's folders of its notes (each console's), the newest state.json among them
        /// is when a save was last synced here.
        std::function<QStringList()> sync_folders;
        /// The shared Save sync window, which answers a conflict.
        std::function<void()> open_sync_window;
#endif
        /// A console's game folders changed: the games are looked for again.
        std::function<void()> folders_changed;
    };

    SettingsWindow(QWidget* parent, PlaySettings& play, const QList<PlayConsole>& consoles, QString popup_folder,
                   HomeWindow* home, Hooks hooks);

    /// Shows it again with what is set now (the pages may have changed something).
    void Refresh();
    /// Brings a tab to the front by its name ("Save sync", "Achievements", "Text server", ...).
    void ShowTab(const QString& name);
#ifndef READER_NO_SAVE_SYNC
    /// A pairing link given to the program (pasted, or as its argument): read into the Save
    /// sync tab and kept. False, with why in `error`, when it is not one.
    bool UsePairing(const QString& link, QString* error = nullptr);
#endif

protected:
    /// The wheel over a list or a number not chosen first passes it by: it changed settings on
    /// its way (the maintainer found the consoles' speed limit off).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// The layer chosen in a tab: all consoles (both empty), a console, or the game playing.
    struct Layer {
        QString console;
        QString game;
    };
    [[nodiscard]] Layer LayerOf(const QComboBox* chooser) const;
    void FillLayers(QComboBox* chooser);
    /// One row of `form` for `setting` on that layer: a choice whose first entry is the next
    /// layer's value ("same as ..."), or a number whose lowest is that.
    void AddSettingRow(QFormLayout* form, const PlaySetting& setting, const Layer& layer);
    [[nodiscard]] QString Next(const PlaySetting& setting, const Layer& layer) const;
    void Set(const QString& key, const QString& value, const Layer& layer);

    QWidget* PopupTab();
    QWidget* ConsolesTab();
    QWidget* LookTab();
#ifndef READER_NO_SAVE_SYNC
    QWidget* SyncTab();
#endif
    QWidget* AchievementsTab();
    QWidget* ControllerTab();
#ifndef READER_NO_TEXT_SERVER
    QWidget* TextServerTab();
#endif
    void FillFolders();
#ifndef READER_NO_SAVE_SYNC
    void LoadSync();
    void StoreSync();
    void TestSync();
    void ShowSyncStatus();
#endif
    void StoreAchievements();
    void LogInAchievements();
#ifndef READER_NO_TEXT_SERVER
    void StoreTextServer();
#endif
    void FillPopupRows();
    void FillConsoleRows();
    void LoadCssFile();
    /// The built-in look shown in the CSS editor ("yomitan-dark"...), or nothing for a file.
    [[nodiscard]] QString BuiltInShown() const;
    void SaveCopy(const QString& theme);
    void Calm(QWidget* box);
    /// The popup's look as controls (popup_controls.h), written into the CSS shown.
    QWidget* ControlsPanel(QWidget* parent);
    void ControlsFromCss();
    void CssFromControls();
    void ShowPreview();

    PlaySettings& play;
    QList<PlayConsole> consoles;
    QString popup_folder;
    HomeWindow* home;
    Hooks hooks;

    QComboBox* popup_layer{};
    QFormLayout* popup_form{};
    QComboBox* css_file{};
    QPlainTextEdit* css_text{};
    QPushButton* css_save{};
    QTabWidget* editor_tabs{};
    QMap<QString, QWidget*> controls; ///< By the control's key
    ReaderUi::PopupWeb* preview{};
    QTimer* preview_timer{};
    QComboBox* console_layer{};
    QFormLayout* console_form{};
    QWidget* folders_box{};
    QListWidget* folders_list{};
    QTabWidget* tabs{};
#ifndef READER_NO_SAVE_SYNC
    // Save sync
    QLineEdit* pair_link{};
    QCheckBox* sync_on{};
    QListWidget* sync_addresses{};
    QLineEdit* sync_code{};
    QLineEdit* sync_device{};
    QLabel* sync_test{};
    QLabel* sync_status{};
    QTimer* sync_timer{};
    quint64 sync_seen{~0ull};
#endif
    // RetroAchievements
    QLineEdit* ra_user{};
    QLineEdit* ra_token{};
    QCheckBox* ra_hardcore{};
    QLabel* ra_note{};
    // The text server
    // The controller
    QComboBox* pointer_button{};

    QCheckBox* hook_on{};
    QSpinBox* hook_port{};
    QCheckBox* hook_lan{};
    QLabel* hook_note{};
    bool filling{};
};

} // namespace Home
