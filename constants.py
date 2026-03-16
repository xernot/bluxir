# -*- coding: utf-8 -*-
#
# bluxir - BluOS Terminal Controller
# Copyright (C) 2026 xir
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#

# ── Logging ──────────────────────────────────────────────────────────────────

# Maximum size of each log file before rotation (bytes)
LOG_MAX_BYTES = 1024 * 1024

# Number of rotated log backups to keep
LOG_BACKUP_COUNT = 1

# Log line format: timestamp - logger name - level - message
LOG_FORMAT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'

# Shorter log format without logger name (used by musicbrainz)
LOG_FORMAT_SHORT = '%(asctime)s - %(levelname)s - %(message)s'

# Timestamp format for log entries
LOG_DATE_FORMAT = '%Y-%m-%d %H:%M:%S'

# ── Timing ───────────────────────────────────────────────────────────────────

# Curses input polling interval (ms) — controls UI responsiveness
CURSES_POLL_MS = 100

# Blocking mode for curses input (-1 = wait indefinitely)
INPUT_BLOCKING = -1

# Minimum seconds between MusicBrainz API requests (rate limit compliance)
MUSICBRAINZ_RATE_LIMIT = 1.0

# HTTP timeout for player API requests (seconds)
HTTP_TIMEOUT_PLAYER = 5

# HTTP timeout for MusicBrainz API requests (seconds)
HTTP_TIMEOUT_MUSICBRAINZ = 5

# HTTP timeout for Wikipedia API requests (seconds)
HTTP_TIMEOUT_WIKIPEDIA = 5

# HTTP timeout for single-field OpenAI requests (seconds)
HTTP_TIMEOUT_OPENAI = 10

# HTTP timeout for combined OpenAI requests (seconds)
HTTP_TIMEOUT_OPENAI_COMBINED = 15

# HTTP timeout for station info OpenAI requests (seconds)
HTTP_TIMEOUT_OPENAI_STATION = 15

# HTTP timeout for LRCLIB lyrics requests (seconds)
HTTP_TIMEOUT_LYRICS = 10

# HTTP timeout for health check web interface requests (seconds)
HTTP_TIMEOUT_HEALTH = 10

# Delay between retries when initializing sources (seconds)
SOURCE_INIT_RETRY_DELAY = 1

# Interval between player status polls in the main loop (seconds)
STATUS_POLL_INTERVAL = 3

# Interval for local progress counter increment (seconds)
PROGRESS_INCREMENT_INTERVAL = 1

# ── Source Initialization ────────────────────────────────────────────────────

# Maximum number of retries when fetching initial source list
SOURCE_INIT_MAX_RETRIES = 3

# ── API Endpoints ────────────────────────────────────────────────────────────

# MusicBrainz API base URL
MUSICBRAINZ_BASE_URL = "https://musicbrainz.org/ws/2"

# User-Agent header sent with all external API requests
MUSICBRAINZ_USER_AGENT = "bluxir/1.2 (https://github.com/xernot/bluxir)"

# Wikipedia REST API summary endpoint template — {lang} and {title} are substituted
WIKIPEDIA_API_TEMPLATE = "https://{lang}.wikipedia.org/api/rest_v1/page/summary/{title}"

# OpenAI chat completions endpoint
OPENAI_API_URL = "https://api.openai.com/v1/chat/completions"

# LRCLIB lyrics lookup endpoint
LRCLIB_API_URL = "https://lrclib.net/api/get"

# BluOS API port on local network players
BLUOS_API_PORT = 11000

# mDNS service type for discovering Blusound players
MDNS_SERVICE_TYPE = "_musc._tcp.local."

# ── AI Parameters ────────────────────────────────────────────────────────────

# Default OpenAI model when none is configured
DEFAULT_OPENAI_MODEL = "gpt-4o-mini"

# Max tokens for single track info responses
TRACK_INFO_MAX_TOKENS = 200

# Temperature for single track info (higher = more creative)
TRACK_INFO_TEMPERATURE = 0.7

# Max tokens for combined album+track info responses
COMBINED_INFO_MAX_TOKENS = 300

# Temperature for combined info (lower = more factual)
COMBINED_INFO_TEMPERATURE = 0.3

# Max tokens for station info responses
STATION_INFO_MAX_TOKENS = 300

