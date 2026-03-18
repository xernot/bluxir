/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <ncurses.h>
#include <pthread.h>

#include "types.h"
#include "constants.h"
#include "config.h"
#include "player.h"
#include "metadata.h"
#include "discover.h"
#include "cover_art.h"
#include "ui.h"
#include "util.h"
#include "logger.h"

/* Forward declarations for UI display functions in ui_player.c / ui_browse.c / ui_search.c */
void ui_display_player_control(WINDOW *win, AppState *app);
void ui_display_source_selection(WINDOW *win, AppState *app);
void ui_display_player_selection(WINDOW *win, AppState *app);
void ui_display_search_source_selection(WINDOW *win, AppState *app);
void ui_display_search_results(WINDOW *win, AppState *app);

/* Forward declarations for functions used before definition */
static void check_metadata_update(AppState *app);
static void update_player_status(AppState *app);

static Logger *main_logger = NULL;

/* ── Application State ──────────────────────────────────────────────────── */

static void app_state_init(AppState *app)
{
    memset(app, 0, sizeof(AppState));
    app->source_sort = 'o';
    pthread_mutex_init(&app->data_lock, NULL);
}

static void app_state_destroy(AppState *app)
{
    free(app->wiki_text);
    free(app->lyrics_text);
    free(app->cover_art_raw);
    free(app->playlist);
    /* search_results and current_sources point to player-owned or malloc'd memory */
    player_sources_free(app->search_results, app->search_results_count);
    free(app->searchable_sources);
    /* search history */
    for (int i = 0; i < app->search_history_count; i++) {
        player_sources_free(app->search_history[i].results,
                            app->search_history[i].results_count);
    }
    pthread_mutex_destroy(&app->data_lock);
}

/* ── Status Update ──────────────────────────────────────────────────────── */

static void update_player_status(AppState *app)
{
    if (!app->active_player) return;
    PlayerStatus status;
    if (!player_get_status(app->active_player, &status)) {
        LOG_ERROR(main_logger, "Error updating player status");
        return;
    }
    app->player_status = status;
    app->has_status = true;

    if (strcmp(status.state, "stream") == 0) {
        app->is_radio = true;
        free(app->playlist);
        app->playlist = NULL;
        app->playlist_count = 0;
        if (status.title2[0]) safe_strcpy(app->radio_title2, status.title2, sizeof(app->radio_title2));
        if (status.title3[0]) safe_strcpy(app->radio_title3, status.title3, sizeof(app->radio_title3));
    } else if (app->is_radio && (strcmp(status.state, "pause") == 0 || strcmp(status.state, "stop") == 0)) {
        free(app->playlist);
        app->playlist = NULL;
        app->playlist_count = 0;
    } else {
        app->is_radio = false;
        app->radio_title2[0] = '\0';
        app->radio_title3[0] = '\0';
        free(app->playlist);
        app->playlist = player_get_playlist(app->active_player, &app->playlist_count);
    }
    check_metadata_update(app);
}

/* ── Metadata Thread Launchers ──────────────────────────────────────────── */

typedef struct {
    AppState *app;
    char title[STR_MEDIUM];
    char artist[STR_MEDIUM];
    char album[STR_MEDIUM];
} MetaThreadArg;

static void *fetch_combined_thread(void *arg)
{
    MetaThreadArg *mta = arg;
    AppState *app = mta->app;

    char *api_key = config_get("openai_api_key");
    char *sys_prompt = config_get("openai_system_prompt");
    char *model = config_get("openai_model");

    CombinedInfo info;
    metadata_get_combined(mta->title, mta->artist, mta->album,
                          api_key, sys_prompt, model ? model : DEFAULT_OPENAI_MODEL, &info);

    pthread_mutex_lock(&app->data_lock);
    app->mb_info = info;
    app->mb_has_info = true;
    free(app->wiki_text);
    app->wiki_text = info.has_track_info ? strdup(info.track_info) : NULL;
    app->mb_loading = false;
    app->wiki_loading = false;
    pthread_mutex_unlock(&app->data_lock);

    free(api_key);
    free(sys_prompt);
    free(model);
    free(mta);
    return NULL;
}

