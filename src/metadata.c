/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "metadata.h"
#include "cache.h"
#include "util.h"
#include "constants.h"
#include "../lib/cJSON.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>

static Logger *meta_logger = NULL;
static LRUCache *meta_cache = NULL;
static double last_request_time = 0;
static pthread_mutex_t rate_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── HTTP helpers ───────────────────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t size;
} HttpBuffer;

static size_t meta_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpBuffer *buf = userdata;
    size_t total = size * nmemb;
    char *tmp = realloc(buf->data, buf->size + total + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static void rate_limit(void)
{
    pthread_mutex_lock(&rate_lock);
    double now = now_sec();
    double elapsed = now - last_request_time;
    if (elapsed < MUSICBRAINZ_RATE_LIMIT) {
        int ms = (int)((MUSICBRAINZ_RATE_LIMIT - elapsed) * 1000000);
        usleep(ms);
    }
    last_request_time = now_sec();
    pthread_mutex_unlock(&rate_lock);
}

static bool meta_http_get(const char *url, long timeout, const char *ua, HttpBuffer *out)
{
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    out->data = NULL;
    out->size = 0;

    struct curl_slist *headers = NULL;
    char ua_header[STR_MEDIUM];
    snprintf(ua_header, sizeof(ua_header), "User-Agent: %s", ua);
    headers = curl_slist_append(headers, ua_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, meta_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code >= 400) {
        free(out->data);
        out->data = NULL;
        out->size = 0;
        return false;
    }
    return true;
}

static bool openai_post(const char *api_key, const char *model,
                        const char *prompt, int max_tokens, double temperature,
                        long timeout, char *response, size_t resp_size)
{
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    /* Build JSON body */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", model);
    cJSON *messages = cJSON_AddArrayToObject(body, "messages");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", prompt);
    cJSON_AddItemToArray(messages, msg);
    cJSON_AddNumberToObject(body, "max_tokens", max_tokens);
    cJSON_AddNumberToObject(body, "temperature", temperature);
    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    struct curl_slist *headers = NULL;
    char auth[STR_URL];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    HttpBuffer buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL, OPENAI_API_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, meta_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_str);

    if (res != CURLE_OK || !buf.data) {
        free(buf.data);
        return false;
    }

    /* Extract choices[0].message.content */
    cJSON *root = cJSON_Parse(buf.data);
    free(buf.data);
    if (!root) return false;

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(root);
        return false;
    }
    cJSON *first = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(first, "message");
    cJSON *content = message ? cJSON_GetObjectItem(message, "content") : NULL;
    if (!content || !cJSON_IsString(content)) {
        cJSON_Delete(root);
        return false;
    }
    safe_strcpy(response, content->valuestring, resp_size);
    str_strip(response);
    cJSON_Delete(root);
    return true;
}

/* ── MusicBrainz ────────────────────────────────────────────────────────── */

static int match_score(cJSON *release, const char *artist, const char *album)
{
    int score = 0;
    cJSON *title_j = cJSON_GetObjectItem(release, "title");
    const char *title = title_j ? title_j->valuestring : "";

    /* Title scoring */
    if (strcasecmp_portable(title, album) == 0) {
        score += EXACT_TITLE_SCORE;
    } else if (str_contains_ci(title, album)) {
        score += PARTIAL_TITLE_SCORE;
    } else if (str_contains_ci(album, title)) {
        score += REVERSE_PARTIAL_SCORE;
    }

    /* Artist scoring */
    cJSON *credits = cJSON_GetObjectItem(release, "artist-credit");
    if (credits && cJSON_IsArray(credits)) {
        int n = cJSON_GetArraySize(credits);
        for (int i = 0; i < n; i++) {
            cJSON *credit = cJSON_GetArrayItem(credits, i);
            cJSON *cname = cJSON_GetObjectItem(credit, "name");
            if (!cname) {
                cJSON *cart = cJSON_GetObjectItem(credit, "artist");
                cname = cart ? cJSON_GetObjectItem(cart, "name") : NULL;
            }
            if (cname && cname->valuestring) {
                if (strcasecmp_portable(cname->valuestring, artist) == 0)
                    score += EXACT_ARTIST_SCORE;
                else if (str_contains_ci(cname->valuestring, artist) ||
                         str_contains_ci(artist, cname->valuestring))
                    score += PARTIAL_ARTIST_SCORE;
            }
        }
    }

    cJSON *mb_score = cJSON_GetObjectItem(release, "score");
    if (mb_score && cJSON_IsNumber(mb_score))
        score += (int)(mb_score->valuedouble) / MB_SCORE_DIVISOR;

    return score;
}

