/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 */

#include "player.h"
#include "config.h"
#include "constants.h"
#include "util.h"
#include <ctype.h>
#include <curl/curl.h>
#include <expat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

Logger *player_logger = NULL;

/* ── HTTP helpers ───────────────────────────────────────────────────────── */

typedef struct {
  char *data;
  size_t size;
} HttpBuffer;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
  HttpBuffer *buf = userdata;
  size_t total = size * nmemb;
  char *tmp = realloc(buf->data, buf->size + total + 1);
  if (!tmp)
    return 0;
  buf->data = tmp;
  memcpy(buf->data + buf->size, ptr, total);
  buf->size += total;
  buf->data[buf->size] = '\0';
  return total;
}

static bool http_get(const char *url, long timeout, HttpBuffer *out) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return false;
  out->data = NULL;
  out->size = 0;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK) {
    if (player_logger)
      LOG_ERROR(player_logger, "HTTP GET failed: %s — %s", url,
                curl_easy_strerror(res));
    free(out->data);
    out->data = NULL;
    out->size = 0;
    return false;
  }
  return true;
}

static bool player_request(BlusoundPlayer *p, const char *path,
                           const char *params, HttpBuffer *out) {
  char url[STR_URL * 2];
  if (params && params[0]) {
    snprintf(url, sizeof(url), "%s%s?%s", p->base_url, path, params);
  } else {
    snprintf(url, sizeof(url), "%s%s", p->base_url, path);
  }
  return http_get(url, HTTP_TIMEOUT_PLAYER, out);
}

/* ── XML parsing helpers ────────────────────────────────────────────────── */

typedef struct {
  char tag[128];
  char text[STR_MEDIUM];
  PlayerStatus *status;
} StatusParseCtx;

static void status_start(void *data, const char *el, const char **attr) {
  StatusParseCtx *ctx = data;
  safe_strcpy(ctx->tag, el, sizeof(ctx->tag));
  ctx->text[0] = '\0';
  /* Root <status> element has etag attribute */
  if (strcmp(el, "status") == 0) {
    for (int i = 0; attr[i]; i += 2) {
      if (strcmp(attr[i], "etag") == 0)
        safe_strcpy(ctx->status->etag, attr[i + 1], sizeof(ctx->status->etag));
    }
  }
}

static void status_chars(void *data, const char *s, int len) {
  StatusParseCtx *ctx = data;
  size_t cur = strlen(ctx->text);
  size_t avail = sizeof(ctx->text) - cur - 1;
  size_t copy = (size_t)len < avail ? (size_t)len : avail;
  memcpy(ctx->text + cur, s, copy);
  ctx->text[cur + copy] = '\0';
}

static int safe_atoi(const char *s) {
  if (!s || !s[0])
    return 0;
  return atoi(s);
}

static double safe_atof(const char *s) {
  if (!s || !s[0])
    return 0.0;
  return atof(s);
}

static void status_parse_playback_fields(PlayerStatus *st, const char *el,
                                         const char *t) {
  if (strcmp(el, "album") == 0)
    safe_strcpy(st->album, t, sizeof(st->album));
  else if (strcmp(el, "artist") == 0)
    safe_strcpy(st->artist, t, sizeof(st->artist));
  else if (strcmp(el, "title1") == 0) {
    safe_strcpy(st->title1, t, sizeof(st->title1));
    safe_strcpy(st->name, t, sizeof(st->name));
  } else if (strcmp(el, "title2") == 0)
    safe_strcpy(st->title2, t, sizeof(st->title2));
  else if (strcmp(el, "title3") == 0)
    safe_strcpy(st->title3, t, sizeof(st->title3));
  else if (strcmp(el, "state") == 0)
    safe_strcpy(st->state, t, sizeof(st->state));
  else if (strcmp(el, "volume") == 0)
    st->volume = safe_atoi(t);
  else if (strcmp(el, "service") == 0)
    safe_strcpy(st->service, t, sizeof(st->service));
  else if (strcmp(el, "inputId") == 0)
    safe_strcpy(st->input_id, t, sizeof(st->input_id));
  else if (strcmp(el, "canMovePlayback") == 0)
    st->can_move_playback = (strcmp(t, "true") == 0);
  else if (strcmp(el, "canSeek") == 0)
    st->can_seek = (safe_atoi(t) == 1);
  else if (strcmp(el, "cursor") == 0)
    st->cursor = safe_atoi(t);
  else if (strcmp(el, "db") == 0)
    st->db = safe_atof(t);
  else if (strcmp(el, "fn") == 0)
    safe_strcpy(st->fn, t, sizeof(st->fn));
  else if (strcmp(el, "image") == 0)
    safe_strcpy(st->image, t, sizeof(st->image));
}

static void status_parse_extra_fields(PlayerStatus *st, const char *el,
                                      const char *t) {
  if (strcmp(el, "indexing") == 0)
    st->indexing = safe_atoi(t);
  else if (strcmp(el, "mid") == 0)
    st->mid = safe_atoi(t);
  else if (strcmp(el, "mode") == 0)
    st->mode = safe_atoi(t);
  else if (strcmp(el, "mute") == 0)
    st->mute = (safe_atoi(t) == 1);
  else if (strcmp(el, "pid") == 0)
    st->pid = safe_atoi(t);
  else if (strcmp(el, "prid") == 0)
    st->prid = safe_atoi(t);
  else if (strcmp(el, "quality") == 0)
    safe_strcpy(st->quality, t, sizeof(st->quality));
  else if (strcmp(el, "repeat") == 0)
    st->repeat = safe_atoi(t);
  else if (strcmp(el, "serviceIcon") == 0)
    safe_strcpy(st->service_icon, t, sizeof(st->service_icon));
  else if (strcmp(el, "serviceName") == 0)
    safe_strcpy(st->service_name, t, sizeof(st->service_name));
  else if (strcmp(el, "shuffle") == 0)
    st->shuffle = (safe_atoi(t) == 1);
  else if (strcmp(el, "sid") == 0)
    st->sid = safe_atoi(t);
  else if (strcmp(el, "sleep") == 0)
    safe_strcpy(st->sleep_str, t, sizeof(st->sleep_str));
  else if (strcmp(el, "song") == 0)
    st->song = safe_atoi(t);
  else if (strcmp(el, "streamFormat") == 0)
    safe_strcpy(st->stream_format, t, sizeof(st->stream_format));
  else if (strcmp(el, "syncStat") == 0)
    st->sync_stat = safe_atoi(t);
  else if (strcmp(el, "totlen") == 0)
    st->totlen = safe_atoi(t);
  else if (strcmp(el, "secs") == 0)
    st->secs = safe_atoi(t);
  else if (strcmp(el, "albumid") == 0)
    safe_strcpy(st->albumid, t, sizeof(st->albumid));
  else if (strcmp(el, "artistid") == 0)
    safe_strcpy(st->artistid, t, sizeof(st->artistid));
  else if (strcmp(el, "composer") == 0)
    safe_strcpy(st->composer, t, sizeof(st->composer));
  else if (strcmp(el, "isFavourite") == 0)
    st->is_favourite = (strcmp(t, "1") == 0);
}

