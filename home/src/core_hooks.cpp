// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core_hooks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <functional>
#include <optional>

namespace Home::CoreHooks {

namespace {

QString ReadText(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void Tally(Count* count, const QString& why) {
    if (count != nullptr) {
        ++count->why_not[why];
    }
}

/// The body of the brace that opens at `open`, without its braces; empty when unmatched.
QString Braced(const QString& text, qsizetype open, qsizetype* end = nullptr) {
    int depth = 0;
    QChar quote;
    for (qsizetype i = open; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == u'\\') {
                ++i;
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            quote = c;
        } else if (c == u'{') {
            ++depth;
        } else if (c == u'}' && --depth == 0) {
            if (end != nullptr) {
                *end = i;
            }
            return text.mid(open + 1, i - open - 1);
        }
    }
    return {};
}

/// Comments taken out (// and /* */), strings kept.
QString Uncommented(const QString& text) {
    QString out;
    QChar quote;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            out += c;
            if (c == u'\\' && i + 1 < text.size()) {
                out += text[++i];
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            // A C++ raw string: copied whole.
            if (c == u'"' && i > 0 && text[i - 1] == u'R') {
                const qsizetype paren = text.indexOf(u'(', i);
                const QString delimiter = text.mid(i + 1, paren - i - 1);
                const qsizetype close = text.indexOf(u')' + delimiter + u'"', paren);
                if (paren > 0 && close > 0) {
                    out += text.mid(i, close + delimiter.size() + 2 - i);
                    i = close + delimiter.size() + 1;
                    continue;
                }
            }
            quote = c;
            out += c;
        } else if (c == u'/' && i + 1 < text.size() && text[i + 1] == u'/') {
            while (i < text.size() && text[i] != u'\n') {
                ++i;
            }
            out += u'\n';
        } else if (c == u'/' && i + 1 < text.size() && text[i + 1] == u'*') {
            const qsizetype close = text.indexOf(QStringLiteral("*/"), i + 2);
            i = close < 0 ? text.size() : close + 1;
            out += u' ';
        } else {
            out += c;
        }
    }
    return out;
}

/// The text with what is inside its string literals blanked out (raw strings too).
QString Unquoted(const QString& text) {
    QString out;
    QChar quote;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            if (c == u'\\') {
                ++i;
                out += QStringLiteral("  ");
                continue;
            }
            if (c == quote) {
                quote = QChar();
                out += c;
            } else {
                out += u' ';
            }
            continue;
        }
        if (c == u'"' && i > 0 && text[i - 1] == u'R') {
            const qsizetype paren = text.indexOf(u'(', i);
            const QString delimiter = text.mid(i + 1, paren - i - 1);
            const qsizetype close = text.indexOf(u')' + delimiter + u'"', paren);
            if (paren > 0 && close > 0) {
                out += u'"' + QString(close + delimiter.size() - i, u' ') + u'"';
                i = close + delimiter.size() + 1;
                continue;
            }
        }
        if (c == u'"' || c == u'\'') {
            quote = c;
        }
        out += c;
    }
    return out;
}

/// Splits at `separator` outside brackets and strings (and JavaScript's /regex/ literals are
/// not looked into: a statement of ours has its regex inside a call's brackets).
QStringList SplitTop(const QString& text, QChar separator) {
    QStringList out;
    QString part;
    int depth = 0;
    QChar quote;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (!quote.isNull()) {
            part += c;
            if (c == u'\\' && i + 1 < text.size()) {
                part += text[++i];
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == u'"' && i > 0 && text[i - 1] == u'R') {
            const qsizetype paren = text.indexOf(u'(', i);
            const QString delimiter = text.mid(i + 1, paren - i - 1);
            const qsizetype close = text.indexOf(u')' + delimiter + u'"', paren);
            if (paren > 0 && close > 0) {
                part += text.mid(i, close + delimiter.size() + 2 - i);
                i = close + delimiter.size() + 1;
                continue;
            }
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            quote = c;
        } else if (c == u'(' || c == u'[' || c == u'{' || c == u'<' && separator == u',') {
            ++depth;
        } else if (c == u')' || c == u']' || c == u'}' || c == u'>' && separator == u',') {
            --depth;
        } else if (c == separator && depth == 0) {
            out.append(part.trimmed());
            part.clear();
            continue;
        }
        part += c;
    }
    if (!part.trimmed().isEmpty()) {
        out.append(part.trimmed());
    }
    return out;
}

// ---- C++ literals (LunaHook's tables) ----

struct Literal {
    enum Kind { Plain, Wide, Utf8 } kind{Plain};
    QByteArray bytes; ///< Plain: the bytes as written; Utf8: UTF-8
    QString text;     ///< Wide: the text
    bool raw{};       ///< Written as R"(…)": no escapes were read
};

/// One C++ string literal (L"…", u8"…", R"(…)", LR"(…)", u8R"(…)"), whole; nothing otherwise.
std::optional<Literal> CppLiteral(QString code) {
    code = code.trimmed();
    Literal literal;
    if (code.startsWith(QStringLiteral("u8"))) {
        literal.kind = Literal::Utf8;
        code = code.mid(2);
    } else if (code.startsWith(u'L')) {
        literal.kind = Literal::Wide;
        code = code.mid(1);
    }
    if (code.startsWith(QStringLiteral("R\""))) {
        const qsizetype paren = code.indexOf(u'(');
        const QString delimiter = code.mid(2, paren - 2);
        if (paren < 0 || !code.endsWith(u')' + delimiter + u'"')) {
            return std::nullopt;
        }
        const QString inner = code.mid(paren + 1, code.size() - paren - 3 - delimiter.size());
        literal.raw = true;
        literal.text = inner;
        literal.bytes = inner.toUtf8();
        return literal;
    }
    if (code.size() < 2 || !code.startsWith(u'"') || !code.endsWith(u'"')) {
        return std::nullopt;
    }
    QByteArray bytes;
    QString wide;
    for (qsizetype i = 1; i + 1 < code.size(); ++i) {
        QChar c = code[i];
        if (c == u'"') {
            return std::nullopt; // two literals side by side
        }
        if (c != u'\\') {
            bytes += QString(c).toUtf8();
            wide += c;
            continue;
        }
        if (++i + 1 > code.size() - 1) {
            return std::nullopt;
        }
        c = code[i];
        uint value = 0;
        if (c == u'x') {
            int digits = 0;
            while (i + 1 < code.size() - 1 && QStringLiteral("0123456789abcdefABCDEF").contains(code[i + 1])) {
                value = value * 16 + static_cast<uint>(QString(code[++i]).toUInt(nullptr, 16));
                ++digits;
            }
            if (digits == 0) {
                return std::nullopt;
            }
        } else if (c >= u'0' && c <= u'7') {
            value = c.unicode() - '0';
            for (int k = 0; k < 2 && i + 1 < code.size() - 1 && code[i + 1] >= u'0' && code[i + 1] <= u'7'; ++k) {
                value = value * 8 + (code[++i].unicode() - '0');
            }
        } else if (c == u'n') {
            value = '\n';
        } else if (c == u'r') {
            value = '\r';
        } else if (c == u't') {
            value = '\t';
        } else if (c == u'\\' || c == u'"' || c == u'\'' || c == u'?') {
            value = c.unicode();
        } else {
            return std::nullopt;
        }
        if (literal.kind == Literal::Wide) {
            wide += QChar(static_cast<char16_t>(value));
        } else {
            bytes += static_cast<char>(value & 0xFF);
        }
    }
    literal.bytes = bytes;
    literal.text = literal.kind == Literal::Wide ? wide : QString::fromUtf8(bytes);
    return literal;
}

