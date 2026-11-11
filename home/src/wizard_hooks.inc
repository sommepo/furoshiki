// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The wizard's door C, "A TEXT HOOK" (docs/furoshiki-3.md, 9; docs/hooks.md; the maintainer,
// 2026-11-10: "native texthook/lunatranslator/agent support baked in now. agent scripts take
// priority"). How is it hooked: an Agent script (0xDC00's, run with Frida), LunaHook, Textractor,
// each started by Furoshiki against a program running here, or a texthooker the user runs himself
// (its WebSocket, or the clipboard). Then, for an engine of Furoshiki's: which script (Agent),
// which program, and what the hook says there now, thread by thread, of which the dialogue's is
// chosen; then its name, as every door ends. Drawn by the wizard's rules: type and hairlines,
// one filled key a screen. Part of home_window.cpp, after wizard_page.inc, inside namespace Home.

struct HomeWindow::HookSteps {
    enum Way { AgentScript, LunaHook, Textractor, WebSocket, Clipboard, Ways };
    int way{};
    // An Agent script: the fetched scripts, sifted by what was typed.
    QList<Hooks::Agent::Script> scripts, found;
    QList<Hooks::Agent::Script> matches; ///< Those that look written for the window's program
    bool listed{};
    bool searched{}; ///< The script was chosen from a search, not from the matches
    QString typed;
    int script_row{};
    Hooks::Agent::Script script;
    // The program it is put in.
    QList<Hooks::Program> programs;
    qint64 listed_at{-100000};
    Hooks::Program program;
    // The engine running against it, and its threads chosen as the dialogue.
    Hooks::Runner runner;
    QStringList chosen;
    // Files fetched: 0xDC00's scripts, an engine.
    Hooks::Fetch fetch;
    QString fetch_said;
    bool then_hook{}; ///< Once the engine is fetched, hook the program chosen

    [[nodiscard]] bool Engine() const {
        return way <= Textractor;
    }
    [[nodiscard]] Hooks::Engine EngineOf() const {
        return way == LunaHook ? Hooks::Engine::Luna : way == Textractor ? Hooks::Engine::Textractor : Hooks::Engine::Agent;
    }
};

namespace {

const char* const HookWayNames[] = {"An Agent script", "LunaHook", "Textractor", "A texthooker I run myself", "The clipboard"};

/// What an engine's files are called where they are fetched, and whether they are here.
QString HookKitOf(int way) {
    return way == HomeWindow::HookSteps::LunaHook ? QStringLiteral("luna") : way == HomeWindow::HookSteps::Textractor ? QStringLiteral("textractor") : QStringLiteral("frida");
}

bool HookKitHere(int way, const QString& shared) {
    if (way == HomeWindow::HookSteps::AgentScript) {
        return !Hooks::FridaInject(shared).isEmpty();
    }
    return way == HomeWindow::HookSteps::LunaHook ? !Hooks::LunaCommand(shared).isEmpty() : !Hooks::TextractorCommand(shared, false).isEmpty();
}

/// The ways a game window's program can be read, as the Match step lists them: each matching
/// script (its index), then -1 a search of the scripts, -2 LunaHook, -3 Textractor, -4 OCR; with
/// no script like it, OCR first (the maintainer: "if there isnt one OCR is the option").
QList<int> MatchRows(const HomeWindow::HookSteps& h) {
    QList<int> rows;
    for (int i = 0; i < h.matches.size(); ++i) {
        rows.append(i);
    }
    if (h.matches.isEmpty()) {
        rows.append(-4);
    }
    rows += {-1, -2, -3};
    if (!h.matches.isEmpty()) {
        rows.append(-4);
    }
    return rows;
}

/// A program as a row says it: its window's title, else its file's name.
QString ProgramShown(const Hooks::Program& program) {
    return program.window.isEmpty() ? program.name : program.window;
}

} // namespace

bool HomeWindow::HookStepOn() const {
    return wizard_on && wizard && wizard->hook &&
           (wizard->step == Wizard::Hook || wizard->step == Wizard::Script || wizard->step == Wizard::Program || wizard->step == Wizard::Thread ||
            wizard->step == Wizard::Match);
}

void HomeWindow::HookOpen() {
    Wizard& w = *wizard;
    if (w.hook) {
        return;
    }
    w.hook = std::make_shared<HookSteps>();
    HookSteps* h = w.hook.get();
    Wizard* of = wizard.get();
    connect(&h->runner, &Hooks::Runner::Changed, this, [this, of] {
        if (wizard.get() == of && wizard_on) {
            update();
        }
    });
    connect(&h->fetch, &Hooks::Fetch::Done, this, [this, of, h](const QString& key, bool ok, const QString& said) {
        if (wizard.get() != of) {
            return;
        }
        h->fetch_said = said.toUpper();
        sounds.Play(ok ? Sound::Select : Sound::Refuse);
        if (key == QStringLiteral("scripts")) {
            h->listed = false;
        } else if (ok && h->then_hook) {
            HookStart(); // the engine is here now: the program chosen is hooked
        }
        h->then_hook = false;
        update();
    });
}