static void status_end(void *data, const char *el) {
  StatusParseCtx *ctx = data;
  status_parse_playback_fields(ctx->status, el, ctx->text);
  status_parse_extra_fields(ctx->status, el, ctx->text);
  ctx->text[0] = '\0';
}

/* ── Browse XML parsing ─────────────────────────────────────────────────── */

typedef struct {
  PlayerSource *items;
  int count;
  int capacity;
  char search_key[STR_URL];
  char next_key[STR_URL];
} BrowseParseCtx;

static void ensure_browse_capacity(BrowseParseCtx *ctx) {
  if (ctx->count >= ctx->capacity) {
    int new_cap = ctx->capacity ? ctx->capacity * 2 : 32;
    PlayerSource *tmp = realloc(ctx->items, new_cap * sizeof(PlayerSource));
    if (!tmp)
      return;
    ctx->items = tmp;
    ctx->capacity = new_cap;
  }
}

static void parse_item_attributes(PlayerSource *s, const char **attr) {
  for (int i = 0; attr[i]; i += 2) {
    const char *k = attr[i];
    const char *v = attr[i + 1];
    if (strcmp(k, "text") == 0)
      safe_strcpy(s->text, v, sizeof(s->text));
    else if (strcmp(k, "text2") == 0)
      safe_strcpy(s->text2, v, sizeof(s->text2));
    else if (strcmp(k, "image") == 0)
      safe_strcpy(s->image, v, sizeof(s->image));
    else if (strcmp(k, "browseKey") == 0)
      safe_strcpy(s->browse_key, v, sizeof(s->browse_key));
    else if (strcmp(k, "playURL") == 0)
      safe_strcpy(s->play_url, v, sizeof(s->play_url));
    else if (strcmp(k, "actionURL") == 0)
      safe_strcpy(s->play_url, v, sizeof(s->play_url));
    else if (strcmp(k, "inputType") == 0)
      safe_strcpy(s->input_type, v, sizeof(s->input_type));
    else if (strcmp(k, "type") == 0)
      safe_strcpy(s->type, v, sizeof(s->type));
    else if (strcmp(k, "contextMenuKey") == 0)
      safe_strcpy(s->context_menu_key, v, sizeof(s->context_menu_key));
    else if (strcmp(k, "isFavourite") == 0)
      s->is_favourite = (strcmp(v, "true") == 0);
    else if (strcmp(k, "searchKey") == 0)
      safe_strcpy(s->search_key, v, sizeof(s->search_key));
  }
  str_strip(s->text);
}

static void browse_start(void *data, const char *el, const char **attr) {
  BrowseParseCtx *ctx = data;

  /* Capture searchKey/nextKey from root element (may be <browse>, <items>,
   * etc.) */
  if (strcmp(el, "item") != 0) {
    for (int i = 0; attr[i]; i += 2) {
      if (strcmp(attr[i], "searchKey") == 0)
        safe_strcpy(ctx->search_key, attr[i + 1], sizeof(ctx->search_key));
      else if (strcmp(attr[i], "nextKey") == 0)
        safe_strcpy(ctx->next_key, attr[i + 1], sizeof(ctx->next_key));
    }
  }

  if (strcmp(el, "item") == 0) {
    ensure_browse_capacity(ctx);
    PlayerSource *s = &ctx->items[ctx->count];
    memset(s, 0, sizeof(PlayerSource));
    safe_strcpy(s->search_key, ctx->search_key, sizeof(s->search_key));
    parse_item_attributes(s, attr);
    ctx->count++;
  }
}

static void browse_end(void *data, const char *el) {
  (void)data;
  (void)el;
}

static PlayerSource *parse_browse_xml(const char *xml, int *count,
                                      char *search_key, size_t sk_size,
                                      char *next_key, size_t nk_size) {
  BrowseParseCtx ctx = {0};
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &ctx);
  XML_SetElementHandler(parser, browse_start, browse_end);

  if (XML_Parse(parser, xml, (int)strlen(xml), 1) == XML_STATUS_ERROR) {
    if (player_logger)
      LOG_ERROR(player_logger, "Browse XML parse error: %s",
                XML_ErrorString(XML_GetErrorCode(parser)));
  }
  XML_ParserFree(parser);

  *count = ctx.count;
  if (search_key)
    safe_strcpy(search_key, ctx.search_key, sk_size);
  if (next_key)
    safe_strcpy(next_key, ctx.next_key, nk_size);
  return ctx.items;
}

/* ── Playlist XML parsing ───────────────────────────────────────────────── */

typedef struct {
  PlaylistEntry *entries;
  int count;
  int capacity;
  char tag[64];
  char text[STR_MEDIUM];
  PlaylistEntry current;
  bool in_song;
} PlaylistParseCtx;

static void playlist_start(void *data, const char *el, const char **attr) {
  PlaylistParseCtx *ctx = data;
  safe_strcpy(ctx->tag, el, sizeof(ctx->tag));
  ctx->text[0] = '\0';

  if (strcmp(el, "song") == 0) {
    memset(&ctx->current, 0, sizeof(ctx->current));
    ctx->in_song = true;
    /* Some responses use attributes instead of child elements */
    for (int i = 0; attr[i]; i += 2) {
      if (strcmp(attr[i], "title") == 0)
        safe_strcpy(ctx->current.title, attr[i + 1],
                    sizeof(ctx->current.title));
      else if (strcmp(attr[i], "art") == 0)
        safe_strcpy(ctx->current.artist, attr[i + 1],
                    sizeof(ctx->current.artist));
      else if (strcmp(attr[i], "alb") == 0)
        safe_strcpy(ctx->current.album, attr[i + 1],
                    sizeof(ctx->current.album));
    }
  }
}

static void playlist_chars(void *data, const char *s, int len) {
  PlaylistParseCtx *ctx = data;
  size_t cur = strlen(ctx->text);
  size_t avail = sizeof(ctx->text) - cur - 1;
  size_t copy = (size_t)len < avail ? (size_t)len : avail;
  memcpy(ctx->text + cur, s, copy);
  ctx->text[cur + copy] = '\0';
}

