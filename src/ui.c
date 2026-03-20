/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "ui.h"
#include "../lib/cJSON.h"
#include "config.h"
#include "constants.h"
#include "player.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Message & Highlight ────────────────────────────────────────────────── */

void ui_set_message(AppState *app, const char *msg) {
  if (msg && msg[0]) {
    safe_strcpy(app->header_message, msg, sizeof(app->header_message));
    app->header_message_time = now_sec();
  }
}

void ui_highlight(AppState *app, const char *field) {
  double t = now_sec();
  if (strcmp(field, "volume") == 0)
    app->highlight_times.volume = t;
  else if (strcmp(field, "mute") == 0)
    app->highlight_times.mute = t;
  else if (strcmp(field, "repeat") == 0)
    app->highlight_times.repeat = t;
  else if (strcmp(field, "shuffle") == 0)
    app->highlight_times.shuffle = t;
  else if (strcmp(field, "state") == 0)
    app->highlight_times.state = t;
  else if (strcmp(field, "health") == 0)
    app->highlight_times.health = t;
}

bool ui_is_highlighted(AppState *app, const char *field) {
  double t = 0;
  if (strcmp(field, "volume") == 0)
    t = app->highlight_times.volume;
  else if (strcmp(field, "mute") == 0)
    t = app->highlight_times.mute;
  else if (strcmp(field, "repeat") == 0)
    t = app->highlight_times.repeat;
  else if (strcmp(field, "shuffle") == 0)
    t = app->highlight_times.shuffle;
  else if (strcmp(field, "state") == 0)
    t = app->highlight_times.state;
  else if (strcmp(field, "health") == 0)
    t = app->highlight_times.health;
  return (now_sec() - t) < HIGHLIGHT_DURATION;
}

static bool any_highlighted(AppState *app) {
  return ui_is_highlighted(app, "volume") || ui_is_highlighted(app, "mute") ||
         ui_is_highlighted(app, "repeat") ||
         ui_is_highlighted(app, "shuffle") || ui_is_highlighted(app, "state") ||
         ui_is_highlighted(app, "health");
}

/* ── Header ─────────────────────────────────────────────────────────────── */

typedef struct {
  char text[STR_MEDIUM];
  bool highlighted;
  int color; /* 0 = default (green when highlighted), nonzero = forced color
                pair */
} HeaderSegment;

static int build_header_segments(AppState *app, HeaderSegment *segs) {
  PlayerStatus *ps = &app->player_status;
  const char *repeat_str[] = {"Queue", "Track", "Off"};
  int ri = (ps->repeat >= 0 && ps->repeat <= 2) ? ps->repeat : 2;
  snprintf(segs[0].text, STR_MEDIUM, "Repeat:%s", repeat_str[ri]);
  segs[0].highlighted = ui_is_highlighted(app, "repeat");

  snprintf(segs[1].text, STR_MEDIUM, "Shuffle:%s", ps->shuffle ? "On" : "Off");
  segs[1].highlighted = ui_is_highlighted(app, "shuffle");

  char state[STR_SHORT];
  safe_strcpy(state, ps->state, sizeof(state));
  if (state[0] >= 'a' && state[0] <= 'z')
    state[0] -= 32;
  if (!state[0])
    safe_strcpy(state, "-", sizeof(state));
  safe_strcpy(segs[2].text, state, STR_MEDIUM);
  segs[2].highlighted = ui_is_highlighted(app, "state");
  if (strcmp(ps->state, "play") == 0 || strcmp(ps->state, "stream") == 0)
    segs[2].color = COLOR_PAIR(3); /* Green for playing */
  else if (strcmp(ps->state, "pause") == 0)
    segs[2].color = COLOR_PAIR(4); /* Red for paused */

  char vol_bar[VOLUME_BAR_WIDTH + 4];
  create_volume_bar(ps->volume, VOLUME_BAR_WIDTH, vol_bar, sizeof(vol_bar));
  if (ps->mute)
    snprintf(segs[3].text, STR_MEDIUM, "MUTED %s %d%%", vol_bar, ps->volume);
  else
    snprintf(segs[3].text, STR_MEDIUM, "%s %d%%", vol_bar, ps->volume);
  segs[3].highlighted =
      ui_is_highlighted(app, "volume") || ui_is_highlighted(app, "mute");

  return 4;
}

static void header_draw_status_segments(WINDOW *win, AppState *app,
                                        int header_len, int width) {
  int green = COLOR_PAIR(3);
  HeaderSegment segs[4] = {0};
  int seg_count = build_header_segments(app, segs);
  int total_w = 0;
  for (int i = 0; i < seg_count; i++) {
    total_w += (int)strlen(segs[i].text);
    if (i < seg_count - 1)
      total_w += 3;
  }
  int info_x = width - total_w - 2;
  if (info_x > header_len + 4) {
    int x = info_x;
    for (int i = 0; i < seg_count; i++) {
      int attr = segs[i].color         ? segs[i].color
                 : segs[i].highlighted ? green
                                       : 0;
      wattron(win, attr);
      mvwaddstr(win, 1, x, segs[i].text);
      wattroff(win, attr);
      x += (int)strlen(segs[i].text);
      if (i < seg_count - 1) {
        mvwaddstr(win, 1, x, " | ");
        x += 3;
      }
    }
  }
}

