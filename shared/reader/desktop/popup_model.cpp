// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QRegularExpression>
#include "reader/desktop/glossary.h"
#include "reader/desktop/names.h"
#include "reader/desktop/popup_model.h"

namespace ReaderApp::PopupModel {

bool IsFormsList(const TermEntry& entry) {
    return entry.definition_tags.split(u' ').contains(QStringLiteral("forms"));
}

QJsonArray Senses(const LookupResult& result) {
    QList<const TermEntry*> shown;
    QStringList dictionaries;
    for (const TermEntry& entry : result.entries) {
        if (!IsFormsList(entry)) {
            shown.append(&entry);
            if (!dictionaries.contains(entry.dictionary)) {
                dictionaries.append(entry.dictionary);
            }
        }
    }
    const bool several = dictionaries.size() > 1;
    QJsonArray out;
    QString dictionary;
    bool first = true;
    int number = 0;
    QStringList last_tags;
    for (const TermEntry* entry : shown) {
        const QList<Glossary::Compact> definitions = Glossary::CompactDefinitions(entry->glossary_json);
        if (definitions.isEmpty()) {
            continue;
        }
        QJsonObject sense;
        if (first || entry->dictionary != dictionary) {
            first = false;
            dictionary = entry->dictionary;
            number = 0;
            last_tags.clear();
            if (several) {
                sense.insert(QStringLiteral("heading"), entry->dictionary);
            }
        }
        ++number;
        // Numbers among the tags only repeat the order of the senses; tags the sense before
        // had already are not said again.
        // A name's 名前 is said once, by the headword (Entry's "name").
        QStringList tags;
        for (const QString& tag : entry->definition_tags.split(u' ', Qt::SkipEmptyParts)) {
            bool is_number = false;
            tag.toInt(&is_number);
            if (!is_number && !(entry->scoped && tag == NameTag)) {
                tags.append(tag);
            }
        }
        QJsonArray glosses;
        QStringList notes;
        for (const Glossary::Compact& definition : definitions) {
            for (const QString& gloss : definition.glosses.split(QStringLiteral(" | "), Qt::SkipEmptyParts)) {
                glosses.append(gloss);
            }
            if (!definition.notes.isEmpty()) {
                notes.append(definition.notes);
            }
        }
        sense.insert(QStringLiteral("dictionary"), entry->dictionary);
        if (entry->scoped) {
            sense.insert(QStringLiteral("name"), true);
        }
        sense.insert(QStringLiteral("number"), number);
        sense.insert(QStringLiteral("tags"), QJsonArray::fromStringList(tags == last_tags ? QStringList() : tags));
        sense.insert(QStringLiteral("glosses"), glosses);
        sense.insert(QStringLiteral("notes"), notes.join(QStringLiteral("; ")));
        last_tags = tags;
        out.append(sense);
    }
    return out;
}

namespace {
const QRegularExpression& Marks() {
    static const QRegularExpression marks{
        QStringLiteral("[（(][^）)]*[）)]|[\\x{2600}-\\x{27BF}\\x{3280}-\\x{32BF}\\x{2B50}\\x{1F300}-\\x{1FAFF}\\x{FE0F}]")};
    return marks;
}
} // namespace

QStringList Forms(const LookupResult& result) {
    QStringList out;
    for (const TermEntry& entry : result.entries) {
        if (!IsFormsList(entry)) {
            continue;
        }
        for (const Glossary::Compact& definition : Glossary::CompactDefinitions(entry.glossary_json)) {
            for (QString form : definition.glosses.split(QRegularExpression(QStringLiteral("\\s*[|;、,]\\s*")),
                                                         Qt::SkipEmptyParts)) {
                // JMdict marks a rare, irregular or search-only form with a sign (★, ㊗, ⚠...),
                // sometimes in brackets: the form is the word without them.
                form.remove(Marks());
                form = form.trimmed();
                if (!form.isEmpty() && !out.contains(form)) {
                    out.append(form);
                }
            }
        }
    }
    return out;
}

QJsonObject Entry(const LookupResult& result, const QString& history) {
    QJsonArray pitches;
    for (const PitchAccent& pitch : result.pitches) {
        QStringList positions;
        for (const int position : pitch.positions) {
            positions.append(QString::number(position));
        }
        pitches.append(u'[' + positions.join(u',') + u']');
    }
    QJsonArray frequencies;
    for (const Frequency& frequency : result.frequencies) {
        frequencies.append(QJsonObject{{QStringLiteral("dictionary"), frequency.dictionary},
                                       {QStringLiteral("value"), frequency.display}});
    }
    QJsonObject entry{{QStringLiteral("expression"), result.expression},
                      {QStringLiteral("reading"), result.DisplayReading()},
                      {QStringLiteral("reasons"), QJsonArray::fromStringList(result.reasons)},
                      {QStringLiteral("pitches"), pitches},
                      {QStringLiteral("frequencies"), frequencies},
                      {QStringLiteral("senses"), Senses(result)},
                      {QStringLiteral("forms"), QJsonArray::fromStringList(Forms(result))},
                      {QStringLiteral("state"), QStringLiteral("add")},
                      {QStringLiteral("unknown"), false}};
    if (!result.entries.isEmpty() && result.entries.first().scoped) {
        // A name in the running game, from its names dictionary: the page tags it 名前.
        entry.insert(QStringLiteral("name"), true);
        // Who or what it is, as a card, when its dictionary says (tools/titles.py): the page
        // draws that in place of the senses, with the picture the dictionary's zip has.
        const TermEntry& first = result.entries.first();
        if (const Names::Card card = Names::CardOf(first.glossary_json); card.found) {
            QJsonObject shown{{QStringLiteral("name"), card.name},
                              {QStringLiteral("kind"), card.kind.isEmpty() ? QStringLiteral("person") : card.kind},
                              {QStringLiteral("also"), QJsonArray::fromStringList(card.also)}};
            const auto put = [&shown](const char* name, const QString& value) {
                if (!value.isEmpty()) {
                    shown.insert(QLatin1String(name), value);
                }
            };
            put("id", card.id);
            put("reading", card.reading);
            put("romaji", card.romaji);
            put("role", card.role);
            put("about", card.about);
            put("image", Names::Picture(first.dictionary, card.image));
            entry.insert(QStringLiteral("card"), shown);
        }
    }
    if (!history.isEmpty()) {
        entry.insert(QStringLiteral("history"), history);
    }
    return entry;
}

QString StateName(int state) {
    // In the order of LookupPopup::CardState.
    static const char* const names[] = {"add", "working", "inDeck", "checking", "away"};
    return state >= 0 && state < 5 ? QString::fromLatin1(names[state]) : QStringLiteral("add");
}

/// What the preview shows: a word not in Anki, with a frequency and the count of lookups, and one
/// Anki has.
QJsonArray Sample() {
    LookupResult unknown;
    unknown.expression = QStringLiteral("死ぬほど");
    unknown.reading = QStringLiteral("しぬほど");
    unknown.frequencies = {Frequency{QStringLiteral("JPDB"), QStringLiteral("7621"), 7621.0, true}};
    TermEntry sense;
    sense.dictionary = QStringLiteral("JMdict");
    sense.definition_tags = QStringLiteral("exp");
    sense.glossary_json = QString::fromUtf8(R"j(["to death (bored, scared, etc.)","to distraction","like crazy","so much"])j");
    TermEntry forms;
    forms.dictionary = QStringLiteral("JMdict");
    forms.definition_tags = QStringLiteral("forms");
    forms.glossary_json = QString::fromUtf8(R"j(["死ぬほど | 死ぬ程"])j");
    unknown.entries = {sense, forms};
    QJsonObject first = Entry(unknown, QStringLiteral("Looked up 7 times · first 3 Oct in STEINS;GATE"));
    first.insert(QStringLiteral("unknown"), true);

    LookupResult known;
    known.expression = QStringLiteral("死ぬ");
    known.reading = QStringLiteral("しぬ");
    known.reasons = {QStringLiteral("masu")};
    known.pitches = {PitchAccent{QStringLiteral("NHK"), QStringLiteral("しぬ"), {0}}};
    TermEntry die;
    die.dictionary = QStringLiteral("JMdict");
    die.definition_tags = QStringLiteral("v5n vi");
    die.glossary_json = QString::fromUtf8(R"j(["to die","to pass away"])j");
    known.entries = {die};
    QJsonObject second = Entry(known, {});
    second.insert(QStringLiteral("state"), QStringLiteral("inDeck"));
    // A name of a game, as its names dictionary gives it (no picture here: those are in the zips).
    LookupResult who;
    who.expression = QStringLiteral("助手");
    who.reading = QStringLiteral("じょしゅ");
    TermEntry name;
    name.dictionary = QStringLiteral("名前 · STEINS;GATE");
    name.definition_tags = NameTag;
    name.scoped = true;
    name.glossary_json = QString::fromUtf8(
        R"j([{"type":"structured-content","content":{"tag":"div","data":{"furoshiki":"name","id":"c6487","kind":"person","name":"牧瀬 紅莉栖","reading":"まきせ くりす","romaji":"Makise Kurisu","role":"Main character","also":"助手、クリスティーナ","about":"A neuroscience researcher of eighteen whom Okabe insists on calling his assistant."},"content":[{"tag":"div","content":"牧瀬 紅莉栖（まきせ くりす）"}]}}])j");
    who.entries = {name};
    const QJsonObject third = Entry(who, {});
    return {first, second, third};
}

} // namespace ReaderApp::PopupModel
