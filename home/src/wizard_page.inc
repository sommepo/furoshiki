// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Adding a title (docs/furoshiki-3.md, 8; the maintainer, 2026-11-10: "i havent seen anything
// like a setup wizard yet for hooks/ocr"). From the shelf's last spine, or the pages' Settings:
// three doors side by side, A a disc or file (a console's folder of games), B a game's window
// on this computer (read by OCR), C a text hook (lines by WebSocket, or the clipboard); then
// what that door needs (B: which window, and where in its picture the text is, with a test
// read), then its name, matched exactly to a title record; then it stands on the shelf. Drawn
// as the shelf is: type and hairlines, one filled key a screen. Every key a controller has
// works it (the d-pad, A, B, the bumpers); the pointer chooses and draws the area; only a
// name no record has and the helper's address want a keyboard.
// Part of home_window.cpp, after shelf_page.inc, inside namespace Home.

struct HomeWindow::Wizard {
    enum Step { Doors, Console, Hook, Helper, Window, Area, Name, Script, Program, Thread, Match };
    Step step{Doors};
    int door{};
    bool helper_only{}; ///< Opened from the Settings' row for the helper alone
    int row{};
    // The helper (B): its three settings, and what it said when asked.
    Lines::Helper helper;
    int field{};
    QString helper_said;
    bool helper_ok{}, helper_asking{}, helper_then_on{};
    // A text hook (C): where a WebSocket's lines come from.
    QString address;
    // The windows there are (B), each with a small picture.
    QList<Windows::Entry> windows;
    QList<QImage> thumbs;
    QList<Hooks::Program> window_programs; ///< The program each window is of (none: read by OCR only)
    qint64 listed_at{-100000};
    Windows::Entry window;
    Hooks::Program game; ///< The chosen window's program, which a hook goes into
    bool by_ocr{};       ///< The chosen window is read by OCR (else by a hook)
    // The chosen window's picture and the text's area in it.
    QImage picture;
    qint64 pictured_at{-100000};
    /// The boxes of it that are read: the first the text's own, others read with it as one
    /// line or only when asked for; `box` is the one in hand.
    QList<Lines::Box> boxes{{QRectF(0.08, 0.66, 0.84, 0.28), false}};
    int box{};
    QRectF& Rect() {
        return boxes[std::clamp(box, 0, static_cast<int>(boxes.size()) - 1)].rect;
    }
    bool sizing{};
    QRectF picture_box; ///< Where the picture was drawn on the sheet
    bool drawing{};
    QPointF draw_from;
    QString read;
    bool read_ok{}, read_waiting{}, read_stale{true};
    qint64 area_at{};
    qint64 read_at{}; ///< When the helper was last asked to read the area
    // Its name.
    QString name;
    QString typed; ///< What was typed, which the records below are sifted by
    int suggestion{-1};
    OcrClient client;
    // A text hook (C): its engine, script, program and threads (wizard_hooks.inc).
    std::shared_ptr<HookSteps> hook;
};

namespace {

struct NamedRecord {
    QString key, title, english;
};

/// Every title record, by its English name.
const QList<NamedRecord>& AllRecords() {
    static QList<NamedRecord> all;
    static bool read = false;
    if (!read) {
        read = true;
        const QDir folder(QStringLiteral(":/titles"));
        for (const QString& file : folder.entryList({QStringLiteral("*.json")}, QDir::Files)) {
            QFile data(folder.filePath(file));
            if (data.open(QIODevice::ReadOnly)) {
                const QJsonObject record = QJsonDocument::fromJson(data.readAll()).object();
                all.append({QFileInfo(file).completeBaseName(), record.value(QStringLiteral("title")).toString(),
                            record.value(QStringLiteral("english")).toString()});
            }
        }
        std::sort(all.begin(), all.end(), [](const NamedRecord& a, const NamedRecord& b) {
            return (a.english.isEmpty() ? a.title : a.english).compare(b.english.isEmpty() ? b.title : b.english, Qt::CaseInsensitive) < 0;
        });
    }
    return all;
}

/// The records whose name has `typed` in it (all of them for nothing typed).
QList<NamedRecord> RecordsLike(const QString& typed) {
    const QString want = typed.trimmed();
    QList<NamedRecord> found;
    for (const NamedRecord& record : AllRecords()) {
        if (want.isEmpty() || record.english.contains(want, Qt::CaseInsensitive) || record.title.contains(want, Qt::CaseInsensitive)) {
            found.append(record);
        }
    }
    return found;
}

const char* const DoorLetters[] = {"A", "B", "C"};
const char* const DoorNames[] = {"A DISC OR FILE", "A GAME WINDOW", "LINES I SEND"};

constexpr double WizardTop = 196;     // under the step's question
constexpr double WizardSide = 850;    // the right column: what is known, and the key
constexpr int WindowColumns = 4, WindowRows = 2;

/// A line to type on: the text (or what to type, muted), a caret after it while it is the one
/// in hand, a rule under it.
template <class LookT> void Field(QPainter& p, const LookT& l, const QFont& face, double x, double baseline, double width, const QString& text,
           const QString& otherwise, bool on) {
    const QFontMetricsF metrics(face);
    const QString shown = metrics.elidedText(text, Qt::ElideLeft, width - 12);
    double at = x;
    if (text.isEmpty()) {
        Put(p, face, x, baseline, metrics.elidedText(otherwise, Qt::ElideRight, width - 12), Faded(l.muted, 0.7));
    } else {
        at += Put(p, face, x, baseline, shown, l.ink);
    }
    if (on) {
        p.fillRect(QRectF(at + 3, baseline - metrics.capHeight() - 3, 2, metrics.capHeight() + 7), l.accent);
    }
    p.fillRect(QRectF(x, baseline + metrics.descent() + 5, width, 1), on ? l.ink : l.rule);
}

/// Text that may take several lines; returns how tall it stood.
double Wrapped(QPainter& p, const QFont& face, const QRectF& box, const QString& text, const QColor& colour) {
    p.setFont(face);
    p.setPen(colour);
    QRectF used;
    p.drawText(box, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text, &used);
    return used.height();
}

} // namespace

