// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The first steps (the maintainer, 2026-11-20: "a welcome on Windows"): on a first start, with
// nothing set up yet, the library opens on three calm steps drawn as the Add game wizard's are,
// each one skipped with Escape, counted truly: 1 of 3 a dictionary (lookups need one), 2 of 3
// where the games are, 3 of 3 Anki (optional). It stands in the wizard's place (Wizard::welcome),
// so the wizard's keys, pointer and coming and going are its own; and it takes the place of the
// shared layer's two questions at a first start (ReaderController::FirstStart leaves them out
// where the host has settings pages). Settings, About, Setup opens it again.
// Part of home_window.cpp, after wizard_page.inc, inside namespace Home.

void HomeWindow::OpenWelcome() {
    if (!shelf_on) {
        SetShelf(true, false);
    }
    OpenWizard();
    Wizard& w = *wizard;
    w.welcome = true;
    w.welcome_step = 0;
    w.welcome_add = false;
    w.row = 0;
    QSettings().setValue(QStringLiteral("welcome/shown"), true);
    update();
}

void HomeWindow::WelcomeIfFirst() {
    // A first start: never shown before, and nothing here yet (no game, no dictionary).
    QSettings settings;
    if (settings.value(QStringLiteral("welcome/shown")).toBool() || wizard_on || dry_welcome_off) {
        return;
    }
    const bool dictionaries = setting_value && !setting_value(QStringLiteral("Dictionaries")).startsWith(QStringLiteral("NONE"));
    if (!library.games.isEmpty() || dictionaries) {
        settings.setValue(QStringLiteral("welcome/shown"), true); // set up before this was asked
        return;
    }
    OpenWelcome();
}

int HomeWindow::WelcomeRows() const {
    const Wizard& w = *wizard;
    // The dictionary: Jitendex, your own, not now. The games: each console's folder, another
    // way, not now. Anki: set it up, not now.
    return w.welcome_step == 0 ? 3 : w.welcome_step == 1 ? static_cast<int>(consoles.size()) + 2 : 2;
}

void HomeWindow::WelcomeNext() {
    Wizard& w = *wizard;
    if (w.welcome_step < 2) {
        ++w.welcome_step;
        w.row = 0;
        wizard_seen_step = -1; // the next step comes in as a step does
        sounds.Play(Sound::Select);
        update();
        return;
    }
    const bool add = w.welcome_add;
    CloseWizard();
    SetPages(false);
    sounds.Play(Sound::Select);
    if (add) {
        OpenWizard(); // "Add game another way", asked for at the second step
    } else {
        Say(QStringLiteral("Setup is done. It is in Settings, About, if you want it again"));
    }
}

void HomeWindow::WelcomeAccept() {
    Wizard& w = *wizard;
    const int rows = WelcomeRows();
    w.row = std::clamp(w.row, 0, rows - 1);
    if (w.row == rows - 1) {
        WelcomeNext(); // not now
        return;
    }
    if (w.welcome_step == 0) {
        if (!setting_step) {
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("Dictionaries need an emulator, and this build has none"));
            return;
        }
        // Jitendex fetched and installed, or the dictionaries window for the user's own files.
        setting_step(w.row == 0 ? QStringLiteral("Download Jitendex") : QStringLiteral("Dictionaries"), 0);
        if (setting_value && !setting_value(QStringLiteral("Dictionaries")).startsWith(QStringLiteral("NONE"))) {
            WelcomeNext();
        }
        update();
        return;
    }
    if (w.welcome_step == 1) {
        if (w.row < consoles.size()) {
            // The Consoles page's own picker: a folder of that console's games.
            if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
                Say(QStringLiteral("Couldn't open the folder chooser: there is no display"));
                return;
            }
            console_layer = w.row + 1;
            console_game = 0;
            const QString console = LayerConsole();
            const int before = play != nullptr ? static_cast<int>(play->Folders(console).size()) : 0;
            ConsoleHeadAction(2, false);
            if (play != nullptr && play->Folders(console).size() > before) {
                WelcomeNext();
            }
            return;
        }
        w.welcome_add = true;
        WelcomeNext();
        return;
    }
    if (setting_step) {
        setting_step(QStringLiteral("Anki"), 0);
        if (setting_value && !setting_value(QStringLiteral("Anki")).startsWith(QStringLiteral("NOT SET"))) {
            WelcomeNext();
        }
        update();
    } else {
        sounds.Play(Sound::Refuse);
    }
}

