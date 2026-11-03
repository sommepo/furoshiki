// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

// The development channel's commands of this console's own (reader_control.cpp asks here
// first), and fetching from the network with the emulator's own downloader.

#include <algorithm>
#include <cstdlib>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include "common/HTTPDownloader.h"
#include "pcsx2/Host.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/qt/host.h"
#include "reader/qt/ps2_home.h"

namespace ReaderUi {

bool HostCommand(const QString& name, const QStringList& parts, const QString& rest,
                 const std::function<void(const QString&)>& reply, QObject* context) {
    const auto ok = [&reply](const QString& text = {}) {
        reply(text.isEmpty() ? QStringLiteral("OK") : QStringLiteral("OK ") + text);
    };
    if (name == QStringLiteral("home")) {
        // The PS2-style menu: which of its screens is up, what it shows, what is chosen.
        ok(QString::fromUtf8(QJsonDocument(Home()->Describe()).toJson(QJsonDocument::Compact)));
    } else if (name == QStringLiteral("pad")) {
        // A button of the first controller, held for a moment: pad NAME [MS].
        static const QStringList names{QStringLiteral("up"), QStringLiteral("right"), QStringLiteral("down"),
                                       QStringLiteral("left"), QStringLiteral("triangle"), QStringLiteral("circle"),
                                       QStringLiteral("cross"), QStringLiteral("square"), QStringLiteral("select"),
                                       QStringLiteral("start"), QStringLiteral("l1"), QStringLiteral("l2"),
                                       QStringLiteral("r1"), QStringLiteral("r2")};
        const int button = static_cast<int>(names.indexOf(parts.value(1).toLower()));
        if (button < 0) {
            reply(QStringLiteral("ERR one of: ") + names.join(u' '));
            return true;
        }
        const int hold = parts.size() > 2 ? std::clamp(parts.value(2).toInt(), 20, 10000) : 120;
        Reader::Emu::PressButton(button, true);
        QTimer::singleShot(hold, context, [button, reply] {
            Reader::Emu::PressButton(button, false);
            reply(QStringLiteral("OK"));
        });
    } else if (name == QStringLiteral("dump")) {
        // The console's main memory, to a file (under private/): for finding a game's text.
        ok(Reader::Emu::DumpMemory(rest.toStdString()) ? rest : QStringLiteral("(not written)"));
    } else if (name == QStringLiteral("state")) {
        // state save N / state load N: a save state slot of the running game (harness only).
        const bool save = parts.value(1) == QStringLiteral("save");
        const int slot = parts.value(2).toInt();
        if ((!save && parts.value(1) != QStringLiteral("load")) || slot < 1 || slot > 10) {
            reply(QStringLiteral("ERR state save|load 1-10"));
            return true;
        }
        Reader::Emu::RunOnEmulator([save, slot, reply] {
            bool fine = true;
            if (save) {
                Reader::Emu::SaveStateSlot(slot);
            } else {
                fine = Reader::Emu::LoadStateSlot(slot);
            }
            PostToGui([fine, reply] { reply(fine ? QStringLiteral("OK") : QStringLiteral("ERR not loaded")); });
        });
    } else if (name == QStringLiteral("pause")) {
        // pause on / pause off: the game held or let go (harness only).
        const bool paused = parts.value(1) != QStringLiteral("off");
        Reader::Emu::RunOnEmulator([paused] { Reader::Emu::SetPaused(paused); });
        ok();
    } else if (name == QStringLiteral("speed")) {
        // speed turbo / speed normal: the speed limit off or on (harness only).
        const bool turbo = parts.value(1) == QStringLiteral("turbo");
        Reader::Emu::RunOnEmulator([turbo] { Reader::Emu::SetTurbo(turbo); });
        ok();
    } else if (name == QStringLiteral("iop")) {
        // iop PATH: the IOP's main memory, to a file (harness only).
        ok(Reader::Emu::DumpIopMemory(rest.toStdString()) ? rest : QStringLiteral("(not written)"));
    } else if (name == QStringLiteral("vram")) {
        // vram PATH: the GS's memory as a 4-bit texture 1024 wide, a byte a pixel (harness only).
        ok(Reader::Emu::DumpVram4(rest.toStdString()) ? rest : QStringLiteral("(not written)"));
    } else if (name == QStringLiteral("lua")) {
        // A piece of Lua in the engine, on the emulator's thread; what it returns.
        const std::string code = rest.toStdString();
        Reader::Emu::RunOnEmulator([code, reply] {
            std::string output;
            const bool fine = Reader::Engine::Instance().Execute(code, output);
            const QString text = QString::fromStdString(output);
            PostToGui([fine, text, reply] { reply((fine ? QStringLiteral("OK ") : QStringLiteral("ERR ")) + text); });
        });
    } else if (name == QStringLiteral("profile")) {
        // Loads a profile from a file (one being written, under private/) into the running game.
        QFile file(rest);
        if (!file.open(QIODevice::ReadOnly)) {
            reply(QStringLiteral("ERR cannot read ") + rest);
            return true;
        }
        const std::string source = file.readAll().toStdString();
        const std::string chunk = QFileInfo(rest).fileName().toStdString();
        Reader::Emu::RunOnEmulator([source, chunk, reply] {
            Reader::Engine& engine = Reader::Engine::Instance();
            const bool fine = engine.LoadProfile(chunk, source);
            const QString text = fine ? QStringLiteral("loaded") : QString::fromStdString(engine.LastError());
            PostToGui([fine, text, reply] { reply((fine ? QStringLiteral("OK ") : QStringLiteral("ERR ")) + text); });
        });
    } else if (name == QStringLiteral("home-demo")) {
        // Made-up games in the Browser, to look at it without any game: N of them, every
        // third without a picture, every other one marked as readable.
        const int count = std::clamp(parts.value(1).toInt(), 0, 200);
        QList<Ps2Home::Game> games;
        const QStringList names{QStringLiteral("夏の夜の物語"), QStringLiteral("Made-up Adventure"),
                                QStringLiteral("作り話 -another story-"), QStringLiteral("Sample Quest II"),
                                QStringLiteral("ひぐらしの例"), QStringLiteral("Example Racing")};
        for (int index = 0; index < count; ++index) {
            Ps2Home::Game game;
            game.path = QStringLiteral("/made-up/%1.iso").arg(index);
            game.name = names.at(index % names.size()) + (index >= names.size() ? QStringLiteral(" %1").arg(index) : QString());
            game.serial = QStringLiteral("SLPM-%1").arg(60000 + index * 37);
            game.readable = index % 2 == 0;
            if (index % 3 != 2) {
                QPixmap picture(184, 260);
                QPainter painter(&picture);
                QLinearGradient shade(0, 0, 184, 260);
                shade.setColorAt(0, QColor::fromHsv((index * 47) % 360, 150, 190));
                shade.setColorAt(1, QColor::fromHsv((index * 47 + 60) % 360, 190, 70));
                painter.fillRect(picture.rect(), shade);
                painter.fillRect(QRect(0, 0, 184, 26), QColor(10, 10, 14));
                painter.setPen(Qt::white);
                QFont font = painter.font();
                font.setPixelSize(20);
                painter.setFont(font);
                painter.drawText(QRect(10, 60, 164, 160), Qt::AlignCenter | Qt::TextWrapAnywhere, game.name);
                painter.end();
                game.picture = picture;
            }
            games.append(game);
        }
        Home()->SetGames(games);
        ok();
    } else if (name == QStringLiteral("home-key")) {
        // A key to the menu, as the keyboard would send it: Left, Return, Escape, a.
        const QKeySequence sequence(parts.value(1));
        if (sequence.isEmpty()) {
            reply(QStringLiteral("ERR unknown key"));
            return true;
        }
        const int key = sequence[0].key();
        const QString text = parts.value(1).size() == 1 ? parts.value(1).toLower() : QString();
        Ps2Home* home = Home();
        QTimer::singleShot(0, home, [home, key, text] {
            QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(home, &down);
        });
        ok();
    } else if (name == QStringLiteral("home-click")) {
        // A click in the menu: X Y in its own coordinates.
        Ps2Home* home = Home();
        const QPointF at(parts.value(1).toDouble(), parts.value(2).toDouble());
        QTimer::singleShot(0, home, [home, at] {
            QMouseEvent press(QEvent::MouseButtonPress, at, home->mapToGlobal(at), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(home, &press);
        });
        ok();
    } else if (name == QStringLiteral("home-grab")) {
        ok(Home()->grab().save(rest) ? rest : QStringLiteral("(not written)"));
    } else {
        return false;
    }
    return true;
}

std::optional<QByteArray> HostFetch(const QUrl& url, QString* error) {
    // With the emulator's own downloader: the caller's window waits, the app goes on drawing.
    std::unique_ptr<HTTPDownloader> http = HTTPDownloader::Create(Host::GetHTTPUserAgent());
    if (!http) {
        *error = QCoreApplication::translate("ReaderUi", "The app cannot go on the network here");
        return std::nullopt;
    }
    http->SetTimeout(30.0f);
    std::optional<QByteArray> out;
    s32 status = 0;
    http->CreateRequest(url.toString(QUrl::FullyEncoded).toStdString(),
                        [&out, &status](s32 code, const std::string&, std::vector<u8> data) {
                            status = code;
                            if (code == HTTPDownloader::HTTP_STATUS_OK) {
                                out = QByteArray(reinterpret_cast<const char*>(data.data()),
                                                 static_cast<qsizetype>(data.size()));
                            }
                        });
    while (http->HasAnyRequests()) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        http->PollRequests();
        QThread::msleep(10);
    }
    if (!out) {
        *error = status == HTTPDownloader::HTTP_STATUS_TIMEOUT
                     ? QCoreApplication::translate("ReaderUi", "%1 did not answer").arg(url.host())
                     : QCoreApplication::translate("ReaderUi", "Could not fetch %1 (%2)").arg(url.toString()).arg(status);
    }
    return out;
}

} // namespace ReaderUi
