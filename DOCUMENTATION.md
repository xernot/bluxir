# bluxir - Technical Documentation

## Overview

bluxir is a C terminal controller for Blusound network music streamers. It communicates with BluOS players over HTTP (port 11000), renders a curses-based split-screen TUI, supports multiroom grouping with per-player volume control, and fetches metadata from external APIs in background threads.

## Architecture

```
+------------------+     +------------------+     +-------------------+
|     main.c       |---->|    player.c      |---->| BluOS HTTP API    |
| (curses + input) |     | (libcurl + expat)|     | (port 11000)      |
+------------------+     +------------------+     +-------------------+
        |                        |
        |                        +---> discover.c (Avahi mDNS)
        |
        +---> ui.c / ui_player.c / ui_browse.c / ui_search.c
        |     (group manager, volume overlay)
        |
        +---> metadata.c ------> MusicBrainz API
        |     (libcurl + cJSON)  OpenAI API
        |                        LRCLIB API
        |                        Wikipedia API
        |
        +---> cover_art.c (stb_image + ncurses 256-color)
        |
        +---> config.c (cJSON) ---> config.json / ~/.bluxir.json
        |
        +---> cache.c (LRU hash table)
        |
        +---> logger.c (rotating file logger)
```

## Key Data Structures (types.h)

### PlayerStatus

Holds all fields from the `/Status` XML response. Fixed-size `char[]` arrays eliminate per-field malloc:

- `name[256]`, `artist[256]`, `album[256]` - current track
- `state[64]` - play, pause, stop, stream
- `volume` (int), `mute` (bool), `repeat` (int), `shuffle` (bool)
- `secs`, `totlen` - playback position
- `stream_format[256]` - codec and bitrate info
- `image[1024]` - cover art URL

### PlayerSource

Represents a browsable/playable item from the Browse API:

- `text[256]`, `text2[256]` - display name and secondary text
- `browse_key[1024]` - key for navigating deeper
- `play_url[1024]` - URL for direct playback
- `context_menu_key[1024]` - key for context menu (favourites, queue actions)
- `search_key[1024]` - search capability key
- `children` / `children_count` - nested items (dynamic array)

### GroupInfo

Holds the current multiroom group state, cached in AppState and refreshed every 15 seconds:

- `master_ip[256]` — IP of the master player (empty if standalone or this player is master)
- `slave_ips[16][256]` — IPs of grouped slave players
- `slave_names[16][256]` — display names of grouped slaves (from SyncStatus XML)
- `slave_count` — number of slaves in the group

### AppState

Single struct replacing the Python `BlusoundCLI` class. Contains all UI state, player references, metadata, group info, locks, and highlight times. Stack-allocated in `main()`.

Source selection state includes sort/filter support:
- `source_sort` (`'o'`/`'t'`/`'a'`) — current sort mode
- `source_filter[256]` — active filter text (empty = no filter)
- `unsorted_sources` — backup pointer to original data before sort/filter was applied
- When sort or filter is activated, `source_ensure_backup()` saves the original `current_sources` pointer and creates a working copy. `source_apply_sort_filter()` rebuilds the copy by filtering from backup then sorting with `qsort`. State is cleared on navigate deeper/back/exit.

Multiroom state:
- `group_info` (GroupInfo) — cached group info, polled every 15 seconds
- `last_group_update_time` — timer for group info polling

### LRUCache

Thread-safe hash table (256 buckets, djb2 hash) with doubly-linked list for LRU ordering. Internal pthread mutex. Entries own copies of keys and values, freed on eviction or destroy.

## Memory Management

- **Structs**: fixed-size `char[]` fields (no per-field malloc)
- **Dynamic arrays**: `count/capacity` pattern with `realloc`
- **Heap blobs**: `lyrics_text`, `wiki_text`, `cover_art_raw` - malloc'd, freed on track change, protected by `data_lock`
- **libcurl buffers**: write-callback to dynamic buffer, freed by caller after parsing
- **Cache**: entries own copies of values, freed on eviction or `cache_destroy()`
- **AppState**: stack-allocated in `main()`, cleaned up via `app_state_destroy()`

## Threading Model

Matches the Python threading model:

- **Main thread**: curses event loop (100ms poll), status polling (3s), progress increment (1s)
- **Detached pthreads**: metadata fetch, lyrics fetch, cover art download, station info
- **One global mutex** (`data_lock`): protects mb_info, wiki_text, lyrics_text, cover_art, loading flags
- **Discovery mutex**: separate lock for player list updates (in DiscoveryState)
- **Cache mutex**: internal to cache module
- **Rate limiter mutex**: internal to metadata module
- **libcurl**: each thread creates its own `CURL*` handle; `curl_global_init()` once in `main()`