static void *fetch_station_thread(void *arg)
{
    MetaThreadArg *mta = arg;
    AppState *app = mta->app;

    char *api_key = config_get("openai_api_key");
    char *model = config_get("openai_model");
    char *text = metadata_get_station_info(mta->title, api_key,
                                           model ? model : DEFAULT_OPENAI_MODEL);

    pthread_mutex_lock(&app->data_lock);
    app->mb_has_info = false;
    free(app->wiki_text);
    app->wiki_text = text ? text : strdup("No further information available.");
    app->mb_loading = false;
    app->wiki_loading = false;
    pthread_mutex_unlock(&app->data_lock);

    free(api_key);
    free(model);
    free(mta);
    return NULL;
}

static void *fetch_lyrics_thread(void *arg)
{
    MetaThreadArg *mta = arg;
    AppState *app = mta->app;
    char *text = metadata_get_lyrics(mta->title, mta->artist);

    pthread_mutex_lock(&app->data_lock);
    free(app->lyrics_text);
    app->lyrics_text = text;
    app->lyrics_loading = false;
    pthread_mutex_unlock(&app->data_lock);

    free(mta);
    return NULL;
}

typedef struct { unsigned char *data; size_t size; } CoverBuf;

static size_t cover_write_cb(void *ptr, size_t sz, size_t nm, void *ud)
{
    CoverBuf *b = ud;
    size_t total = sz * nm;
    unsigned char *tmp = realloc(b->data, b->size + total);
    if (!tmp) return 0;
    b->data = tmp;
    memcpy(b->data + b->size, ptr, total);
    b->size += total;
    return total;
}

static void *fetch_cover_thread(void *arg)
{
    MetaThreadArg *mta = arg;
    AppState *app = mta->app;

    char url[STR_URL * 2];
    if (strncmp(mta->title, "http://", 7) == 0 || strncmp(mta->title, "https://", 8) == 0) {
        safe_strcpy(url, mta->title, sizeof(url));
    } else {
        snprintf(url, sizeof(url), "%s%s", app->active_player->base_url, mta->title);
    }

    CURL *curl = curl_easy_init();
    if (!curl) goto done;

    typedef struct { unsigned char *data; size_t size; } CoverBuf;
    CoverBuf buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cover_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_PLAYER);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    pthread_mutex_lock(&app->data_lock);
    free(app->cover_art_raw);
    if (res == CURLE_OK && buf.data && buf.size > 0) {
        app->cover_art_raw = buf.data;
        app->cover_art_raw_size = buf.size;
        app->cover_art_valid = true;
    } else {
        app->cover_art_raw = NULL;
        app->cover_art_raw_size = 0;
        app->cover_art_valid = false;
        free(buf.data);
    }
    app->cover_art_loading = false;
    pthread_mutex_unlock(&app->data_lock);
    free(mta);
    return NULL;

done:
    pthread_mutex_lock(&app->data_lock);
    app->cover_art_valid = false;
    app->cover_art_loading = false;
    pthread_mutex_unlock(&app->data_lock);
    free(mta);
    return NULL;
}

static void launch_detached(void *(*func)(void *), void *arg)
{
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, func, arg);
    pthread_attr_destroy(&attr);
}

