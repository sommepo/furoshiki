// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Included by home_window.cpp: the settings' Dictionaries page (the maintainer, 2026-11-23: "the
// dictionary importer and enabler should feel native to the app, not a separate popup ... inside
// a separate dictionaries area of settings"), then for a hundred and more ("a hybrid approach
// between something winui feeling and furoshiki feeling which allows for powerful mass edits"):
// the page's place for rows is a list of its own (dictionary_panel.h), placed over it as the page
// is drawn and handed the page's keys. The work is DictionaryShelf's.

void HomeWindow::SetDictionaries(DictionaryShelf* shelf) {
    dictionaries = shelf;
    delete dictionary_panel;
    delete conversion_strip;
    delete conversion_box;
    if (dictionaries != nullptr) {
        // While the old dictionary file is moved into the engine, a strip at the program window's
        // foot, whatever it shows (dressed and started as the page is first drawn), and first a
        // box over the window that says what is happening (the maintainer, 2026-11-24: "it needs
        // to be a prompt so they are aware it's happening, with a progress bar"); the strip
        // brings the box back.
        conversion_strip = new DictionaryConversionStrip(window(), dictionaries);
        conversion_box = new DictionaryConversionBox(window(), dictionaries);
        conversion_box->may_show = [this] { return running.isEmpty() && !playing_game; };
        conversion_box->shown_changed = [this](bool shown) {
            if (conversion_strip) {
                conversion_strip->SetSuppressed(shown);
            }
        };
        conversion_box->import_ask = [this] {
            ShowDictionaries();
            DictionaryImportAsk();
        };
        conversion_strip->clicked = [this] {
            if (conversion_box) {
                conversion_box->Reopen();
            }
        };
        dictionary_panel = new DictionaryPanel(this, dictionaries);
        dictionary_panel->hide();
        dictionary_panel->ask = [this](const QString& title, const QStringList& said, int now, std::function<void(int)> take) {
            OpenChoices(title, said, now, std::move(take));
        };
        dictionary_panel->sound = [this](int which) {
            static constexpr Sound sounds_of[] = {Sound::Move, Sound::Select, Sound::Back, Sound::Refuse};
            sounds.Play(sounds_of[std::clamp(which, 0, 3)]);
        };
        dictionary_panel->import_ask = [this] { DictionaryImportAsk(); };
        dictionary_panel->entered = [this] {
            if (!pages_rows) {
                pages_rows = true;
                update();
            }
        };
        dictionary_panel->say = [this](const QString& text) { Say(text); };
        dictionaries->changed = [this] {
            if (dictionary_panel) {
                dictionary_panel->Reload();
            }
            update();
        };
        dictionaries->say = [this](const QString& text) { Say(text); };
    }
    Changed();
}

void HomeWindow::ShowDictionaries() {
    if (!shelf_pages) {
        OpenPages();
    }
    const QList<Item> items = Items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::Dictionaries) {
            axis_choice = i;
            pages_rows = true;
        }
    }
    if (dictionaries != nullptr) {
        dictionaries->Refresh(); // a names dictionary a game installed meanwhile
    }
    update();
}

namespace {

/// A console's name as the library names it (what Art::VndbPlatform reads), by its key.
QString LibrarySystemOf(const QString& key) {
    static const QHash<QString, QString> names{
        {QStringLiteral("ps1"), QStringLiteral("PlayStation")},          {QStringLiteral("ps2"), QStringLiteral("PlayStation 2")},
        {QStringLiteral("psp"), QStringLiteral("PlayStation Portable")}, {QStringLiteral("psvita"), QStringLiteral("PlayStation Vita")},
        {QStringLiteral("dc"), QStringLiteral("Dreamcast")},             {QStringLiteral("nds"), QStringLiteral("Nintendo DS")},
        {QStringLiteral("3ds"), QStringLiteral("Nintendo 3DS")},         {QStringLiteral("switch"), QStringLiteral("Nintendo Switch")},
        {QStringLiteral("lines"), QStringLiteral("PC")},
    };
    return names.value(key);
}

} // namespace

