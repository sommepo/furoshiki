// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The wizard's door A, a game of a console played here that has no script of ours: READ BY HOOK
// (docs/core-hooks.md; the maintainer, 2026-11-13: "add a title for core games without direct
// support via hook (luna and agent do also hook emulated games too so they should be supported by
// hook)"). From the console's step, H lists that console's games found in its folders that no
// script reads, each with what LunaHook and Agent's scripts know of it; Enter on one writes its
// hook description into the user's profiles (core_hooks.h, Install), and the game is then listed
// and read like any scripted one. The last row fetches (again) LunaHook's tables and Agent's
// scripts and converts them. Part of home_window.cpp, after wizard_hooks.inc, inside namespace Home.

struct HomeWindow::CoreHookSteps {
    QString console;      ///< Its key: "psp"
    QString console_name; ///< As the line says it
    QList<UnscriptedGame> games;
    QList<CoreHooks::Game> known; ///< Every description there is (the index)
    Hooks::Fetch fetch;
    QStringList fetching; ///< The kits still to fetch, in turn
    QString said;
};

namespace {

/// The user's own profiles: where a description goes (ReaderSession's data folder's profiles).
QString UserProfiles(const QString& shared) {
    return shared + QStringLiteral("/program/reader/profiles");
}

/// What is known of a game for its row: "AGENT · 3 HOOKS", or nothing.
QString KnownOf(const QList<CoreHooks::Game>& found) {
    if (found.isEmpty()) {
        return QStringLiteral("NO HOOK FOUND");
    }
    const int hooks = static_cast<int>(found.first().hooks.size());
    return QStringLiteral("%1  ·  %2 HOOK%3%4")
        .arg(found.first().source.toUpper())
        .arg(hooks)
        .arg(hooks == 1 ? QString() : QStringLiteral("S"))
        .arg(found.size() > 1 ? QStringLiteral("  ·  +%1").arg(found.size() - 1) : QString());
}

} // namespace

bool HomeWindow::CoreHookStepOn() const {
    return wizard_on && wizard && wizard->core_hook && wizard->step == Wizard::CoreHook;
}

void HomeWindow::CoreHookOpen() {
    Wizard& w = *wizard;
    if (consoles.isEmpty()) {
        return;
    }
    const PlayConsole& console = consoles[std::clamp(w.row, 0, static_cast<int>(consoles.size()) - 1)];
    if (!w.core_hook) {
        w.core_hook = std::make_shared<CoreHookSteps>();
        CoreHookSteps* c = w.core_hook.get();
        Wizard* of = wizard.get();
        connect(&c->fetch, &Hooks::Fetch::Done, this, [this, of, c](const QString&, bool ok, const QString& said) {
            if (wizard.get() != of) {
                return;
            }
            c->said = said.toUpper();
            if (ok && !c->fetching.isEmpty()) {
                c->fetch.Start(c->fetching.takeFirst(), SharedFolder());
            } else {
                c->fetching.clear();
                QString converted;
                if (CoreHooks::Rebuild(SharedFolder(), converted) >= 0) {
                    c->known = CoreHooks::Load(SharedFolder());
                }
                c->said = (ok ? converted : said).toUpper();
                sounds.Play(ok ? Sound::Select : Sound::Refuse);
            }
            update();
        });
    }
    CoreHookSteps& c = *w.core_hook;
    c.console = console.key;
    c.console_name = console.name;
    c.games = unscripted_games ? unscripted_games(console.key) : QList<UnscriptedGame>();
    c.known = CoreHooks::Load(SharedFolder());
    c.said.clear();
    w.step = Wizard::CoreHook;
    w.row = 0;
    sounds.Play(Sound::Select);
    update();
}

