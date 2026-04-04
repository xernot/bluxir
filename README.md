# bluxir v3.2

A BluOS terminal controller for Blusound network music streamers, written in C.

Features: player discovery, multiroom grouping, playback control, source browsing, search, favorites, playlists, metadata (MusicBrainz/OpenAI/LRCLIB), cover art rendering, lyrics, health check, and a full curses TUI.

Originally based on [blucli](https://github.com/irrelative/blucli) by @irrelative. This project has since diverged significantly.

## What's New in v3.2

- **Player switching** (`X`) — discover all Blusound players on the network via mDNS and switch between them
- **Group management** (`G`) — add/remove players to a synchronized playback group
- **Per-player volume** — when grouped, volume controls open an overlay to adjust each player independently
- **Group display** — header shows active group members: `Livingroom 40ST (&office)`
- **Lightweight discovery** — players are discovered instantly; sources only load when a player is activated

## Features

- Automatic mDNS discovery of Blusound players via Avahi
- Multiroom: player switching, group management, per-player volume control
- Interactive player selection and control
- Volume, mute, play/pause, skip, repeat, shuffle
- Split-screen curses UI with player info, playlist, and metadata
- Source browsing with pagination, sorting (title/artist), and filtering (case-insensitive)
- Search within Qobuz and TuneIn
- Qobuz favorites quick access
- Playlist save/load/delete
- Album metadata from OpenAI (year, label, genre, track description)
- MusicBrainz fallback when no API key is configured
- Cover art rendered as half-block terminal characters with 256 colors
- Lyrics from lrclib.net with scroll support
- Radio streaming with station info from OpenAI
- Player health check (diagnostics, firmware update status)
- Visual feedback: controls highlight green for 5 seconds after changes
- Rotating log files for debugging

## Requirements

### Build Dependencies

```
sudo apt install build-essential cmake libcurl4-openssl-dev libncursesw5-dev libexpat1-dev libavahi-client-dev
```

| Library | Purpose |
|---------|---------|
| ncursesw | Terminal UI (wide-char for UTF-8 and half-block rendering) |
| libcurl | HTTP client for BluOS API, MusicBrainz, OpenAI, LRCLIB |
| expat | SAX-style XML parsing of BluOS API responses |
| avahi-client | mDNS player discovery |
| pthread | Background threads for metadata fetching |

### Vendored (no system install needed)

| Library | Purpose |
|---------|---------|
| [stb_image.h](https://github.com/nothings/stb) | Image decoding for cover art |
| [cJSON](https://github.com/DaveGamble/cJSON) | JSON parsing for config and API responses |

## Building

```bash
mkdir -p build && cd build
cmake ..
make
```

The binary is built at `build/bluxir`.

## Running

Run from the project root directory (where `config.json` is located):

```bash
./build/bluxir
```

On first run without a stored player, bluxir discovers Blusound players on the local network via Avahi mDNS. Select a player with UP/DOWN and press ENTER. The player's address is saved to `config.json` for future sessions.

## Configuration

### Project config: `config.json` (in project root)

```json
{
  "player_host": "192.168.68.61",
  "player_name": "Livingroom 40ST",
  "openai_model": "gpt-4o-mini",
  "openai_system_prompt": "Keep it short, max 3-4 sentences."
}
```

### Private config: `~/.bluxir.json` (in home directory)

```json
{
  "openai_api_key": "sk-..."
}
```

Without an OpenAI API key, bluxir falls back to MusicBrainz for album metadata.

## Keyboard Shortcuts

### Player Control

| Key | Action |
|-----|--------|
| UP/DOWN | Adjust volume (opens group overlay when grouped) |
| SPACE | Play/Pause |
| RIGHT/LEFT | Skip/Previous track |
| g | Go to track number |
| m | Toggle mute |
| r | Cycle repeat (off/queue/track) |
| x | Toggle shuffle |
| +/- | Add/Remove album from favourites |

### Multiroom

| Key | Action |
|-----|--------|
| X | Switch player (discover and select) |
| G | Group manager (add/remove players) |

When grouped, the volume overlay appears on UP/DOWN:

| Key | Action |
|-----|--------|
| LEFT/RIGHT | Select player |
| UP/DOWN | Adjust volume for selected player |
| q/ESC | Close overlay |

### Navigation

| Key | Action |
|-----|--------|
| i | Browse input sources |
| s | Search |
| f | Qobuz favorites |
| l | Load playlist |
| w | Save playlist |

### Browse / Favorites

| Key | Action |
|-----|--------|
| UP/DOWN | Navigate list |
| RIGHT/ENTER | Expand / Play |
| LEFT | Navigate back |
| n/p | Next/Previous page |
| t | Sort by title |
| a | Sort by artist |
| o | Restore original order |
| / | Filter (empty input clears filter) |
| +/- | Add/Remove from favourites |
| b | Exit browse |

### Display

| Key | Action |
|-----|--------|
| c | Toggle cover art |
| t | Toggle lyrics |
| PgUp/PgDn | Scroll lyrics |
| h | Health check overlay |
| p | Pretty print (JSON debug) |
| ? | Show all shortcuts |
| b | Back to player list |
| q | Quit |

## Project Structure

```
bluxir/
  CMakeLists.txt          Build configuration
  src/
    main.c                Entry point, curses init, main loop, input handling
    constants.h           All #define constants with semantic comments
    types.h               All struct definitions
    config.c / config.h   JSON config read/write (cJSON)
    player.c / player.h   BluOS HTTP API client (libcurl + expat)
    metadata.c / metadata.h  MusicBrainz, OpenAI, LRCLIB, Wikipedia
    cache.c / cache.h     Thread-safe LRU cache (hash table + doubly-linked list)
    ui.c / ui.h           Header, footer, modals, input prompts, health check, group/volume overlays
    ui_player.c           Player control view (left/right panels)
    ui_browse.c           Source selection, browsing, player selection
    ui_search.c           Search 3-phase state machine
    cover_art.c / cover_art.h  stb_image decode + 256-color half-block render
    discover.c / discover.h    Avahi mDNS discovery
    logger.c / logger.h   Rotating file logger
    util.c / util.h       format_time, url_encode, safe string helpers
  lib/
    stb_image.h           Vendored header-only image decoder
    cJSON.c / cJSON.h     Vendored JSON library
```

## Known Limitations

- Mainly tested with Qobuz streaming
- No Spotify support (BluOS limitation)
- Radio stations cannot be stored as favorites
- MusicBrainz metadata can be inaccurate; OpenAI metadata may contain errors

## License

GPL v3 - Copyright (C) 2026 xir