void HomeWindow::SetScripts(ScriptShelf* shelf) {
    scripts = shelf;
    delete scripts_panel;
    if (scripts == nullptr) {
        Changed();
        return;
    }
    scripts_panel = new ScriptsPanel(this, scripts);
    scripts_panel->hide();
    scripts_panel->ask = [this](const QString& title, const QStringList& said, int now, std::function<void(int)> take) {
        OpenChoices(title, said, now, std::move(take));
    };
    scripts_panel->sound = [this](int which) {
        static constexpr Sound sounds_of[] = {Sound::Move, Sound::Select, Sound::Back, Sound::Refuse};
        sounds.Play(sounds_of[std::clamp(which, 0, 3)]);
    };
    scripts_panel->entered = [this] {
        if (!pages_rows) {
            pages_rows = true;
            update();
        }
    };
    scripts_panel->say = [this](const QString& text) { Say(text); };
    scripts_panel->files_ask = [this] {
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            Say(QStringLiteral("Couldn't open the file chooser: there is no display"));
            return;
        }
        QTimer::singleShot(0, this, [this] {
            const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Install game scripts"), QDir::homePath(),
                                                                    QStringLiteral("Game scripts (*.lua);;All files (*)"));
            if (!files.isEmpty()) {
                scripts->InstallFiles(files);
            }
        });
    };
    // Box art: what the build has (tools/titles.py covers and spines) or was found here
    // (program/art), as the shelf draws it.
    scripts_panel->art = [this](const QString& id) {
        const TitleRecord record = RecordOf(id);
        const QString platform = ArtPlatform(id);
        const bool cover = (!record.key.isEmpty() && !platform.isEmpty() && QFileInfo::exists(QStringLiteral(":/covers/%1.%2.jpg").arg(record.key, platform))) ||
                           QFileInfo::exists(Art::CoverFile(SharedFolder(), id));
        const bool spine = !SpineOf(record.key, id).isNull() || QFileInfo::exists(Art::SpineFile(SharedFolder(), id));
        return std::make_pair(cover, spine);
    };
    scripts_panel->english = [](const QString& id) { return RecordOf(id).english; };
    scripts_panel->fetch_art = [this](const QList<ScriptGame>& games) {
        if (script_art_queue.isEmpty() && (script_art == nullptr || !script_art->Busy())) {
            script_art_done = 0;
            script_art_total = 0;
            script_art_found = 0;
        }
        QSet<QString> pending;
        for (const ScriptGame& q : script_art_queue) {
            pending.insert(q.id);
        }
        if (script_art != nullptr && script_art->Busy()) {
            pending.insert(script_art->GameId());
        }
        for (const ScriptGame& game : games) {
            if (!pending.contains(game.id)) {
                pending.insert(game.id);
                script_art_queue.append(game);
                ++script_art_total;
            }
        }
        ScriptArtNext();
        update();
    };
    scripts_panel->cancel_art = [this] {
        script_art_queue.clear();
        Say(QStringLiteral("Cancelled remaining box art lookups"));
        if (scripts_panel) {
            scripts_panel->update();
        }
        update();
    };
    scripts_panel->art_work = [this] { return std::make_pair(script_art_done, script_art_queue.isEmpty() && (script_art == nullptr || !script_art->Busy()) ? 0 : script_art_total); };
    scripts->library = [this] {
        // The shelf's games, with the console the program plays them as.
        QList<ScriptLibraryGame> out;
        for (const Game& game : library.games) {
            const QString key = game.app.startsWith(QStringLiteral("reader-home-")) ? game.app.mid(12) : QString();
            out.append({game.id, ShelfName(game.title), key});
        }
        return out;
    };
    scripts->changed = [this] {
        if (scripts_panel) {
            scripts_panel->Reload();
        }
        update();
    };
    scripts->say = [this](const QString& text) { Say(text); };
    Changed();
}

void HomeWindow::ScriptArtNext() {
    if (script_art == nullptr) {
        script_art = new Art::Finder(this);
        connect(script_art, &Art::Finder::Done, this, [this](const QString& id, bool found, const QString&) {
            ++script_art_done;
            script_art_found += found ? 1 : 0;
            covers.remove(Art::CoverFile(SharedFolder(), id));
            covers.remove(Art::SpineFile(SharedFolder(), id));
            shelf_tints.clear();
            if (scripts_panel) {
                scripts_panel->RefreshArt();
            }
            if (script_art_queue.isEmpty()) {
                if (script_art_total == 1) {
                    Say(script_art_found == 1 ? QStringLiteral("Box art found") : QStringLiteral("No box art found"));
                } else {
                    Say(QStringLiteral("Found box art for %1 of %2 games").arg(script_art_found).arg(script_art_total));
                }
            } else {
                QTimer::singleShot(0, this, [this] { ScriptArtNext(); });
            }
            update();
        });
    }
    while (!script_art->Busy() && !script_art_queue.isEmpty()) {
        const ScriptGame game = script_art_queue.takeFirst();
        Art::Want want;
        want.game_id = game.id;
        want.title = ShelfName(game.title);
        want.english = RecordOf(game.id).english;
        want.system = LibrarySystemOf(game.console);
        want.vndb = RecordOf(game.id).vndb;
        if (!script_art->Start(want, SharedFolder())) {
            ++script_art_done;
        }
    }
}

