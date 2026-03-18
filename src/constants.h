/*
 * bluxir - BluOS Terminal Controller
 * Copyright (C) 2026 xir
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

/* ── Logging ────────────────────────────────────────────────────────────── */

/* Maximum size of each log file before rotation (bytes) */
#define LOG_MAX_BYTES (1024 * 1024)

/* Number of rotated log backups to keep */
#define LOG_BACKUP_COUNT 1

/* ── Timing ─────────────────────────────────────────────────────────────── */

/* Curses input polling interval (ms) — controls UI responsiveness */
#define CURSES_POLL_MS 100

/* Blocking mode for curses input (-1 = wait indefinitely) */
#define INPUT_BLOCKING (-1)

/* Minimum seconds between MusicBrainz API requests (rate limit compliance) */
#define MUSICBRAINZ_RATE_LIMIT 1.0

/* HTTP timeout for player API requests (seconds) */
#define HTTP_TIMEOUT_PLAYER 5L

/* HTTP timeout for MusicBrainz API requests (seconds) */
#define HTTP_TIMEOUT_MUSICBRAINZ 5L

/* HTTP timeout for Wikipedia API requests (seconds) */
#define HTTP_TIMEOUT_WIKIPEDIA 5L

/* HTTP timeout for single-field OpenAI requests (seconds) */
#define HTTP_TIMEOUT_OPENAI 10L

/* HTTP timeout for combined OpenAI requests (seconds) */
#define HTTP_TIMEOUT_OPENAI_COMBINED 15L

/* HTTP timeout for station info OpenAI requests (seconds) */
#define HTTP_TIMEOUT_OPENAI_STATION 15L

/* HTTP timeout for LRCLIB lyrics requests (seconds) */
#define HTTP_TIMEOUT_LYRICS 10L

/* HTTP timeout for health check web interface requests (seconds) */
#define HTTP_TIMEOUT_HEALTH 10L

/* Delay between retries when initializing sources (seconds) */
#define SOURCE_INIT_RETRY_DELAY 1

/* Interval between player status polls in the main loop (seconds) */
#define STATUS_POLL_INTERVAL 3

/* Interval for local progress counter increment (seconds) */
#define PROGRESS_INCREMENT_INTERVAL 1

/* ── Source Initialization ──────────────────────────────────────────────── */

/* Maximum number of retries when fetching initial source list */
#define SOURCE_INIT_MAX_RETRIES 3

/* ── API Endpoints ──────────────────────────────────────────────────────── */

/* MusicBrainz API base URL */
#define MUSICBRAINZ_BASE_URL "https://musicbrainz.org/ws/2"

/* User-Agent header sent with all external API requests */
#define MUSICBRAINZ_USER_AGENT "bluxir/1.2 (https://github.com/xernot/bluxir)"

/* Wikipedia REST API summary endpoint template — %s = lang, %s = title */
#define WIKIPEDIA_API_TEMPLATE "https://%s.wikipedia.org/api/rest_v1/page/summary/%s"

/* OpenAI chat completions endpoint */
#define OPENAI_API_URL "https://api.openai.com/v1/chat/completions"

/* LRCLIB lyrics lookup endpoint */
#define LRCLIB_API_URL "https://lrclib.net/api/get"

/* BluOS API port on local network players */
#define BLUOS_API_PORT 11000

/* mDNS service type for discovering Blusound players */
#define MDNS_SERVICE_TYPE "_musc._tcp"

/* ── AI Parameters ──────────────────────────────────────────────────────── */

/* Default OpenAI model when none is configured */
#define DEFAULT_OPENAI_MODEL "gpt-4o-mini"

/* Max tokens for single track info responses */
#define TRACK_INFO_MAX_TOKENS 200

/* Temperature for single track info (higher = more creative) */
#define TRACK_INFO_TEMPERATURE 0.7

/* Max tokens for combined album+track info responses */
#define COMBINED_INFO_MAX_TOKENS 300

/* Temperature for combined info (lower = more factual) */
#define COMBINED_INFO_TEMPERATURE 0.3

/* Max tokens for station info responses */
#define STATION_INFO_MAX_TOKENS 300

/* Temperature for station info (lower = more factual) */
#define STATION_INFO_TEMPERATURE 0.3

/* ── MusicBrainz Scoring ────────────────────────────────────────────────── */

/* Score awarded for an exact album title match */
#define EXACT_TITLE_SCORE 100

/* Score for album title partially contained in release title */
#define PARTIAL_TITLE_SCORE 60

/* Score for release title partially contained in album title */
#define REVERSE_PARTIAL_SCORE 50

/* Score multiplier per overlapping word between title and album */
#define WORD_OVERLAP_MULTIPLIER 10

/* Score for exact artist name match */
#define EXACT_ARTIST_SCORE 50

