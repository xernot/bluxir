/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef TYPES_H
#define TYPES_H

#include "constants.h"
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

/* ── Player Status ──────────────────────────────────────────────────────── */

typedef struct {
  char etag[STR_SHORT];
  char album[STR_MEDIUM];
  char artist[STR_MEDIUM];
  char name[STR_MEDIUM]; /* title1 / track name */
  char state[STR_SHORT]; /* play, pause, stop, stream */
  int volume;
  char service[STR_MEDIUM];
  char input_id[STR_SHORT];
  bool can_move_playback;
  bool can_seek;
  int cursor;
  double db;
  char fn[STR_URL];
  char image[STR_URL];
  int indexing;
  int mid;
  int mode;
  bool mute;
  int pid;
  int prid;
  char quality[STR_MEDIUM];
  int repeat; /* 0=queue, 1=track, 2=off */
  char service_icon[STR_URL];
  char service_name[STR_MEDIUM];
  bool shuffle;
  int sid;
  char sleep_str[STR_SHORT];
  int song; /* current track index (0-based) */
  char stream_format[STR_MEDIUM];
  int sync_stat;
  char title1[STR_MEDIUM];
  char title2[STR_MEDIUM];
  char title3[STR_MEDIUM];
  int totlen; /* total length in seconds */
  int secs;   /* current position in seconds */
  char albumid[STR_MEDIUM];
  char artistid[STR_MEDIUM];
  char composer[STR_MEDIUM];
  bool is_favourite;
} PlayerStatus;

/* ── Player Source ──────────────────────────────────────────────────────── */

typedef struct PlayerSource {
  char text[STR_MEDIUM];
  char text2[STR_MEDIUM];
  char image[STR_URL];
  char browse_key[STR_URL];
  char play_url[STR_URL];
  char input_type[STR_SHORT];
  char type[STR_SHORT];
  char context_menu_key[STR_URL];
  bool is_favourite;
  char search_key[STR_URL];

  struct PlayerSource *children;
  int children_count;
  int children_capacity;
} PlayerSource;

/* ── Playlist Entry ─────────────────────────────────────────────────────── */

typedef struct {
  char title[STR_MEDIUM];
  char artist[STR_MEDIUM];
  char album[STR_MEDIUM];
} PlaylistEntry;

/* ── Key-Value Pair ─────────────────────────────────────────────────────── */

typedef struct {
  char key[STR_MEDIUM];
  char value[STR_LONG];
} KVPair;

/* ── Browse Path Cache Entry ────────────────────────────────────────────── */

typedef struct BrowseCacheEntry {
  char path_key[STR_URL];
  char browse_key[STR_URL];
  struct BrowseCacheEntry *next;
} BrowseCacheEntry;

/* ── Blusound Player ────────────────────────────────────────────────────── */

typedef struct {
  char host_name[STR_MEDIUM];
  char name[STR_MEDIUM];
  char base_url[STR_URL];

  PlayerSource *sources;
  int sources_count;
  int sources_capacity;

  BrowseCacheEntry *browse_cache;
} BlusoundPlayer;

/* ── Group Info ──────────────────────────────────────────────────────────── */

typedef struct {
  char master_ip[STR_MEDIUM];       /* IP of master (empty = standalone) */
  char slave_ips[16][STR_MEDIUM];   /* IPs of grouped slaves */
  char slave_names[16][STR_MEDIUM]; /* names of grouped slaves */
  int slave_count;
} GroupInfo;

/* ── Combined Info (from AI or MusicBrainz) ─────────────────────────────── */

typedef struct {
  char year[STR_SHORT];
  char label[STR_MEDIUM];
  char genre[STR_MEDIUM];
  char track_info[STR_TEXT];
  bool has_track_info;
} CombinedInfo;

/* ── Search History Entry ───────────────────────────────────────────────── */

typedef struct {
  PlayerSource *results;
  int results_count;
  int selected_index;
} SearchHistoryEntry;

/* ── Highlight Times ────────────────────────────────────────────────────── */

typedef struct {
  double volume;
  double mute;
  double repeat;
  double shuffle;
  double state;
  double health;
} HighlightTimes;

/* ── Application State ──────────────────────────────────────────────────── */

typedef struct {
  /* Header */
  char header_message[STR_MEDIUM];
  double header_message_time;

  /* UI flags */
  bool shortcuts_open;
  bool selector_shortcuts_open;
  bool source_selection_mode;
  bool search_mode;
  bool show_cover_art;
  bool show_lyrics;

  /* Player selection */
  BlusoundPlayer **players;
  int players_count;
  int players_capacity;
  int selected_index;
  BlusoundPlayer *active_player;
  PlayerStatus player_status;
  bool has_status;

  /* Source selection */
  int selected_source_index[32]; /* navigation depth stack */
  int source_depth;
  PlayerSource *current_sources;
  int current_sources_count;
  char source_sort;               /* 'o'=original, 't'=title, 'a'=artist */
  char source_filter[STR_MEDIUM]; /* active filter text (empty = none) */
  PlayerSource
      *unsorted_sources; /* backup of original data before sort/filter */
  int unsorted_sources_count;

  /* Playlist */
  PlaylistEntry *playlist;
  int playlist_count;
  int playlist_capacity;
  int playlist_scroll; /* scroll offset for j/k navigation */

  /* Search */
  char search_phase[STR_SHORT]; /* "source_select", "input", "results" */
  PlayerSource *searchable_sources;
  int searchable_count;
  int search_source_index;
  PlayerSource *search_results;
  int search_results_count;
  int search_selected_index;
  char active_search_key[STR_URL];
  char search_source_name[STR_MEDIUM];
  SearchHistoryEntry search_history[32];
  int search_history_count;

  /* Metadata */
  CombinedInfo mb_info;
  bool mb_has_info;
  bool mb_loading;
  char *wiki_text;
  char wiki_track_key[STR_LONG];
  bool wiki_loading;

  /* Radio state */
  bool is_radio;
  char radio_title2[STR_MEDIUM];
  char radio_title3[STR_MEDIUM];

  /* Cover art */
  unsigned char *cover_art_raw;
  size_t cover_art_raw_size;
  bool cover_art_valid;
  char cover_art_key[STR_URL];
  bool cover_art_loading;

  /* Lyrics */
  char *lyrics_text;
  char lyrics_track_key[STR_LONG];
  bool lyrics_loading;
  int lyrics_scroll;

  /* Timing */
  double last_update_time;
  double last_progress_time;

  /* Group */
  GroupInfo group_info;
  double last_group_update_time;

  /* Threading */
  pthread_mutex_t data_lock;
  HighlightTimes highlight_times;
} AppState;

#endif /* TYPES_H */
