# About bluxir

This project is originaly written by @irrelative and published on github
under https://github.com/irrelative/blucli. if you want the original software
get it there.
i forked it on feb 15th 2026 and made some major changes to the interface
and the behavior of the controller. i used the api from the original project.
its only for private usage. i just made it, because its incredible borring
to switch to a mobile phone while working just because you want to hear different
sounds. how ever.  its all free. use it how ever you want. no licence at all.

credits again to @irrelative. i just forked it and added the features i need.
this readme is mainly written by irrelative


# Blusound CLI

A basic CLI interface to control Blusound streamers.

## Features

* Automatic discovery of Blusound players on the network
* Interactive player selection and control
* Volume adjustment
* Play/pause functionality
* Track navigation (skip forward/backward)
* Display of currently playing information
* Input selection for each player
* Detailed view of player status

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