static void header_draw_message(WINDOW *win, AppState *app, int header_len,
                                int width) {
  if (!app->header_message[0])
    return;
  if ((now_sec() - app->header_message_time) >= HEADER_MESSAGE_DURATION)
    return;
  int msg_start = header_len + 6;
  int max_msg = width - msg_start - 2;
  if (max_msg > 0) {
    int attr = any_highlighted(app) ? COLOR_PAIR(3) : 0;
    wattron(win, attr);
    mvwaddnstr(win, 1, msg_start, app->header_message, max_msg);
    wattroff(win, attr);
  }
}

void ui_draw_header(WINDOW *win, AppState *app, const char *view) {
  int height, width;
  getmaxyx(win, height, width);
  (void)height;

  /* Top line */
  mvwhline(win, 0, 0, ACS_HLINE, width);

  /* Header text */
  char header[STR_MEDIUM];
  if (app->active_player) {
    int n = snprintf(header, sizeof(header), "%s - %s", view,
                     app->active_player->name);
    if (app->group_info.slave_count > 0 && n > 0 && n < (int)sizeof(header)) {
      n += snprintf(header + n, sizeof(header) - n, " (&");
      for (int i = 0;
           i < app->group_info.slave_count && n < (int)sizeof(header); i++)
        n += snprintf(header + n, sizeof(header) - n, "%s%s", i > 0 ? ", " : "",
                      app->group_info.slave_names[i]);
      snprintf(header + n, sizeof(header) - n, ")");
    }
  } else {
    safe_strcpy(header, view, sizeof(header));
  }
  int header_len = int_min((int)strlen(header), width - 4);
  mvwaddnstr(win, 1, 2, header, header_len);
  wattron(win, A_BOLD);
  mvwaddnstr(win, 1, 2, header, header_len);
  wattroff(win, A_BOLD);

  if (app->has_status)
    header_draw_status_segments(win, app, header_len, width);
  header_draw_message(win, app, header_len, width);

  /* Separator */
  mvwhline(win, 2, 0, ACS_HLINE, width);
}

/* ── Footer ─────────────────────────────────────────────────────────────── */

void ui_draw_footer(WINDOW *win, AppState *app, int height, int width) {
  (void)app;
  wattron(win, A_DIM);
  mvwaddstr(win, height - 2, 2, FOOTER_HELP);
  int vlen = (int)strlen(VERSION_STRING);
  if (width > vlen + 2) {
    mvwaddstr(win, height - 2, width - vlen - 2, VERSION_STRING);
  }
  wattroff(win, A_DIM);
  mvwhline(win, height - 1, 0, ACS_HLINE, width);
}

/* ── Modal ──────────────────────────────────────────────────────────────── */

static void modal_draw_box(WINDOW *win, int sy, int sx, int modal_w,
                           int modal_h) {
  mvwaddch(win, sy, sx, ACS_ULCORNER);
  mvwhline(win, sy, sx + 1, ACS_HLINE, modal_w - 2);
  mvwaddch(win, sy, sx + modal_w - 1, ACS_URCORNER);
  mvwaddch(win, sy + modal_h - 1, sx, ACS_LLCORNER);
  mvwhline(win, sy + modal_h - 1, sx + 1, ACS_HLINE, modal_w - 2);
  mvwaddch(win, sy + modal_h - 1, sx + modal_w - 1, ACS_LRCORNER);
  for (int row = 1; row < modal_h - 1; row++) {
    mvwaddch(win, sy + row, sx, ACS_VLINE);
    for (int c = 1; c < modal_w - 1; c++)
      mvwaddch(win, sy + row, sx + c, ' ');
    mvwaddch(win, sy + row, sx + modal_w - 1, ACS_VLINE);
  }
}

void ui_draw_modal(WINDOW *win, const char *title, const KeyBinding *entries,
                   int count) {
  int height, width;
  getmaxyx(win, height, width);
  /* Calculate modal size */
  int max_entry = 0;
  for (int i = 0; i < count; i++) {
    int len =
        (int)strlen(entries[i].key) + (int)strlen(entries[i].description) + 7;
    if (len > max_entry)
      max_entry = len;
  }
  int modal_w = int_min(width - 4, int_max((int)strlen(title) + 4, max_entry));
  int modal_h = count + 6;
  int sy = int_max(0, (height - modal_h) / 2);
  int sx = int_max(0, (width - modal_w) / 2);

  modal_draw_box(win, sy, sx, modal_w, modal_h);

  /* Title */
  wattron(win, A_BOLD);
  mvwaddnstr(win, sy + 1, sx + 2, title, modal_w - 4);
  wattroff(win, A_BOLD);

  /* Entries */
  for (int i = 0; i < count; i++) {
    char line[STR_MEDIUM];
    snprintf(line, sizeof(line), "  %-12s %s", entries[i].key,
             entries[i].description);
    mvwaddnstr(win, sy + 2 + i, sx + 2, line, modal_w - 4);
  }

  /* Footer */
  wattron(win, A_DIM);
  mvwaddnstr(win, sy + modal_h - 3, sx + 2, ABOUT_ATTRIBUTION, modal_w - 4);
  mvwaddnstr(win, sy + modal_h - 2, sx + 2, PROJECT_URL, modal_w - 4);
  wattroff(win, A_DIM);
}