static void playlist_end(void *data, const char *el) {
  PlaylistParseCtx *ctx = data;
  if (ctx->in_song) {
    if (strcmp(el, "title") == 0)
      safe_strcpy(ctx->current.title, ctx->text, sizeof(ctx->current.title));
    else if (strcmp(el, "art") == 0)
      safe_strcpy(ctx->current.artist, ctx->text, sizeof(ctx->current.artist));
    else if (strcmp(el, "alb") == 0)
      safe_strcpy(ctx->current.album, ctx->text, sizeof(ctx->current.album));
    else if (strcmp(el, "song") == 0) {
      if (ctx->count >= ctx->capacity) {
        int new_cap = ctx->capacity ? ctx->capacity * 2 : 32;
        PlaylistEntry *tmp =
            realloc(ctx->entries, new_cap * sizeof(PlaylistEntry));
        if (!tmp)
          return;
        ctx->entries = tmp;
        ctx->capacity = new_cap;
      }
      str_strip(ctx->current.title);
      str_strip(ctx->current.artist);
      str_strip(ctx->current.album);
      ctx->entries[ctx->count++] = ctx->current;
      ctx->in_song = false;
    }
  }
  ctx->text[0] = '\0';
}

/* ── SyncStatus parsing ─────────────────────────────────────────────────── */

typedef struct {
  char name[STR_MEDIUM];
  char brand[STR_MEDIUM];
  char model_name[STR_MEDIUM];
  char model_id[STR_SHORT];
  char group[STR_MEDIUM];
  char master_ip[STR_MEDIUM];
  char slave_ips[16][STR_MEDIUM];
  char slave_names[16][STR_MEDIUM];
  int slave_count;
  char current_tag[STR_SHORT];
  char current_text[STR_MEDIUM];
} SyncInfo;

static void sync_start(void *data, const char *el, const char **attr) {
  SyncInfo *info = data;
  safe_strcpy(info->current_tag, el, sizeof(info->current_tag));
  info->current_text[0] = '\0';
  if (strcmp(el, "SyncStatus") == 0) {
    for (int i = 0; attr[i]; i += 2) {
      if (strcmp(attr[i], "name") == 0)
        safe_strcpy(info->name, attr[i + 1], sizeof(info->name));
      else if (strcmp(attr[i], "brand") == 0)
        safe_strcpy(info->brand, attr[i + 1], sizeof(info->brand));
      else if (strcmp(attr[i], "modelName") == 0)
        safe_strcpy(info->model_name, attr[i + 1], sizeof(info->model_name));
      else if (strcmp(attr[i], "model") == 0)
        safe_strcpy(info->model_id, attr[i + 1], sizeof(info->model_id));
      else if (strcmp(attr[i], "group") == 0)
        safe_strcpy(info->group, attr[i + 1], sizeof(info->group));
    }
  } else if (strcmp(el, "slave") == 0 && info->slave_count < 16) {
    int idx = info->slave_count;
    for (int i = 0; attr[i]; i += 2) {
      if (strcmp(attr[i], "id") == 0)
        safe_strcpy(info->slave_ips[idx], attr[i + 1],
                    sizeof(info->slave_ips[0]));
      else if (strcmp(attr[i], "name") == 0)
        safe_strcpy(info->slave_names[idx], attr[i + 1],
                    sizeof(info->slave_names[0]));
    }
    if (info->slave_ips[idx][0])
      info->slave_count++;
  }
}

static void sync_chars(void *data, const char *s, int len) {
  SyncInfo *info = data;
  size_t cur = strlen(info->current_text);
  size_t avail = sizeof(info->current_text) - cur - 1;
  size_t copy = (size_t)len < avail ? (size_t)len : avail;
  memcpy(info->current_text + cur, s, copy);
  info->current_text[cur + copy] = '\0';
}

static void sync_end(void *data, const char *el) {
  SyncInfo *info = data;
  if (strcmp(el, "master") == 0)
    safe_strcpy(info->master_ip, info->current_text, sizeof(info->master_ip));
  info->current_text[0] = '\0';
}

/* ── Diagnostics HTML parsing (hand-parse) ──────────────────────────────── */

static bool skip_health_key(const char *key) {
  return strcmp(key, HEALTH_SKIP_KEY_TOTAL_SONGS) == 0 ||
         strcmp(key, HEALTH_SKIP_KEY_OTHER_PLAYERS) == 0;
}

static void strip_html_tags(char *s) {
  char *out = s;
  bool in_tag = false;
  for (char *p = s; *p; p++) {
    if (*p == '<')
      in_tag = true;
    else if (*p == '>')
      in_tag = false;
    else if (!in_tag)
      *out++ = *p;
  }
  *out = '\0';
}

static bool parse_diag_block(const char **p, const char **a_start,
                             const char **a_end, const char **b_start,
                             const char **b_end) {
  *a_start = strstr(*p, "\"ui-block-a\">");
  if (!*a_start)
    return false;
  *a_start += strlen("\"ui-block-a\">");
  *a_end = strstr(*a_start, "</div>");
  if (!*a_end)
    return false;
  *b_start = strstr(*a_end, "\"ui-block-b\">");
  if (!*b_start)
    return false;
  *b_start += strlen("\"ui-block-b\">");
  *b_end = strstr(*b_start, "</div>");
  return *b_end != NULL;
}

static void extract_diag_field(char *out, size_t out_size, const char *start,
                               const char *end) {
  size_t len = int_min((int)(end - start), (int)out_size - 1);
  memcpy(out, start, len);
  out[len] = '\0';
  strip_html_tags(out);
  str_strip(out);
}

static int parse_diagnostics(const char *html, KVPair *out, int max) {
  int count = 0;
  const char *p = html;
  while (count < max) {
    const char *a_start, *a_end, *b_start, *b_end;
    if (!parse_diag_block(&p, &a_start, &a_end, &b_start, &b_end))
      break;

    char key[STR_MEDIUM], value[STR_LONG];
    extract_diag_field(key, sizeof(key), a_start, a_end);
    size_t kl = strlen(key);
    if (kl > 0 && key[kl - 1] == ':')
      key[kl - 1] = '\0';
    extract_diag_field(value, sizeof(value), b_start, b_end);

    if (key[0] && value[0] && !skip_health_key(key)) {
      safe_strcpy(out[count].key, key, sizeof(out[count].key));
      safe_strcpy(out[count].value, value, sizeof(out[count].value));
      count++;
    }
    p = b_end;
  }
  return count;
}