void HomeWindow::CoreHookAccept() {
    Wizard& w = *wizard;
    CoreHookSteps& c = *w.core_hook;
    const QString shared = SharedFolder();
    if (w.row >= c.games.size()) {
        // The last row: LunaHook's tables and Agent's scripts, fetched and converted.
        if (c.fetch.Busy()) {
            return;
        }
        c.fetching = {QStringLiteral("scripts")};
        c.said = QStringLiteral("DOWNLOADING LUNAHOOK TABLES");
        c.fetch.Start(QStringLiteral("luna-tables"), shared);
        sounds.Play(Sound::Select);
        update();
        return;
    }
    const UnscriptedGame& game = c.games[w.row];
    const QList<CoreHooks::Game> found = CoreHooks::Find(c.known, c.console, game.id);
    if (found.isEmpty()) {
        sounds.Play(Sound::Refuse);
        c.said = c.known.isEmpty() ? QStringLiteral("DOWNLOAD HOOKS FIRST (LAST ROW)") : QStringLiteral("NO HOOK FOUND FOR %1").arg(game.id);
        update();
        return;
    }
    QString error;
    if (!CoreHooks::Install(found.first(), game.id, game.title, UserProfiles(shared), error)) {
        sounds.Play(Sound::Refuse);
        c.said = error.toUpper();
        update();
        return;
    }
    const QString console = c.console, title = game.title.isEmpty() ? game.id : game.title;
    CloseWizard();
    SetPages(false);
    if (console_folders_changed) {
        console_folders_changed(console); // looked for again: it is listed now
    }
    Say(QStringLiteral("Added %1 (hook from %2)").arg(title, found.first().source));
}

bool HomeWindow::CoreHookKey(int key) {
    Wizard& w = *wizard;
    CoreHookSteps& c = *w.core_hook;
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        CoreHookAccept();
    } else if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        sounds.Play(Sound::Back);
        w.step = Wizard::Console;
        w.row = std::max(0, static_cast<int>(std::find_if(consoles.cbegin(), consoles.cend(), [&](const PlayConsole& one) { return one.key == c.console; }) - consoles.cbegin()));
        update();
    } else if (key == Qt::Key_Up || key == Qt::Key_Down) {
        const int next = std::clamp(w.row + (key == Qt::Key_Up ? -1 : 1), 0, static_cast<int>(c.games.size()));
        if (next != w.row) {
            w.row = next;
            sounds.Play(Sound::Move);
            update();
        }
    }
    return true;
}