void HomeWindow::ShowScripts(const QString& console) {
    if (!shelf_pages) {
        OpenPages();
    }
    const QList<Item> items = Items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::Scripts) {
            axis_choice = i;
            pages_rows = true;
        }
    }
    if (scripts_panel) {
        scripts_panel->ChooseConsole(console);
    }
    update();
}

bool HomeWindow::ScriptsShown() const {
    if (!shelf_on || !shelf_pages || scripts == nullptr || !find_text.isNull()) {
        return false;
    }
    const QList<Item> items = Items();
    return axis_choice >= 0 && axis_choice < items.size() && items[axis_choice].kind == Item::Scripts;
}

bool HomeWindow::DictionariesShown() const {
    if (!shelf_on || !shelf_pages || dictionaries == nullptr || !find_text.isNull()) {
        return false;
    }
    const QList<Item> items = Items();
    return axis_choice >= 0 && axis_choice < items.size() && items[axis_choice].kind == Item::Dictionaries;
}

void HomeWindow::PlaceDictionaries(bool there) {
    // Over the page's place for rows, once the page is at rest; dressed in the page's look.
    if (!dictionary_panel && !scripts_panel) {
        return;
    }
    if (shelf_look) {
        // Both dressed in the page's look, again when the theme or the window's size changes.
        const ShelfLook& l = *shelf_look;
        const double scale = std::min(width() / SheetW, height() / SheetH);
        const QString dressed = QStringLiteral("%1|%2").arg(l.key).arg(scale);
        if (dressed != dictionary_style) {
            dictionary_style = dressed;
            DictionaryPanel::Style style;
            style.ground = l.ground;
            style.raised = l.raised;
            style.ink = l.ink;
            style.muted = l.muted;
            style.accent = l.accent;
            style.rule = l.rule.isValid() && l.rule.alpha() > 0 ? l.rule : l.muted;
            style.row = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 13.5);
            style.mono = ShelfFont(l.mono, 10.5, 0.12);
            style.mono_small = ShelfFont(l.mono, 10, 0.08);
            style.head = ShelfFont(l.mono, 9, 0.14);
            style.scale = scale;
            if (dictionary_panel) {
                dictionary_panel->SetStyle(style);
            }
            if (scripts_panel) {
                scripts_panel->SetStyle(style);
            }
            if (conversion_strip) {
                conversion_strip->SetStyle(style);
            }
            if (conversion_box) {
                conversion_box->SetStyle(style);
            }
        }
        if (conversion_strip) {
            conversion_strip->Watch();
        }
        if (conversion_box) {
            conversion_box->Watch();
        }
    }
    {
        // The Game scripts page's list, as the Dictionaries one below.
        const bool scripts_there = there && scripts_panel && ScriptsShown() && pages_at > 0.98 && !wizard_on;
        if (scripts_panel && !scripts_there && scripts_panel->isVisible()) {
            if (scripts_panel->isAncestorOf(QApplication::focusWidget())) {
                setFocus();
            }
            scripts_panel->hide();
        } else if (scripts_there) {
            const double scale = std::min(width() / SheetW, height() / SheetH);
            const QRectF area(440, 92, ShelfRight + 12 - 440, 552);
            const QRect place = QRectF((width() - SheetW * scale) / 2 + area.x() * scale, (height() - SheetH * scale) / 2 + area.y() * scale,
                                       area.width() * scale, area.height() * scale)
                                    .toRect();
            if (scripts_panel->geometry() != place) {
                scripts_panel->setGeometry(place);
            }
            scripts_panel->SetActive(pages_rows);
            if (!scripts_panel->isVisible()) {
                scripts_panel->Reload();
                scripts_panel->show();
            }
        }
    }
    if (!dictionary_panel) {
        return;
    }
    there = there && DictionariesShown() && pages_at > 0.98 && !wizard_on;
    if (!there) {
        if (dictionary_panel->isVisible()) {
            if (dictionary_panel->isAncestorOf(QApplication::focusWidget())) {
                setFocus();
            }
            dictionary_panel->hide();
        }
        return;
    }
    const double scale = std::min(width() / SheetW, height() / SheetH);
    const QRectF area(440, 92, ShelfRight + 12 - 440, 552);
    const QRect place = QRectF((width() - SheetW * scale) / 2 + area.x() * scale, (height() - SheetH * scale) / 2 + area.y() * scale,
                               area.width() * scale, area.height() * scale)
                            .toRect();
    if (dictionary_panel->geometry() != place) {
        dictionary_panel->setGeometry(place);
    }
    dictionary_panel->SetActive(pages_rows);
    if (!dictionary_panel->isVisible()) {
        dictionary_panel->Reload();
        dictionary_panel->show();
    }
}

