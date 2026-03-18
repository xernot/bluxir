/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "config.h"
#include "constants.h"
#include "cover_art.h"
#include "ui.h"
#include "util.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static void draw_track_info(WINDOW *win, AppState *app, int left_max,
                            int start_row, int bottom_line);
static void draw_detail_section(WINDOW *win, AppState *app, int left_max,
                                int divider_x, int bottom_line);

/* ── Left Text Helper ───────────────────────────────────────────────────── */

static void left_text(WINDOW *win, int left_max, int row, int col,
                      const char *text, int attr) {
  int max_len = left_max - col;
  if (max_len <= 0)
    return;
  if (attr)
    wattron(win, attr);
  mvwaddnstr(win, row, col, text, max_len);
  if (attr)
    wattroff(win, attr);
}

/* ── Player Info (top-left) ─────────────────────────────────────────────── */

static void draw_player_info(WINDOW *win, AppState *app, int divider_x) {
  PlayerStatus *ps = &app->player_status;
  int left_max = divider_x - 1;
  int label_w = 13; /* "Now Playing: " */

  char line[STR_LONG];
  snprintf(line, sizeof(line), "%-*s %s - %s", label_w,
           "Now Playing:", ps->name, ps->artist);
  left_text(win, left_max, 3, 2, line, 0);

  snprintf(line, sizeof(line), "%-*s %s", label_w, "Album:", ps->album);
  left_text(win, left_max, 4, 2, line, 0);

  snprintf(line, sizeof(line), "%-*s %s", label_w, "Service:", ps->service);
  left_text(win, left_max, 5, 2, line, 0);

  char progress[64];
  if (ps->totlen > 0) {
    char cur[16], tot[16];
    format_time(ps->secs, cur, sizeof(cur));
    format_time(ps->totlen, tot, sizeof(tot));
    snprintf(progress, sizeof(progress), "%s / %s", cur, tot);
  } else {
    format_time(ps->secs, progress, sizeof(progress));
  }
  snprintf(line, sizeof(line), "%-*s %s", label_w, "Progress:", progress);
  left_text(win, left_max, 6, 2, line, 0);

  /* Separator */
  mvwhline(win, 7, 0, ACS_HLINE, divider_x);
  mvwaddch(win, 7, divider_x, ACS_RTEE);
}

/* ── Cover Art ──────────────────────────────────────────────────────────── */

static void draw_cover_art(WINDOW *win, AppState *app, int left_max,
                           int bottom_line) {
  int art_start = 8;
  int avail_rows = bottom_line - art_start;
  int avail_cols = left_max - 4;

  if (app->cover_art_loading) {
    left_text(win, left_max, art_start, 2, "Loading cover art...", A_DIM);
  } else if (app->cover_art_valid && app->cover_art_raw) {
    int next_pair = 10;
    RenderedCover *art =
        cover_art_render(app->cover_art_raw, app->cover_art_raw_size,
                         avail_cols, avail_rows, &next_pair);
    if (art) {
      cover_art_draw(win, art, art_start, 2);
      cover_art_free(art);
    } else {
      left_text(win, left_max, art_start, 2, "Error rendering cover art.",
                A_DIM);
    }
  } else {
    left_text(win, left_max, art_start, 2, "No cover art available.", A_DIM);
  }
}

/* ── Detail Section ─────────────────────────────────────────────────────── */

typedef struct {
  const char *label;
  char value[STR_MEDIUM];
} DetailEntry;

