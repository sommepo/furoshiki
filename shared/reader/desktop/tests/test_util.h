// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <catch2/catch_tostring.hpp>
#include <QString>
#include <QStringList>
#include "reader/core/console.h"

/// Text of a test, as the QString the code under test works with.
inline QString Q(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// Made-up game IDs of the console's form (reader/core/console.h), and the app's key.
inline const QString TestGame = QString::fromLatin1(Reader::ConsoleTraits::Test::Game);
inline const QString TestOtherGame = QString::fromLatin1(Reader::ConsoleTraits::Test::OtherGame);
inline const QString TestUnknownGame = QString::fromLatin1(Reader::ConsoleTraits::Test::UnknownGame);
inline const QString TestScriptGame = QString::fromLatin1(Reader::ConsoleTraits::Test::ScriptGame);
inline const QString TestOtherScriptGame = QString::fromLatin1(Reader::ConsoleTraits::Test::OtherScriptGame);
inline const QString TestAppKey = QString::fromLatin1(Reader::ConsoleTraits::AppKey);

namespace Catch {
template <>
struct StringMaker<QString> {
    static std::string convert(const QString& value) {
        return '"' + value.toStdString() + '"';
    }
};
template <>
struct StringMaker<QStringList> {
    static std::string convert(const QStringList& value) {
        return '[' + value.join(QStringLiteral(", ")).toStdString() + ']';
    }
};
} // namespace Catch
