// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PCSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "reader/core/next_choice.h"

namespace Reader {

void NextChoice::Begin(u64 frame, u64 choices, u64 texts) {
    running = true;
    started = frame;
    choices_at_start = choices;
    texts_at_start = texts;
    last_texts = texts;
    last_text_frame = frame;
    passed = 0;
}

NextChoice::Step NextChoice::OnFrame(u64 frame, u64 choices, u64 texts, bool user_input) {
    Step step;
    if (!running) {
        return step;
    }
    passed = texts - texts_at_start;
    if (texts != last_texts) {
        last_texts = texts;
        last_text_frame = frame;
    }
    if (choices != choices_at_start) {
        step.end = End::Choice;
    } else if (user_input) {
        step.end = End::UserInput;
    } else if (frame - last_text_frame >= NoTextFrames) {
        step.end = End::NoText;
    }
    if (step.end) {
        running = false;
        return step;
    }
    step.press = (frame - started) % (HeldFrames + ReleasedFrames) < HeldFrames;
    return step;
}

void NextChoice::Cancel() {
    running = false;
}

} // namespace Reader
