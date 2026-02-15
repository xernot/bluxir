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
        |               |    config.py     |---> ~/.blusoundcli.json
        +---------------+------------------+
```

### Project Structure

```
bluxir/
  bluxir.py          Main application (UI, keyboard handling, main loop)
  player.py          Blusound player API wrapper, mDNS discovery, data models
  musicbrainz.py     External metadata (MusicBrainz, OpenAI, Wikipedia)
  config.py          Configuration persistence (~/.blusoundcli.json)
  requirements.txt   Python dependencies
  docs/
    DOCUMENTATION.md This file
    README.md        Project readme
    LICENSE          GPL v3 license
  logs/              Log files (created at runtime)
```

### Log Files

All log files are stored in the `logs/` directory with rotating file handlers (1 MB max, 1 backup):

| Log File | Source |
|---|---|
| `logs/bluxir.log` | Main application (bluxir.py) |
| `logs/cli.log` | Player module (player.py) |
| `logs/musicbrainz.log` | MusicBrainz and OpenAI calls (musicbrainz.py) |

## How It Works

### Startup Sequence

1. The application checks `~/.blusoundcli.json` for a stored player (`player_host`, `player_name`)
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
Player Control - PlayerName     Status: Play  Volume: [########----] 75%
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
(s) search  (f) favorites  (i) select source  (?) help    bluxir v2.0
──────────────────────────────────────────────────────────────────────
```

The interface uses a 60:40 split layout:
- **Left side**: Player info (top), detail section with two sub-columns (middle), AI-generated track info (bottom)
- **Right side**: Full-height scrollable playlist with current track highlighted

### Keyboard Shortcuts

| Key | Action |
|---|---|
| `UP` / `DOWN` | Adjust volume (+/- 5%) |
| `SPACE` | Toggle play/pause |
| `RIGHT` / `LEFT` | Skip / Previous track |
| `s` | Search within a source |
| `f` | Quick access to Qobuz favorites |
| `i` | Select input source |
| `p` | Pretty print player state (JSON debug view) |
| `b` | Back to player list |
| `?` | Show keyboard shortcuts |
| `q` | Quit |

### Views

1. **Player Selection** - Lists discovered Blusound players on the network. Select with UP/DOWN, activate with ENTER.
2. **Player Control** - Main view with split-screen layout showing now playing, details, track info, and playlist.
3. **Source Selection** - Hierarchical browser for player sources (streaming services, inputs, etc.). Supports pagination with `n`/`p` keys.
4. **Search** - Two-phase search: first select a source to search within, then enter a search term. Results are browsable and playable.

## Metadata Integration

### MusicBrainz

When the album changes, a background thread queries the MusicBrainz API to fetch:
- **Year** - Release date
- **Label** - Record label
- **Genre** - Top 3 tags from the release group

The search uses a scoring system to find the best match among up to 25 results. Scoring factors:
- Exact title match: +100 points
- Partial title match: +50-60 points
- Exact artist match: +50 points
- MusicBrainz confidence score: up to +10 points

Rate limiting enforces 1 request per second as required by the MusicBrainz API terms.

### OpenAI (Track Info)

When the track changes, a background thread sends the track title and artist to OpenAI's `gpt-4o-mini` model with a prompt requesting a short 3-4 sentence description. The response is displayed in the "Track Info" section below the detail box.

The API key is stored in `~/.blusoundcli.json` under the `openai_api_key` field.

Both MusicBrainz and OpenAI results are cached in memory for the duration of the session to avoid redundant API calls.

### Background Threading

All external API calls (MusicBrainz, OpenAI) run in daemon threads to avoid blocking the UI. The main loop checks for updated data on each render cycle. Thread safety is achieved through simple attribute assignment (Python's GIL ensures atomic reference updates).

## Configuration

Config file: `~/.blusoundcli.json`

```json
{
  "player_host": "192.168.86.21",
  "player_name": "Living Room",
  "openai_api_key": "sk-..."
}
```

| Key | Purpose |
|---|---|
| `player_host` | IP address of the Blusound player (skip discovery on startup) |
| `player_name` | Friendly name of the player |
| `openai_api_key` | OpenAI API key for track info feature |

## Dependencies

| Package | Purpose |
|---|---|
| `zeroconf` | mDNS/Zeroconf player discovery |
| `requests` | HTTP client for BluOS API, MusicBrainz, OpenAI, Wikipedia |
| `curses` | Terminal UI (Python standard library) |

## Status Update Cycle

1. The main loop runs with a 100ms timeout on keyboard input
2. Every second, the local progress counter increments (client-side interpolation)
3. Every 10 seconds, a full status refresh is fetched from the player via `/Status`
4. On track/album change, background metadata fetches are triggered
5. On any user action (play/pause, skip, volume), an immediate status refresh occurs