/// A C++ character literal ('\n', L'x', 'c').
std::optional<Literal> CppChar(QString code) {
    code = code.trimmed();
    if (code.startsWith(u'L')) {
        code = code.mid(1);
    }
    if (code.size() < 3 || !code.startsWith(u'\'') || !code.endsWith(u'\'')) {
        return std::nullopt;
    }
    return CppLiteral(u'"' + code.mid(1, code.size() - 2) + u'"');
}

bool Ascii(const QByteArray& bytes) {
    for (const char c : bytes) {
        if (static_cast<unsigned char>(c) >= 0x80) {
            return false;
        }
    }
    return true;
}

/// A literal as a filter's text, for a hook of encoding `text`: `bytes` true when it must be
/// matched against the game's bytes (a Shift-JIS hook's own bytes); nothing when it cannot be.
std::optional<std::pair<QString, bool>> FilterText(const Literal& literal, const QString& text) {
    if (literal.kind != Literal::Plain || Ascii(literal.bytes)) {
        return std::make_pair(literal.text, false);
    }
    if (text == QStringLiteral("sjis")) {
        return std::make_pair(QString::fromLatin1(literal.bytes), true); // bytes, kept one a char
    }
    if (text == QStringLiteral("utf8")) {
        return std::make_pair(QString::fromUtf8(literal.bytes), false);
    }
    return std::nullopt;
}

/// LunaHook's filter functions by name, their bodies, from every file read.
using Bodies = QMap<QString, QString>;

Bodies FilterBodies(const QString& source) {
    Bodies bodies;
    static const QRegularExpression head(QStringLiteral(R"(void\s+(\w+)\s*\(\s*TextBuffer\s*\*\s*\w*\s*,\s*HookParam\s*\*\s*\w*\s*\)\s*\{)"));
    auto it = head.globalMatch(source);
    while (it.hasNext()) {
        const auto match = it.next();
        bodies.insert(match.captured(1), Braced(source, match.capturedEnd() - 1));
    }
    return bodies;
}