/* ── Help Shortcuts ─────────────────────────────────────────────────────── */

static void shortcuts_draw_box(WINDOW *win, int sy, int sx, int modal_w,
                               int modal_h, int div_x) {
  /* Top border */
  mvwaddch(win, sy, sx, ACS_ULCORNER);
  mvwhline(win, sy, sx + 1, ACS_HLINE, modal_w - 2);
  mvwaddch(win, sy, sx + modal_w - 1, ACS_URCORNER);

  /* Bottom border */
  mvwaddch(win, sy + modal_h - 1, sx, ACS_LLCORNER);
  mvwhline(win, sy + modal_h - 1, sx + 1, ACS_HLINE, modal_w - 2);
  mvwaddch(win, sy + modal_h - 1, sx + modal_w - 1, ACS_LRCORNER);

  /* Side borders and fill */
  for (int row = 1; row < modal_h - 1; row++) {
    mvwaddch(win, sy + row, sx, ACS_VLINE);
    for (int c = 1; c < modal_w - 1; c++)
      mvwaddch(win, sy + row, sx + c, ' ');
    mvwaddch(win, sy + row, sx + modal_w - 1, ACS_VLINE);
  }

  /* Title */
  wattron(win, A_BOLD);
  mvwaddnstr(win, sy + 1, sx + 2, "Keyboard Shortcuts", modal_w - 4);
  wattroff(win, A_BOLD);

  /* Title separator */
  int sep_y = sy + 2;
  mvwaddch(win, sep_y, sx, ACS_LTEE);
  mvwhline(win, sep_y, sx + 1, ACS_HLINE, modal_w - 2);
  mvwaddch(win, sep_y, div_x, ACS_TTEE);
  mvwaddch(win, sep_y, sx + modal_w - 1, ACS_RTEE);
}

static void shortcuts_draw_entries(WINDOW *win, int sy, int sx, int div_x,
                                   int modal_w, int num_rows) {
  /* Vertical divider */
  for (int i = 0; i < num_rows; i++)
    mvwaddch(win, sy + 3 + i, div_x, ACS_VLINE);

  /* Entries */
  for (int i = 0; i < num_rows; i++) {
    int row = sy + 3 + i;
    if (i < HELP_LEFT_COUNT) {
      char line[64];
      snprintf(line, sizeof(line), "  %-10s %s", HELP_LEFT_KEYS[i].key,
               HELP_LEFT_KEYS[i].description);
      mvwaddnstr(win, row, sx + 2, line, HELP_DIV_OFFSET - 2);
    }
    if (i < HELP_RIGHT_COUNT) {
      char line[64];
      snprintf(line, sizeof(line), " %-10s %s", HELP_RIGHT_KEYS[i].key,
               HELP_RIGHT_KEYS[i].description);
      mvwaddnstr(win, row, div_x + 1, line, modal_w - HELP_DIV_OFFSET - 2);
    }
  }

  /* Footer separator */
  int fsep_y = sy + 3 + num_rows;
  mvwaddch(win, fsep_y, sx, ACS_LTEE);
  mvwhline(win, fsep_y, sx + 1, ACS_HLINE, modal_w - 2);
  mvwaddch(win, fsep_y, div_x, ACS_BTEE);
  mvwaddch(win, fsep_y, sx + modal_w - 1, ACS_RTEE);
}

void ui_draw_shortcuts(WINDOW *win, AppState *app) {
  (void)app;
  int height, width;
  getmaxyx(win, height, width);

  int col_w = HELP_COL_WIDTH;
  int num_rows = int_max(HELP_LEFT_COUNT, HELP_RIGHT_COUNT);
  int modal_w = int_min(width - 4, col_w * 2 + 7);
  int modal_h = num_rows + 7;
  int sy = int_max(0, (height - modal_h) / 2);
  int sx = int_max(0, (width - modal_w) / 2);
  int div_x = sx + HELP_DIV_OFFSET;

  shortcuts_draw_box(win, sy, sx, modal_w, modal_h, div_x);
  shortcuts_draw_entries(win, sy, sx, div_x, modal_w, num_rows);

  /* Footer */
  wattron(win, A_DIM);
  mvwaddnstr(win, sy + modal_h - 3, sx + 2, ABOUT_ATTRIBUTION, modal_w - 4);
  mvwaddnstr(win, sy + modal_h - 2, sx + 2, PROJECT_URL, modal_w - 4);
  wattroff(win, A_DIM);
}

void ui_draw_selector_shortcuts(WINDOW *win) {
  ui_draw_modal(win, "Player Selector Shortcuts", SELECTOR_SHORTCUTS,
                SELECTOR_SHORTCUTS_COUNT);
}