static void check_metadata_update(AppState *app)
{
    if (!app->has_status) return;
    PlayerStatus *ps = &app->player_status;

    /* Cover art */
    const char *img = ps->image;
    if (strcmp(img, app->cover_art_key) != 0) {
        safe_strcpy(app->cover_art_key, img, sizeof(app->cover_art_key));
        app->cover_art_valid = false;
        app->cover_art_loading = true;
        MetaThreadArg *arg = calloc(1, sizeof(MetaThreadArg));
        arg->app = app;
        safe_strcpy(arg->title, img, sizeof(arg->title));
        launch_detached(fetch_cover_thread, arg);
    }

    /* Track/station info */
    char track_key[STR_LONG];
    if (app->is_radio)
        snprintf(track_key, sizeof(track_key), "radio|%s", ps->title1);
    else
        snprintf(track_key, sizeof(track_key), "%s|%s|%s", ps->name, ps->artist, ps->album);

    if (strcmp(track_key, app->wiki_track_key) != 0) {
        safe_strcpy(app->wiki_track_key, track_key, sizeof(app->wiki_track_key));
        app->mb_has_info = false;
        app->mb_loading = true;
        pthread_mutex_lock(&app->data_lock);
        free(app->wiki_text);
        app->wiki_text = NULL;
        pthread_mutex_unlock(&app->data_lock);
        app->wiki_loading = true;

        MetaThreadArg *arg = calloc(1, sizeof(MetaThreadArg));
        arg->app = app;
        safe_strcpy(arg->title, app->is_radio ? ps->title1 : ps->name, sizeof(arg->title));
        safe_strcpy(arg->artist, ps->artist, sizeof(arg->artist));
        safe_strcpy(arg->album, ps->album, sizeof(arg->album));

        if (app->is_radio)
            launch_detached(fetch_station_thread, arg);
        else
            launch_detached(fetch_combined_thread, arg);
    }

    /* Lyrics */
    char lyrics_key[STR_LONG];
    snprintf(lyrics_key, sizeof(lyrics_key), "%s|%s", ps->name, ps->artist);
    if (strcmp(lyrics_key, app->lyrics_track_key) != 0) {
        safe_strcpy(app->lyrics_track_key, lyrics_key, sizeof(app->lyrics_track_key));
        pthread_mutex_lock(&app->data_lock);
        free(app->lyrics_text);
        app->lyrics_text = NULL;
        pthread_mutex_unlock(&app->data_lock);
        app->lyrics_loading = true;
        app->lyrics_scroll = 0;

        if (!app->is_radio) {
            MetaThreadArg *arg = calloc(1, sizeof(MetaThreadArg));
            arg->app = app;
            safe_strcpy(arg->title, ps->name, sizeof(arg->title));
            safe_strcpy(arg->artist, ps->artist, sizeof(arg->artist));
            launch_detached(fetch_lyrics_thread, arg);
        } else {
            app->lyrics_loading = false;
        }
    }
}

/* ── Input Handling ─────────────────────────────────────────────────────── */

static bool try_stored_player(AppState *app)
{
    char *host = config_get("player_host");
    char *name = config_get("player_name");
    if (!host) { free(name); return false; }

    BlusoundPlayer *player = player_create(host, name ? name : host);
    PlayerStatus status;
    if (player_get_status(player, &status)) {
        app->active_player = player;
        app->player_status = status;
        app->has_status = true;
        app->playlist = player_get_playlist(player, &app->playlist_count);
        check_metadata_update(app);
        /* Store in players list */
        app->players = malloc(sizeof(BlusoundPlayer *));
        app->players[0] = player;
        app->players_count = 1;
        app->players_capacity = 1;
        free(host);
        free(name);
        return true;
    }
    player_destroy(player);
    free(host);
    free(name);
    return false;
}

static void handle_playback_keys(int key, WINDOW *win, AppState *app)
{
    PlayerStatus *ps = &app->player_status;
    BlusoundPlayer *p = app->active_player;

    if (key == KEY_UP && p) {
        int nv = int_min(100, ps->volume + VOLUME_INCREMENT);
        if (player_set_volume(p, nv)) {
            ps->volume = nv;
            ui_highlight(app, "volume");
        }
    } else if (key == KEY_DOWN && p) {
        int nv = int_max(0, ps->volume - VOLUME_INCREMENT);
        if (player_set_volume(p, nv)) {
            ps->volume = nv;
            ui_highlight(app, "volume");
        }
    } else if (key == ' ' && p) {
        if (player_toggle_play_pause(p)) {
            ui_highlight(app, "state");
            update_player_status(app);
        }
    } else if (key == KEY_RIGHT && p) {
        if (player_skip(p))
            app->last_update_time = now_sec() - 2;
    } else if (key == KEY_LEFT && p) {
        if (player_back(p))
            app->last_update_time = now_sec() - 2;
    } else if (key == 'm' && p && app->has_status) {
        if (player_toggle_mute(p, ps->mute)) {
            ps->mute = !ps->mute;
            ui_highlight(app, "mute");
        }
    } else if (key == 'r' && p && app->has_status) {
        int new_repeat;
        if (player_cycle_repeat(p, ps->repeat, &new_repeat)) {
            ps->repeat = new_repeat;
            ui_highlight(app, "repeat");
        }
    } else if (key == 'x' && p && app->has_status) {
        if (player_toggle_shuffle(p, ps->shuffle)) {
            ps->shuffle = !ps->shuffle;
            ui_highlight(app, "shuffle");
        }
    } else if (key == 'g' && p && app->has_status && app->playlist_count > 0) {
        /* Go to track */
        int height, width;
        getmaxyx(win, height, width);
        (void)width;
        wmove(win, height - 2, 0);
        wclrtoeol(win);
        char prompt[64];
        snprintf(prompt, sizeof(prompt), "Go to track (1-%d): ", app->playlist_count);
        wattron(win, A_BOLD);
        mvwaddstr(win, height - 2, 2, prompt);
        wattroff(win, A_BOLD);
        echo();
        curs_set(1);
        wtimeout(win, INPUT_BLOCKING);
        char input[16] = "";
        wgetnstr(win, input, TRACK_INPUT_MAX_DIGITS);
        noecho();
        curs_set(0);
        wtimeout(win, CURSES_POLL_MS);
        int num = atoi(input);
        if (num >= 1 && num <= app->playlist_count) {
            if (player_play_queue_track(p, num - 1))
                update_player_status(app);
        }
    }
}

