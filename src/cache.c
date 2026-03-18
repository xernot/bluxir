/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "cache.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Hash table bucket count — must be power of 2 */
#define HASH_BUCKETS 256

typedef struct CacheNode {
  char *key;
  char *value;    /* may be NULL (cache negative results) */
  bool has_value; /* true even when value is NULL */
  struct CacheNode *prev;
  struct CacheNode *next;
  struct CacheNode *hash_next;
} CacheNode;

struct LRUCache {
  CacheNode *head; /* most recently used */
  CacheNode *tail; /* least recently used */
  CacheNode *buckets[HASH_BUCKETS];
  int count;
  int capacity;
  pthread_mutex_t lock;
};

static unsigned int hash_str(const char *s) {
  unsigned int h = 5381;
  while (*s) {
    h = ((h << 5) + h) + (unsigned char)*s;
    s++;
  }
  return h & (HASH_BUCKETS - 1);
}

static CacheNode *find_node(LRUCache *cache, const char *key,
                            unsigned int bucket) {
  CacheNode *n = cache->buckets[bucket];
  while (n) {
    if (strcmp(n->key, key) == 0)
      return n;
    n = n->hash_next;
  }
  return NULL;
}

static void detach_node(LRUCache *cache, CacheNode *node) {
  if (node->prev)
    node->prev->next = node->next;
  else
    cache->head = node->next;
  if (node->next)
    node->next->prev = node->prev;
  else
    cache->tail = node->prev;
  node->prev = node->next = NULL;
}

static void push_front(LRUCache *cache, CacheNode *node) {
  node->prev = NULL;
  node->next = cache->head;
  if (cache->head)
    cache->head->prev = node;
  cache->head = node;
  if (!cache->tail)
    cache->tail = node;
}

static void remove_from_hash(LRUCache *cache, CacheNode *node) {
  unsigned int b = hash_str(node->key);
  CacheNode **pp = &cache->buckets[b];
  while (*pp) {
    if (*pp == node) {
      *pp = node->hash_next;
      return;
    }
    pp = &(*pp)->hash_next;
  }
}

static void free_node(CacheNode *node) {
  free(node->key);
  free(node->value);
  free(node);
}

static void evict_lru(LRUCache *cache) {
  CacheNode *victim = cache->tail;
  if (!victim)
    return;
  detach_node(cache, victim);
  remove_from_hash(cache, victim);
  free_node(victim);
  cache->count--;
}

LRUCache *cache_create(int capacity) {
  LRUCache *cache = calloc(1, sizeof(LRUCache));
  if (!cache)
    return NULL;
  cache->capacity = capacity;
  pthread_mutex_init(&cache->lock, NULL);
  return cache;
}

void cache_destroy(LRUCache *cache) {
  if (!cache)
    return;
  CacheNode *n = cache->head;
  while (n) {
    CacheNode *next = n->next;
    free_node(n);
    n = next;
  }
  pthread_mutex_destroy(&cache->lock);
  free(cache);
}

char *cache_get(LRUCache *cache, const char *key) {
  pthread_mutex_lock(&cache->lock);
  unsigned int b = hash_str(key);
  CacheNode *node = find_node(cache, key, b);
  char *result = NULL;
  if (node) {
    detach_node(cache, node);
    push_front(cache, node);
    if (node->value)
      result = strdup(node->value);
  }
  pthread_mutex_unlock(&cache->lock);
  return result;
}

bool cache_has(LRUCache *cache, const char *key) {
  pthread_mutex_lock(&cache->lock);
  unsigned int b = hash_str(key);
  CacheNode *node = find_node(cache, key, b);
  bool found = (node != NULL);
  pthread_mutex_unlock(&cache->lock);
  return found;
}

void cache_set(LRUCache *cache, const char *key, const char *value) {
  pthread_mutex_lock(&cache->lock);
  unsigned int b = hash_str(key);
  CacheNode *existing = find_node(cache, key, b);

  if (existing) {
    free(existing->value);
    existing->value = value ? strdup(value) : NULL;
    existing->has_value = true;
    detach_node(cache, existing);
    push_front(cache, existing);
    pthread_mutex_unlock(&cache->lock);
    return;
  }

  if (cache->count >= cache->capacity) {
    evict_lru(cache);
  }

  CacheNode *node = calloc(1, sizeof(CacheNode));
  if (!node) {
    pthread_mutex_unlock(&cache->lock);
    return;
  }
  node->key = strdup(key);
  if (!node->key) {
    free(node);
    pthread_mutex_unlock(&cache->lock);
    return;
  }
  node->value = value ? strdup(value) : NULL;
  node->has_value = true;
  node->hash_next = cache->buckets[b];
  cache->buckets[b] = node;
  push_front(cache, node);
  cache->count++;
  pthread_mutex_unlock(&cache->lock);
}
