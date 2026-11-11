// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <functional>
#include <QList>
#include "reader/core/console.h"
#include "reader/desktop/settings.h"
#include "reader_rows.h"

namespace ReaderRows {

namespace {

using Settings = ReaderApp::ReaderSettings;

struct Choice {
    QString said;
    QString value;
};

struct Entry {
    QString name;
    QString key;
    enum Kind { Switch, Number, Chosen } kind;
    /// What it is now, as it is kept ("1"/"0" for a switch).
    std::function<QString(const Settings&)> now;
    int low{}, high{}, step{1};
    QString unit;
    QList<Choice> choices;
};

Entry Switch(const char* name, const QString& key, bool (Settings::*now)() const) {
    return {QString::fromLatin1(name), key, Entry::Switch,
            [now](const Settings& s) { return (s.*now)() ? QStringLiteral("1") : QStringLiteral("0"); }};
}

Entry Number(const char* name, const QString& key, int (Settings::*now)() const, int low, int high, int step,
             const char* unit) {
    return {QString::fromLatin1(name), key, Entry::Number,
            [now](const Settings& s) { return QString::number((s.*now)()); }, low, high, step, QString::fromLatin1(unit)};
}

Entry Chosen(const char* name, const QString& key, std::function<QString(const Settings&)> now, QList<Choice> choices) {
    return {QString::fromLatin1(name), key, Entry::Chosen, std::move(now), 0, 0, 1, {}, std::move(choices)};
}

/// The controller's buttons the reader may have for its own, as the console names them.
QList<Choice> Buttons(bool for_menu) {
    QList<Choice> out{{QStringLiteral("None"), QStringLiteral("-1")}};
    for (const auto& button : Reader::ConsoleTraits::ReaderButtons) {
        if (for_menu || !button.menu_only) {
            out.append({QString::fromLatin1(button.name), QString::number(button.number)});
        }
    }
    return out;
}

const QList<Entry>& Entries() {
    static const QList<Entry> entries = [] {
        const auto text = [](QString (Settings::*now)() const) {
            return [now](const Settings& s) { return (s.*now)(); };
        };
        const auto number = [](int (Settings::*now)() const) {
            return [now](const Settings& s) { return QString::number((s.*now)()); };
        };
        QList<Entry> out{
            // Reading
            Switch("Click words in the game's text", Settings::KeyScreenText, &Settings::ScreenText),
            Chosen("Look up by pointing", Settings::KeyHoverLookup, text(&Settings::HoverLookup),
                   {{QStringLiteral("Off"), QStringLiteral("off")},
                    {QStringLiteral("With Shift"), QStringLiteral("shift")},
                    {QStringLiteral("Always"), QStringLiteral("always")}}),
            Chosen("Mark the word looked up", Settings::KeyWordMark, text(&Settings::WordMark),
                   {{QStringLiteral("Outline"), QStringLiteral("outline")},
                    {QStringLiteral("Highlight"), QStringLiteral("highlight")},
                    {QStringLiteral("None"), QStringLiteral("none")}}),
            Switch("Play by touch", Settings::KeyTouchPlay, &Settings::TouchPlay),
            Switch("Outline what can be clicked", Settings::KeyScreenTextOutline, &Settings::ScreenTextOutline),
            Switch("Session line over the game", Settings::KeySessionHud, &Settings::SessionHud),
            Chosen("Session line's look", Settings::KeyStatsStyle, text(&Settings::StatsStyle),
                   {{QStringLiteral("Paper"), QStringLiteral("paper")},
                    {QStringLiteral("Paper dark"), QStringLiteral("paper-dark")},
                    {QStringLiteral("Card"), QStringLiteral("card")},
                    {QStringLiteral("Ink"), QStringLiteral("ink")}}),
            Switch("Pause the reading clock", Settings::KeyStatsPaused, &Settings::StatsPaused),
            {QStringLiteral("Idle after"), Settings::KeyIdleSeconds, Entry::Number,
             [](const Settings& s) { return QString::number(s.IdleTimeoutMs() / 1000); }, Settings::IdleSecondsMin, 300, 5,
             QStringLiteral("S")},
            Switch("Discord status", Settings::KeyDiscordEnabled, &Settings::DiscordEnabled),
            // Popup
            Number("Text size", Settings::KeyPopupScale, &Settings::PopupScale, 60, 200, 5, "%"),
            Number("Height, at most", Settings::KeyPopupHeight, &Settings::PopupHeightPercent, 10, 80, 5, "%"),
            Number("Close after", Settings::KeyPopupAutoHide, &Settings::PopupAutoHideSeconds, 0, 120, 5, "S"),
            Switch("Close when the pointer leaves", Settings::KeyPopupHideOnExit, &Settings::PopupHideOnExit),
            Chosen("Add button's corner", Settings::KeyCardCorner, text(&Settings::CardCorner),
                   {{QStringLiteral("Bottom left"), QStringLiteral("bottom_left")},
                    {QStringLiteral("Bottom right"), QStringLiteral("bottom_right")},
                    {QStringLiteral("Top left"), QStringLiteral("top_left")},
                    {QStringLiteral("Top right"), QStringLiteral("top_right")}}),
            // Cards
            Chosen("Line audio", Settings::KeyCardAudio,
                   [](const Settings& s) { return Settings::CardAudioValue(s.CardAudioGeneral()); },
                   {{QStringLiteral("Ask"), QStringLiteral("ask")},
                    {QStringLiteral("Always"), QStringLiteral("always")},
                    {QStringLiteral("Never"), QStringLiteral("never")}}),
            Switch("Cut the clip to the voice", Settings::KeyAudioVoiceTrim, &Settings::AudioVoiceTrim),
            Number("Lines before, in {context}", Settings::KeyCardContextLines, &Settings::CardContextLines, 0, 5, 1, ""),
            Switch("Keep the game's audio", Settings::KeyCaptureAudio, &Settings::CaptureAudio),
            Number("Clip, at most", Settings::KeyAudioMax, &Settings::AudioClipMaxSeconds, 3, 60, 1, "S"),
            Number("Start the clip earlier by", Settings::KeyAudioLead, &Settings::AudioLeadMs, 0, 2000, 50, "MS"),
            Number("Pictures kept", Settings::KeyScreenshotCache, &Settings::ScreenshotCacheLines, 0, 200, 10, "LINES"),
            Number("Picture width, at most", Settings::KeyScreenshotWidth, &Settings::ScreenshotMaxWidth, 320, 3840, 160, "PX"),
            // Sources
            Switch("Copy each line to the clipboard", Settings::KeyClipboardAuto, &Settings::ClipboardAuto),
#ifndef READER_NO_TEXT_SERVER
            Switch("Text server", Settings::KeyHookEnabled, &Settings::TextHookEnabled),
            Number("Text server's port", Settings::KeyHookPort, &Settings::TextHookPort, 1024, 65535, 1, ""),
            Switch("Other devices may connect", Settings::KeyHookLan, &Settings::TextHookLan),
            Chosen("Text server's format", Settings::KeyHookFormat, text(&Settings::TextHookFormat),
                   {{QStringLiteral("Plain text"), QStringLiteral("plain")},
                    {QStringLiteral("Sentence"), QStringLiteral("sentence")},
                    {QStringLiteral("Full JSON"), QStringLiteral("json")}}),
            Switch("Speaker's name before the line", Settings::KeyHookSpeaker, &Settings::TextHookSpeaker),
#endif
            // Controller
            Switch("Pointer button: press once", Settings::KeyCursorToggle, &Settings::CursorToggle),
            Number("Pointer speed", Settings::KeyCursorSpeed, &Settings::CursorSpeed, 25, 300, 5, "%"),
            Chosen("Reader menu button", Settings::KeyMenuButton, number(&Settings::MenuButton), Buttons(true)),
            Chosen("Reading clock button", Settings::KeyStatsButton, number(&Settings::StatsButton), Buttons(false)),
            Switch("Frame the character pointed at", Settings::KeyCursorFrame, &Settings::CursorFrame),
            Switch("Pointer jumps to each new line", Settings::KeyCursorHome, &Settings::CursorHome),
        };
        // The buttons on a controller's back, which no game uses.
        QList<Choice> actions{{QStringLiteral("Nothing"), QStringLiteral("none")},
                              {QStringLiteral("Pause the game"), QStringLiteral("reader_pause_game")},
                              {QStringLiteral("Pause the clock"), QStringLiteral("reader_pause_stats")},
                              {QStringLiteral("Sentence card"), QStringLiteral("reader_sentence_card")},
                              {QStringLiteral("Word card"), QStringLiteral("reader_word_card")},
                              {QStringLiteral("Take the card back"), QStringLiteral("reader_undo_card")},
                              {QStringLiteral("Backlog"), QStringLiteral("reader_backlog")},
                              {QStringLiteral("Explain"), QStringLiteral("reader_explain")},
                              {QStringLiteral("Close the popup"), QStringLiteral("reader_close_popup")},
                              {QStringLiteral("Reader menu"), QStringLiteral("reader_menu")}};
        if (Reader::ConsoleTraits::HasNextChoice) {
            actions.insert(3, {QStringLiteral("Next choice"), QStringLiteral("reader_next_choice")});
        }
        const char* const names[] = {"Grip 1", "Grip 2", "Grip 3", "Grip 4", "The \"...\" button"};
        for (int button = 1; button <= 5; ++button) {
            out.append(Chosen(names[button - 1], Settings::KeyBackButton + QString::number(button),
                              [button](const Settings& s) {
                                  const QString now = s.BackButtonAction(button);
                                  return now.isEmpty() ? QStringLiteral("none") : now;
                              },
                              actions));
        }
        return out;
    }();
    return entries;
}

const Entry* Find(const QString& name) {
    for (const Entry& entry : Entries()) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

QString Value(const Settings& settings, const QString& name) {
    const Entry* entry = Find(name);
    if (entry == nullptr) {
        return {};
    }
    const QString now = entry->now(settings);
    switch (entry->kind) {
    case Entry::Switch:
        return now == QStringLiteral("1") ? QStringLiteral("ON") : QStringLiteral("OFF");
    case Entry::Number:
        // Nothing after so many seconds is "never", not "0 s".
        if (now == QStringLiteral("0") && entry->name == QStringLiteral("Close after")) {
            return QStringLiteral("NEVER");
        }
        return entry->unit.isEmpty() ? now : entry->unit == QStringLiteral("%") ? now + entry->unit : now + QLatin1Char(' ') + entry->unit;
    case Entry::Chosen:
        for (const Choice& choice : entry->choices) {
            if (choice.value == now) {
                return choice.said.toUpper();
            }
        }
        return now.toUpper();
    }
    return {};
}

bool Step(Settings& settings, const QString& name, int step) {
    const Entry* entry = Find(name);
    if (entry == nullptr) {
        return false;
    }
    const QString now = entry->now(settings);
    switch (entry->kind) {
    case Entry::Switch:
        settings.SetValue(entry->key, now != QStringLiteral("1"));
        break;
    case Entry::Number: {
        const int value = now.toInt() / entry->step * entry->step + step * entry->step;
        settings.SetValue(entry->key, std::clamp(value, entry->low, entry->high));
        break;
    }
    case Entry::Chosen: {
        int at = 0;
        for (int i = 0; i < entry->choices.size(); ++i) {
            if (entry->choices[i].value == now) {
                at = i;
            }
        }
        const int count = static_cast<int>(entry->choices.size());
        const QString next = entry->choices[((at + step) % count + count) % count].value;
        // A button is kept as its number.
        bool is_number = false;
        const int as_number = next.toInt(&is_number);
        if (is_number) {
            settings.SetValue(entry->key, as_number);
        } else {
            settings.SetValue(entry->key, next);
        }
        break;
    }
    }
    settings.Sync();
    return true;
}

bool IsSet(const Settings& settings, const QString& name) {
    const Entry* entry = Find(name);
    return entry != nullptr && settings.Has(entry->key);
}

bool Reset(Settings& settings, const QString& name) {
    const Entry* entry = Find(name);
    if (entry == nullptr) {
        return false;
    }
    settings.Remove(entry->key);
    settings.Sync();
    return true;
}

bool IsNumber(const QString& name) {
    const Entry* entry = Find(name);
    return entry != nullptr && entry->kind == Entry::Number;
}

QStringList Names() {
    QStringList out;
    for (const Entry& entry : Entries()) {
        out.append(entry.name);
    }
    return out;
}

} // namespace ReaderRows