static void handle_navigation_keys(int key, WINDOW *win, AppState *app)
{
    if (key == 'i') {
        app->source_selection_mode = true;
        app->source_depth = 0;
        app->selected_source_index[0] = 0;
        app->current_sources = app->active_player->sources;
        app->current_sources_count = app->active_player->sources_count;
    } else if (key == 's') {
        /* Find searchable sources */
        int count = 0;
        PlayerSource *searchable = malloc(app->active_player->sources_count * sizeof(PlayerSource));
        for (int si = 0; si < SEARCH_SERVICES_COUNT; si++) {
            for (int i = 0; i < app->active_player->sources_count; i++) {
                if (strcmp(app->active_player->sources[i].browse_key, SEARCH_SERVICES[si]) == 0) {
                    searchable[count++] = app->active_player->sources[i];
                }
            }
        }
        if (count > 0) {
            app->search_mode = true;
            safe_strcpy(app->search_phase, "source_select", sizeof(app->search_phase));
            free(app->searchable_sources);
            app->searchable_sources = searchable;
            app->searchable_count = count;
            app->search_source_index = 0;
        } else {
            free(searchable);
            ui_set_message(app, "No searchable sources available");
        }
    } else if (key == 'f') {
        ui_set_message(app, "Loading Qobuz favorites...");
        const char *path[] = {"Qobuz", "Fav", "Album"};
        bool full_match = false;
        int count = 0;
        PlayerSource *items = player_browse_path(app->active_player, path, 3, &full_match, &count);
        if (items && count > 0) {
            app->source_selection_mode = true;
            app->current_sources = items;
            app->current_sources_count = count;
            app->source_depth = 0;
            app->selected_source_index[0] = 0;
            char msg[64];
            snprintf(msg, sizeof(msg), "Found %d favorite albums", count);
            ui_set_message(app, msg);
        } else {
            free(items);
            ui_set_message(app, "Qobuz not found in sources");
        }
    } else if (key == 'w') {
        wmove(win, 0, 0); /* position for input */
        char name[STR_MEDIUM] = "";
        mvwaddstr(win, LINES - 2, 2, "");
        int len = ui_get_input(win, "Save playlist as: ", name, sizeof(name));
        if (len > 0) {
            char msg[STR_MEDIUM];
            player_save_playlist(app->active_player, name, msg, sizeof(msg));
            ui_set_message(app, msg);
        } else {
            ui_set_message(app, "Cancelled");
        }
    } else if (key == 'l') {
        int count = 0;
        PlayerSource *playlists = player_get_playlists(app->active_player, &count);
        if (playlists && count > 0) {
            app->source_selection_mode = true;
            app->current_sources = playlists;
            app->current_sources_count = count;
            app->source_depth = 0;
            app->selected_source_index[0] = 0;
            char msg[64];
            snprintf(msg, sizeof(msg), "Found %d playlists", count);
            ui_set_message(app, msg);
        } else {
            free(playlists);
            ui_set_message(app, "No saved playlists");
        }
    }
}

