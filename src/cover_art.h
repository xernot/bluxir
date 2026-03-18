/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef COVER_ART_H
#define COVER_ART_H

#include <ncurses.h>
#include <stdbool.h>
#include <stddef.h>

/* A single cell in the rendered cover art grid */
typedef struct {
    int pair_id;   /* curses color pair number */
} CoverCell;

/* Rendered cover art ready for display */
typedef struct {
    CoverCell *cells;  /* row-major array: cells[row * width + col] */
    int width;
    int height;
} RenderedCover;

/* Render cover art from raw image bytes using half-block characters.
   Returns a RenderedCover that must be freed with cover_art_free().
   next_pair is the starting color pair index (>= 10), updated on return. */
RenderedCover *cover_art_render(const unsigned char *data, size_t data_size,
                                int target_width, int target_height,
                                int *next_pair);

/* Draw rendered cover art onto the curses window */
void cover_art_draw(WINDOW *win, RenderedCover *art, int start_row, int start_col);

/* Free rendered cover art */
void cover_art_free(RenderedCover *art);

/* Map RGB to nearest xterm-256 color index */
int rgb_to_256(int r, int g, int b);

#endif /* COVER_ART_H */
