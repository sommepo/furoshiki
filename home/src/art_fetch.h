// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// A title's box art found by the program itself, where none is built in (docs/furoshiki-3.md, 14;
// the maintainer, 2026-11-10: "there should be an option where box art is missing to fetch it").
// A game Steam installed: Steam's own portrait art of it (its app ID from its folder and Steam's
// manifests). Any game: VNDB's box front and spine of a release on its console, the entry found
// by its name exactly as tools/titles.py does (or by the title record's VNDB ID). Kept in the
// shared folder's program/art as <game ID>.cover.jpg and <game ID>.spine.jpg; a spine not found
// is made in its console's look on the shelf.

#pragma once

#include <functional>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace Home::Art {

[[nodiscard]] QString CoverFile(const QString& shared, const QString& game_id);
[[nodiscard]] QString SpineFile(const QString& shared, const QString& game_id);
/// VNDB's name of a console, as the library names it ("PC" -> "win"); empty for one it has none of.
[[nodiscard]] QString VndbPlatform(const QString& system);
/// Steam's app ID of a game Steam installed: from its path (steamapps/common/<folder>, the
/// library's appmanifest_*.acf), its steam_appid.txt, or else the Steam libraries of this computer
/// searched for its program's file. Empty when it is no Steam game.
[[nodiscard]] QString SteamAppId(const QString& exe_path, const QString& exe_name, const QString& steam_root = {});
/// Letters and figures only, folded: names compared as titles.py compares them.
[[nodiscard]] QString PlainName(const QString& text);

struct Want {
    QString game_id;
    QString title, english; ///< Its names: the one it was added under, its record's English one
    QString system;         ///< The console, as the library names it
    QString exe_path, exe_name;
    QString vndb;           ///< Its record's VNDB ID ("v12345"), where it has one
};

class Finder : public QObject {
    Q_OBJECT

public:
    explicit Finder(QObject* parent = nullptr);
    /// Begins looking; false while it looks for another.
    bool Start(const Want& want, const QString& shared);
    [[nodiscard]] bool Busy() const {
        return busy;
    }
    [[nodiscard]] const QString& GameId() const {
        return want.game_id;
    }

signals:
    /// `said`: what was found, for the user.
    void Done(const QString& game_id, bool found, const QString& said);

private:
    void Steam();
    void Vndb();
    void Releases(const QString& vn);
    void Save(const QString& url, const QString& file, bool spine, std::function<void(bool)> then);
    void Finish();

    QNetworkAccessManager* net{};
    Want want;
    QString shared;
    bool busy{}, got_cover{}, got_spine{};
    QStringList from;
};

} // namespace Home::Art
