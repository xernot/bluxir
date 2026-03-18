/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef DISCOVER_H
#define DISCOVER_H

#include "types.h"
#include <stdbool.h>
#include <pthread.h>

/* Discovery state — shared between main thread and discovery thread */
typedef struct {
    BlusoundPlayer **players;
    int count;
    int capacity;
    pthread_mutex_t lock;
    bool running;
} DiscoveryState;

/* Start mDNS discovery in a background thread.
   Returns a DiscoveryState that the main thread can poll. */
DiscoveryState *discover_start(void);

/* Stop discovery and free resources */
void discover_stop(DiscoveryState *state);

/* Get current player count (thread-safe) */
int discover_player_count(DiscoveryState *state);

/* Copy current player pointers into dst array (thread-safe).
   Returns number of players copied. */
int discover_get_players(DiscoveryState *state, BlusoundPlayer **dst, int max);

#endif /* DISCOVER_H */