/* Score for partial artist name match */
#define PARTIAL_ARTIST_SCORE 30

/* Divisor applied to MusicBrainz's own relevance score */
#define MB_SCORE_DIVISOR 10

/* Maximum number of releases to fetch per MusicBrainz search */
#define MB_SEARCH_LIMIT 25

/* Maximum entries in the in-memory MusicBrainz/AI result cache */
#define MB_CACHE_SIZE 256

/* Number of top genre tags to display from MusicBrainz */
#define TOP_GENRE_COUNT 3

/* ── Wikipedia ──────────────────────────────────────────────────────────── */

/* Language editions to try when fetching Wikipedia summaries */
#define WIKIPEDIA_LANG_COUNT 2
static const char *WIKIPEDIA_LANGUAGES[WIKIPEDIA_LANG_COUNT] = {"en", "de"};

/* ── UI Display ─────────────────────────────────────────────────────────── */

/* Maximum characters for album/source names in the browse list */
#define SOURCE_NAME_MAX_LENGTH 100

/* Width of the volume bar in the header (characters) */
#define VOLUME_BAR_WIDTH 12

/* Volume change per UP/DOWN keypress (percent) */
#define VOLUME_INCREMENT 5

/* Left panel width as a percentage of terminal width */
#define LAYOUT_LEFT_PCT 60

/* Rows reserved below the source list for footer/status */
#define SOURCE_LIST_HEIGHT_OFFSET 10

/* Rows reserved below the search results for footer/status */
#define SEARCH_HEIGHT_OFFSET 8

/* Number of lines to scroll lyrics per PgUp/PgDn keypress */
#define LYRICS_SCROLL_STEP 5

/* Maximum digits accepted when entering a track number */
#define TRACK_INPUT_MAX_DIGITS 5

/* Width of each column in the help screen modal */
#define HELP_COL_WIDTH 30

/* Offset from column edge to the vertical divider in help modal */
#define HELP_DIV_OFFSET 33

/* Header message display duration (seconds) */
#define HEADER_MESSAGE_DURATION 2

/* Duration to highlight a changed control in green (seconds) */
#define HIGHLIGHT_DURATION 5

/* Extra width factor for the health check modal (1.2 = 20% wider than content) */
#define HEALTH_WIDTH_FACTOR 1.2

/* ── UI Strings ─────────────────────────────────────────────────────────── */

/* Footer help text shown on the main player screen */
#define FOOTER_HELP "(s) search  (f) fav  (l) playlists  (w) save  (c) cover  (t) lyrics  (+/-) fav  (i) source  (h) health  (?) help  (q) quit"

/* Version string shown in the footer */
#define VERSION_STRING "bluxir v3.0"

/* Attribution line shown in help/about modals */
#define ABOUT_ATTRIBUTION "(c) written by xir - under GPL"

/* Project URL shown in help/about modals */
#define PROJECT_URL "https://github.com/xernot/bluxir"

/* Browse source selection instructions (line 1) */
#define BROWSE_INSTRUCTIONS_1 "UP/DOWN: select, ENTER: play, RIGHT: expand, LEFT: back"

/* Browse source selection instructions (line 2) */
#define BROWSE_INSTRUCTIONS_2 "s: search, /: filter, n/p: next/prev page, +: add fav, -: remove fav, b: back"

/* Browse sort instructions */
#define BROWSE_SORT_INSTRUCTIONS "Sort: (t) title  (a) artist  (o) original"

/* Search source selection instructions */
#define SEARCH_SOURCE_INSTRUCTIONS "UP/DOWN: navigate, ENTER: select, b: back"

/* Search results instructions */
#define SEARCH_RESULTS_INSTRUCTIONS "UP/DOWN: navigate, ENTER: play, RIGHT: expand, b: back"

/* Lyrics attribution text */
#define LYRICS_ATTRIBUTION "(lyrics from lrclib.net)"

/* Lyrics scroll hint appended when content overflows */
#define LYRICS_SCROLL_HINT "  [PgUp/PgDn to scroll]"

/* Discovery screen hint */
#define DISCOVERY_HINT "Discovering Blusound players..."

/* Terminal window title escape sequence */
#define TERMINAL_TITLE "\033]0;bluxir\007"

/* Pretty print popup title bar text */
#define PRETTY_PRINT_TITLE " Player State (UP/DOWN scroll, 'q' to close) "

/* Health check overlay title (base and status-ok variant) */
#define HEALTH_CHECK_TITLE "Player Health Check"
#define HEALTH_CHECK_TITLE_OK "Player Health Check - Status OK"

/* Diagnostics keys to skip in the health check overlay */
#define HEALTH_SKIP_KEY_TOTAL_SONGS "Total Songs"
#define HEALTH_SKIP_KEY_OTHER_PLAYERS "Other Players"

