/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef UI_H
#define UI_H

#include "types.h"
#include <ncurses.h>

/* Draw the header bar (top line, player name, status indicators) */
void ui_draw_header(WINDOW *win, AppState *app, const char *view);

/* Draw the footer bar */
void ui_draw_footer(WINDOW *win, AppState *app, int height, int width);

/* Draw a generic modal dialog with key-description entries */
void ui_draw_modal(WINDOW *win, const char *title,
                   const KeyBinding *entries, int count);

/* Draw the help shortcuts modal (two columns) */
void ui_draw_shortcuts(WINDOW *win, AppState *app);

/* Draw the player selector shortcuts modal */
void ui_draw_selector_shortcuts(WINDOW *win);

/* Show a y/n confirmation prompt on the footer line. Returns true for y/Y. */
bool ui_confirm_prompt(WINDOW *win, const char *prompt);

/* Get text input from user (blocking). Result stored in buf. Returns length. */
int ui_get_input(WINDOW *win, const char *prompt, char *buf, size_t buf_size);

/* Set a header message */
void ui_set_message(AppState *app, const char *msg);

/* Mark a field as highlighted (for green flash) */
void ui_highlight(AppState *app, const char *field);

/* Check if a field is currently highlighted */
bool ui_is_highlighted(AppState *app, const char *field);

/* Show the health check overlay (blocking) */
void ui_show_health_check(WINDOW *win, AppState *app);

/* Show pretty print popup (blocking) */
void ui_show_pretty_print(WINDOW *win, AppState *app);

/* Show the queue action dialog. Returns "play_now", "add_next", "add_last", or NULL. */
const char *ui_show_queue_dialog(WINDOW *win);

#endif /* UI_H */
