# About bluxir

This is an comprehensive command-line interface (CLI) for bluOS streamers.

I wrote it, because its incredible borring to switch to a mobile phone while 
working just because you want to hear different sounds, written in python.

This project is originaly written by @irrelative and published on github
under https://github.com/irrelative/blucli. if you want the original software
get it there.  I forked it on feb 15th 2026 and extensivly extended the project.

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

## Known Limitations
* No Spotify Support. BluOS does not support it
* Not tested local/private streaming-services




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