/* ── Input ──────────────────────────────────────────────────────────────── */

bool ui_confirm_prompt(WINDOW *win, const char *prompt) {
  int height, width;
  getmaxyx(win, height, width);
  (void)width;
  int footer_row = height - 2;
  wmove(win, footer_row, 0);
  wclrtoeol(win);
  wattron(win, A_BOLD);
  mvwaddstr(win, footer_row, 2, prompt);
  wattroff(win, A_BOLD);
  wrefresh(win);
  wtimeout(win, INPUT_BLOCKING);
  int ch = wgetch(win);
  wtimeout(win, CURSES_POLL_MS);
  return (ch == 'y' || ch == 'Y');
}

int ui_get_input(WINDOW *win, const char *prompt, char *buf, size_t buf_size) {
  noecho();
  waddstr(win, prompt);
  wrefresh(win);
  int len = 0;
  buf[0] = '\0';

  while (1) {
    int ch = wgetch(win);
    if (ch == 10)
      break;        /* Enter */
    if (ch == 27) { /* Escape */
      len = 0;
      buf[0] = '\0';
      break;
    }
    if (ch == 127 || ch == 8 || ch == KEY_BACKSPACE) {
      if (len > 0) {
        len--;
        buf[len] = '\0';
        int y, x;
        getyx(win, y, x);
        mvwaddch(win, y, x - 1, ' ');
        wmove(win, y, x - 1);
      }
    } else if (ch >= 32 && ch <= 126 && len + 1 < (int)buf_size) {
      buf[len++] = (char)ch;
      buf[len] = '\0';
      waddch(win, ch);
    }
    wrefresh(win);
  }
  return len;
}

const char *ui_show_queue_dialog(WINDOW *win) {
  int height, width;
  getmaxyx(win, height, width);
  (void)width;
  int footer_row = height - 2;
  wmove(win, footer_row, 0);
  wclrtoeol(win);
  wattron(win, A_BOLD);
  mvwaddstr(win, footer_row, 2, QUEUE_ACTION_PROMPT);
  wattroff(win, A_BOLD);
  wrefresh(win);
  wtimeout(win, INPUT_BLOCKING);
  int key = wgetch(win);
  wtimeout(win, CURSES_POLL_MS);
  switch (key) {
  case '1':
    return "play_now";
  case '2':
    return "add_next";
  case '3':
    return "add_last";
  default:
    return NULL;
  }
}

/* ── Health Check ───────────────────────────────────────────────────────── */

static void health_add_web_entries(BlusoundPlayer *p, KVPair *entries,
                                   int *entry_count, bool update_available) {
  char web_url[STR_URL];
  player_get_web_url(p, web_url, sizeof(web_url));
  if (update_available) {
    safe_strcpy(entries[*entry_count].key, "Upgrade URL",
                sizeof(entries[0].key));
    snprintf(entries[*entry_count].value, sizeof(entries[0].value),
             "%s/upgrade", web_url);
    (*entry_count)++;
  }
  safe_strcpy(entries[*entry_count].key, "Web Interface",
              sizeof(entries[0].key));
  safe_strcpy(entries[*entry_count].value, web_url, sizeof(entries[0].value));
  (*entry_count)++;
}

static bool health_fetch_entries(AppState *app, KVPair *entries,
                                 int *entry_count) {
  *entry_count = 0;

  KVPair sync[8];
  int sync_count = 0;
  player_get_sync_info(app->active_player, sync, &sync_count, 8);
  for (int i = 0; i < sync_count && *entry_count < 30; i++)
    entries[(*entry_count)++] = sync[i];

  KVPair diag[20];
  int diag_count = 0;
  player_get_diagnostics(app->active_player, diag, &diag_count, 20);
  for (int i = 0; i < diag_count && *entry_count < 30; i++)
    entries[(*entry_count)++] = diag[i];

  char upgrade[STR_LONG] = "";
  player_get_upgrade_status(app->active_player, upgrade, sizeof(upgrade));
  bool is_valid = (strncasecmp(upgrade, "error", 5) != 0);
  bool update_available =
      is_valid && !str_contains_ci(upgrade, HEALTH_NO_UPDATE);

  safe_strcpy(entries[*entry_count].key, "Update Status",
              sizeof(entries[0].key));
  safe_strcpy(entries[*entry_count].value, upgrade, sizeof(entries[0].value));
  (*entry_count)++;

  health_add_web_entries(app->active_player, entries, entry_count,
                         update_available);

  return update_available;
}