static void handle_info_keys(int key, WINDOW *win, AppState *app)
{
    if (key == 'c') {
        app->show_cover_art = !app->show_cover_art;
    } else if (key == 't' && !app->is_radio) {
        app->show_lyrics = !app->show_lyrics;
        app->lyrics_scroll = 0;
    } else if (key == KEY_PPAGE && app->show_lyrics) {
        app->lyrics_scroll = int_max(0, app->lyrics_scroll - LYRICS_SCROLL_STEP);
    } else if (key == KEY_NPAGE && app->show_lyrics) {
        app->lyrics_scroll += LYRICS_SCROLL_STEP;
    } else if (key == '+' && app->has_status) {
        if (!app->player_status.albumid[0]) {
            ui_set_message(app, "No album info available");
        } else if (app->player_status.is_favourite) {
            ui_set_message(app, "Already in favourites");
        } else {
            char prompt[STR_MEDIUM];
            snprintf(prompt, sizeof(prompt), "Add '%s' to favourites? (y/n)",
                     app->player_status.album[0] ? app->player_status.album : "this album");
            if (ui_confirm_prompt(win, prompt)) {
                char msg[STR_MEDIUM];
                player_add_album_favourite(app->active_player, &app->player_status, msg, sizeof(msg));
                ui_set_message(app, msg);
                update_player_status(app);
            } else {
                ui_set_message(app, "Cancelled");
            }
        }
    } else if (key == '-' && app->has_status) {
        if (!app->player_status.albumid[0]) {
            ui_set_message(app, "No album info available");
        } else {
            char prompt[STR_MEDIUM];
            snprintf(prompt, sizeof(prompt), "Remove '%s' from favourites? (y/n)",
                     app->player_status.album[0] ? app->player_status.album : "this album");
            if (ui_confirm_prompt(win, prompt)) {
                char msg[STR_MEDIUM];
                player_remove_album_favourite(app->active_player, &app->player_status, msg, sizeof(msg));
                ui_set_message(app, msg);
                update_player_status(app);
            } else {
                ui_set_message(app, "Cancelled");
            }
        }
    } else if (key == '?') {
        app->shortcuts_open = !app->shortcuts_open;
    } else if (key == 'h') {
        ui_show_health_check(win, app);
    } else if (key == 'p') {
        ui_show_pretty_print(win, app);
    }
}

static bool handle_player_control(int key, WINDOW *win, AppState *app)
{
    if (key == 'b') return false;
    if (key == KEY_UP || key == KEY_DOWN || key == ' ' || key == KEY_RIGHT ||
        key == KEY_LEFT || key == 'm' || key == 'r' || key == 'x' || key == 'g') {
        handle_playback_keys(key, win, app);
    } else if (key == 'i' || key == 's' || key == 'f' || key == 'w' || key == 'l') {
        handle_navigation_keys(key, win, app);
    } else if (key == 'c' || key == 't' || key == KEY_PPAGE || key == KEY_NPAGE ||
               key == '+' || key == '-' || key == '?' || key == 'p' || key == 'h') {
        handle_info_keys(key, win, app);
    }
    return true;
}