static int build_left_details(PlayerStatus *ps, DetailEntry *left) {
  int bit_depth = 0;
  double sample_rate = 0;
  bool has_quality = parse_quality(ps->stream_format, &bit_depth, &sample_rate);
  char quality[32];
  derive_quality(ps->stream_format, quality, sizeof(quality));

  left[0].label = "Format";
  safe_strcpy(left[0].value, ps->stream_format[0] ? ps->stream_format : "-",
              sizeof(left[0].value));
  left[1].label = "Quality";
  safe_strcpy(left[1].value, quality, sizeof(left[1].value));
  left[2].label = "Sample Rate";
  if (has_quality)
    snprintf(left[2].value, sizeof(left[2].value), "%.1f kHz", sample_rate);
  else
    safe_strcpy(left[2].value, "-", sizeof(left[2].value));
  left[3].label = "Bit Depth";
  if (has_quality)
    snprintf(left[3].value, sizeof(left[3].value), "%d bit", bit_depth);
  else
    safe_strcpy(left[3].value, "-", sizeof(left[3].value));
  left[4].label = "dB Level";
  snprintf(left[4].value, sizeof(left[4].value), "%.1f", ps->db);
  return 5;
}

static int build_right_details(PlayerStatus *ps, AppState *app,
                               DetailEntry *right) {
  right[0].label = "Track-Nr";
  snprintf(right[0].value, sizeof(right[0].value), "%d", ps->song + 1);
  right[1].label = "Composer";
  safe_strcpy(right[1].value, ps->composer[0] ? ps->composer : "-",
              sizeof(right[1].value));
  right[2].label = "Year";
  safe_strcpy(right[2].value, app->mb_has_info ? app->mb_info.year : "-",
              sizeof(right[2].value));
  right[3].label = "Label";
  safe_strcpy(right[3].value, app->mb_has_info ? app->mb_info.label : "-",
              sizeof(right[3].value));
  right[4].label = "Genre";
  safe_strcpy(right[4].value, app->mb_has_info ? app->mb_info.genre : "-",
              sizeof(right[4].value));
  return 5;
}

static void draw_detail_columns(WINDOW *win, DetailEntry *left, int left_count,
                                DetailEntry *right, int right_count,
                                int left_max, int sub_col2_x, int bottom_line) {
  int dl = 11; /* "Sample Rate" */
  int dr = 8;  /* "Track-Nr" */
  int max_rows = int_max(left_count, right_count);

  for (int i = 0; i < max_rows && (8 + i) < bottom_line; i++) {
    int row = 8 + i;
    if (i < left_count) {
      char label[32];
      snprintf(label, sizeof(label), "%-*s", dl + 1, left[i].label);
      wattron(win, A_DIM);
      mvwaddnstr(win, row, 2, label, sub_col2_x - 4);
      wattroff(win, A_DIM);
      mvwaddnstr(win, row, 2 + dl + 2, left[i].value, sub_col2_x - dl - 4);
    }
    if (i < right_count) {
      char label[32];
      snprintf(label, sizeof(label), "%-*s", dr + 1, right[i].label);
      int val_start = sub_col2_x + dr + 2;
      int val_max = int_max(0, left_max - val_start);
      wattron(win, A_DIM);
      mvwaddnstr(win, row, sub_col2_x, label, left_max - sub_col2_x);
      wattroff(win, A_DIM);
      if (val_max > 0)
        mvwaddnstr(win, row, val_start, right[i].value, val_max);
    }
  }
}

static void draw_detail_section(WINDOW *win, AppState *app, int left_max,
                                int divider_x, int bottom_line) {
  PlayerStatus *ps = &app->player_status;
  int sub_col2_x = 2 + (left_max / 2);

  DetailEntry left[5] = {0};
  int left_count = build_left_details(ps, left);
  DetailEntry right[5] = {0};
  int right_count = build_right_details(ps, app, right);

  draw_detail_columns(win, left, left_count, right, right_count, left_max,
                      sub_col2_x, bottom_line);

  int max_rows = int_max(left_count, right_count);
  int detail_bottom = 8 + max_rows;
  if (detail_bottom < bottom_line) {
    mvwhline(win, detail_bottom, 0, ACS_HLINE, divider_x);
    mvwaddch(win, detail_bottom, divider_x, ACS_RTEE);
  }

  draw_track_info(win, app, left_max, detail_bottom + 1, bottom_line);
}

/* ── Word Wrap Helper ──────────────────────────────────────────────────── */

