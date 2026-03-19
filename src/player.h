/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef PLAYER_H
#define PLAYER_H

#include "logger.h"
#include "types.h"
#include <stdbool.h>

/* Global logger for player module (set in player_init) */
extern Logger *player_logger;

/* Initialize player module (call once, after curl_global_init) */
void player_init(void);

/* Create a BlusoundPlayer. Fetches name and initializes sources. */
BlusoundPlayer *player_create(const char *host, const char *name);

/* Fetch and update player name from SyncStatus */
void player_fetch_sync_name(BlusoundPlayer *p);

/* Initialize player sources (call once when player becomes active) */
void player_init_sources(BlusoundPlayer *p);

/* Free a BlusoundPlayer and all owned memory */
void player_destroy(BlusoundPlayer *p);

/* Fetch current player status. Returns true on success. */
bool player_get_status(BlusoundPlayer *p, PlayerStatus *out);

/* Get player identity info from SyncStatus API */
bool player_get_sync_info(BlusoundPlayer *p, KVPair *out, int *count, int max);

/* Scrape diagnostics from web interface */
bool player_get_diagnostics(BlusoundPlayer *p, KVPair *out, int *count,
                            int max);

/* Check firmware upgrade status */
bool player_get_upgrade_status(BlusoundPlayer *p, char *out, size_t out_size);

/* Get web interface URL */
void player_get_web_url(BlusoundPlayer *p, char *out, size_t out_size);

/* Set volume (0-100) */
bool player_set_volume(BlusoundPlayer *p, int volume);

/* Toggle mute */
bool player_toggle_mute(BlusoundPlayer *p, bool current_mute);

/* Toggle shuffle (off→on forces reshuffle) */
bool player_toggle_shuffle(BlusoundPlayer *p, bool current_shuffle);

/* Cycle repeat: 2→0→1→2 */
bool player_cycle_repeat(BlusoundPlayer *p, int current_repeat,
                         int *new_repeat);

/* Toggle play/pause */
bool player_toggle_play_pause(BlusoundPlayer *p);

/* Skip to next track */
bool player_skip(BlusoundPlayer *p);

/* Go back to previous track */
bool player_back(BlusoundPlayer *p);

/* Play a specific queue track by index */
bool player_play_queue_track(BlusoundPlayer *p, int index);

/* Select an input source */
bool player_select_input(BlusoundPlayer *p, PlayerSource *source);

/* Browse sources at a given key (NULL = root). Caller must free result. */
PlayerSource *player_browse(BlusoundPlayer *p, const char *key, int *count);

/* Browse all pages (follows nextKey pagination). Caller must free result. */
PlayerSource *player_browse_all(BlusoundPlayer *p, const char *key, int *count);

/* Fetch nested sources into source->children */
void player_get_nested(BlusoundPlayer *p, PlayerSource *source);

/* Search within a source. Caller must free result. */
PlayerSource *player_search(BlusoundPlayer *p, const char *search_key,
                            const char *query, int *count);

/* Get current playlist. Caller must free result. */
PlaylistEntry *player_get_playlist(BlusoundPlayer *p, int *count);

/* Save current queue as playlist */
bool player_save_playlist(BlusoundPlayer *p, const char *name, char *msg,
                          size_t msg_size);

/* Get saved playlists. Caller must free result. */
PlayerSource *player_get_playlists(BlusoundPlayer *p, int *count);

/* Delete a saved playlist */
bool player_delete_playlist(BlusoundPlayer *p, const char *name);

/* Browse path: navigate hierarchy by name matching. Caller must free result. */
PlayerSource *player_browse_path(BlusoundPlayer *p, const char **names,
                                 int name_count, bool *full_match, int *count);

/* Add currently playing album to favourites */
bool player_add_album_favourite(BlusoundPlayer *p, PlayerStatus *status,
                                char *msg, size_t msg_size);

/* Remove currently playing album from favourites */
bool player_remove_album_favourite(BlusoundPlayer *p, PlayerStatus *status,
                                   char *msg, size_t msg_size);

/* Toggle favourite via context menu */
bool player_toggle_favourite(BlusoundPlayer *p, PlayerSource *source, bool add,
                             char *msg, size_t msg_size);

/* Get queue actions from context menu. Returns action URLs. */
typedef struct {
  char play_now[STR_URL];
  char add_next[STR_URL];
  char add_last[STR_URL];
} QueueActions;
bool player_get_queue_actions(BlusoundPlayer *p, PlayerSource *source,
                              QueueActions *out);

/* Get group info (master/slave IPs) from SyncStatus */
bool player_get_group_info(BlusoundPlayer *p, GroupInfo *out);

/* Add a player as slave to this player's group */
bool player_add_slave(BlusoundPlayer *p, const char *slave_ip);

/* Remove a slave from this player's group */
bool player_remove_slave(BlusoundPlayer *p, const char *slave_ip);

/* Leave the current group (sends RemoveSlave to the master) */
bool player_leave_group(BlusoundPlayer *p);

/* Free a dynamically allocated PlayerSource array */
void player_sources_free(PlayerSource *sources, int count);

#endif /* PLAYER_H */
