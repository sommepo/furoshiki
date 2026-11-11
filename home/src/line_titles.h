// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The titles read from lines (docs/furoshiki-3.md, "Sources"): which there are on this device,
// and where a WebSocket's lines come from, in <shared folder>/program/sources.json:
//
//   { "address": "ws://localhost:6677",
//     "ocr": { "address": "http://127.0.0.1:8794", "source": "furoshiki", "token": "…" },
//     "titles": [ { "id": "LINES-3F9A0C1D", "title": "…", "by": "websocket" },
//                 { "id": "LINES-0B1C2D3E", "title": "…", "by": "ocr",
//                   "window": "the window's title", "area": [0.08, 0.7, 0.84, 0.25] } ] }
//
// A title read by a hook Furoshiki starts itself ("hook", hook_source.h; docs/hooks.md) has
//   { "id": "…", "title": "…", "by": "hook", "engine": "agent", "script": "NS_….js",
//     "process": "yuzu.exe", "codes": ["/HS-4@12345:game.exe"], "threads": ["NAME|CODE|CTX|CTX2"],
//     "auto": true }
// its engine ("agent", "luna", "textractor"), an Agent script's file among the fetched scripts,
// the program's file name it is hooked in, hook codes put in besides the engine's own, the
// threads that are its dialogue (hook_engine.h, ThreadKey; an Agent script has none to choose),
// and whether it is read by itself when that program is seen running.
//
// A title read by OCR ("ocr", ocr_source.h) is a window of this computer: which one (by its
// title, as it was when chosen) and where in its picture the text is (0-1 of the picture). "ocr"
// is the OCR helper, Furochibi's service: its address, the picture source this device feeds
// there and that source's token, all settings of the device (there is no address built in).
//
// The page lists them as it lists every game: from a file in <shared folder>/apps, which the
// program writes for them as it does for each console it plays (apps/reader-home-lines.json,
// the "console" Lines), with what was read of them.

#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace Home::Lines {

inline const QString AppKey = QStringLiteral("reader-home-lines");
/// The shelf of titles read this way (the maintainer, 2026-11-10: "it should go into a PC
/// section"); one played in an emulator here stands on that console's shelf instead (ConsoleOf).
inline const QString System = QStringLiteral("PC");
inline const QString DefaultAddress = QStringLiteral("ws://localhost:6677");

/// Another box of a window read by OCR, besides its first: read with it as one line, or only
/// when asked for (a menu, a status screen: the key that reads on request).
struct Box {
    QRectF rect;
    bool ask{};
};

struct Title {
    QString id;    ///< Its game ID in the reader (line_feed.h, GameId)
    QString title;
    QString by;    ///< "websocket", "clipboard", "ocr" or "hook"
    QString window; ///< Read by OCR: the window's title
    QRectF area;    ///< Read by OCR: where the text is, 0-1 of the window's picture
    QList<Box> more; ///< Read by OCR: its other boxes ("areas" in the file, each with "read")
    // Read by a hook (hook_source.h):
    QString engine;      ///< "agent", "luna" or "textractor"
    QString script;      ///< An Agent script's file
    QString process;     ///< The program's file name: "game.exe"
    QStringList codes;   ///< Hook codes put in besides the engine's own
    QStringList threads; ///< The threads that are its dialogue
    bool hook_auto{true}; ///< Read by itself when its program is seen running
    QString path;        ///< The program's whole path where known (its box art is found by it)
    QString console;     ///< The console it is a game of, as the library names them; empty: by its program
};

/// The console a title read from lines is a game of: an emulator's (by its program's name, or its
/// window's), else the PC.
[[nodiscard]] QString ConsoleOf(const Title& title);
/// The console an emulator plays, by its program's file or window's first word; empty: none.
[[nodiscard]] QString EmulatorConsole(const QString& name);

/// The OCR helper (Furochibi's service) a window's pictures go to.
struct Helper {
    QString address;                            ///< "http://host:port"; empty: none set
    QString source{QStringLiteral("furoshiki")}; ///< The pushed picture source this device feeds
    QString token;                              ///< That source's token
    /// A helper installed on this computer: its folder (ocr_helper.h), which Furoshiki starts
    /// it from when nothing answers at the address. Empty: a helper someone else runs.
    QString program;
    /// Which engine reads there: "meikiocr", "oneocr", "screenai" or "glens" (Google Lens, which
    /// sends the text area to Google). Empty: as the helper has it.
    QString engine;
};

/// The engines a helper may read with, in the settings' order, and what each is called.
inline const QStringList OcrEngines{QStringLiteral("meikiocr"), QStringLiteral("oneocr"), QStringLiteral("screenai"), QStringLiteral("glens")};
[[nodiscard]] QString OcrEngineName(const QString& engine);

struct Sources {
    QString address{DefaultAddress};
    Helper ocr;
    QList<Title> titles;
};

[[nodiscard]] Sources Load(const QString& folder);
bool Save(const QString& folder, const Sources& sources);
/// Adds a title read by `by`, or changes how one already there is read. Its ID, or nothing
/// when the title is empty or `by` is no way lines are read.
QString Add(Sources& sources, const QString& title, const QString& by, const QString& record_key);
/// The same for a window read by OCR: which window, and the text's area in its picture.
QString AddWindow(Sources& sources, const QString& title, const QString& window, const QRectF& area, const QString& record_key,
                  const QList<Box>& more = {});
/// The same for a title read by a hook: `hook` has its engine, script, process, codes and threads.
QString AddHook(Sources& sources, const QString& title, const Title& hook, const QString& record_key);
/// The titles a program seen running is read by itself: those read by a hook in `process` with
/// "auto", when exactly one names that program (an emulator hooked for several games is the
/// user's to choose).
[[nodiscard]] QList<Title> HookedIn(const Sources& sources, const QString& process);
/// Takes a title off the list. False when there is none of that ID.
bool Remove(Sources& sources, const QString& id);
/// An area kept within the picture and no smaller than a hundredth of it each way.
[[nodiscard]] QRectF CleanArea(const QRectF& area);
/// The helper's address as it is used: "http://host:port" without a slash at its end; empty
/// when `said` is no http or https address.
[[nodiscard]] QString CleanHelperAddress(const QString& said);

/// Where a title's cover is kept, when it has one (<shared folder>/program/covers/<ID>.jpg): a
/// window read by OCR has its picture as it was when it was added.
[[nodiscard]] QString CoverOf(const QString& folder, const QString& id);
/// How the app's file names a title, in place of a game's file: "lines://websocket/<ID>".
[[nodiscard]] QString PathOf(const Title& title);
/// The way lines are read, for the page: "WEBSOCKET", "CLIPBOARD", "OCR", "HOOK-AGENT"; empty for another path.
[[nodiscard]] QString WayOf(const QString& path);
/// What reads such a title, as the page says it: "LINES  ·  WEBSOCKET", "WINDOW  ·  OCR",
/// "HOOK  ·  AGENT"; empty for a path that is no such title's.
[[nodiscard]] QString ReadByOf(const QString& path);

/// Writes apps/reader-home-lines.json: the titles, and `stats` (the reader's record of them);
/// without `stats`, what the file had stays. `program` is the program's own file.
bool WriteApp(const QString& folder, const Sources& sources, const QString& program, const QJsonObject* stats);

} // namespace Home::Lines