void HomeWindow::PaintCoreHook(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    Wizard& w = *wizard;
    CoreHookSteps& c = *w.core_hook;
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont value = ShelfFont(l.mono, 12.5, 0.05);
    const QFont name_face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 15.5);
    const QFont key_face = ShelfFont(l.display, 12, 0.16);
    const double bottom = ShelfBottom, table_width = WizardSide - 50 - ShelfLeft;

    // The head, as every step of the wizard has it.
    double x = ShelfLeft;
    x += Put(p, small, x, 49, QStringLiteral("‹  LIBRARY"), l.muted);
    x += Put(p, small, x, 49, QStringLiteral("  ·  "), l.muted);
    Put(p, small, x, 49, QStringLiteral("ADD GAME"), l.ink);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    right -= Put(p, small, right, 49, QStringLiteral(" / 02  ·  "), l.muted, 1);
    right -= Put(p, small, right, 49, QStringLiteral("02"), l.accent, 1);
    Put(p, small, right, 49, QStringLiteral("%1  ·  %2  ·  ").arg(QLatin1String(DoorLetters[0]), QLatin1String(DoorNames[0])), l.muted, 1);
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
    shelf_hits.append({QRectF(ShelfLeft, 30, 140, 30), 20, 0});

    Put(p, ShelfFont(l.display, 27, 0.02), ShelfLeft, ShelfTop + 44, QStringLiteral("WHICH GAME?"), l.ink);
    Put(p, small, ShelfLeft, ShelfTop + 72, QStringLiteral("%1 GAMES WITHOUT A SCRIPT  ·  ↑ ↓   ENTER   ESC").arg(c.console_name.toUpper()), l.muted);

    const bool fetch_row = w.row >= c.games.size();
    const QList<CoreHooks::Game> found = fetch_row ? QList<CoreHooks::Game>() : CoreHooks::Find(c.known, c.console, c.games[w.row].id);
    const QString key_label = fetch_row ? (c.fetch.Busy() ? QStringLiteral("FETCHING") : QStringLiteral("DOWNLOAD AND CONVERT"))
                              : found.isEmpty() ? QStringLiteral("NO HOOK FOUND") : QStringLiteral("USE HOOK");
    const QRectF key(WizardSide, bottom - 44, ShelfRight - WizardSide, 44);
    p.fillRect(key, l.ink);
    Put(p, key_face, key.center().x(), key.center().y() + 4.6, key_label, l.ground, 2);
    shelf_hits.append({key, 22, 0});

    // The games, then the row that fetches; a long list from where the chosen one is.
    double y = WizardTop + 30;
    const int shown = std::max(1, static_cast<int>((bottom - 60 - y) / 34));
    const int count = static_cast<int>(c.games.size()) + 1;
    const int first = std::clamp(w.row - shown / 2, 0, std::max(0, count - shown));
    p.fillRect(QRectF(ShelfLeft, y - 22, table_width, 1), l.rule);
    for (int i = first; i < std::min(count, first + shown); ++i) {
        const bool on = i == w.row;
        QString left, right_text;
        if (i < c.games.size()) {
            left = c.games[i].title.isEmpty() ? c.games[i].id : c.games[i].title;
            right_text = KnownOf(CoreHooks::Find(c.known, c.console, c.games[i].id));
        } else {
            left = c.known.isEmpty() ? QStringLiteral("Download hooks (LunaHook, Agent)") : QStringLiteral("Download again");
            right_text = c.known.isEmpty() ? QStringLiteral("NONE") : QStringLiteral("%1 DESCRIPTIONS").arg(c.known.size());
        }
        if (on) {
            p.fillRect(QRectF(ShelfLeft, y - 21, 3, 33), l.accent);
        }
        Put(p, tiny, ShelfLeft + 16, y - 1, QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), on ? l.ink : l.muted);
        Put(p, name_face, ShelfLeft + 56, y, Paint::Fit(left, name_face, table_width - 56 - 300), on ? l.ink : Faded(l.ink, 0.62));
        Put(p, value, ShelfLeft + table_width, y - 1, Paint::Fit(right_text, value, 290), on ? l.ink : l.muted, 1);
        p.fillRect(QRectF(ShelfLeft, y + 12, table_width, 1), l.rule);
        shelf_hits.append({QRectF(ShelfLeft, y - 22, table_width, 34), 21, i});
        y += 34;
    }

    // What is known of the game in hand, beside the list.
    QList<QPair<QString, QString>> facts;
    if (fetch_row) {
        const Hooks::Kit luna = Hooks::KitOf(QStringLiteral("luna-tables")), agent = Hooks::KitOf(QStringLiteral("scripts"));
        facts = {{QStringLiteral("LUNAHOOK"), luna.whose.toUpper()}, {QStringLiteral("AGENT"), agent.whose.toUpper()},
                 {QStringLiteral("SIZE"), QStringLiteral("%1 + %2").arg(luna.size, agent.size)}, {QStringLiteral("SAVED IN"), QStringLiteral("PROGRAM/HOOKS")}};
    } else {
        facts.append({QStringLiteral("ID"), c.games[w.row].id});
        if (!found.isEmpty()) {
            const CoreHooks::Game& game = found.first();
            facts.append({QStringLiteral("FROM"), game.source.toUpper() + QStringLiteral("  ·  ") + game.from});
            if (!game.version.isEmpty()) {
                facts.append({QStringLiteral("VERSION"), game.version});
            }
            QStringList names;
            for (const CoreHooks::Hook& hook : game.hooks) {
                names.append(hook.name);
            }
            facts.append({QStringLiteral("HOOKS"), names.join(QStringLiteral("  ·  "))});
            facts.append({QStringLiteral("ON THE SCREEN"), QStringLiteral("TEXT STRIP ONLY")});
        }
    }
    if (!c.said.isEmpty()) {
        facts.append({QStringLiteral("SAID"), c.fetch.Busy() ? c.fetch.Said().toUpper() : c.said});
    }
    double fy = WizardTop + 27;
    for (const auto& fact : facts) {
        p.fillRect(QRectF(WizardSide, fy - 19, ShelfRight - WizardSide, 1), l.rule);
        Put(p, tiny, WizardSide, fy, fact.first, l.muted);
        Put(p, value, ShelfRight, fy, Paint::Fit(fact.second, value, ShelfRight - WizardSide - 110), l.ink, 1);
        fy += 30;
    }
    p.fillRect(QRectF(WizardSide, fy - 19, ShelfRight - WizardSide, 1), l.rule);
}