static bool handle_source_selection(int key, WINDOW *win, AppState *app)
{
    int height, width;
    getmaxyx(win, height, width);
    (void)width;
    int max_display = height - SOURCE_LIST_HEIGHT_OFFSET;

    if (key == 'b' || (key == KEY_LEFT && app->source_depth == 0)) {
        app->source_selection_mode = false;
        app->current_sources = NULL;
        app->current_sources_count = 0;
        app->source_depth = 0;
        app->source_sort = 'o';
        return false;
    }

    int *sel = &app->selected_source_index[app->source_depth];

    if (key == KEY_UP && *sel > 0) {
        (*sel)--;
    } else if (key == KEY_DOWN && *sel < app->current_sources_count - 1) {
        (*sel)++;
    } else if (key == 'n') {
        int next = ((*sel / max_display) + 1) * max_display;
        *sel = int_min(next, app->current_sources_count - 1);
    } else if (key == 'p') {
        int prev = ((*sel / max_display) - 1) * max_display;
        *sel = int_max(prev, 0);
    } else if (key == KEY_RIGHT || key == 10) {
        PlayerSource *src = &app->current_sources[*sel];
        if (key == 10 && src->play_url[0]) {
            player_select_input(app->active_player, src);
            update_player_status(app);
            app->source_selection_mode = false;
            return false;
        }
        if (src->browse_key[0]) {
            player_get_nested(app->active_player, src);
            if (src->children && src->children_count > 0) {
                app->current_sources = src->children;
                app->current_sources_count = src->children_count;
                app->source_depth++;
                app->selected_source_index[app->source_depth] = 0;
            }
        }
    } else if (key == KEY_LEFT && app->source_depth > 0) {
        app->source_depth--;
        /* Navigate back up — re-resolve current sources from player */
        PlayerSource *sources = app->active_player->sources;
        int count = app->active_player->sources_count;
        for (int d = 0; d < app->source_depth; d++) {
            int idx = app->selected_source_index[d];
            if (idx < count && sources[idx].children) {
                sources = sources[idx].children;
                count = sources[idx].children_count;
            }
        }
        app->current_sources = sources;
        app->current_sources_count = count;
    } else if (key == '+' && app->current_sources_count > 0) {
        PlayerSource *src = &app->current_sources[*sel];
        if (src->context_menu_key[0] && !src->is_favourite) {
            char label[STR_LONG];
            if (src->text2[0]) snprintf(label, sizeof(label), "%s - %s", src->text, src->text2);
            else safe_strcpy(label, src->text, sizeof(label));
            char prompt[STR_LONG];
            snprintf(prompt, sizeof(prompt), "Add '%s' to favourites? (y/n)", label);
            if (ui_confirm_prompt(win, prompt)) {
                char msg[STR_MEDIUM];
                player_toggle_favourite(app->active_player, src, true, msg, sizeof(msg));
                ui_set_message(app, msg);
            }
        }
    } else if (key == '-' && app->current_sources_count > 0) {
        PlayerSource *src = &app->current_sources[*sel];
        if (src->context_menu_key[0]) {
            char label[STR_LONG];
            if (src->text2[0]) snprintf(label, sizeof(label), "%s - %s", src->text, src->text2);
            else safe_strcpy(label, src->text, sizeof(label));
            char prompt[STR_LONG];
            snprintf(prompt, sizeof(prompt), "Remove '%s' from favourites? (y/n)", label);
            if (ui_confirm_prompt(win, prompt)) {
                char msg[STR_MEDIUM];
                player_toggle_favourite(app->active_player, src, false, msg, sizeof(msg));
                ui_set_message(app, msg);
            }
        }
    } else if (key == 't' || key == 'a' || key == 'o') {
        /* Sort not yet implemented for C arrays — would need qsort */
        app->source_sort = (char)key;
        *sel = 0;
    } else if (key == '/') {
        char filter[STR_MEDIUM] = "";
        mvwaddstr(win, height - 2, 2, "");
        int len = ui_get_input(win, "Filter: ", filter, sizeof(filter));
        if (len > 0) {
            ui_set_message(app, "Filter applied");
        }
    }
    return true;
}