void HomeWindow::HookClose() {
    if (wizard && wizard->hook) {
        wizard->hook->runner.Stop();
    }
}

void HomeWindow::HookGo(int to) {
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    w.step = static_cast<Wizard::Step>(to);
    if (w.step != Wizard::Thread) {
        h.runner.Stop();
    }
    if (!h.fetch.Busy()) {
        h.fetch_said.clear(); // what the step before fetched is said there
    }
    switch (w.step) {
    case Wizard::Hook:
        w.row = h.way >= HookSteps::WebSocket ? h.way - HookSteps::WebSocket : 0; // a text hook's own: the WebSocket, the clipboard
        break;
    case Wizard::Match:
        h.listed = false;
        w.row = 0;
        break;
    case Wizard::Script:
        h.listed = false;
        w.row = h.script_row;
        break;
    case Wizard::Program:
        h.listed_at = -100000;
        w.row = 0;
        break;
    case Wizard::Thread:
        w.row = 0;
        h.chosen.clear();
        break;
    default:
        break;
    }
    update();
}

void HomeWindow::HookMatch() {
    Wizard& w = *wizard;
    HookOpen();
    HookSteps& h = *w.hook;
    w.by_ocr = false;
    h.program = w.game;
    h.searched = false;
    h.typed.clear();
    HookGo(Wizard::Match);
    // The scripts are looked through at once: fetched first where they are not here yet.
    if (!Hooks::Agent::HasRuntime(Hooks::ScriptsFolder(SharedFolder()))) {
        HookFetch(QStringLiteral("scripts"));
    }
}

void HomeWindow::HookStep() {
    if (!HookStepOn()) {
        return;
    }
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    const qint64 now = clock.elapsed();
    if ((w.step == Wizard::Script || w.step == Wizard::Match) && !h.listed) {
        h.listed = true;
        h.scripts = Hooks::Agent::List(Hooks::ScriptsFolder(SharedFolder()));
        h.found = Hooks::Agent::Search(h.scripts, h.typed);
        h.matches = Hooks::Agent::Suggest(h.scripts, h.program.name, h.program.path, h.program.window);
        w.row = std::clamp(w.row, 0, std::max(0, static_cast<int>(h.found.size()) - 1));
        update();
    }
    if (w.step == Wizard::Program && now - h.listed_at > 1000) {
        // The programs as they run now: the game may be started while this stands open.
        h.listed_at = now;
        const qint64 chosen = w.row < h.programs.size() ? h.programs[w.row].pid : 0;
        h.programs = Hooks::Programs();
        for (int i = 0; i < h.programs.size(); ++i) {
            if (h.programs[i].pid == chosen) {
                w.row = i;
            }
        }
        w.row = std::clamp(w.row, 0, std::max(0, static_cast<int>(h.programs.size()) - 1));
        update();
    }
    if (h.fetch.Busy()) {
        update(); // its words change as it goes
    }
}

bool HomeWindow::HookText(int key, const QString& text) {
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    QString* into = w.step == Wizard::Hook && w.row == 0 ? &w.address : w.step == Wizard::Script ? &h.typed : nullptr;
    if (into == nullptr) {
        return false;
    }
    if (key == Qt::Key_Backspace) {
        if (into->isEmpty()) {
            return false; // nothing to take: Backspace is a step back
        }
        into->chop(1);
    } else if (!text.isEmpty() && text.at(0).isPrint() && key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Tab) {
        into->append(QString(text).remove(QLatin1Char('\n')).remove(QLatin1Char('\r')));
    } else {
        return false;
    }
    if (w.step == Wizard::Script) {
        h.found = Hooks::Agent::Search(h.scripts, h.typed);
        w.row = 0;
    }
    update();
    return true;
}

void HomeWindow::HookFetch(const QString& kit) {
    HookSteps& h = *wizard->hook;
    if (h.fetch.Busy()) {
        return;
    }
    if (!Hooks::CanFetch(kit)) {
        sounds.Play(Sound::Refuse);
        h.fetch_said = kit == QStringLiteral("frida") ? QStringLiteral("FRIDA-INJECT COMES WITH FUROSHIKI'S PACKAGE, IN HOOKS BESIDE THE PROGRAM: IT IS NOT THERE")
                                                      : QStringLiteral("%1 IS A WINDOWS PROGRAM: IT HOOKS GAMES ON WINDOWS ONLY").arg(Hooks::KitOf(kit).name.toUpper());
        update();
        return;
    }
    if (dry_run && qEnvironmentVariableIsEmpty("READER_HOME_SCRIPTS_URL") && qEnvironmentVariableIsEmpty("READER_HOME_FRIDA_URL")) {
        // A test run fetches nothing from the network unless it names where from.
        h.fetch_said = QStringLiteral("A TEST RUN FETCHES NOTHING");
        update();
        return;
    }
    h.fetch_said.clear();
    h.fetch.Start(kit, SharedFolder());
    sounds.Play(Sound::Select);
    update();
}

