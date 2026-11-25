// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QFileInfo>
#include <QDirIterator>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QApplication>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QPalette>
#include <QJsonArray>
#include <QTimer>
#include <QTextBrowser>
#include <QTextLayout>
#include <QThreadPool>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "reader/qt/host.h"
#include "reader/core/engine.h"
#include "reader/desktop/anki.h"
#include "reader/desktop/card_builder.h"
#include "reader/desktop/explain.h"
#include "reader/qt/lookup_flow.h"
#include "reader/qt/reader_crt_page.h"
#include "reader/qt/reader_dialogs.h"
#include "reader/qt/choice_preview.h"
#include "reader/qt/lookup_popup.h"
#include "reader/desktop/popup_model.h"
#include "reader/qt/popup_web.h"
#include "reader/qt/reader_overlay.h"
#include "reader/qt/reader_session.h"
#include "reader/core/console.h"

namespace ReaderUi {

using ReaderApp::ReadingCounters;

namespace {

QString Tr(const char* text) {
    return QCoreApplication::translate("ReaderUi", text);
}

/// Runs `work` on a thread of its own while the window stays alive, and returns when it is
/// done. For the few things that take a moment: asking Anki, importing a dictionary.
void RunBlocking(const std::function<void()>& work) {
    QEventLoop loop;
    QThreadPool pool;
    pool.setMaxThreadCount(1);
    pool.start([&work, &loop] {
        work();
        QMetaObject::invokeMethod(&loop, [&loop] { loop.quit(); }, Qt::QueuedConnection);
    });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    pool.waitForDone();
}

QString Number(qint64 value) {
    return QLocale().toString(value);
}

QString Speed(const ReadingCounters& counters) {
    const std::optional<double> speed = counters.CharactersPerHour();
    if (!speed || counters.active_ms < ReaderApp::SessionSummary::SpeedAfterMs) {
        return QStringLiteral("–");
    }
    return Number(static_cast<qint64>(*speed / 10.0 + 0.5) * 10) + QStringLiteral("/h");
}

QString When(qint64 epoch_ms) {
    return QDateTime::fromMSecsSinceEpoch(epoch_ms).toString(QStringLiteral("yyyy/MM/dd HH:mm"));
}

// ---- A line of text whose words can be clicked ----

/// Shows one line of the backlog as text of the window's own; a click on a character says
/// which one, and a range of it can be marked.
class LineView : public QWidget {
public:
    explicit LineView(QWidget* parent) : QWidget(parent) {
        QFont big = font();
        big.setFamilies(Style::JapaneseFamilies());
        big.setPointSizeF(big.pointSizeF() * 1.5);
        setFont(big);
        setCursor(Qt::IBeamCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMinimumHeight(fontMetrics().height() * 3);
    }

    /// The character clicked: its index in the line's text and where it is in this view.
    std::function<void(int offset, const QRect& where)> on_click;

    void SetLine(const QString& text_) {
        text = text_;
        mark_first = -1;
        mark_last = -1;
        Relayout();
    }

    void SetMark(int first, int last) {
        mark_first = first;
        mark_last = last;
        update();
    }

    [[nodiscard]] QSize sizeHint() const override {
        return QSize(400, std::max(static_cast<int>(text_height) + 8, minimumHeight()));
    }

protected:
    void resizeEvent(QResizeEvent*) override {
        Relayout();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QList<QTextLayout::FormatRange> selections;
        if (mark_first >= 0 && mark_last >= mark_first) {
            QTextLayout::FormatRange range;
            range.start = mark_first;
            range.length = mark_last - mark_first + 1;
            range.format.setBackground(palette().highlight());
            range.format.setForeground(palette().highlightedText());
            selections.append(range);
        }
        painter.setPen(palette().color(QPalette::Text));
        layout.draw(&painter, QPointF(4, 4), selections);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !on_click) {
            return;
        }
        const QPointF point = event->position() - QPointF(4, 4);
        for (int i = 0; i < layout.lineCount(); ++i) {
            const QTextLine line = layout.lineAt(i);
            if (point.y() < line.y() || point.y() >= line.y() + line.height()) {
                continue;
            }
            if (point.x() < 0 || point.x() > line.naturalTextWidth()) {
                return;
            }
            const int offset = line.xToCursor(point.x(), QTextLine::CursorOnCharacter);
            if (offset < 0 || offset >= text.size()) {
                return;
            }
            const qreal left = line.cursorToX(offset);
            const qreal right = line.cursorToX(offset + 1);
            on_click(offset, QRectF(left + 4, line.y() + 4, std::max(right - left, 1.0),
                                    line.height())
                                 .toAlignedRect());
            return;
        }
    }

private:
    void Relayout() {
        // A line break inside the text is kept as one character, so that offsets stay those
        // of the line's text.
        QString shown = text;
        shown.replace(u'\n', QChar::LineSeparator);
        layout.setText(shown);
        layout.setFont(font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal y = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(std::max(width() - 8, 40));
            line.setPosition(QPointF(0, y));
            y += line.height();
        }
        layout.endLayout();
        text_height = y;
        updateGeometry();
        update();
    }

