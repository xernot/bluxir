# About bluxir

This is an comprehensive command-line interface (CLI) for bluOS streamers.

I wrote it, because its incredible borring to switch to a mobile phone while 
working just because you want to hear different sounds, written in python.

Originally based on https://github.com/irrelative/blucli (opens in a new tab)
This project has since diverged significantly and is maintained independently.

Extensive usage of claude-code (opus4.6) 


# Blusound CLI

A comprehensive CLI interface to control Blusound streamers.

## Features

* Automatic discovery of Blusound players on the network
* Interactive player selection and control
* Volume adjustment
* Play/pause functionality
* Track navigation (skip forward/backward)
* Display of currently playing information
* Input selection for each player
* Detailed view of player status
* Displays the current playlist and let you toggle
* API call to musicbrainz for additional information about the track
* openai API call to get textbased information about the track
* Displays an ascii-art image from the cover (c)
* Radio Integration with Tunein & Radio Paradise

## Known Limitations
* It mainly works well for qobuz support
* No Spotify Support. BluOS does not support it
* Not tested local/private streaming-services
* The information form MusicBrainz is sometimes wrong. It is used
  when no OPENAI_API_KEY is present. 
  All other additional Informations are from chatgpt - so it could be wrong
* Radio Stations cannot be stored as favorites. 


## Requirements

* Python 3.6+

## Installation

1. Clone this repository:
   ```
   git clone https://github.com/xernot/bluxir.git
   cd bluxir
   ```

2. Set up a virtual environment:
   ```
   python3 -m venv venv
   source venv/bin/activate  # On Windows use `venv\Scripts\activate`
   ```

3. Install the required dependencies:
   ```
   pip install -r requirements.txt
   ```

## Usage

Run the script using:

```
python bluxir.py
```


## Functionality

The Blusound CLI provides an intuitive interface to control your Blusound players:

1. **Player Discovery**: Automatically detects Blusound players on your network.
2. **Player Selection**: Choose which player you want to control.
3. **Playback Control**: Play, pause, skip tracks, and adjust volume.
4. **Input Selection**: Change the input source for each player.
5. **Status Display**: View current track information, volume level, and other player details.

The application uses a curses-based interface for an interactive experience in the terminal.
