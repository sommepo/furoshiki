// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// What keeps reading a core's own picture by OCR cheap (core_ocr.h; docs/furoshiki-3.md, "A
// core's game read by OCR"): a picture goes to the helper only while what stands in the game's
// boxes changes, and a moment longer, so that the helper's stabiliser sees it stand still and
// reads it once. While the boxes look the same nothing is sent (but one picture now and then,
// which keeps the helper's feed). And where the helper's places for a line's characters, given
// in the whole picture, are on the console's screens. No Qt widgets, no network: tested.

#include <QImage>
#include <QList>
#include <QRectF>

namespace Home::OcrGate {

/// What stands in `boxes` (0-1 of the picture), small and grey: one image to compare with the
/// next picture's. Null for no picture or no box.
[[nodiscard]] QImage Signature(const QImage& picture, const QList<QRectF>& boxes);
/// The share of `now` that is unlike `before` (1 when they cannot be compared).
[[nodiscard]] double ChangedShare(const QImage& before, const QImage& now);
/// Less than this share is movement (a cursor that blinks), not new text: the stabiliser's own figure.
inline constexpr double MinChange = 0.004;

/// Whether the picture taken now is sent.
class Gate {
public:
    /// How long pictures go on being sent after the last change (the helper waits 0.4 s for the
    /// text to stand still, and reads a box that never does after 2.5 s), and how often one is
    /// sent while nothing changes.
    explicit Gate(qint64 tail_ms = 1500, qint64 keep_ms = 10000) : tail_ms(tail_ms), keep_ms(keep_ms) {}

    /// `signature` is of the picture taken at `now_ms`. True: send it.
    bool Want(const QImage& signature, qint64 now_ms);
    /// The picture Want said to send did not go (the one before is still on its way): asked again.
    void Missed() {
        sent_at = -1;
    }
    void Reset() {
        *this = Gate(tail_ms, keep_ms);
    }
    /// When the change that goes on now (or went on last) began; -1: none yet.
    [[nodiscard]] qint64 BeganAt() const {
        return began_at;
    }
    /// Pictures looked at and pictures let through, since Reset.
    [[nodiscard]] int Seen() const {
        return seen;
    }
    [[nodiscard]] int Passed() const {
        return passed;
    }

private:
    qint64 tail_ms, keep_ms;
    QImage last;
    qint64 changed_at{-1}, began_at{-1}, sent_at{-1};
    int seen{}, passed{};
};

/// A place on one of the console's screens.
struct OnScreen {
    int screen{-1}; ///< 0 the (upper) screen, 1 the lower; -1: on neither
    QRectF rect;    ///< 0-1 of that screen
};
/// Where `box` (0-1 of the whole picture, which is drawn in `picture` of the window) is on the
/// console's screens (`upper` and `lower`, where they are drawn in the window; `lower` empty
/// for a console with one): the screen its middle is on. A console whose one screen is the
/// whole picture gives the box back.
[[nodiscard]] OnScreen Place(const QRectF& box, const QRectF& picture, const QRectF& upper, const QRectF& lower);

} // namespace Home::OcrGate