void HomeWindow::OpenWizard(bool helper_only) {
    auto made = std::make_shared<Wizard>();
    const Lines::Sources sources = Lines::Load(SharedFolder());
    made->helper = sources.ocr;
    if (made->helper.address.isEmpty()) {
        // A helper installed on this computer (ocr_helper.h) needs no typing: its own file says
        // where it answers, its source and its token, and it is started when a window is read.
        if (const LocalHelper local = FindLocalHelper(QCoreApplication::applicationDirPath()); local.Valid()) {
            made->helper = SettingOf(local);
            Lines::Sources with = sources;
            with.ocr = made->helper;
            if (Lines::Save(SharedFolder(), with)) {
                Changed();
                Say(QStringLiteral("The OCR helper installed in %1 is used").arg(QDir::toNativeSeparators(local.folder)));
            }
        }
    }
    made->address = sources.address;
    made->helper_only = helper_only;
    made->step = helper_only ? Wizard::Helper : Wizard::Doors;
    made->door = helper_only ? 1 : 0;
    made->client.Set(made->helper);
    Wizard* w = made.get();
    connect(&w->client, &OcrClient::Answered, this, [this, w](bool ok, const QString& what) {
        if (wizard.get() != w) {
            return;
        }
        w->helper_asking = false;
        w->helper_ok = ok;
        w->helper_said = ok ? QStringLiteral("ANSWERS  ·  %1").arg(what.toUpper()) : what.toUpper();
        if (w->step == Wizard::Helper && ok && w->helper_then_on) {
            sounds.Play(Sound::Select);
            if (w->helper_only) {
                CloseWizard();
                Say(QStringLiteral("The OCR helper answers at %1").arg(w->helper.address));
                return;
            }
            WizardGo(w->window.title.isEmpty() ? Wizard::Window : Wizard::Area);
        }
        w->helper_then_on = false;
        update();
    });
    connect(&w->client, &OcrClient::Chosen, this, [this, w](bool ok, const QString& what) {
        if (wizard.get() == w && !ok) {
            w->read_ok = false;
            w->read_waiting = false;
            w->read = what;
            update();
        }
    });
    connect(&w->client, &OcrClient::Tested, this, [this, w](bool ok, const QString& text) {
        if (wizard.get() == w) {
            w->read_waiting = false;
            w->read_ok = ok;
            w->read = text;
            update();
        }
    });
    own_picture = grab().toImage();
    wizard = made;
    wizard_on = true;
    sounds.Play(Sound::Select);
    update();
}

void HomeWindow::CloseWizard() {
    if (!wizard_on) {
        return;
    }
    wizard_on = false;
    if (wizard) {
        wizard->client.Unfollow();
    }
    HookClose();
    setCursor(Qt::ArrowCursor);
    update();
}

void HomeWindow::WizardGo(int to) {
    Wizard& w = *wizard;
    const qint64 now = clock.elapsed();
    w.step = static_cast<Wizard::Step>(to);
    switch (w.step) {
    case Wizard::Window:
        w.listed_at = -100000;
        w.helper_asking = true;
        w.client.Set(w.helper);
        w.client.Ask();
        break;
    case Wizard::Area:
        w.picture = Windows::Grab(w.window);
        w.pictured_at = -100000;
        w.read.clear();
        w.read_ok = false;
        w.read_waiting = false;
        w.read_stale = true;
        w.area_at = now;
        w.client.Choose(); // this device's pictures are the ones read there now
        break;
    case Wizard::Name:
        if (w.name.isEmpty() && w.door == 1) {
            w.name = w.window.title;
        }
        w.typed = w.name;
        w.suggestion = -1;
        break;
    case Wizard::Helper:
        w.field = w.helper.address.isEmpty() ? 0 : w.helper.token.isEmpty() ? 2 : 0;
        break;
    default:
        w.row = 0;
        break;
    }
    update();
}

void HomeWindow::WizardStep() {
    if (!wizard_on || !wizard) {
        return;
    }
    Wizard& w = *wizard;
    const qint64 now = clock.elapsed();
    if (w.step == Wizard::Window && now - w.listed_at > 800) {
        // The windows as they are now: one may have opened, and a game's picture moves.
        w.listed_at = now;
        const QString chosen = w.row < w.windows.size() ? w.windows[w.row].title : QString();
        w.windows = Windows::List();
        // Each window with the program it is of, which a hook goes into; and the programs with a
        // window of their own no picture of is had (another desktop's, a test's stand-ins).
        const QList<Hooks::Program> programs = Hooks::Programs();
        w.window_programs.clear();
        for (const Windows::Entry& window : w.windows) {
            w.window_programs.append(Hooks::ProgramOfWindow(programs, window.handle, window.title));
        }
        for (const Hooks::Program& program : programs) {
            const bool shown = std::any_of(w.window_programs.begin(), w.window_programs.end(), [&](const Hooks::Program& one) { return one.pid == program.pid && !one.name.isEmpty(); });
            if (!program.window.isEmpty() && !shown) {
                w.windows.append({program.window, 0, QString()});
                w.window_programs.append(program);
            }
        }
        w.thumbs.clear();
        for (int i = 0; i < w.windows.size(); ++i) {
            const QImage picture = w.windows[i].handle == 0 && w.windows[i].file.isEmpty() ? QImage() : Windows::Grab(w.windows[i]);
            w.thumbs.append(picture.isNull() ? picture : picture.scaled(640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            if (!chosen.isEmpty() && w.windows[i].title == chosen) {
                w.row = i;
            }
        }
        w.row = std::clamp(w.row, 0, std::max(0, static_cast<int>(w.windows.size()) - 1));
        update();
    }
    if (w.step == Wizard::Area) {
        if (now - w.pictured_at > 250) {
            // Four pictures a second, as a title read from this window sends them.
            w.pictured_at = now;
            const Windows::Entry still = Windows::Find(w.window.title, w.window.handle);
            const QImage picture = still.title.isEmpty() ? QImage() : Windows::Grab(still);
            if (!picture.isNull()) {
                w.picture = picture;
                w.client.Push(picture);
                update();
            }
        }
        // The area stood still a moment and a picture is there: what the helper reads in it.
        // And again every two seconds, so that what stands there is what the game shows now.
        if (!w.read_waiting && now - w.read_at > 2000) {
            w.read_stale = true;
        }
        if (w.read_stale && !w.read_waiting && !w.drawing && now - w.area_at > 600 && w.client.Taken() > 0) {
            w.read_stale = false;
            w.read_waiting = true;
            w.read_at = now;
            w.client.Test(w.Rect());
            update();
        }
    }
    HookStep();
}

bool HomeWindow::WizardText(int key, const QString& text) {
    if (!wizard_on || !wizard) {
        return false;
    }
    Wizard& w = *wizard;
    if (HookStepOn()) {
        return HookText(key, text);
    }
    QString* into = nullptr;
    if (w.step == Wizard::Name) {
        into = &w.typed;
    } else if (w.step == Wizard::Helper) {
        into = w.field == 0 ? &w.helper.address : w.field == 1 ? &w.helper.source : &w.helper.token;
    }
    if (into == nullptr) {
        return false;
    }
    if (key == Qt::Key_Backspace) {
        if (w.step == Wizard::Name && w.suggestion >= 0) {
            w.typed = w.name; // the record's name, now changed by hand
        }
        into->chop(1);
    } else if (!text.isEmpty() && text.at(0).isPrint() && key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Tab) {
        if (w.step == Wizard::Name && w.suggestion >= 0) {
            w.typed = w.name;
        }
        // A pasted address or token comes in one piece, without its line break.
        into->append(QString(text).remove(QLatin1Char('\n')).remove(QLatin1Char('\r')));
    } else {
        return false;
    }
    if (w.step == Wizard::Name) {
        w.name = w.typed;
        w.suggestion = -1;
    }
    if (w.step == Wizard::Helper) {
        w.helper_said.clear();
        w.helper_ok = false;
    }
    update();
    return true;
}

void HomeWindow::WizardAccept() {
    Wizard& w = *wizard;
    if (HookStepOn()) {
        HookAccept();
        return;
    }
    switch (w.step) {
    case Wizard::Doors:
        if (w.door == 0) {
            if (consoles.isEmpty()) {
                sounds.Play(Sound::Refuse);
                Say(QStringLiteral("This build of Furoshiki plays no console itself: it has no cores"));
                return;
            }
            WizardGo(Wizard::Console);
        } else if (w.door == 1) {
            w.window = {};
            w.game = {};
            WizardGo(Wizard::Window);
        } else {
            HookOpen();
            HookGo(Wizard::Hook);
        }
        sounds.Play(Sound::Select);
        return;
    case Wizard::Console: {
        // The Consoles page's own picker: a folder of that console's games.
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            Say(QStringLiteral("Without a display no folder can be picked"));
            return;
        }
        console_layer = std::clamp(w.row, 0, static_cast<int>(consoles.size()) - 1) + 1;
        console_game = 0;
        const QString console = LayerConsole();
        const int before = play != nullptr ? static_cast<int>(play->Folders(console).size()) : 0;
        ConsoleHeadAction(2, false);
        if (play != nullptr && play->Folders(console).size() > before) {
            const QString name = consoles[console_layer - 1].name;
            CloseWizard();
            SetPages(false);
            Say(QStringLiteral("%1's games in %2 stand on the shelf as they are found").arg(name, play->Folders(console).last()));
        }
        return;
    }
    case Wizard::Helper: {
        const QString address = Lines::CleanHelperAddress(w.helper.address);
        if (address.isEmpty()) {
            sounds.Play(Sound::Refuse);
            w.helper_said = QStringLiteral("AN ADDRESS IS HTTP://HOST:PORT");
            w.field = 0;
            update();
            return;
        }
        // Another address than the installed helper's own: it is someone else's to run.
        if (!w.helper.program.isEmpty() && Refreshed(w.helper).address != address) {
            w.helper.program.clear();
        }
        w.helper.address = address;
        w.helper.source = w.helper.source.trimmed().isEmpty() ? QStringLiteral("furoshiki") : w.helper.source.trimmed();
        w.helper.token = w.helper.token.trimmed();
        Lines::Sources sources = Lines::Load(SharedFolder());
        sources.ocr = w.helper;
        if (!Lines::Save(SharedFolder(), sources)) {
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("The helper's settings could not be written"));
            return;
        }
        w.client.Set(w.helper);
        w.helper_asking = true;
        w.helper_then_on = true;
        w.helper_said.clear();
        w.client.Ask();
        Changed();
        return;
    }
    case Wizard::Window:
        if (w.row >= w.windows.size()) {
            sounds.Play(Sound::Refuse);
            return;
        }
        w.window = w.windows[w.row];
        w.game = w.window_programs.value(w.row);
        sounds.Play(Sound::Select);
        // A hook for its program first, else OCR; a window no program is known of is read by OCR.
        if (w.game.name.isEmpty()) {
            WizardOcr();
        } else {
            HookMatch();
        }
        return;
    case Wizard::Area:
        if (!w.read_ok) {
            sounds.Play(Sound::Refuse);
            Say(w.read_waiting || w.read.isEmpty() ? QStringLiteral("The area is used once the helper has read it") : QStringLiteral("The helper could not read it: %1").arg(w.read));
            return;
        }
        sounds.Play(Sound::Select);
        WizardGo(Wizard::Name);
        return;
    case Wizard::Name: {
        const QString name = w.name.trimmed();
        if (name.isEmpty()) {
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("A title wants a name"));
            return;
        }
        const QString id = w.door == 1 && w.by_ocr ? AddWindowTitle(name, w.window.title, w.boxes[0].rect, w.picture, w.boxes.mid(1))
                                                   : HookTitle(name);
        if (!id.isEmpty()) {
            shelf_want = id;
            shelf_section = 0;
            shelf_stale = true;
            shelf_stage = shelf_log = false;
            CloseWizard();
            SetPages(false);
        }
        return;
    }
    default:
        return; // a text hook's steps: wizard_hooks.inc
    }
}

