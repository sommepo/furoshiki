// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QTextLayout>
#include <QTimer>
#include <QWidget>
#include "choice_picker.h"
#include "reader/desktop/reader_database.h"
#include "reader/desktop/screen_text.h"
#include "reader/qt/lookup_flow.h"
#include "reader/qt/reader_session.h"
#ifndef READER_NO_EXPLAIN
#include "reader/desktop/explain.h"
#endif

namespace Home {

/**
 * Native in-game Backlog overlay over the paused game: matches the in-game menu's blurred
 * backdrop and shelf theme palette, displays speaker names and timestamps in large readable
 * Japanese type, and is fully navigable with a controller (no pointer required), keyboard, or mouse.
 */
class GameBacklogOverlay : public QWidget {
public:
    static GameBacklogOverlay* Open(QWidget* over, const ChoicePicker::Colours& colours,
                                    std::function<void(const ReaderApp::PresentedLine&)> on_card,
                                    std::function<void(const ReaderApp::PresentedLine&)> on_explain);

    bool Key(int key);
    void Notify(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    GameBacklogOverlay(QWidget* over, const ChoicePicker::Colours& colours,
                       std::function<void(const ReaderApp::PresentedLine&)> on_card,
                       std::function<void(const ReaderApp::PresentedLine&)> on_explain);
    void Blur();
    void LoadLines(qint64 keep_id = -1);
    [[nodiscard]] QRectF Card() const;
    [[nodiscard]] QRectF ListRect() const;
    [[nodiscard]] double MetaColWidth() const;
    [[nodiscard]] double TextWidth() const;
    void LayoutText(QTextLayout& layout, int index, double text_width) const;
    [[nodiscard]] double RowHeight(int index, double text_width) const;
    [[nodiscard]] int CharOffsetAt(int index, const QPointF& pt, double row_y, double text_x, double text_w) const;
    [[nodiscard]] QRectF SpanRect(int index, int first, int last, double row_y, double text_x, double text_w) const;
    void BuildWordsForChosen();
    void LookupAt(int index, int offset);
    void ToggleCounted(int index);
    void ClearDuplicates();
    void EnsureVisible(int index);
    void Leave();
    [[nodiscard]] ReaderApp::PresentedLine PresentedFor(int index) const;
    [[nodiscard]] ReaderApp::PresentedLine SelectedPresented() const;

    ChoicePicker::Colours colours;
    QFont jp_font, speaker_font, meta_font;
    QList<ReaderApp::BacklogEntry> entries; // oldest first, newest at bottom
    QString game_title;
    int chosen{};
    int hover{-1};
    double scroll_y{};
    double scroll_target{};
    double total_height{};
    QList<double> row_tops;
    QList<double> row_heights;
    QString notice;
    qint64 notice_until{};
    struct BtnHit {
        QRectF rect;
        int key{};
    };
    struct CountHit {
        QRectF rect;
        int row{};
    };
    QList<BtnHit> btn_hits;
    QList<CountHit> count_hits;
    std::unique_ptr<ReaderUi::LookupFlow> lookups;
    qint64 mark_id{-1};
    int mark_first{-1};
    int mark_last{-1};
    int words_row{-1};
    QList<ReaderApp::WordWalk::Span> words;
    int word_at{-1};
    std::function<void(const ReaderApp::PresentedLine&)> on_card;
    std::function<void(const ReaderApp::PresentedLine&)> on_explain;
    QPixmap behind;
    QElapsedTimer clock;
    QTimer frames;
    bool leaving{};
    qint64 left_at{};
};

#ifndef READER_NO_EXPLAIN
/**
 * Native in-game Explain Line overlay over the paused game: displays the source line, speaker,
 * English translation, per-segment grammar breakdown, and notes in Furoshiki's native style,
 * scrollable and actionable with a controller without a pointer.
 */
class GameExplainOverlay : public QWidget {
public:
    static GameExplainOverlay* Open(QWidget* over, const ChoicePicker::Colours& colours,
                                    const ReaderApp::PresentedLine& presented,
                                    std::function<void(const ReaderApp::PresentedLine&)> on_card);

    bool Key(int key);
    void Notify(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    GameExplainOverlay(QWidget* over, const ChoicePicker::Colours& colours,
                       const ReaderApp::PresentedLine& presented,
                       std::function<void(const ReaderApp::PresentedLine&)> on_card);
    void Blur();
    void Fetch(bool force);
    [[nodiscard]] QRectF Card() const;
    [[nodiscard]] QRectF BodyRect() const;
    void Leave();

    ChoicePicker::Colours colours;
    QFont jp_font, body_font, meta_font, seg_font;
    ReaderApp::PresentedLine presented;
    ReaderApp::Explain::Answer answer;
    bool asking{false};
    bool has_answer{false};
    double scroll_y{};
    double scroll_target{};
    double content_height{};
    QString notice;
    qint64 notice_until{};
    struct BtnHit {
        QRectF rect;
        int key{};
    };
    QList<BtnHit> btn_hits;
    std::function<void(const ReaderApp::PresentedLine&)> on_card;
    QPixmap behind;
    QElapsedTimer clock;
    QTimer frames;
    bool leaving{};
    qint64 left_at{};
};
#endif

} // namespace Home