static void health_calc_dimensions(KVPair *entries, int entry_count, int scr_h,
                                   int scr_w, int *key_w, int *modal_w,
                                   int *modal_h, int *sy, int *sx) {
  *key_w = 0;
  for (int i = 0; i < entry_count; i++) {
    int kl = (int)strlen(entries[i].key);
    if (kl > *key_w)
      *key_w = kl;
  }
  int content_w = 0;
  for (int i = 0; i < entry_count; i++) {
    int cw = *key_w + (int)strlen(entries[i].value) + 5;
    if (cw > content_w)
      content_w = cw;
  }
  int title_len = (int)strlen(HEALTH_CHECK_TITLE_OK);
  if (content_w < title_len + 4)
    content_w = title_len + 4;
  int padded_w = (int)(content_w * HEALTH_WIDTH_FACTOR);
  *modal_w = int_min(scr_w - 2, padded_w);
  *modal_h = int_min(scr_h - 2, entry_count + 5);
  *sy = int_max(0, (scr_h - *modal_h) / 2);
  *sx = int_max(0, (scr_w - *modal_w) / 2);
}

static void health_render_entries(WINDOW *popup, KVPair *entries,
                                  int entry_count, int key_w, int modal_w,
                                  int modal_h, bool update_available) {
  int green_attr = COLOR_PAIR(3);
  int red_attr = COLOR_PAIR(4);

  if (update_available) {
    wattron(popup, A_BOLD);
    mvwaddnstr(popup, 1, 2, HEALTH_CHECK_TITLE, modal_w - 4);
    wattroff(popup, A_BOLD);
  } else {
    wattron(popup, green_attr | A_BOLD);
    mvwaddnstr(popup, 1, 2, HEALTH_CHECK_TITLE_OK, modal_w - 4);
    wattroff(popup, green_attr | A_BOLD);
  }

  mvwhline(popup, 2, 1, ACS_HLINE, modal_w - 2);
  mvwaddch(popup, 2, 0, ACS_LTEE);
  mvwaddch(popup, 2, modal_w - 1, ACS_RTEE);

  int max_rows = modal_h - 5;
  for (int i = 0; i < entry_count && i < max_rows; i++) {
    char line[STR_LONG];
    snprintf(line, sizeof(line), " %-*s  %s", key_w, entries[i].key,
             entries[i].value);
    bool is_warning = (strcmp(entries[i].key, "Update Status") == 0 &&
                       !str_contains_ci(entries[i].value, HEALTH_NO_UPDATE)) ||
                      strcmp(entries[i].key, "Upgrade URL") == 0;
    int attr = is_warning ? (red_attr | A_BOLD) : 0;
    wattron(popup, attr);
    mvwaddnstr(popup, 3 + i, 1, line, modal_w - 2);
    wattroff(popup, attr);
  }

  const char *hint = "Press 'q' to close";
  int hint_x = int_max(1, modal_w - (int)strlen(hint) - 2);
  wattron(popup, A_DIM);
  mvwaddstr(popup, modal_h - 2, hint_x, hint);
  wattroff(popup, A_DIM);
}

static void health_draw_overlay(WINDOW *win, KVPair *entries, int entry_count,
                                bool update_available) {
  int height, width;
  getmaxyx(win, height, width);
  int key_w, modal_w, modal_h, sy, sx;
  health_calc_dimensions(entries, entry_count, height, width, &key_w, &modal_w,
                         &modal_h, &sy, &sx);

  WINDOW *popup = newwin(modal_h, modal_w, sy, sx);
  box(popup, 0, 0);
  health_render_entries(popup, entries, entry_count, key_w, modal_w, modal_h,
                        update_available);
  wrefresh(popup);

  wtimeout(win, INPUT_BLOCKING);
  while (1) {
    int key = wgetch(win);
    if (key == 'q')
      break;
  }
  wtimeout(win, CURSES_POLL_MS);
  delwin(popup);
  touchwin(win);
  wrefresh(win);
}

void ui_show_health_check(WINDOW *win, AppState *app) {
  if (!app->active_player)
    return;

  KVPair entries[32];
  int entry_count = 0;
  bool update_available = health_fetch_entries(app, entries, &entry_count);

  if (!update_available) {
    ui_set_message(app, HEALTH_STATUS_OK);
    ui_highlight(app, "health");
  }

  health_draw_overlay(win, entries, entry_count, update_available);
}

/* ── Group Manager ──────────────────────────────────────────────────────── */

static bool is_player_grouped(const char *ip, GroupInfo *group) {
  for (int i = 0; i < group->slave_count; i++) {
    if (strcmp(group->slave_ips[i], ip) == 0)
      return true;
  }
  return false;
}

static void group_draw_header(WINDOW *popup, AppState *app, GroupInfo *group,
                              int modal_w) {
  wattron(popup, A_BOLD);
  mvwaddnstr(popup, 1, 2, GROUP_MANAGER_TITLE, modal_w - 4);
  wattroff(popup, A_BOLD);
  mvwhline(popup, 2, 1, ACS_HLINE, modal_w - 2);
  mvwaddch(popup, 2, 0, ACS_LTEE);
  mvwaddch(popup, 2, modal_w - 1, ACS_RTEE);
  char line[STR_MEDIUM];
  if (group->master_ip[0])
    snprintf(line, sizeof(line), "Grouped with: %s", group->master_ip);
  else
    snprintf(line, sizeof(line), "Master: %s (%s)", app->active_player->name,
             app->active_player->host_name);
  mvwaddnstr(popup, 3, 2, line, modal_w - 4);
}