static bool handle_search(int key, WINDOW *win, AppState *app)
{
    if (strcmp(app->search_phase, "source_select") == 0) {
        if (key == KEY_UP && app->search_source_index > 0)
            app->search_source_index--;
        else if (key == KEY_DOWN && app->search_source_index < app->searchable_count - 1)
            app->search_source_index++;
        else if (key == 10 && app->searchable_count > 0) {
            PlayerSource *sel = &app->searchable_sources[app->search_source_index];
            int nested_count = 0;
            PlayerSource *nested = player_browse(app->active_player, sel->browse_key, &nested_count);
            const char *sk = (nested && nested_count > 0) ? nested[0].search_key : NULL;
            if (sk && sk[0]) {
                safe_strcpy(app->active_search_key, sk, sizeof(app->active_search_key));
                safe_strcpy(app->search_source_name, sel->text, sizeof(app->search_source_name));
                safe_strcpy(app->search_phase, "input", sizeof(app->search_phase));
            } else {
                ui_set_message(app, "Source doesn't support search");
                free(nested);
                return false;
            }
            free(nested);
        } else if (key == 'b') {
            return false;
        }
        return true;
    }

    if (strcmp(app->search_phase, "input") == 0) {
        char term[STR_MEDIUM] = "";
        wmove(win, 3, 2);
        char prompt[STR_MEDIUM];
        if (app->search_source_name[0])
            snprintf(prompt, sizeof(prompt), "Search %s: ", app->search_source_name);
        else
            safe_strcpy(prompt, "Search: ", sizeof(prompt));
        int len = ui_get_input(win, prompt, term, sizeof(term));
        if (len > 0) {
            player_sources_free(app->search_results, app->search_results_count);
            app->search_results = player_search(app->active_player,
                                                 app->active_search_key, term,
                                                 &app->search_results_count);
            app->search_selected_index = 0;
            if (app->search_results_count > 0) {
                safe_strcpy(app->search_phase, "results", sizeof(app->search_phase));
                return true;
            }
            ui_set_message(app, "No results found");
        }
        return false;
    }

    if (strcmp(app->search_phase, "results") == 0) {
        if (key == KEY_UP && app->search_selected_index > 0)
            app->search_selected_index--;
        else if (key == KEY_DOWN && app->search_selected_index < app->search_results_count - 1)
            app->search_selected_index++;
        else if (key == KEY_RIGHT && app->search_results_count > 0) {
            PlayerSource *sel = &app->search_results[app->search_selected_index];
            if (sel->browse_key[0]) {
                player_get_nested(app->active_player, sel);
                if (sel->children && sel->children_count > 0) {
                    /* Push to history */
                    if (app->search_history_count < 32) {
                        app->search_history[app->search_history_count].results = app->search_results;
                        app->search_history[app->search_history_count].results_count = app->search_results_count;
                        app->search_history[app->search_history_count].selected_index = app->search_selected_index;
                        app->search_history_count++;
                    }
                    app->search_results = sel->children;
                    app->search_results_count = sel->children_count;
                    app->search_selected_index = 0;
                    /* Detach children from parent so they aren't double-freed */
                    sel->children = NULL;
                    sel->children_count = 0;
                }
            }
        } else if (key == 10 && app->search_results_count > 0) {
            PlayerSource *sel = &app->search_results[app->search_selected_index];
            /* TuneIn: play directly */
            if (sel->play_url[0] || strcmp(app->search_source_name, "TuneIn") == 0) {
                if (player_select_input(app->active_player, sel)) {
                    update_player_status(app);
                    return false;
                }
            } else if (sel->browse_key[0]) {
                player_get_nested(app->active_player, sel);
                if (sel->children && sel->children_count > 0) {
                    if (app->search_history_count < 32) {
                        app->search_history[app->search_history_count].results = app->search_results;
                        app->search_history[app->search_history_count].results_count = app->search_results_count;
                        app->search_history[app->search_history_count].selected_index = app->search_selected_index;
                        app->search_history_count++;
                    }
                    app->search_results = sel->children;
                    app->search_results_count = sel->children_count;
                    app->search_selected_index = 0;
                    sel->children = NULL;
                    sel->children_count = 0;
                }
            }
        } else if (key == 'b' || key == KEY_LEFT) {
            if (app->search_history_count > 0) {
                app->search_history_count--;
                SearchHistoryEntry *prev = &app->search_history[app->search_history_count];
                player_sources_free(app->search_results, app->search_results_count);
                app->search_results = prev->results;
                app->search_results_count = prev->results_count;
                app->search_selected_index = prev->selected_index;
            } else {
                return false;
            }
        }
        return true;
    }
    return false;
}

/* ── Main View Drawing ──────────────────────────────────────────────────── */

static void draw_current_view(WINDOW *win, AppState *app, bool player_mode,
                              bool discovery_started)
{
    const char *view;
    if (!player_mode) view = "Player Selection";
    else if (app->search_mode) view = "Search";
    else if (app->source_selection_mode) view = "Source Selection";
    else view = "BluOS Player Control";

    ui_draw_header(win, app, view);

    if (!player_mode) {
        ui_display_player_selection(win, app);
    } else if (app->search_mode) {
        if (strcmp(app->search_phase, "source_select") == 0)
            ui_display_search_source_selection(win, app);
        else if (strcmp(app->search_phase, "results") == 0)
            ui_display_search_results(win, app);
    } else if (app->source_selection_mode) {
        ui_display_source_selection(win, app);
    } else {
        ui_display_player_control(win, app);
    }
}

/* ── Progress Tick ──────────────────────────────────────────────────────── */

static void tick_progress(AppState *app)
{
    double now = now_sec();
    if (app->has_status &&
        (strcmp(app->player_status.state, "stream") == 0 ||
         strcmp(app->player_status.state, "play") == 0)) {
        if (now - app->last_progress_time >= PROGRESS_INCREMENT_INTERVAL) {
            app->player_status.secs++;
            if (app->player_status.totlen > 0)
                app->player_status.secs = int_min(app->player_status.secs, app->player_status.totlen);
            app->last_progress_time = now;
        }
    }
    if (app->active_player && now - app->last_update_time >= STATUS_POLL_INTERVAL) {
        update_player_status(app);
        app->last_update_time = now;
        app->last_progress_time = now;
    }
}