static void parse_upgrade_status(const char *html, char *out, size_t out_size) {
  const char *content = strstr(html, "data-role=\"content\"");
  if (!content) {
    safe_strcpy(out, "Unknown", out_size);
    return;
  }
  const char *gt = strchr(content, '>');
  if (!gt) {
    safe_strcpy(out, "Unknown", out_size);
    return;
  }
  gt++;
  const char *end = strstr(gt, "</div>");
  if (!end) {
    safe_strcpy(out, "Unknown", out_size);
    return;
  }

  size_t len = int_min((int)(end - gt), (int)out_size - 1);
  memcpy(out, gt, len);
  out[len] = '\0';
  strip_html_tags(out);

  /* Find first non-empty line */
  char *p = out;
  while (*p && (isspace((unsigned char)*p)))
    p++;
  if (*p) {
    memmove(out, p, strlen(p) + 1);
  }
  char *nl = strchr(out, '\n');
  if (nl)
    *nl = '\0';
  char *stripped = str_strip(out);
  if (stripped != out && stripped[0]) {
    memmove(out, stripped, strlen(stripped) + 1);
  }
  if (!out[0])
    safe_strcpy(out, "Unknown", out_size);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void player_init(void) {
  player_logger =
      logger_create(LOG_FILE_PLAYER, LOG_MAX_BYTES, LOG_BACKUP_COUNT);
}

void player_fetch_sync_name(BlusoundPlayer *p) {
  HttpBuffer buf = {0};
  if (!player_request(p, "/SyncStatus", NULL, &buf) || !buf.data)
    return;
  SyncInfo info = {0};
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &info);
  XML_SetElementHandler(parser, sync_start, sync_end);
  XML_SetCharacterDataHandler(parser, sync_chars);
  XML_Parse(parser, buf.data, (int)buf.size, 1);
  XML_ParserFree(parser);
  if (info.name[0]) {
    safe_strcpy(p->name, info.name, sizeof(p->name));
    LOG_INFO(player_logger, "Player name: %s", p->name);
  }
  free(buf.data);
}

void player_init_sources(BlusoundPlayer *p) {
  for (int attempt = 0; attempt < SOURCE_INIT_MAX_RETRIES; attempt++) {
    int count = 0;
    PlayerSource *sources = player_browse(p, NULL, &count);
    if (sources && count > 0) {
      p->sources = sources;
      p->sources_count = count;
      p->sources_capacity = count;
      LOG_INFO(player_logger, "Initialized %d sources for %s", count, p->name);
      return;
    }
    free(sources);
    LOG_WARN(player_logger, "No sources for %s, attempt %d/%d", p->name,
             attempt + 1, SOURCE_INIT_MAX_RETRIES);
    if (attempt < SOURCE_INIT_MAX_RETRIES - 1)
      sleep(SOURCE_INIT_RETRY_DELAY);
  }
  LOG_ERROR(player_logger, "Failed to initialize sources for %s", p->name);
}

BlusoundPlayer *player_create(const char *host, const char *name) {
  BlusoundPlayer *p = calloc(1, sizeof(BlusoundPlayer));
  if (!p)
    return NULL;
  safe_strcpy(p->host_name, host, sizeof(p->host_name));
  safe_strcpy(p->name, name ? name : host, sizeof(p->name));
  snprintf(p->base_url, sizeof(p->base_url), "http://%s:%d", host,
           BLUOS_API_PORT);
  LOG_INFO(player_logger, "Initialized BlusoundPlayer: %s at %s", p->name,
           p->host_name);
  return p;
}

void player_sources_free(PlayerSource *sources, int count) {
  if (!sources)
    return;
  for (int i = 0; i < count; i++) {
    if (sources[i].children) {
      player_sources_free(sources[i].children, sources[i].children_count);
    }
  }
  free(sources);
}

void player_destroy(BlusoundPlayer *p) {
  if (!p)
    return;
  player_sources_free(p->sources, p->sources_count);
  /* Free browse cache */
  BrowseCacheEntry *bc = p->browse_cache;
  while (bc) {
    BrowseCacheEntry *next = bc->next;
    free(bc);
    bc = next;
  }
  free(p);
}

bool player_get_status(BlusoundPlayer *p, PlayerStatus *out) {
  HttpBuffer buf = {0};
  if (!player_request(p, "/Status", NULL, &buf))
    return false;

  memset(out, 0, sizeof(PlayerStatus));
  StatusParseCtx ctx = {0};
  ctx.status = out;

  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &ctx);
  XML_SetElementHandler(parser, status_start, status_end);
  XML_SetCharacterDataHandler(parser, status_chars);
  XML_Parse(parser, buf.data, (int)buf.size, 1);
  XML_ParserFree(parser);
  free(buf.data);

  LOG_INFO(player_logger, "Status: state=%s vol=%d song=%d", out->state,
           out->volume, out->song);
  return true;
}

bool player_get_sync_info(BlusoundPlayer *p, KVPair *out, int *count, int max) {
  HttpBuffer buf = {0};
  if (!player_request(p, "/SyncStatus", NULL, &buf))
    return false;

  SyncInfo info = {0};
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &info);
  XML_SetElementHandler(parser, sync_start, sync_end);
  XML_SetCharacterDataHandler(parser, sync_chars);
  XML_Parse(parser, buf.data, (int)buf.size, 1);
  XML_ParserFree(parser);
  free(buf.data);

  *count = 0;
  if (*count < max && info.name[0]) {
    safe_strcpy(out[*count].key, "Player Name", sizeof(out->key));
    safe_strcpy(out[*count].value, info.name, sizeof(out->value));
    (*count)++;
  }
  if (*count < max && info.brand[0]) {
    safe_strcpy(out[*count].key, "Brand", sizeof(out->key));
    safe_strcpy(out[*count].value, info.brand, sizeof(out->value));
    (*count)++;
  }
  if (*count < max && info.model_name[0]) {
    safe_strcpy(out[*count].key, "Model", sizeof(out->key));
    safe_strcpy(out[*count].value, info.model_name, sizeof(out->value));
    (*count)++;
  }
  if (*count < max && info.model_id[0]) {
    safe_strcpy(out[*count].key, "Model ID", sizeof(out->key));
    safe_strcpy(out[*count].value, info.model_id, sizeof(out->value));
    (*count)++;
  }
  return true;
}

static void strip_port_suffix(char *ip) {
  char *colon = strchr(ip, ':');
  if (colon)
    *colon = '\0';
}

