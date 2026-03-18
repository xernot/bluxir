/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "constants.h"
#include "ui.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static void browse_draw_source_list(WINDOW *win, AppState *app, int sel,
                                    int start, int end, int width) {
  for (int i = start; i < end; i++) {
    PlayerSource *src = &app->current_sources[i];
    char prefix = (i == sel) ? '>' : ' ';
    char expand = src->browse_key[0] ? '+' : ' ';

    char label[STR_LONG];
    if (src->text2[0])
      snprintf(label, sizeof(label), "%s - %s", src->text, src->text2);
    else
      safe_strcpy(label, src->text, sizeof(label));
    if ((int)strlen(label) > SOURCE_NAME_MAX_LENGTH)
      label[SOURCE_NAME_MAX_LENGTH - 1] = '\0';

    char line[STR_LONG];
    snprintf(line, sizeof(line), "%c %c %s", prefix, expand, label);

    if (i == sel)
      wattron(win, COLOR_PAIR(2));
    mvwaddnstr(win, 8 + (i - start), 4, line, width - 6);
    if (i == sel)
      wattroff(win, COLOR_PAIR(2));
  }
}

void ui_display_source_selection(WINDOW *win, AppState *app) {
  int height, width;
  getmaxyx(win, height, width);
  int max_display = height - SOURCE_LIST_HEIGHT_OFFSET;

  mvwaddstr(win, 3, 2, BROWSE_INSTRUCTIONS_1);
  mvwaddstr(win, 4, 2, BROWSE_INSTRUCTIONS_2);
  mvwaddstr(win, 5, 2, BROWSE_SORT_INSTRUCTIONS);

  const char *sort_label = "Original";
  if (app->source_sort == 't')
    sort_label = "Title";
  else if (app->source_sort == 'a')
    sort_label = "Artist";
  char sort_text[STR_MEDIUM];
  snprintf(sort_text, sizeof(sort_text), "Select Source:  [sorted by %s]",
           sort_label);
  mvwaddstr(win, 7, 2, sort_text);

  if (!app->current_sources || app->current_sources_count == 0) {
    mvwaddstr(win, 8, 4, "No sources available.");
    return;
  }

  int sel = app->selected_source_index[app->source_depth];
  if (sel >= app->current_sources_count)
    sel = int_max(0, app->current_sources_count - 1);
  if (sel < 0)
    sel = 0;
  app->selected_source_index[app->source_depth] = sel;

  int total = app->current_sources_count;
  int current_page = int_max(0, sel / int_max(1, max_display));
  int start = int_max(0, current_page * max_display);
  int end = int_min(start + max_display, total);

  browse_draw_source_list(win, app, sel, start, end, width);

  if (total > max_display) {
    char page_info[32];
    int total_pages = (total + max_display - 1) / max_display;
    snprintf(page_info, sizeof(page_info), "Page %d/%d", current_page + 1,
             total_pages);
    mvwaddstr(win, height - 2, width - (int)strlen(page_info) - 2, page_info);
  }
}

void ui_display_player_selection(WINDOW *win, AppState *app) {
  if (app->selector_shortcuts_open) {
    ui_draw_selector_shortcuts(win);
    return;
  }

  mvwaddstr(win, 3, 2, "Discovered Blusound players:");
  for (int i = 0; i < app->players_count; i++) {
    char line[STR_LONG];
    const char *marker = (app->players[i] == app->active_player) ? "* " : "  ";
    snprintf(line, sizeof(line), "%s%s (%s)", marker, app->players[i]->name,
             app->players[i]->host_name);
    if (i == app->selected_index)
      wattron(win, COLOR_PAIR(2));
    mvwaddstr(win, 4 + i, 4, line);
    if (i == app->selected_index)
      wattroff(win, COLOR_PAIR(2));
  }
  int height, width;
  getmaxyx(win, height, width);
  (void)width;
  mvwaddstr(win, height - 1, 2, "Press '?' to show keyboard shortcuts");
}
