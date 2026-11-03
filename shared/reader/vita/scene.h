/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * What the reading layer of a Vita app shows over the game, as plain data: the rear pad's
 * pointer, the looked-up word's mark, the popup's entries and the plain view. LookupLayer (lookup_layer.h)
 * makes it; scene_draw.c draws it with vita2d. Plain C, so that a program built without C++'s
 * library (Adrenaline's adrenaline_user) can take one from a module that has it and draw it.
 */
#ifndef READER_VITA_SCENE_H
#define READER_VITA_SCENE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define READER_SCENE_MARKS 24
#define READER_SCENE_ENTRIES 32
#define READER_SCENE_TEXT 6144
/* The whole line (LookupLayer's plain view): its UTF-8, at most 608 characters of the grid. */
#define READER_SCENE_PLAIN 2900
#define READER_SCENE_STATUS 120

/* What an entry of the popup is: it is drawn in its own size and colour. */
enum {
    READER_ENTRY_HEAD = 1,       /* a word and its reading: 大丈夫  【だいじょうぶ】 */
    READER_ENTRY_REASONS = 2,    /* the inflections undone to reach it: « past « negative */
    READER_ENTRY_DEFINITION = 3, /* one numbered definition, wrapped by the drawing */
};

/* In the Vita screen's pixels (960 by 544). */
typedef struct {
    float x, y, width, height;
} ReaderSceneRect;

typedef struct {
    uint16_t kind;   /* READER_ENTRY_* */
    uint16_t offset; /* its UTF-8 text in ReaderScene.text, ended by a NUL */
} ReaderSceneEntry;

typedef struct {
    uint32_t struct_size;
    /* The rear pad's pointer, while it shows (fading out over its last half second). */
    int pointer_shown;
    float pointer_x, pointer_y, pointer_alpha;
    /* The looked-up word: a box round each of its characters' cells. */
    int mark_count;
    ReaderSceneRect marks[READER_SCENE_MARKS];
    /* The popup: a third of the screen high, above or below `line` (all the text of the
     * word's window, which it does not cover where there is room) and beside `word` (the
     * marks' outline); its entries in order, shown from line `scroll` of them on (the drawing
     * says how far that can go: readerSceneDraw). */
    int popup_shown;
    ReaderSceneRect word;
    ReaderSceneRect line;
    int scroll;
    int in_deck; /* a deck has the word already: a tick in the popup's upper right corner */
    int entry_count;
    ReaderSceneEntry entries[READER_SCENE_ENTRIES];
    char text[READER_SCENE_TEXT];
    /* The plain view: the current line on a filled box over the game, drawn under the marks, the
     * popup and the pointer. Its characters go along a grid, `plain_columns`
     * to a row, one in each cell of plain_cell by plain_row from (plain_left, plain_top): where a
     * tap finds them. */
    int plain_shown;
    int plain_columns;
    float plain_left, plain_top, plain_cell, plain_row;
    ReaderSceneRect plain_box;
    char plain_text[READER_SCENE_PLAIN];
    /* A short word from the app (a card sent), at the top of the screen, or empty. */
    char status[READER_SCENE_STATUS];
    /* The reading so far, small and quiet in the upper right corner, or empty. */
    char session[READER_SCENE_STATUS];
} ReaderScene;

#ifdef __cplusplus
}
#endif

#endif /* READER_VITA_SCENE_H */
