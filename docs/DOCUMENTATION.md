# bluxir - Documentation

## Overview

bluxir is a terminal-based controller for Blusound network music streamers. It provides a curses-based split-screen interface to control playback, browse sources, search music services, view the play queue, and display metadata about the currently playing track.

The project originated as a fork of [blucli](https://github.com/irrelative/blucli) by @irrelative and has since evolved into a substantially different application with a redesigned interface and additional features including MusicBrainz metadata lookup, OpenAI-powered track information, and Qobuz favorites integration.

## Architecture

```
+------------------+     +------------------+     +-------------------+
|    bluxir.py     |---->|    player.py     |---->| BluOS HTTP API    |
|  (UI / curses)   |     | (player control) |     | (port 11000)      |
+------------------+     +------------------+     +-------------------+
        |                        |
        |                        +---> Zeroconf/mDNS discovery
        |
        +----------+     +------------------+     +-------------------+
        |          |---->| musicbrainz.py   |---->| MusicBrainz API   |
        |          |     | (metadata)       |---->| OpenAI API        |
        |          |     +------------------+     +-------------------+
        |          |
        |          +---->+------------------+
        |          |     |    config.py     |---> ~/.bluxir.json
        |          |     +------------------+
        |          |
        |          +---->+------------------+
        |               |  constants.py    |  (all hardcoded values)
        +---------------+------------------+
```

### Project Structure

```
bluxir/
  bluxir.py          Main application (UI, keyboard handling, main loop)
  player.py          Blusound player API wrapper, mDNS discovery, data models
  musicbrainz.py     External metadata (MusicBrainz, OpenAI, Wikipedia, LRCLIB)
  config.py          Configuration module (reads both config files)
  constants.py       All hardcoded values (timing, URLs, UI strings, scoring, etc.)
  config.json        Project config (player settings, system prompt)
  requirements.txt   Python dependencies
  docs/
    DOCUMENTATION.md This file
    README.md        Project readme
    LICENSE          GPL v3 license
  logs/              Log files (created at runtime)

~/.bluxir.json       Private config (API keys, not in repo)
```

### Module Responsibilities

**constants.py** — Central location for all literal values used across the application. Organized by category:
- Logging (format, rotation size, backup count)
- Timing (poll intervals, rate limits, highlight duration)
- API endpoints (MusicBrainz, Wikipedia, OpenAI, LRCLIB, BluOS)
- AI parameters (model defaults, token limits, temperatures, prompt templates)
- MusicBrainz scoring (match weights for title, artist, overlap)
- UI display (layout percentages, scroll steps, volume increment)
- UI strings (footer help, instructions, version, attribution)
- Search (skip categories, service list)
- Help screen (keybinding tuples for left/right columns)

**bluxir.py** — The curses-based UI. The `BlusoundCLI` class manages all views and input handling. Long functions are split into focused methods:
- `display_summary_view` dispatches to `_draw_footer`, `_draw_player_info`, `_draw_cover_art`, `_draw_detail_and_track_info`, `_draw_track_info`, and right panel methods (`_draw_radio_panel`, `_draw_lyrics_panel`, `_draw_playlist_panel`)
- `handle_player_control` dispatches to `_handle_playback_keys`, `_handle_navigation_keys`, `_handle_info_keys`
- `handle_source_selection` dispatches to `_handle_source_navigation`, `_handle_source_enter`, `_handle_source_sort`, `_handle_source_filter`, `_handle_source_add_favourite`, `_handle_source_remove_favourite`
- `main` dispatches to `_try_stored_player`, `_draw_current_view`, `_handle_input`, `_tick_progress`

**player.py** — Wraps the BluOS HTTP API. `BlusoundPlayer` handles all player communication (status, volume, playback, browsing, search, playlists, favourites). `MyListener` + `discover()` handle mDNS discovery via Zeroconf.

**musicbrainz.py** — All external metadata fetching. Functions are split into focused helpers:
- `get_album_info` uses `_query_musicbrainz` and `_extract_album_metadata`
- `get_combined_info` uses `_build_combined_prompt`, `_parse_combined_response`, and `_mb_fallback_result`
- `get_track_wiki`, `get_track_info_ai`, `get_station_info`, `get_lyrics` are standalone

**config.py** — Reads/writes two JSON config files: `config.json` (project settings) and `~/.bluxir.json` (private API keys). Routes keys to the correct file based on `_PRIVATE_KEYS`.

### Log Files

All log files are stored in the `logs/` directory with rotating file handlers (1 MB max, 1 backup):

| Log File | Source |
|---|---|
| `logs/bluxir.log` | Main application (bluxir.py) |
| `logs/cli.log` | Player module (player.py) |
| `logs/musicbrainz.log` | MusicBrainz and OpenAI calls (musicbrainz.py) |

## How It Works

### Startup Sequence

1. The application checks `config.json` for a stored player (`player_host`, `player_name`)
2. If a stored player exists, it connects directly via HTTP to port 11000
3. If no stored player is found (or connection fails), it starts mDNS discovery using Zeroconf, listening for `_musc._tcp.local.` services
4. On first successful connection, the player's host and name are saved to the config file for future sessions

### BluOS HTTP API

All communication with the Blusound player happens over HTTP on port 11000. The API returns XML responses.

| Endpoint | Purpose |
|---|---|
| `GET /Status` | Current player state (track, artist, album, volume, playback state, etc.) |
| `GET /SyncStatus` | Player identity (friendly name) |
| `GET /Playlist` | Current play queue |
| `GET /Browse` | Browse available sources, navigate hierarchies, search |
| `GET /Volume?level=N` | Set volume (0-100) |
| `GET /Pause?toggle=1` | Toggle play/pause |
| `GET /Skip` | Skip to next track |
| `GET /Back` | Go to previous track |

The Browse API supports pagination via a `nextKey` attribute on the XML root element. The `capture_all_sources()` method follows all pages to retrieve complete listings (important for large collections like Qobuz favorites).

### Player Discovery

mDNS/Zeroconf discovery runs in a daemon thread, continuously scanning for Blusound players on the local network. When a player is found, its IPv4 address is extracted and a `BlusoundPlayer` instance is created. The player's friendly name is fetched via the `/SyncStatus` endpoint.

### Data Models

**PlayerStatus** - Dataclass holding all player state fields parsed from the `/Status` XML response. Key fields:
- `name`, `artist`, `album` - Current track info
- `state` - Playback state (play, stream, pause, stop)
- `volume` - Volume level (0-100)
- `song` - Current track index in the queue (0-based)
- `secs`, `totlen` - Playback position and total track length in seconds
- `stream_format`, `quality`, `db` - Audio quality info

**PlayerSource** - Dataclass representing a browsable/playable item from the Browse API. Contains `browse_key` for navigation, `play_url` for playback, and `search_key` for search capability.

## User Interface

### Layout (Player Control View)

```
────────────────────────────────────────────── music is my drug
Player Control - PlayerName     Repeat:Off | Shuffle:Off | Play | [####----] 50%
──────────────────────────────────────────────────────────────────────
Now Playing:  Track Name - Artist          │ Playlist:
Album:        Album Name                   │   1. First Track - Artist
Service:      Qobuz                        │   2. Second Track - Artist
Progress:     2:30 / 5:00                  │ > 3. Current Track - Artist
───────────────────────────────────┤        │   4. Fourth Track - Artist
Format:    FLAC    Track-Nr:  3    │        │   5. Fifth Track - Artist
Quality:   HR      Year:      1977 │        │   ...
dB Level:  -20.0   Label:     EMI  │        │
Service:   Qobuz   Genre:     rock │        │
───────────────────────────────────┤        │
                                   │        │
Track Info:                        │        │
"Song Title" is a track from the   │        │
album "Album Name" released in     │        │
1977. It was written by...         │        │
                                   │        │
──────────────────────────────────────────────
(s) search  (f) favorites  (i) select source  (?) help    bluxir v2.2
──────────────────────────────────────────────────────────────────────
```

The interface uses a 60:40 split layout:
- **Left side**: Player info (top), detail section with two sub-columns (middle), AI-generated track info (bottom)
- **Right side**: Full-height scrollable playlist with current track highlighted

### Visual Feedback

When you change a control (volume, mute, repeat, shuffle, play/pause), the corresponding segment in the header bar highlights green for 5 seconds, then returns to the default color. Status messages also appear in green during this period.

### Keyboard Shortcuts

| Key | Action |
|---|---|
| `UP` / `DOWN` | Adjust volume (+/- 5%) |
| `SPACE` | Toggle play/pause |
| `RIGHT` / `LEFT` | Skip / Previous track |
| `s` | Search within a source |
| `f` | Quick access to Qobuz favorites |
| `l` | Load a saved playlist |
| `w` | Save current playlist |
| `i` | Select input source |
| `c` | Toggle cover art display |
| `t` | Toggle lyrics view |
| `PgUp` / `PgDn` | Scroll lyrics |
| `g` | Go to track number |
| `m` | Toggle mute |
| `r` | Cycle repeat (off / queue / track) |
| `x` | Toggle shuffle |
| `+` / `-` | Add / remove current album from favourites |
| `p` | Pretty print player state (JSON debug view) |
| `?` | Show keyboard shortcuts |
| `b` | Back to player list |
| `q` | Quit |

### Views

1. **Player Selection** - Lists discovered Blusound players on the network. Select with UP/DOWN, activate with ENTER.
2. **Player Control** - Main view with split-screen layout showing now playing, details, track info, and playlist.
3. **Source Selection** - Hierarchical browser for player sources (streaming services, inputs, etc.). Supports pagination with `n`/`p` keys, sorting by title/artist, and text filtering.
4. **Search** - Two-phase search: first select a source to search within, then enter a search term. Results are browsable and playable.

## Metadata Integration

### MusicBrainz

When the album changes, a background thread queries the MusicBrainz API to fetch:
- **Year** - Release date
- **Label** - Record label
- **Genre** - Top 3 tags from the release group

The search uses a scoring system (`_match_score`) to find the best match among up to 25 results. Scoring factors:
- Exact title match: +100 points
- Partial title match: +50-60 points
- Exact artist match: +50 points
- MusicBrainz confidence score: up to +10 points

All scoring constants are defined in `constants.py` for easy tuning.

Rate limiting enforces 1 request per second as required by the MusicBrainz API terms.

### OpenAI (Combined Info)

When the track changes, a background thread sends a single request to OpenAI asking for album metadata (year, label, genre) and a short track description in one JSON response. The prompt template is defined in `constants.py` (`COMBINED_INFO_PROMPT`). A configurable system prompt in `config.json` controls the style of the track description.

For radio stations, a separate prompt (`STATION_INFO_PROMPT`) fetches a description of the station.

The API key is stored in `~/.bluxir.json` and the system prompt in `config.json` in the project root.

Both MusicBrainz and OpenAI results are cached in memory for the duration of the session to avoid redundant API calls.

### Background Threading

All external API calls (MusicBrainz, OpenAI, LRCLIB, cover art) run in daemon threads to avoid blocking the UI. The main loop checks for updated data on each render cycle. A threading lock (`_data_lock`) protects shared state updates from background threads.

## Configuration

Configuration is split into two files:

### Project config: `config.json` (in project root)

Contains player settings and OpenAI system prompt. Safe to commit to version control.

```json
{
  "player_host": "192.168.86.21",
  "player_name": "Living Room",
  "openai_model": "gpt-4o-mini",
  "openai_system_prompt": "Always include the year the song was first written and who wrote it. Keep it short, max 3-4 sentences."
}
```

| Key | Purpose |
|---|---|
| `player_host` | IP address of the Blusound player (skip discovery on startup) |
| `player_name` | Friendly name of the player |
| `openai_model` | OpenAI model to use (default: `gpt-4o-mini`) |
| `openai_system_prompt` | System prompt sent to OpenAI to control the style and content of track info responses |

### Private config: `~/.bluxir.json` (in home directory)

Contains sensitive data like API keys. Not part of the repository.

```json
{
  "openai_api_key": "sk-..."
}
```

| Key | Purpose |
|---|---|
| `openai_api_key` | OpenAI API key for track info feature |

### Constants: `constants.py`

All hardcoded values (timing, URLs, UI strings, scoring weights, prompt templates, etc.) are centralized in `constants.py` with semantic comments explaining each value. This makes it easy to tune behavior without searching through application code.

## Dependencies

| Package | Purpose |
|---|---|
| `zeroconf` | mDNS/Zeroconf player discovery |
| `requests` | HTTP client for BluOS API, MusicBrainz, OpenAI, Wikipedia, LRCLIB |
| `Pillow` | Cover art image processing (resize, pixel extraction for terminal rendering) |
| `curses` | Terminal UI (Python standard library) |

## Status Update Cycle

1. The main loop runs with a 100ms timeout on keyboard input
2. Every second, the local progress counter increments (client-side interpolation)
3. Every 3 seconds, a full status refresh is fetched from the player via `/Status`
4. On track/album change, background metadata fetches are triggered
5. On skip/back, a refresh is scheduled after ~1 second to allow the player to transition
6. On other user actions (play/pause, volume), an immediate status refresh occurs