/// LunaHook's filter function as our filters, for a hook of encoding `text`: nothing, with
/// `why`, where it does anything else.
std::optional<QList<QStringList>> Filters(const QString& name, const Bodies& bodies, const QString& text, QString& why, int depth = 0) {
    if (name == QStringLiteral("0") || name == QStringLiteral("nullptr")) {
        return QList<QStringList>{};
    }
    if (name == QStringLiteral("NewLineCharFilterA") || name == QStringLiteral("NewLineCharFilterW")) {
        return QList<QStringList>{{QStringLiteral("cut"), QStringLiteral("\n")}};
    }
    if (name == QStringLiteral("NewLineCharToSpaceA") || name == QStringLiteral("NewLineCharToSpaceW")) {
        return QList<QStringList>{{QStringLiteral("swap"), QStringLiteral("\n"), QStringLiteral(" ")}};
    }
    if (name == QStringLiteral("all_ascii_Filter")) {
        return QList<QStringList>{{QStringLiteral("japanese")}};
    }
    if (depth > 4 || !bodies.contains(name)) {
        why = name.contains(u'<') ? QStringLiteral("a filter made from a template") : QStringLiteral("a filter not in the tables");
        return std::nullopt;
    }
    QString body = Uncommented(bodies.value(name));
    // An if's one statement written in braces, as a plain if.
    static const QRegularExpression braced_if(QStringLiteral(R"(\)\s*\{\s*([^{};]*;)\s*\})"));
    body.replace(braced_if, QStringLiteral(") \\1"));
    if (Unquoted(body).contains(u'{')) {
        why = QStringLiteral("a filter with blocks of its own");
        return std::nullopt;
    }
    QList<QStringList> out;
    QString cache, last;
    const QString lit = QStringLiteral(R"(((?:u8|L)?R?"(?:[^"\\]|\\.)*"|(?:u8|L)?R"[^(]*\(.*?\)[^"]*"))");
    const auto re = [](const QString& pattern) {
        return QRegularExpression(QStringLiteral("^") + pattern + QStringLiteral("$"), QRegularExpression::DotMatchesEverythingOption);
    };
    const QRegularExpression declare = re(QStringLiteral(R"((?:const\s+)?(?:auto|std::w?string|std::w?string_view)\s*&{0,2}\s*\w+\s*=\s*buffer->(?:strA|strAW|strW|strU|viewA|viewW)\(\))"));
    const QRegularExpression give = re(QStringLiteral(R"(buffer->(?:from|fromWA|fromW|fromA)\(\w+\))"));
    const QRegularExpression sub = re(QStringLiteral(R"((\w+)\s*=\s*re::sub\(\s*\1\s*,\s*)") + lit + QStringLiteral(R"((?:\s*,\s*)") + lit + QStringLiteral(R"()?\s*\))"));
    const QRegularExpression cut = re(QStringLiteral(R"(StringFilter\(\s*buffer\s*,\s*TEXTANDLEN\(\s*)") + lit + QStringLiteral(R"(\s*\)\s*\))"));
    const QRegularExpression swap = re(QStringLiteral(R"(StringReplacer\(\s*buffer\s*,\s*TEXTANDLEN\(\s*)") + lit + QStringLiteral(R"(\s*\)\s*,\s*TEXTANDLEN\(\s*)") + lit + QStringLiteral(R"(\s*\)\s*\))"));
    const QRegularExpression char_swap = re(QStringLiteral(R"(StringCharReplacer\(\s*buffer\s*,\s*TEXTANDLEN\(\s*)") + lit + QStringLiteral(R"(\s*\)\s*,\s*(L?'(?:[^'\\]|\\.)+')\s*\))"));
    const QRegularExpression char_cut = re(QStringLiteral(R"(CharFilter\(\s*buffer\s*,\s*(L?'(?:[^'\\]|\\.)+')\s*\))"));
    const QRegularExpression char_char = re(QStringLiteral(R"(CharReplacer\(\s*buffer\s*,\s*(L?'(?:[^'\\]|\\.)+')\s*,\s*(L?'(?:[^'\\]|\\.)+')\s*\))"));
    const QRegularExpression replace = re(QStringLiteral(R"(strReplace\(\s*\w+\s*,\s*)") + lit + QStringLiteral(R"((?:\s*,\s*)") + lit + QStringLiteral(R"()?\s*\))"));
    const QRegularExpression lru = re(QStringLiteral(R"(static\s+lru_cache<\s*std::w?string\s*>\s*(\w+)\s*\(\s*(\d+)\s*\))"));
    const QRegularExpression touch = re(QStringLiteral(R"(if\s*\(\s*(\w+)\.touch\(\s*\w+\s*\)\s*\)\s*return\s+buffer->clear\(\))"));
    const QRegularExpression static_last = re(QStringLiteral(R"(static\s+std::w?string\s+(\w+))"));
    const QRegularExpression same = re(QStringLiteral(R"(if\s*\(\s*(\w+)\s*==\s*(\w+)\s*\)\s*return\s+buffer->clear\(\))"));
    const QRegularExpression same_text = re(QStringLiteral(R"(if\s*\(\s*\w+\s*==\s*)") + lit + QStringLiteral(R"(\s*\)\s*return\s+buffer->clear\(\))"));
    const QRegularExpression assign = re(QStringLiteral(R"((\w+)\s*=\s*(\w+))"));
    const QRegularExpression ascii = re(QStringLiteral(R"(if\s*\(\s*all_ascii\(\s*\w+\s*\)\s*\)\s*(?:return\s+)?buffer->clear\(\))"));
    const QRegularExpression split = re(QStringLiteral(R"((\w+)\s*=\s*strSplit\(\s*\1\s*,\s*)") + lit + QStringLiteral(R"(\s*\)\s*\[\s*0\s*\])"));
    const QRegularExpression call = re(QStringLiteral(R"((?:return\s+)?(\w+)\(\s*buffer\s*,\s*hp\s*\))"));
    const auto text_of = [&](const QString& code, QString& value, bool& bytes) {
        const auto literal = CppLiteral(code);
        if (!literal) {
            return false;
        }
        const auto as = FilterText(*literal, text);
        if (!as) {
            return false;
        }
        value = as->first;
        bytes = as->second;
        return true;
    };
    for (const QString& statement : SplitTop(body, u';')) {
        QRegularExpressionMatch m;
        QString a, b;
        bool a_bytes = false, b_bytes = false;
        if (statement.isEmpty() || declare.match(statement).hasMatch() || give.match(statement).hasMatch()) {
            continue;
        }
        if ((m = sub.match(statement)).hasMatch()) {
            const auto pattern = CppLiteral(m.captured(2));
            const auto replacement = m.captured(3).isEmpty() ? std::optional<Literal>(Literal{}) : CppLiteral(m.captured(3));
            if (!pattern || !replacement) {
                why = QStringLiteral("a regular expression that could not be read");
                return std::nullopt;
            }
            // A pattern of a Shift-JIS hook's bytes (\x81\x40) means bytes, not characters.
            static const QRegularExpression byte_escape(QStringLiteral(R"(\\x[89a-fA-F][0-9a-fA-F])"));
            if (text == QStringLiteral("sjis") && pattern->kind == Literal::Plain &&
                (!Ascii(pattern->bytes) || byte_escape.match(pattern->text).hasMatch())) {
                why = QStringLiteral("a regular expression of Shift-JIS bytes");
                return std::nullopt;
            }
            out.append({QStringLiteral("re"), pattern->text, replacement->text});
        } else if ((m = cut.match(statement)).hasMatch() && text_of(m.captured(1), a, a_bytes)) {
            out.append({a_bytes ? QStringLiteral("cutb") : QStringLiteral("cut"), a});
        } else if ((m = swap.match(statement)).hasMatch() && text_of(m.captured(1), a, a_bytes) && text_of(m.captured(2), b, b_bytes)) {
            if (a_bytes || b_bytes) {
                if (!a_bytes) {
                    a = QString::fromLatin1(a.toUtf8());
                }
                if (!b_bytes) {
                    b = QString::fromLatin1(b.toUtf8());
                }
            }
            out.append({a_bytes || b_bytes ? QStringLiteral("swapb") : QStringLiteral("swap"), a, b});
        } else if ((m = char_swap.match(statement)).hasMatch() && text_of(m.captured(1), a, a_bytes) && CppChar(m.captured(2))) {
            out.append({a_bytes ? QStringLiteral("swapb") : QStringLiteral("swap"), a, CppChar(m.captured(2))->text});
        } else if ((m = char_cut.match(statement)).hasMatch() && CppChar(m.captured(1))) {
            out.append({QStringLiteral("cut"), CppChar(m.captured(1))->text});
        } else if ((m = char_char.match(statement)).hasMatch() && CppChar(m.captured(1)) && CppChar(m.captured(2))) {
            out.append({QStringLiteral("swap"), CppChar(m.captured(1))->text, CppChar(m.captured(2))->text});
        } else if ((m = replace.match(statement)).hasMatch() && text_of(m.captured(1), a, a_bytes) &&
                   (m.captured(2).isEmpty() || text_of(m.captured(2), b, b_bytes)) && !a_bytes && !b_bytes) {
            out.append({QStringLiteral("swap"), a, b});
        } else if ((m = lru.match(statement)).hasMatch()) {
            cache = m.captured(1) + u'\t' + m.captured(2);
        } else if ((m = touch.match(statement)).hasMatch() && cache.section(u'\t', 0, 0) == m.captured(1)) {
            out.append({QStringLiteral("once"), cache.section(u'\t', 1, 1)});
        } else if ((m = static_last.match(statement)).hasMatch()) {
            last = m.captured(1);
        } else if ((m = same.match(statement)).hasMatch() && !last.isEmpty() && (m.captured(1) == last || m.captured(2) == last)) {
            out.append({QStringLiteral("once"), QStringLiteral("1")});
        } else if ((m = assign.match(statement)).hasMatch() && !last.isEmpty() && m.captured(1) == last) {
            continue; // the line kept for the next time: "once" does it
        } else if ((m = same_text.match(statement)).hasMatch() && text_of(m.captured(1), a, a_bytes) && !a_bytes) {
            out.append({QStringLiteral("skip"), a});
        } else if (ascii.match(statement).hasMatch()) {
            out.append({QStringLiteral("japanese")});
        } else if ((m = split.match(statement)).hasMatch() && text_of(m.captured(2), a, a_bytes) && !a_bytes) {
            out.append({QStringLiteral("upto"), a});
        } else if ((m = call.match(statement)).hasMatch()) {
            const auto inner = Filters(m.captured(1), bodies, text, why, depth + 1);
            if (!inner) {
                return std::nullopt;
            }
            out += *inner;
        } else {
            why = QStringLiteral("a filter that does more than cut, swap and replace");
            return std::nullopt;
        }
    }
    return out;
}

QString ConsoleOfLunaFile(const QString& file) {
    const QString name = file.toLower();
    if (name.startsWith(QStringLiteral("ppsspp"))) {
        return QStringLiteral("psp");
    }
    if (name.startsWith(QStringLiteral("pcsx2"))) {
        return QStringLiteral("ps2");
    }
    if (name.startsWith(QStringLiteral("vita3k"))) {
        return QStringLiteral("psvita");
    }
    if (name.startsWith(QStringLiteral("yuzu"))) {
        return QStringLiteral("switch");
    }
    return {};
}

/// Where a Switch hook goes: its address in the game's executable (the emulators' tables count
/// from 0x80004000, where yuzu puts a 64-bit game's main), or nothing for a 32-bit game.
std::optional<quint64> SwitchOffset(quint64 address) {
    if (address < 0x80004000ULL) {
        return std::nullopt;
    }
    return address - 0x80004000ULL;
}

void AddHook(QList<Game>& games, const Game& of, const Hook& hook) {
    for (Game& game : games) {
        if (game.console == of.console && game.ids == of.ids && game.version == of.version && game.source == of.source && game.from == of.from) {
            game.hooks.append(hook);
            return;
        }
    }
    Game game = of;
    game.hooks = {hook};
    games.append(game);
}

void CountGames(Count* count, const QList<Game>& games) {
    if (count == nullptr) {
        return;
    }
    QMap<QString, QStringList> seen;
    for (const Game& game : games) {
        for (const QString& id : game.ids) {
            if (!seen[game.console].contains(id)) {
                seen[game.console].append(id);
            }
        }
    }
    for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
        count->games[it.key()] += static_cast<int>(it.value().size());
    }
}