static void group_draw_players(WINDOW *popup, BlusoundPlayer **others,
                               int count, GroupInfo *group, int selected,
                               int modal_w) {
  if (count == 0) {
    mvwaddnstr(popup, 5, 2, GROUP_NO_PLAYERS_MSG, modal_w - 4);
    wattron(popup, A_DIM);
    mvwaddnstr(popup, 6, 2, GROUP_DISCOVER_HINT, modal_w - 4);
    wattroff(popup, A_DIM);
    return;
  }
  for (int i = 0; i < count; i++) {
    bool grouped = is_player_grouped(others[i]->host_name, group);
    char line[STR_LONG];
    snprintf(line, sizeof(line), " %s %s (%s)", grouped ? "[G]" : "[ ]",
             others[i]->name, others[i]->host_name);
    if (i == selected)
      wattron(popup, COLOR_PAIR(2));
    mvwaddnstr(popup, 5 + i, 2, line, modal_w - 4);
    if (i == selected)
      wattroff(popup, COLOR_PAIR(2));
  }
}

static void group_handle_toggle(AppState *app, BlusoundPlayer *player,
                                GroupInfo *group) {
  if (is_player_grouped(player->host_name, group))
    player_remove_slave(app->active_player, player->host_name);
  else
    player_add_slave(app->active_player, player->host_name);
  usleep(500000);
  player_get_group_info(app->active_player, group);
}

static int group_handle_input(int key, AppState *app, BlusoundPlayer **others,
                              int other_count, GroupInfo *group, bool *is_slave,
                              int selected) {
  if (key == 'q' || key == 27)
    return -1;
  if (*is_slave && key == 10) {
    player_leave_group(app->active_player);
    player_get_group_info(app->active_player, group);
    *is_slave = (group->master_ip[0] != '\0');
  } else if (!*is_slave && other_count > 0) {
    if (key == KEY_UP && selected > 0)
      selected--;
    else if (key == KEY_DOWN && selected < other_count - 1)
      selected++;
    else if (key == 10)
      group_handle_toggle(app, others[selected], group);
  }
  return selected;
}

void ui_show_group_manager(WINDOW *win, AppState *app) {
  if (!app->active_player)
    return;
  GroupInfo group = app->group_info;
  BlusoundPlayer *others[16];
  int other_count = 0;
  for (int i = 0; i < app->players_count && other_count < 16; i++) {
    if (strcmp(app->players[i]->host_name, app->active_player->host_name) != 0)
      others[other_count++] = app->players[i];
  }
  bool is_slave = (group.master_ip[0] != '\0');
  int height, width;
  getmaxyx(win, height, width);
  int modal_h = int_min(height - 2, int_max(other_count + 9, 10));
  int modal_w = int_min(width - 4, GROUP_MODAL_WIDTH);
  int sy = int_max(0, (height - modal_h) / 2);
  int sx = int_max(0, (width - modal_w) / 2);
  int selected = 0;
  WINDOW *popup = newwin(modal_h, modal_w, sy, sx);
  while (1) {
    werase(popup);
    box(popup, 0, 0);
    group_draw_header(popup, app, &group, modal_w);
    if (is_slave)
      mvwaddnstr(popup, 5, 2, GROUP_SLAVE_MSG, modal_w - 4);
    else
      group_draw_players(popup, others, other_count, &group, selected, modal_w);
    const char *hint = is_slave ? GROUP_SLAVE_HINT : GROUP_MANAGER_HINT;
    wattron(popup, A_DIM);
    mvwaddstr(popup, modal_h - 2, int_max(1, modal_w - (int)strlen(hint) - 2),
              hint);
    wattroff(popup, A_DIM);
    wrefresh(popup);
    wtimeout(win, INPUT_BLOCKING);
    int key = wgetch(win);
    selected = group_handle_input(key, app, others, other_count, &group,
                                  &is_slave, selected);
    if (selected < 0)
      break;
  }
  app->group_info = group;
  wtimeout(win, CURSES_POLL_MS);
  delwin(popup);
  touchwin(win);
  wrefresh(win);
}

/* ── Volume Overlay (group mode) ───────────────────────────────────────── */

static BlusoundPlayer *find_player_by_ip(AppState *app, const char *ip) {
  for (int i = 0; i < app->players_count; i++) {
    if (strcmp(app->players[i]->host_name, ip) == 0)
      return app->players[i];
  }
  return NULL;
}

static int vol_build_entries(AppState *app, GroupInfo *group,
                             BlusoundPlayer **players, int *volumes) {
  int count = 0;
  players[count] = app->active_player;
  volumes[count] = app->player_status.volume;
  count++;
  for (int i = 0; i < group->slave_count && count < 16; i++) {
    BlusoundPlayer *p = find_player_by_ip(app, group->slave_ips[i]);
    if (!p)
      continue;
    volumes[count] = player_get_volume(p);
    players[count] = p;
    count++;
  }
  return count;
}