## BluOS HTTP API

All communication uses HTTP GET on port 11000. Responses are XML parsed with expat (SAX-style).

| Endpoint | Purpose |
|----------|---------|
| `/Status` | Current playback state |
| `/SyncStatus` | Player identity, group info (master/slaves) |
| `/Browse` | Browse sources, search, context menus |
| `/Browse?key=...` | Navigate into a source/category |
| `/Browse?key=...&q=...` | Search within a source |
| `/Playlist` | Current play queue |
| `/Volume?level=N` | Set volume |
| `/Volume?mute=0\|1` | Toggle mute |
| `/Pause?toggle=1` | Toggle play/pause |
| `/Skip` | Next track |
| `/Back` | Previous track |
| `/Play?id=N` | Jump to queue track (0-based) |
| `/Shuffle?state=0\|1` | Set shuffle |
| `/Repeat?state=0\|1\|2` | Set repeat (0=queue, 1=track, 2=off) |
| `/Save?name=...` | Save playlist |
| `/Delete?name=...` | Delete playlist |
| `/AddFavourite?albumid=...&service=...` | Add album to favourites |
| `/AddSlave?slave=IP&port=11000` | Add player to group (sent to master) |
| `/RemoveSlave?slave=IP&port=11000` | Remove player from group (sent to master) |

### URL Encoding

BluOS browse keys contain pre-encoded URL segments with `&` characters:

```
Qobuz:Album/%2FAlbums%3Fcategory=FAVOURITES&service=Qobuz
```

When used as query parameter values, only `&` must be encoded as `%26` to prevent splitting the query string. Do NOT full-encode these keys - the `%2F`, `%3F`, `:`, `/`, `=` are intentional and already correctly formatted.

Two encoding functions:

- `url_encode_param()` - encodes only `&`. Used for browse keys, context menu keys, search keys.
- `url_encode()` - full RFC 3986 encoding. Used for user-supplied text (search queries, playlist names, artist names in context menu key construction).

Python's `requests` library handles this automatically via `urllib.parse.urlencode`.

### Browse XML Structure

```xml
<browse searchKey="Qobuz:Search" nextKey="..." type="albums">
  <item text="Album Name" text2="Artist" browseKey="..." playURL="..."
        contextMenuKey="..." isFavourite="true" type="album" />
</browse>
```

The root element may have different names (`<browse>`, `<items>`, etc.). The parser captures `searchKey` and `nextKey` from any non-`<item>` element. Items can appear nested inside `<category>` elements - the SAX parser handles this automatically since it fires for all `<item>` elements regardless of depth.

Context menu items use `actionURL` (not `playURL`) for actions like add/remove favourite.

### SyncStatus XML Structure

The SyncStatus response uses child elements (not attributes) for group info:

```xml
<!-- Master player (has slaves) -->
<SyncStatus name="Livingroom 40ST" group="Livingroom 40ST+office" ...>
  <slave id="192.168.68.65" port="11000" name="office" model="P130" />
</SyncStatus>

<!-- Slave player (has master) -->
<SyncStatus name="office" ...>
  <master port="11000">192.168.68.61</master>
</SyncStatus>

<!-- Standalone player (no group) -->
<SyncStatus name="Livingroom 40ST" ...>
</SyncStatus>
```

The parser uses expat SAX callbacks:
- `sync_start` — captures attributes from `<SyncStatus>` (name, brand, model, group) and `<slave>` (id, name)
- `sync_chars` — accumulates text content for `<master>` element
- `sync_end` — extracts master IP from text content

### BluOS Web Interface (Port 80)

Used by the health check feature:

| Page | Data |
|------|------|
| `/diagnostics` | Network info, signal strength, IP, MAC, firmware versions, uptime |
| `/upgrade` | Firmware update availability |

Parsed with hand-written HTML extractors (no regex library needed).

## Metadata Integration

### MusicBrainz (metadata.c)

Searches for album releases matching artist + album name. Uses a scoring system:

| Factor | Score |
|--------|-------|
| Exact title match | +100 |
| Partial title match | +50-60 |
| Word overlap | +10 per word |
| Exact artist match | +50 |
| Partial artist match | +30 |
| MB confidence score | +score/10 |

Fetches genre tags from the release-group endpoint (top 3 by count). Rate-limited to 1 request/second.

### OpenAI (metadata.c)

Single combined request for year, label, genre, and track description. Response is JSON, possibly wrapped in markdown fences (stripped before parsing). Falls back to MusicBrainz when no API key or on failure.