static int draw_word_wrapped(WINDOW *win, const char *text, int col, int wrap_w,
                             int row, int bottom_line) {
  char buf[STR_TEXT];
  safe_strcpy(buf, text, sizeof(buf));
  char *word = strtok(buf, " ");
  char line[STR_LONG] = "";
  while (word && row < bottom_line) {
    if (line[0] && (int)(strlen(line) + 1 + strlen(word)) > wrap_w) {
      mvwaddnstr(win, row++, col, line, wrap_w);
      safe_strcpy(line, word, sizeof(line));
    } else {
      if (line[0]) {
        strncat(line, " ", sizeof(line) - strlen(line) - 1);
        strncat(line, word, sizeof(line) - strlen(line) - 1);
      } else {
        safe_strcpy(line, word, sizeof(line));
      }
    }
    word = strtok(NULL, " ");
  }
  if (line[0] && row < bottom_line)
    mvwaddnstr(win, row++, col, line, wrap_w);
  return row;
}

/* ── Track Info ─────────────────────────────────────────────────────────── */

static void draw_track_info_attribution(WINDOW *win, int left_max,
                                        int bottom_line) {
  int wrap_w = left_max - 4;
  char *model = config_get("openai_model");
  const char *model_name = (model && model[0]) ? model : DEFAULT_OPENAI_MODEL;
  char attr_text[STR_MEDIUM];
  snprintf(attr_text, sizeof(attr_text), "(generated by %s)", model_name);
  wattron(win, A_DIM);
  mvwaddnstr(win, bottom_line - 1, 2, attr_text, wrap_w);
  wattroff(win, A_DIM);
  free(model);
}

static void draw_track_info(WINDOW *win, AppState *app, int left_max,
                            int start_row, int bottom_line) {
  if (start_row >= bottom_line)
    return;
  if (app->wiki_loading) {
    if (start_row + 1 < bottom_line)
      left_text(win, left_max, start_row + 1, 2, "Loading track info...",
                A_DIM);
    return;
  }

  pthread_mutex_lock(&app->data_lock);
  char *wiki = app->wiki_text ? strdup(app->wiki_text) : NULL;
  pthread_mutex_unlock(&app->data_lock);
  if (!wiki)
    return;

  int row = start_row + 1; /* blank line */
  if (row < bottom_line) {
    wattron(win, A_BOLD);
    mvwaddstr(win, row, 2, "Track Info:");
    wattroff(win, A_BOLD);
    row++;
  }

  draw_word_wrapped(win, wiki, 2, left_max - 4, row, bottom_line);
  draw_track_info_attribution(win, left_max, bottom_line);
  free(wiki);
}

/* ── Right Panel ────────────────────────────────────────────────────────── */

static void draw_radio_panel(WINDOW *win, AppState *app, int right_start,
                             int right_w, int bottom_line) {
  PlayerStatus *ps = &app->player_status;
  wattron(win, A_BOLD);
  mvwaddnstr(win, 3, right_start, "Radio:", right_w);
  wattroff(win, A_BOLD);

  int row = 5;
  const char *t3 = ps->title3[0] ? ps->title3 : app->radio_title3;
  if (t3[0] && row < bottom_line) {
    wattron(win, A_DIM);
    mvwaddnstr(win, row, right_start, "Now playing:", right_w);
    wattroff(win, A_DIM);
    row++;
    row = draw_word_wrapped(win, t3, right_start, right_w, row, bottom_line);
  }

  const char *t2 = ps->title2[0] ? ps->title2 : app->radio_title2;
  if (t2[0] && row < bottom_line) {
    row++;
    if (row < bottom_line) {
      wattron(win, A_DIM);
      mvwaddnstr(win, row, right_start, "Next:", right_w);
      wattroff(win, A_DIM);
      row++;
      draw_word_wrapped(win, t2, right_start, right_w, row, bottom_line);
    }
  }
}