# Temperature for station info (lower = more factual)
STATION_INFO_TEMPERATURE = 0.3

# ── AI Prompts ───────────────────────────────────────────────────────────────

# Prompt template for single track info requests — {title} and {artist} are substituted
TRACK_INFO_PROMPT = 'Tell me about the song "{title}" by {artist}.'

# Prompt template for station info requests — {station_name} is substituted
STATION_INFO_PROMPT = (
    'Radio station: "{station_name}".\n\n'
    'Tell me about this radio station in 3-5 sentences. '
    'Include: what kind of station it is, what country/region it serves, '
    'what type of music or content it broadcasts, and any notable facts.\n\n'
    'IMPORTANT: Only include facts you are certain about. If you don\'t know '
    'this station, respond with just "-".'
)

# Prompt template for combined album+track info — {title}, {artist}, {album}, {track_info_instruction} substituted
COMBINED_INFO_PROMPT = (
    'Song: "{title}" by {artist}, from the album "{album}".\n\n'
    'Return a JSON object with these exact keys:\n'
    '- "year": the original release year of the album (4-digit string, or "-" if unknown)\n'
    '- "label": the record label that released the album (or "-" if unknown)\n'
    '- "genre": the genre(s) of this album, comma-separated (or "-" if unknown)\n'
    '- "track_info": {track_info_instruction}\n\n'
    'IMPORTANT: Only include facts you are certain about. If you don\'t know the artist or song, '
    'set "track_info" to "-" rather than guessing. Never invent musician names, studios, or details.\n\n'
    'Respond ONLY with the JSON object, no markdown, no explanation.'
)

# ── MusicBrainz Scoring ─────────────────────────────────────────────────────

# Score awarded for an exact album title match
EXACT_TITLE_SCORE = 100

# Score for album title partially contained in release title
PARTIAL_TITLE_SCORE = 60

# Score for release title partially contained in album title
REVERSE_PARTIAL_SCORE = 50

# Score multiplier per overlapping word between title and album
WORD_OVERLAP_MULTIPLIER = 10

# Score for exact artist name match
EXACT_ARTIST_SCORE = 50

# Score for partial artist name match
PARTIAL_ARTIST_SCORE = 30

# Divisor applied to MusicBrainz's own relevance score
MB_SCORE_DIVISOR = 10

# Maximum number of releases to fetch per MusicBrainz search
MB_SEARCH_LIMIT = 25

# Maximum entries in the in-memory MusicBrainz/AI result cache
MB_CACHE_SIZE = 256

# Number of top genre tags to display from MusicBrainz
TOP_GENRE_COUNT = 3

# ── Wikipedia ────────────────────────────────────────────────────────────────

# Language editions to try when fetching Wikipedia summaries (in priority order)
WIKIPEDIA_LANGUAGES = ["en", "de"]

# ── UI Display ───────────────────────────────────────────────────────────────

# Maximum characters for album/source names in the browse list
SOURCE_NAME_MAX_LENGTH = 100

# Width of the volume bar in the header (characters)
VOLUME_BAR_WIDTH = 12

# Volume change per UP/DOWN keypress (percent)
VOLUME_INCREMENT = 5

# Left panel width as a percentage of terminal width
LAYOUT_LEFT_PCT = 60

# Rows reserved below the source list for footer/status
SOURCE_LIST_HEIGHT_OFFSET = 10

# Rows reserved below the search results for footer/status
SEARCH_HEIGHT_OFFSET = 8

# Number of lines to scroll lyrics per PgUp/PgDn keypress
LYRICS_SCROLL_STEP = 5

# Maximum digits accepted when entering a track number
TRACK_INPUT_MAX_DIGITS = 5

# Width of each column in the help screen modal
HELP_COL_WIDTH = 30

# Offset from column edge to the vertical divider in help modal
HELP_DIV_OFFSET = 33

# Header message display duration (seconds)
HEADER_MESSAGE_DURATION = 2

# Duration to highlight a changed control in green (seconds)
HIGHLIGHT_DURATION = 5

# ── UI Strings ───────────────────────────────────────────────────────────────

# Footer help text shown on the main player screen
FOOTER_HELP = "(s) search  (f) fav  (l) playlists  (w) save  (c) cover  (t) lyrics  (+/-) fav  (i) source  (h) health  (?) help  (q) quit"