/* ── Signal Handler ─────────────────────────────────────────────────────── */

static void signal_handler(int sig)
{
    (void)sig;
    endwin();
    exit(0);
}

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    setlocale(LC_ALL, "");
    signal(SIGINT, signal_handler);

    /* Ensure logs directory exists */
    mkdir("logs", 0755);

    curl_global_init(CURL_GLOBAL_ALL);
    player_init();
    metadata_init();
    main_logger = logger_create("logs/bluxir.log", LOG_MAX_BYTES, LOG_BACKUP_COUNT);

    AppState app;
    app_state_init(&app);

    /* Set terminal title */
    printf("%s", TERMINAL_TITLE);
    fflush(stdout);

    /* Initialize curses */
    WINDOW *stdscr_ptr = initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr_ptr, TRUE);
    start_color();
    use_default_colors();
    init_pair(2, COLOR_BLACK, COLOR_WHITE);  /* Selection highlight */
    init_pair(3, COLOR_GREEN, -1);           /* Green highlight */
    init_pair(4, COLOR_RED, -1);             /* Red warning */

    bool player_mode = try_stored_player(&app);
    DiscoveryState *discovery = NULL;

    if (!player_mode) {
        discovery = discover_start();
        mvwaddstr(stdscr_ptr, 3, 2, DISCOVERY_HINT);
        wrefresh(stdscr_ptr);
    }

    /* Main loop */
    while (1) {
        /* Handle terminal resize */
        int cur_h, cur_w;
        getmaxyx(stdscr_ptr, cur_h, cur_w);
        if (is_term_resized(cur_h, cur_w)) {
            int new_h, new_w;
            getmaxyx(stdscr_ptr, new_h, new_w);
            resizeterm(new_h, new_w);
            wclear(stdscr_ptr);
        }

        werase(stdscr_ptr);

        /* Update discovery results */
        if (discovery && !player_mode) {
            BlusoundPlayer *disc_players[16];
            int n = discover_get_players(discovery, disc_players, 16);
            if (n != app.players_count) {
                app.players = realloc(app.players, n * sizeof(BlusoundPlayer *));
                memcpy(app.players, disc_players, n * sizeof(BlusoundPlayer *));
                app.players_count = n;
            }
        }

        draw_current_view(stdscr_ptr, &app, player_mode, discovery != NULL);
        wrefresh(stdscr_ptr);
        wtimeout(stdscr_ptr, CURSES_POLL_MS);
        int key = wgetch(stdscr_ptr);

        if (key == 'q') {
            if (ui_confirm_prompt(stdscr_ptr, "Quit bluxir? (y/n)"))
                break;
            continue;
        }

        /* Input dispatch */
        if (!player_mode) {
            if (app.selector_shortcuts_open) {
                if (key != -1) app.selector_shortcuts_open = false;
            } else if (key == '?') {
                app.selector_shortcuts_open = !app.selector_shortcuts_open;
            } else if (key == KEY_UP && app.selected_index > 0) {
                app.selected_index--;
            } else if (key == KEY_DOWN && app.selected_index < app.players_count - 1) {
                app.selected_index++;
            } else if (key == 10 && app.players_count > 0) {
                app.active_player = app.players[app.selected_index];
                PlayerStatus status;
                if (player_get_status(app.active_player, &status)) {
                    app.player_status = status;
                    app.has_status = true;
                    app.playlist = player_get_playlist(app.active_player, &app.playlist_count);
                    config_set("player_host", app.active_player->host_name);
                    config_set("player_name", app.active_player->name);
                    player_mode = true;
                    update_player_status(&app);
                }
            }
        } else {
            if (app.shortcuts_open) {
                if (key != -1) app.shortcuts_open = false;
            } else if (app.search_mode) {
                app.search_mode = handle_search(key, stdscr_ptr, &app);
            } else if (app.source_selection_mode) {
                app.source_selection_mode = handle_source_selection(key, stdscr_ptr, &app);
            } else {
                player_mode = handle_player_control(key, stdscr_ptr, &app);
            }
        }

        tick_progress(&app);
    }

    endwin();
    if (discovery) discover_stop(discovery);
    app_state_destroy(&app);
    metadata_cleanup();
    logger_destroy(player_logger);
    logger_destroy(main_logger);
    curl_global_cleanup();
    return 0;
}
