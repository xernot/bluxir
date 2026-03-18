/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "config.h"
#include "../lib/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

/* Project config path (in working directory) */
#define PROJECT_CONFIG "config.json"

/* Private config file name (in home directory) */
#define PRIVATE_CONFIG_NAME ".bluxir.json"

/* Keys that belong in the private config */
static bool is_private_key(const char *key)
{
    return strcmp(key, "openai_api_key") == 0;
}

static char *get_home_dir(void)
{
    const char *home = getenv("HOME");
    if (home) return strdup(home);
    struct passwd *pw = getpwuid(getuid());
    if (pw) return strdup(pw->pw_dir);
    return NULL;
}

static char *get_private_path(void)
{
    char *home = get_home_dir();
    if (!home) return NULL;
    size_t len = strlen(home) + 1 + strlen(PRIVATE_CONFIG_NAME) + 1;
    char *path = malloc(len);
    snprintf(path, len, "%s/%s", home, PRIVATE_CONFIG_NAME);
    free(home);
    return path;
}

static cJSON *load_json_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return cJSON_CreateObject();
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return cJSON_CreateObject();
    }
    char *buf = malloc(size + 1);
    size_t nread = fread(buf, 1, size, f);
    buf[nread] = '\0';
    fclose(f);
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    return json ? json : cJSON_CreateObject();
}

static bool save_json_file(const char *path, cJSON *json)
{
    char *str = cJSON_Print(json);
    if (!str) return false;
    FILE *f = fopen(path, "w");
    if (!f) {
        free(str);
        return false;
    }
    fputs(str, f);
    fclose(f);
    free(str);
    return true;
}

char *config_get(const char *key)
{
    const char *path;
    char *private_path = NULL;

    if (is_private_key(key)) {
        private_path = get_private_path();
        if (!private_path) return NULL;
        path = private_path;
    } else {
        path = PROJECT_CONFIG;
    }

    cJSON *root = load_json_file(path);
    free(private_path);
    if (!root) return NULL;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    char *result = NULL;
    if (cJSON_IsString(item) && item->valuestring) {
        result = strdup(item->valuestring);
    }
    cJSON_Delete(root);
    return result;
}

bool config_set(const char *key, const char *value)
{
    char *private_path = NULL;
    const char *path;

    if (is_private_key(key)) {
        private_path = get_private_path();
        if (!private_path) return false;
        path = private_path;
    } else {
        path = PROJECT_CONFIG;
    }

    cJSON *root = load_json_file(path);
    cJSON_DeleteItemFromObjectCaseSensitive(root, key);
    cJSON_AddStringToObject(root, key, value);
    bool ok = save_json_file(path, root);
    cJSON_Delete(root);
    free(private_path);
    return ok;
}
