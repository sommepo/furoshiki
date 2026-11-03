/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Draws a reading layer's scene (scene.h) with vita2d, between vita2d_start_drawing and
 * vita2d_end_drawing, over the game's picture: the word's mark, the popup beside it, the
 * pointer. Plain C (no C++ library), for PS1 Reader and for Adrenaline's own draw thread.
 */
#ifndef READER_VITA_SCENE_DRAW_H
#define READER_VITA_SCENE_DRAW_H

#include "reader/vita/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vita2d_pgf;

/* `font`: a Japanese PGF font (vita2d_load_default_pgf, or the system's jpn0.pgf). Returns how
 * many lines the popup can scroll (its lines beyond what it shows), for LookupLayer's
 * SetScrollLimit; 0 without a popup. */
int readerSceneDraw(const ReaderScene *scene, struct vita2d_pgf *font);

#ifdef __cplusplus
}
#endif

#endif /* READER_VITA_SCENE_DRAW_H */