static bool lyrics_wrap_append(char ***wrapped, int *wrap_count, int *wrap_cap,
                               const char *line) {
  if (*wrap_count >= *wrap_cap) {
    *wrap_cap = *wrap_cap ? *wrap_cap * 2 : 64;
    char **tmp = realloc(*wrapped, *wrap_cap * sizeof(char *));
    if (!tmp)
      return false;
    *wrapped = tmp;
  }
  (*wrapped)[(*wrap_count)++] = strdup(line);
  return true;
}

static void lyrics_free_wrapped(char **wrapped, int wrap_count) {
  for (int i = 0; i < wrap_count; i++)
    free(wrapped[i]);
  free(wrapped);
}

static bool lyrics_wrap_segment(char *seg, int right_w, char ***wrapped,
                                int *wrap_count, int *wrap_cap) {
  while (seg[0]) {
    if ((int)strlen(seg) <= right_w)
      return lyrics_wrap_append(wrapped, wrap_count, wrap_cap, seg);
    int split = right_w;
    for (int j = right_w - 1; j > 0; j--) {
      if (seg[j] == ' ') {
        split = j;
        break;
      }
    }
    char saved = seg[split];
    seg[split] = '\0';
    if (!lyrics_wrap_append(wrapped, wrap_count, wrap_cap, seg))
      return false;
    seg[split] = saved;
    seg += split;
    while (*seg == ' ')
      seg++;
  }
  return true;
}

static bool lyrics_wrap_text(char *lyrics, int right_w, char ***out_wrapped,
                             int *out_count) {
  char **wrapped = NULL;
  int wrap_count = 0, wrap_cap = 0;
  char *line_start = lyrics;

  for (char *p = lyrics;; p++) {
    if (*p != '\n' && *p != '\0')
      continue;
    char saved = *p;
    *p = '\0';
    if (!line_start[0]) {
      if (!lyrics_wrap_append(&wrapped, &wrap_count, &wrap_cap, "")) {
        lyrics_free_wrapped(wrapped, wrap_count);
        return false;
      }
    } else if (!lyrics_wrap_segment(line_start, right_w, &wrapped, &wrap_count,
                                    &wrap_cap)) {
      lyrics_free_wrapped(wrapped, wrap_count);
      return false;
    }
    if (saved == '\0')
      break;
    line_start = p + 1;
  }

  *out_wrapped = wrapped;
  *out_count = wrap_count;
  return true;
}

static void lyrics_render_visible(WINDOW *win, char **wrapped, int wrap_count,
                                  AppState *app, int right_start, int right_w,
                                  int bottom_line) {
  int max_visible = bottom_line - 5;
  int max_scroll = int_max(0, wrap_count - max_visible);
  if (app->lyrics_scroll > max_scroll)
    app->lyrics_scroll = max_scroll;

  int start = app->lyrics_scroll;
  for (int i = 0; i < max_visible && (start + i) < wrap_count; i++) {
    int row = 4 + i;
    if (row >= bottom_line - 1)
      break;
    if (wrapped[start + i][0])
      mvwaddnstr(win, row, right_start, wrapped[start + i], right_w);
  }

  /* Attribution */
  char attr_line[STR_MEDIUM];
  safe_strcpy(attr_line, LYRICS_ATTRIBUTION, sizeof(attr_line));
  if (max_scroll > 0)
    strncat(attr_line, LYRICS_SCROLL_HINT,
            sizeof(attr_line) - strlen(attr_line) - 1);
  wattron(win, A_DIM);
  mvwaddnstr(win, bottom_line - 1, right_start, attr_line, right_w);
  wattroff(win, A_DIM);
}

