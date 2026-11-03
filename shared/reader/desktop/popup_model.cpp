// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QRegularExpression>
#include "reader/desktop/glossary.h"
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
        QStringList tags;
        for (const QString& tag : entry->definition_tags.split(u' ', Qt::SkipEmptyParts)) {
            bool is_number = false;
            tag.toInt(&is_number);
            if (!is_number) {
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

} // namespace ReaderApp::PopupModel
