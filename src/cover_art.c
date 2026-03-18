/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "../lib/stb_image.h"

#include "cover_art.h"
#include "util.h"
#include <stdlib.h>
#include <math.h>

int rgb_to_256(int r, int g, int b)
{
    /* Check if close to grayscale */
    if (abs(r - g) < 10 && abs(g - b) < 10) {
        int gray = (r + g + b) / 3;
        if (gray < 8) return 16;
        if (gray > 248) return 231;
        return (int)round((gray - 8) / 247.0 * 23) + 232;
    }
    /* Map to 6x6x6 color cube (indices 16-231) */
    int ri = (int)round(r / 255.0 * 5);
    int gi = (int)round(g / 255.0 * 5);
    int bi = (int)round(b / 255.0 * 5);
    return 16 + 36 * ri + 6 * gi + bi;
}

RenderedCover *cover_art_render(const unsigned char *data, size_t data_size,
                                int target_width, int target_height,
                                int *next_pair)
{
    int img_w, img_h, channels;
    unsigned char *pixels = stbi_load_from_memory(data, (int)data_size,
                                                   &img_w, &img_h, &channels, 3);
    if (!pixels) return NULL;

    /* Resize using nearest-neighbor (stb_image doesn't have resize built-in) */
    int pixel_h = target_height * 2;  /* Each terminal row = 2 pixel rows */
    unsigned char *resized = malloc(target_width * pixel_h * 3);
    if (!resized) {
        stbi_image_free(pixels);
        return NULL;
    }

    for (int y = 0; y < pixel_h; y++) {
        int src_y = y * img_h / pixel_h;
        if (src_y >= img_h) src_y = img_h - 1;
        for (int x = 0; x < target_width; x++) {
            int src_x = x * img_w / target_width;
            if (src_x >= img_w) src_x = img_w - 1;
            int src_idx = (src_y * img_w + src_x) * 3;
            int dst_idx = (y * target_width + x) * 3;
            resized[dst_idx] = pixels[src_idx];
            resized[dst_idx + 1] = pixels[src_idx + 1];
            resized[dst_idx + 2] = pixels[src_idx + 2];
        }
    }
    stbi_image_free(pixels);

    int max_pairs = COLOR_PAIRS - 10;
    if (max_pairs < 0) max_pairs = 246;

    RenderedCover *art = calloc(1, sizeof(RenderedCover));
    art->width = target_width;
    art->height = target_height;
    art->cells = calloc(target_width * target_height, sizeof(CoverCell));

    /* Simple hash map for color pair dedup */
    typedef struct { int fg; int bg; int pair; } PairEntry;
    int pair_cap = 1024;
    PairEntry *pair_map = calloc(pair_cap, sizeof(PairEntry));
    int pair_count = 0;
    bool *pair_used = calloc(pair_cap, sizeof(bool));

    for (int row = 0; row < target_height; row++) {
        int top_y = row * 2;
        int bot_y = top_y + 1;
        for (int col = 0; col < target_width; col++) {
            int ti = (top_y * target_width + col) * 3;
            int bi = (bot_y * target_width + col) * 3;
            int fg = rgb_to_256(resized[ti], resized[ti + 1], resized[ti + 2]);
            int bg = rgb_to_256(resized[bi], resized[bi + 1], resized[bi + 2]);

            /* Find or create color pair */
            int pair_id = 0;
            bool found = false;
            for (int p = 0; p < pair_count; p++) {
                if (pair_map[p].fg == fg && pair_map[p].bg == bg) {
                    pair_id = pair_map[p].pair;
                    found = true;
                    break;
                }
            }
            if (!found && *next_pair < max_pairs + 10) {
                init_pair(*next_pair, fg, bg);
                pair_id = *next_pair;
                if (pair_count < pair_cap) {
                    pair_map[pair_count].fg = fg;
                    pair_map[pair_count].bg = bg;
                    pair_map[pair_count].pair = pair_id;
                    pair_count++;
                }
                (*next_pair)++;
            }

            art->cells[row * target_width + col].pair_id = pair_id;
        }
    }

    free(pair_map);
    free(pair_used);
    free(resized);
    return art;
}

void cover_art_draw(WINDOW *win, RenderedCover *art, int start_row, int start_col)
{
    if (!art) return;
    for (int row = 0; row < art->height; row++) {
        for (int col = 0; col < art->width; col++) {
            int pair = art->cells[row * art->width + col].pair_id;
            wattron(win, COLOR_PAIR(pair));
            mvwaddstr(win, start_row + row, start_col + col, "\xe2\x96\x80"); /* U+2580 ▀ */
            wattroff(win, COLOR_PAIR(pair));
        }
    }
}

void cover_art_free(RenderedCover *art)
{
    if (!art) return;
    free(art->cells);
    free(art);
}