void HomeWindow::HookAccept() {
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    const QString shared = SharedFolder();
    switch (w.step) {
    case Wizard::Hook:
        h.way = HookSteps::WebSocket + std::clamp(w.row, 0, 1);
        if (h.way == HookSteps::WebSocket && w.address.trimmed() != Lines::Load(shared).address) {
            const QUrl url(w.address.trimmed());
            if (!url.isValid() || url.scheme() != QStringLiteral("ws") || url.host().isEmpty()) {
                sounds.Play(Sound::Refuse);
                Say(QStringLiteral("\"%1\" is no WebSocket address: ws://host:port").arg(w.address));
                return;
            }
            SetLinesAddress(w.address);
        }
        sounds.Play(Sound::Select);
        if (!h.Engine()) {
            WizardGo(Wizard::Name);
        } else {
            HookGo(h.way == HookSteps::AgentScript ? Wizard::Script : Wizard::Program);
        }
        return;
    case Wizard::Match: {
        const QList<int> rows = MatchRows(h);
        const int pick = rows.value(std::clamp(w.row, 0, static_cast<int>(rows.size()) - 1));
        sounds.Play(Sound::Select);
        if (pick >= 0) {
            h.way = HookSteps::AgentScript;
            h.script = h.matches[pick];
            HookStart();
        } else if (pick == -1) {
            h.way = HookSteps::AgentScript;
            // The search begins with the game's name, as its window or its file says it.
            h.typed = h.program.window.isEmpty() ? QFileInfo(h.program.name).completeBaseName() : h.program.window.section(QLatin1Char(' '), 0, 1);
            HookGo(Wizard::Script);
        } else if (pick == -2 || pick == -3) {
            h.way = pick == -2 ? HookSteps::LunaHook : HookSteps::Textractor;
            HookStart();
        } else {
            WizardOcr();
        }
        return;
    }
    case Wizard::Script:
        if (h.fetch.Busy()) {
            return;
        }
        if (h.scripts.isEmpty() || !Hooks::Agent::HasRuntime(Hooks::ScriptsFolder(shared))) {
            HookFetch(QStringLiteral("scripts"));
            return;
        }
        if (w.row >= h.found.size()) {
            sounds.Play(Sound::Refuse);
            return;
        }
        h.script = h.found[w.row];
        h.script_row = w.row;
        h.searched = true;
        sounds.Play(Sound::Select);
        if (w.door == 1) {
            HookStart(); // the window's program is known
        } else {
            HookGo(Wizard::Program);
        }
        return;
    case Wizard::Program:
        if (h.fetch.Busy()) {
            return;
        }
        if (w.row >= h.programs.size()) {
            sounds.Play(Sound::Refuse);
            return;
        }
        h.program = h.programs[w.row];
        HookStart();
        return;
    case Wizard::Thread: {
        const QList<Hooks::Thread>& threads = h.runner.Threads();
        if (h.way == HookSteps::AgentScript) {
            // The script is the choice; it must have loaded.
            if (!h.runner.Attached()) {
                sounds.Play(Sound::Refuse);
                Say(!h.runner.Fault().isEmpty() ? QStringLiteral("The script is not in the program: %1").arg(h.runner.Fault()) : QStringLiteral("The script has not loaded yet"));
                return;
            }
        } else {
            if (h.chosen.isEmpty() && w.row < threads.size()) {
                h.chosen = {threads[w.row].key};
            }
            if (h.chosen.isEmpty()) {
                sounds.Play(Sound::Refuse);
                Say(QStringLiteral("No thread has said anything yet: go on a line in the game"));
                return;
            }
        }
        if (w.name.isEmpty()) {
            w.name = h.way == HookSteps::AgentScript ? h.script.name : !h.program.window.isEmpty() ? h.program.window : QFileInfo(h.program.name).completeBaseName();
        }
        sounds.Play(Sound::Select);
        h.runner.Stop(); // the title's own hook takes over
        WizardGo(Wizard::Name);
        return;
    }
    default:
        return;
    }
}

void HomeWindow::HookStart() {
    HookSteps& h = *wizard->hook;
    const QString shared = SharedFolder();
    if (h.fetch.Busy()) {
        return;
    }
    if (!HookKitHere(h.way, shared)) {
        // The engine's files, at first use; then this program is hooked.
        h.then_hook = true;
        HookFetch(HookKitOf(h.way));
        return;
    }
    Hooks::Plan plan;
    plan.engine = h.EngineOf();
    plan.pid = h.program.pid;
    plan.is32 = h.program.is32;
    plan.script = h.script.file;
    plan.title = QStringLiteral("wizard");
    plan.shared = shared;
    QString error;
    if (!h.runner.Start(plan, error)) {
        sounds.Play(Sound::Refuse);
        h.fetch_said = error.toUpper();
        update();
        return;
    }
    h.fetch_said.clear();
    sounds.Play(Sound::Select);
    HookGo(Wizard::Thread);
    return;
}