bool player_get_group_info(BlusoundPlayer *p, GroupInfo *out) {
  memset(out, 0, sizeof(GroupInfo));
  HttpBuffer buf = {0};
  if (!player_request(p, "/SyncStatus", NULL, &buf))
    return false;
  SyncInfo info = {0};
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &info);
  XML_SetElementHandler(parser, sync_start, sync_end);
  XML_SetCharacterDataHandler(parser, sync_chars);
  XML_Parse(parser, buf.data, (int)buf.size, 1);
  XML_ParserFree(parser);
  free(buf.data);
  safe_strcpy(out->master_ip, info.master_ip, sizeof(out->master_ip));
  strip_port_suffix(out->master_ip);
  out->slave_count = int_min(info.slave_count, GROUP_MAX_SLAVES);
  for (int i = 0; i < out->slave_count; i++) {
    safe_strcpy(out->slave_ips[i], info.slave_ips[i],
                sizeof(out->slave_ips[0]));
    safe_strcpy(out->slave_names[i], info.slave_names[i],
                sizeof(out->slave_names[0]));
  }
  LOG_INFO(player_logger, "GroupInfo: master='%s' slaves=%d group='%s'",
           out->master_ip, out->slave_count, info.group);
  return true;
}

bool player_add_slave(BlusoundPlayer *p, const char *slave_ip) {
  char params[STR_MEDIUM];
  snprintf(params, sizeof(params), "slave=%s&port=%d", slave_ip,
           BLUOS_API_PORT);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/AddSlave", params, &buf);
  LOG_INFO(player_logger, "AddSlave %s: %s — %s", slave_ip,
           ok ? "ok" : "failed", buf.data ? buf.data : "(no response)");
  free(buf.data);
  return ok;
}

bool player_remove_slave(BlusoundPlayer *p, const char *slave_ip) {
  char params[STR_MEDIUM];
  snprintf(params, sizeof(params), "slave=%s&port=%d", slave_ip,
           BLUOS_API_PORT);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/RemoveSlave", params, &buf);
  LOG_INFO(player_logger, "RemoveSlave %s: %s — %s", slave_ip,
           ok ? "ok" : "failed", buf.data ? buf.data : "(no response)");
  free(buf.data);
  return ok;
}

static bool command_to_host(const char *host, const char *path,
                            const char *params) {
  char url[STR_URL * 2];
  if (params && params[0])
    snprintf(url, sizeof(url), "http://%s:%d%s?%s", host, BLUOS_API_PORT, path,
             params);
  else
    snprintf(url, sizeof(url), "http://%s:%d%s", host, BLUOS_API_PORT, path);
  HttpBuffer buf = {0};
  bool ok = http_get(url, HTTP_TIMEOUT_PLAYER, &buf);
  free(buf.data);
  return ok;
}

bool player_leave_group(BlusoundPlayer *p) {
  GroupInfo group;
  if (!player_get_group_info(p, &group) || !group.master_ip[0])
    return false;
  char params[STR_MEDIUM];
  snprintf(params, sizeof(params), "slave=%s&port=%d", p->host_name,
           BLUOS_API_PORT);
  return command_to_host(group.master_ip, "/RemoveSlave", params);
}

bool player_get_diagnostics(BlusoundPlayer *p, KVPair *out, int *count,
                            int max) {
  char url[STR_URL];
  snprintf(url, sizeof(url), "http://%s/diagnostics", p->host_name);
  HttpBuffer buf = {0};
  if (!http_get(url, HTTP_TIMEOUT_HEALTH, &buf))
    return false;
  *count = parse_diagnostics(buf.data, out, max);
  free(buf.data);
  return true;
}

bool player_get_upgrade_status(BlusoundPlayer *p, char *out, size_t out_size) {
  char url[STR_URL];
  snprintf(url, sizeof(url), "http://%s/upgrade", p->host_name);
  HttpBuffer buf = {0};
  if (!http_get(url, HTTP_TIMEOUT_HEALTH, &buf)) {
    safe_strcpy(out, "Error: connection failed", out_size);
    return false;
  }
  parse_upgrade_status(buf.data, out, out_size);
  free(buf.data);
  return true;
}

void player_get_web_url(BlusoundPlayer *p, char *out, size_t out_size) {
  snprintf(out, out_size, "http://%s", p->host_name);
}

bool player_set_volume(BlusoundPlayer *p, int volume) {
  char params[64];
  snprintf(params, sizeof(params), "level=%d", volume);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Volume", params, &buf);
  free(buf.data);
  return ok;
}

bool player_toggle_mute(BlusoundPlayer *p, bool current_mute) {
  char params[32];
  snprintf(params, sizeof(params), "mute=%d", current_mute ? 0 : 1);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Volume", params, &buf);
  free(buf.data);
  return ok;
}

bool player_toggle_shuffle(BlusoundPlayer *p, bool current_shuffle) {
  int new_state = current_shuffle ? 0 : 1;
  HttpBuffer buf = {0};
  if (new_state == 1) {
    /* Toggle off then on to force reshuffle */
    player_request(p, "/Shuffle", "state=0", &buf);
    free(buf.data);
    buf = (HttpBuffer){0};
  }
  char params[32];
  snprintf(params, sizeof(params), "state=%d", new_state);
  bool ok = player_request(p, "/Shuffle", params, &buf);
  free(buf.data);
  return ok;
}

bool player_cycle_repeat(BlusoundPlayer *p, int current, int *new_repeat) {
  /* 0=repeat queue, 1=repeat track, 2=off — cycle: 2→0→1→2 */
  int next;
  switch (current) {
  case 2:
    next = 0;
    break;
  case 0:
    next = 1;
    break;
  default:
    next = 2;
    break;
  }
  *new_repeat = next;
  char params[32];
  snprintf(params, sizeof(params), "state=%d", next);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Repeat", params, &buf);
  free(buf.data);
  return ok;
}

bool player_toggle_play_pause(BlusoundPlayer *p) {
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Pause", "toggle=1", &buf);
  free(buf.data);
  return ok;
}

bool player_skip(BlusoundPlayer *p) {
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Skip", NULL, &buf);
  free(buf.data);
  return ok;
}

bool player_back(BlusoundPlayer *p) {
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Back", NULL, &buf);
  free(buf.data);
  return ok;
}

bool player_play_queue_track(BlusoundPlayer *p, int index) {
  char params[32];
  snprintf(params, sizeof(params), "id=%d", index);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Play", params, &buf);
  free(buf.data);
  return ok;
}

