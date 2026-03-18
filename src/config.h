/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/* Get a preference value. Returns NULL if not found. Caller must free() result. */
char *config_get(const char *key);

/* Set a preference value. Private keys go to ~/.bluxir.json, others to config.json */
bool config_set(const char *key, const char *value);

#endif /* CONFIG_H */
