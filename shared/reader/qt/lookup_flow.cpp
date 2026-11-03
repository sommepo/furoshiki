// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <exception>
#include <utility>
#include <QDateTime>
#include <QEvent>
#include "reader/qt/host.h"
#include "reader/qt/lookup_flow.h"
#include "reader/qt/reader_session.h"

namespace ReaderUi {

using ReaderApp::LookupResult;
using ReaderApp::PresentedLine;

namespace {
constexpr int Margin = 6;
} // namespace

LookupFlow::LookupFlow(QWidget* popup_parent)
    : popup{new LookupPopup(popup_parent)}, prompt{new AudioPrompt(popup_parent)},
      home{popup_parent} {
    worker.setMaxThreadCount(1);
    popup->hide();
    // Left alone for the time in the settings, the popup closes: not while a card waits for
    // its answer, and not with the pointer resting on it or on its word.
    hide_timer.setSingleShot(true);
    connect(&hide_timer, &QTimer::timeout, this, [this] {
        if (!IsOpen()) {
            return;
        }
        if (pointer_inside_now) {
            pointer_inside = pointer_inside_now();
        }
        if (IsAsking() || pointer_inside) {
            KeepOpen();
        } else {
            Close();
        }
    });
    connect(popup, &LookupPopup::LayoutChanged, this, [this] {
        if (show_pending) {
            show_pending = false;
            ShowNow();
        } else if (popup->isVisible()) {
            Position();
        }
    });
    count_timer.setSingleShot(true);
    count_timer.setInterval(CountAfterMs);
    connect(&count_timer, &QTimer::timeout, this, &LookupFlow::CountLookup);
    exit_timer.setSingleShot(true);
    exit_timer.setInterval(ExitGraceMs);
    connect(&exit_timer, &QTimer::timeout, this, [this] {
        if (IsOpen() && !IsAsking() && !pointer_inside) {
            Close();
        }
    });
    popup->installEventFilter(this);
    prompt->hide();
    const auto listen = [this] {
        if (player.IsPlaying()) {
            player.Stop();
        } else if (!asking.isEmpty() && !player.Play(asking.first().clip.samples) && notify) {
            notify(tr("The clip cannot be played: no sound device"));
        }
    };
    connect(prompt, &AudioPrompt::ListenClicked, this, listen);
    connect(prompt, &AudioPrompt::KeepClicked, this, [this] { AnswerQuestion(true); });
    connect(prompt, &AudioPrompt::DropClicked, this, [this] { AnswerQuestion(false); });
    // The same question in the popup page, where it draws the popup.
    connect(popup, &LookupPopup::ListenRequested, this, listen);
    connect(popup, &LookupPopup::KeepRequested, this, [this] { AnswerQuestion(true); });
    connect(popup, &LookupPopup::DropRequested, this, [this] { AnswerQuestion(false); });
    connect(&player, &ClipPlayer::PlayingChanged, this, [this](bool playing) {
        if (prompt) {
            prompt->SetPlaying(playing);
        }
        if (asking_on_page) {
            popup->AskPlaying(playing);
        }
    });
    connect(popup, &LookupPopup::AddRequested, this, &LookupFlow::Mine);
    connect(popup, &LookupPopup::NotesRequested, this, &LookupFlow::ShowNotes);
    connect(popup, &LookupPopup::DictionariesRequested, this, [this] {
        Close();
        if (open_dictionaries) {
            open_dictionaries();
        }
    });
}

LookupFlow::~LookupFlow() {
    worker.waitForDone();
    // What still waits for an answer is dropped: nobody is left to hear how it ended.
    asking.clear();
    player.Stop();
    delete prompt.data();
    delete popup.data();
}

bool LookupFlow::IsOpen() const {
    return popup->isVisible();
}

bool LookupFlow::IsAbout(qint64 backlog_id, int offset) const {
    return IsOpen() && backlog_id == word_line && offset >= word_first && offset <= word_last;
}

void LookupFlow::KeepOpen() {
    hide_timer.stop();
    const int seconds = ReaderSession::Instance().Settings().PopupAutoHideSeconds();
    if (IsOpen() && seconds > 0) {
        hide_timer.start(seconds * 1000);
    }
}

void LookupFlow::PointerMoved(bool inside) {
    const bool was_inside = pointer_inside;
    pointer_inside = inside;
    if (!IsOpen()) {
        return;
    }
    if (inside) {
        exit_timer.stop();
        KeepOpen();
        return;
    }
    if (was_inside) {
        // Just left: its time counts from now, not from when it was last renewed.
        KeepOpen();
    }
    if (ReaderSession::Instance().Settings().PopupHideOnExit() && !IsAsking() &&
               !exit_timer.isActive()) {
        exit_timer.start();
    }
}

bool LookupFlow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == popup) {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            PointerMoved(true);
            break;
        case QEvent::Leave:
            PointerMoved(false);
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void LookupFlow::SetPopupParent(QWidget* parent) {
    if (popup->parentWidget() == parent) {
        return;
    }
    const bool was_open = popup->isVisible();
    popup->setParent(parent, popup->windowFlags());
    if (was_open) {
        popup->show();
    }
    const bool was_asking = prompt->isVisible();
    prompt->setParent(parent, prompt->windowFlags());
    if (was_asking) {
        prompt->show();
    }
}

void LookupFlow::Lookup(const PresentedLine& presented, int offset, const Anchor& beside,
                        bool quiet) {
    const QString& text = presented.line.text;
    if (offset < 0 || offset >= text.size()) {
        return;
    }
    if (quiet && IsAsking()) {
        // The pointer on its way to the question about a card's audio looks nothing up.
        return;
    }
    auto& session = ReaderSession::Instance();
    session.NoteInteraction();
    const int request = ++asked;
    const QString rest = text.mid(offset);
    anchor = beside;
    ReaderApp::DictionaryDatabase* dictionaries = &session.Dictionaries();
    ReaderApp::DictionaryLookup* lookup = &session.Lookup();
    ReaderApp::ReaderDatabase* database = &session.Database();
    const bool histories = session.Settings().PopupLookups() != QStringLiteral("off");
    worker.start([this, request, presented, offset, quiet, rest, dictionaries, lookup, database,
                  histories] {
        if (request != asked) {
            return; // the pointer has gone on to another word since
        }
        bool has_dictionaries = false;
        QList<LookupResult> results;
        QList<ReaderApp::LookupHistory> history;
        try {
            has_dictionaries = dictionaries->HasEnabledDictionaries();
            if (has_dictionaries) {
                results = lookup->Lookup(rest);
            }
            if (histories) {
                for (const LookupResult& result : results) {
                    history.append(database->Lookups(result.expression, result.reading));
                }
            }
        } catch (const std::exception& error) {
            LOG_ERROR(Frontend, "[Reader] Lookup failed: {}", error.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, request, presented, offset, quiet, has_dictionaries, results, history] {
                OnLookupDone(request, presented, offset, quiet, has_dictionaries, results,
                             history);
            },
            Qt::QueuedConnection);
    });
}

void LookupFlow::OnLookupDone(int request, const PresentedLine& presented, int offset, bool quiet,
                              bool has_dictionaries, const QList<LookupResult>& results,
                              const QList<ReaderApp::LookupHistory>& history) {
    if (request != asked) {
        return;
    }
    if (quiet && has_dictionaries && results.isEmpty()) {
        // No word here: the popup about the word the pointer came from goes.
        Close();
        return;
    }
    ++token;
    if (pinned && mark) {
        mark(pinned->presented.backlog_id, -1, -1);
    }
    pinned.reset();
    word_line = presented.backlog_id;
    word_first = offset;
    word_last = offset;
    auto& settings = ReaderSession::Instance().Settings();
    popup->SetTextScale(settings.PopupScale());
    popup->SetTheme(settings.PopupTheme());
    popup->SetUserCss(settings.PopupCss());
    if (popup->IsWeb()) {
        // The page lays itself out at the popup's width: it has that width before it is shown.
        popup->resize(PopupWidth(), std::max(popup->height(), 56));
    }
    if (!has_dictionaries) {
        popup->ShowMessage(tr("No dictionary is installed yet."), true);
    } else if (results.isEmpty()) {
        popup->ShowMessage(tr("No entry for %1").arg(presented.line.text.mid(offset, 8)), false);
    } else {
        if (mark) {
            mark(presented.backlog_id, offset,
                 offset + static_cast<int>(results.first().matched_text.size()) - 1);
        }
        word_last = offset + static_cast<int>(results.first().matched_text.size()) - 1;
        pinned = Pinned{presented, offset, results, {}};
        popup->SetAllowDuplicates(settings.AnkiAllowDuplicates());
        popup->SetCardCorner(LookupPopup::CornerOf(settings.CardCorner()));
        popup->SetMarkUnknown(settings.PopupMarkUnknown());
        popup->ShowResults(results, history, settings.PopupLookups() == QStringLiteral("where"));
        AskAnki();
        count_timer.start();
    }
    if (!popup->HasLayout()) {
        // Shown once the page has said how tall it is (LayoutChanged): no jump. A page that
        // says nothing in time does not keep the popup away.
        show_pending = true;
        QTimer::singleShot(LayoutWaitMs, this, [this, shown = token] {
            if (show_pending && shown == token) {
                show_pending = false;
                ShowNow();
            }
        });
        return;
    }
    ShowNow();
}

void LookupFlow::ShowNow() {
    Position();
    const bool was_open = popup->isVisible();
    popup->show();
    popup->update();
    exit_timer.stop();
    if (pointer_inside_now) {
        // A pointer that was resting on the word when its popup came up is on it.
        pointer_inside = pointer_inside_now();
    }
    KeepOpen();
    if (!was_open) {
        emit Opened();
    }
}

int LookupFlow::PopupWidth() const {
    const int room = anchor.bounds.isValid() ? anchor.bounds.width() - 2 * Margin : popup->MaxWidth();
    return std::max(120, std::min(room, popup->MaxWidth()));
}

void LookupFlow::Position() {
    if (!anchor.owner || !anchor.bounds.isValid()) {
        return;
    }
    // Above what it keeps clear of (the word's row) if it fits there, else below, else on the side with more room and only
    // as tall as that room. It is only as large as its content.
    const QRect& bounds = anchor.bounds;
    auto& settings = ReaderSession::Instance().Settings();
    const int widest = PopupWidth();
    const int above = anchor.keep_clear.top() - bounds.top() - 2 * Margin;
    const int below = bounds.bottom() - anchor.keep_clear.bottom() - 2 * Margin;
    const auto fit = [this, widest](int tallest) { return popup->Fit(widest, std::max(tallest, 56)); };
    QSize size = fit(std::min(bounds.height() * settings.PopupHeightPercent() / 100,
                              std::max(above, below)));
    const bool goes_above = size.height() <= above || above >= below;
    const int room = goes_above ? above : below;
    if (size.height() > room) {
        size = fit(room);
    }
    const int left = std::clamp(anchor.near_x - 10, bounds.left() + Margin,
                                std::max(bounds.left() + Margin,
                                         bounds.right() - Margin - size.width()));
    const int top = goes_above
                        ? std::max(anchor.keep_clear.top() - Margin - size.height(),
                                   bounds.top() + Margin)
                        : std::min(anchor.keep_clear.bottom() + Margin,
                                   std::max(bounds.top() + Margin,
                                            bounds.bottom() - Margin - size.height()));
    popup->setGeometry(QRect(anchor.owner->mapToGlobal(QPoint(left, top)), size));
}

void LookupFlow::Reposition(const Anchor& beside) {
    anchor = beside;
    if (popup->isVisible()) {
        Position();
    }
    PlaceQuestion();
}

// ---- The question about a card's audio ----

void LookupFlow::Ask(const MediaCapture::Clip& clip, Miner::AudioAnswer answer) {
    asking.append(Asked{clip, std::move(answer)});
    if (asking.size() == 1) {
        ShowQuestion();
    }
}

void LookupFlow::ShowQuestion() {
    // A word's card asked from a popup the page draws: the question in the popup, in its look,
    // in the place of that word's buttons.
    asking_on_page = popup->IsWeb() && popup->isVisible() && asking_index >= 0;
    if (asking_on_page) {
        popup->AskOnPage(asking_index);
        return;
    }
    prompt->Prepare(ReaderSession::Instance().Settings().PopupScale());
    PlaceQuestion();
    prompt->show();
    prompt->raise();
}

/**
 * Beside the popup the card was asked from, on the side away from the text the popup belongs
 * to, with its right edge at the popup's; over the popup's own edge where there is no room
 * beside it. In the middle when there is no popup.
 */
void LookupFlow::PlaceQuestion() {
    if (asking.isEmpty() || !prompt || asking_on_page) {
        return;
    }
    QWidget* owner = anchor.owner ? anchor.owner : home.data();
    if (!owner) {
        return;
    }
    const QRect bounds(owner->mapToGlobal(QPoint(0, 0)), owner->size());
    const QSize size = prompt->sizeHint();
    int left = bounds.center().x() - size.width() / 2;
    int top = bounds.center().y() - size.height() / 2;
    const QRect plus = asking_index >= 0 && popup->isVisible() ? popup->AddBox(asking_index) : QRect();
    if (plus.isValid()) {
        // Over the plus that asked for the card, on the popup itself: no gap to cross, nothing
        // of the game's text on the way, and the answers right where the pointer is.
        const QPoint at = popup->mapToGlobal(plus.topLeft());
        left = at.x() - prompt->contentsMargins().left() - 8;
        top = at.y() + plus.height() / 2 - size.height() / 2;
    } else if (popup->isVisible()) {
        const QRect box = popup->geometry();
        left = box.right() + 1 - size.width();
        const bool popup_above_text =
            !anchor.owner ||
            box.top() < anchor.owner->mapToGlobal(anchor.keep_clear.topLeft()).y();
        if (popup_above_text) {
            const int above = box.top() - Margin - size.height();
            top = above >= bounds.top() + Margin ? above : box.top();
        } else {
            const int below = box.bottom() + 1 + Margin;
            top = below + size.height() <= bounds.bottom() - Margin
                      ? below
                      : box.bottom() + 1 - size.height();
        }
    }
    left = std::clamp(left, bounds.left() + Margin,
                      std::max(bounds.left() + Margin, bounds.right() - Margin - size.width()));
    top = std::clamp(top, bounds.top() + Margin,
                     std::max(bounds.top() + Margin, bounds.bottom() - Margin - size.height()));
    prompt->move(left, top);
}

void LookupFlow::AnswerQuestion(std::optional<bool> keep) {
    if (asking.isEmpty()) {
        return;
    }
    player.Stop();
    const Asked asked = asking.takeFirst();
    if (asking_on_page) {
        asking_on_page = false;
        popup->AskDone();
    }
    prompt->hide();
    asked.answer(keep);
    if (!asking.isEmpty()) {
        ShowQuestion();
    }
}

void LookupFlow::CancelQuestions() {
    while (!asking.isEmpty()) {
        AnswerQuestion(std::nullopt);
    }
}

void LookupFlow::Close() {
    if (asking_on_page) {
        // The question goes with the popup it stood in: the card is not made.
        CancelQuestions();
    }
    ++token;
    ++asked;
    if (pinned && mark) {
        mark(pinned->presented.backlog_id, -1, -1);
    }
    pinned.reset();
    word_line = -1;
    word_first = -1;
    word_last = -1;
    hide_timer.stop();
    exit_timer.stop();
    count_timer.stop();
    show_pending = false;
    const bool was_open = popup->isVisible();
    popup->hide();
    if (was_open) {
        emit Closed();
    }
}

/**
 * Finds out which of the results Anki has already: at once from this computer's copy of the
 * deck, where it can tell (a turning ring where it cannot), then from Anki, for all of them
 * in one request. Their boxes turn into open books; where Anki does not answer, the plus
 * shows that it is away.
 */
void LookupFlow::AskAnki() {
    if (!pinned) {
        return;
    }
    using State = LookupPopup::CardState;
    using Answer = ReaderApp::AnkiIndex::Answer;
    const int request = token;
    bool set_up = false;
    for (int index = 0; index < pinned->results.size(); ++index) {
        const LookupResult& result = pinned->results.at(index);
        const int last = pinned->offset + static_cast<int>(result.matched_text.size()) - 1;
        const Miner::Known known = miner.KnownNow(pinned->presented, result, pinned->offset, last);
        if (!known.set_up) {
            // No card can be made: the plus says why when it is pressed.
            return;
        }
        set_up = true;
        if (known.answer == Answer::Present) {
            pinned->notes.insert(index, known.notes);
        }
        popup->SetUnknown(index, known.answer == Answer::Absent);
        popup->SetCardState(index, known.answer == Answer::Present  ? State::InDeck
                                   : known.answer == Answer::Absent ? State::Add
                                                                    : State::Checking);
    }
    if (!set_up) {
        return;
    }
    miner.Confirm(pinned->presented, pinned->results, pinned->offset,
                  [this, request](const Miner::Confirmed& confirmed) {
                      // The popup may show other words by now; those were asked about
                      // separately.
                      if (request != token || !pinned) {
                          return;
                      }
                      for (int index = 0; index < pinned->results.size(); ++index) {
                          const State now = popup->GetCardState(index);
                          if (now == State::Working) {
                              continue;
                          }
                          if (!confirmed.reached) {
                              if (now != State::InDeck) {
                                  popup->SetCardState(index, State::Away);
                              }
                              continue;
                          }
                          const auto& found = confirmed.notes.value(index);
                          if (!found) {
                              if (now == State::Checking) {
                                  popup->SetCardState(index, State::Add);
                              }
                          } else if (found->isEmpty()) {
                              pinned->notes.remove(index);
                              popup->SetCardState(index, State::Add);
                              popup->SetUnknown(index, true);
                          } else {
                              pinned->notes.insert(index, *found);
                              popup->SetCardState(index, State::InDeck);
                              popup->SetUnknown(index, false);
                          }
                      }
                  });
}

void LookupFlow::CountLookup() {
    if (!pinned || pinned->counted || pinned->results.isEmpty()) {
        return;
    }
    pinned->counted = true;
    const LookupResult& word = pinned->results.first();
    const QString key = QStringLiteral("%1\x1f%2\x1f%3")
                            .arg(pinned->presented.backlog_id)
                            .arg(word.expression, word.reading);
    if (counted.contains(key)) {
        return;
    }
    if (counted.size() > 5000) {
        counted.clear();
    }
    counted.insert(key);
    auto& session = ReaderSession::Instance();
    ReaderApp::ReaderDatabase* database = &session.Database();
    worker.start([database, expression = word.expression, reading = word.reading,
                  game = session.GameKey(), now = QDateTime::currentMSecsSinceEpoch()] {
        try {
            database->CountLookup(expression, reading, game, now);
        } catch (const std::exception& error) {
            LOG_WARNING(Frontend, "[Reader] Could not count a lookup: {}", error.what());
        }
    });
}

void LookupFlow::Mine(int index) {
    if (!pinned || index < 0 || index >= pinned->results.size()) {
        return;
    }
    const int request = token;
    LookupPopup::CardState before = popup->GetCardState(index);
    if (before == LookupPopup::CardState::Working) {
        return;
    }
    if (before == LookupPopup::CardState::Checking || before == LookupPopup::CardState::Away) {
        // Making the card finds out what Anki has; a card that fails leaves the plus.
        before = LookupPopup::CardState::Add;
    }
    popup->SetCardState(index, LookupPopup::CardState::Working);
    CountLookup();
    const LookupResult& result = pinned->results.at(index);
    const int last = pinned->offset + static_cast<int>(result.matched_text.size()) - 1;
    asking_index = index; // the question about its audio comes up over its plus
    miner.Mine(pinned->presented, result, pinned->offset, last,
               [this](const MediaCapture::Clip& clip, Miner::AudioAnswer answer) {
                   Ask(clip, std::move(answer));
               },
               [this, request, index, before](const Miner::Outcome& outcome) {
                   Report(outcome);
                   if (request != token || !pinned) {
                       return;
                   }
                   bool known = false;
                   if (outcome.kind == Miner::Outcome::Kind::Added) {
                       pinned->notes[index].append(outcome.notes);
                       known = true;
                   } else if (outcome.kind == Miner::Outcome::Kind::Exists) {
                       pinned->notes.insert(index, outcome.notes);
                       known = true;
                   }
                   popup->SetCardState(index, known ? LookupPopup::CardState::InDeck : before);
                   if (known) {
                       popup->SetUnknown(index, false);
                   }
               });
}

void LookupFlow::MineSentence(const PresentedLine& presented) {
    asking_index = -1;
    miner.Mine(presented, std::nullopt, -1, -1,
               [this](const MediaCapture::Clip& clip, Miner::AudioAnswer answer) {
                   Ask(clip, std::move(answer));
               },
               [this](const Miner::Outcome& outcome) { Report(outcome); });
}

void LookupFlow::ShowNotes(int index) {
    if (!pinned) {
        return;
    }
    const QList<qint64> known = pinned->notes.value(index);
    if (known.isEmpty()) {
        return;
    }
    CountLookup();
    miner.ShowNotes(known, [this](const QString& failure) {
        if (!failure.isEmpty() && notify) {
            notify(tr("Anki could not show the note: %1").arg(failure));
        }
    });
}

void LookupFlow::Report(const Miner::Outcome& outcome) {
    if (!notify) {
        return;
    }
    switch (outcome.kind) {
    case Miner::Outcome::Kind::Added:
        notify(tr("Card added"));
        break;
    case Miner::Outcome::Kind::Exists:
        notify(tr("Anki has this word already"));
        break;
    case Miner::Outcome::Kind::Failed:
        notify(tr("No card: %1").arg(outcome.message));
        break;
    case Miner::Outcome::Kind::Cancelled:
        break; // the user called it off: nothing to say
    }
}

} // namespace ReaderUi
