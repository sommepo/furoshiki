/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "reader/vita/scene_draw.h"
#include "reader/vita/scene_layout.h"

#include <math.h>
#include <string.h>
#include <vita2d.h>

/* The maintainer's look: indigo glass, a lavender double border, white text with a soft glow,
 * the word marked in magenta. */
#define POPUP_FILL RGBA8(0x24, 0x1c, 0x5c, 0xd8)
#define PLAIN_FILL RGBA8(0x24, 0x1c, 0x5c, 0xff)
#define SESSION_TEXT RGBA8(0xe8, 0xe4, 0xf8, 0x90)
#define SESSION_SHADOW RGBA8(0x00, 0x00, 0x00, 0x70)
#define POPUP_BORDER_OUTER RGBA8(0xc8, 0xb8, 0xf0, 0xff)
#define POPUP_BORDER_INNER RGBA8(0x8a, 0x78, 0xd0, 0xff)
#define TEXT_WHITE RGBA8(0xff, 0xff, 0xff, 0xff)
#define TEXT_GLOW RGBA8(0xb0, 0xa0, 0xff, 0x60)
#define TEXT_DIM RGBA8(0xd0, 0xc8, 0xf0, 0xff)
#define TEXT_REASONS RGBA8(0xff, 0xc8, 0xf0, 0xff)
#define TICK RGBA8(0x8c, 0xf0, 0xa8, 0xff)
#define MARK RGBA8(0xff, 0x3c, 0xb4, 0xff)
#define MARK_FILL RGBA8(0xff, 0x3c, 0xb4, 0x30)

#define POPUP_WIDTH READER_POPUP_WIDTH
#define LINE_STEP(scale) (22 * (scale) + 4)
#define SCROLL_BAR RGBA8(0xc8, 0xb8, 0xf0, 0xc0)
#define SCROLL_TRACK RGBA8(0xc8, 0xb8, 0xf0, 0x30)
#define PAD 12.0f
#define DEFINITION_INDENT 10.0f
#define MAX_LINES 40
#define LINE_BYTES 256

typedef struct {
    char text[LINE_BYTES];
    unsigned color;
    float scale;
    float indent;
} Line;

static float minf(float a, float b) {
    return a < b ? a : b;
}

static float maxf(float a, float b) {
    return a > b ? a : b;
}

/* The length in bytes of the UTF-8 character that begins with `c`. */
static int charLength(unsigned char c) {
    return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
}

/* Adds `text` to `lines` as lines no wider than `width` by the font's measure. */
static int wrap(struct vita2d_pgf *font, float scale, const char *text, float width, unsigned color, float indent,
                Line *lines, int count) {
    Line *line = NULL;
    int used = 0;
    for (const char *p = text; *p != '\0' && count <= MAX_LINES;) {
        int n = charLength((unsigned char)*p);
        if (line == NULL) {
            if (count == MAX_LINES) {
                break;
            }
            line = &lines[count++];
            line->color = color;
            line->scale = scale;
            line->indent = indent;
            used = 0;
            if (*p == ' ') { /* a wrapped line does not begin with a space */
                p++;
                continue;
            }
        }
        if (used + n >= LINE_BYTES) {
            line = NULL;
            continue;
        }
        memcpy(line->text + used, p, n);
        line->text[used + n] = '\0';
        if (used > 0 && vita2d_pgf_text_width(font, scale, line->text) > width) {
            line->text[used] = '\0';
            line = NULL; /* this character begins the next line */
            continue;
        }
        used += n;
        p += n;
    }
    return count;
}

static void glowText(struct vita2d_pgf *font, float x, float y, unsigned color, float scale, const char *text) {
    vita2d_pgf_draw_text(font, (int)(x + 1), (int)(y + 1), TEXT_GLOW, scale, text);
    vita2d_pgf_draw_text(font, (int)x, (int)y, color, scale, text);
}

