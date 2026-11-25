// SPDX-FileCopyrightText: Readers contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <cctype>
#include <QCache>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include "reader/desktop/glossary.h"
#include "reader/desktop/names.h"
#include "reader/desktop/popup_model.h"

namespace ReaderApp::PopupModel {

bool IsFormsList(const TermEntry& entry) {
    return entry.definition_tags.split(u' ').contains(QStringLiteral("forms"));
}

QString KindOfTitle(const QString& title) {
    static const QRegularExpression grammar{QStringLiteral("grammar|文法|文型|語法|dojg|bunpro"),
                                            QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression names{QStringLiteral("jmnedict|人名"), QRegularExpression::CaseInsensitiveOption};
    static const QRegularExpression kanji{QStringLiteral("kanji|漢字|字典"), QRegularExpression::CaseInsensitiveOption};
    if (grammar.match(title).hasMatch()) {
        return QStringLiteral("grammar");
    }
    if (names.match(title).hasMatch()) {
        return QStringLiteral("name");
    }
    if (kanji.match(title).hasMatch()) {
        return QStringLiteral("kanji");
    }
    return {};
}

QString KindOf(const TermEntry& entry, const QHash<QString, QString>& kinds) {
    static const QStringList known{QStringLiteral("word"), QStringLiteral("grammar"), QStringLiteral("kanji"),
                                   QStringLiteral("name")};
    if (const QString chosen = kinds.value(entry.dictionary); known.contains(chosen)) {
        return chosen;
    }
    if (entry.scoped) {
        return QStringLiteral("name");
    }
    const QString by_title = KindOfTitle(entry.dictionary);
    if (by_title == QStringLiteral("kanji")) {
        // One character (one code point) is a character; a compound in such a dictionary is a word.
        return entry.expression.toUcs4().size() == 1 ? by_title : QStringLiteral("word");
    }
    return by_title.isEmpty() ? QStringLiteral("word") : by_title;
}

QJsonArray Senses(const LookupResult& result, const QHash<QString, QString>& kinds) {
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
        // A glossary with no text the compact form keeps (a picture alone) still has its layout.
        const QJsonDocument laid = QJsonDocument::fromJson(entry->glossary_json.toUtf8());
        const QList<Glossary::Compact> definitions = Glossary::CompactDefinitions(laid.array());
        if (definitions.isEmpty() && laid.array().isEmpty()) {
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
        const QString kind = KindOf(*entry, kinds);
        sense.insert(QStringLiteral("kind"), kind);
        // The glossary whole, as the dictionary laid it out: the page builds it element by element
        // (a word's when its layout is "full", a grammar or a character dictionary's always),
        // with the pictures it shows.
        if (laid.isArray()) {
            sense.insert(QStringLiteral("content"), laid.array());
        }
        if (!entry->media.isEmpty()) {
            QJsonObject media;
            for (auto it = entry->media.cbegin(); it != entry->media.cend(); ++it) {
                media.insert(it.key(), it.value());
            }
            sense.insert(QStringLiteral("media"), media);
        }
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

namespace {

/// What a headword's model costs to make (its senses, forms and card), kept: a game's words
/// come again and again, and a sense is its dictionary's glossary parsed with its pictures.
struct Kept {
    QJsonArray senses;
    QJsonArray forms;
    QJsonObject card;
};

QMutex kept_mutex;
QCache<QString, Kept> kept_models(64 * 1024 * 1024); // by the size of what they hold, roughly

/// The headword's entries (by dictionary and row) and the kinds given: the same key, the same
/// senses. A row is only ever one entry of one dictionary, and ids are not given again.
QString KeyOf(const LookupResult& result, const QHash<QString, QString>& kinds) {
    QString key = result.expression + u'\n' + result.reading;
    for (const TermEntry& entry : result.entries) {
        key += u'\n' + QString::number(entry.dictionary_id) + u':' + QString::number(entry.id) +
               (entry.scoped ? u'n' : u'w');
    }
    if (!kinds.isEmpty()) {
        QStringList given;
        for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
            given.append(it.key() + u'=' + it.value());
        }
        given.sort();
        key += u'\n' + given.join(u'\t');
    }
    return key;
}

Kept Make(const LookupResult& result, const QHash<QString, QString>& kinds) {
    Kept made;
    made.senses = Senses(result, kinds);
    made.forms = QJsonArray::fromStringList(Forms(result));
    if (!result.entries.isEmpty() && result.entries.first().scoped) {
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
            made.card = shown;
        }
    }
    return made;
}

int CostOf(const LookupResult& result) {
    qint64 cost = 1;
    for (const TermEntry& entry : result.entries) {
        cost += entry.glossary_json.size() * 2;
        for (const QString& picture : entry.media) {
            cost += picture.size() * 2;
        }
    }
    return static_cast<int>(std::min<qint64>(cost, 64 * 1024 * 1024));
}

bool CanCache(const LookupResult& result) {
    if (result.entries.isEmpty()) {
        return false;
    }
    for (const TermEntry& entry : result.entries) {
        if (entry.dictionary_id == 0 && entry.id == 0) {
            return false;
        }
    }
    return true;
}

Kept KeptOrMade(const LookupResult& result, const QHash<QString, QString>& kinds) {
    if (!CanCache(result)) {
        return Make(result, kinds);
    }
    const QString key = KeyOf(result, kinds);
    {
        QMutexLocker lock(&kept_mutex);
        if (const Kept* known = kept_models.object(key)) {
            return *known;
        }
    }
    Kept made = Make(result, kinds);
    QMutexLocker lock(&kept_mutex);
    kept_models.insert(key, new Kept(made), CostOf(result));
    return made;
}

} // namespace

namespace {

// ---- The page's model as text (EntryText) ----
// A glossary is JSON already: it goes to the page as it is stored, after a check that it is
// JSON (a dictionary's broken text must not break the page), not parsed into objects and
// written out again, which took most of a hover with many dictionaries.

const char* SkipJsonSpace(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
        ++p;
    }
    return p;
}

/// The end of the JSON value at p, or nullptr when it is not one.
const char* JsonValueEnd(const char* p, const char* end, int depth) {
    p = SkipJsonSpace(p, end);
    if (p >= end || depth > 200) {
        return nullptr;
    }
    const char c = *p;
    if (c == '"') {
        for (++p; p < end; ++p) {
            const auto byte = static_cast<unsigned char>(*p);
            if (*p == '"') {
                return p + 1;
            }
            if (byte < 0x20) {
                return nullptr;
            }
            if (*p == '\\') {
                if (++p >= end) {
                    return nullptr;
                }
                if (*p == 'u') {
                    if (end - p < 5) {
                        return nullptr;
                    }
                    for (int i = 1; i <= 4; ++i) {
                        if (!std::isxdigit(static_cast<unsigned char>(p[i]))) {
                            return nullptr;
                        }
                    }
                    p += 4;
                } else if (std::strchr("\"\\/bfnrt", *p) == nullptr) {
                    return nullptr;
                }
            }
        }
        return nullptr;
    }
    if (c == '[' || c == '{') {
        const char close = c == '[' ? ']' : '}';
        p = SkipJsonSpace(p + 1, end);
        if (p < end && *p == close) {
            return p + 1;
        }
        for (;;) {
            if (c == '{') {
                p = SkipJsonSpace(p, end);
                if (p >= end || *p != '"') {
                    return nullptr;
                }
                p = JsonValueEnd(p, end, depth + 1);
                if (p == nullptr) {
                    return nullptr;
                }
                p = SkipJsonSpace(p, end);
                if (p >= end || *p != ':') {
                    return nullptr;
                }
                ++p;
            }
            p = JsonValueEnd(p, end, depth + 1);
            if (p == nullptr) {
                return nullptr;
            }
            p = SkipJsonSpace(p, end);
            if (p < end && *p == ',') {
                ++p;
                continue;
            }
            if (p < end && *p == close) {
                return p + 1;
            }
            return nullptr;
        }
    }
    for (const char* word : {"true", "false", "null"}) {
        const std::size_t n = std::strlen(word);
        if (static_cast<std::size_t>(end - p) >= n && std::memcmp(p, word, n) == 0) {
            return p + n;
        }
    }
    if (p < end && *p == '-') {
        ++p;
    }
    if (p >= end || !std::isdigit(static_cast<unsigned char>(*p))) {
        return nullptr;
    }
    if (*p == '0') {
        ++p;
    } else {
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) {
            ++p;
        }
    }
    if (p < end && *p == '.') {
        ++p;
        if (p >= end || !std::isdigit(static_cast<unsigned char>(*p))) {
            return nullptr;
        }
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) {
            ++p;
        }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) {
            ++p;
        }
        if (p >= end || !std::isdigit(static_cast<unsigned char>(*p))) {
            return nullptr;
        }
        while (p < end && std::isdigit(static_cast<unsigned char>(*p))) {
            ++p;
        }
    }
    return p;
}