bool player_select_input(BlusoundPlayer *p, PlayerSource *source) {
  HttpBuffer buf = {0};
  bool ok;
  if (source->play_url[0]) {
    char url[STR_URL * 2];
    snprintf(url, sizeof(url), "%s%s", p->base_url, source->play_url);
    ok = http_get(url, HTTP_TIMEOUT_PLAYER, &buf);
  } else if (source->browse_key[0]) {
    char encoded[STR_URL * 2];
    url_encode_param(source->browse_key, encoded, sizeof(encoded));
    char params[STR_URL * 2];
    snprintf(params, sizeof(params), "key=%s", encoded);
    ok = player_request(p, "/Browse", params, &buf);
  } else {
    return false;
  }
  free(buf.data);
  return ok;
}

PlayerSource *player_browse(BlusoundPlayer *p, const char *key, int *count) {
  char params[STR_URL * 2] = "";
  if (key && key[0]) {
    char encoded[STR_URL * 2];
    url_encode_param(key, encoded, sizeof(encoded));
    snprintf(params, sizeof(params), "key=%s", encoded);
  }
  HttpBuffer buf = {0};
  LOG_INFO(player_logger, "player_browse key='%s' params='%s'",
           key ? key : "(null)", params);
  if (!player_request(p, "/Browse", params[0] ? params : NULL, &buf)) {
    LOG_ERROR(player_logger, "player_browse request failed for key='%s'",
              key ? key : "(null)");
    *count = 0;
    return NULL;
  }
  LOG_INFO(player_logger, "player_browse response size=%zu data='%.2000s'",
           buf.size, buf.data ? buf.data : "(null)");
  PlayerSource *items = parse_browse_xml(buf.data, count, NULL, 0, NULL, 0);
  free(buf.data);
  LOG_INFO(player_logger, "Browsed %d items for %s", *count, p->name);
  return items;
}

static bool browse_all_append(PlayerSource **all, int *all_count, int *all_cap,
                              PlayerSource *page, int page_count) {
  if (*all_count + page_count > *all_cap) {
    *all_cap = int_max(*all_cap * 2, *all_count + page_count);
    PlayerSource *tmp = realloc(*all, *all_cap * sizeof(PlayerSource));
    if (!tmp) {
      free(*all);
      *all = NULL;
      return false;
    }
    *all = tmp;
  }
  memcpy(*all + *all_count, page, page_count * sizeof(PlayerSource));
  *all_count += page_count;
  return true;
}

static PlayerSource *browse_all_fetch_page(BlusoundPlayer *p,
                                           const char *current_key,
                                           int *page_count, char *next_key,
                                           size_t nk_size) {
  char params[STR_URL * 2] = "";
  if (current_key[0]) {
    char encoded[STR_URL * 2];
    url_encode_param(current_key, encoded, sizeof(encoded));
    snprintf(params, sizeof(params), "key=%s", encoded);
  }
  HttpBuffer buf = {0};
  LOG_INFO(player_logger, "browse_all params='%s'", params);
  if (!player_request(p, "/Browse", params[0] ? params : NULL, &buf)) {
    LOG_ERROR(player_logger, "browse_all request FAILED");
    return NULL;
  }
  LOG_INFO(player_logger, "browse_all response size=%zu", buf.size);
  PlayerSource *page =
      parse_browse_xml(buf.data, page_count, NULL, 0, next_key, nk_size);
  free(buf.data);
  return page;
}

PlayerSource *player_browse_all(BlusoundPlayer *p, const char *key,
                                int *total_count) {
  PlayerSource *all = NULL;
  int all_count = 0, all_cap = 0;
  char current_key[STR_URL] = "";
  if (key)
    safe_strcpy(current_key, key, sizeof(current_key));

  while (1) {
    int page_count = 0;
    char next_key[STR_URL] = "";
    PlayerSource *page = browse_all_fetch_page(p, current_key, &page_count,
                                               next_key, sizeof(next_key));

    if (page && page_count > 0) {
      if (!browse_all_append(&all, &all_count, &all_cap, page, page_count)) {
        free(page);
        *total_count = 0;
        return NULL;
      }
    }
    free(page);

    if (!next_key[0])
      break;
    safe_strcpy(current_key, next_key, sizeof(current_key));
  }
  *total_count = all_count;
  return all;
}

void player_get_nested(BlusoundPlayer *p, PlayerSource *source) {
  if (!source->browse_key[0])
    return;
  int count = 0;
  PlayerSource *children = player_browse(p, source->browse_key, &count);
  if (children && count > 0) {
    source->children = children;
    source->children_count = count;
    source->children_capacity = count;
  } else {
    free(children);
    LOG_WARN(player_logger, "No nested sources for %s", source->text);
  }
}

static bool search_ensure_capacity(PlayerSource **results, int *res_cap,
                                   int res_count, int needed) {
  if (res_count + needed <= *res_cap)
    return true;
  *res_cap = int_max(*res_cap * 2, res_count + needed);
  PlayerSource *tmp = realloc(*results, *res_cap * sizeof(PlayerSource));
  if (!tmp) {
    free(*results);
    *results = NULL;
    return false;
  }
  *results = tmp;
  return true;
}

static PlayerSource *search_filter_results(BlusoundPlayer *p, PlayerSource *raw,
                                           int raw_count, int *res_count) {
  PlayerSource *results = NULL;
  int count = 0, cap = 0;

  for (int i = 0; i < raw_count; i++) {
    PlayerSource *item = &raw[i];
    if (strcmp(item->text, SEARCH_SKIP_ARTISTS) == 0 ||
        strcmp(item->text, SEARCH_SKIP_PLAYLISTS) == 0)
      continue;

    if (strcmp(item->text, LIBRARY_CATEGORY_NAME) == 0 && item->browse_key[0]) {
      int lib_count = 0;
      PlayerSource *lib = player_browse(p, item->browse_key, &lib_count);
      if (lib && lib_count > 0) {
        if (!search_ensure_capacity(&results, &cap, count, lib_count)) {
          free(lib);
          *res_count = 0;
          return NULL;
        }
        memcpy(results + count, lib, lib_count * sizeof(PlayerSource));
        count += lib_count;
      }
      free(lib);
      continue;
    }
    if (!search_ensure_capacity(&results, &cap, count, 1)) {
      *res_count = 0;
      return NULL;
    }
    results[count++] = *item;
  }
  *res_count = count;
  return results;
}