// ---- JavaScript (Agent's scripts) ----

/// A JavaScript string literal ('…', "…", `…` without ${}), unescaped.
std::optional<QString> JsString(QString code) {
    code = code.trimmed();
    if (code.size() < 2 || (code[0] != u'\'' && code[0] != u'"' && code[0] != u'`') || code.back() != code[0]) {
        return std::nullopt;
    }
    if (code[0] == u'`' && code.contains(QStringLiteral("${"))) {
        return std::nullopt;
    }
    QString out;
    for (qsizetype i = 1; i + 1 < code.size(); ++i) {
        QChar c = code[i];
        if (c != u'\\') {
            out += c;
            continue;
        }
        c = code[++i];
        if (c == u'n') {
            out += u'\n';
        } else if (c == u'r') {
            out += u'\r';
        } else if (c == u't') {
            out += u'\t';
        } else if (c == u'u' && code.mid(i + 1, 1) == QStringLiteral("{")) {
            const qsizetype close = code.indexOf(u'}', i);
            out += QString::fromUcs4(std::array<char32_t, 1>{static_cast<char32_t>(code.mid(i + 2, close - i - 2).toUInt(nullptr, 16))}.data(), 1);
            i = close;
        } else if (c == u'u') {
            out += QChar(static_cast<char16_t>(code.mid(i + 1, 4).toUShort(nullptr, 16)));
            i += 4;
        } else if (c == u'x') {
            out += QChar(static_cast<char16_t>(code.mid(i + 1, 2).toUShort(nullptr, 16)));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

/// A JavaScript regular expression literal at `at` (its opening slash): pattern, flags, and the
/// place after it.
std::optional<std::tuple<QString, QString, qsizetype>> JsRegex(const QString& code, qsizetype at) {
    if (at >= code.size() || code[at] != u'/') {
        return std::nullopt;
    }
    bool in_class = false;
    for (qsizetype i = at + 1; i < code.size(); ++i) {
        const QChar c = code[i];
        if (c == u'\\') {
            ++i;
        } else if (c == u'[') {
            in_class = true;
        } else if (c == u']') {
            in_class = false;
        } else if (c == u'/' && !in_class) {
            qsizetype end = i + 1;
            while (end < code.size() && code[end].isLetter()) {
                ++end;
            }
            return std::make_tuple(code.mid(at + 1, i - at - 1), code.mid(i + 1, end - i - 1), end);
        } else if (c == u'\n') {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

/// `.replace(…)`, `.replaceAll(…)`, `.trim()` one after another, as filters; nothing where
/// the chain has anything else.
std::optional<QList<QStringList>> JsChain(QString chain) {
    QList<QStringList> out;
    chain = chain.trimmed();
    while (!chain.isEmpty()) {
        if (chain.startsWith(QStringLiteral(".trim()"))) {
            out.append({QStringLiteral("trim")});
            chain = chain.mid(7).trimmed();
            continue;
        }
        const bool all = chain.startsWith(QStringLiteral(".replaceAll("));
        if (!all && !chain.startsWith(QStringLiteral(".replace("))) {
            return std::nullopt;
        }
        qsizetype at = all ? 12 : 9;
        while (at < chain.size() && chain[at].isSpace()) {
            ++at;
        }
        QString pattern, flags, plain;
        bool is_regex = false;
        if (const auto regex = JsRegex(chain, at)) {
            std::tie(pattern, flags, at) = *regex;
            is_regex = true;
        } else {
            const qsizetype comma = SplitTop(chain.mid(at), u',').value(0).size();
            const auto text = JsString(chain.mid(at, comma));
            if (!text) {
                return std::nullopt;
            }
            plain = *text;
            at += comma;
        }
        // The replacement, to the call's closing bracket.
        int depth = 1;
        qsizetype end = at;
        QChar quote;
        for (; end < chain.size(); ++end) {
            const QChar c = chain[end];
            if (!quote.isNull()) {
                if (c == u'\\') {
                    ++end;
                } else if (c == quote) {
                    quote = QChar();
                }
                continue;
            }
            if (c == u'\'' || c == u'"' || c == u'`') {
                quote = c;
            } else if (c == u'(') {
                ++depth;
            } else if (c == u')' && --depth == 0) {
                break;
            }
        }
        QString rest = chain.mid(at, end - at).trimmed();
        if (!rest.startsWith(u',')) {
            return std::nullopt;
        }
        const auto replacement = JsString(rest.mid(1));
        if (!replacement) {
            return std::nullopt; // a function as the replacement
        }
        if (is_regex) {
            QString ours;
            for (const QChar f : flags) {
                if (f == u'i' || f == u'm' || f == u's') {
                    ours += f;
                } else if (f != u'g' && f != u'u') {
                    return std::nullopt;
                }
            }
            out.append({QStringLiteral("re"), pattern, *replacement, ours});
        } else {
            out.append({QStringLiteral("swap"), plain, *replacement});
        }
        chain = chain.mid(end + 1).trimmed();
    }
    return out;
}

struct JsRead {
    QString index;   ///< The register's index: a number, or the handler's parameter's name
    qint64 add{};
    QString text;    ///< "sjis", "utf8", "utf16"
    QList<QStringList> filters;
};

/// An Agent handler's body as a read and filters: nothing where it does anything else.
std::optional<JsRead> JsHandler(const QString& body_, QString& why) {
    QString body = Uncommented(body_);
    JsRead read;
    QMap<QString, QPair<QString, qint64>> pointers; // a variable holding regs[i].value(.add(n))
    QMap<QString, bool> texts;                      // a variable holding the text
    static const QRegularExpression pointer(QStringLiteral(R"(^regs\[\s*(\w+)\s*\]\.value(?:\.add\(\s*(0x[0-9a-fA-F]+|\d+)\s*\))?)"));
    static const QRegularExpression reading(QStringLiteral(R"(^\.read(ShiftJis|Utf8|Utf16)String\(\s*\))"));
    static const QRegularExpression declared(QStringLiteral(R"(^(?:(?:const|let|var)\s+)?(\w+)\s*=\s*(.*)$)"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression noise(QStringLiteral(R"(^(?:console\.\w+\(.*\)|hexdump\(.*\))$)"), QRegularExpression::DotMatchesEverythingOption);
    bool returned = false;
    // A value: a pointer, or text (read, or a text variable with its chain).
    const auto value = [&](const QString& expression, QString& kind, QPair<QString, qint64>& at, QList<QStringList>& chain) {
        QString rest = expression.trimmed();
        QRegularExpressionMatch m = pointer.match(rest);
        if (m.hasMatch()) {
            at = {m.captured(1), m.captured(2).isEmpty() ? 0 : m.captured(2).toLongLong(nullptr, 0)};
            rest = rest.mid(m.capturedEnd());
            kind = QStringLiteral("pointer");
        } else {
            static const QRegularExpression word(QStringLiteral(R"(^(\w+))"));
            m = word.match(rest);
            if (!m.hasMatch()) {
                return false;
            }
            if (pointers.contains(m.captured(1))) {
                at = pointers.value(m.captured(1));
                kind = QStringLiteral("pointer");
            } else if (texts.contains(m.captured(1))) {
                kind = QStringLiteral("text");
            } else {
                return false;
            }
            rest = rest.mid(m.capturedEnd());
        }
        if (kind == QStringLiteral("pointer")) {
            m = reading.match(rest);
            if (!m.hasMatch()) {
                return rest.trimmed().isEmpty();
            }
            const QString how = m.captured(1);
            const QString text = how == QStringLiteral("ShiftJis") ? QStringLiteral("sjis") : how == QStringLiteral("Utf8") ? QStringLiteral("utf8") : QStringLiteral("utf16");
            if (!read.text.isEmpty() && (read.text != text || read.index != at.first || read.add != at.second)) {
                return false; // two reads
            }
            read.text = text;
            read.index = at.first;
            read.add = at.second;
            rest = rest.mid(m.capturedEnd());
            kind = QStringLiteral("text");
        }
        const auto filters = JsChain(rest);
        if (!filters) {
            return false;
        }
        chain = *filters;
        return true;
    };
    for (const QString& statement : SplitTop(body, u';')) {
        if (statement.isEmpty() || noise.match(statement).hasMatch()) {
            continue;
        }
        if (returned) {
            why = QStringLiteral("a handler that goes on after it returns");
            return std::nullopt;
        }
        QString kind;
        QPair<QString, qint64> at;
        QList<QStringList> chain;
        if (statement.startsWith(QStringLiteral("return "))) {
            if (!value(statement.mid(7), kind, at, chain) || kind != QStringLiteral("text")) {
                why = QStringLiteral("a handler that returns something worked out");
                return std::nullopt;
            }
            read.filters += chain;
            returned = true;
            continue;
        }
        const QRegularExpressionMatch m = declared.match(statement);
        if (m.hasMatch() && value(m.captured(2), kind, at, chain)) {
            if (kind == QStringLiteral("pointer")) {
                pointers.insert(m.captured(1), at);
            } else {
                texts.insert(m.captured(1), true);
                read.filters += chain;
            }
            continue;
        }
        why = QStringLiteral("a handler that does more than read and replace");
        return std::nullopt;
    }
    if (!returned || read.text.isEmpty()) {
        why = QStringLiteral("a handler that reads no string itself");
        return std::nullopt;
    }
    return read;
}

/// What an Agent script's console is, by its file's name.
QString ConsoleOfAgentFile(const QString& file) {
    if (file.startsWith(QStringLiteral("PSP_"))) {
        return QStringLiteral("psp");
    }
    if (file.startsWith(QStringLiteral("PSVita_"))) {
        return QStringLiteral("psvita");
    }
    if (file.startsWith(QStringLiteral("NS_"))) {
        return QStringLiteral("switch");
    }
    if (file.startsWith(QStringLiteral("PS2_"))) {
        return QStringLiteral("ps2");
    }
    if (file.startsWith(QStringLiteral("3DS_"))) {
        return QStringLiteral("3ds");
    }
    return {};
}

QString LuaString(const QString& text, bool bytes) {
    const QByteArray data = bytes ? text.toLatin1() : text.toUtf8();
    QByteArray out = "\"";
    for (const char c : data) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20 || u == 0x7F || (bytes && u >= 0x80)) {
            out += QStringLiteral("\\%1").arg(u, 3, 10, QLatin1Char('0')).toLatin1();
        } else {
            out += c; // UTF-8 as it is: Lua keeps a string's bytes
        }
    }
    out += '"';
    return QString::fromUtf8(out);
}

} // namespace

// ---- LunaHook ----

QList<Game> ConvertLunaSource(const QString& file_console, const QString& source, Count* count) {
    return [&] {
        QList<Game> games;
        const QString console = ConsoleOfLunaFile(file_console);
        if (console.isEmpty()) {
            return games;
        }
        const Bodies bodies = FilterBodies(source);
        static const QRegularExpression row(QStringLiteral(R"(^\s*\{\s*(0x[0-9A-Fa-f]+)\s*,\s*\{(.*)\}\s*\}\s*,?\s*(?://(.*))?$)"), QRegularExpression::MultilineOption);
        static const QRegularExpression id_text(QStringLiteral(R"("([^"]+)\")"));
        static const QRegularExpression id_number(QStringLiteral(R"(0x([0-9A-Fa-f]{16})ull)"));
        static const QRegularExpression reg_name(QStringLiteral(R"(PCSX2_REG_OFFSET\(\s*(\w+)\s*\))"));
        static const QStringList mips{QStringLiteral("r0"), QStringLiteral("at"), QStringLiteral("v0"), QStringLiteral("v1"), QStringLiteral("a0"), QStringLiteral("a1"),
                                      QStringLiteral("a2"), QStringLiteral("a3"), QStringLiteral("t0"), QStringLiteral("t1"), QStringLiteral("t2"), QStringLiteral("t3"),
                                      QStringLiteral("t4"), QStringLiteral("t5"), QStringLiteral("t6"), QStringLiteral("t7"), QStringLiteral("s0"), QStringLiteral("s1"),
                                      QStringLiteral("s2"), QStringLiteral("s3"), QStringLiteral("s4"), QStringLiteral("s5"), QStringLiteral("s6"), QStringLiteral("s7"),
                                      QStringLiteral("t8"), QStringLiteral("t9"), QStringLiteral("k0"), QStringLiteral("k1"), QStringLiteral("gp"), QStringLiteral("sp"),
                                      QStringLiteral("s8"), QStringLiteral("ra")};
        auto it = row.globalMatch(source);
        while (it.hasNext()) {
            const auto match = it.next();
            if (count != nullptr) {
                ++count->entries;
            }
            const quint64 address = match.captured(1).toULongLong(nullptr, 16);
            const QStringList parts = SplitTop(match.captured(2), u',');
            if (parts.size() < 6) {
                Tally(count, QStringLiteral("a row that could not be read"));
                continue;
            }
            Game game;
            game.console = console;
            game.source = QStringLiteral("LunaHook");
            game.from = file_console + QStringLiteral(".cpp");
            const QString ids = parts.mid(5, console == QStringLiteral("switch") ? 1 : -1).join(u',');
            for (auto m = id_text.globalMatch(ids); m.hasNext();) {
                game.ids.append(m.next().captured(1));
            }
            for (auto m = id_number.globalMatch(ids); m.hasNext();) {
                game.ids.append(m.next().captured(1).toUpper());
            }
            if (console == QStringLiteral("switch") && parts.size() > 6) {
                game.version = id_text.match(parts[6]).captured(1);
            }
            if (game.ids.isEmpty()) {
                Tally(count, QStringLiteral("no game ID"));
                continue;
            }
            Hook hook;
            hook.name = QStringLiteral("%1").arg(address, 0, 16).toUpper().prepend(QStringLiteral("0x"));
            hook.address = address;
            hook.whole = false;
            bool bad = false;
            QString why;
            for (QString flag : parts[0].split(u'|')) {
                flag = flag.trimmed();
                if (flag == QStringLiteral("0") || flag == QStringLiteral("NO_CONTEXT") || flag == QStringLiteral("USING_STRING") || flag == QStringLiteral("CODEC_ANSI_LE")) {
                } else if (flag == QStringLiteral("FULL_STRING")) {
                    hook.whole = true;
                } else if (flag == QStringLiteral("CODEC_UTF16")) {
                    hook.text = QStringLiteral("utf16");
                } else if (flag == QStringLiteral("CODEC_UTF8")) {
                    hook.text = QStringLiteral("utf8");
                } else if (flag == QStringLiteral("DIRECT_READ")) {
                    hook.read = true;
                } else if (flag == QStringLiteral("USING_CHAR")) {
                    bad = true;
                    why = QStringLiteral("a character a call");
                } else if (flag == QStringLiteral("DATA_INDIRECT")) {
                    bad = true;
                    why = QStringLiteral("a pointer to a pointer");
                } else {
                    bad = true;
                    why = QStringLiteral("a flag of its own (%1)").arg(flag);
                }
            }
            if (!bad && parts[3] != QStringLiteral("0") && parts[3] != QStringLiteral("nullptr")) {
                bad = true;
                why = QStringLiteral("text read by a function of its own");
            }
            if (bad) {
                Tally(count, why);
                continue;
            }
            // Where the text's pointer is: the argument's register.
            const QString offset = parts[1];
            int index = -1;
            if (const auto name = reg_name.match(offset); name.hasMatch()) {
                index = static_cast<int>(mips.indexOf(name.captured(1)));
            } else {
                bool ok = false;
                index = offset.toInt(&ok, 0);
                if (!ok) {
                    index = -1;
                }
            }
            if (!hook.read) {
                if (index < 0) {
                    Tally(count, QStringLiteral("a register that could not be read"));
                    continue;
                }
                if (console == QStringLiteral("ps2")) {
                    // PCSX2's and ARMSX2's cores carry out no hooks: their games are read from memory.
                    Tally(count, QStringLiteral("a code hook on the PS2 (our PS2 cores run none)"));
                    continue;
                }
                if (console == QStringLiteral("psp")) {
                    hook.from = QStringLiteral("r%1").arg(4 + index); // the arguments from a0
                } else if (console == QStringLiteral("psvita")) {
                    hook.from = QStringLiteral("r%1").arg(index);
                } else {
                    const auto at = SwitchOffset(address);
                    if (!at) {
                        Tally(count, QStringLiteral("a 32-bit Switch game"));
                        continue;
                    }
                    hook.address = *at;
                    hook.relative = true;
                    hook.from = QStringLiteral("x%1").arg(index);
                    hook.wide = true;
                }
            } else if (console == QStringLiteral("switch")) {
                Tally(count, QStringLiteral("memory read on the Switch"));
                continue;
            }
            hook.add = parts[2].toLongLong(nullptr, 0);
            const auto filters = Filters(parts[4], bodies, hook.text, why);
            if (!filters) {
                Tally(count, why);
                continue;
            }
            hook.filters = *filters;
            if (count != nullptr) {
                ++count->converted;
            }
            AddHook(games, game, hook);
        }
        return games;
    }();
}

QList<Game> ConvertLuna(const QString& folder, Count* count) {
    QList<Game> games;
    for (const QString& name : {QStringLiteral("ppsspp_1"), QStringLiteral("PCSX2_1"), QStringLiteral("vita3k_1"), QStringLiteral("yuzu_1")}) {
        const QString source = ReadText(folder + u'/' + name + QStringLiteral(".cpp"));
        if (!source.isEmpty()) {
            games += ConvertLunaSource(name, source, count);
        }
    }
    CountGames(count, games);
    return games;
}

// ---- Agent ----

QList<Game> ConvertAgentSource(const QString& file, const QString& source_, Count* count) {
    QList<Game> games;
    const QString console = ConsoleOfAgentFile(file);
    if (console.isEmpty()) {
        return games;
    }
    if (count != nullptr) {
        ++count->entries;
    }
    const QString source = Uncommented(source_);
    static const QRegularExpression name_line(QStringLiteral(R"(//\s*@name\s+(.*))"));
    static const QRegularExpression version_line(QStringLiteral(R"(//\s*@version\s+(.*))"));
    const QString name = name_line.match(source_).captured(1).trimmed();
    static const QRegularExpression bracket_id(QStringLiteral(R"(^\[([0-9A-Za-z\-]+)\]\s*(.*))"));
    const auto id_match = bracket_id.match(name);
    if (!id_match.hasMatch()) {
        Tally(count, QStringLiteral("no game ID"));
        return games;
    }
    QString id = id_match.captured(1).toUpper();
    if (console == QStringLiteral("ps2") && !id.contains(u'-') && id.size() == 9) {
        id.insert(4, u'-'); // SLPM65732 is SLPM-65732 to our core
    }
    Game game;
    game.console = console;
    game.ids = {id};
    game.name = id_match.captured(2).trimmed();
    game.source = QStringLiteral("Agent");
    game.from = file;
    game.version = version_line.match(source_).captured(1).trimmed();
    // The handlers: NAME = trans.send(FUNCTION, …).
    QMap<QString, QString> handlers;
    static const QRegularExpression send(QStringLiteral(R"((?:const|let|var)\s+(\w+)\s*=\s*trans\.send\(\s*(\w+))"));
    for (auto it = send.globalMatch(source); it.hasNext();) {
        const auto m = it.next();
        handlers.insert(m.captured(1), m.captured(2));
    }
    static const QRegularExpression set_hook(QStringLiteral(R"(\bsetHook(EE)?\s*\(\s*\{)"));
    const auto set = set_hook.match(source);
    if (!set.hasMatch()) {
        Tally(count, QStringLiteral("no hook table"));
        return games;
    }
    if (!set.captured(1).isEmpty()) {
        Tally(count, QStringLiteral("a PS2 handler that reads the processor's state itself"));
        return games;
    }
    QString table = Braced(source, set.capturedEnd() - 1);
    // A table by the game's version: the first one's (the one the script says is the game's).
    static const QRegularExpression versioned(QStringLiteral(R"(^\s*['"]([\d.]+)['"]\s*:\s*\{)"));
    if (const auto v = versioned.match(table); v.hasMatch()) {
        game.version = v.captured(1);
        table = Braced(table, v.capturedEnd() - 1);
    }
    static const QRegularExpression entry(QStringLiteral(R"(^\[?\s*(0x[0-9A-Fa-f]+)\s*(?:-\s*(0x[0-9A-Fa-f]+))?\s*\]?\s*:\s*(\w+)(?:\.bind_?\(\s*null\s*(?:,\s*(.*))?\))?\s*$)"),
                                             QRegularExpression::DotMatchesEverythingOption);
    QString why;
    int converted = 0, entries = 0;
    for (const QString& line : SplitTop(table, u',')) {
        if (line.isEmpty()) {
            continue;
        }
        ++entries;
        const auto m = entry.match(line);
        if (!m.hasMatch()) {
            why = QStringLiteral("a hook table entry that could not be read");
            continue;
        }
        quint64 address = m.captured(1).toULongLong(nullptr, 16);
        if (!m.captured(2).isEmpty()) {
            address -= m.captured(2).toULongLong(nullptr, 16);
            if (console == QStringLiteral("switch")) {
                address += 0x80004000ULL; // [0x8xxxxxxx - 0x80004000]: counted as the tables count
            }
        }
        const QString function = handlers.value(m.captured(3), m.captured(3));
        static const QRegularExpression function_head(QStringLiteral(R"(function\s+(\w+)\s*\(([^)]*)\)\s*\{)"));
        QString body, parameters;
        for (auto f = function_head.globalMatch(source); f.hasNext();) {
            const auto fm = f.next();
            if (fm.captured(1) == function) {
                parameters = fm.captured(2);
                body = Braced(source, fm.capturedEnd() - 1);
                break;
            }
        }
        if (body.isEmpty()) {
            why = QStringLiteral("a handler not found");
            continue;
        }
        const auto read = JsHandler(body, why);
        if (!read) {
            continue;
        }
        // The register: a number written in the handler, or the bound argument of its parameter.
        const QStringList names = SplitTop(parameters, u',');
        const QStringList bound = SplitTop(m.captured(4), u',');
        bool number = false;
        int index = read->index.toInt(&number, 0);
        if (!number) {
            const qsizetype at = names.indexOf(read->index);
            if (at < 1 || at - 1 >= bound.size()) {
                why = QStringLiteral("a register that could not be read");
                continue;
            }
            index = bound[at - 1].toInt(&number, 0);
            if (!number) {
                why = QStringLiteral("a register that could not be read");
                continue;
            }
        }
        Hook hook;
        hook.name = bound.size() >= 2 && JsString(bound.last()) ? *JsString(bound.last()) : QStringLiteral("0x%1").arg(m.captured(1).mid(2).toUpper());
        hook.address = address;
        hook.text = read->text;
        hook.add = read->add;
        hook.filters = read->filters;
        hook.whole = true; // a script's handler gets a whole string a call
        if (console == QStringLiteral("psp")) {
            if (index > 3) {
                why = QStringLiteral("a register that could not be read");
                continue;
            }
            hook.from = QStringLiteral("r%1").arg(4 + index);
        } else if (console == QStringLiteral("psvita")) {
            hook.from = QStringLiteral("r%1").arg(index);
        } else if (console == QStringLiteral("switch")) {
            const auto at = SwitchOffset(address);
            if (!at) {
                why = QStringLiteral("a 32-bit Switch game");
                continue;
            }
            hook.address = *at;
            hook.relative = true;
            hook.from = QStringLiteral("x%1").arg(index);
            hook.wide = true;
        } else {
            why = QStringLiteral("a console without a core's ID here");
            continue;
        }
        // Several names may be bound to one address: each is a hook of its own name.
        AddHook(games, game, hook);
        ++converted;
    }
    if (converted == 0) {
        Tally(count, why.isEmpty() ? QStringLiteral("no hook") : why);
    } else if (count != nullptr) {
        ++count->converted;
        if (converted < entries) {
            ++count->why_not[QStringLiteral("(converted in part: some hooks left out)")];
        }
    }
    return games;
}

QList<Game> ConvertAgent(const QString& folder, Count* count) {
    QList<Game> games;
    const QDir dir(folder);
    for (const QString& file : dir.entryList({QStringLiteral("*.js")}, QDir::Files, QDir::Name)) {
        if (ConsoleOfAgentFile(file).isEmpty()) {
            continue;
        }
        games += ConvertAgentSource(file, ReadText(dir.filePath(file)), count);
    }
    CountGames(count, games);
    return games;
}

// ---- The index ----

QString IndexFile(const QString& shared) {
    return shared + QStringLiteral("/program/hooks/core-hooks.json");
}

QJsonObject ToJson(const QList<Game>& games) {
    QJsonArray list;
    for (const Game& game : games) {
        QJsonArray hooks;
        for (const Hook& hook : game.hooks) {
            QJsonArray filters;
            for (const QStringList& filter : hook.filters) {
                filters.append(QJsonArray::fromStringList(filter));
            }
            QJsonObject one{{QStringLiteral("name"), hook.name},     {QStringLiteral("address"), QString::number(hook.address, 16)},
                            {QStringLiteral("text"), hook.text},     {QStringLiteral("whole"), hook.whole},
                            {QStringLiteral("filters"), filters}};
            if (hook.read) {
                one.insert(QStringLiteral("read"), true);
            } else {
                one.insert(QStringLiteral("from"), hook.from);
            }
            if (hook.relative) {
                one.insert(QStringLiteral("relative"), true);
            }
            if (hook.wide) {
                one.insert(QStringLiteral("wide"), true);
            }
            if (hook.add != 0) {
                one.insert(QStringLiteral("add"), hook.add);
            }
            hooks.append(one);
        }
        list.append(QJsonObject{{QStringLiteral("console"), game.console}, {QStringLiteral("ids"), QJsonArray::fromStringList(game.ids)},
                                {QStringLiteral("version"), game.version}, {QStringLiteral("name"), game.name},
                                {QStringLiteral("source"), game.source},   {QStringLiteral("from"), game.from},
                                {QStringLiteral("hooks"), hooks}});
    }
    return QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("games"), list}};
}

QList<Game> FromJson(const QJsonObject& json) {
    QList<Game> games;
    for (const QJsonValue& value : json.value(QStringLiteral("games")).toArray()) {
        const QJsonObject o = value.toObject();
        Game game;
        game.console = o.value(QStringLiteral("console")).toString();
        for (const QJsonValue& id : o.value(QStringLiteral("ids")).toArray()) {
            game.ids.append(id.toString());
        }
        game.version = o.value(QStringLiteral("version")).toString();
        game.name = o.value(QStringLiteral("name")).toString();
        game.source = o.value(QStringLiteral("source")).toString();
        game.from = o.value(QStringLiteral("from")).toString();
        for (const QJsonValue& h : o.value(QStringLiteral("hooks")).toArray()) {
            const QJsonObject ho = h.toObject();
            Hook hook;
            hook.name = ho.value(QStringLiteral("name")).toString();
            hook.address = ho.value(QStringLiteral("address")).toString().toULongLong(nullptr, 16);
            hook.read = ho.value(QStringLiteral("read")).toBool();
            hook.relative = ho.value(QStringLiteral("relative")).toBool();
            hook.from = ho.value(QStringLiteral("from")).toString();
            hook.wide = ho.value(QStringLiteral("wide")).toBool();
            hook.add = ho.value(QStringLiteral("add")).toInteger();
            hook.text = ho.value(QStringLiteral("text")).toString(QStringLiteral("sjis"));
            hook.whole = ho.value(QStringLiteral("whole")).toBool();
            for (const QJsonValue& f : ho.value(QStringLiteral("filters")).toArray()) {
                QStringList filter;
                for (const QJsonValue& part : f.toArray()) {
                    filter.append(part.toString());
                }
                hook.filters.append(filter);
            }
            game.hooks.append(hook);
        }
        if (!game.console.isEmpty() && !game.ids.isEmpty() && !game.hooks.isEmpty()) {
            games.append(game);
        }
    }
    return games;
}

int Rebuild(const QString& shared, QString& said) {
    Count luna, agent;
    const QString hooks = shared + QStringLiteral("/program/hooks");
    QList<Game> games = ConvertAgent(hooks + QStringLiteral("/scripts"), &agent);
    games += ConvertLuna(hooks + QStringLiteral("/luna-tables"), &luna);
    QDir().mkpath(hooks);
    QSaveFile out(IndexFile(shared));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(ToJson(games)).toJson(QJsonDocument::Compact)) < 0 || !out.commit()) {
        said = QStringLiteral("The hooks could not be kept in %1").arg(IndexFile(shared));
        return -1;
    }
    QStringList known;
    for (const Game& game : games) {
        for (const QString& id : game.ids) {
            if (!known.contains(game.console + u'/' + id)) {
                known.append(game.console + u'/' + id);
            }
        }
    }
    said = QStringLiteral("%1 games known: LunaHook %2 of %3 hooks, Agent %4 of %5 scripts").arg(known.size()).arg(luna.converted).arg(luna.entries).arg(agent.converted).arg(agent.entries);
    return static_cast<int>(known.size());
}

QList<Game> Load(const QString& shared) {
    QFile in(IndexFile(shared));
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    return FromJson(QJsonDocument::fromJson(in.readAll()).object());
}

QList<Game> Find(const QList<Game>& games, const QString& console, const QString& id) {
    QList<Game> agent, luna;
    for (const Game& game : games) {
        if (game.console == console && game.ids.contains(id, Qt::CaseInsensitive)) {
            (game.source == QStringLiteral("Agent") ? agent : luna).append(game);
        }
    }
    return agent + luna;
}

// ---- The profile ----

QString Profile(const Game& game, const QString& id, const QString& title, const QStringList& only) {
    QString out;
    if (!title.isEmpty()) {
        out += QStringLiteral("-- reader: title %1\n").arg(title);
    }
    out += QStringLiteral("-- reader: by hook\n");
    bool any_hook = false;
    for (const Hook& hook : game.hooks) {
        any_hook = any_hook || !hook.read;
    }
    if (!any_hook) {
        out += QStringLiteral("-- reader: no hooks\n");
    }
    out += QStringLiteral("-- %1 by hook description, made by Furoshiki from %2's %3%4 (docs/core-hooks.md).\n")
               .arg(id, game.source, game.from, game.version.isEmpty() ? QString() : QStringLiteral(", for its version ") + game.version);
    out += game.source == QStringLiteral("Agent") ? QStringLiteral("-- Agent's scripts: 0xDC00 and contributors, MIT (github.com/0xDC00/scripts).\n")
                                                  : QStringLiteral("-- LunaHook: HIllya51 and contributors, GPL-3.0 (github.com/HIllya51/LunaTranslator).\n");
    out += QStringLiteral("reader.hooks{\n    source = %1,\n").arg(LuaString(game.source + QStringLiteral(" ") + game.from, false));
    if (!only.isEmpty()) {
        QStringList quoted;
        for (const QString& name : only) {
            quoted.append(LuaString(name, false));
        }
        out += QStringLiteral("    only = { %1 },\n").arg(quoted.join(QStringLiteral(", ")));
    }
    for (const Hook& hook : game.hooks) {
        QStringList fields{QStringLiteral("name = ") + LuaString(hook.name, false)};
        fields.append((hook.read ? QStringLiteral("read = 0x") : QStringLiteral("at = 0x")) + QString::number(hook.address, 16).toUpper());
        if (hook.relative) {
            fields.append(QStringLiteral("base = \"main\""));
        }
        if (!hook.read) {
            fields.append(QStringLiteral("from = ") + LuaString(hook.from, false));
        }
        if (hook.wide) {
            fields.append(QStringLiteral("wide = true"));
        }
        if (hook.add != 0) {
            fields.append(QStringLiteral("add = %1").arg(hook.add));
        }
        fields.append(QStringLiteral("text = ") + LuaString(hook.text, false));
        if (hook.whole) {
            fields.append(QStringLiteral("whole = true"));
        }
        if (!hook.filters.isEmpty()) {
            QStringList filters;
            for (const QStringList& filter : hook.filters) {
                const bool bytes = filter.value(0) == QStringLiteral("cutb") || filter.value(0) == QStringLiteral("swapb");
                QStringList parts{LuaString(filter.value(0), false)};
                for (qsizetype i = 1; i < filter.size(); ++i) {
                    bool number = false;
                    filter[i].toInt(&number);
                    parts.append(filter.value(0) == QStringLiteral("once") && number ? filter[i] : LuaString(filter[i], bytes));
                }
                filters.append(QStringLiteral("{ ") + parts.join(QStringLiteral(", ")) + QStringLiteral(" }"));
            }
            fields.append(QStringLiteral("filters = { ") + filters.join(QStringLiteral(", ")) + QStringLiteral(" }"));
        }
        out += QStringLiteral("    { ") + fields.join(QStringLiteral(", ")) + QStringLiteral(" },\n");
    }
    out += QStringLiteral("}\n");
    return out;
}

bool IsDescription(const QString& source) {
    for (const QString& line : source.split(u'\n')) {
        const QString text = line.trimmed();
        if (text.isEmpty()) {
            continue;
        }
        if (!text.startsWith(QStringLiteral("--"))) {
            return false;
        }
        if (text == QStringLiteral("-- reader: by hook")) {
            return true;
        }
    }
    return false;
}

bool Install(const Game& game, const QString& id, const QString& title, const QString& profiles, QString& error) {
    const QString file = profiles + u'/' + id + QStringLiteral(".lua");
    if (QFileInfo::exists(file) && !IsDescription(ReadText(file))) {
        error = QStringLiteral("%1 has a script of its own already").arg(id);
        return false;
    }
    QDir().mkpath(profiles);
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(Profile(game, id, title).toUtf8()) < 0 || !out.commit()) {
        error = QStringLiteral("%1 could not be written").arg(file);
        return false;
    }
    return true;
}

bool Choose(const QString& profiles, const QString& id, const QStringList& only, QString& error) {
    const QString file = profiles + u'/' + id + QStringLiteral(".lua");
    QString source = ReadText(file);
    if (!IsDescription(source)) {
        error = QStringLiteral("%1 is not read by hook").arg(id);
        return false;
    }
    static const QRegularExpression old_only(QStringLiteral(R"(\n    only = \{[^\n]*\},)"));
    source.remove(old_only);
    if (!only.isEmpty()) {
        QStringList quoted;
        for (const QString& name : only) {
            quoted.append(LuaString(name, false));
        }
        source.replace(QStringLiteral("reader.hooks{\n"), QStringLiteral("reader.hooks{\n    only = { %1 },\n").arg(quoted.join(QStringLiteral(", "))));
    }
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(source.toUtf8()) < 0 || !out.commit()) {
        error = QStringLiteral("%1 could not be written").arg(file);
        return false;
    }
    return true;
}

} // namespace Home::CoreHooks