# Version string shown in the footer
VERSION_STRING = "bluxir v2.2"

# Attribution line shown in help/about modals
ABOUT_ATTRIBUTION = "(c) written by xir - under GPL"

# Project URL shown in help/about modals
PROJECT_URL = "https://github.com/xernot/bluxir"

# Browse source selection instructions (line 1)
BROWSE_INSTRUCTIONS_1 = "UP/DOWN: select, ENTER: play, RIGHT: expand, LEFT: back"

# Browse source selection instructions (line 2)
BROWSE_INSTRUCTIONS_2 = "s: search, /: filter, n/p: next/prev page, +: add fav, -: remove fav, b: back"

# Browse sort instructions
BROWSE_SORT_INSTRUCTIONS = "Sort: (t) title  (a) artist  (o) original"

# Search source selection instructions
SEARCH_SOURCE_INSTRUCTIONS = "UP/DOWN: navigate, ENTER: select, b: back"

# Search results instructions
SEARCH_RESULTS_INSTRUCTIONS = "UP/DOWN: navigate, ENTER: play, RIGHT: expand, b: back"

# Lyrics attribution text
LYRICS_ATTRIBUTION = "(lyrics from lrclib.net)"

# Lyrics scroll hint appended when content overflows
LYRICS_SCROLL_HINT = "  [PgUp/PgDn to scroll]"

# Discovery screen hint
DISCOVERY_HINT = "Discovering Blusound players..."

# Terminal window title
TERMINAL_TITLE = "\033]0;bluxir\007"

# Pretty print popup title bar text
PRETTY_PRINT_TITLE = " Player State (UP/DOWN scroll, 'q' to close) "

# Health check overlay title (base and status-ok variant)
HEALTH_CHECK_TITLE = "Player Health Check"
HEALTH_CHECK_TITLE_OK = "Player Health Check - Status OK"

# Extra width factor for the health check modal (1.2 = 20% wider than content)
HEALTH_WIDTH_FACTOR = 1.2

# Diagnostics keys to skip in the health check overlay
HEALTH_SKIP_KEYS = {'Total Songs', 'Other Players'}

# Header message shown after a successful health check
HEALTH_STATUS_OK = "Status OK"

# Exact status text when no firmware update is available
HEALTH_NO_UPDATE = "no update available"

# Queue action dialog prompt
QUEUE_ACTION_PROMPT = "(1) Play now  (2) Add next  (3) Add last  (ESC) Cancel"

# ── Search ───────────────────────────────────────────────────────────────────

# Categories to skip when displaying search results
SEARCH_SKIP_CATEGORIES = {'Artists', 'Playlists'}

# Category name that triggers expanded library search
LIBRARY_CATEGORY_NAME = "Library"

# Services that support search (in display order)
SEARCH_SERVICES = ('Qobuz:', 'TuneIn:')

# ── Help Screen Keybindings ──────────────────────────────────────────────────

# Left column of the help screen — player/playback controls
HELP_LEFT_KEYS = [
    ("UP/DOWN", "Adjust volume"),
    ("SPACE", "Play/Pause"),
    ("ENTER", "Play / Add next / Add last"),
    (">/<", "Skip/Previous track"),
    ("g", "Go to track number"),
    ("m", "Toggle mute"),
    ("r", "Cycle repeat"),
    ("x", "Toggle shuffle"),
    ("+/-", "Add/Remove favourite"),
    ("ESC", "Cancel"),
]

# Right column of the help screen — navigation/display controls
HELP_RIGHT_KEYS = [
    ("i", "Select input"),
    ("s", "Search"),
    ("f", "Qobuz favorites"),
    ("l", "Load playlist"),
    ("w", "Save playlist"),
    ("c", "Toggle cover art"),
    ("t", "Toggle lyrics"),
    ("PgUp/PgDn", "Scroll lyrics"),
    ("h", "Health check"),
    ("p", "Pretty print"),
    ("b", "Back to player list"),
    ("q", "Quit"),
]

# Player selector help screen entries
SELECTOR_SHORTCUTS = [
    ("UP/DOWN", "Select player"),
    ("ENTER", "Activate player"),
    ("q", "Quit"),
]