PlayerSource *player_search(BlusoundPlayer *p, const char *search_key,
                            const char *query, int *count) {
  char enc_query[STR_URL];
  url_encode(query, enc_query, sizeof(enc_query));
  char enc_key[STR_URL * 2];
  url_encode_param(search_key, enc_key, sizeof(enc_key));
  char params[STR_URL * 3];
  snprintf(params, sizeof(params), "key=%s&q=%s", enc_key, enc_query);

  LOG_INFO(player_logger, "Searching '%s' with key '%s'", query, search_key);
  HttpBuffer buf = {0};
  if (!player_request(p, "/Browse", params, &buf)) {
    *count = 0;
    return NULL;
  }

  int raw_count = 0;
  char sk[STR_URL] = "";
  PlayerSource *raw =
      parse_browse_xml(buf.data, &raw_count, sk, sizeof(sk), NULL, 0);
  free(buf.data);

  PlayerSource *results = search_filter_results(p, raw, raw_count, count);
  free(raw);
  LOG_INFO(player_logger, "Found %d search results", *count);
  return results;
}

PlaylistEntry *player_get_playlist(BlusoundPlayer *p, int *count) {
  HttpBuffer buf = {0};
  if (!player_request(p, "/Playlist", NULL, &buf)) {
    *count = 0;
    return NULL;
  }

  PlaylistParseCtx ctx = {0};
  XML_Parser parser = XML_ParserCreate(NULL);
  XML_SetUserData(parser, &ctx);
  XML_SetElementHandler(parser, playlist_start, playlist_end);
  XML_SetCharacterDataHandler(parser, playlist_chars);
  XML_Parse(parser, buf.data, (int)buf.size, 1);
  XML_ParserFree(parser);
  free(buf.data);

  *count = ctx.count;
  return ctx.entries;
}

bool player_save_playlist(BlusoundPlayer *p, const char *name, char *msg,
                          size_t msg_size) {
  char encoded[STR_URL];
  url_encode(name, encoded, sizeof(encoded));
  char params[STR_URL];
  snprintf(params, sizeof(params), "name=%s", encoded);
  HttpBuffer buf = {0};
  if (!player_request(p, "/Save", params, &buf)) {
    snprintf(msg, msg_size, "Error saving playlist");
    return false;
  }
  /* Try to extract entry count */
  const char *entries = strstr(buf.data ? buf.data : "", "<entries>");
  if (entries) {
    entries += 9;
    snprintf(msg, msg_size, "Saved '%s' (%.*s tracks)", name,
             (int)(strchr(entries, '<') ? strchr(entries, '<') - entries : 0),
             entries);
  } else {
    snprintf(msg, msg_size, "Saved '%s'", name);
  }
  free(buf.data);
  return true;
}

PlayerSource *player_get_playlists(BlusoundPlayer *p, int *count) {
  return player_browse(p, "playlists", count);
}

bool player_delete_playlist(BlusoundPlayer *p, const char *name) {
  char encoded[STR_URL];
  url_encode(name, encoded, sizeof(encoded));
  char params[STR_URL];
  snprintf(params, sizeof(params), "name=%s", encoded);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/Delete", params, &buf);
  free(buf.data);
  return ok;
}

static PlayerSource *
browse_path_check_cache(BlusoundPlayer *p, const char *cache_key, int *count) {
  BrowseCacheEntry *bc = p->browse_cache;
  while (bc) {
    if (strcmp(bc->path_key, cache_key) == 0) {
      PlayerSource *result = player_browse_all(p, bc->browse_key, count);
      if (result && *count > 0)
        return result;
      return NULL;
    }
    bc = bc->next;
  }
  return NULL;
}

static bool browse_path_walk(BlusoundPlayer *p, const char **names,
                             int name_count, char *current_key,
                             size_t key_size) {
  for (int depth = 0; depth < name_count; depth++) {
    int items_count = 0;
    PlayerSource *items =
        player_browse(p, current_key[0] ? current_key : NULL, &items_count);
    if (!items || items_count == 0) {
      free(items);
      return false;
    }
    bool found = false;
    for (int i = 0; i < items_count; i++) {
      LOG_INFO(player_logger,
               "browse_path depth=%d item=%d text='%s' browseKey='%s'", depth,
               i, items[i].text, items[i].browse_key);
      if (str_contains_ci(items[i].text, names[depth]) &&
          items[i].browse_key[0]) {
        safe_strcpy(current_key, items[i].browse_key, key_size);
        found = true;
        LOG_INFO(player_logger, "browse_path matched '%s' at depth %d",
                 names[depth], depth);
        break;
      }
    }
    free(items);
    if (!found) {
      LOG_INFO(player_logger,
               "browse_path: '%s' not found at depth %d (%d items checked)",
               names[depth], depth, items_count);
      return false;
    }
  }
  return true;
}

PlayerSource *player_browse_path(BlusoundPlayer *p, const char **names,
                                 int name_count, bool *full_match, int *count) {
  char cache_key[STR_URL] = "";
  for (int i = 0; i < name_count; i++) {
    if (i > 0)
      strncat(cache_key, "/", sizeof(cache_key) - strlen(cache_key) - 1);
    strncat(cache_key, names[i], sizeof(cache_key) - strlen(cache_key) - 1);
  }

  PlayerSource *cached = browse_path_check_cache(p, cache_key, count);
  if (cached) {
    *full_match = true;
    return cached;
  }

  char current_key[STR_URL] = "";
  if (!browse_path_walk(p, names, name_count, current_key,
                        sizeof(current_key))) {
    *full_match = false;
    *count = 0;
    return NULL;
  }

  BrowseCacheEntry *entry = calloc(1, sizeof(BrowseCacheEntry));
  if (entry) {
    safe_strcpy(entry->path_key, cache_key, sizeof(entry->path_key));
    safe_strcpy(entry->browse_key, current_key, sizeof(entry->browse_key));
    entry->next = p->browse_cache;
    p->browse_cache = entry;
  }

  PlayerSource *result = player_browse_all(p, current_key, count);
  *full_match = (result && *count > 0);
  return result;
}

bool player_add_album_favourite(BlusoundPlayer *p, PlayerStatus *status,
                                char *msg, size_t msg_size) {
  if (!status->albumid[0] || !status->service[0]) {
    safe_strcpy(msg, "No album info available", msg_size);
    return false;
  }
  char params[STR_URL];
  snprintf(params, sizeof(params), "albumid=%s&service=%s", status->albumid,
           status->service);
  HttpBuffer buf = {0};
  bool ok = player_request(p, "/AddFavourite", params, &buf);
  free(buf.data);
  if (ok)
    snprintf(msg, msg_size, "Added to favourites: %s", status->album);
  else
    safe_strcpy(msg, "Error adding favourite", msg_size);
  return ok;
}

