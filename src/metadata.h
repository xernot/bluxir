/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef METADATA_H
#define METADATA_H

#include "types.h"
#include "logger.h"
#include <stdbool.h>

/* Initialize metadata module (call once) */
void metadata_init(void);

/* Destroy metadata module and free cache */
void metadata_cleanup(void);

/* Get combined album+track info from OpenAI (with MusicBrainz fallback).
   Result is stored in *out. Returns true on success. */
bool metadata_get_combined(const char *title, const char *artist, const char *album,
                           const char *api_key, const char *system_prompt,
                           const char *model, CombinedInfo *out);

/* Get station info from OpenAI. Caller must free() result. Returns NULL on failure. */
char *metadata_get_station_info(const char *station_name, const char *api_key,
                                const char *model);

/* Get lyrics from LRCLIB. Caller must free() result. Returns NULL if not found. */
char *metadata_get_lyrics(const char *title, const char *artist);

/* Get Wikipedia summary. Caller must free() result. Returns NULL if not found. */
char *metadata_get_wiki(const char *title);

#endif /* METADATA_H */