void HomeWindow::WizardBack() {
    Wizard& w = *wizard;
    if (HookStepOn() || (w.step == Wizard::Name && (w.door == 2 || (w.door == 1 && !w.by_ocr)))) {
        HookBack();
        return;
    }
    sounds.Play(Sound::Back);
    switch (w.step) {
    case Wizard::Doors:
        CloseWizard();
        return;
    case Wizard::Helper:
        if (w.helper_only) {
            CloseWizard();
            return;
        }
        if (!w.window.title.isEmpty()) {
            // Back to where OCR was chosen: the window's ways, or the windows.
            if (w.game.name.isEmpty()) {
                WizardGo(Wizard::Window);
            } else {
                HookMatch();
            }
            return;
        }
        [[fallthrough]];
    case Wizard::Console:
    case Wizard::Window:
        w.step = Wizard::Doors;
        break;
    case Wizard::Area:
        if (w.game.name.isEmpty()) {
            WizardGo(Wizard::Window);
        } else {
            HookMatch();
        }
        return;
    case Wizard::Name:
        if (w.door == 1) {
            w.step = Wizard::Area;
            w.read_stale = true;
            w.area_at = clock.elapsed();
        }
        break;
    default:
        break;
    }
    update();
}

void HomeWindow::WizardChooseWindow(const QString& words) {
    if (!wizard_on || !wizard) {
        return;
    }
    for (int i = 0; i < wizard->windows.size(); ++i) {
        if (wizard->windows[i].title.contains(words)) {
            wizard->row = i;
            update();
            return;
        }
    }
}

void HomeWindow::WizardOcr() {
    Wizard& w = *wizard;
    w.by_ocr = true;
    HookClose();
    WizardGo(w.client.Configured() ? Wizard::Area : Wizard::Helper);
}

void HomeWindow::WizardArea(const QRectF& area) {
    Wizard& w = *wizard;
    const QRectF clean = Lines::CleanArea(area);
    if (!clean.isValid() || clean == w.Rect()) {
        return;
    }
    w.Rect() = clean;
    w.area_at = clock.elapsed();
    w.read_stale = true;
    w.read_ok = false;
    w.read.clear();
    update();
}

