// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include "reader/desktop/explain.h"
#include "reader/desktop/tests/test_util.h"

using namespace ReaderApp;

TEST_CASE("Explain: what Claude is asked", "[reader][explain]") {
    Explain::Request request;
    request.line = Q(u"行ってしまった。");
    REQUIRE(Explain::Prompt(request) == Q(u"The line: 行ってしまった。"));
    request.title = QStringLiteral("STEINS;GATE");
    request.names = {Q(u"紅莉栖")};
    request.before = {Q(u"一つ目"), Q(u"二つ目")};
    REQUIRE(Explain::Prompt(request) ==
            Q(u"Title: STEINS;GATE\n\nThis title's names in the line:\n- 紅莉栖\n\n"
              u"The lines just before:\n- 一つ目\n- 二つ目\n\nThe line: 行ってしまった。"));
    const QStringList arguments = Explain::Arguments({}, request);
    REQUIRE(arguments.first() == QStringLiteral("-p"));
    REQUIRE(arguments.at(arguments.indexOf(QStringLiteral("--model")) + 1) ==
            QStringLiteral("claude-sonnet-5-5"));
    REQUIRE(arguments.at(arguments.indexOf(QStringLiteral("--tools")) + 1).isEmpty());
    REQUIRE(arguments.last() == Explain::Prompt(request));
}

TEST_CASE("Explain: the answer is read, and what is no answer is said", "[reader][explain]") {
    const QByteArray good = Q(
        u"{\"is_error\":false,\"result\":\"\",\"structured_output\":{\"translation\":\"He has gone.\","
        u"\"note\":\"\",\"segments\":[{\"text\":\"行って\",\"reading\":\"いって\",\"gloss\":\"went\","
        u"\"grammar\":[]},{\"text\":\"しまった\",\"reading\":\"\",\"gloss\":\"regret\","
        u"\"grammar\":[\"〜てしまう\"]}]}}").toUtf8();
    const Explain::Answer answer = Explain::Parse(good);
    REQUIRE(answer.error.isEmpty());
    REQUIRE(answer.translation == QStringLiteral("He has gone."));
    REQUIRE(answer.segments.size() == 2);
    REQUIRE(answer.segments.at(1).grammar == QStringList{Q(u"〜てしまう")});
    REQUIRE(Explain::ToHtml(answer).contains(Q(u"〜てしまう")));

    REQUIRE(Explain::Parse("{\"is_error\":true,\"result\":\"Not logged in\"}").error ==
            QStringLiteral("Not logged in"));
    REQUIRE(Explain::Parse("", "claude: no such option").error ==
            QStringLiteral("claude: no such option"));
    REQUIRE(!Explain::Parse("{\"structured_output\":{}}").error.isEmpty());
}

TEST_CASE("Explain: a service's answer, and what it is sent", "[reader][explain]") {
    Explain::Request request;
    request.line = Q(u"行く");
    request.before = {Q(u"前")};
    request.title = QStringLiteral("STEINS;GATE");
    REQUIRE(Explain::ServiceBody(request, QStringLiteral("ULJM05718")) ==
            Q(u"{\"before\":[\"前\"],\"game\":\"STEINS;GATE\",\"game_id\":\"ULJM05718\",\"text\":\"行く\"}").toUtf8());
    const Explain::Answer answer = Explain::ParseService(
        Q(u"{\"translation\":\"To go.\",\"note\":\"\",\"cached\":false,\"segments\":[{\"text\":\"行く\","
          u"\"reading\":\"いく\",\"gloss\":\"go\",\"grammar\":[]}]}").toUtf8());
    REQUIRE(answer.error.isEmpty());
    REQUIRE(answer.segments.first().reading == Q(u"いく"));
    REQUIRE(Explain::ParseService("today's 300 explanations are used up").error ==
            QStringLiteral("today's 300 explanations are used up"));
    REQUIRE(Explain::ParseService("", "curl: (7) Failed to connect").error == QStringLiteral("curl: (7) Failed to connect"));
    REQUIRE(Explain::ParseService("{}").error == QStringLiteral("No answer from the service."));
}

TEST_CASE("Explain: answers are kept by line", "[reader][explain]") {
    QTemporaryDir folder;
    Explain::Cache cache(folder.filePath(QStringLiteral("explanations.json")));
    REQUIRE(!cache.Of(Q(u"行く")));
    Explain::Answer answer;
    answer.translation = QStringLiteral("To go.");
    answer.segments = {{Q(u"行く"), Q(u"いく"), QStringLiteral("go"), {}}};
    cache.Keep(Q(u"行く"), answer);
    cache.Keep(Q(u"来る"), Explain::Answer{}); // nothing: not kept
    REQUIRE(cache.TranslationOf(Q(u" 行く\n")) == QStringLiteral("To go."));
    REQUIRE(cache.Of(Q(u"行く"))->segments.first().reading == Q(u"いく"));
    REQUIRE(cache.TranslationOf(Q(u"来る")).isEmpty());
}

// Asks the real Claude Code, on the user's login: only when READER_EXPLAIN_LIVE is set.
TEST_CASE("Explain: a real answer", "[.][explain-live]") {
    if (qEnvironmentVariableIsEmpty("READER_EXPLAIN_LIVE")) {
        SKIP("READER_EXPLAIN_LIVE is not set");
    }
    Explain::Request request;
    request.line = Q(u"そんなこと、言わなければよかったのに。");
    request.before = {Q(u"彼女は黙って部屋を出ていった。")};
    const Explain::Answer answer = Explain::Ask({}, request);
    INFO(answer.error.toStdString());
    REQUIRE(answer.error.isEmpty());
    REQUIRE(!answer.translation.isEmpty());
    REQUIRE(answer.segments.size() >= 2);
    WARN(Explain::ToHtml(answer).toStdString());
}