/* The popup's glass and lavender double border. */
static void drawBox(float x, float y, float width, float height, unsigned fill) {
    vita2d_draw_rectangle(x, y, width, height, fill);
    for (int b = 0; b < 2; b++) {
        const float inset = b == 0 ? 0.0f : 3.0f;
        const unsigned color = b == 0 ? POPUP_BORDER_OUTER : POPUP_BORDER_INNER;
        const float x0 = x + inset, y0 = y + inset, x1 = x + width - inset, y1 = y + height - inset;
        vita2d_draw_line(x0, y0, x1, y0, color);
        vita2d_draw_line(x0, y1, x1, y1, color);
        vita2d_draw_line(x0, y0, x0, y1, color);
        vita2d_draw_line(x1, y0, x1, y1, color);
    }
}

/* The plain view: the box, and each character in its cell of the grid. */
static void drawPlain(const ReaderScene *scene, struct vita2d_pgf *font) {
    const float scale = scene->plain_cell * 0.9f / 22.0f;
    const ReaderSceneRect *box = &scene->plain_box;
    drawBox(box->x, box->y, box->width, box->height, PLAIN_FILL);
    int index = 0;
    for (const char *p = scene->plain_text; *p != '\0'; index++) {
        char glyph[5] = {0};
        const int n = charLength((unsigned char)*p);
        memcpy(glyph, p, n);
        p += n;
        const float x = scene->plain_left + (index % scene->plain_columns) * scene->plain_cell;
        const float y = scene->plain_top + (index / scene->plain_columns) * scene->plain_row;
        const float width = (float)vita2d_pgf_text_width(font, scale, glyph);
        glowText(font, x + (scene->plain_cell - width) / 2, y + scene->plain_cell * 0.95f, TEXT_WHITE, scale, glyph);
    }
}

static void drawStatus(const ReaderScene *scene, struct vita2d_pgf *font) {
    const float width = (float)vita2d_pgf_text_width(font, 0.85f, scene->status);
    drawBox(12, 10, width + 2 * PAD, 34, PLAIN_FILL);
    glowText(font, 12 + PAD, 34, TEXT_WHITE, 0.85f, scene->status);
}

/* Small and quiet in the corner, as Furoshiki's: over the game, so with a shadow of its own. */
static void drawSession(const ReaderScene *scene, struct vita2d_pgf *font) {
    const float width = (float)vita2d_pgf_text_width(font, 0.7f, scene->session);
    const float x = 960 - 12 - width;
    vita2d_pgf_draw_text(font, (int)x + 1, 25, SESSION_SHADOW, 0.7f, scene->session);
    vita2d_pgf_draw_text(font, (int)x, 24, SESSION_TEXT, 0.7f, scene->session);
}

static void drawMarks(const ReaderScene *scene) {
    for (int i = 0; i < scene->mark_count; i++) {
        const ReaderSceneRect *r = &scene->marks[i];
        vita2d_draw_rectangle(r->x - 1, r->y - 1, r->width + 2, r->height + 2, MARK_FILL);
        vita2d_draw_line(r->x - 1, r->y + r->height + 1, r->x + r->width + 1, r->y + r->height + 1, MARK);
        vita2d_draw_line(r->x - 1, r->y + r->height + 2, r->x + r->width + 1, r->y + r->height + 2, MARK);
    }
}

/* A tick of small squares: the software drawing has no slanted lines. */
static void drawTick(float x, float y) {
    for (int i = 0; i < 7; i++) {
        vita2d_draw_rectangle(x + i, y + 9 + i, 3, 3, TICK);
    }
    for (int i = 0; i < 16; i++) {
        vita2d_draw_rectangle(x + 7 + i, y + 15 - i, 3, 3, TICK);
    }
}