bool HomeWindow::WizardKey(int key) {
    Wizard& w = *wizard;
    if (key == Qt::Key_F4) {
        return false; // the theme, as everywhere on this page
    }
    if (HookStepOn()) {
        return HookKey(key);
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        WizardAccept();
        return true;
    }
    const bool types = w.step == Wizard::Name || w.step == Wizard::Helper;
    if (key == Qt::Key_Escape || (key == Qt::Key_Backspace && !types)) {
        WizardBack();
        return true;
    }
    if (key == Qt::Key_Backspace) {
        WizardText(key, QString());
        return true;
    }
    const auto move = [&](int& value, int by, int count) {
        const int next = std::clamp(value + by, 0, std::max(0, count - 1));
        if (next != value) {
            value = next;
            sounds.Play(Sound::Move);
            update();
        }
    };
    const int by = key == Qt::Key_Left || key == Qt::Key_Up ? -1 : 1;
    const bool across = key == Qt::Key_Left || key == Qt::Key_Right, along = key == Qt::Key_Up || key == Qt::Key_Down;
    switch (w.step) {
    case Wizard::Doors:
        if (across) {
            move(w.door, by, 3);
        }
        break;
    case Wizard::Console:
        if (along) {
            move(w.row, by, static_cast<int>(consoles.size()));
        }
        break;
    case Wizard::Helper:
        if (along) {
            move(w.field, by, 3);
        }
        break;
    case Wizard::Window:
        if (across) {
            move(w.row, by, static_cast<int>(w.windows.size()));
        } else if (along) {
            move(w.row, by * WindowColumns, static_cast<int>(w.windows.size()));
        }
        break;
    case Wizard::Area:
        if (across || along) {
            // A hundredth of the picture a press: its place, or (L and R change which) its size.
            const double dx = across ? by * 0.01 : 0, dy = along ? by * 0.01 : 0;
            QRectF area = w.Rect();
            if (w.sizing) {
                area.setWidth(std::clamp(area.width() + dx, 0.02, 1 - area.x()));
                area.setHeight(std::clamp(area.height() + dy, 0.02, 1 - area.y()));
            } else {
                area.moveTo(std::clamp(area.x() + dx, 0.0, 1 - area.width()), std::clamp(area.y() + dy, 0.0, 1 - area.height()));
            }
            if (area != w.Rect()) {
                sounds.Play(Sound::Move);
                WizardArea(area);
            }
        } else if (key == Qt::Key_Q || key == Qt::Key_E || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            w.sizing = !w.sizing;
            sounds.Play(Sound::Tab);
            update();
        } else if (key == Qt::Key_N || key == Qt::Key_M || key == Qt::Key_Tab || key == Qt::Key_Delete || key == Qt::Key_X) {
            // Another box (N: read with the text; M: read only when asked for), the next one in
            // hand (Tab), the one in hand taken away (X; never the text's own).
            const int count = static_cast<int>(w.boxes.size());
            if (key == Qt::Key_Tab) {
                w.box = (w.box + 1) % count;
            } else if (key == Qt::Key_N || key == Qt::Key_M) {
                if (count >= 8) {
                    sounds.Play(Sound::Refuse);
                    return true;
                }
                w.boxes.append({QRectF(0.08, 0.06 + 0.16 * ((count - 1) % 4), 0.4, 0.12), key == Qt::Key_M});
                w.box = count;
            } else if (w.box > 0) {
                w.boxes.removeAt(w.box);
                w.box = std::min(w.box, static_cast<int>(w.boxes.size()) - 1);
            } else {
                sounds.Play(Sound::Refuse);
                Say(QStringLiteral("The first box is the text's own: it stays"));
                return true;
            }
            w.sizing = false;
            w.read_stale = true;
            w.read_ok = false;
            w.read.clear();
            w.area_at = clock.elapsed() - 600;
            sounds.Play(Sound::Tab);
            update();
        } else if (key == Qt::Key_Space || key == Qt::Key_F5) {
            w.read_stale = true;
            w.area_at = clock.elapsed() - 600;
            sounds.Play(Sound::Move);
            update();
        }
        break;
    case Wizard::Name:
        if (along) {
            // Down into the records that have what was typed in their name; up and out again.
            const QList<NamedRecord> like = RecordsLike(w.typed);
            const int next = std::clamp(w.suggestion + by, -1, static_cast<int>(like.size()) - 1);
            if (next != w.suggestion) {
                w.suggestion = next;
                w.name = next < 0 ? w.typed : like[next].english.isEmpty() ? like[next].title : like[next].english;
                sounds.Play(Sound::Move);
                update();
            }
        }
        break;
    default:
        break;
    }
    return true; // while it is open every key is its own
}

bool HomeWindow::WizardPress(const QPointF& at) {
    Wizard& w = *wizard;
    if (w.step == Wizard::Area && w.picture_box.contains(at)) {
        w.drawing = true;
        w.draw_from = at;
        return true;
    }
    for (const Hit& hit : shelf_hits) {
        if (!hit.area.contains(at)) {
            continue;
        }
        if (hit.kind == 20) {
            WizardBack();
        } else if (hit.kind == 22) {
            WizardAccept();
        } else if (hit.kind == 21) {
            // A click chooses, a second one takes, as on the shelf.
            int& chosen = w.step == Wizard::Doors ? w.door : w.step == Wizard::Helper ? w.field : w.row;
            if (chosen == hit.index && w.step != Wizard::Helper) {
                WizardAccept();
            } else {
                chosen = hit.index;
                sounds.Play(Sound::Move);
                update();
            }
        } else if (hit.kind == 23) {
            const QList<NamedRecord> like = RecordsLike(w.typed);
            if (hit.index < like.size()) {
                w.suggestion = hit.index;
                w.name = like[hit.index].english.isEmpty() ? like[hit.index].title : like[hit.index].english;
                sounds.Play(Sound::Move);
                update();
            }
        } else if (hit.kind == 24) {
            w.sizing = !w.sizing;
            update();
        } else if (hit.kind == 26) {
            WizardKey(hit.index);
        }
        return true;
    }
    return true;
}

void HomeWindow::WizardDrag(const QPointF& at, bool done) {
    Wizard& w = *wizard;
    if (!w.drawing) {
        return;
    }
    // The pointer draws the area: from where it was pressed to where it is, within the picture.
    const QRectF box = w.picture_box;
    const auto share = [&](const QPointF& point) {
        return QPointF(std::clamp((point.x() - box.left()) / box.width(), 0.0, 1.0), std::clamp((point.y() - box.top()) / box.height(), 0.0, 1.0));
    };
    const QRectF drawn = QRectF(share(w.draw_from), share(at)).normalized();
    if (drawn.width() >= 0.02 && drawn.height() >= 0.02) {
        WizardArea(drawn);
    }
    if (done) {
        w.drawing = false;
        w.area_at = clock.elapsed();
        update();
    }
}

QString HomeWindow::AddWindowTitle(const QString& title, const QString& window, const QRectF& area, const QImage& picture,
                                   const QList<Lines::Box>& more) {
    const QString folder = SharedFolder();
    Lines::Sources sources = Lines::Load(folder);
    const QString id = Lines::AddWindow(sources, title, window, area, RecordNamed(title.trimmed()), more);
    if (id.isEmpty() || !Lines::Save(folder, sources)) {
        sounds.Play(Sound::Refuse);
        Say(id.isEmpty() ? QStringLiteral("A window read by OCR wants a name and an area of its picture")
                         : QStringLiteral("The titles read from lines could not be written"));
        return {};
    }
    if (!picture.isNull()) {
        // The window as it looks now is the title's cover, until it has a better one.
        const QString cover = Lines::CoverOf(folder, id);
        QDir().mkpath(QFileInfo(cover).path());
        picture.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(cover, "JPEG", 90);
        covers.remove(cover);
    }
    if (lines_changed) {
        lines_changed();
    } else {
        Lines::WriteApp(folder, sources, QCoreApplication::applicationFilePath(), nullptr);
    }
    sounds.Play(Sound::Select);
    ReloadKeepingChoice();
    Changed();
    Say(QStringLiteral("%1 is on the shelf, read by OCR from the window \"%2\"").arg(title.trimmed(), window));
    return id;
}

