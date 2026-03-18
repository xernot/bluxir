/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "ui.h"
#include "util.h"
#include "constants.h"
#include <string.h>
#include <stdio.h>

void ui_display_search_source_selection(WINDOW *win, AppState *app)
{
    int height, width;
    getmaxyx(win, height, width);
    (void)width;
    int max_display = height - SEARCH_HEIGHT_OFFSET;

    mvwaddstr(win, 3, 2, "Select a source to search:");
    mvwaddstr(win, 4, 2, SEARCH_SOURCE_INSTRUCTIONS);

    int count = int_min(app->searchable_count, max_display);
    for (int i = 0; i < count; i++) {
        char line[STR_MEDIUM];
        snprintf(line, sizeof(line), "  %s", app->searchable_sources[i].text);
        if (i == app->search_source_index) wattron(win, COLOR_PAIR(2));
        mvwaddstr(win, 6 + i, 4, line);
        if (i == app->search_source_index) wattroff(win, COLOR_PAIR(2));
    }
}

void ui_display_search_results(WINDOW *win, AppState *app)
{
    int height, width;
    getmaxyx(win, height, width);
    int max_display = height - SEARCH_HEIGHT_OFFSET;

    mvwaddstr(win, 3, 2, "Search Results:");
    mvwaddstr(win, 4, 2, SEARCH_RESULTS_INSTRUCTIONS);

    if (!app->search_results || app->search_results_count == 0) {
        mvwaddstr(win, 6, 4, "No results found.");
        return;
    }

    int start = 0;
    if (app->search_selected_index >= max_display)
        start = app->search_selected_index - max_display + 1;

    for (int i = start; i < int_min(start + max_display, app->search_results_count); i++) {
        PlayerSource *src = &app->search_results[i];
        int display_i = i - start;
        char prefix = src->browse_key[0] ? '+' : ' ';

        char label[STR_LONG];
        if (src->text2[0])
            snprintf(label, sizeof(label), "%s - %s", src->text, src->text2);
        else
            safe_strcpy(label, src->text, sizeof(label));

        char line[STR_LONG];
        snprintf(line, sizeof(line), "%c %s", prefix, label);

        if (i == app->search_selected_index) wattron(win, COLOR_PAIR(2));
        mvwaddnstr(win, 6 + display_i, 4, line, width - 6);
        if (i == app->search_selected_index) wattroff(win, COLOR_PAIR(2));
    }
}