void HomeWindow::HookBack() {
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    sounds.Play(Sound::Back);
    switch (w.step) {
    case Wizard::Hook:
        w.step = Wizard::Doors;
        update();
        return;
    case Wizard::Match:
        h.runner.Stop();
        WizardGo(Wizard::Window);
        return;
    case Wizard::Script:
        HookGo(w.door == 1 ? Wizard::Match : Wizard::Hook);
        return;
    case Wizard::Program:
        HookGo(h.way == HookSteps::AgentScript ? Wizard::Script : Wizard::Hook);
        return;
    case Wizard::Thread:
        HookGo(w.door == 1 ? (h.searched ? Wizard::Script : Wizard::Match) : Wizard::Program);
        return;
    case Wizard::Name:
        // From the name, back to what came before it on this way.
        if (w.door == 1) {
            HookGo(Wizard::Match);
        } else if (h.Engine()) {
            HookGo(Wizard::Program);
        } else {
            HookGo(Wizard::Hook);
        }
        return;
    default:
        return;
    }
}

bool HomeWindow::HookKey(int key) {
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        HookAccept();
        return true;
    }
    if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        HookBack();
        return true;
    }
    const int by = key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : key == Qt::Key_PageUp || key == Qt::Key_Q ? -8 : key == Qt::Key_PageDown || key == Qt::Key_E ? 8 : 0;
    const int count = w.step == Wizard::Hook ? 2 : w.step == Wizard::Match ? static_cast<int>(MatchRows(h).size()) : w.step == Wizard::Script ? static_cast<int>(h.found.size())
                      : w.step == Wizard::Program ? static_cast<int>(h.programs.size()) : static_cast<int>(h.runner.Threads().size());
    // In the script's search Q and E are letters (HookText took them); PageUp and PageDown go eight.
    if (by != 0) {
        const int next = std::clamp(w.row + by, 0, std::max(0, count - 1));
        if (next != w.row) {
            w.row = next;
            sounds.Play(Sound::Move);
            update();
        }
        return true;
    }
    if (key == Qt::Key_F5 && w.step == Wizard::Script) {
        HookFetch(QStringLiteral("scripts")); // the authors' newest
    } else if (key == Qt::Key_Space && w.step == Wizard::Thread && h.way != HookSteps::AgentScript && w.row < h.runner.Threads().size()) {
        // Another thread that is the dialogue too (a game that writes names and speech apart).
        const QString thread = h.runner.Threads()[w.row].key;
        if (!h.chosen.removeOne(thread)) {
            h.chosen.append(thread);
        }
        sounds.Play(Sound::Move);
        update();
    }
    return true;
}

QString HomeWindow::HookTitle(const QString& name) {
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    if (!h.Engine()) {
        return AddLinesTitle(h.way == HookSteps::WebSocket ? QStringLiteral("websocket") : QStringLiteral("clipboard"), name);
    }
    Lines::Title hook;
    hook.engine = Hooks::KeyOf(h.EngineOf());
    hook.script = h.way == HookSteps::AgentScript ? h.script.file : QString();
    hook.process = h.program.name;
    hook.path = h.program.path;
    hook.threads = h.chosen;
    return AddHookTitle(name, hook);
}

QString HomeWindow::AddHookTitle(const QString& title, const Lines::Title& hook) {
    const QString folder = SharedFolder();
    Lines::Sources sources = Lines::Load(folder);
    const QString id = Lines::AddHook(sources, title, hook, RecordNamed(title.trimmed()));
    if (id.isEmpty() || !Lines::Save(folder, sources)) {
        sounds.Play(Sound::Refuse);
        Say(id.isEmpty() ? QStringLiteral("A title read by a hook wants a name, an engine and the program it is hooked in")
                         : QStringLiteral("The titles read from lines could not be written"));
        return {};
    }
    if (lines_changed) {
        lines_changed();
    } else {
        Lines::WriteApp(folder, sources, QCoreApplication::applicationFilePath(), nullptr);
    }
    sounds.Play(Sound::Select);
    ReloadKeepingChoice();
    Changed();
    const auto engine = Hooks::EngineOf(hook.engine);
    Say(QStringLiteral("%1 is on the shelf, hooked in %2 by %3").arg(title.trimmed(), hook.process, engine == Hooks::Engine::Agent ? QStringLiteral("the Agent script ") + hook.script
                                                                                                    : engine == Hooks::Engine::Luna ? QStringLiteral("LunaHook") : QStringLiteral("Textractor")));
    return id;
}

QString HomeWindow::HookReadBy() const {
    const HookSteps& h = *wizard->hook;
    return h.way == HookSteps::WebSocket ? QStringLiteral("LINES  ·  WEBSOCKET") : h.way == HookSteps::Clipboard ? QStringLiteral("LINES  ·  CLIPBOARD")
           : QStringLiteral("HOOK  ·  ") + Hooks::LabelOf(h.EngineOf());
}