void HomeWindow::DictionaryImportAsk() {
    // One action for both: a file picker takes files or a folder, never either.
    OpenChoices(QStringLiteral("IMPORT DICTIONARIES"),
                {QStringLiteral("Files"), QStringLiteral("Folders")}, -1,
                [this](int index) { QTimer::singleShot(0, this, [this, index] { DictionaryImportFrom(index); }); });
}

void HomeWindow::DictionaryImportFrom(int index) {
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        Say(QStringLiteral("Couldn't open the file chooser: there is no display"));
        return;
    }
    QSettings settings;
    const QString last = settings.value(QStringLiteral("dictionaries/last_folder"), QDir::homePath()).toString();
    QStringList chosen;
    if (index == 0) {
        chosen = QFileDialog::getOpenFileNames(this, QStringLiteral("Import dictionaries"), last,
                                               QStringLiteral("Yomitan dictionaries (*.zip);;All files (*)"));
    } else {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Import a folder of dictionaries"), last);
        if (!folder.isEmpty()) {
            chosen.append(folder);
        }
    }
    if (chosen.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("dictionaries/last_folder"),
                      index == 0 ? QFileInfo(chosen.first()).absolutePath() : chosen.first());
    dictionaries->Import(chosen);
}


void HomeWindow::ScreenScraperAccount() {
    // ScreenScraper asks every program for a developer ID besides the user's own account; the
    // user types both (nothing of ScreenScraper's is built in). Kept in the program's settings.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        Say(QStringLiteral("Couldn't open the account window: there is no display"));
        return;
    }
    Art::Sources sources = Art::LoadSources();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("ScreenScraper account"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* about = new QLabel(QStringLiteral("Fetch box art and spines from screenscraper.fr. Enter your ScreenScraper "
                                            "account along with a ScreenScraper developer ID and password (available on "
                                            "request from ScreenScraper)."));
    about->setWordWrap(true);
    layout->addWidget(about);
    auto* form = new QFormLayout;
    auto* user = new QLineEdit(sources.ss_user);
    auto* password = new QLineEdit(sources.ss_password);
    password->setEchoMode(QLineEdit::Password);
    auto* dev_id = new QLineEdit(sources.ss_dev_id);
    auto* dev_password = new QLineEdit(sources.ss_dev_password);
    dev_password->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Username"), user);
    form->addRow(QStringLiteral("Password"), password);
    form->addRow(QStringLiteral("Developer ID"), dev_id);
    form->addRow(QStringLiteral("Developer password"), dev_password);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(460, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    sources.ss_user = user->text().trimmed();
    sources.ss_password = password->text();
    sources.ss_dev_id = dev_id->text().trimmed();
    sources.ss_dev_password = dev_password->text();
    if (!sources.ss_dev_id.isEmpty() && !sources.ss_dev_password.isEmpty()) {
        sources.screenscraper = true;
    } else if (sources.ss_dev_id.isEmpty() && sources.ss_dev_password.isEmpty() && sources.ss_user.isEmpty()) {
        sources.screenscraper = false;
    }
    Art::SaveSources(sources);
    Say(sources.ScreenScraperReady() ? QStringLiteral("ScreenScraper enabled for box art") : QStringLiteral("ScreenScraper account saved"));
    Changed();
}