static bool mb_get_album_info(const char *artist, const char *album, CombinedInfo *out)
{
    if (!artist[0] || !album[0]) return false;

    char cache_key[STR_LONG];
    snprintf(cache_key, sizeof(cache_key), "%s|%s", artist, album);
    char *cached = cache_get(meta_cache, cache_key);
    if (cached) {
        /* Parse cached "year|label|genre" */
        char *p1 = strchr(cached, '|');
        if (p1) {
            *p1 = '\0';
            safe_strcpy(out->year, cached, sizeof(out->year));
            char *p2 = strchr(p1 + 1, '|');
            if (p2) {
                *p2 = '\0';
                safe_strcpy(out->label, p1 + 1, sizeof(out->label));
                safe_strcpy(out->genre, p2 + 1, sizeof(out->genre));
            }
        }
        free(cached);
        return true;
    }
    if (cache_has(meta_cache, cache_key)) return false;

    /* Search MusicBrainz */
    const char *queries[] = {
        "artist:\"%s\" AND release:%s",
        "artist:%s release:%s"
    };

    for (int q = 0; q < 2; q++) {
        rate_limit();
        char query[STR_URL];
        snprintf(query, sizeof(query), queries[q], artist, album);
        char encoded_query[STR_URL * 2];
        url_encode(query, encoded_query, sizeof(encoded_query));

        char url[STR_URL * 3];
        snprintf(url, sizeof(url), "%s/release/?query=%s&fmt=json&limit=%d",
                 MUSICBRAINZ_BASE_URL, encoded_query, MB_SEARCH_LIMIT);

        HttpBuffer buf = {0};
        if (!meta_http_get(url, HTTP_TIMEOUT_MUSICBRAINZ, MUSICBRAINZ_USER_AGENT, &buf))
            continue;

        cJSON *root = cJSON_Parse(buf.data);
        free(buf.data);
        if (!root) continue;

        cJSON *releases = cJSON_GetObjectItem(root, "releases");
        if (!releases || !cJSON_IsArray(releases)) {
            cJSON_Delete(root);
            continue;
        }

        cJSON *best = NULL;
        int best_score = -1;
        int n = cJSON_GetArraySize(releases);
        for (int i = 0; i < n; i++) {
            cJSON *rel = cJSON_GetArrayItem(releases, i);
            int s = match_score(rel, artist, album);
            if (s > best_score) {
                best_score = s;
                best = rel;
            }
        }

        if (best) {
            /* Extract metadata */
            cJSON *date = cJSON_GetObjectItem(best, "date");
            if (date && cJSON_IsString(date) && strlen(date->valuestring) >= 4) {
                char yr[5];
                memcpy(yr, date->valuestring, 4);
                yr[4] = '\0';
                safe_strcpy(out->year, yr, sizeof(out->year));
            } else {
                safe_strcpy(out->year, "-", sizeof(out->year));
            }

            safe_strcpy(out->label, "-", sizeof(out->label));
            cJSON *label_info = cJSON_GetObjectItem(best, "label-info");
            if (label_info && cJSON_IsArray(label_info) && cJSON_GetArraySize(label_info) > 0) {
                cJSON *first_li = cJSON_GetArrayItem(label_info, 0);
                cJSON *label = cJSON_GetObjectItem(first_li, "label");
                if (label) {
                    cJSON *lname = cJSON_GetObjectItem(label, "name");
                    if (lname && cJSON_IsString(lname))
                        safe_strcpy(out->label, lname->valuestring, sizeof(out->label));
                }
            }

            /* Fetch genre tags from release-group */
            safe_strcpy(out->genre, "-", sizeof(out->genre));
            cJSON *rg = cJSON_GetObjectItem(best, "release-group");
            if (rg) {
                cJSON *rg_id = cJSON_GetObjectItem(rg, "id");
                if (rg_id && cJSON_IsString(rg_id)) {
                    rate_limit();
                    char tag_url[STR_URL];
                    snprintf(tag_url, sizeof(tag_url),
                             "%s/release-group/%s?inc=tags&fmt=json",
                             MUSICBRAINZ_BASE_URL, rg_id->valuestring);
                    HttpBuffer tbuf = {0};
                    if (meta_http_get(tag_url, HTTP_TIMEOUT_MUSICBRAINZ,
                                      MUSICBRAINZ_USER_AGENT, &tbuf)) {
                        cJSON *troot = cJSON_Parse(tbuf.data);
                        if (troot) {
                            cJSON *tags = cJSON_GetObjectItem(troot, "tags");
                            if (tags && cJSON_IsArray(tags) && cJSON_GetArraySize(tags) > 0) {
                                /* Sort by count (simple selection of top N) */
                                int tag_n = cJSON_GetArraySize(tags);
                                char genre_buf[STR_MEDIUM] = "";
                                int taken = 0;
                                /* Simple top-N by iterating N times */
                                bool used[128] = {0};
                                for (int t = 0; t < int_min(TOP_GENRE_COUNT, tag_n); t++) {
                                    int best_idx = -1;
                                    int best_cnt = -1;
                                    for (int j = 0; j < int_min(tag_n, 128); j++) {
                                        if (used[j]) continue;
                                        cJSON *tag = cJSON_GetArrayItem(tags, j);
                                        cJSON *cnt = cJSON_GetObjectItem(tag, "count");
                                        int c = cnt ? (int)cnt->valuedouble : 0;
                                        if (c > best_cnt) {
                                            best_cnt = c;
                                            best_idx = j;
                                        }
                                    }
                                    if (best_idx >= 0) {
                                        used[best_idx] = true;
                                        cJSON *tag = cJSON_GetArrayItem(tags, best_idx);
                                        cJSON *tname = cJSON_GetObjectItem(tag, "name");
                                        if (tname && cJSON_IsString(tname)) {
                                            if (taken > 0)
                                                strncat(genre_buf, ", ",
                                                        sizeof(genre_buf) - strlen(genre_buf) - 1);
                                            strncat(genre_buf, tname->valuestring,
                                                    sizeof(genre_buf) - strlen(genre_buf) - 1);
                                            taken++;
                                        }
                                    }
                                }
                                if (genre_buf[0])
                                    safe_strcpy(out->genre, genre_buf, sizeof(out->genre));
                            }
                            cJSON_Delete(troot);
                        }
                        free(tbuf.data);
                    }
                }
            }

            /* Cache the result */
            char cache_val[STR_LONG];
            snprintf(cache_val, sizeof(cache_val), "%s|%s|%s", out->year, out->label, out->genre);
            cache_set(meta_cache, cache_key, cache_val);
            cJSON_Delete(root);
            return true;
        }
        cJSON_Delete(root);
    }

    cache_set(meta_cache, cache_key, NULL);
    return false;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void metadata_init(void)
{
    meta_logger = logger_create("logs/musicbrainz.log", LOG_MAX_BYTES, LOG_BACKUP_COUNT);
    meta_cache = cache_create(MB_CACHE_SIZE);
}

void metadata_cleanup(void)
{
    cache_destroy(meta_cache);
    meta_cache = NULL;
    logger_destroy(meta_logger);
    meta_logger = NULL;
}

static void strip_markdown_fences(char *s)
{
    if (!s) return;
    /* Strip leading ```json or ``` */
    if (strncmp(s, "```", 3) == 0) {
        char *nl = strchr(s, '\n');
        if (nl) memmove(s, nl + 1, strlen(nl + 1) + 1);
        else memmove(s, s + 3, strlen(s + 3) + 1);
    }
    /* Strip trailing ``` */
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    if (len >= 3 && strcmp(s + len - 3, "```") == 0) {
        s[len - 3] = '\0';
    }
    str_strip(s);
}

bool metadata_get_combined(const char *title, const char *artist, const char *album,
                           const char *api_key, const char *system_prompt,
                           const char *model, CombinedInfo *out)
{
    memset(out, 0, sizeof(CombinedInfo));
    safe_strcpy(out->year, "-", sizeof(out->year));
    safe_strcpy(out->label, "-", sizeof(out->label));
    safe_strcpy(out->genre, "-", sizeof(out->genre));
    out->has_track_info = false;

    char cache_key[STR_LONG];
    snprintf(cache_key, sizeof(cache_key), "combined|%s|%s|%s", title, artist, album);
    char *cached = cache_get(meta_cache, cache_key);
    if (cached) {
        /* Cached as JSON */
        cJSON *j = cJSON_Parse(cached);
        if (j) {
            cJSON *y = cJSON_GetObjectItem(j, "year");
            cJSON *l = cJSON_GetObjectItem(j, "label");
            cJSON *g = cJSON_GetObjectItem(j, "genre");
            cJSON *t = cJSON_GetObjectItem(j, "track_info");
            if (y) safe_strcpy(out->year, y->valuestring, sizeof(out->year));
            if (l) safe_strcpy(out->label, l->valuestring, sizeof(out->label));
            if (g) safe_strcpy(out->genre, g->valuestring, sizeof(out->genre));
            if (t && cJSON_IsString(t) && strcmp(t->valuestring, "-") != 0) {
                safe_strcpy(out->track_info, t->valuestring, sizeof(out->track_info));
                out->has_track_info = true;
            }
            cJSON_Delete(j);
        }
        free(cached);
        return true;
    }
    if (cache_has(meta_cache, cache_key)) return false;

    if (!api_key || !api_key[0]) {
        /* MusicBrainz fallback */
        mb_get_album_info(artist, album, out);
        return true;
    }

    /* Build combined prompt */
    char track_instr[STR_MEDIUM] = "a short paragraph (2-4 sentences) about this specific song";
    if (system_prompt && system_prompt[0]) {
        strncat(track_instr, ". ", sizeof(track_instr) - strlen(track_instr) - 1);
        strncat(track_instr, system_prompt, sizeof(track_instr) - strlen(track_instr) - 1);
    }

    char prompt[STR_TEXT];
    snprintf(prompt, sizeof(prompt), COMBINED_INFO_PROMPT_FMT,
             title, artist, album, track_instr);

    const char *use_model = (model && model[0]) ? model : DEFAULT_OPENAI_MODEL;
    char response[STR_TEXT] = "";
    if (!openai_post(api_key, use_model, prompt, COMBINED_INFO_MAX_TOKENS,
                     COMBINED_INFO_TEMPERATURE, HTTP_TIMEOUT_OPENAI_COMBINED,
                     response, sizeof(response))) {
        LOG_ERROR(meta_logger, "Combined AI request failed, falling back to MB");
        mb_get_album_info(artist, album, out);
        return true;
    }

    strip_markdown_fences(response);
    cJSON *j = cJSON_Parse(response);
    if (!j) {
        LOG_ERROR(meta_logger, "Failed to parse combined AI response");
        mb_get_album_info(artist, album, out);
        return true;
    }

    cJSON *y = cJSON_GetObjectItem(j, "year");
    cJSON *l = cJSON_GetObjectItem(j, "label");
    cJSON *g = cJSON_GetObjectItem(j, "genre");
    cJSON *t = cJSON_GetObjectItem(j, "track_info");
    if (y && cJSON_IsString(y)) safe_strcpy(out->year, y->valuestring, sizeof(out->year));
    if (l && cJSON_IsString(l)) safe_strcpy(out->label, l->valuestring, sizeof(out->label));
    if (g && cJSON_IsString(g)) safe_strcpy(out->genre, g->valuestring, sizeof(out->genre));
    if (t && cJSON_IsString(t) && strcmp(t->valuestring, "-") != 0) {
        safe_strcpy(out->track_info, t->valuestring, sizeof(out->track_info));
        out->has_track_info = true;
    }

    /* Cache as JSON */
    char *cache_val = cJSON_PrintUnformatted(j);
    cache_set(meta_cache, cache_key, cache_val);
    free(cache_val);
    cJSON_Delete(j);
    return true;
}

char *metadata_get_station_info(const char *station_name, const char *api_key,
                                const char *model)
{
    if (!station_name || !station_name[0] || !api_key || !api_key[0]) return NULL;

    char cache_key[STR_LONG];
    snprintf(cache_key, sizeof(cache_key), "station|%s", station_name);
    char *cached = cache_get(meta_cache, cache_key);
    if (cached) return cached;
    if (cache_has(meta_cache, cache_key)) return NULL;

    char prompt[STR_TEXT];
    snprintf(prompt, sizeof(prompt), STATION_INFO_PROMPT_FMT, station_name);
    const char *use_model = (model && model[0]) ? model : DEFAULT_OPENAI_MODEL;

    char response[STR_TEXT] = "";
    if (!openai_post(api_key, use_model, prompt, STATION_INFO_MAX_TOKENS,
                     STATION_INFO_TEMPERATURE, HTTP_TIMEOUT_OPENAI_STATION,
                     response, sizeof(response))) {
        cache_set(meta_cache, cache_key, NULL);
        return NULL;
    }

    if (strcmp(response, "-") == 0) {
        cache_set(meta_cache, cache_key, NULL);
        return NULL;
    }

    cache_set(meta_cache, cache_key, response);
    return strdup(response);
}

char *metadata_get_lyrics(const char *title, const char *artist)
{
    if (!title || !title[0] || !artist || !artist[0]) return NULL;

    char cache_key[STR_LONG];
    snprintf(cache_key, sizeof(cache_key), "lyrics|%s|%s", title, artist);
    char *cached = cache_get(meta_cache, cache_key);
    if (cached) return cached;
    if (cache_has(meta_cache, cache_key)) return NULL;

    char enc_artist[STR_URL], enc_title[STR_URL];
    url_encode(artist, enc_artist, sizeof(enc_artist));
    url_encode(title, enc_title, sizeof(enc_title));

    char url[STR_URL * 2];
    snprintf(url, sizeof(url), "%s?artist_name=%s&track_name=%s",
             LRCLIB_API_URL, enc_artist, enc_title);

    HttpBuffer buf = {0};
    if (!meta_http_get(url, HTTP_TIMEOUT_LYRICS, MUSICBRAINZ_USER_AGENT, &buf)) return NULL;

    cJSON *root = cJSON_Parse(buf.data);
    free(buf.data);
    if (!root) return NULL;

    cJSON *plain = cJSON_GetObjectItem(root, "plainLyrics");
    char *result = NULL;
    if (plain && cJSON_IsString(plain) && plain->valuestring[0]) {
        result = strdup(plain->valuestring);
        cache_set(meta_cache, cache_key, result);
    } else {
        cache_set(meta_cache, cache_key, NULL);
    }
    cJSON_Delete(root);
    return result;
}

char *metadata_get_wiki(const char *title)
{
    if (!title || !title[0]) return NULL;

    char cache_key[STR_LONG];
    snprintf(cache_key, sizeof(cache_key), "wiki|%s", title);
    char *cached = cache_get(meta_cache, cache_key);
    if (cached) return cached;
    if (cache_has(meta_cache, cache_key)) return NULL;

    char encoded[STR_URL];
    url_encode(title, encoded, sizeof(encoded));

    for (int i = 0; i < WIKIPEDIA_LANG_COUNT; i++) {
        rate_limit();
        char url[STR_URL * 2];
        snprintf(url, sizeof(url), WIKIPEDIA_API_TEMPLATE,
                 WIKIPEDIA_LANGUAGES[i], encoded);

        HttpBuffer buf = {0};
        if (!meta_http_get(url, HTTP_TIMEOUT_WIKIPEDIA, MUSICBRAINZ_USER_AGENT, &buf))
            continue;

        cJSON *root = cJSON_Parse(buf.data);
        free(buf.data);
        if (!root) continue;

        cJSON *extract = cJSON_GetObjectItem(root, "extract");
        if (extract && cJSON_IsString(extract) && extract->valuestring[0]) {
            char *result = strdup(extract->valuestring);
            cache_set(meta_cache, cache_key, result);
            cJSON_Delete(root);
            return result;
        }
        cJSON_Delete(root);
    }

    cache_set(meta_cache, cache_key, NULL);
    return NULL;
}