static int drawPopup(const ReaderScene *scene, struct vita2d_pgf *font) {
    static Line lines[MAX_LINES];
    int count = 0;
    const float text_width = POPUP_WIDTH - 2 * PAD - 8; /* room for the scroll bar */
    for (int i = 0; i < scene->entry_count && count < MAX_LINES; i++) {
        const ReaderSceneEntry *entry = &scene->entries[i];
        const char *text = scene->text + entry->offset;
        if (entry->kind == READER_ENTRY_HEAD) {
            count = wrap(font, 1.15f, text, text_width, TEXT_WHITE, 0, lines, count);
        } else if (entry->kind == READER_ENTRY_REASONS) {
            count = wrap(font, 0.8f, text, text_width, TEXT_REASONS, 0, lines, count);
        } else {
            count = wrap(font, 0.85f, text, text_width - DEFINITION_INDENT, TEXT_DIM, DEFINITION_INDENT, lines, count);
        }
    }
    float px, py, height;
    readerScenePlacePopup(scene, &px, &py, &height);

    /* How many lines fit from the last one back: so far can it scroll. */
    const float room = height - 2 * PAD;
    int fit_from_end = 0;
    for (float used = 0; fit_from_end < count && used + LINE_STEP(lines[count - 1 - fit_from_end].scale) <= room;) {
        used += LINE_STEP(lines[count - 1 - fit_from_end].scale);
        fit_from_end++;
    }
    const int limit = count - fit_from_end;
    const int first = scene->scroll < 0 ? 0 : scene->scroll > limit ? limit : scene->scroll;

    drawBox(px, py, POPUP_WIDTH, height, POPUP_FILL);
    if (scene->in_deck) {
        drawTick(px + POPUP_WIDTH - 40, py + 10);
    }
    float y = py + PAD;
    for (int i = first; i < count; i++) {
        if (y + LINE_STEP(lines[i].scale) > py + height - PAD + 4) {
            break;
        }
        y += 22 * lines[i].scale;
        glowText(font, px + PAD + lines[i].indent, y, lines[i].color, lines[i].scale, lines[i].text);
        y += 4;
    }
    if (limit > 0) { /* a thin bar at the right: where the lines shown are among all of them */
        const float track_x = px + POPUP_WIDTH - 9, track_y = py + 8, track_h = height - 16;
        const float bar_h = maxf(12.0f, track_h * (float)(count - limit) / (float)count);
        const float bar_y = track_y + (track_h - bar_h) * (float)first / (float)limit;
        vita2d_draw_rectangle(track_x, track_y, 3, track_h, SCROLL_TRACK);
        vita2d_draw_rectangle(track_x, bar_y, 3, bar_h, SCROLL_BAR);
    }
    return limit;
}

/* A filled circle as rows of rectangles: vita2d's own circle draws nothing in Adrenaline's
 * vita2d (its triangle fan), while rectangles do everywhere. */
static void fillCircle(float x, float y, float radius, unsigned color) {
    for (float dy = -radius + 0.5f; dy < radius; dy += 1.0f) {
        const float half = sqrtf(radius * radius - dy * dy);
        vita2d_draw_rectangle(x - half, y + dy - 0.5f, 2 * half, 1.0f, color);
    }
}

static void drawPointer(const ReaderScene *scene) {
    const float a = scene->pointer_alpha;
    const float x = scene->pointer_x, y = scene->pointer_y;
    fillCircle(x, y, 8, RGBA8(0x24, 0x1c, 0x5c, (unsigned)(0xa0 * a)));
    fillCircle(x, y, 5, RGBA8(0xc8, 0xb8, 0xf0, (unsigned)(0xff * a)));
    fillCircle(x, y, 3, RGBA8(0xff, 0xff, 0xff, (unsigned)(0xff * a)));
}

int readerSceneDraw(const ReaderScene *scene, struct vita2d_pgf *font) {
    int limit = 0;
    if (scene == NULL || scene->struct_size < sizeof(ReaderScene)) {
        return 0;
    }
    if (scene->session[0] != '\0' && font != NULL) {
        drawSession(scene, font);
    }
    if (scene->plain_shown && font != NULL) {
        drawPlain(scene, font);
    }
    drawMarks(scene);
    if (scene->popup_shown && font != NULL) {
        limit = drawPopup(scene, font);
    }
    if (scene->pointer_shown) {
        drawPointer(scene);
    }
    if (scene->status[0] != '\0' && font != NULL) {
        drawStatus(scene, font);
    }
    return limit;
}
