/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Where a scene's popup goes on the Vita's screen (960 by 544): the same for every way of
 * drawing it (scene_draw.c with vita2d, Dreamcast Reader's with ImGui). Plain C.
 */
#ifndef READER_VITA_SCENE_LAYOUT_H
#define READER_VITA_SCENE_LAYOUT_H

#include "reader/vita/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define READER_POPUP_WIDTH 520.0f
/* A third of the screen's height: the popup is always this high, its text scrolls. */
#define READER_POPUP_HEIGHT (544.0f / 3.0f)
#define READER_POPUP_MARGIN 8.0f

/* The whole height where there is room above or below all the text (`scene->line`), else
 * beside the word's own row where there is more room, as high as that allows. */
void readerScenePlacePopup(const ReaderScene *scene, float *x, float *y, float *height);

#ifdef __cplusplus
}
#endif

#endif /* READER_VITA_SCENE_LAYOUT_H */