static void draw_lyrics_panel(WINDOW *win, AppState *app, int right_start,
                              int right_w, int bottom_line) {
  wattron(win, A_BOLD);
  mvwaddnstr(win, 3, right_start, "Lyrics:", right_w);
  wattroff(win, A_BOLD);

  if (app->lyrics_loading) {
    wattron(win, A_DIM);
    mvwaddnstr(win, 5, right_start, "Loading lyrics...", right_w);
    wattroff(win, A_DIM);
    return;
  }

  pthread_mutex_lock(&app->data_lock);
  char *lyrics = app->lyrics_text ? strdup(app->lyrics_text) : NULL;
  pthread_mutex_unlock(&app->data_lock);

  if (!lyrics) {
    wattron(win, A_DIM);
    mvwaddnstr(win, 5, right_start, "No lyrics available.", right_w);
    wattroff(win, A_DIM);
    return;
  }

  char **wrapped = NULL;
  int wrap_count = 0;
  if (!lyrics_wrap_text(lyrics, right_w, &wrapped, &wrap_count)) {
    free(lyrics);
    return;
  }
  free(lyrics);

  lyrics_render_visible(win, wrapped, wrap_count, app, right_start, right_w,
                        bottom_line);
  lyrics_free_wrapped(wrapped, wrap_count);
}

static void draw_playlist_panel(WINDOW *win, AppState *app, int right_start,
                                int right_w, int bottom_line) {
  wattron(win, A_BOLD);
  mvwaddnstr(win, 3, right_start, "Playlist:", right_w);
  wattroff(win, A_BOLD);

  if (!app->playlist || app->playlist_count == 0) {
    mvwaddnstr(win, 4, right_start, "No playlist loaded.", right_w);
    return;
  }

  int current_song = app->player_status.song;
  int max_rows = bottom_line - 4;
  int start_idx = 0;
  if (current_song > max_rows / 2)
    start_idx = current_song - max_rows / 2;
  if (start_idx + max_rows > app->playlist_count)
    start_idx = int_max(0, app->playlist_count - max_rows);

  for (int i = start_idx;
       i < int_min(start_idx + max_rows, app->playlist_count); i++) {
    int row = 4 + (i - start_idx);
    if (row >= bottom_line)
      break;
    char text[STR_LONG];
    snprintf(text, sizeof(text), "%3d. %s - %s", i + 1, app->playlist[i].title,
             app->playlist[i].artist);
    int max_w = int_min(right_w, (int)sizeof(text));
    if (i == current_song) {
      wattron(win, COLOR_PAIR(2));
      mvwaddnstr(win, row, right_start, text, max_w);
      wattroff(win, COLOR_PAIR(2));
    } else {
      mvwaddnstr(win, row, right_start, text, max_w);
    }
  }
}

/* ── Main Summary View ──────────────────────────────────────────────────── */

static void draw_layout_dividers(WINDOW *win, int divider_x, int bottom_line,
                                 int width) {
  for (int row = 3; row < bottom_line; row++)
    mvwaddch(win, row, divider_x, ACS_VLINE);
  mvwaddch(win, 2, divider_x, ACS_TTEE);
  mvwhline(win, bottom_line, 0, ACS_HLINE, width);
  mvwaddch(win, bottom_line, divider_x, ACS_BTEE);
}

void ui_display_player_control(WINDOW *win, AppState *app) {
  if (app->shortcuts_open) {
    ui_draw_shortcuts(win, app);
    return;
  }
  if (!app->active_player || !app->has_status)
    return;

  int height, width;
  getmaxyx(win, height, width);
  int divider_x = width * LAYOUT_LEFT_PCT / 100;
  int bottom_line = height - 3;

  draw_layout_dividers(win, divider_x, bottom_line, width);
  ui_draw_footer(win, app, height, width);
  draw_player_info(win, app, divider_x);

  int left_max = divider_x - 1;
  if (app->show_cover_art)
    draw_cover_art(win, app, left_max, bottom_line);
  else
    draw_detail_section(win, app, left_max, divider_x, bottom_line);

  int right_start = divider_x + 2;
  int right_w = width - right_start - 1;
  if (app->is_radio)
    draw_radio_panel(win, app, right_start, right_w, bottom_line);
  else if (app->show_lyrics)
    draw_lyrics_panel(win, app, right_start, right_w, bottom_line);
  else
    draw_playlist_panel(win, app, right_start, right_w, bottom_line);
}
