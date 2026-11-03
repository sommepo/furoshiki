/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "reader/vita/scene_layout.h"

static float minf(float a, float b) {
    return a < b ? a : b;
}

static float maxf(float a, float b) {
    return a > b ? a : b;
}

void readerScenePlacePopup(const ReaderScene *scene, float *x, float *y, float *height) {
    const ReaderSceneRect *line = &scene->line;
    const ReaderSceneRect *word = &scene->word;
    const float margin = READER_POPUP_MARGIN;
    const float below = 544 - margin - (line->y + line->height + margin);
    const float above = line->y - margin - margin;
    *x = maxf(margin, minf(word->x - 20, 960 - READER_POPUP_WIDTH - margin));
    *height = READER_POPUP_HEIGHT;
    if (below >= READER_POPUP_HEIGHT) {
        *y = line->y + line->height + margin;
    } else if (above >= READER_POPUP_HEIGHT) {
        *y = line->y - margin - READER_POPUP_HEIGHT;
    } else {
        const float word_below = 544 - margin - (word->y + word->height + margin);
        const float word_above = word->y - margin - margin;
        if (word_below >= word_above) {
            *height = minf(READER_POPUP_HEIGHT, word_below);
            *y = word->y + word->height + margin;
        } else {
            *height = minf(READER_POPUP_HEIGHT, word_above);
            *y = word->y - margin - *height;
        }
    }
}
