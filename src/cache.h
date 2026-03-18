/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stddef.h>

/* Opaque LRU cache type */
typedef struct LRUCache LRUCache;

/* Create a new thread-safe LRU cache with the given max capacity */
LRUCache *cache_create(int capacity);

/* Destroy cache and free all entries */
void cache_destroy(LRUCache *cache);

/* Get a cached value. Returns a malloc'd copy or NULL if not found.
   If found, entry is moved to front (most recent). */
char *cache_get(LRUCache *cache, const char *key);

/* Check if key exists in cache (also returns true for NULL-valued entries) */
bool cache_has(LRUCache *cache, const char *key);

/* Set a value in cache. value may be NULL. Key and value are copied. */
void cache_set(LRUCache *cache, const char *key, const char *value);

#endif /* CACHE_H */