static void build_favourite_cm_key(PlayerStatus *status, char *cm_key,
                                   size_t cm_key_size) {
  char artist_enc[STR_URL];
  url_encode(status->artist, artist_enc, sizeof(artist_enc));
  snprintf(cm_key, cm_key_size,
           "%s:CM/%s-Album?albumid=%s&artist=%s&artistid=%s", status->service,
           status->service, status->albumid, artist_enc, status->artistid);
}

static bool favourite_find_and_execute(BlusoundPlayer *p, PlayerSource *items,
                                       int items_count, char *msg,
                                       size_t msg_size, const char *album) {
  for (int i = 0; i < items_count; i++) {
    if (str_contains_ci(items[i].text, "favourite") && items[i].play_url[0]) {
      char action_url[STR_URL * 2];
      snprintf(action_url, sizeof(action_url), "%s%s", p->base_url,
               items[i].play_url);
      HttpBuffer abuf = {0};
      bool ok = http_get(action_url, HTTP_TIMEOUT_PLAYER, &abuf);
      free(abuf.data);
      free(items);
      if (ok)
        snprintf(msg, msg_size, "Removed from favourites: %s", album);
      else
        safe_strcpy(msg, "Error removing favourite", msg_size);
      return ok;
    }
  }
  free(items);
  safe_strcpy(msg, "Favourite action not found in context menu", msg_size);
  return false;
}

bool player_remove_album_favourite(BlusoundPlayer *p, PlayerStatus *status,
                                   char *msg, size_t msg_size) {
  if (!status->albumid[0] || !status->service[0] || !status->artistid[0]) {
    safe_strcpy(msg, "No album info available", msg_size);
    return false;
  }
  char cm_key[STR_URL];
  build_favourite_cm_key(status, cm_key, sizeof(cm_key));
  char cm_enc[STR_URL * 2];
  url_encode_param(cm_key, cm_enc, sizeof(cm_enc));
  char params[STR_URL * 2];
  snprintf(params, sizeof(params), "key=%s", cm_enc);

  HttpBuffer buf = {0};
  if (!player_request(p, "/Browse", params, &buf)) {
    safe_strcpy(msg, "Error accessing context menu", msg_size);
    return false;
  }

  int items_count = 0;
  PlayerSource *items =
      parse_browse_xml(buf.data, &items_count, NULL, 0, NULL, 0);
  free(buf.data);

  return favourite_find_and_execute(p, items, items_count, msg, msg_size,
                                    status->album);
}

static bool favourite_try_action_url(BlusoundPlayer *p, PlayerSource *source,
                                     PlayerSource *item, bool add, char *msg,
                                     size_t msg_size) {
  if (!item->play_url[0])
    return false;
  char action_url[STR_URL * 2];
  snprintf(action_url, sizeof(action_url), "%s%s", p->base_url, item->play_url);
  HttpBuffer abuf = {0};
  bool ok = http_get(action_url, HTTP_TIMEOUT_PLAYER, &abuf);
  free(abuf.data);
  if (ok) {
    source->is_favourite = add;
    snprintf(msg, msg_size, "%s favourites: %s",
             add ? "Added to" : "Removed from", source->text);
  } else {
    safe_strcpy(msg, "Error toggling favourite", msg_size);
  }
  return true; /* handled regardless of ok */
}

static bool favourite_try_browse_key(BlusoundPlayer *p, PlayerSource *source,
                                     PlayerSource *item, bool add, char *msg,
                                     size_t msg_size) {
  if (!item->browse_key[0])
    return false;
  char bparams[STR_URL];
  snprintf(bparams, sizeof(bparams), "key=%s", item->browse_key);
  HttpBuffer bbuf = {0};
  bool ok = player_request(p, "/Browse", bparams, &bbuf);
  free(bbuf.data);
  if (ok) {
    source->is_favourite = add;
    snprintf(msg, msg_size, "%s favourites: %s",
             add ? "Added to" : "Removed from", source->text);
  }
  return true; /* handled regardless of ok */
}

bool player_toggle_favourite(BlusoundPlayer *p, PlayerSource *source, bool add,
                             char *msg, size_t msg_size) {
  if (!source->context_menu_key[0]) {
    safe_strcpy(msg, "No context menu available for this item", msg_size);
    return false;
  }

  char cm_enc[STR_URL * 2];
  url_encode_param(source->context_menu_key, cm_enc, sizeof(cm_enc));
  char params[STR_URL * 2];
  snprintf(params, sizeof(params), "key=%s", cm_enc);
  HttpBuffer buf = {0};
  if (!player_request(p, "/Browse", params, &buf)) {
    safe_strcpy(msg, "Error fetching context menu", msg_size);
    return false;
  }

  int items_count = 0;
  PlayerSource *items =
      parse_browse_xml(buf.data, &items_count, NULL, 0, NULL, 0);
  free(buf.data);

  for (int i = 0; i < items_count; i++) {
    if (!str_contains_ci(items[i].text, "favourite"))
      continue;
    if (favourite_try_action_url(p, source, &items[i], add, msg, msg_size) ||
        favourite_try_browse_key(p, source, &items[i], add, msg, msg_size)) {
      free(items);
      return true;
    }
  }
  free(items);
  safe_strcpy(msg, "Favourite not found in context menu", msg_size);
  return false;
}

bool player_get_queue_actions(BlusoundPlayer *p, PlayerSource *source,
                              QueueActions *out) {
  memset(out, 0, sizeof(QueueActions));
  if (!source->context_menu_key[0])
    return false;

  char cm_enc[STR_URL * 2];
  url_encode_param(source->context_menu_key, cm_enc, sizeof(cm_enc));
  char params[STR_URL * 2];
  snprintf(params, sizeof(params), "key=%s", cm_enc);
  HttpBuffer buf = {0};
  if (!player_request(p, "/Browse", params, &buf))
    return false;

  int items_count = 0;
  PlayerSource *items =
      parse_browse_xml(buf.data, &items_count, NULL, 0, NULL, 0);
  free(buf.data);

  for (int i = 0; i < items_count; i++) {
    /* actionURL is mapped to play_url in our XML parser */
    if (!items[i].play_url[0])
      continue;
    char lower[STR_MEDIUM];
    safe_strcpy(lower, items[i].text, sizeof(lower));
    for (char *c = lower; *c; c++)
      *c = tolower((unsigned char)*c);

    if (strstr(lower, "play now"))
      safe_strcpy(out->play_now, items[i].play_url, sizeof(out->play_now));
    else if (strstr(lower, "add next"))
      safe_strcpy(out->add_next, items[i].play_url, sizeof(out->add_next));
    else if (strstr(lower, "add last"))
      safe_strcpy(out->add_last, items[i].play_url, sizeof(out->add_last));
  }
  free(items);
  return true;
}
