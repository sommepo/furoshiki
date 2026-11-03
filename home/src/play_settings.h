// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The one program's settings for playing (docs/one-program.md, "Settings"): one file in Reader
// Home's folder, every setting looked up in layers, the first that has it winning:
//   1. this game          (optional)
//   2. this console       ("only for the PS2")
//   3. all consoles
//   4. the core's own default
// A setting every console has (picture.resolution, sound.volume: cores/reader_core.h,
// READER_CORE_SETTING_*) can be set for all consoles and changed for one; a console's own
// (ps1.true_color) only for that console. The cores say which they understand; none writes
// settings of its own. The file is program/settings.json:
//   { "format": 1, "all": { "sound.volume": "80" },
//     "consoles": { "ps1": { "picture.resolution": "3" } },
//     "games": { "ps1": { "SLPS-01645": { "ps1.true_color": "1" } } } }

#include <optional>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Home {

/// A setting a core understands, as the pages show it (cores/reader_core.h,
/// reader_core_setting, copied out of the core).
struct PlaySetting {
    enum Kind { Switch = 0, Number = 1, Choice = 2, Text = 3, Folder = 4 };
    QString key;
    QString label;
    int kind{Switch};
    QString default_value;
    double low{}, high{}, step{};
    QStringList choices;

    /// One every console has (a key without its console's name in front).
    [[nodiscard]] bool Common() const;
    /// Not on the pages: the reader's CRT picture has its own page, the speed is for writing
    /// profiles.
    [[nodiscard]] bool Hidden() const;
    /// A value moved by so many steps: a switch flipped, a choice the next or the one before,
    /// a number by its step within its ends.
    [[nodiscard]] QString Moved(const QString& value, int steps) const;
    /// The value as a page shows it ("ON", "3×", "4:3").
    [[nodiscard]] QString Shown(const QString& value) const;
};

/// A console the program has a core for, and what that core understands.
struct PlayConsole {
    QString key;   ///< "ps1"
    QString name;  ///< Its label on the line: "PS1"
    QString app;   ///< The reader app whose games it plays ("station-reader")
    QList<PlaySetting> settings;
};

/// What the common settings are called on the pages, where a core gives no label.
[[nodiscard]] QString CommonLabel(const QString& key);

class PlaySettings {
public:
    enum class Layer { Game, Console, All, Default };
    struct Resolved {
        QString value;
        Layer from{Layer::Default};
    };

    PlaySettings() = default;
    /// Read from (and written to) `file`; nothing there is the same as nothing set.
    explicit PlaySettings(QString file);

    /// The value for `setting` on `console` (and `game`, where given), and the layer it came
    /// from. A console's own setting is never taken from all consoles' layer.
    [[nodiscard]] Resolved Value(const PlaySetting& setting, const QString& console, const QString& game = {}) const;
    /// Whether the layer has the setting: all consoles' (console empty), a console's, a game's.
    [[nodiscard]] bool IsSet(const QString& key, const QString& console = {}, const QString& game = {}) const;
    /// Sets it in that layer, or (an empty value) takes it out so the next layer shows.
    void Set(const QString& key, const QString& value, const QString& console = {}, const QString& game = {});
    void Clear(const QString& key, const QString& console = {}, const QString& game = {}) {
        Set(key, {}, console, game);
    }
    /// Everything a console's core is to be handed, by key, for a game (or none).
    [[nodiscard]] QList<QPair<QString, QString>> ValuesFor(const PlayConsole& console, const QString& game = {}) const;
    /// The folders the program looks for a console's games in, besides those of the games the
    /// reader apps' files name ("folders": { "ps1": [ ... ] }; written by hand so far).
    [[nodiscard]] QStringList Folders(const QString& console) const;
    void SetFolders(const QString& console, const QStringList& folders);
    /// A layer's own value of a key, whatever setting it is (the cheats a game has, a port's
    /// controller), or empty.
    [[nodiscard]] QString Raw(const QString& key, const QString& console = {}, const QString& game = {}) const {
        return LayerOf(console, game).value(key).toString();
    }
    /// Whether any setting is set for this game in particular.
    [[nodiscard]] bool HasGame(const QString& console, const QString& game) const;
    /// Whether the old reader apps' own settings were read in (old_settings.h; once only).
    [[nodiscard]] bool OldAppsRead() const;
    void SetOldAppsRead();

    bool Save() const;
    [[nodiscard]] const QString& File() const {
        return file;
    }

private:
    [[nodiscard]] QJsonObject LayerOf(const QString& console, const QString& game) const;

    QString file;
    QJsonObject root;
};

} // namespace Home