static void vol_draw_entry(WINDOW *popup, BlusoundPlayer *player, int volume,
                           bool selected, int row, int name_w, int modal_w) {
  char name_buf[STR_MEDIUM];
  snprintf(name_buf, sizeof(name_buf), "%-*.*s", name_w, name_w, player->name);
  int green = COLOR_PAIR(3);
  if (selected)
    wattron(popup, green | A_BOLD);
  mvwaddnstr(popup, row, 2, name_buf, modal_w - 4);
  if (selected)
    wattroff(popup, A_BOLD);
  if (volume >= 0) {
    char bar[VOLUME_BAR_WIDTH + 4];
    create_volume_bar(volume, VOLUME_BAR_WIDTH, bar, sizeof(bar));
    char vol_text[32];
    snprintf(vol_text, sizeof(vol_text), "  %s %3d%%", bar, volume);
    waddstr(popup, vol_text);
  }
  if (selected)
    wattroff(popup, green);
}

static int vol_max_name_width(BlusoundPlayer **players, int count) {
  int max_w = (int)strlen(GROUP_VOLUME_LABEL);
  for (int i = 0; i < count; i++) {
    int w = (int)strlen(players[i]->name);
    if (w > max_w)
      max_w = w;
  }
  return max_w;
}

static int vol_group_average(int *volumes, int count) {
  int sum = 0, n = 0;
  for (int i = 0; i < count; i++) {
    if (volumes[i] >= 0) {
      sum += volumes[i];
      n++;
    }
  }
  return n > 0 ? sum / n : 0;
}

static void vol_draw_group_row(WINDOW *popup, int avg_vol, bool selected,
                               int name_w, int modal_w) {
  char name_buf[STR_MEDIUM];
  snprintf(name_buf, sizeof(name_buf), "%-*.*s", name_w, name_w,
           GROUP_VOLUME_LABEL);
  int green = COLOR_PAIR(3);
  if (selected)
    wattron(popup, green | A_BOLD);
  mvwaddnstr(popup, 2, 2, name_buf, modal_w - 4);
  if (selected)
    wattroff(popup, A_BOLD);
  char bar[VOLUME_BAR_WIDTH + 4];
  create_volume_bar(avg_vol, VOLUME_BAR_WIDTH, bar, sizeof(bar));
  char vol_text[32];
  snprintf(vol_text, sizeof(vol_text), "  %s %3d%%", bar, avg_vol);
  waddstr(popup, vol_text);
  if (selected)
    wattroff(popup, green);
}

static void vol_draw_entries(WINDOW *popup, BlusoundPlayer **players,
                             int *volumes, int count, int selected, int name_w,
                             int modal_w) {
  vol_draw_group_row(popup, vol_group_average(volumes, count), selected == 0,
                     name_w, modal_w);
  for (int i = 0; i < count; i++)
    vol_draw_entry(popup, players[i], volumes[i], (i + 1) == selected, 3 + i,
                   name_w, modal_w);
}

void ui_show_volume_overlay(WINDOW *win, AppState *app, GroupInfo *group) {
  BlusoundPlayer *players[16];
  int volumes[16];
  int count = vol_build_entries(app, group, players, volumes);
  if (count < 2)
    return;
  int name_w = vol_max_name_width(players, count);
  int content_w = name_w + VOLUME_BAR_WIDTH + 12;
  int height, width;
  getmaxyx(win, height, width);
  int modal_w = int_min(width - 4, int_max(content_w + 6, 30));
  int total_rows = count + 1;
  int modal_h = total_rows + 3;
  int sy = int_max(0, (height - modal_h) / 2);
  int sx = int_max(0, (width - modal_w) / 2);
  int selected = 0;
  WINDOW *popup = newwin(modal_h, modal_w, sy, sx);
  while (1) {
    werase(popup);
    box(popup, 0, 0);
    int green = COLOR_PAIR(3);
    wattron(popup, green | A_BOLD);
    mvwaddnstr(popup, 0, 2, " Volume ", modal_w - 4);
    wattroff(popup, green | A_BOLD);
    mvwhline(popup, 1, 1, ACS_HLINE, modal_w - 2);
    mvwaddch(popup, 1, 0, ACS_LTEE);
    mvwaddch(popup, 1, modal_w - 1, ACS_RTEE);
    volumes[0] = app->player_status.volume;
    vol_draw_entries(popup, players, volumes, count, selected, name_w, modal_w);
    wrefresh(popup);
    wtimeout(win, CURSES_POLL_MS);
    int key = wgetch(win);
    if (key == -1)
      continue;
    if (key == 'q' || key == 27)
      break;
    if (key == KEY_LEFT && selected > 0)
      selected--;
    else if (key == KEY_RIGHT && selected < total_rows - 1)
      selected++;
    else if (key == KEY_UP || key == KEY_DOWN) {
      int delta = (key == KEY_UP) ? VOLUME_INCREMENT : -VOLUME_INCREMENT;
      if (selected == 0) {
        for (int i = 0; i < count; i++) {
          if (volumes[i] < 0)
            continue;
          int nv = int_max(0, int_min(100, volumes[i] + delta));
          if (player_set_volume(players[i], nv)) {
            volumes[i] = nv;
            if (players[i] == app->active_player)
              app->player_status.volume = nv;
          }
        }
      } else {
        int pi = selected - 1;
        if (volumes[pi] >= 0) {
          int nv = int_max(0, int_min(100, volumes[pi] + delta));
          if (player_set_volume(players[pi], nv)) {
            volumes[pi] = nv;
            if (players[pi] == app->active_player)
              app->player_status.volume = nv;
          }
        }
      }
    }
  }
  wtimeout(win, CURSES_POLL_MS);
  delwin(popup);
  touchwin(win);
  wrefresh(win);
}