bool HomeWindow::WelcomeKey(int key) {
    Wizard& w = *wizard;
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        WelcomeAccept();
    } else if (key == Qt::Key_Escape) {
        WelcomeNext(); // each step can be skipped
    } else if (key == Qt::Key_Backspace || key == Qt::Key_Left) {
        if (w.welcome_step > 0) {
            --w.welcome_step;
            w.row = 0;
            wizard_seen_step = -1;
            sounds.Play(Sound::Back);
            update();
        }
    } else if (key == Qt::Key_Up || key == Qt::Key_Down) {
        const int next = std::clamp(w.row + (key == Qt::Key_Up ? -1 : 1), 0, WelcomeRows() - 1);
        if (next != w.row) {
            w.row = next;
            sounds.Play(Sound::Move);
            update();
        }
    }
    return true;
}

void HomeWindow::PaintWelcome(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    Wizard& w = *wizard;
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont value = ShelfFont(l.mono, 12.5, 0.05);
    const QFont body = ShelfFont(l.mono, 11.5, 0.03);
    const QFont name_face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 15.5);
    const QFont key_face = ShelfFont(l.display, 12, 0.16);
    const double bottom = ShelfBottom, table_width = WizardSide - 50 - ShelfLeft;
    const int rows = WelcomeRows();
    w.row = std::clamp(w.row, 0, rows - 1);

    // The head: the way out, which of the three steps this is.
    double x = ShelfLeft;
    x += Put(p, small, x, 49, QStringLiteral("‹  LIBRARY"), Mix(l.muted, l.ink, Ease(20, 0, ShelfHovered(20, 0))));
    x += Put(p, small, x, 49, QStringLiteral(" · "), l.muted);
    Put(p, small, x, 49, QStringLiteral("SETUP"), l.ink);
    static const char* const names[] = {"DICTIONARY", "GAMES", "ANKI"};
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    right -= Put(p, small, right, 49, QStringLiteral(" / 03 · "), l.muted, 1);
    right -= Put(p, small, right, 49, QStringLiteral("%1").arg(w.welcome_step + 1, 2, 10, QLatin1Char('0')), l.accent, 1);
    Put(p, small, right, 49, QLatin1String(names[w.welcome_step]) + QStringLiteral(" · "), l.muted, 1);
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
    shelf_hits.append({QRectF(ShelfLeft, 30, 140, 30), 20, 0});

    // The step's own: its question, its rows, what is known beside them, a few words under.
    QString question, note, key_label;
    QList<QPair<QString, QString>> list, facts;
    const QString dictionaries = setting_value ? setting_value(QStringLiteral("Dictionaries")) : QString();
    const QString anki = setting_value ? setting_value(QStringLiteral("Anki")) : QString();
    if (w.welcome_step == 0) {
        question = QStringLiteral("WHICH DICTIONARY?");
        list = {{QStringLiteral("Download Jitendex"), QStringLiteral("FREE · 39 MB")},
                {QStringLiteral("Choose your own"), QStringLiteral("YOMITAN .ZIP FILES")},
                {QStringLiteral("Not now"), QString()}};
        facts = {{QStringLiteral("DICTIONARIES"), dictionaries.isEmpty() ? QStringLiteral("NONE INSTALLED") : dictionaries}};
        note = QStringLiteral("LOOKING UP A WORD NEEDS A DICTIONARY. MORE CAN BE ADDED LATER IN SETTINGS, READING, DICTIONARIES.");
        key_label = w.row == 0 ? QStringLiteral("DOWNLOAD") : w.row == 1 ? QStringLiteral("CHOOSE FILES") : QStringLiteral("SKIP");
    } else if (w.welcome_step == 1) {
        question = QStringLiteral("WHERE ARE YOUR GAMES?");
        for (const PlayConsole& console : consoles) {
            const int folders = play != nullptr ? static_cast<int>(play->Folders(console.key).size()) : 0;
            list.append({ConsoleFull(console.key, console.name), folders == 0 ? QStringLiteral("CHOOSE A FOLDER") : folders == 1 ? QStringLiteral("1 FOLDER") : QStringLiteral("%1 FOLDERS").arg(folders)});
        }
        list.append({QStringLiteral("Add game another way"), QStringLiteral("ONE GAME · A WINDOW · A TEXTHOOKER")});
        list.append({QStringLiteral("Not now"), QString()});
        facts = {{QStringLiteral("GAMES IN THE LIBRARY"), QString::number(library.games.size())}};
        note = QStringLiteral("CHOOSE THE FOLDER A CONSOLE'S GAMES ARE IN. A GAME WITH A GAME SCRIPT IS ADDED BY ITSELF; GAME SCRIPTS ARE INSTALLED IN SETTINGS, CONSOLES. ANY OTHER GAME IS ADDED WITH ADD GAME.");
        key_label = w.row < consoles.size() ? QStringLiteral("CHOOSE A FOLDER") : w.row == rows - 1 ? QStringLiteral("SKIP") : QStringLiteral("ADD GAME AFTER SETUP");
    } else {
        question = QStringLiteral("MAKE ANKI CARDS?");
        list = {{QStringLiteral("Set up Anki"), QStringLiteral("DECK · NOTE TYPE · FIELDS")}, {QStringLiteral("Not now"), QString()}};
        facts = {{QStringLiteral("ANKI"), anki.isEmpty() ? QStringLiteral("NOT SET") : anki}};
        note = QStringLiteral("OPTIONAL. IT NEEDS ANKI OPEN ON THIS COMPUTER, WITH THE ANKICONNECT ADD-ON (IN ANKI: TOOLS, ADD-ONS, GET ADD-ONS, CODE 2055492159). THEN PRESS CONNECT.");
        key_label = w.row == 0 ? QStringLiteral("SET UP ANKI") : QStringLiteral("FINISH");
    }

    // The step comes in as a wizard's does, and the mark glides from row to row.
    if (wizard_seen_step != 100 + w.welcome_step) {
        wizard_seen_step = 100 + w.welcome_step;
        wizard_t = shelf_still ? 1 : 0;
        wizard_mark_at = w.row;
    }
    GlideTo(wizard_mark_at, w.row, 0.035);
    shelf_busy = shelf_busy || wizard_t != 1;
    p.save();
    p.setOpacity(p.opacity() * wizard_t);
    p.translate(14 * (1 - wizard_t) * (1 - wizard_t), 0);
    Put(p, ShelfFont(l.display, 27, 0.02), ShelfLeft, ShelfTop + 44, question, l.ink);

    const QRectF key(WizardSide, bottom - 44, ShelfRight - WizardSide, 44);
    {
        const double lit = Ease(22, 0, ShelfHovered(22, 0));
        p.fillRect(key, l.ink);
        p.fillRect(QRectF(key.left(), key.bottom() - 4 * lit, key.width(), 4 * lit), l.accent);
        Put(p, key_face, key.center().x(), key.center().y() + 4.6 - 1.5 * lit, key_label, l.ground, 2);
        shelf_hits.append({key, 22, 0});
    }
    double y = WizardTop + 30;
    p.fillRect(QRectF(ShelfLeft, y - 22, table_width, 1), l.rule);
    p.fillRect(QRectF(ShelfLeft, y - 21 + std::clamp(wizard_mark_at, 0.0, rows - 1.0) * 34, 3, 33), l.accent);
    for (int i = 0; i < list.size(); ++i) {
        const bool on = i == w.row;
        const double lit = Ease(21, i, ShelfHovered(21, i));
        if (!on && lit > 0) {
            p.fillRect(QRectF(ShelfLeft, y - 21, 3, 33), Faded(l.accent, 0.4 * lit));
        }
        Put(p, tiny, ShelfLeft + 16, y - 1, QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), on ? l.ink : l.muted);
        Put(p, name_face, ShelfLeft + 56, y, Paint::Fit(list[i].first, name_face, table_width - 56 - 320), on ? l.ink : Mix(Faded(l.ink, 0.62), l.ink, lit));
        Put(p, value, ShelfLeft + table_width, y - 1, Paint::Fit(list[i].second, value, 310), on ? l.ink : Mix(l.muted, l.ink, lit), 1);
        p.fillRect(QRectF(ShelfLeft, y + 12, table_width, 1), l.rule);
        shelf_hits.append({QRectF(ShelfLeft, y - 22, table_width, 34), 21, i});
        y += 34;
    }
    double fy = WizardTop + 27;
    for (const auto& fact : facts) {
        p.fillRect(QRectF(WizardSide, fy - 19, ShelfRight - WizardSide, 1), l.rule);
        Put(p, tiny, WizardSide, fy, fact.first, l.muted);
        Put(p, value, ShelfRight, fy, Paint::Fit(Capitals(fact.second), value, ShelfRight - WizardSide - 150), l.ink, 1);
        fy += 30;
    }
    p.fillRect(QRectF(WizardSide, fy - 19, ShelfRight - WizardSide, 1), l.rule);
    Wrapped(p, body, QRectF(WizardSide, fy - 4, ShelfRight - WizardSide, key.top() - fy - 8), note, l.muted);
    p.restore();

    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    PutFoot(p, tiny, w.welcome_step == 2 ? QStringLiteral("↑ ↓ CHOOSE   ENTER NEXT   ESC FINISH") : QStringLiteral("↑ ↓ CHOOSE   ENTER NEXT   ESC SKIP"));
}