### LRCLIB (metadata.c)

Free lyrics API, no key needed. Returns plain lyrics text. Network errors are NOT cached (allow retry on next track change); only 404s are cached as negative results.

### Cache (cache.c)

All metadata results are cached in a single LRU cache (default 256 entries). Cache keys use prefixes to namespace different data types: `combined|`, `station|`, `lyrics|`, `wiki|`, `artist|album`.

## Cover Art Rendering (cover_art.c)

1. Image decoded from raw bytes using stb_image (JPEG, PNG, BMP)
2. Nearest-neighbor resize to target terminal dimensions (width x height*2 pixels)
3. Each terminal row represents 2 pixel rows using the half-block character U+2580 (▀)
4. Foreground color = top pixel, background color = bottom pixel
5. RGB values mapped to xterm-256 color indices (grayscale ramp or 6x6x6 color cube)
6. Color pairs allocated dynamically starting at pair index 10
7. Graceful fallback to pair 0 when color pair limit is exhausted

## Player Discovery (discover.c)

Uses Avahi (the Linux mDNS/DNS-SD implementation) to discover Blusound players:

1. Background thread creates an Avahi simple poll and client
2. Service browser listens for `_musc._tcp` services
3. On discovery, resolves to IPv4 address
4. Creates lightweight `BlusoundPlayer` via `player_create()` (sets host/name/base_url only, no HTTP)
5. Adds to shared player list (protected by mutex)
6. Main thread polls the list for UI updates
7. Sync name fetch and source initialization happen only when a player is activated (selected via ENTER)

Discovery is started on demand (when `X` or `G` is pressed) rather than at startup if a stored player connects successfully.

## Multiroom (player.c, ui.c, main.c)

### Player Switching (X)

Pressing `X` starts mDNS discovery (if not already running) and shows the player selection screen. The active player is marked with `*`. Pressing `b`/`ESC` returns to the current player without switching.

When switching players, `reset_for_player_switch()` clears all cached metadata (wiki, lyrics, cover art, MusicBrainz info) so fresh data loads for the new player.

### Group Management (G)

Pressing `G` opens a blocking modal overlay showing all discovered players (excluding the active one). Players in the current group show `[G]`, others show `[ ]`. ENTER toggles group membership via `/AddSlave` or `/RemoveSlave`.

If the active player is currently a slave of another master, the overlay shows the master IP and ENTER leaves the group (sends `/RemoveSlave` to the master via `player_leave_group()`).

On close, `app->group_info` is updated so the header reflects changes immediately.

### Volume Overlay

When the active player is in a group (has slaves), pressing UP/DOWN for volume opens a blocking overlay showing all group members (master + slaves) with volume bars. The first volume change is applied to the master immediately before the overlay opens.

- LEFT/RIGHT selects a player
- UP/DOWN adjusts volume for the selected player (5% increments)
- Selected player is highlighted in green (COLOR_PAIR(3))
- Master volume updates are synced back to `app->player_status.volume`
- Slave volumes are fetched via `player_get_status()` when the overlay opens

### Header Group Display

When grouped, the header shows: `BluOS Player Control - Livingroom 40ST (&office)`

Slave names come from the `name` attribute on `<slave>` elements in the SyncStatus XML, stored in `GroupInfo.slave_names[]`. The group info is cached in `app->group_info` and polled every 15 seconds (`GROUP_POLL_INTERVAL`).

## Log Files

All log files are in the `logs/` directory (relative to CWD), with rotating file handlers (1 MB max, 1 backup):

| Log File | Source |
|----------|--------|
| `logs/bluxir.log` | Main application |
| `logs/cli.log` | Player module |
| `logs/musicbrainz.log` | Metadata module |

## Error Handling

- API functions return `bool` (success/failure) with output parameters
- HTTP errors: logged via `curl_easy_strerror()`, caller receives false
- XML errors: logged via expat error codes, empty results returned
- Curses errors: silent ignore (matches Python's `try/except curses.error: pass`)
- Fatal errors: `endwin()`, log, `exit(1)`

## Status Update Cycle

1. Main loop runs with 100ms curses timeout (`CURSES_POLL_MS`)
2. Every second, local progress counter increments (client-side interpolation)
3. Every 3 seconds, full status refresh from `/Status` API
4. Every 15 seconds, group info refresh from `/SyncStatus` API
5. On track/album change, background metadata threads are launched
6. On skip/back, next refresh is scheduled ~1 second early
7. On volume/mute/repeat/shuffle, optimistic UI update (set value directly, no refresh)