/* ── Pretty Print ───────────────────────────────────────────────────────── */

static char *pretty_print_build_json(AppState *app) {
  cJSON *root = cJSON_CreateObject();
  cJSON *player_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(player_obj, "name", app->active_player->name);
  cJSON_AddStringToObject(player_obj, "host_name",
                          app->active_player->host_name);
  cJSON_AddStringToObject(player_obj, "base_url", app->active_player->base_url);
  cJSON_AddItemToObject(root, "player", player_obj);

  PlayerStatus *ps = &app->player_status;
  cJSON *status = cJSON_CreateObject();
  cJSON_AddStringToObject(status, "name", ps->name);
  cJSON_AddStringToObject(status, "artist", ps->artist);
  cJSON_AddStringToObject(status, "album", ps->album);
  cJSON_AddStringToObject(status, "state", ps->state);
  cJSON_AddNumberToObject(status, "volume", ps->volume);
  cJSON_AddStringToObject(status, "service", ps->service);
  cJSON_AddNumberToObject(status, "song", ps->song);
  cJSON_AddNumberToObject(status, "secs", ps->secs);
  cJSON_AddNumberToObject(status, "totlen", ps->totlen);
  cJSON_AddStringToObject(status, "stream_format", ps->stream_format);
  cJSON_AddNumberToObject(status, "repeat", ps->repeat);
  cJSON_AddBoolToObject(status, "shuffle", ps->shuffle);
  cJSON_AddBoolToObject(status, "mute", ps->mute);
  cJSON_AddStringToObject(status, "image", ps->image);
  cJSON_AddItemToObject(root, "status", status);

  char *pretty = cJSON_Print(root);
  cJSON_Delete(root);
  return pretty;
}

static void pretty_print_count_lines(const char *text, int *line_count,
                                     int *max_line_w) {
  *line_count = 1;
  *max_line_w = 0;
  int cur_w = 0;
  for (const char *p = text; *p; p++) {
    if (*p == '\n') {
      if (cur_w > *max_line_w)
        *max_line_w = cur_w;
      cur_w = 0;
      (*line_count)++;
    } else {
      cur_w++;
    }
  }
  if (cur_w > *max_line_w)
    *max_line_w = cur_w;
}

static void pretty_print_fill_pad(WINDOW *pad, char *pretty, int content_w) {
  int line_idx = 0;
  char *line_start = pretty;
  for (char *p = pretty;; p++) {
    if (*p == '\n' || *p == '\0') {
      char saved = *p;
      *p = '\0';
      mvwaddnstr(pad, line_idx, 0, line_start, content_w);
      line_idx++;
      if (saved == '\0')
        break;
      line_start = p + 1;
    }
  }
}

static void pretty_print_show_popup(WINDOW *win, char *pretty, int line_count,
                                    int max_line_w) {
  int height, width;
  getmaxyx(win, height, width);
  int content_w = int_min(max_line_w, width - 6);
  int content_h = int_min(line_count, height - 6);
  int popup_h = content_h + 2;
  int popup_w = content_w + 2;
  int py = (height - popup_h) / 2;
  int px = (width - popup_w) / 2;

  WINDOW *popup_win = newwin(popup_h, popup_w, py, px);
  box(popup_win, 0, 0);
  wattron(popup_win, A_REVERSE);
  mvwaddstr(popup_win, 0, 2, PRETTY_PRINT_TITLE);
  wattroff(popup_win, A_REVERSE);

  WINDOW *pad = newpad(line_count + 1, content_w + 1);
  pretty_print_fill_pad(pad, pretty, content_w);

  int pad_pos = 0;
  wrefresh(popup_win);
  while (1) {
    prefresh(pad, pad_pos, 0, py + 1, px + 1, py + popup_h - 2,
             px + popup_w - 2);
    int key = wgetch(win);
    if (key == 'q')
      break;
    if (key == KEY_DOWN && pad_pos < line_count - content_h)
      pad_pos++;
    if (key == KEY_UP && pad_pos > 0)
      pad_pos--;
  }

  delwin(pad);
  delwin(popup_win);
  touchwin(win);
  wrefresh(win);
}

void ui_show_pretty_print(WINDOW *win, AppState *app) {
  if (!app->active_player || !app->has_status)
    return;

  char *pretty = pretty_print_build_json(app);
  if (!pretty)
    return;

  int line_count, max_line_w;
  pretty_print_count_lines(pretty, &line_count, &max_line_w);
  pretty_print_show_popup(win, pretty, line_count, max_line_w);
  free(pretty);
}