    QString text;
    QTextLayout layout;
    qreal text_height{};
    int mark_first{-1};
    int mark_last{-1};
};

// ---- A line explained ----

QString ExplanationsFile() {
    return ReaderSession::DataDirectory() + QStringLiteral("/explanations.json");
}

/// The line in English with its grammar, asked of Claude Code unless it was asked before;
/// the English is then the line's {translation} on a card.
void Explain(QWidget* parent, const ReaderApp::PresentedLine& presented) {
    auto& session = ReaderSession::Instance();
    QDialog dialog(parent);
    dialog.setWindowTitle(Tr("Explain"));
    dialog.resize(720, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* line = new QLabel(presented.line.text);
    QFont line_font = line->font();
    line_font.setFamilies(Style::JapaneseFamilies());
    line_font.setPointSizeF(line_font.pointSizeF() * 1.35);
    line->setFont(line_font);
    line->setWordWrap(true);
    line->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(line);
    auto* view = new QTextBrowser;
    QFont view_font = view->font();
    view_font.setFamilies(Style::JapaneseFamilies());
    view->setFont(view_font);
    view->setOpenLinks(false);
    layout->addWidget(view, 1);
    auto* buttons = new QHBoxLayout;
    auto* again = new QPushButton(Tr("Ask again"));
    auto* card = new QPushButton(Tr("Add card with translation"));
    auto* close = new QPushButton(Tr("Close"));
    buttons->addWidget(again);
    buttons->addStretch(1);
    buttons->addWidget(card);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    card->setEnabled(false);

    ReaderApp::Explain::Request request;
    request.line = presented.line.text;
    request.title = session.GameTitle();
    if (presented.backlog_id >= 0 && !session.GameKey().isEmpty()) {
        try {
            const qint64 before = presented.first_backlog_id >= 0 ? presented.first_backlog_id
                                                                   : presented.backlog_id;
            for (const ReaderApp::BacklogEntry& entry :
                 session.Database().RecentLines(session.GameKey(), 3, before)) {
                request.before.prepend(entry.line.text);
            }
        } catch (const std::exception& error) {
            LOG_WARNING(Frontend, "[Reader] The lines before an explanation: {}", error.what());
        }
    }
    ReaderApp::Explain::Setup setup;
    setup.command = session.Settings().ExplainCommand();
    setup.model = session.Settings().ExplainModel();

    const auto show = [view, card, again](const ReaderApp::Explain::Answer& answer) {
        view->setHtml(ReaderApp::Explain::ToHtml(answer));
        card->setEnabled(answer.error.isEmpty());
        again->setEnabled(true);
    };
    const auto ask = [&dialog, view, again, card, request, setup, show] {
        view->setHtml(QStringLiteral("<p>") + Tr("Asking…") + QStringLiteral("</p>"));
        again->setEnabled(false);
        card->setEnabled(false);
        QPointer<QDialog> alive(&dialog);
        const QString service = ExplainService() ? ExplainService()() : QString();
        const QString game_id = ReaderSession::Instance().GameKey();
        QThreadPool::globalInstance()->start([alive, request, setup, show, service, game_id] {
            // The user's service first, where there is one (the same answer on every device);
            // Claude Code on this computer when it does not answer.
            ReaderApp::Explain::Answer answer;
            if (!service.isEmpty()) {
                answer = ReaderApp::Explain::AskService(service, request, game_id);
            }
            if (service.isEmpty() || !answer.error.isEmpty()) {
                const ReaderApp::Explain::Answer here = ReaderApp::Explain::Ask(setup, request);
                if (here.error.isEmpty() || service.isEmpty()) {
                    answer = here;
                }
            }
            ReaderApp::Explain::Cache(ExplanationsFile()).Keep(request.line, answer);
            if (alive) {
                QMetaObject::invokeMethod(alive.data(), [show, answer] { show(answer); },
                                          Qt::QueuedConnection);
            }
        });
    };
    if (const auto kept = ReaderApp::Explain::Cache(ExplanationsFile()).Of(request.line)) {
        show(*kept);
    } else {
        ask();
    }
    LookupFlow flow(&dialog);
    flow.notify = [&dialog](const QString& text) { dialog.setWindowTitle(text); };
    QObject::connect(again, &QPushButton::clicked, &dialog, ask);
    QObject::connect(card, &QPushButton::clicked, &dialog,
                     [&flow, presented] { flow.MineSentence(presented); });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    Style::DressDialog(&dialog);
    dialog.exec();
    flow.Close();
}

// ---- Backlog ----

constexpr int BacklogPage = 200;

void Backlog(QWidget* parent) {
    auto& session = ReaderSession::Instance();
    auto& database = session.Database();

    QDialog dialog(parent);
    dialog.setWindowTitle(Tr("Backlog"));
    dialog.resize(820, 620);
    auto* layout = new QVBoxLayout(&dialog);

    auto* top = new QHBoxLayout;
    auto* games = new QComboBox;
    QString current_game = session.GameKey();
    try {
        for (const ReaderApp::GameTotals& game : database.AllGameTotals()) {
            games->addItem(game.title, game.game);
        }
    } catch (const std::exception& error) {
        LOG_ERROR(Frontend, "[Reader] Backlog: {}", error.what());
    }
    if (!current_game.isEmpty() && games->findData(current_game) < 0) {
        games->insertItem(0, session.GameTitle(), current_game);
    }
    if (!current_game.isEmpty()) {
        games->setCurrentIndex(games->findData(current_game));
    }
    auto* search = new QLineEdit;
    search->setPlaceholderText(Tr("Search"));
    search->setClearButtonEnabled(true);
    top->addWidget(games, 1);
    top->addWidget(search, 2);
    layout->addLayout(top);

    auto* list = new QListWidget;
    QFont list_font = list->font();
    list_font.setFamilies(Style::JapaneseFamilies());
    list->setFont(list_font);
    list->setWordWrap(true);
    list->setAlternatingRowColors(true);
    // Several lines chosen (Shift or Ctrl) make one card of the exchange.
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(list, 1);

    auto* speaker = new QLabel;
    layout->addWidget(speaker);
    auto* view = new LineView(&dialog);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(view);
    scroll->setMinimumHeight(view->minimumHeight() + 12);
    scroll->setMaximumHeight(220);
    layout->addWidget(scroll);

    auto* status = new QLabel(Tr("Click a word to look it up."));
    layout->addWidget(status);
    auto* buttons = new QHBoxLayout;
    auto* older = new QPushButton(Tr("Older lines"));
    auto* card = new QPushButton(Tr("Add sentence card"));
    auto* explain = new QPushButton(Tr("Explain"));
    auto* close = new QPushButton(Tr("Close"));
    buttons->addWidget(older);
    buttons->addStretch(1);
    buttons->addWidget(explain);
#ifdef READER_NO_EXPLAIN // a published Furoshiki has no Explain
    explain->hide();
#endif
    buttons->addWidget(card);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    auto entries = std::make_shared<QList<ReaderApp::BacklogEntry>>();
    LookupFlow flow(&dialog);
    flow.mark = [view, entries, list](qint64 backlog_id, int first, int last) {
        const int row = list->currentRow();
        if (first < 0 || (row >= 0 && row < entries->size() && entries->at(row).id == backlog_id)) {
            view->SetMark(first, last);
        }
    };
    flow.notify = [status](const QString& text) { status->setText(text); };
    flow.open_dictionaries = [&dialog] { RunDictionariesDialog(&dialog); };

    const auto game_key = [games] { return games->currentData().toString(); };
    const auto show_entries = [list, entries] {
        list->clear();
        for (const ReaderApp::BacklogEntry& entry : std::as_const(*entries)) {
            QString text = entry.line.text;
            text.replace(u'\n', u' ');
            if (!entry.line.speaker.isEmpty()) {
                text = entry.line.speaker + QStringLiteral("： ") + text;
            }
            list->addItem(text);
        }
    };
    const auto reload = [&, show_entries] {
        flow.Close();
        entries->clear();
        const QString game = game_key();
        if (!game.isEmpty()) {
            try {
                const QString query = search->text().trimmed();
                *entries = query.isEmpty() ? database.RecentLines(game, BacklogPage)
                                           : database.SearchLines(game, query, BacklogPage);
            } catch (const std::exception& error) {
                status->setText(QString::fromUtf8(error.what()));
            }
        }
        show_entries();
        older->setEnabled(search->text().trimmed().isEmpty() && entries->size() >= BacklogPage);
        if (!entries->isEmpty()) {
            list->setCurrentRow(0);
        } else {
            view->SetLine({});
            speaker->clear();
        }
    };
    const auto selected = [list, entries]() -> std::optional<ReaderApp::PresentedLine> {
        const int row = list->currentRow();
        if (row < 0 || row >= entries->size()) {
            return std::nullopt;
        }
        const ReaderApp::BacklogEntry& entry = entries->at(row);
        // A line of this run still has its place in the game's audio.
        if (auto recent = ReaderSession::Instance().RecentLine(entry.id)) {
            return recent;
        }
        ReaderApp::PresentedLine presented;
        presented.line = entry.line;
        presented.backlog_id = entry.id;
        return presented;
    };

    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&, selected](int) {
        flow.Close();
        const auto presented = selected();
        view->SetLine(presented ? presented->line.text : QString());
        speaker->setText(presented ? presented->line.speaker : QString());
        card->setEnabled(presented.has_value());
    });
    view->on_click = [&, selected](int offset, const QRect& where) {
        const auto presented = selected();
        if (!presented) {
            return;
        }
        LookupFlow::Anchor anchor;
        anchor.owner = &dialog;
        anchor.bounds = dialog.rect();
        const QPoint origin = view->mapTo(&dialog, QPoint(0, 0));
        anchor.keep_clear = QRect(origin + where.topLeft(), where.size()).adjusted(0, -4, 0, 4);
        anchor.near_x = origin.x() + where.left();
        flow.Lookup(*presented, offset, anchor);
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [reload] { reload(); });
    QObject::connect(games, &QComboBox::currentIndexChanged, &dialog, [reload] { reload(); });
    QObject::connect(older, &QPushButton::clicked, &dialog, [&, show_entries] {
        if (entries->isEmpty()) {
            return;
        }
        try {
            const auto more = database.RecentLines(game_key(), BacklogPage, entries->last().id);
            const int row = list->currentRow();
            entries->append(more);
            show_entries();
            list->setCurrentRow(row);
            older->setEnabled(more.size() >= BacklogPage);
        } catch (const std::exception& error) {
            status->setText(QString::fromUtf8(error.what()));
        }
    });
    // The chosen lines as one, oldest first (the list is newest first).
    const auto chosen = [list, entries, selected]() -> std::optional<ReaderApp::PresentedLine> {
        QList<int> rows;
        for (const QListWidgetItem* item : list->selectedItems()) {
            rows.append(list->row(item));
        }
        if (rows.size() < 2) {
            return selected();
        }
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        QList<ReaderApp::PresentedLine> lines;
        for (const int row : rows) {
            if (row < 0 || row >= entries->size()) {
                continue;
            }
            const ReaderApp::BacklogEntry& entry = entries->at(row);
            if (auto recent = ReaderSession::Instance().RecentLine(entry.id)) {
                lines.append(*recent);
            } else {
                ReaderApp::PresentedLine presented;
                presented.line = entry.line;
                presented.line.audio_position = -1;
                presented.backlog_id = entry.id;
                lines.append(presented);
            }
        }
        return ReaderApp::PresentedLine::Join(lines);
    };
    QObject::connect(list, &QListWidget::itemSelectionChanged, &dialog, [list, card] {
        const int count = static_cast<int>(list->selectedItems().size());
        card->setText(count > 1 ? Tr("Add card (%1 lines)").arg(count)
                                : Tr("Add sentence card"));
    });
    QObject::connect(explain, &QPushButton::clicked, &dialog, [&, chosen] {
        if (const auto presented = chosen()) {
            Explain(&dialog, *presented);
        }
    });
    QObject::connect(card, &QPushButton::clicked, &dialog, [&, chosen] {
        if (const auto presented = chosen()) {
            status->setText(Tr("Adding card…"));
            flow.MineSentence(*presented);
        }
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    reload();
    Style::DressDialog(&dialog);
    dialog.exec();
    flow.Close();
}

// ---- Statistics ----

QStringList CounterColumns(const ReadingCounters& counters) {
    return {Number(counters.characters),      Number(counters.unique_characters),
            Number(counters.reread_characters), Number(counters.lines),
            ReaderApp::SessionSummary::Duration(counters.active_ms), Speed(counters)};
}

QTreeWidget* MakeTable(const QStringList& first_columns) {
    auto* tree = new QTreeWidget;
    QStringList headers = first_columns;
    headers << Tr("Characters") << Tr("New") << Tr("Reread") << Tr("Lines") << Tr("Time")
            << Tr("Speed");
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return tree;
}

void Statistics(QWidget* parent) {
    auto& session = ReaderSession::Instance();
    auto& database = session.Database();

    QDialog dialog(parent);
    dialog.setWindowTitle(Tr("Reading statistics"));
    dialog.resize(860, 600);
    auto* layout = new QVBoxLayout(&dialog);

    auto* summary = new QLabel;
    summary->setTextFormat(Qt::PlainText);
    layout->addWidget(summary);

    auto* tabs = new QTabWidget;
    auto* days = MakeTable({Tr("Reading day")});
    auto* games = MakeTable({Tr("Game")});
    auto* sessions = MakeTable({Tr("Started"), Tr("Game")});
    tabs->addTab(days, Tr("Days"));
    tabs->addTab(games, Tr("Games"));
    tabs->addTab(sessions, Tr("Sessions"));
    layout->addWidget(tabs, 1);

    auto* paused = new QCheckBox(Tr("Pause reading timer"));
    paused->setChecked(session.Settings().StatsPaused());
    layout->addWidget(paused);

    auto* buttons = new QHBoxLayout;
    auto* delete_session = new QPushButton(Tr("Delete session"));
    auto* export_json = new QPushButton(Tr("Export JSON…"));
    auto* export_csv = new QPushButton(Tr("Export CSV…"));
    auto* strip = new QPushButton(Tr("Remove duplicate text…"));
    strip->setToolTip(Tr("Removes repeated text from the counts"));
    auto* clear_game = new QPushButton(Tr("Clear this game"));
    auto* clear_all = new QPushButton(Tr("Clear everything"));
    auto* close = new QPushButton(Tr("Close"));
    buttons->addWidget(delete_session);
    buttons->addWidget(export_json);
    buttons->addWidget(export_csv);
    buttons->addWidget(strip);
    buttons->addStretch(1);
    buttons->addWidget(clear_game);
    buttons->addWidget(clear_all);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    clear_game->setEnabled(session.IsActive());

    const auto refresh = [&] {
        try {
            const QString today = ReaderApp::ReadingRules::ReadingDay(
                QDateTime::currentMSecsSinceEpoch());
            const auto line = [](const QString& label, const ReadingCounters& counters) {
                return QStringLiteral("%1: %2 %3 · %4 · %5\n")
                    .arg(label, Number(counters.characters), Tr("characters"),
                         ReaderApp::SessionSummary::Duration(counters.active_ms), Speed(counters));
            };
            QString text = line(Tr("Today"), database.DayTotals(today));
            if (session.IsActive()) {
                text += line(session.GameTitle(), database.Totals(session.GameKey()));
            }
            text += line(Tr("All games"), database.Totals());
            summary->setText(text.trimmed());

            QHash<QString, QString> titles;
            games->clear();
            for (const ReaderApp::GameTotals& game : database.AllGameTotals()) {
                titles.insert(game.game, game.title);
                new QTreeWidgetItem(games, QStringList{game.title} + CounterColumns(game.counters));
            }
            days->clear();
            for (const auto& [day, counters] : database.DailyHistory({}, 30)) {
                new QTreeWidgetItem(days, QStringList{day} + CounterColumns(counters));
            }
            sessions->clear();
            for (const ReaderApp::SessionRecord& record : database.Sessions()) {
                auto* item = new QTreeWidgetItem(
                    sessions, QStringList{When(record.started),
                                          titles.value(record.game, record.game)} +
                                  CounterColumns(record.counters));
                item->setData(0, Qt::UserRole, record.id);
            }
        } catch (const std::exception& error) {
            summary->setText(QString::fromUtf8(error.what()));
        }
    };

    const QPointer<QDialog> alive(&dialog);
    // What has not been saved yet goes in first.
    const auto flush_and_refresh = [&session, alive, refresh] {
        session.Flush([alive, refresh] {
            if (alive) {
                refresh();
            }
        });
    };

    QObject::connect(paused, &QCheckBox::toggled, &dialog, [&session](bool on) {
        session.Settings().SetStatsPaused(on);
        session.OnSettingsChanged();
    });
    QObject::connect(delete_session, &QPushButton::clicked, &dialog, [&] {
        QTreeWidgetItem* item = sessions->currentItem();
        if (!item || tabs->currentWidget() != sessions) {
            tabs->setCurrentWidget(sessions);
            return;
        }
        if (QMessageBox::question(&dialog, Tr("Delete session"),
                                  Tr("Delete this session?")) !=
            QMessageBox::Yes) {
            return;
        }
        database.DeleteSession(item->data(0, Qt::UserRole).toLongLong());
        session.OnActivityCleared();
        refresh();
    });
    const auto save = [&dialog](const QString& name, const QString& filter,
                                const QByteArray& bytes) {
        const QString path = QFileDialog::getSaveFileName(&dialog, Tr("Export"), name, filter);
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            QMessageBox::warning(&dialog, Tr("Export"), Tr("Couldn't save the file. Choose a folder you can write to"));
        }
    };
    QObject::connect(export_json, &QPushButton::clicked, &dialog, [&, save] {
        save(QString::fromLatin1(Reader::ConsoleTraits::AppKey) + QStringLiteral("-activity.json"), QStringLiteral("JSON (*.json)"),
             QJsonDocument(database.ExportActivityJson()).toJson(QJsonDocument::Indented));
    });
    QObject::connect(export_csv, &QPushButton::clicked, &dialog, [&, save] {
        save(QString::fromLatin1(Reader::ConsoleTraits::AppKey) + QStringLiteral("-sessions.csv"), QStringLiteral("CSV (*.csv)"),
             database.ExportSessionsCsv().toUtf8());
    });
    const auto clear = [&](const QString& game, const QString& question) {
        if (QMessageBox::question(&dialog, Tr("Clear statistics"), question) != QMessageBox::Yes) {
            return;
        }
        // Sessions and totals only: the backlog and what counts as already read stay.
        database.ClearActivity(game);
        session.OnActivityCleared();
        refresh();
    };
    QObject::connect(clear_game, &QPushButton::clicked, &dialog, [&, clear] {
        clear(session.GameKey(), Tr("Clear statistics for this game?"));
    });
    QObject::connect(strip, &QPushButton::clicked, &dialog, [&] {
        try {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const qint64 found = database.StripOverlaps(false);
            QApplication::restoreOverrideCursor();
            if (found == 0) {
                QMessageBox::information(&dialog, Tr("Remove duplicate text"),
                                         Tr("No duplicate text found"));
                return;
            }
            if (QMessageBox::question(&dialog, Tr("Remove duplicate text"),
                                      Tr("Remove %1 duplicate characters from the counts?")
                                          .arg(Number(found))) != QMessageBox::Yes) {
                return;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            database.StripOverlaps(true);
            QApplication::restoreOverrideCursor();
        } catch (const std::exception& error) {
            QApplication::restoreOverrideCursor();
            QMessageBox::warning(&dialog, Tr("Remove duplicate text"), QString::fromUtf8(error.what()));
        }
        refresh();
    });
    QObject::connect(clear_all, &QPushButton::clicked, &dialog, [&, clear] {
        clear({}, Tr("Clear all statistics?"));
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    refresh();
    flush_and_refresh();
    Style::DressDialog(&dialog);
    dialog.exec();
}

// ---- Dictionaries ----

void Dictionaries(QWidget* parent) {
    auto& database = ReaderSession::Instance().Dictionaries();

    QDialog dialog(parent);
    dialog.setWindowTitle(Tr("Dictionaries"));
    dialog.resize(640, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(
        Tr("Words are looked up in these dictionaries, from the top. Import takes Yomitan dictionaries (.zip)."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* list = new QListWidget;
    layout->addWidget(list, 1);
    auto* buttons = new QHBoxLayout;
    // One button for both: Windows' file picker takes files or a folder, never either.
    auto* import = new QPushButton(Tr("Import…"));
    auto* import_menu = new QMenu(import);
    QAction* import_files_action = import_menu->addAction(Tr("Dictionary files (.zip)…"));
    QAction* import_folder_action = import_menu->addAction(Tr("A folder of dictionaries, and the folders in it…"));
    import->setMenu(import_menu);
    auto* starter = new QPushButton(Tr("Download Jitendex"));
    starter->setToolTip(Tr("A free Japanese-English dictionary (39 MB)"));
    auto* up = new QPushButton(Tr("Move up"));
    auto* down_button = new QPushButton(Tr("Move down"));
    auto* remove = new QPushButton(Tr("Delete"));
    auto* close = new QPushButton(Tr("Close"));
    buttons->addWidget(import);
    buttons->addWidget(starter);
    buttons->addWidget(up);
    buttons->addWidget(down_button);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    bool filling = false;
    const auto refresh = [&](qint64 select) {
        filling = true;
        list->clear();
        try {
            for (const ReaderApp::DictionaryInfo& info : database.Dictionaries()) {
                QString text = info.title;
                QStringList details;
                if (info.term_count > 0) {
                    details.append(Tr("%1 entries").arg(Number(info.term_count)));
                }
                if (info.meta_count > 0) {
                    details.append(Tr("%1 frequency/pitch entries").arg(Number(info.meta_count)));
                }
                if (!info.revision.isEmpty()) {
                    details.append(info.revision);
                }
                text += QStringLiteral(" · ") + details.join(QStringLiteral(", "));
                auto* item = new QListWidgetItem(text, list);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(info.enabled ? Qt::Checked : Qt::Unchecked);
                item->setData(Qt::UserRole, info.id);
                if (info.id == select) {
                    list->setCurrentItem(item);
                }
            }
        } catch (const std::exception& error) {
            QMessageBox::warning(&dialog, Tr("Dictionaries"), QString::fromUtf8(error.what()));
        }
        const bool none = list->count() == 0;
        if (none) {
            // Nothing installed: what to do about it, where the list would be.
            auto* empty = new QListWidgetItem(Tr("No dictionaries yet. Download Jitendex, or import Yomitan dictionaries (.zip)."), list);
            empty->setFlags(Qt::NoItemFlags);
        }
        for (QPushButton* button : {up, down_button, remove}) {
            button->setEnabled(!none);
        }
        filling = false;
    };
    const auto ids = [list] {
        QList<qint64> out;
        for (int i = 0; i < list->count(); ++i) {
            out.append(list->item(i)->data(Qt::UserRole).toLongLong());
        }
        return out;
    };
    const auto move = [&, ids](int by) {
        const int row = list->currentRow();
        const int target = row + by;
        if (row < 0 || target < 0 || target >= list->count()) {
            return;
        }
        QList<qint64> order = ids();
        order.swapItemsAt(row, target);
        database.SetOrder(order);
        refresh(order.at(target));
    };

    QObject::connect(list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* item) {
        if (!filling) {
            database.SetEnabled(item->data(Qt::UserRole).toLongLong(),
                                item->checkState() == Qt::Checked);
        }
    });
    QObject::connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
    QObject::connect(down_button, &QPushButton::clicked, &dialog, [move] { move(1); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        QListWidgetItem* item = list->currentItem();
        if (!item) {
            return;
        }
        if (QMessageBox::question(&dialog, Tr("Delete dictionary"),
                                  Tr("Delete this dictionary?")) != QMessageBox::Yes) {
            return;
        }
        const qint64 id = item->data(Qt::UserRole).toLongLong();
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        RunBlocking([&database, id] {
            try {
                database.Delete(id);
            } catch (const std::exception& error) {
                LOG_ERROR(Frontend, "[Reader] Deleting a dictionary failed: {}", error.what());
            }
        });
        QGuiApplication::restoreOverrideCursor();
        refresh(-1);
    });
    const auto import_files = [&](const QStringList& files) {
        QStringList problems;
        for (const QString& file : files) {
            QProgressDialog progress(Tr("Importing…"), Tr("Cancel"), 0, 100, &dialog);
            progress.setWindowModality(Qt::WindowModal);
            progress.setMinimumDuration(0);
            progress.setAutoClose(false);
            progress.setAutoReset(false);
            progress.setValue(0);
            std::atomic<bool> cancelled{false};
            QObject::connect(&progress, &QProgressDialog::canceled, &progress,
                             [&cancelled] { cancelled = true; });
            ReaderApp::DictionaryImporter::Result result;
            QEventLoop loop;
            QThreadPool pool;
            pool.start([&] {
                ReaderApp::DictionaryImporter importer(database);
                result = importer.Import(
                    file,
                    [&progress](int done, int total, const QString& title) {
                        QMetaObject::invokeMethod(
                            &progress,
                            [&progress, done, total, title] {
                                progress.setLabelText(Tr("Importing %1…").arg(title));
                                progress.setMaximum(std::max(total, 1));
                                progress.setValue(done);
                            },
                            Qt::QueuedConnection);
                    },
                    [&cancelled] { return !cancelled.load(); });
                QMetaObject::invokeMethod(&loop, [&loop] { loop.quit(); }, Qt::QueuedConnection);
            });
            loop.exec();
            pool.waitForDone();
            // Asked before the dialog goes: closing it says "cancelled" too, and the next
            // dictionaries of a folder were then left out.
            const bool stop = cancelled.load();
            progress.close();
            // Each import's speed, in Furoshiki's log, for whoever looks into a slow one.
            LOG_INFO(Frontend, "[Reader] Dictionary {} ({})", result.Summary().toStdString(),
                     QFileInfo(file).fileName().toStdString());
            if (!result.ok && !stop && !result.not_dictionary && !result.already_there) {
                problems.append(result.error);
            }
            if (stop) {
                break;
            }
        }
        // The dictionary engine's index of the whole library, made again for what came in.
        RunBlocking([&database] { database.Compact(); });
        refresh(-1);
        if (!problems.isEmpty()) {
            QMessageBox::warning(&dialog, Tr("Import dictionaries"), problems.join(u'\n'));
        }
    };
    QObject::connect(import_files_action, &QAction::triggered, &dialog, [&] {
        import_files(QFileDialog::getOpenFileNames(&dialog, Tr("Import dictionaries"), QString(),
                                                   Tr("Yomitan dictionaries (*.zip);;All files (*)")));
    });
    QObject::connect(import_folder_action, &QAction::triggered, &dialog, [&] {
        const QString folder = QFileDialog::getExistingDirectory(&dialog, Tr("Import a folder of dictionaries"));
        if (folder.isEmpty()) {
            return;
        }
        // Every zip in this folder and the ones in it; a zip that is no dictionary (a
        // dictionary's audio, a font) is passed by without a word (Result::not_dictionary).
        QStringList found;
        QDirIterator walk(folder, {QStringLiteral("*.zip"), QStringLiteral("*.ZIP")}, QDir::Files,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            found.append(walk.next());
        }
        found.sort();
        if (found.isEmpty()) {
            QMessageBox::information(&dialog, Tr("Import a folder of dictionaries"),
                                     Tr("There are no Yomitan dictionaries (.zip) in that folder."));
            return;
        }
        import_files(found);
    });
    QObject::connect(starter, &QPushButton::clicked, &dialog, [&] {
        QString problem;
        if (!FetchStarterDictionary(&dialog, &problem) && !problem.isEmpty()) {
            QMessageBox::warning(&dialog, Tr("Dictionaries"), Tr("Couldn't install Jitendex: %1").arg(problem));
        }
        refresh(-1);
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    refresh(-1);
    Style::DressDialog(&dialog);
    dialog.exec();
}

// ---- Anki ----

void AnkiSetup(QWidget* parent) {
    auto& settings = ReaderSession::Instance().Settings();
    using Settings = ReaderApp::ReaderSettings;

    QDialog dialog(parent);
    dialog.setWindowTitle(Tr("Anki setup"));
    dialog.resize(680, 620);
    auto* layout = new QVBoxLayout(&dialog);

    auto* steps = new QLabel(Tr("Open Anki, install the AnkiConnect add-on (in Anki: Tools, Add-ons, Get Add-ons, code 2055492159), "
                                "then press Connect to choose your deck and note type."));
    steps->setWordWrap(true);
    layout->addWidget(steps);
    auto* enabled = new QCheckBox(Tr("Add cards to Anki"));
    enabled->setChecked(settings.AnkiTransport() == QStringLiteral("ankiconnect"));
    layout->addWidget(enabled);

    auto* form = new QFormLayout;
    auto* url = new QLineEdit(settings.AnkiConnectUrl());
    auto* key = new QLineEdit(settings.AnkiConnectKey());
    key->setPlaceholderText(Tr("Optional"));
    auto* connect_row = new QHBoxLayout;
    auto* connect_button = new QPushButton(Tr("Connect"));
    auto* connection = new QLabel;
    connection->setWordWrap(true);
    connect_row->addWidget(connect_button);
    connect_row->addWidget(connection, 1);
    auto* deck = new QComboBox;
    auto* model = new QComboBox;
    deck->setEditable(true);
    model->setEditable(true);
    deck->setCurrentText(settings.AnkiDeck());
    model->setCurrentText(settings.AnkiModel());
    form->addRow(Tr("AnkiConnect address"), url);
    form->addRow(Tr("API key"), key);
    form->addRow(QString(), connect_row);
    form->addRow(Tr("Deck"), deck);
    form->addRow(Tr("Note type"), model);
    layout->addLayout(form);

    auto* fields = new QTableWidget(0, 2);
    fields->setHorizontalHeaderLabels({Tr("Field"), Tr("Content")});
    fields->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fields->horizontalHeader()->setStretchLastSection(true);
    fields->verticalHeader()->setVisible(false);
    layout->addWidget(fields, 1);
    auto* fields_empty = new QLabel(Tr("Press Connect to load your note type's fields."));
    layout->addWidget(fields_empty);
    auto* markers_hint = new QLabel(
        Tr("Each field takes markers such as {expression}, {sentence}, {screenshot} or {audio}."));
    markers_hint->setWordWrap(true);
    layout->addWidget(markers_hint);
    auto* suggest = new QPushButton(Tr("Suggest from field names"));
    auto* suggest_row = new QHBoxLayout;
    suggest_row->addWidget(suggest);
    suggest_row->addStretch(1);
    layout->addLayout(suggest_row);

    auto* form2 = new QFormLayout;
    auto* tags = new QLineEdit(settings.AnkiTags());
    form2->addRow(Tr("Tags"), tags);
    layout->addLayout(form2);
    auto* duplicates = new QCheckBox(Tr("Allow duplicate cards"));
    duplicates->setChecked(settings.AnkiAllowDuplicates());
    auto* anywhere = new QCheckBox(Tr("Check for duplicates in all decks"));
    anywhere->setChecked(settings.AnkiDuplicatesAnywhere());
    layout->addWidget(duplicates);
    layout->addWidget(anywhere);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(box);

    auto mapping = std::make_shared<QMap<QString, QString>>(
        ReaderApp::CardBuilder::ParseMapping(settings.AnkiFieldMap()));
    auto field_order = std::make_shared<QStringList>();

    const auto template_of = [fields](int row) {
        auto* combo = qobject_cast<QComboBox*>(fields->cellWidget(row, 1));
        return combo ? combo->currentText() : QString();
    };
    const auto read_table = [fields, mapping, template_of] {
        for (int row = 0; row < fields->rowCount(); ++row) {
            mapping->insert(fields->item(row, 0)->text(), template_of(row));
        }
    };
    const auto fill_table = [fields, mapping, field_order, fields_empty] {
        fields->setRowCount(0);
        QStringList names = *field_order;
        if (names.isEmpty()) {
            names = mapping->keys(); // not connected: what was saved, in no particular order
        }
        for (const QString& name : std::as_const(names)) {
            const int row = fields->rowCount();
            fields->insertRow(row);
            auto* item = new QTableWidgetItem(name);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            fields->setItem(row, 0, item);
            auto* combo = new QComboBox;
            combo->setEditable(true);
            combo->addItem(QString());
            for (const auto& [marker, description] : ReaderApp::CardBuilder::Markers()) {
                combo->addItem(u'{' + marker + u'}');
                combo->setItemData(combo->count() - 1, description, Qt::ToolTipRole);
            }
            combo->setCurrentText(mapping->value(name));
            fields->setCellWidget(row, 1, combo);
        }
        fields_empty->setVisible(fields->rowCount() == 0);
    };
    const auto client = [url, key] {
        return ReaderApp::AnkiConnectClient(url->text().trimmed(), key->text(), 4000, 15000);
    };
    const auto load_fields = [&, read_table, fill_table, client] {
        const QString name = model->currentText().trimmed();
        if (name.isEmpty()) {
            return;
        }
        read_table();
        QStringList names;
        QString problem;
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        RunBlocking([&names, &problem, name, client] {
            try {
                auto anki = client();
                names = anki.FieldNames(name);
            } catch (const ReaderApp::AnkiError& error) {
                problem = error.Message();
            }
        });
        QGuiApplication::restoreOverrideCursor();
        if (!problem.isEmpty()) {
            connection->setText(problem);
            return;
        }
        *field_order = names;
        fill_table();
    };
    QObject::connect(connect_button, &QPushButton::clicked, &dialog, [&, client, load_fields] {
        QStringList decks;
        QStringList models;
        QString problem;
        int version = 0;
        connection->setText(Tr("Connecting…"));
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        RunBlocking([&decks, &models, &problem, &version, client] {
            try {
                auto anki = client();
                version = anki.Version();
                decks = anki.DeckNames();
                models = anki.ModelNames();
            } catch (const ReaderApp::AnkiError& error) {
                problem = error.Message();
            }
        });
        QGuiApplication::restoreOverrideCursor();
        if (!problem.isEmpty()) {
            connection->setText(problem);
            return;
        }
        connection->setText(Tr("Connected (AnkiConnect %1): %2 decks, %3 note types")
                                .arg(version)
                                .arg(decks.size())
                                .arg(models.size()));
        const QString chosen_deck = deck->currentText();
        const QString chosen_model = model->currentText();
        decks.sort(Qt::CaseInsensitive);
        models.sort(Qt::CaseInsensitive);
        {
            const QSignalBlocker block_deck(deck);
            const QSignalBlocker block_model(model);
            deck->clear();
            deck->addItems(decks);
            deck->setCurrentText(chosen_deck);
            model->clear();
            model->addItems(models);
            model->setCurrentText(chosen_model);
        }
        load_fields();
    });
    QObject::connect(model, &QComboBox::activated, &dialog, [load_fields] { load_fields(); });
    QObject::connect(suggest, &QPushButton::clicked, &dialog, [&, fill_table] {
        QStringList names;
        for (int row = 0; row < fields->rowCount(); ++row) {
            names.append(fields->item(row, 0)->text());
        }
        const auto suggested = ReaderApp::CardBuilder::SuggestMapping(names);
        for (auto it = suggested.constBegin(); it != suggested.constEnd(); ++it) {
            mapping->insert(it.key(), it.value());
        }
        fill_table();
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&, read_table] {
        read_table();
        // Only the fields of the chosen note type are kept.
        QMap<QString, QString> kept;
        for (int row = 0; row < fields->rowCount(); ++row) {
            const QString name = fields->item(row, 0)->text();
            kept.insert(name, mapping->value(name));
        }
        settings.SetValue(Settings::KeyAnkiTransport, enabled->isChecked()
                                                          ? QStringLiteral("ankiconnect")
                                                          : QStringLiteral("none"));
        settings.SetValue(Settings::KeyAnkiConnectUrl, url->text().trimmed());
        settings.SetValue(Settings::KeyAnkiConnectKey, key->text());
        settings.SetValue(Settings::KeyAnkiDeck, deck->currentText().trimmed());
        settings.SetValue(Settings::KeyAnkiModel, model->currentText().trimmed());
        settings.SetValue(Settings::KeyAnkiFields, ReaderApp::CardBuilder::MappingToJson(kept));
        settings.SetValue(Settings::KeyAnkiTags, tags->text().trimmed());
        settings.SetValue(Settings::KeyAnkiDuplicates, duplicates->isChecked());
        settings.SetValue(Settings::KeyAnkiDuplicatesAnywhere, anywhere->isChecked());
        // The note type's first field is what Anki tells notes apart by: recorded while it
        // is known, so that asking "does Anki have this word" needs no extra question.
        if (!field_order->isEmpty()) {
            settings.SetValue(Settings::KeyAnkiKeyField,
                              Settings::KeyFieldValue(model->currentText().trimmed(),
                                                      field_order->first()));
        }
        settings.Sync();
        Miner::Forget();
        dialog.accept();
    });

    fill_table();
    Style::DressDialog(&dialog);
    dialog.exec();
}


// ---- Settings ----

void ReaderSettingsDialog(QWidget* parent) {
    auto& session = ReaderSession::Instance();
    auto& settings = session.Settings();
    using Settings = ReaderApp::ReaderSettings;

    // One page per section, the sections in a list beside them: each page is short.
    QDialog dialog(parent);
    dialog.setWindowTitle(Tr("Reader settings"));
    auto* outer = new QVBoxLayout(&dialog);
    auto* body = new QHBoxLayout;
    auto* sections = new QListWidget;
    sections->setFixedWidth(170);
    auto* pages = new QStackedWidget;
    body->addWidget(sections);
    body->addWidget(pages, 1);
    outer->addLayout(body, 1);
    QObject::connect(sections, &QListWidget::currentRowChanged, pages,
                     &QStackedWidget::setCurrentIndex);

    QList<std::function<void()>> savers;
    const auto section = [sections, pages](const QString& title) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        layout->addStretch(1);
        pages->addWidget(page);
        sections->addItem(title);
        return form;
    };
    const auto check = [&](QFormLayout* form, const QString& label, const QString& key_name,
                           bool value) {
        auto* box = new QCheckBox(label);
        box->setChecked(value);
        form->addRow(box);
        savers.append([&settings, box, key_name] { settings.SetValue(key_name, box->isChecked()); });
        return box;
    };
    const auto spin = [&](QFormLayout* form, const QString& label, const QString& key_name,
                          int value, int low, int high, const QString& suffix) {
        auto* box = new QSpinBox;
        box->setRange(low, high);
        box->setValue(value);
        box->setSuffix(suffix);
        form->addRow(label, box);
        savers.append([&settings, box, key_name] { settings.SetValue(key_name, box->value()); });
        return box;
    };
    const auto choice = [&](QFormLayout* form, const QString& label, const QString& key_name,
                            const QList<std::pair<QString, QString>>& options,
                            const QString& value) {
        auto* box = new QComboBox;
        for (const auto& [text, data] : options) {
            box->addItem(text, data);
        }
        box->setCurrentIndex(std::max(0, box->findData(value)));
        form->addRow(label, box);
        savers.append(
            [&settings, box, key_name] { settings.SetValue(key_name, box->currentData()); });
        return box;
    };

    // The controller's buttons the reader may have for its own, as the console names them
    // (console.h); one no game is told of only for the menu's.
    const auto buttons = [](bool for_menu) {
        QList<std::pair<QString, QString>> options{{Tr("None"), QStringLiteral("-1")}};
        for (const auto& button : Reader::ConsoleTraits::ReaderButtons) {
            if (for_menu || !button.menu_only) {
                options.append({Tr(button.name), QString::number(button.number)});
            }
        }
        return options;
    };

    QFormLayout* text = section(Tr("Lookup"));
    check(text, Tr("Click to look up"), Settings::KeyScreenText,
          settings.ScreenText());
    check(text, Tr("Tap to advance"),
          Settings::KeyTouchPlay, settings.TouchPlay());
    choice(text, Tr("Look up on hover"), Settings::KeyHoverLookup,
           {{Tr("Off"), QStringLiteral("off")},
            {Tr("While holding Shift"), QStringLiteral("shift")},
            {Tr("Always"), QStringLiteral("always")}},
           settings.HoverLookup());
    spin(text, Tr("Maximum popup height"), Settings::KeyPopupHeight,
         settings.PopupHeightPercent(), 10, 80, Tr(" % of screen"));
    spin(text, Tr("Auto-close popup after"), Settings::KeyPopupAutoHide,
         settings.PopupAutoHideSeconds(), 0, 120, Tr(" s (0: never)"));
    check(text, Tr("Close popup when the mouse leaves"),
          Settings::KeyPopupHideOnExit, settings.PopupHideOnExit());
    choice(text, Tr("Highlight looked-up word"), Settings::KeyWordMark,
           {{Tr("Outline"), QStringLiteral("outline")},
            {Tr("Highlight"), QStringLiteral("highlight")},
            {Tr("None"), QStringLiteral("none")}},
           settings.WordMark());
    check(text, Tr("Outline clickable text"), Settings::KeyScreenTextOutline,
          settings.ScreenTextOutline());

    // The popup: its look and what it shows, beside a demo of it drawn as it will be (the
    // maintainer, 2026-11-10: "the popup settings from within the client should also show a
    // preview of what the popup looks like with a small demo").
    auto* popup_page = new QWidget;
    auto* popup_row = new QHBoxLayout(popup_page);
    auto* look = new QFormLayout;
    auto* look_column = new QVBoxLayout;
    look_column->addLayout(look);
    look_column->addStretch(1);
    popup_row->addLayout(look_column, 3);
    pages->addWidget(popup_page);
    sections->addItem(Tr("Popup"));
    QSpinBox* scale_box = spin(look, Tr("Popup text size"), Settings::KeyPopupScale, settings.PopupScale(), 60, 200,
         QStringLiteral(" %"));
    QComboBox* theme_box = choice(look, Tr("Popup theme"), Settings::KeyPopupTheme,
           {{Tr("Match home theme"), QStringLiteral("auto")},
            {Tr("Console colours"), QStringLiteral("console")},
            {Tr("Furoshiki glass"), QStringLiteral("furoshiki")},
            {Tr("Yomitan dark"), QStringLiteral("yomitan-dark")},
            {Tr("Paper"), QStringLiteral("paper")},
            {Tr("Paper dark"), QStringLiteral("paper-dark")},
            {Tr("Solarized dark"), QStringLiteral("solarized-dark")},
            {Tr("Solarized light"), QStringLiteral("solarized-light")},
            {Tr("Glass"), QStringLiteral("glass")},
            {Tr("Dense"), QStringLiteral("dense")},
            {Tr("Kuro"), QStringLiteral("kuro")},
            {Tr("Shoin"), QStringLiteral("shoin")},
            {Tr("Ookii (large text)"), QStringLiteral("ookii")},
            {Tr("Kumihan"), QStringLiteral("kumihan")},
            {Tr("Kon (indigo)"), QStringLiteral("kon")},
            {Tr("Kon Neo"), QStringLiteral("kon-neo")},
            {Tr("Inaba"), QStringLiteral("inaba")},
            {Tr("Obsidian"), QStringLiteral("obsidian")},
            {Tr("Bunko"), QStringLiteral("bunko")},
            {Tr("Notebook"), QStringLiteral("notebook")},
            {Tr("Ki"), QStringLiteral("ki")},
            {Tr("Classic"), QStringLiteral("classic")}},
           // "auto" (the default) stays itself here: the look of Furoshiki's shelf theme.
           [&settings] {
               const QString chosen = settings.Value(Settings::KeyPopupTheme).toString();
               return chosen.isEmpty() || ReaderApp::ReaderSettings::ResolvePopupTheme(chosen) != chosen
                          ? QStringLiteral("auto") : chosen;
           }());
    QSpinBox* opacity_box = spin(look, Tr("Popup opacity"), Settings::KeyPopupOpacity, settings.PopupOpacity(), 0, 100,
         Tr(" % (0: default)"));
    QSpinBox* radius_box = spin(look, Tr("Corner radius"), Settings::KeyPopupRadius, settings.PopupRadius(), -1, 28,
         Tr(" px (-1: default)"));
    spin(look, Tr("Maximum width"), Settings::KeyPopupWidth, settings.PopupWidth(), 0, 1000,
         Tr(" px (0: default)"));
    QComboBox* glossary_box = choice(look, Tr("Definition layout"), Settings::KeyPopupGlossary,
           {{Tr("Dictionary layout"), QStringLiteral("full")},
            {Tr("Compact"), QStringLiteral("compact")}},
           settings.PopupGlossary());
    QSpinBox* compact_box = spin(look, Tr("Definitions before \"more\""), Settings::KeyPopupCompact, settings.PopupCompact(), 0, 9,
         Tr(" (0: all)"));
    QComboBox* pitch_box = choice(look, Tr("Pitch accent"), Settings::KeyPopupPitch,
           {{Tr("Line over reading"), QStringLiteral("over")},
            {Tr("Graph"), QStringLiteral("graph")},
            {Tr("Reading with drop"), QStringLiteral("text")},
            {Tr("Number"), QStringLiteral("number")},
            {Tr("Graph, reading & number"), QStringLiteral("all")},
            {Tr("Off"), QStringLiteral("off")}},
           settings.PopupPitch());
    QCheckBox* tags_box = check(look, Tr("Hide grammar tags"), Settings::KeyPopupHideTags,
          settings.PopupHideTags());
    QComboBox* portrait_box = choice(look, Tr("Character portraits"), Settings::KeyPopupPortrait,
           {{Tr("Small"), QStringLiteral("small")},
            {Tr("Large"), QStringLiteral("large")},
            {Tr("Off"), QStringLiteral("off")}},
           settings.PopupPortrait());
    QCheckBox* mark_box = check(look, Tr("Highlight words not in Anki"), Settings::KeyPopupMarkUnknown,
          settings.PopupMarkUnknown());
    QComboBox* lookups_box = choice(look, Tr("Lookup count"), Settings::KeyPopupLookups,
           {{Tr("Count"), QStringLiteral("count")},
            {Tr("Count and first seen"), QStringLiteral("where")},
            {Tr("Off"), QStringLiteral("off")}},
           settings.PopupLookups());
    QComboBox* corner_box = choice(look, Tr("Add button position"), Settings::KeyCardCorner,
           {{Tr("Bottom left"), QStringLiteral("bottom_left")},
            {Tr("Bottom right"), QStringLiteral("bottom_right")},
            {Tr("Top left"), QStringLiteral("top_left")},
            {Tr("Top right"), QStringLiteral("top_right")}},
           settings.CardCorner());
    if (auto* demo = PopupWeb::Create(popup_page, ReaderSession::DataDirectory() + QStringLiteral("/webview2"))) {
        demo->setMinimumSize(340, 320);
        auto* side = new QVBoxLayout;
        side->addWidget(new QLabel(Tr("Preview")));
        side->addWidget(demo, 1);
        if (PopupCustomiser()) {
            auto* customise = new QPushButton(Tr("Customise…"));
            side->addWidget(customise);
            QObject::connect(customise, &QPushButton::clicked, &dialog, [&dialog] {
                dialog.reject();
                QTimer::singleShot(0, [] {
                    if (PopupCustomiser()) {
                        PopupCustomiser()();
                    }
                });
            });
        }
        popup_row->addLayout(side, 2);
        const auto js = [](const QString& text) {
            return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
        };
        // The theme the open list is on, shown before it is taken (choice_preview.h).
        auto trying = std::make_shared<QString>();
        const auto show = [=, &settings] {
            QString theme = ReaderApp::ReaderSettings::ResolvePopupTheme(trying->isEmpty() ? theme_box->currentData().toString() : *trying);
            if (theme == QStringLiteral("classic")) {
                theme = QStringLiteral("console");
            }
            demo->Call(QStringLiteral("document.documentElement.style.background = %1").arg(js(demo->palette().color(QPalette::Window).name())));
            demo->Call(QStringLiteral("Reader.setStyle(\"console\", %1)").arg(js(LookupPopup::ConsoleStyle())));
            demo->Call(QStringLiteral("Reader.setStyle(\"theme\", %1)").arg(js(LookupPopup::ThemeStyle(theme))));
            // The design drawn over the look in use (Furoshiki's customiser), while that look is chosen.
            demo->Call(QStringLiteral("Reader.setStyle(\"user\", %1)").arg(js(theme == settings.PopupTheme() ? settings.PopupCss() : QString())));
            QJsonArray entries = ReaderApp::PopupModel::Sample();
            const QString lookups = lookups_box->currentData().toString();
            QJsonObject first = entries[0].toObject();
            if (lookups == QStringLiteral("off")) {
                first.remove(QStringLiteral("history"));
            } else if (lookups != QStringLiteral("where")) {
                first.insert(QStringLiteral("history"), QStringLiteral("Looked up 7 times"));
            }
            entries[0] = first;
            const QJsonObject model{
                {QStringLiteral("entries"), entries},
                {QStringLiteral("corner"), corner_box->currentData().toString()},
                {QStringLiteral("duplicates"), false},
                {QStringLiteral("markUnknown"), mark_box->isChecked()},
                {QStringLiteral("scale"), scale_box->value()},
                {QStringLiteral("look"), QJsonObject{{QStringLiteral("opacity"), opacity_box->value()},
                                                     {QStringLiteral("radius"), radius_box->value()},
                                                     {QStringLiteral("compact"), compact_box->value()},
                                                     {QStringLiteral("hideTags"), tags_box->isChecked()},
                                                     {QStringLiteral("portrait"), portrait_box->currentData().toString()},
                                                     {QStringLiteral("glossary"), glossary_box->currentData().toString()},
                                                     {QStringLiteral("pitch"), pitch_box->currentData().toString()}}},
                {QStringLiteral("labels"), QJsonObject{{QStringLiteral("forms"), Tr("forms")}, {QStringLiteral("more"), Tr("more")}}}};
            demo->Call(QStringLiteral("Reader.show(%1)").arg(QString::fromUtf8(QJsonDocument(model).toJson(QJsonDocument::Compact))));
        };
        for (QComboBox* box : {theme_box, portrait_box, lookups_box, corner_box, glossary_box, pitch_box}) {
            QObject::connect(box, &QComboBox::currentIndexChanged, demo, show);
        }
        for (QSpinBox* box : {scale_box, opacity_box, radius_box, compact_box}) {
            QObject::connect(box, &QSpinBox::valueChanged, demo, show);
        }
        for (QCheckBox* box : {tags_box, mark_box}) {
            QObject::connect(box, &QCheckBox::toggled, demo, show);
        }
        PreviewWhileOpen(theme_box, [=](int index) {
            const QString value = index < 0 ? QString() : theme_box->itemData(index).toString();
            if (*trying != value) {
                *trying = value;
                show();
            }
        });
        QObject::connect(demo, &PopupWeb::Message, demo, [show](const QJsonObject& message) {
            if (message.value(QStringLiteral("type")).toString() == QStringLiteral("ready")) {
                show();
            }
        });
        show();
    }

    QFormLayout* cursor = section(Tr("Cursor"));
    choice(cursor, Tr("Button"), Settings::KeyCursorButton,
           buttons(false),
           QString::number(settings.CursorButton()));
    check(cursor, Tr("Toggle instead of hold"), Settings::KeyCursorToggle,
          settings.CursorToggle());
    choice(cursor, Tr("Quick menu button"), Settings::KeyMenuButton, buttons(true),
           QString::number(settings.MenuButton()));
    spin(cursor, Tr("Speed"), Settings::KeyCursorSpeed, settings.CursorSpeed(), 25, 300,
         QStringLiteral(" %"));
    check(cursor, Tr("Highlight character under cursor"), Settings::KeyCursorFrame,
          settings.CursorFrame());
    check(cursor, Tr("Move to new lines"), Settings::KeyCursorHome,
          settings.CursorHome());

    QFormLayout* cards = section(Tr("Cards"));
    choice(cards, Tr("Audio on cards"), Settings::KeyCardAudio,
           {{Tr("Ask each time"), QStringLiteral("ask")},
            {Tr("Always"), QStringLiteral("always")},
            {Tr("Never"), QStringLiteral("never")}},
           Settings::CardAudioValue(settings.CardAudioGeneral()));
    check(cards, Tr("Record game audio"), Settings::KeyCaptureAudio,
          settings.CaptureAudio());
    spin(cards, Tr("Maximum clip length"), Settings::KeyAudioMax, settings.AudioClipMaxSeconds(),
         3, 60, Tr(" s"));
    spin(cards, Tr("Clip lead-in"), Settings::KeyAudioLead, settings.AudioLeadMs(), 0,
         2000, Tr(" ms"));
    check(cards, Tr("Trim audio to the voice"), Settings::KeyAudioVoiceTrim,
          settings.AudioVoiceTrim());
    spin(cards, Tr("Context lines"), Settings::KeyCardContextLines,
         settings.CardContextLines(), 0, 5, Tr(" lines"));
    spin(cards, Tr("Screenshots kept"), Settings::KeyScreenshotCache,
         settings.ScreenshotCacheLines(), 0, 200, Tr(" lines"));
    spin(cards, Tr("Maximum screenshot width"), Settings::KeyScreenshotWidth,
         settings.ScreenshotMaxWidth(), 320, 3840, Tr(" px"));

    // The buttons on a controller's back, which no game uses.
    QFormLayout* back = section(Tr("Back buttons"));
    {
        QList<std::pair<QString, QString>> actions{
            {Tr("Nothing"), QStringLiteral("none")},
            {Tr("Pause game"), QStringLiteral("reader_pause_game")},
            {Tr("Pause statistics"), QStringLiteral("reader_pause_stats")},
            {Tr("Add sentence card"), QStringLiteral("reader_sentence_card")},
            {Tr("Backlog"), QStringLiteral("reader_backlog")},
            {Tr("Statistics"), QStringLiteral("reader_statistics")},
            {Tr("Close popup"), QStringLiteral("reader_close_popup")},
            {Tr("Quick menu"), QStringLiteral("reader_menu")}};
        // Only where the emulator can go on to the next choice (console.h).
        if (Reader::ConsoleTraits::HasNextChoice) {
            actions.insert(3, {Tr("Skip to next choice"), QStringLiteral("reader_next_choice")});
        }
        const QStringList names{Tr("Grip 1"), Tr("Grip 2"), Tr("Grip 3"), Tr("Grip 4"), Tr("The \"...\" button")};
        for (int button = 1; button <= names.size(); ++button) {
            const QString now = settings.BackButtonAction(button);
            choice(back, names.at(button - 1), Settings::KeyBackButton + QString::number(button), actions,
                   now.isEmpty() ? QStringLiteral("none") : now);
        }
    }

    QFormLayout* stats = section(Tr("Statistics"));
    check(stats, Tr("Session stats overlay"), Settings::KeySessionHud,
          settings.SessionHud());
    choice(stats, Tr("Overlay style"), Settings::KeyStatsStyle,
           {{Tr("Paper"), QStringLiteral("paper")},
            {Tr("Paper dark"), QStringLiteral("paper-dark")},
            {Tr("Card"), QStringLiteral("card")},
            {Tr("Ink (no box)"), QStringLiteral("ink")}},
           settings.StatsStyle());
    check(stats, Tr("Pause statistics (Ctrl+T)"), Settings::KeyStatsPaused, settings.StatsPaused());
    choice(stats, Tr("Pause statistics button"), Settings::KeyStatsButton,
           buttons(false),
           QString::number(settings.StatsButton()));
    spin(stats, Tr("Idle timeout"), Settings::KeyIdleSeconds,
         static_cast<int>(settings.IdleTimeoutMs() / 1000), Settings::IdleSecondsMin,
         static_cast<int>(ReaderApp::ReadingRules::MaxIdleTimeoutMs / 1000), Tr(" s"));
    check(stats, Tr("Show in Discord status"),
          Settings::KeyDiscordEnabled, settings.DiscordEnabled());
    {
        auto* id = new QLineEdit(settings.DiscordApplicationId());
        id->setPlaceholderText(Tr("17–20 digits"));
        stats->addRow(Tr("Discord application ID"), id);
        savers.append([&settings, id] {
            settings.SetValue(Settings::KeyDiscordApplicationId, id->text().trimmed());
        });
        auto* about = new QLabel(
            Tr("Create one at discord.com/developers/applications."));
        about->setWordWrap(true);
        stats->addRow(about);
    }

    QFormLayout* output = section(Tr("Text output"));
#ifndef READER_NO_TEXT_SERVER
    check(output, Tr("WebSocket text server"), Settings::KeyHookEnabled,
          settings.TextHookEnabled());
    spin(output, Tr("Port"), Settings::KeyHookPort, settings.TextHookPort(), 1024, 65535,
         QString());
    check(output, Tr("Allow other devices (no password)"), Settings::KeyHookLan,
          settings.TextHookLan());
    choice(output, Tr("Format"), Settings::KeyHookFormat,
           {{Tr("Plain text"), QStringLiteral("plain")},
            {QStringLiteral("{\"sentence\": …}"), QStringLiteral("sentence")},
            {Tr("Full JSON event"), QStringLiteral("json")}},
           settings.TextHookFormat());
    check(output, Tr("Include speaker name"), Settings::KeyHookSpeaker,
          settings.TextHookSpeaker());
#endif
    check(output, Tr("Copy lines to clipboard"), Settings::KeyClipboardAuto,
          settings.ClipboardAuto());
#ifndef READER_NO_TEXT_SERVER
    const QString address = session.TextHookAddress();
    if (!address.isEmpty()) {
        output->addRow(new QLabel(Tr("Listening at %1").arg(address)));
    }
#endif

    // The CRT picture: a page built from the table in reader/core/crt.h. What it changes shows in
    // the game's picture at once, and is put back when the window is left unsaved.
    const ReaderCrtPage::Page crt = ReaderCrtPage::Make(settings, &dialog);
    pages->addWidget(crt.widget);
    sections->addItem(Tr("CRT filter"));
    savers.append(crt.save);
    QObject::connect(&dialog, &QDialog::rejected, &dialog, crt.revert);

    sections->setCurrentRow(0);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    outer->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
        for (const auto& save : std::as_const(savers)) {
            save();
        }
        settings.Sync();
        dialog.accept();
    });
    dialog.resize(640, 380);
    Style::DressDialog(&dialog);
    dialog.exec();
}

} // namespace

std::function<void()>& PopupCustomiser() {
    static std::function<void()> hook;
    return hook;
}

std::function<QString()>& ExplainService() {
    static std::function<QString()> where;
    return where;
}

std::function<QString()>& AnkiElsewhere() {
    static std::function<QString()> where;
    return where;
}

std::function<void()>& SettingsPages() {
    static std::function<void()> open;
    return open;
}

std::function<void()>& DictionaryPages() {
    static std::function<void()> open;
    return open;
}

// ---- Progress set by hand (reader.progress) ----

/// The routes (or whatever else the game's script offers to mark) with a box each: for
/// progress made in another copy of the game. A box changed is written into the game's memory
/// at once by the script; the game keeps it as it keeps its own progress (the note says how).
static void ProgressWindow(QWidget* parent) {
    auto& engine = Reader::Engine::Instance();
    std::optional<Reader::ProgressList> list = engine.Progress();
    if (!list || list->items.empty()) {
        QMessageBox::information(parent, Tr("Routes and progress"),
                                 Tr("This game has nothing to mark"));
        return;
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(list->title.empty() ? Tr("Routes and progress")
                                              : QString::fromStdString(list->title));
    dialog.resize(560, 640);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(Tr("Mark what you've completed elsewhere."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    QFont tree_font = tree->font();
    tree_font.setFamilies(Style::JapaneseFamilies());
    tree->setFont(tree_font);
    layout->addWidget(tree, 1);

    auto* note = new QLabel(QString::fromStdString(list->note));
    note->setWordWrap(true);
    note->setVisible(!list->note.empty());
    layout->addWidget(note);
    auto* status = new QLabel;
    status->setWordWrap(true);
    layout->addWidget(status);

    QHash<QString, QTreeWidgetItem*> rows;
    QHash<QString, QTreeWidgetItem*> groups;
    for (const Reader::ProgressItem& item : list->items) {
        QTreeWidgetItem* under = nullptr;
        if (!item.group.empty()) {
            const QString group = QString::fromStdString(item.group);
            under = groups.value(group);
            if (!under) {
                under = new QTreeWidgetItem(tree, QStringList{group});
                under->setFlags(Qt::ItemIsEnabled);
                QFont bold = tree_font;
                bold.setBold(true);
                under->setFont(0, bold);
                groups.insert(group, under);
            }
        }
        auto* row = under ? new QTreeWidgetItem(under) : new QTreeWidgetItem(tree);
        row->setText(0, QString::fromStdString(item.label));
        row->setData(0, Qt::UserRole, QString::fromStdString(item.id));
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        row->setCheckState(0, item.done ? Qt::Checked : Qt::Unchecked);
        rows.insert(QString::fromStdString(item.id), row);
    }
    tree->expandAll();

    // A mark may change others (a route's light, say): every box shows the game's state.
    bool refreshing = false;
    const auto refresh = [&] {
        const auto now = engine.Progress();
        if (!now) {
            return;
        }
        refreshing = true;
        for (const Reader::ProgressItem& item : now->items) {
            if (QTreeWidgetItem* row = rows.value(QString::fromStdString(item.id))) {
                row->setCheckState(0, item.done ? Qt::Checked : Qt::Unchecked);
            }
        }
        refreshing = false;
    };
    QObject::connect(tree, &QTreeWidget::itemChanged, &dialog,
                     [&](QTreeWidgetItem* row, int) {
                         if (refreshing) {
                             return;
                         }
                         const QString id = row->data(0, Qt::UserRole).toString();
                         if (id.isEmpty()) {
                             return;
                         }
                         const bool done = row->checkState(0) == Qt::Checked;
                         std::string error;
                         if (engine.MarkProgress(id.toStdString(), done, error)) {
                             status->setText((done ? Tr("Completed: %1")
                                                   : Tr("Not completed: %1"))
                                                 .arg(row->text(0)));
                         } else {
                             status->setText(Tr("Couldn't change: %1")
                                                 .arg(QString::fromStdString(error).left(200)));
                         }
                         refresh();
                     });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

bool ProgressOffered() {
    const auto list = Reader::Engine::Instance().Progress();
    return list && !list->items.empty();
}

void RunProgressDialog(QWidget* parent) {
    ProgressWindow(parent);
}

bool FetchStarterDictionary(QWidget* parent, QString* problem) {
    const QString address =
        QStringLiteral("https://github.com/stephenmk/stephenmk.github.io/releases/latest/download/jitendex-yomitan.zip");
    const QString file = ReaderSession::DataDirectory() + QStringLiteral("/jitendex-yomitan.zip");
    QProgressDialog progress(Tr("Downloading Jitendex (39 MB)…"), Tr("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();
    bool cancelled = false;
    {
        QProcess curl;
        QEventLoop loop;
        QObject::connect(&curl, &QProcess::finished, &loop, &QEventLoop::quit);
        QObject::connect(&curl, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
        QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] {
            cancelled = true;
            curl.kill();
        });
        curl.start(QStringLiteral("curl"),
                   {QStringLiteral("-L"), QStringLiteral("--proto"), QStringLiteral("=https"),
                    QStringLiteral("--proto-redir"), QStringLiteral("=https"), QStringLiteral("--connect-timeout"),
                    QStringLiteral("30"), QStringLiteral("--fail"), QStringLiteral("--silent"),
                    QStringLiteral("--show-error"), QStringLiteral("--max-time"), QStringLiteral("600"),
                    QStringLiteral("-o"), file, address});
        if (curl.state() != QProcess::NotRunning) {
            loop.exec();
        }
        if (cancelled) {
            QFile::remove(file);
            return false;
        }
        if (curl.error() == QProcess::FailedToStart || curl.exitStatus() != QProcess::NormalExit || curl.exitCode() != 0) {
            const QString said = QString::fromUtf8(curl.readAllStandardError()).trimmed();
            if (problem != nullptr) {
                *problem = said.isEmpty() ? Tr("Couldn't download Jitendex. Check your internet connection and try again")
                                          : said;
            }
            QFile::remove(file);
            return false;
        }
    }
    // Installed as a file chosen by hand is.
    std::atomic<bool> stopped{false};
    QObject::connect(&progress, &QProgressDialog::canceled, &progress, [&stopped] { stopped = true; });
    ReaderApp::DictionaryImporter::Result result;
    QEventLoop loop;
    QThreadPool pool;
    auto& database = ReaderSession::Instance().Dictionaries();
    pool.start([&] {
        ReaderApp::DictionaryImporter importer(database);
        result = importer.Import(
            file,
            [&progress](int done, int total, const QString& title) {
                QMetaObject::invokeMethod(
                    &progress,
                    [&progress, done, total, title] {
                        progress.setLabelText(title.isEmpty() ? Tr("Importing Jitendex…") : Tr("Importing %1…").arg(title));
                        progress.setMaximum(std::max(total, 1));
                        progress.setValue(done);
                    },
                    Qt::QueuedConnection);
            },
            [&stopped] { return !stopped.load(); });
        if (result.ok) {
            database.Compact();
        }
        QMetaObject::invokeMethod(&loop, [&loop] { loop.quit(); }, Qt::QueuedConnection);
    });
    loop.exec();
    pool.waitForDone();
    LOG_INFO(Frontend, "[Reader] Dictionary {} (downloaded)", result.Summary().toStdString());
    const bool was_stopped = stopped.load();
    progress.close();
    QFile::remove(file);
    if (!result.ok && !was_stopped && problem != nullptr) {
        *problem = result.error;
    }
    return result.ok;
}

QString SetUpAnkiByItself() {
    using Settings = ReaderApp::ReaderSettings;
    auto& settings = ReaderSession::Instance().Settings();
    if (settings.AnkiTransport() != QStringLiteral("ankiconnect") || !settings.AnkiModel().isEmpty()) {
        return {};
    }
    try {
        ReaderApp::AnkiConnectClient client(settings.AnkiConnectUrl().trimmed(), settings.AnkiConnectKey(), 1200, 8000);
        const QStringList decks = client.DeckNames();
        QString deck;
        for (const QString& each : decks) {
            if (deck.isEmpty() && each.contains(QStringLiteral("mining"), Qt::CaseInsensitive)) {
                deck = each;
            }
        }
        for (const QString& each : decks) {
            if (deck.isEmpty() && each != QStringLiteral("Default") && each != QStringLiteral("デフォルト")) {
                deck = each;
            }
        }
        if (deck.isEmpty()) {
            return {};
        }
        // In it, the note type most of its notes are.
        QString model;
        qsizetype most = 0;
        for (const QString& each : client.ModelNames()) {
            const qsizetype count = client
                                        .FindNotesWhere(QStringLiteral("\"deck:%1\" \"note:%2\"")
                                                            .arg(ReaderApp::AnkiSearch::Escape(deck),
                                                                 ReaderApp::AnkiSearch::Escape(each)))
                                        .size();
            if (count > most) {
                most = count;
                model = each;
            }
        }
        if (model.isEmpty()) {
            return {};
        }
        const QStringList fields = client.FieldNames(model);
        const QMap<QString, QString> mapping =
            ReaderApp::CardBuilder::OfferWordAudio(ReaderApp::CardBuilder::SuggestMapping(fields));
        bool has_word = false;
        QMap<QString, QString> kept;
        for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it) {
            has_word = has_word || it.value().contains(QStringLiteral("{expression}"));
            if (!it.value().isEmpty()) {
                kept.insert(it.key(), it.value());
            }
        }
        if (!has_word || fields.isEmpty()) {
            return {}; // not a note type whose fields say what they are: the window asks
        }
        settings.SetValue(Settings::KeyAnkiDeck, deck);
        settings.SetValue(Settings::KeyAnkiModel, model);
        settings.SetValue(Settings::KeyAnkiFields, ReaderApp::CardBuilder::MappingToJson(kept));
        settings.SetValue(Settings::KeyAnkiKeyField, Settings::KeyFieldValue(model, fields.first()));
        settings.Sync();
        Miner::Forget();
        return deck + QStringLiteral(" · ") + model;
    } catch (const std::exception& error) {
        LOG_INFO(Frontend, "[Reader] Anki was not set up by itself: {}", error.what());
        return {};
    }
}

void RunExplainDialog(QWidget* parent) {
    if (const auto& current = ReaderSession::Instance().Current()) {
        Explain(parent, *current);
    }
}

QString ExplainedTranslation(const QString& line) {
    return ReaderApp::Explain::Cache(ExplanationsFile()).TranslationOf(line);
}

void RunBacklogDialog(QWidget* parent) {
    Backlog(parent);
}

void RunStatisticsDialog(QWidget* parent) {
    Statistics(parent);
}

void RunDictionariesDialog(QWidget* parent) {
    Dictionaries(parent);
}

void RunAnkiSetupDialog(QWidget* parent) {
    AnkiSetup(parent);
}

void RunSettingsDialog(QWidget* parent) {
    ReaderSettingsDialog(parent);
}

} // namespace ReaderUi