void HomeWindow::SetOcrHelper(const QString& address, const QString& source, const QString& token) {
    Lines::Sources sources = Lines::Load(SharedFolder());
    sources.ocr.address = Lines::CleanHelperAddress(address);
    if (!source.isEmpty()) {
        sources.ocr.source = source;
    }
    sources.ocr.token = token;
    sources.ocr.program.clear(); // set by hand: someone else's to run
    Lines::Save(SharedFolder(), sources);
    Changed();
}

void HomeWindow::PaintWizard(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    Wizard& w = *wizard;
    if (HookStepOn()) {
        PaintHook(p); // a text hook's steps: wizard_hooks.inc
        return;
    }
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont value = ShelfFont(l.mono, 12.5, 0.05);
    const QFont body = ShelfFont(l.mono, 11.5, 0.03);
    const QFont name_face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 15.5);
    const QFont key_face = ShelfFont(l.display, 12, 0.16);

    // The head: the way back, which door, which step of how many.
    const int steps = w.door == 0 ? 2 : HookStepCount();
    const int step = w.step == Wizard::Doors ? 1 : w.step == Wizard::Name ? steps : w.step == Wizard::Area || w.step == Wizard::Helper ? 4 : 2;
    double x = ShelfLeft;
    x += Put(p, small, x, 49, QStringLiteral("‹  LIBRARY"), Mix(l.muted, l.ink, Ease(20, 0, ShelfHovered(20, 0))));
    x += Put(p, small, x, 49, QStringLiteral("  ·  "), l.muted);
    x += Put(p, small, x, 49, w.helper_only ? QStringLiteral("THE OCR HELPER") : QStringLiteral("ADD A TITLE"), l.ink);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    if (!w.helper_only) {
        right -= Put(p, small, right, 49, QStringLiteral(" / %1  ·  ").arg(steps, 2, 10, QLatin1Char('0')), l.muted, 1);
        right -= Put(p, small, right, 49, QStringLiteral("%1").arg(step, 2, 10, QLatin1Char('0')), l.accent, 1);
        if (w.step != Wizard::Doors) {
            Put(p, small, right, 49, QStringLiteral("%1  ·  %2  ·  ").arg(QLatin1String(DoorLetters[w.door]), QLatin1String(DoorNames[w.door])), l.muted, 1);
        }
    }
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
    shelf_hits.append({QRectF(ShelfLeft, 30, 140, 30), 20, 0});

    // What this step asks, large; under it, in a line, what answering it does.
    QString question, about, keys, key_label;
    switch (w.step) {
    case Wizard::Doors:
        question = QStringLiteral("WHERE IS THE GAME?");
        keys = QStringLiteral("← →   ENTER   ESC");
        break;
    case Wizard::Console:
        question = QStringLiteral("WHICH CONSOLE'S?");
        keys = QStringLiteral("↑ ↓   ENTER   ESC");
        key_label = QStringLiteral("CHOOSE A FOLDER");
        break;
    case Wizard::Helper:
        question = QStringLiteral("WHERE IS THE OCR HELPER?");
        keys = QStringLiteral("↑ ↓   TYPE OR PASTE   ENTER   ESC");
        key_label = w.helper_asking ? QStringLiteral("ASKING") : QStringLiteral("ASK THE HELPER");
        break;
    case Wizard::Window:
        question = QStringLiteral("WHICH GAME?");
        keys = QStringLiteral("← ↑ ↓ →   ENTER   ESC");
        key_label = QStringLiteral("THIS GAME");
        break;
    case Wizard::Area:
        question = QStringLiteral("WHERE IS ITS TEXT?");
        keys = QStringLiteral("DRAG   ← ↑ ↓ → %1   Q E PLACE / SIZE   N M TAB X BOXES   SPACE READ AGAIN   ENTER   ESC").arg(w.sizing ? QStringLiteral("SIZE") : QStringLiteral("PLACE"));
        key_label = QStringLiteral("USE THIS AREA");
        break;
    case Wizard::Name:
        question = QStringLiteral("WHAT IS IT CALLED?");
        keys = QStringLiteral("TYPE   ↑ ↓   ENTER   ESC");
        key_label = QStringLiteral("PUT IT ON THE SHELF");
        break;
    default:
        break;
    }
    // A step just come to: what it shows comes in, and its mark stands where it is.
    const int mark = w.step == Wizard::Doors ? w.door : w.step == Wizard::Helper ? w.field : w.step == Wizard::Name ? w.suggestion : w.row;
    if (wizard_seen_step != static_cast<int>(w.step)) {
        wizard_seen_step = static_cast<int>(w.step);
        wizard_t = shelf_still ? 1 : 0;
        wizard_mark_at = mark;
    }
    GlideTo(wizard_mark_at, mark, 0.035);
    shelf_busy = shelf_busy || wizard_t != 1;
    p.save();
    p.setOpacity(p.opacity() * wizard_t);
    p.translate(14 * (1 - wizard_t) * (1 - wizard_t), 0);
    Put(p, ShelfFont(l.display, 27, 0.02), ShelfLeft, ShelfTop + 44, question, l.ink);

    const double bottom = ShelfBottom;
    // The one filled thing: the key that goes on (the doors have theirs in the chosen door).
    const QRectF key(WizardSide, bottom - 44, ShelfRight - WizardSide, 44);
    if (!key_label.isEmpty()) {
        // Under the pointer it takes a line of the accent along its foot.
        const double lit = Ease(22, 0, ShelfHovered(22, 0));
        p.fillRect(key, l.ink);
        p.fillRect(QRectF(key.left(), key.bottom() - 4 * lit, key.width(), 4 * lit), l.accent);
        Put(p, key_face, key.center().x(), key.center().y() + 4.6 - 1.5 * lit, key_label, l.ground, 2);
        shelf_hits.append({key, 22, 0});
    }
    // The right column's table: what is known so far.
    const auto facts = [&](double y, const QList<QPair<QString, QString>>& rows) {
        for (const auto& row : rows) {
            p.fillRect(QRectF(WizardSide, y - 19, ShelfRight - WizardSide, 1), l.rule);
            Put(p, tiny, WizardSide, y, row.first, l.muted);
            // A name in Japanese in the text face: the mono face has none.
            const QFont said = HasJapanese(row.second) ? ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 14) : value;
            Put(p, said, ShelfRight, y, Paint::Fit(row.second, said, ShelfRight - WizardSide - 96), l.ink, 1);
            y += 30;
        }
        p.fillRect(QRectF(WizardSide, y - 19, ShelfRight - WizardSide, 1), l.rule);
        return y;
    };
    // Numbered rows between hairlines, as the pages have them.
    const auto rows = [&](double left, double width, double y, const QList<QPair<QString, QString>>& list, int chosen, int kind) {
        p.fillRect(QRectF(left, y - 22, width, 1), l.rule);
        if (!list.isEmpty()) {
            p.fillRect(QRectF(left, y - 21 + std::clamp(wizard_mark_at, 0.0, list.size() - 1.0) * 34, 3, 33), l.accent); // glides
        }
        for (int i = 0; i < list.size(); ++i) {
            const bool on = i == chosen;
            const double lit = Ease(kind, i, ShelfHovered(kind, i));
            if (!on && lit > 0) {
                p.fillRect(QRectF(left, y - 21, 3, 33), Faded(l.accent, 0.4 * lit));
            }
            Put(p, tiny, left + 16, y - 1, QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), on ? l.ink : l.muted);
            Put(p, name_face, left + 56, y, Paint::Fit(list[i].first, name_face, width - 56 - 320), on ? l.ink : Mix(Faded(l.ink, 0.62), l.ink, lit));
            Put(p, value, left + width, y - 1, Paint::Fit(list[i].second, value, 300), on ? l.ink : Mix(l.muted, l.ink, lit), 1);
            p.fillRect(QRectF(left, y + 12, width, 1), l.rule);
            shelf_hits.append({QRectF(left, y - 22, width, 34), kind, i});
            y += 34;
        }
        return y;
    };
    const double table_width = WizardSide - 50 - ShelfLeft;

    if (w.step == Wizard::Doors) {
        // Three doors side by side, a hairline between them; the one in hand in ink, with the key.
        const double width = (ShelfRight - ShelfLeft) / 3;
        const Lines::Sources sources = Lines::Load(SharedFolder());
        const QList<QPair<QString, QString>> door_facts[] = {
            {{QStringLiteral("READ BY"), QStringLiteral("A CORE")},
             {QStringLiteral("HERE"), consoles.isEmpty() ? QStringLiteral("NO CORES") : consoles.size() == 1 ? QStringLiteral("1 CONSOLE") : QStringLiteral("%1 CONSOLES").arg(consoles.size())}},
            {{QStringLiteral("READ BY"), QStringLiteral("A HOOK, ELSE OCR")},
             {QStringLiteral("OCR"), sources.ocr.address.isEmpty() ? QStringLiteral("NOT SET") : QUrl(sources.ocr.address).authority().toUpper()}},
            {{QStringLiteral("READ BY"), QStringLiteral("LINES")},
             {QStringLiteral("FROM"), QStringLiteral("WEBSOCKET  ·  CLIPBOARD")}},
        };
        p.fillRect(QRectF(ShelfLeft, WizardTop, ShelfRight - ShelfLeft, 1), l.rule);
        for (int i = 0; i < 3; ++i) {
            const bool on = i == w.door;
            const double left = ShelfLeft + i * width, inner = left + (i == 0 ? 0 : 28), room = width - 28 - (i == 0 ? 0 : 28);
            if (i > 0) {
                p.fillRect(QRectF(left, WizardTop, 1, bottom - WizardTop), l.rule);
            }
            const double lit = Ease(21, i, ShelfHovered(21, i));
            // The door in hand under a line of the accent, which grows out from its left as it is taken.
            const double taken = Ease(25, i, on);
            if (taken > 0) {
                p.fillRect(QRectF(inner, WizardTop, room * taken, 3), l.accent);
            }
            if (!on && lit > 0) {
                p.fillRect(QRectF(inner, WizardTop, room, 3), Faded(l.accent, 0.35 * lit));
            }
            Put(p, ShelfFont(l.display, 64, 0), inner - 3, WizardTop + 96 - 3 * lit * (on ? 0 : 1), QLatin1String(DoorLetters[i]), on ? l.ink : Mix(Faded(l.muted, 0.55), l.ink, 0.8 * lit));
            Put(p, ShelfFont(l.display, 19, 0.03), inner, WizardTop + 142, QLatin1String(DoorNames[i]), on ? l.ink : Mix(l.muted, l.ink, lit));
            double y = WizardTop + 196;
            for (const auto& fact : door_facts[i]) {
                p.fillRect(QRectF(inner, y - 19, room, 1), l.rule);
                Put(p, tiny, inner, y, fact.first, on ? l.muted : Faded(l.muted, 0.7));
                Put(p, value, inner + room, y, Paint::Fit(fact.second, value, room - 110), on ? l.ink : l.muted, 1);
                y += 30;
            }
            p.fillRect(QRectF(inner, y - 19, room, 1), l.rule);
            if (on) {
                const QRectF take(inner, bottom - 62, room, 36);
                p.fillRect(take, l.ink);
                p.fillRect(QRectF(take.left(), take.bottom() - 4 * lit, take.width(), 4 * lit), l.accent);
                Put(p, key_face, take.center().x(), take.center().y() + 4.4, QStringLiteral("CHOOSE"), l.ground, 2);
            }
            shelf_hits.append({QRectF(left, WizardTop, width, bottom - WizardTop), 21, i});
        }
    } else if (w.step == Wizard::Console) {
        QList<QPair<QString, QString>> list;
        for (const PlayConsole& console : consoles) {
            const int folders = play != nullptr ? static_cast<int>(play->Folders(console.key).size()) : 0;
            list.append({console.name, folders == 0 ? QStringLiteral("NO FOLDER YET") : folders == 1 ? QStringLiteral("1 FOLDER") : QStringLiteral("%1 FOLDERS").arg(folders)});
        }
        w.row = std::clamp(w.row, 0, std::max(0, static_cast<int>(list.size()) - 1));
        rows(ShelfLeft, table_width, WizardTop + 30, list, w.row, 21);
        facts(WizardTop + 27, {{QStringLiteral("READ BY"), QStringLiteral("A CORE")},
                               {QStringLiteral("CONSOLE"), w.row < consoles.size() ? consoles[w.row].name.toUpper() : QString()},
                               {QStringLiteral("ITS NAME"), QStringLiteral("FROM ITS TITLE RECORD")}});
    } else if (w.step == Wizard::Helper) {
        const QFont field_face = ShelfFont(l.mono, 19, 0.02);
        const QString labels[] = {QStringLiteral("ADDRESS"), QStringLiteral("PICTURE SOURCE"), QStringLiteral("TOKEN")};
        const QString hints[] = {QStringLiteral("http://127.0.0.1:8794"), QStringLiteral("furoshiki"), QStringLiteral("as the helper's configuration has it")};
        // The token is not shown whole: its length, and its last characters.
        const QString token = w.helper.token.isEmpty() ? QString()
                                                       : QString(std::max<qsizetype>(0, w.helper.token.size() - 4), QChar(0x2022)) + w.helper.token.right(4);
        const QString values[] = {w.helper.address, w.helper.source, token};
        double y = WizardTop + 36;
        for (int i = 0; i < 3; ++i) {
            const bool on = i == w.field;
            if (on) {
                p.fillRect(QRectF(ShelfLeft, y - 12, 3, 62), l.accent);
            }
            Put(p, tiny, ShelfLeft + 16, y, labels[i], on ? l.ink : l.muted);
            Field(p, l, field_face, ShelfLeft + 16, y + 36, table_width - 16, values[i], hints[i], on);
            shelf_hits.append({QRectF(ShelfLeft, y - 14, table_width, 66), 21, i});
            y += 84;
        }
        const QString said = w.helper_asking ? QStringLiteral("%1 %2").arg(OcrHelper::Instance().Starting() ? QStringLiteral("STARTING THE HELPER AT") : QStringLiteral("ASKING"), w.helper.address.toUpper())
                             : w.helper_said.isEmpty() ? QStringLiteral("NOT ASKED YET") : w.helper_said;
        facts(WizardTop + 27, {{QStringLiteral("THE HELPER"), w.helper_ok ? QStringLiteral("ANSWERS") : w.helper_asking ? QStringLiteral("…") : QStringLiteral("—")},
                               {QStringLiteral("KEPT IN"), QStringLiteral("THIS DEVICE'S SETTINGS")}});
        Wrapped(p, body, QRectF(WizardSide, WizardTop + 84, ShelfRight - WizardSide, 120), said, w.helper_ok ? l.ink : l.muted);
    } else if (w.step == Wizard::Window) {
        // The windows, each as it looks now, four across; the one in hand edged in ink.
        const int count = static_cast<int>(w.windows.size());
        // Two rows of them, each with its name under it, above the line that counts them and the key.
        const double gap = 20, cell = (ShelfRight - ShelfLeft - gap * (WindowColumns - 1)) / WindowColumns, under = 36;
        const double pitch = (bottom - 58 - (WizardTop + 4)) / WindowRows, tall = std::min(cell * 9 / 16, pitch - under);
        const int first_row = std::max(0, w.row / WindowColumns - (WindowRows - 1));
        const QFont caption = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 13.5);
        for (int i = first_row * WindowColumns; i < std::min(count, (first_row + WindowRows) * WindowColumns); ++i) {
            const bool on = i == w.row;
            const int column = i % WindowColumns, line = i / WindowColumns - first_row;
            const QRectF box(ShelfLeft + column * (cell + gap), WizardTop + 4 + line * pitch, cell, tall);
            const double lit = Ease(21, i, ShelfHovered(21, i)), taken = Ease(25, i, on);
            const double seen = 0.6 + 0.4 * std::max(taken, 0.6 * lit);
            p.fillRect(box, l.raised);
            const QImage& thumb = w.thumbs.value(i);
            if (!thumb.isNull()) {
                const double scale = std::min(box.width() / thumb.width(), box.height() / thumb.height());
                const QSizeF size(thumb.width() * scale, thumb.height() * scale);
                p.setOpacity(p.opacity() * seen);
                p.drawImage(QRectF(box.center().x() - size.width() / 2, box.center().y() - size.height() / 2, size.width(), size.height()), thumb);
                p.setOpacity(p.opacity() / seen);
            } else {
                Put(p, tiny, box.center().x(), box.center().y() + 3, QStringLiteral("NO PICTURE"), l.muted, 2);
            }
            p.setPen(QPen(Mix(l.rule, l.ink, std::max(taken, 0.6 * lit)), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
            if (taken > 0) {
                p.fillRect(QRectF(box.left(), box.top(), box.width() * taken, 3), l.accent);
            }
            const double number = Put(p, tiny, box.left(), box.bottom() + 22, QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), on ? l.accent : l.muted);
            const QString title = w.windows[i].title;
            Put(p, HasJapanese(title) ? caption : ShelfFont(l.mono, 11.5, 0.03), box.left() + number + 12, box.bottom() + 22,
                Paint::Fit(HasJapanese(title) ? title : title.toUpper(), HasJapanese(title) ? caption : ShelfFont(l.mono, 11.5, 0.03), cell - number - 14), on ? l.ink : Mix(l.muted, l.ink, lit));
            shelf_hits.append({box.adjusted(0, 0, 0, 30), 21, i});
        }
        if (count == 0) {
            Put(p, ShelfFont(l.display, 19, 0.04), ShelfLeft, WizardTop + 60, QStringLiteral("NO WINDOW IS OPEN"), l.muted);
        }
        Put(p, small, ShelfLeft, bottom - 14, QStringLiteral("%1 %2%3").arg(count).arg(count == 1 ? QStringLiteral("WINDOW") : QStringLiteral("WINDOWS"))
                .arg(w.helper_asking ? (OcrHelper::Instance().Starting() ? QStringLiteral("  ·  STARTING THE HELPER") : QStringLiteral("  ·  ASKING THE HELPER")) : w.helper_ok ? QStringLiteral("  ·  THE HELPER ANSWERS") : QString()), l.muted);
    } else if (w.step == Wizard::Area) {
        // The window's picture, as large as the column lets it be; outside the area it is
        // dimmed, the area itself edged in the accent with a grip at its corner.
        const QRectF room(ShelfLeft, WizardTop + 4, WizardSide - 40 - ShelfLeft, bottom - WizardTop - 4);
        QRectF box = room;
        if (!w.picture.isNull()) {
            const double scale = std::min(room.width() / w.picture.width(), room.height() / w.picture.height());
            box = QRectF(room.left(), room.top(), w.picture.width() * scale, w.picture.height() * scale);
            p.drawImage(box, w.picture);
        } else {
            p.fillRect(box, l.raised);
            Put(p, tiny, box.center().x(), box.center().y(), QStringLiteral("THE WINDOW GIVES NO PICTURE"), l.muted, 2);
        }
        w.picture_box = box;
        const auto on_sheet = [&](const QRectF& share) {
            return QRectF(box.left() + share.x() * box.width(), box.top() + share.y() * box.height(), share.width() * box.width(), share.height() * box.height());
        };
        const QRectF area = on_sheet(w.Rect());
        QPainterPath boxes_path;
        for (const Lines::Box& each : w.boxes) {
            boxes_path.addRect(on_sheet(each.rect));
        }
        QPainterPath outside;
        outside.addRect(box);
        outside = outside.subtracted(boxes_path);
        p.fillPath(outside, Faded(l.ground, 0.62));
        p.setPen(QPen(l.rule, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
        // Every box, each with what it is; the one in hand in the accent with its grip, one read
        // only on request with a broken edge.
        const auto kind_of = [&](int i) {
            return i == 0 ? QStringLiteral("TEXT") : w.boxes[i].ask ? QStringLiteral("ON REQUEST") : QStringLiteral("ALSO");
        };
        for (int i = 0; i < w.boxes.size(); ++i) {
            const bool on = i == w.box;
            const QRectF each = on_sheet(w.boxes[i].rect);
            QPen edge(on ? l.accent : l.ink, on ? 1.5 : 1);
            if (w.boxes[i].ask) {
                edge.setStyle(Qt::DashLine);
            }
            p.setPen(edge);
            p.drawRect(each);
            Put(p, tiny, each.left(), each.top() - 8 < box.top() + 10 ? each.bottom() + 16 : each.top() - 8,
                w.boxes.size() > 1 ? QStringLiteral("%1  %2").arg(i + 1).arg(kind_of(i)) : kind_of(i), on ? l.accent : l.ink);
        }
        p.fillRect(QRectF(w.sizing ? area.right() - 4 : area.left() - 4, w.sizing ? area.bottom() - 4 : area.top() - 4, 8, 8), l.accent);

        const auto percent = [](double v) { return QStringLiteral("%1").arg(static_cast<int>(std::lround(v * 100)), 2, 10, QLatin1Char('0')); };
        const double after = facts(WizardTop + 27, {{QStringLiteral("WINDOW"), w.window.title.toUpper()},
                                                    {QStringLiteral("BOX"), QStringLiteral("%1 OF %2  ·  %3").arg(w.box + 1).arg(w.boxes.size()).arg(kind_of(w.box))},
                                                    {QStringLiteral("PICTURE"), w.picture.isNull() ? QStringLiteral("NONE") : QStringLiteral("%1 × %2").arg(w.picture.width()).arg(w.picture.height())},
                                                    {QStringLiteral("AREA"), QStringLiteral("X %1  Y %2  W %3  H %4").arg(percent(w.Rect().x()), percent(w.Rect().y()), percent(w.Rect().width()), percent(w.Rect().height()))},
                                                    {QStringLiteral("ARROWS"), w.sizing ? QStringLiteral("CHANGE ITS SIZE") : QStringLiteral("MOVE IT")}});
        shelf_hits.append({QRectF(WizardSide, after - 49, ShelfRight - WizardSide, 30), 24, 0});
        // The boxes' own keys, for the pointer too.
        {
            double at = WizardSide;
            const QPair<QString, int> words[] = {{QStringLiteral("N  BOX"), Qt::Key_N}, {QStringLiteral("M  ON REQUEST"), Qt::Key_M},
                                                 {QStringLiteral("TAB  NEXT"), Qt::Key_Tab}, {QStringLiteral("X  REMOVE"), Qt::Key_X}};
            for (const auto& word : words) {
                const bool can = word.second == Qt::Key_X ? w.box > 0 : word.second == Qt::Key_Tab ? w.boxes.size() > 1 : w.boxes.size() < 8;
                const double wide = Put(p, tiny, at, after + 4, word.first, can ? Mix(l.muted, l.ink, Ease(26, word.second & 0xffff, ShelfHovered(26, word.second))) : Faded(l.muted, 0.45));
                shelf_hits.append({QRectF(at - 4, after - 12, wide + 8, 24), 26, word.second});
                at += wide + 16;
            }
        }
        // What the helper reads there.
        Put(p, tiny, WizardSide, after + 36, QStringLiteral("READ"), l.muted);
        const QRectF said(WizardSide, after + 50, ShelfRight - WizardSide, key.top() - after - 68);
        if (w.read_ok && !w.read.isEmpty()) {
            Wrapped(p, ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 19), said, w.read, l.ink);
        } else {
            const QString state = w.read_ok ? QStringLiteral("NOTHING JAPANESE STANDS THERE NOW")
                                  : !w.read.isEmpty() ? w.read.toUpper()
                                  : w.client.Taken() == 0 ? w.client.Status().toUpper() : QStringLiteral("READING");
            Wrapped(p, body, said, state, l.muted);
        }
    } else if (w.step == Wizard::Name) {
        // The name on a line of its own, large; whether a record is exactly that; the records.
        const bool japanese = HasJapanese(w.name);
        Field(p, l, japanese ? ShelfFont(l.text, 30) : ShelfFont(l.display, 24, 0.02), ShelfLeft, WizardTop + 52, table_width, japanese ? w.name : w.name.toUpper(),
              QStringLiteral("ITS NAME"), w.suggestion < 0);
        const QString key_of = RecordNamed(w.name.trimmed());
        const QList<NamedRecord> like = RecordsLike(w.typed);
        double y = WizardTop + 118;
        Put(p, tiny, ShelfLeft, y - 30, like.isEmpty() ? QStringLiteral("NO RECORD HAS THAT IN ITS NAME") : QStringLiteral("TITLE RECORDS  ·  %1").arg(like.size()), l.muted);
        const int shown = 8;
        const int first = std::clamp(w.suggestion - shown / 2, 0, std::max(0, static_cast<int>(like.size()) - shown));
        if (!like.isEmpty()) {
            p.fillRect(QRectF(ShelfLeft, y - 22, table_width, 1), l.rule);
        }
        const QFont own = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 13.5);
        for (int i = first; i < std::min<int>(like.size(), first + shown); ++i) {
            const bool on = i == w.suggestion;
            const bool exact = like[i].key == key_of;
            const double lit = Ease(23, i, ShelfHovered(23, i));
            if (on || lit > 0) {
                p.fillRect(QRectF(ShelfLeft, y - 21, 3, 33), on ? l.accent : Faded(l.accent, 0.4 * lit));
            }
            Put(p, tiny, ShelfLeft + 16, y - 1, QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), on || exact ? l.ink : l.muted);
            const QString english = like[i].english.isEmpty() ? like[i].title : like[i].english;
            const double room = table_width - 56 - 300;
            if (HasJapanese(english)) {
                Put(p, name_face, ShelfLeft + 56, y, Paint::Fit(english, name_face, room), on || exact ? l.ink : Mix(Faded(l.ink, 0.62), l.ink, lit));
            } else {
                const QFont face = ShelfFont(l.display, 11.5, 0.05);
                Put(p, face, ShelfLeft + 56, y - 1, Paint::Fit(english.toUpper(), face, room), on || exact ? l.ink : Mix(Faded(l.ink, 0.62), l.ink, lit));
            }
            if (!like[i].english.isEmpty()) {
                Put(p, own, ShelfLeft + table_width, y, Paint::Fit(like[i].title, own, 280), l.muted, 1);
            }
            p.fillRect(QRectF(ShelfLeft, y + 12, table_width, 1), l.rule);
            shelf_hits.append({QRectF(ShelfLeft, y - 22, table_width, 34), 23, i});
            y += 34;
        }
        // At the right: what goes onto the shelf.
        int more = 0;
        if (!key_of.isEmpty()) {
            CastOf(key_of, 0, &more);
        }
        QString english_of;
        for (const NamedRecord& record : AllRecords()) {
            if (record.key == key_of) {
                english_of = record.english;
            }
        }
        QList<QPair<QString, QString>> known{
            {QStringLiteral("READ BY"), w.door == 1 && w.by_ocr ? QStringLiteral("WINDOW  ·  OCR") : HookReadBy()}};
        if (w.door == 1 && w.by_ocr) {
            known.append({QStringLiteral("WINDOW"), w.window.title.toUpper()});
        } else if (const QString program = HookProgram(); !program.isEmpty()) {
            known.append({QStringLiteral("PROGRAM"), program.toUpper()});
        }
        known.append({QStringLiteral("RECORD"), key_of.isEmpty() ? QStringLiteral("NONE OF EXACTLY THIS NAME") : QStringLiteral("FOUND")});
        if (!key_of.isEmpty()) {
            known.append({QStringLiteral("ON ITS SPINE"), english_of.isEmpty() ? QStringLiteral("ITS OWN NAME") : english_of.toUpper()});
            known.append({QStringLiteral("NAMES"), more > 0 ? QStringLiteral("%1 PEOPLE").arg(more) : QStringLiteral("NONE YET")});
        }
        const double after = facts(WizardTop + 27, known);
        if (key_of.isEmpty() && !w.name.trimmed().isEmpty()) {
            Wrapped(p, body, QRectF(WizardSide, after, ShelfRight - WizardSide, 80),
                    QStringLiteral("IT STANDS UNDER THE NAME AS WRITTEN, WITHOUT A NAMES DICTIONARY. A RECORD CAN BE MADE FOR IT LATER."), l.muted);
        }
    }

    p.restore();
    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    Put(p, tiny, ShelfRight, 674, keys, l.muted, 1);
}