QString HomeWindow::HookProgram() const {
    const HookSteps* h = wizard->hook.get();
    return h != nullptr && h->Engine() ? h->program.name : QString();
}

int HomeWindow::HookStepCount() const {
    // A game window: the doors, the window, how it is read, (the search,) what it says, its name.
    // Lines I send: the doors, where they come from, the name.
    const HookSteps* h = wizard->hook.get();
    if (wizard->door == 1) {
        return h != nullptr && h->searched ? 6 : 5;
    }
    return 3;
}

void HomeWindow::PaintHook(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    Wizard& w = *wizard;
    HookSteps& h = *w.hook;
    const QString shared = SharedFolder();
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont value = ShelfFont(l.mono, 12.5, 0.05);
    const QFont body = ShelfFont(l.mono, 11.5, 0.03);
    const QFont name_face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 15.5);
    const QFont key_face = ShelfFont(l.display, 12, 0.16);
    const double bottom = ShelfBottom, table_width = WizardSide - 50 - ShelfLeft;
    const int way = w.step == Wizard::Hook ? HookSteps::WebSocket + std::clamp(w.row, 0, 1) : h.way;
    const bool agent = way == HookSteps::AgentScript;
    const QString engine_name = QString::fromLatin1(HookWayNames[way]).toUpper();

    // The head, as every step of the wizard has it.
    const int steps = HookStepCount();
    const int step = w.door == 1 ? (w.step == Wizard::Match ? 3 : w.step == Wizard::Script ? 4 : h.searched ? 5 : 4)
                                 : w.step == Wizard::Hook ? 2 : w.step == Wizard::Script ? 3 : w.step == Wizard::Program ? (agent ? 4 : 3) : (agent ? 5 : 4);
    double x = ShelfLeft;
    x += Put(p, small, x, 49, QStringLiteral("‹  LIBRARY"), l.muted);
    x += Put(p, small, x, 49, QStringLiteral("  ·  "), l.muted);
    Put(p, small, x, 49, QStringLiteral("ADD A TITLE"), l.ink);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    right -= Put(p, small, right, 49, QStringLiteral(" / %1  ·  ").arg(steps, 2, 10, QLatin1Char('0')), l.muted, 1);
    right -= Put(p, small, right, 49, QStringLiteral("%1").arg(step, 2, 10, QLatin1Char('0')), l.accent, 1);
    Put(p, small, right, 49, QStringLiteral("%1  ·  %2  ·  ").arg(QLatin1String(DoorLetters[w.door]), QLatin1String(DoorNames[w.door])), l.muted, 1);
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
    shelf_hits.append({QRectF(ShelfLeft, 30, 140, 30), 20, 0});

    const bool have_scripts = !h.scripts.isEmpty() && Hooks::Agent::HasRuntime(Hooks::ScriptsFolder(shared));
    const bool kit_here = way <= HookSteps::Textractor && HookKitHere(way, shared);
    const Hooks::Kit kit = Hooks::KitOf(HookKitOf(way));
    QString question, about, keys, key_label;
    switch (w.step) {
    case Wizard::Hook:
        question = QStringLiteral("FROM WHERE?");
        keys = QStringLiteral("↑ ↓   ENTER   ESC");
        key_label = QStringLiteral("GO ON");
        break;
    case Wizard::Match: {
        question = QStringLiteral("HOW IS IT READ?");
        keys = QStringLiteral("↑ ↓   ENTER   ESC");
        const QList<int> list = MatchRows(h);
        const int pick = list.value(std::clamp(w.row, 0, static_cast<int>(list.size()) - 1));
        key_label = h.fetch.Busy() && pick >= -1 ? QStringLiteral("FETCHING THE SCRIPTS") : pick >= 0 ? QStringLiteral("HOOK WITH THIS SCRIPT") : pick == -1 ? QStringLiteral("SEARCH")
                    : pick == -4 ? QStringLiteral("READ BY OCR") : QStringLiteral("HOOK");
        break;
    }
    case Wizard::Script:
        question = QStringLiteral("WHICH SCRIPT?");
        keys = have_scripts ? QStringLiteral("TYPE TO SEARCH   ↑ ↓   F5 NEWEST   ENTER   ESC") : QStringLiteral("ENTER   ESC");
        key_label = h.fetch.Busy() ? QStringLiteral("FETCHING") : have_scripts ? QStringLiteral("USE THIS SCRIPT") : QStringLiteral("FETCH THE SCRIPTS");
        break;
    case Wizard::Program:
        question = QStringLiteral("WHICH PROGRAM?");
        keys = QStringLiteral("↑ ↓   ENTER   ESC");
        key_label = h.fetch.Busy() ? QStringLiteral("FETCHING") : kit_here ? QStringLiteral("HOOK THIS PROGRAM") : QStringLiteral("FETCH %1  ·  %2").arg(agent ? QStringLiteral("FRIDA") : engine_name, kit.size.toUpper());
        break;
    default:
        question = agent ? QStringLiteral("WHAT DOES IT SAY?") : QStringLiteral("WHICH IS THE DIALOGUE?");
        keys = agent ? QStringLiteral("ENTER   ESC") : QStringLiteral("↑ ↓   SPACE ONE MORE   ENTER   ESC");
        key_label = agent ? QStringLiteral("GO ON") : h.chosen.size() > 1 ? QStringLiteral("USE THESE %1 THREADS").arg(h.chosen.size()) : QStringLiteral("USE THIS THREAD");
        break;
    }
    Put(p, ShelfFont(l.display, 27, 0.02), ShelfLeft, ShelfTop + 44, question, l.ink);

    // The one filled thing: the key that goes on.
    const QRectF key(WizardSide, bottom - 44, ShelfRight - WizardSide, 44);
    p.fillRect(key, l.ink);
    Put(p, key_face, key.center().x(), key.center().y() + 4.6, Paint::Fit(key_label, key_face, key.width() - 24), l.ground, 2);
    shelf_hits.append({key, 22, 0});

    const auto facts = [&](double y, const QList<QPair<QString, QString>>& rows) {
        for (const auto& row : rows) {
            p.fillRect(QRectF(WizardSide, y - 19, ShelfRight - WizardSide, 1), l.rule);
            Put(p, tiny, WizardSide, y, row.first, l.muted);
            const QFont said = HasJapanese(row.second) ? ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 14) : value;
            Put(p, said, ShelfRight, y, Paint::Fit(row.second, said, ShelfRight - WizardSide - 96), l.ink, 1);
            y += 30;
        }
        p.fillRect(QRectF(WizardSide, y - 19, ShelfRight - WizardSide, 1), l.rule);
        return y;
    };
    // Numbered rows between hairlines; `first` is the first one shown of a long list.
    struct Row {
        QString left, right;
        bool marked;
    };
    const auto rows = [&](double y, const QList<Row>& list, int first, int chosen, bool japanese_left) {
        p.fillRect(QRectF(ShelfLeft, y - 22, table_width, 1), l.rule);
        for (int i = 0; i < list.size(); ++i) {
            const int index = first + i;
            const bool on = index == chosen;
            if (on) {
                p.fillRect(QRectF(ShelfLeft, y - 21, 3, 33), l.accent);
            }
            Put(p, tiny, ShelfLeft + 16, y - 1, QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0')), on ? l.ink : l.muted);
            const QColor ink = on || list[i].marked ? l.ink : Faded(l.ink, 0.62);
            const double room = table_width - 56 - 330;
            if (japanese_left || HasJapanese(list[i].left)) {
                Put(p, name_face, ShelfLeft + 56, y, Paint::Fit(list[i].left, name_face, room), ink);
            } else {
                const QFont face = ShelfFont(l.display, 11.5, 0.05);
                Put(p, face, ShelfLeft + 56, y - 1, Paint::Fit(list[i].left.toUpper(), face, room), ink);
            }
            double end = ShelfLeft + table_width;
            if (list[i].marked) {
                // Chosen as the dialogue: a small square in the accent at the row's end.
                p.fillRect(QRectF(end - 7, y - 9, 7, 7), l.accent);
                end -= 18;
            }
            Put(p, value, end, y - 1, Paint::Fit(list[i].right, value, 310), on ? l.ink : l.muted, 1);
            p.fillRect(QRectF(ShelfLeft, y + 12, table_width, 1), l.rule);
            shelf_hits.append({QRectF(ShelfLeft, y - 22, table_width, 34), 21, index});
            y += 34;
        }
        return y;
    };
    const QString fetching = h.fetch.Busy() ? h.fetch.Said().toUpper() + QStringLiteral(" …") : h.fetch_said;

    if (w.step == Wizard::Hook) {
        const double end = rows(WizardTop + 30, {{QString::fromLatin1(HookWayNames[3]), QStringLiteral("WEBSOCKET"), false},
                                                 {QString::fromLatin1(HookWayNames[4]), QStringLiteral("JAPANESE TEXT COPIED"), false}},
                                0, w.row, false);
        if (way == HookSteps::WebSocket) {
            Field(p, l, ShelfFont(l.mono, 19, 0.02), ShelfLeft, end + 60, table_width, w.address, QStringLiteral("ws://localhost:6677"), true);
        }
        facts(WizardTop + 27, {{QStringLiteral("READ BY"), QStringLiteral("LINES")}});
    } else if (w.step == Wizard::Match) {
        // The window's program, and every way it can be read: the scripts like it first.
        const QList<int> list = MatchRows(h);
        w.row = std::clamp(w.row, 0, std::max(0, static_cast<int>(list.size()) - 1));
        const int scripts = static_cast<int>(h.scripts.size());
        QList<Row> shown;
        for (const int pick : list) {
            if (pick >= 0) {
                const Hooks::Agent::Script& script = h.matches[pick];
                shown.append({script.name, script.id.isEmpty() ? QStringLiteral("AGENT  ·  %1").arg(script.platform) : QStringLiteral("AGENT  ·  %1").arg(script.id.toUpper()), false});
            } else if (pick == -1) {
                shown.append({QStringLiteral("Search the scripts"), h.fetch.Busy() ? QStringLiteral("FETCHING …") : have_scripts ? QString::number(scripts) : QStringLiteral("NOT FETCHED"), false});
            } else if (pick == -2) {
                shown.append({QStringLiteral("LunaHook"), QString(), false});
            } else if (pick == -3) {
                shown.append({QStringLiteral("Textractor"), QString(), false});
            } else {
                shown.append({QStringLiteral("OCR"), QString(), false});
            }
        }
        rows(WizardTop + 30, shown, 0, w.row, false);
        QList<QPair<QString, QString>> known{{QStringLiteral("PROGRAM"), h.program.name.toUpper()}};
        if (!h.program.window.isEmpty()) {
            known.append({QStringLiteral("WINDOW"), h.program.window});
        }
        known.append({QStringLiteral("SCRIPTS FOR IT"), h.fetch.Busy() ? QStringLiteral("…") : !have_scripts ? QStringLiteral("—") : QString::number(h.matches.size())});
        const double after = facts(WizardTop + 27, known);
        if (!fetching.isEmpty()) {
            Wrapped(p, body, QRectF(WizardSide, after, ShelfRight - WizardSide, key.top() - after - 10), fetching, l.ink);
        }
    } else if (w.step == Wizard::Script) {
        const bool japanese = HasJapanese(h.typed);
        Field(p, l, japanese ? ShelfFont(l.text, 22) : ShelfFont(l.mono, 19, 0.02), ShelfLeft, WizardTop + 40, table_width, h.typed,
              QStringLiteral("SEARCH: A NAME, AN ID, A CONSOLE"), true);
        double y = WizardTop + 104;
        const int shown = 9;
        if (!have_scripts) {
            Put(p, ShelfFont(l.display, 19, 0.04), ShelfLeft, y + 16, QStringLiteral("NOT FETCHED"), l.muted);
        } else {
            Put(p, tiny, ShelfLeft, y - 30, h.found.isEmpty() ? QStringLiteral("NONE") : QStringLiteral("%1 OF %2").arg(h.found.size()).arg(h.scripts.size()), l.muted);
            const int first = std::clamp(w.row - shown / 2, 0, std::max(0, static_cast<int>(h.found.size()) - shown));
            QList<Row> list;
            for (int i = first; i < std::min<int>(h.found.size(), first + shown); ++i) {
                list.append({h.found[i].name, h.found[i].id.isEmpty() ? h.found[i].platform : QStringLiteral("%1  ·  %2").arg(h.found[i].platform, h.found[i].id.toUpper()), false});
            }
            if (!list.isEmpty()) {
                rows(y, list, first, w.row, false);
            }
        }
        QList<QPair<QString, QString>> known{{QStringLiteral("READ BY"), QStringLiteral("HOOK  ·  AGENT")}};
        if (have_scripts && w.row < h.found.size()) {
            const Hooks::Agent::Script& script = h.found[w.row];
            known.append({QStringLiteral("RUNS IN"), script.runs_on.isEmpty() ? script.platform : script.runs_on.toUpper()});
            known.append({QStringLiteral("GAME'S VERSION"), script.version.isEmpty() ? QStringLiteral("NOT SAID") : script.version.toUpper()});
            known.append({QStringLiteral("WRITTEN BY"), script.author.isEmpty() ? QStringLiteral("NOT SAID") : script.author.toUpper()});
        }
        const qint64 fetched = Hooks::ScriptsFetched(shared);
        known.append({QStringLiteral("FETCHED"), fetched > 0 ? QDateTime::fromMSecsSinceEpoch(fetched).toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("NOT YET")});
        const double after = facts(WizardTop + 27, known);
        double said_at = after;
        if (have_scripts && w.row < h.found.size()) {
            said_at += Wrapped(p, tiny, QRectF(WizardSide, after, ShelfRight - WizardSide, 40), h.found[w.row].file, l.muted) + 12;
        }
        said_at += Wrapped(p, tiny, QRectF(WizardSide, said_at, ShelfRight - WizardSide, 40), Hooks::Agent::Credit, l.muted) + 12;
        if (!fetching.isEmpty()) {
            Wrapped(p, body, QRectF(WizardSide, said_at, ShelfRight - WizardSide, key.top() - said_at - 10), fetching, l.ink);
        }
    } else if (w.step == Wizard::Program) {
        const int shown = 11;
        const int first = std::clamp(w.row - shown / 2, 0, std::max(0, static_cast<int>(h.programs.size()) - shown));
        QList<Row> list;
        for (int i = first; i < std::min<int>(h.programs.size(), first + shown); ++i) {
            const Hooks::Program& program = h.programs[i];
            list.append({ProgramShown(program), QStringLiteral("%1%2  ·  %3").arg(program.window.isEmpty() ? QString() : program.name.toUpper() + QStringLiteral("  ·  "),
                                                                                  program.is32 ? QStringLiteral("32-BIT") : QStringLiteral("64-BIT")).arg(program.pid), false});
        }
        if (list.isEmpty()) {
            Put(p, ShelfFont(l.display, 19, 0.04), ShelfLeft, WizardTop + 60, QStringLiteral("NO PROGRAM WITH A WINDOW IS RUNNING"), l.muted);
        } else {
            rows(WizardTop + 30, list, first, w.row, false);
        }
        QList<QPair<QString, QString>> known{{QStringLiteral("READ BY"), QStringLiteral("HOOK  ·  ") + Hooks::LabelOf(h.EngineOf())}};
        if (agent) {
            known.append({QStringLiteral("SCRIPT"), h.script.name.toUpper()});
            known.append({QStringLiteral("RUNS IN"), h.script.runs_on.isEmpty() ? h.script.platform : h.script.runs_on.toUpper()});
        }
        known.append({QStringLiteral("ITS FILES"), kit_here ? QStringLiteral("HERE") : QStringLiteral("NOT FETCHED YET")});
        known.append({QStringLiteral("PROGRAMS"), QString::number(h.programs.size())});
        const double after = facts(WizardTop + 27, known);
        if (!fetching.isEmpty()) {
            Wrapped(p, body, QRectF(WizardSide, after, ShelfRight - WizardSide, key.top() - after - 10), fetching, l.ink);
        }
    } else {
        // What the hook says now, thread by thread.
        const QList<Hooks::Thread>& threads = h.runner.Threads();
        w.row = std::clamp(w.row, 0, std::max(0, static_cast<int>(threads.size()) - 1));
        const int shown = 11;
        const int first = std::clamp(w.row - shown / 2, 0, std::max(0, static_cast<int>(threads.size()) - shown));
        QList<Row> list;
        for (int i = first; i < std::min<int>(threads.size(), first + shown); ++i) {
            const Hooks::Thread& thread = threads[i];
            QString latest = thread.latest;
            latest.replace(QLatin1Char('\n'), QLatin1Char(' '));
            list.append({latest.isEmpty() ? QStringLiteral("—") : latest,
                         QStringLiteral("%1  ·  %2 %3").arg(thread.name.toUpper()).arg(thread.count).arg(thread.count == 1 ? QStringLiteral("LINE") : QStringLiteral("LINES")),
                         h.chosen.contains(thread.key)});
        }
        // A script that is not for this program says so and stops: its words, not a wait.
        const bool failed = h.runner.Running() && !h.runner.Attached() && !h.runner.Fault().isEmpty();
        if (list.isEmpty()) {
            Put(p, ShelfFont(l.display, 19, 0.04), ShelfLeft, WizardTop + 60,
                !h.runner.Running() ? QStringLiteral("THE HOOK ENDED") : h.runner.Attached() ? QStringLiteral("NOTHING SAID YET")
                : failed ? (agent ? QStringLiteral("THE SCRIPT STOPPED") : QStringLiteral("THE ENGINE REFUSED")) : QStringLiteral("HOOKING"), l.muted);
            Wrapped(p, body, QRectF(ShelfLeft, WizardTop + 78, table_width, 90),
                    !h.runner.Running() || failed ? h.runner.Fault().toUpper() : QString(), failed ? l.ink : l.muted);
        } else {
            rows(WizardTop + 30, list, first, w.row, true);
        }
        QList<QPair<QString, QString>> known{{QStringLiteral("READ BY"), QStringLiteral("HOOK  ·  ") + Hooks::LabelOf(h.EngineOf())},
                                             {QStringLiteral("PROGRAM"), h.program.name.toUpper()},
                                             {QStringLiteral("THE HOOK"), h.runner.Status().section(QStringLiteral("  ·  "), 0, 0).toUpper()}};
        if (agent) {
            known.insert(1, {QStringLiteral("SCRIPT"), h.script.name.toUpper()});
        } else {
            known.append({QStringLiteral("THREADS"), QString::number(threads.size())});
        }
        const double after = facts(WizardTop + 27, known);
        // What the engine and the script themselves say, the newest last.
        Put(p, tiny, WizardSide, after + 12, agent ? QStringLiteral("THE SCRIPT SAYS") : QStringLiteral("THE ENGINE SAYS"), l.muted);
        const QStringList said = h.runner.Said().mid(std::max<qsizetype>(0, h.runner.Said().size() - 5));
        Wrapped(p, tiny, QRectF(WizardSide, after + 26, ShelfRight - WizardSide, key.top() - after - 40),
                said.isEmpty() ? QStringLiteral("NOTHING YET") : said.join(QLatin1Char('\n')).toUpper(), h.runner.Fault().isEmpty() ? l.muted : l.ink);
    }

    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    Put(p, tiny, ShelfRight, 674, keys, l.muted, 1);
}