/// `json` when it is a JSON array (spaces trimmed), else "[]".
QByteArray CheckedArray(const QByteArray& json) {
    const char* begin = SkipJsonSpace(json.constData(), json.constData() + json.size());
    const char* end = json.constData() + json.size();
    if (begin >= end || *begin != '[') {
        return QByteArrayLiteral("[]");
    }
    const char* stop = JsonValueEnd(begin, end, 0);
    if (stop == nullptr || SkipJsonSpace(stop, end) != end) {
        return QByteArrayLiteral("[]");
    }
    return QByteArray(begin, stop - begin);
}

QByteArray Quoted(const QString& text) {
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

QByteArray Compact(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

/// A headword's senses, forms and card as the page's JSON, and the pictures they name.
struct KeptText {
    QByteArray senses;
    QByteArray forms;
    QByteArray card; // empty: none
    QList<std::pair<QString, QString>> pictures; // key ("dictionary\npath") → data address
};

QMutex kept_text_mutex;
QCache<QString, KeptText> kept_text(96 * 1024 * 1024); // by bytes

KeptText MakeText(const LookupResult& result, const QHash<QString, QString>& kinds) {
    KeptText made;
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
    QByteArray senses = "[";
    QString dictionary;
    bool first = true;
    bool any = false;
    int number = 0;
    QStringList last_tags;
    for (const TermEntry* entry : shown) {
        const QByteArray content = CheckedArray(entry->glossary_json.toUtf8());
        if (content == "[]") {
            continue; // nothing to show (the objects' path: no definitions and an empty array)
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
        QStringList tags;
        for (const QString& tag : entry->definition_tags.split(u' ', Qt::SkipEmptyParts)) {
            bool is_number = false;
            tag.toInt(&is_number);
            if (!is_number && !(entry->scoped && tag == NameTag)) {
                tags.append(tag);
            }
        }
        sense.insert(QStringLiteral("dictionary"), entry->dictionary);
        sense.insert(QStringLiteral("kind"), KindOf(*entry, kinds));
        if (!entry->media.isEmpty()) {
            QJsonObject media;
            for (auto it = entry->media.cbegin(); it != entry->media.cend(); ++it) {
                const QString key = entry->dictionary + u'\n' + it.key();
                media.insert(it.key(), QStringLiteral("pic:") + key);
                made.pictures.append({key, it.value()});
            }
            sense.insert(QStringLiteral("media"), media);
        }
        if (entry->scoped) {
            sense.insert(QStringLiteral("name"), true);
        }
        sense.insert(QStringLiteral("number"), number);
        sense.insert(QStringLiteral("tags"), QJsonArray::fromStringList(tags == last_tags ? QStringList() : tags));
        // The page reads glosses and notes only where a sense has no content: every sense here has.
        sense.insert(QStringLiteral("glosses"), QJsonArray());
        sense.insert(QStringLiteral("notes"), QString());
        last_tags = tags;
        QByteArray text = Compact(sense);
        text.chop(1); // its closing brace: the content goes in before it
        text += ",\"content\":";
        text += content;
        text += '}';
        if (any) {
            senses += ',';
        }
        senses += text;
        any = true;
    }
    senses += ']';
    made.senses = senses;
    const QByteArray forms = QJsonDocument(QJsonArray::fromStringList(Forms(result))).toJson(QJsonDocument::Compact);
    made.forms = forms;
    const Kept card = [&] {
        Kept only;
        if (!result.entries.isEmpty() && result.entries.first().scoped) {
            only = Make(result, kinds); // names: small, and rare
        }
        return only;
    }();
    if (!card.card.isEmpty()) {
        made.card = Compact(card.card);
    }
    return made;
}

} // namespace

QByteArray EntryText(const LookupResult& result, const QString& history, const QHash<QString, QString>& kinds,
                     QSet<QString>& sent, QJsonObject& pictures) {
    KeptText kept;
    if (CanCache(result)) {
        const QString key = KeyOf(result, kinds);
        bool known = false;
        {
            QMutexLocker lock(&kept_text_mutex);
            if (const KeptText* found = kept_text.object(key)) {
                kept = *found;
                known = true;
            }
        }
        if (!known) {
            kept = MakeText(result, kinds);
            QMutexLocker lock(&kept_text_mutex);
            const qsizetype cost = kept.senses.size() + kept.forms.size() + kept.card.size() + 64;
            kept_text.insert(key, new KeptText(kept), static_cast<int>(std::min<qsizetype>(cost, 96 * 1024 * 1024)));
        }
    } else {
        kept = MakeText(result, kinds);
    }
    for (const auto& [picture, data] : kept.pictures) {
        if (!sent.contains(picture)) {
            sent.insert(picture);
            pictures.insert(picture, data);
        }
    }
    // What changes from one hover to the next is small: made each time.
    QJsonObject entry = Entry(result, history, kinds, true);
    QByteArray text = Compact(entry);
    text.chop(1);
    text += ",\"senses\":";
    text += kept.senses;
    text += ",\"forms\":";
    text += kept.forms;
    if (!kept.card.isEmpty()) {
        text += ",\"card\":";
        text += kept.card;
    }
    text += '}';
    return text;
}

QByteArray KanjiText(const QList<KanjiEntry>& kanji, const QList<TermMeta>& frequencies) {
    QByteArray out;
    QJsonArray shown;
    for (const TermMeta& meta : frequencies) {
        if (meta.mode != QStringLiteral("freq")) {
            continue;
        }
        // A number, a string, or {value, displayValue}.
        const QJsonDocument data = QJsonDocument::fromJson('[' + meta.data.toUtf8() + ']');
        const QJsonArray array = data.array();
        if (array.isEmpty()) {
            continue;
        }
        const QJsonValue value = array.first();
        QString said;
        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            said = object.value(QStringLiteral("displayValue")).toVariant().toString();
            if (said.isEmpty()) {
                said = object.value(QStringLiteral("value")).toVariant().toString();
            }
        } else {
            said = value.toVariant().toString();
        }
        if (!said.isEmpty()) {
            shown.append(QJsonObject{{QStringLiteral("dictionary"), meta.dictionary}, {QStringLiteral("value"), said}});
        }
    }
    bool first = true;
    for (const KanjiEntry& entry : kanji) {
        const QJsonObject character{{QStringLiteral("character"), entry.character},
                                    {QStringLiteral("dictionary"), entry.dictionary},
                                    {QStringLiteral("meanings"), QJsonArray::fromStringList(entry.meanings)},
                                    {QStringLiteral("onyomi"), QJsonArray::fromStringList(entry.onyomi)},
                                    {QStringLiteral("kunyomi"), QJsonArray::fromStringList(entry.kunyomi)},
                                    {QStringLiteral("tags"), entry.tags},
                                    {QStringLiteral("stats"), entry.stats}};
        const QJsonObject model{{QStringLiteral("expression"), entry.character},
                                {QStringLiteral("reading"), QString()},
                                {QStringLiteral("reasons"), QJsonArray()},
                                {QStringLiteral("senses"), QJsonArray()},
                                {QStringLiteral("forms"), QJsonArray()},
                                {QStringLiteral("frequencies"), first ? shown : QJsonArray()},
                                {QStringLiteral("state"), QStringLiteral("add")},
                                {QStringLiteral("unknown"), false},
                                {QStringLiteral("kanji"), character}};
        if (!out.isEmpty()) {
            out += ',';
        }
        out += Compact(model);
        first = false;
    }
    return out;
}

void PrepareText(const LookupResult& result, const QHash<QString, QString>& kinds) {
    if (!CanCache(result)) {
        return;
    }
    const QString key = KeyOf(result, kinds);
    {
        QMutexLocker lock(&kept_text_mutex);
        if (kept_text.contains(key)) {
            return;
        }
    }
    KeptText made = MakeText(result, kinds);
    QMutexLocker lock(&kept_text_mutex);
    const qsizetype cost = made.senses.size() + made.forms.size() + made.card.size() + 64;
    kept_text.insert(key, new KeptText(std::move(made)), static_cast<int>(std::min<qsizetype>(cost, 96 * 1024 * 1024)));
}

void Prepare(const LookupResult& result, const QHash<QString, QString>& kinds) {
    (void)KeptOrMade(result, kinds);
}

void ForgetPrepared() {
    {
        QMutexLocker lock(&kept_mutex);
        kept_models.clear();
    }
    QMutexLocker lock(&kept_text_mutex);
    kept_text.clear();
}

QJsonObject Entry(const LookupResult& result, const QString& history, const QHash<QString, QString>& kinds,
                  bool without_senses) {
    const Kept kept = without_senses ? Kept{} : KeptOrMade(result, kinds);
    QJsonArray pitches;
    QJsonArray accents;
    for (const PitchAccent& pitch : result.pitches) {
        QStringList positions;
        QJsonArray numbers;
        for (const int position : pitch.positions) {
            positions.append(QString::number(position));
            numbers.append(position);
        }
        pitches.append(u'[' + positions.join(u',') + u']');
        // For the graph and the text form: which reading, and where it falls.
        accents.append(QJsonObject{{QStringLiteral("dictionary"), pitch.dictionary},
                                   {QStringLiteral("reading"), pitch.reading},
                                   {QStringLiteral("positions"), numbers}});
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
                      {QStringLiteral("pitchAccents"), accents},
                      {QStringLiteral("frequencies"), frequencies},

                      {QStringLiteral("state"), QStringLiteral("add")},
                      {QStringLiteral("unknown"), false}};
    if (!without_senses) {
        entry.insert(QStringLiteral("senses"), kept.senses);
        entry.insert(QStringLiteral("forms"), kept.forms);
    }
    if (!result.entries.isEmpty() && result.entries.first().scoped) {
        // A name in the running game, from its names dictionary: the page tags it 名前.
        entry.insert(QStringLiteral("name"), true);
        if (!kept.card.isEmpty()) {
            entry.insert(QStringLiteral("card"), kept.card);
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
    unknown.expression = QStringLiteral("風呂敷");
    unknown.reading = QStringLiteral("ふろしき");
    unknown.pitches = {PitchAccent{QStringLiteral("NHK"), QStringLiteral("ふろしき"), {0}}};
    unknown.frequencies = {Frequency{QStringLiteral("JPDB"), QStringLiteral("18200"), 18200.0, true}};
    TermEntry sense;
    sense.dictionary = QStringLiteral("JMdict");
    sense.definition_tags = QStringLiteral("n");
    sense.glossary_json = QString::fromUtf8(
        R"j(["wrapping cloth","cloth wrapper (traditionally used to carry clothes, gifts, or books)","traditional Japanese square cloth folded around items for transport"])j");
    TermEntry forms;
    forms.dictionary = QStringLiteral("JMdict");
    forms.definition_tags = QStringLiteral("forms");
    forms.glossary_json = QString::fromUtf8(R"j(["風呂敷 | ふろしき"])j");
    unknown.entries = {sense, forms};
    QJsonObject first = Entry(unknown, QStringLiteral("Looked up 7 times · first 3 Oct in STEINS;GATE"));
    first.insert(QStringLiteral("unknown"), true);

    LookupResult known;
    known.expression = QStringLiteral("風呂");
    known.reading = QStringLiteral("ふろ");
    known.pitches = {PitchAccent{QStringLiteral("NHK"), QStringLiteral("ふろ"), {2}}};
    TermEntry bath;
    bath.dictionary = QStringLiteral("JMdict");
    bath.definition_tags = QStringLiteral("n");
    bath.glossary_json = QString::fromUtf8(R"j(["bath","bathhouse"])j");
    known.entries = {bath};
    QJsonObject second = Entry(known, {});
    second.insert(QStringLiteral("state"), QStringLiteral("inDeck"));
    // The three kanji of 風呂敷 for the Kanji tab's Character Rail preview.
    const auto sample_kanji = [](const QString& ch, const QJsonArray& meanings, const QJsonArray& on,
                                 const QJsonArray& kun, int strokes, int grade, int jlpt, int freq,
                                 const QJsonArray& parts) {
        return QJsonObject{
            {QStringLiteral("expression"), ch},
            {QStringLiteral("kanji"),
             QJsonObject{
                 {QStringLiteral("character"), ch},
                 {QStringLiteral("dictionary"), QStringLiteral("KANJIDIC")},
                 {QStringLiteral("meanings"), meanings},
                 {QStringLiteral("onyomi"), on},
                 {QStringLiteral("kunyomi"), kun},
                 {QStringLiteral("stats"),
                  QJsonObject{{QStringLiteral("strokes"), strokes},
                              {QStringLiteral("grade"), grade},
                              {QStringLiteral("jlpt"), jlpt},
                              {QStringLiteral("freq"), freq}}},
                 {QStringLiteral("parts"), parts},
             }},
        };
    };
    const QJsonObject k1 = sample_kanji(
        QStringLiteral("風"),
        QJsonArray{QStringLiteral("wind"), QStringLiteral("air"), QStringLiteral("style"), QStringLiteral("manner")},
        QJsonArray{QStringLiteral("フウ"), QStringLiteral("フ")},
        QJsonArray{QStringLiteral("かぜ"), QStringLiteral("かざ-")},
        9, 2, 4, 558, QJsonArray{QStringLiteral("几"), QStringLiteral("虫")});
    const QJsonObject k2 = sample_kanji(
        QStringLiteral("呂"),
        QJsonArray{QStringLiteral("spine"), QStringLiteral("backbone")},
        QJsonArray{QStringLiteral("ロ"), QStringLiteral("リョ")},
        QJsonArray{QStringLiteral("せぼね")},
        7, 8, 1, 1978, QJsonArray{QStringLiteral("口"), QStringLiteral("ノ")});
    const QJsonObject k3 = sample_kanji(
        QStringLiteral("敷"),
        QJsonArray{QStringLiteral("spread"), QStringLiteral("pave"), QStringLiteral("sit"), QStringLiteral("promulgate")},
        QJsonArray{QStringLiteral("フ")},
        QJsonArray{QStringLiteral("し.く"), QStringLiteral("-し.き")},
        15, 8, 1, 1246, QJsonArray{QStringLiteral("甫"), QStringLiteral("攵")});
    return {first, second, k1, k2, k3};
}

void SharePictures(QJsonArray& entries, QSet<QString>& sent, QJsonObject& pictures) {
    for (qsizetype e = 0; e < entries.size(); ++e) {
        QJsonObject entry = entries.at(e).toObject();
        QJsonArray senses = entry.value(QStringLiteral("senses")).toArray();
        bool changed = false;
        for (qsizetype s = 0; s < senses.size(); ++s) {
            QJsonObject sense = senses.at(s).toObject();
            const QJsonObject media = sense.value(QStringLiteral("media")).toObject();
            if (media.isEmpty()) {
                continue;
            }
            const QString dictionary = sense.value(QStringLiteral("dictionary")).toString();
            QJsonObject named;
            for (auto it = media.constBegin(); it != media.constEnd(); ++it) {
                const QString key = dictionary + u'\n' + it.key();
                if (!sent.contains(key)) {
                    sent.insert(key);
                    pictures.insert(key, it.value());
                }
                named.insert(it.key(), QStringLiteral("pic:") + key);
            }
            sense.insert(QStringLiteral("media"), named);
            senses.replace(s, sense);
            changed = true;
        }
        if (changed) {
            entry.insert(QStringLiteral("senses"), senses);
            entries.replace(e, entry);
        }
    }
}

} // namespace ReaderApp::PopupModel