/* Header message shown after a successful health check */
#define HEALTH_STATUS_OK "Status OK"

/* Exact status text when no firmware update is available */
#define HEALTH_NO_UPDATE "no update available"

/* Queue action dialog prompt */
#define QUEUE_ACTION_PROMPT "(1) Play now  (2) Add next  (3) Add last  (ESC) Cancel"

/* ── Search ─────────────────────────────────────────────────────────────── */

/* Categories to skip when displaying search results */
#define SEARCH_SKIP_ARTISTS "Artists"
#define SEARCH_SKIP_PLAYLISTS "Playlists"

/* Category name that triggers expanded library search */
#define LIBRARY_CATEGORY_NAME "Library"

/* Services that support search (in display order) */
#define SEARCH_SERVICE_QOBUZ "Qobuz:"
#define SEARCH_SERVICE_TUNEIN "TuneIn:"
#define SEARCH_SERVICES_COUNT 2
static const char *SEARCH_SERVICES[SEARCH_SERVICES_COUNT] = {"Qobuz:", "TuneIn:"};

/* ── String Buffer Sizes ────────────────────────────────────────────────── */

/* Maximum length for general string fields in structs */
#define STR_SHORT 64
#define STR_MEDIUM 256
#define STR_LONG 512
#define STR_URL 1024
#define STR_TEXT 4096
#define STR_LARGE 16384

/* Maximum path length for log/config files */
#define PATH_MAX_LEN 512

/* ── Help Screen Keybindings ────────────────────────────────────────────── */

typedef struct {
    const char *key;
    const char *description;
} KeyBinding;

#define HELP_LEFT_COUNT 10
static const KeyBinding HELP_LEFT_KEYS[HELP_LEFT_COUNT] = {
    {"UP/DOWN",  "Adjust volume"},
    {"SPACE",    "Play/Pause"},
    {"ENTER",    "Play / Add next / Add last"},
    {">/<",      "Skip/Previous track"},
    {"g",        "Go to track number"},
    {"m",        "Toggle mute"},
    {"r",        "Cycle repeat"},
    {"x",        "Toggle shuffle"},
    {"+/-",      "Add/Remove favourite"},
    {"ESC",      "Cancel"},
};

#define HELP_RIGHT_COUNT 12
static const KeyBinding HELP_RIGHT_KEYS[HELP_RIGHT_COUNT] = {
    {"i",        "Select input"},
    {"s",        "Search"},
    {"f",        "Qobuz favorites"},
    {"l",        "Load playlist"},
    {"w",        "Save playlist"},
    {"c",        "Toggle cover art"},
    {"t",        "Toggle lyrics"},
    {"PgUp/PgDn","Scroll lyrics"},
    {"h",        "Health check"},
    {"p",        "Pretty print"},
    {"b",        "Back to player list"},
    {"q",        "Quit"},
};

#define SELECTOR_SHORTCUTS_COUNT 3
static const KeyBinding SELECTOR_SHORTCUTS[SELECTOR_SHORTCUTS_COUNT] = {
    {"UP/DOWN",  "Select player"},
    {"ENTER",    "Activate player"},
    {"q",        "Quit"},
};

/* ── AI Prompt Templates ────────────────────────────────────────────────── */

/* Track info prompt — use snprintf with title, artist */
#define TRACK_INFO_PROMPT_FMT "Tell me about the song \"%s\" by %s."

/* Station info prompt — use snprintf with station_name */
#define STATION_INFO_PROMPT_FMT \
    "Radio station: \"%s\".\n\n" \
    "Tell me about this radio station in 3-5 sentences. " \
    "Include: what kind of station it is, what country/region it serves, " \
    "what type of music or content it broadcasts, and any notable facts.\n\n" \
    "IMPORTANT: Only include facts you are certain about. If you don't know " \
    "this station, respond with just \"-\"."

/* Combined info prompt — use snprintf with title, artist, album, track_info_instruction */
#define COMBINED_INFO_PROMPT_FMT \
    "Song: \"%s\" by %s, from the album \"%s\".\n\n" \
    "Return a JSON object with these exact keys:\n" \
    "- \"year\": the original release year of the album (4-digit string, or \"-\" if unknown)\n" \
    "- \"label\": the record label that released the album (or \"-\" if unknown)\n" \
    "- \"genre\": the genre(s) of this album, comma-separated (or \"-\" if unknown)\n" \
    "- \"track_info\": %s\n\n" \
    "IMPORTANT: Only include facts you are certain about. If you don't know the artist or song, " \
    "set \"track_info\" to \"-\" rather than guessing. Never invent musician names, studios, or details.\n\n" \
    "Respond ONLY with the JSON object, no markdown, no explanation."

#endif /* CONSTANTS_H */
