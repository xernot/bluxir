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

import warnings
warnings.filterwarnings("ignore")

import curses
import time
import re
import requests
from typing import List, Optional, Tuple
from player import BlusoundPlayer, PlayerStatus, PlayerSource, threaded_discover
import logging
from logging.handlers import RotatingFileHandler
import json
from config import get_preference, set_preference
import curses.textpad
import signal
import sys
import threading
import io
from musicbrainz import get_album_info, get_track_info_ai, get_combined_info, get_station_info, get_lyrics
from PIL import Image

_QUALITY_RE = re.compile(r'(\d+)/(\d+\.?\d*)')

# Set up logging
log_file = 'logs/bluxir.log'
log_handler = RotatingFileHandler(log_file, maxBytes=1024*1024, backupCount=1)
log_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                                  datefmt='%Y-%m-%d %H:%M:%S')
log_handler.setFormatter(log_formatter)
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
logger.addHandler(log_handler)

# Define key codes
KEY_UP = curses.KEY_UP
KEY_DOWN = curses.KEY_DOWN
KEY_ENTER = 10
KEY_B = ord('b')
KEY_SPACE = ord(' ')
KEY_I = ord('i')
KEY_QUESTION = ord('?')
KEY_P = ord('p')
KEY_RIGHT = curses.KEY_RIGHT
KEY_LEFT = curses.KEY_LEFT
KEY_S = ord('s')
KEY_F = ord('f')
KEY_W = ord('w')
KEY_L = ord('l')

def create_volume_bar(volume, width=20):
    filled = int(volume / 100 * width)
    return f"[{'#' * filled}{'-' * (width - filled)}]"

def format_time(seconds):
    if seconds <= 0:
        return "0:00"
    minutes = seconds // 60
    secs = seconds % 60
    return f"{minutes}:{secs:02d}"

class BlusoundCLI:
    def __init__(self):
        self.header_message: str = ""
        self.header_message_time: float = 0
        self.shortcuts_open: bool = False
        self.selector_shortcuts_open: bool = False
        self.source_selection_mode: bool = False
        self.selected_source_index: List[int] = []
        self.player_status: Optional[PlayerStatus] = None
        self.selected_index: int = 0
        self.active_player: Optional[BlusoundPlayer] = None
        self.players: List[BlusoundPlayer] = []
        self.last_update_time: float = 0.0
        self.last_progress_time: float = 0.0
        self.current_sources: List[PlayerSource] = []
        self.search_mode: bool = False
        self.search_phase: str = ''
        self.searchable_sources: List[PlayerSource] = []
        self.search_source_index: int = 0
        self.search_results: List[PlayerSource] = []
        self.search_selected_index: int = 0
        self.search_history: list = []
        self.active_search_key: Optional[str] = None
        self.search_source_name: str = ""
        self.playlist: list = []
        self.source_sort: str = 'original'
        self.unsorted_sources: List[PlayerSource] = []
        self.source_filter_backup: List[PlayerSource] = []
        self.mb_info: Optional[dict] = None
        self.mb_loading: bool = False
        self.wiki_text: Optional[str] = None
        self.wiki_track_key: str = ""
        self.wiki_loading: bool = False
        self.is_radio: bool = False
        self.radio_title2: str = ''
        self.radio_title3: str = ''
        self.cover_art: Optional[bool] = None
        self._cover_art_raw: Optional[bytes] = None
        self.cover_art_key: str = ""
        self.cover_art_loading: bool = False
        self.show_cover_art: bool = False
        self.lyrics_text: Optional[str] = None
        self.lyrics_track_key: str = ""
        self.lyrics_loading: bool = False
        self.show_lyrics: bool = False
        self.lyrics_scroll: int = 0
        self._data_lock = threading.Lock()

    def _derive_quality(self, stream_format: str) -> str:
        if not stream_format:
            return "-"
        fmt = stream_format.upper()
        if "MQA" in fmt:
            return "MQA"
        if "MP3" in fmt:
            return "MP3"
        # Parse bit depth and sample rate from formats like "FLAC 24/96"
        m = _QUALITY_RE.search(stream_format)
        if m:
            bit_depth = int(m.group(1))
            sample_rate = float(m.group(2))
            if bit_depth > 16 or sample_rate > 44.1:
                return "Hi-Res"
            return "CD-Quality"
        return "-"

    def _show_queue_dialog(self, stdscr: curses.window) -> Optional[str]:
        height, width = stdscr.getmaxyx()
        footer_row = height - 2
        stdscr.move(footer_row, 0)
        stdscr.clrtoeol()
        stdscr.addstr(footer_row, 2, "(1) Play now  (2) Add next  (3) Add last  (ESC) Cancel", curses.A_BOLD)
        stdscr.refresh()
        stdscr.timeout(-1)
        key = stdscr.getch()
        stdscr.timeout(100)
        if key == ord('1'):
            return "play_now"
        elif key == ord('2'):
            return "add_next"
        elif key == ord('3'):
            return "add_last"
        return None

    def _execute_queue_action(self, source: PlayerSource, stdscr: curses.window) -> bool:
        choice = self._show_queue_dialog(stdscr)
        if not choice:
            self.set_message("Cancelled")
            return False

        actions = self.active_player.get_queue_actions(source)
        if choice not in actions:
            self.set_message(f"Action not available for: {source.text}")
            return False

        try:
            self.active_player.request(actions[choice])
            labels = {"play_now": "Playing", "add_next": "Added next", "add_last": "Added to end"}
            self.set_message(f"{labels[choice]}: {source.text}")
            self.update_player_status()
            return True
        except Exception as e:
            self.set_message(f"Error: {e}")
            return False

    def set_message(self, message: str):
        if message:
            self.header_message = message
            self.header_message_time = time.time()

    def draw_header(self, stdscr: curses.window, view: str):
        height, width = stdscr.getmaxyx()
        # Top line
        stdscr.hline(0, 0, curses.ACS_HLINE, width)
        # Header
        header = view
        if self.active_player:
            header += f" - {self.active_player.name}"
        stdscr.addstr(1, 2, header[:width - 4], curses.A_BOLD)
        # Status and Volume on the right side of header line
        if self.player_status and isinstance(self.player_status, PlayerStatus):
            state_str = self.player_status.state.capitalize() if self.player_status.state else "-"
            repeat_str = {0: "Queue", 1: "Track", 2: "Off"}.get(self.player_status.repeat, "Off")
            shuffle_str = "On" if self.player_status.shuffle else "Off"
            vol_bar = create_volume_bar(self.player_status.volume, width=12)
            mute_str = "MUTED | " if self.player_status.mute else ""
            info_right = f"Repeat:{repeat_str} | Shuffle:{shuffle_str} | {state_str} | {mute_str}{vol_bar} {self.player_status.volume}%"
            info_x = width - len(info_right) - 2
            if info_x > len(header) + 4:
                stdscr.addstr(1, info_x, info_right)
        if self.header_message and time.time() - self.header_message_time < 2:
            msg_start = len(header) + 6
            max_msg_width = width - msg_start - 2
            if max_msg_width > 0:
                stdscr.addstr(1, msg_start, self.header_message[:max_msg_width])
        # Separator
        stdscr.hline(2, 0, curses.ACS_HLINE, width)

    def update_player_status(self):
        if self.active_player:
            try:
                success, status = self.active_player.get_status()
                if success:
                    self.player_status = status
                    if status.state == 'stream':
                        self.is_radio = True
                        self.playlist = []
                        if status.title2:
                            self.radio_title2 = status.title2
                        if status.title3:
                            self.radio_title3 = status.title3
                    elif self.is_radio and status.state in ('pause', 'stop'):
                        self.playlist = []
                    else:
                        self.is_radio = False
                        self.radio_title2 = ''
                        self.radio_title3 = ''
                        self.playlist = self.active_player.get_playlist()
                    self._check_mb_update()
                else:
                    logger.error(f"Error updating player status: {status}")
            except requests.RequestException as e:
                logger.error(f"Error updating player status: {e}")

    def _check_mb_update(self):
        if not self.player_status:
            return

        cover_key = self.player_status.image or ""
        if cover_key != self.cover_art_key:
            self.cover_art_key = cover_key
            self.cover_art = None
            self.cover_art_loading = True
            threading.Thread(target=self._fetch_cover_art, daemon=True).start()

        if self.is_radio:
            track_key = f"radio|{self.player_status.title1}"
        else:
            track_key = f"{self.player_status.name}|{self.player_status.artist}|{self.player_status.album}"
        if track_key != self.wiki_track_key:
            self.wiki_track_key = track_key
            self.mb_info = None
            self.mb_loading = True
            self.wiki_text = None
            self.wiki_loading = True
            if self.is_radio:
                logger.info(f"Starting station info fetch for: {self.player_status.title1}")
                threading.Thread(target=self._fetch_station_info, daemon=True).start()
            else:
                logger.info(f"Starting combined info fetch for: {track_key}")
                threading.Thread(target=self._fetch_combined_info, daemon=True).start()

        lyrics_key = f"{self.player_status.name}|{self.player_status.artist}"
        if lyrics_key != self.lyrics_track_key:
            self.lyrics_track_key = lyrics_key
            self.lyrics_text = None
            self.lyrics_loading = True
            self.lyrics_scroll = 0
            if not self.is_radio:
                threading.Thread(target=self._fetch_lyrics, daemon=True).start()
            else:
                self.lyrics_loading = False

    def _fetch_lyrics(self):
        if self.player_status:
            title = self.player_status.name
            artist = self.player_status.artist
            logger.info(f"Fetching lyrics for: {title} - {artist}")
            text = get_lyrics(title, artist)
            with self._data_lock:
                self.lyrics_text = text
                self.lyrics_loading = False
            logger.info(f"Lyrics done: {'found' if text else 'not found'}")

    def _fetch_station_info(self):
        if self.player_status:
            station_name = self.player_status.title1
            api_key = get_preference('openai_api_key')
            openai_model = get_preference('openai_model') or 'gpt-4o-mini'
            logger.info(f"Fetching station info for: {station_name}")
            text = get_station_info(station_name, api_key, model=openai_model)
            with self._data_lock:
                self.mb_info = None
                self.wiki_text = text or "No further information available."
                self.mb_loading = False
                self.wiki_loading = False
            logger.info(f"Station info done: {text[:200] if text else None}")

    def _fetch_combined_info(self):
        if self.player_status:
            title = self.player_status.name
            artist = self.player_status.artist
            album = self.player_status.album
            api_key = get_preference('openai_api_key')
            system_prompt = get_preference('openai_system_prompt')
            openai_model = get_preference('openai_model') or 'gpt-4o-mini'
            logger.info(f"Fetching combined info for: {title} - {artist} - {album}")
            result = get_combined_info(title, artist, album, api_key, system_prompt, model=openai_model)
            with self._data_lock:
                if result:
                    self.mb_info = {
                        "year": result.get("year", "-"),
                        "label": result.get("label", "-"),
                        "genre": result.get("genre", "-"),
                    }
                    self.wiki_text = result.get("track_info")
                else:
                    self.mb_info = None
                    self.wiki_text = None
                self.mb_loading = False
                self.wiki_loading = False
            logger.info(f"Combined info done: {result}")

    def _rgb_to_256(self, r, g, b):
        """Map RGB (0-255) to the nearest xterm-256 color index."""
        # Check if close to grayscale
        if abs(r - g) < 10 and abs(g - b) < 10:
            gray = (r + g + b) // 3
            if gray < 8:
                return 16
            if gray > 248:
                return 231
            return round((gray - 8) / 247 * 23) + 232
        # Map to 6x6x6 color cube (indices 16-231)
        ri = round(r / 255 * 5)
        gi = round(g / 255 * 5)
        bi = round(b / 255 * 5)
        return 16 + 36 * ri + 6 * gi + bi

    def _init_cover_colors(self, stdscr, image_data: bytes, width: int, height: int):
        """Render cover art using half-block chars with 256 colors.
        Each terminal row represents 2 pixel rows using the ▀ character
        with foreground=top pixel color, background=bottom pixel color."""
        max_pairs = curses.COLOR_PAIRS - 10 if hasattr(curses, 'COLOR_PAIRS') else 246
        img = Image.open(io.BytesIO(image_data)).convert('RGB')
        try:
            pixel_h = height * 2
            img = img.resize((width, pixel_h), Image.LANCZOS)
            pixels = list(img.get_flattened_data())

            pair_map = {}
            next_pair = 10

            result = []
            for row in range(height):
                top_y = row * 2
                bot_y = top_y + 1
                cells = []
                for col in range(width):
                    tr, tg, tb = pixels[top_y * width + col]
                    br, bg_, bb = pixels[bot_y * width + col]
                    fg = self._rgb_to_256(tr, tg, tb)
                    bg = self._rgb_to_256(br, bg_, bb)
                    key = (fg, bg)
                    if key not in pair_map:
                        if next_pair >= max_pairs:
                            pair_map[key] = 0
                        else:
                            try:
                                curses.init_pair(next_pair, fg, bg)
                            except curses.error:
                                pair_map[key] = 0
                            else:
                                pair_map[key] = next_pair
                                next_pair += 1
                    cells.append(("▀", curses.color_pair(pair_map[key])))
                result.append(cells)
            return result
        finally:
            img.close()

    def _fetch_cover_art(self):
        if not self.player_status or not self.player_status.image:
            with self._data_lock:
                self.cover_art = None
                self.cover_art_loading = False
            return
        try:
            image = self.player_status.image
            if image.startswith("http://") or image.startswith("https://"):
                url = image
            else:
                url = f"{self.active_player.base_url}{image}"
            logger.info(f"Fetching cover art: {url}")
            response = requests.get(url, timeout=5, allow_redirects=True)
            response.raise_for_status()
            with self._data_lock:
                self._cover_art_raw = response.content
                self.cover_art = True
            logger.info(f"Cover art downloaded: {len(response.content)} bytes")
        except Exception as e:
            logger.error(f"Error fetching cover art: {e}")
            with self._data_lock:
                self.cover_art = None
                self._cover_art_raw = None
        with self._data_lock:
            self.cover_art_loading = False

    def display_player_selection(self, stdscr: curses.window):
        if self.selector_shortcuts_open:
            self.display_selector_shortcuts(stdscr)
        else:
            stdscr.addstr(3, 2, "Discovered Blusound players:")
            for i, player in enumerate(self.players):
                if i == self.selected_index:
                    stdscr.attron(curses.color_pair(2))
                if player == self.active_player:
                    stdscr.addstr(4 + i, 4, f"* {player.name} ({player.host_name})")
                else:
                    stdscr.addstr(4 + i, 4, f"  {player.name} ({player.host_name})")
                if i == self.selected_index:
                    stdscr.attroff(curses.color_pair(2))
            stdscr.addstr(stdscr.getmaxyx()[0] - 1, 2, "Press '?' to show keyboard shortcuts")

    def draw_modal(self, stdscr: curses.window, title: str, entries: list):
        height, width = stdscr.getmaxyx()
        modal_w = min(width - 4, max(len(title) + 4, max(len(k) + len(v) + 7 for k, v in entries)))
        modal_h = len(entries) + 6  # title + entries + blank + copyright + repo + border
        start_y = max(0, (height - modal_h) // 2)
        start_x = max(0, (width - modal_w) // 2)

        # Top border
        stdscr.addch(start_y, start_x, curses.ACS_ULCORNER)
        stdscr.hline(start_y, start_x + 1, curses.ACS_HLINE, modal_w - 2)
        stdscr.addch(start_y, start_x + modal_w - 1, curses.ACS_URCORNER)

        # Bottom border
        stdscr.addch(start_y + modal_h - 1, start_x, curses.ACS_LLCORNER)
        stdscr.hline(start_y + modal_h - 1, start_x + 1, curses.ACS_HLINE, modal_w - 2)
        try:
            stdscr.addch(start_y + modal_h - 1, start_x + modal_w - 1, curses.ACS_LRCORNER)
        except curses.error:
            pass

        # Side borders and fill interior
        for row in range(1, modal_h - 1):
            stdscr.addch(start_y + row, start_x, curses.ACS_VLINE)
            stdscr.addstr(start_y + row, start_x + 1, " " * (modal_w - 2))
            try:
                stdscr.addch(start_y + row, start_x + modal_w - 1, curses.ACS_VLINE)
            except curses.error:
                pass

        # Title
        stdscr.addstr(start_y + 1, start_x + 2, title[:modal_w - 4], curses.A_BOLD)

        # Entries
        for i, (key, desc) in enumerate(entries):
            line = f"  {key:<12} {desc}"
            stdscr.addstr(start_y + 2 + i, start_x + 2, line[:modal_w - 4])

        # Footer
        stdscr.addstr(start_y + modal_h - 3, start_x + 2, "(c) written by xir - under GPL"[:modal_w - 4], curses.A_DIM)
        stdscr.addstr(start_y + modal_h - 2, start_x + 2, "https://github.com/xernot/bluxir"[:modal_w - 4], curses.A_DIM)

    def display_selector_shortcuts(self, stdscr: curses.window):
        shortcuts = [
            ("UP/DOWN", "Select player"),
            ("ENTER", "Activate player"),
            ("q", "Quit"),
        ]

        self.draw_modal(stdscr, "Player Selector Shortcuts", shortcuts)

    def display_player_control(self, stdscr: curses.window):
        if self.shortcuts_open:
            self.display_shortcuts(stdscr)
        elif self.active_player and isinstance(self.player_status, PlayerStatus):
            self.display_summary_view(stdscr)

    def _confirm_prompt(self, stdscr, prompt: str) -> bool:
        """Show a y/n confirmation prompt on the footer line."""
        height, width = stdscr.getmaxyx()
        footer_row = height - 2
        stdscr.move(footer_row, 0)
        stdscr.clrtoeol()
        stdscr.addstr(footer_row, 2, prompt, curses.A_BOLD)
        stdscr.refresh()
        stdscr.timeout(-1)
        confirm = stdscr.getch()
        stdscr.timeout(100)
        return confirm in (ord('y'), ord('Y'))

    def _left_text(self, stdscr, left_max, row, col, text, *args):
        """Write text truncated to the left panel boundary."""
        stdscr.addstr(row, col, text[:left_max - col], *args)

    def display_summary_view(self, stdscr: curses.window):
        player_status = self.player_status
        height, width = stdscr.getmaxyx()

        # Split layout: left 60%, right 40% for playlist
        divider_x = width * 60 // 100
        bottom_line = height - 3

        # Draw vertical divider
        for row in range(3, bottom_line):
            try:
                stdscr.addch(row, divider_x, curses.ACS_VLINE)
            except curses.error:
                pass
        # Connect to header separator
        try:
            stdscr.addch(2, divider_x, curses.ACS_TTEE)
        except curses.error:
            pass

        # Bottom horizontal line
        stdscr.hline(bottom_line, 0, curses.ACS_HLINE, width)
        try:
            stdscr.addch(bottom_line, divider_x, curses.ACS_BTEE)
        except curses.error:
            pass

        # Help text
        stdscr.addstr(height - 2, 2, "(s) search  (f) fav  (l) playlists  (w) save  (c) cover  (t) lyrics  (+/-) fav  (i) source  (?) help  (q) quit")
        version = "bluxir v2.1"
        if width > len(version) + 2:
            stdscr.addstr(height - 2, width - len(version) - 2, version)
        # Bottom horizontal line
        try:
            stdscr.hline(height - 1, 0, curses.ACS_HLINE, width)
        except curses.error:
            pass

        # === Left side: Player info ===
        left_max = divider_x - 1
        labels = ["Now Playing", "Album", "Service", "Progress"]
        max_label_width = max(len(label) for label in labels)

        self._left_text(stdscr, left_max, 3, 2, f"{'Now Playing:':<{max_label_width + 1}} {player_status.name} - {player_status.artist}")
        self._left_text(stdscr, left_max, 4, 2, f"{'Album:':<{max_label_width + 1}} {player_status.album}")
        self._left_text(stdscr, left_max, 5, 2, f"{'Service:':<{max_label_width + 1}} {player_status.service}")

        if player_status.totlen > 0:
            progress = f"{format_time(player_status.secs)} / {format_time(player_status.totlen)}"
        else:
            progress = format_time(player_status.secs)
        self._left_text(stdscr, left_max, 6, 2, f"{'Progress:':<{max_label_width + 1}} {progress}")

        # Horizontal separator (left side only)
        stdscr.hline(7, 0, curses.ACS_HLINE, divider_x)
        try:
            stdscr.addch(7, divider_x, curses.ACS_RTEE)
        except curses.error:
            pass

        if self.show_cover_art:
            # === Cover art mode ===
            art_start_row = 8
            available_rows = bottom_line - art_start_row
            available_cols = left_max - 4
            if self.cover_art_loading:
                self._left_text(stdscr, left_max, art_start_row, 2, "Loading cover art...", curses.A_DIM)
            elif self.cover_art and self._cover_art_raw:
                try:
                    art_cells = self._init_cover_colors(stdscr, self._cover_art_raw, available_cols, available_rows)
                    for i, cells in enumerate(art_cells):
                        row = art_start_row + i
                        if row >= bottom_line:
                            break
                        for col, (ch, attr) in enumerate(cells):
                            try:
                                stdscr.addstr(row, 2 + col, ch, attr)
                            except curses.error:
                                pass
                except Exception as e:
                    logger.error(f"Cover art rendering error: {e}")
                    self._left_text(stdscr, left_max, art_start_row, 2, "Error rendering cover art.", curses.A_DIM)
            else:
                self._left_text(stdscr, left_max, art_start_row, 2, "No cover art available.", curses.A_DIM)
        else:
            # === Detail + Track info mode ===
            # Detail section (two sub-columns within left half)
            sub_col2_x = 2 + (left_max // 2)
            _qm = _QUALITY_RE.search(player_status.stream_format) if player_status.stream_format else None
            detail_left = [
                ("Format", player_status.stream_format or "-"),
                ("Quality", self._derive_quality(player_status.stream_format)),
                ("Sample Rate", f"{_qm.group(2)} kHz" if _qm else "-"),
                ("Bit Depth", f"{_qm.group(1)} bit" if _qm else "-"),
                ("dB Level", f"{player_status.db:.1f}" if player_status.db is not None else "-"),
            ]
            mb = self.mb_info or {}
            detail_right = [
                ("Track-Nr", str(player_status.song + 1)),
                ("Composer", player_status.composer or "-"),
                ("Year", mb.get("year", "-")),
                ("Label", mb.get("label", "-")),
                ("Genre", mb.get("genre", "-")),
            ]
            dl = max(len(k) for k, _ in detail_left)
            dr = max(len(k) for k, _ in detail_right)
            max_detail_rows = max(len(detail_left), len(detail_right))
            for i in range(max_detail_rows):
                row = 8 + i
                if row >= bottom_line:
                    break
                if i < len(detail_left):
                    lk, lv = detail_left[i]
                    stdscr.addstr(row, 2, f"{lk + ':':<{dl + 2}}", curses.A_DIM)
                    stdscr.addstr(row, 2 + dl + 2, lv[:sub_col2_x - dl - 4])
                if i < len(detail_right):
                    rk, rv = detail_right[i]
                    val_start = sub_col2_x + dr + 2
                    val_max = max(0, left_max - val_start)
                    stdscr.addstr(row, sub_col2_x, f"{rk + ':':<{dr + 2}}"[:left_max - sub_col2_x], curses.A_DIM)
                    if val_max > 0:
                        stdscr.addstr(row, val_start, rv[:val_max])

            # Horizontal line under detail section (left side only)
            detail_bottom = 8 + max_detail_rows
            if detail_bottom < bottom_line:
                stdscr.hline(detail_bottom, 0, curses.ACS_HLINE, divider_x)
                try:
                    stdscr.addch(detail_bottom, divider_x, curses.ACS_RTEE)
                except curses.error:
                    pass

            # Track info below detail section
            current_row = detail_bottom + 1
            if current_row < bottom_line:
                if self.wiki_loading:
                    current_row += 1
                    if current_row < bottom_line:
                        self._left_text(stdscr, left_max, current_row, 2, "Loading track info...", curses.A_DIM)
                elif self.wiki_text:
                    current_row += 1  # blank line
                    if current_row < bottom_line:
                        stdscr.addstr(current_row, 2, "Track Info:", curses.A_BOLD)
                        current_row += 1
                        wrap_width = left_max - 4
                        words = self.wiki_text.split()
                        line = ""
                        for word in words:
                            if current_row >= bottom_line:
                                break
                            if line and len(line) + 1 + len(word) > wrap_width:
                                stdscr.addstr(current_row, 2, line[:wrap_width])
                                current_row += 1
                                line = word
                            else:
                                line = f"{line} {word}" if line else word
                        if line and current_row < bottom_line:
                            stdscr.addstr(current_row, 2, line[:wrap_width])
                        _model_name = get_preference('openai_model') or 'gpt-4o-mini'
                        stdscr.addstr(bottom_line - 1, 2, f"(generated by {_model_name})"[:wrap_width], curses.A_DIM)

        # === Right side: Playlist or Radio Info ===
        right_start = divider_x + 2
        right_w = width - right_start - 1

        if self.is_radio:
            stdscr.addstr(3, right_start, "Radio:"[:right_w], curses.A_BOLD)
            row = 5
            radio_t3 = player_status.title3 or self.radio_title3
            if radio_t3 and row < bottom_line:
                stdscr.addstr(row, right_start, "Now playing:"[:right_w], curses.A_DIM)
                row += 1
                words = radio_t3.split()
                line = ""
                for word in words:
                    if row >= bottom_line:
                        break
                    if line and len(line) + 1 + len(word) > right_w:
                        stdscr.addstr(row, right_start, line[:right_w])
                        row += 1
                        line = word
                    else:
                        line = f"{line} {word}" if line else word
                if line and row < bottom_line:
                    stdscr.addstr(row, right_start, line[:right_w])
                    row += 1
            radio_t2 = player_status.title2 or self.radio_title2
            if radio_t2 and row < bottom_line:
                row += 1
                if row < bottom_line:
                    stdscr.addstr(row, right_start, "Next:"[:right_w], curses.A_DIM)
                    row += 1
                    words = radio_t2.split()
                    line = ""
                    for word in words:
                        if row >= bottom_line:
                            break
                        if line and len(line) + 1 + len(word) > right_w:
                            stdscr.addstr(row, right_start, line[:right_w])
                            row += 1
                            line = word
                        else:
                            line = f"{line} {word}" if line else word
                    if line and row < bottom_line:
                        stdscr.addstr(row, right_start, line[:right_w])
        elif self.show_lyrics:
            stdscr.addstr(3, right_start, "Lyrics:"[:right_w], curses.A_BOLD)
            if self.lyrics_loading:
                stdscr.addstr(5, right_start, "Loading lyrics..."[:right_w], curses.A_DIM)
            elif self.lyrics_text:
                # Pre-render all wrapped lines
                wrapped = []
                for lyric_line in self.lyrics_text.splitlines():
                    if not lyric_line.strip():
                        wrapped.append("")
                        continue
                    while lyric_line:
                        if len(lyric_line) <= right_w:
                            wrapped.append(lyric_line)
                            break
                        split_at = lyric_line[:right_w].rfind(' ')
                        if split_at <= 0:
                            split_at = right_w
                        wrapped.append(lyric_line[:split_at])
                        lyric_line = lyric_line[split_at:].lstrip()

                # Clamp scroll
                max_visible = bottom_line - 5  # rows 4..(bottom_line-2), reserve last for attribution
                max_scroll = max(0, len(wrapped) - max_visible)
                if self.lyrics_scroll > max_scroll:
                    self.lyrics_scroll = max_scroll

                # Display scrolled lyrics
                visible = wrapped[self.lyrics_scroll:self.lyrics_scroll + max_visible]
                for i, line in enumerate(visible):
                    row = 4 + i
                    if row >= bottom_line - 1:
                        break
                    if line:
                        stdscr.addstr(row, right_start, line[:right_w])

                # Attribution + scroll hint
                attr_text = "(lyrics from lrclib.net)"
                if max_scroll > 0:
                    attr_text += "  [PgUp/PgDn to scroll]"
                stdscr.addstr(bottom_line - 1, right_start, attr_text[:right_w], curses.A_DIM)
            else:
                stdscr.addstr(5, right_start, "No lyrics available."[:right_w], curses.A_DIM)
        else:
            stdscr.addstr(3, right_start, "Playlist:"[:right_w], curses.A_BOLD)

            if self.playlist:
                current_song = player_status.song
                max_rows = bottom_line - 4

                # Scroll to keep current song visible
                start_idx = 0
                if current_song > max_rows // 2:
                    start_idx = current_song - max_rows // 2
                if start_idx + max_rows > len(self.playlist):
                    start_idx = max(0, len(self.playlist) - max_rows)

                for i in range(start_idx, min(start_idx + max_rows, len(self.playlist))):
                    row = 4 + (i - start_idx)
                    if row >= bottom_line:
                        break
                    entry = self.playlist[i]
                    nr = f"{i + 1:>3}"
                    text = f"{nr}. {entry['title']} - {entry['artist']}"
                    text = text[:right_w]

                    if i == current_song:
                        stdscr.attron(curses.color_pair(2))
                        stdscr.addstr(row, right_start, text)
                        stdscr.attroff(curses.color_pair(2))
                    else:
                        stdscr.addstr(row, right_start, text)
            else:
                stdscr.addstr(4, right_start, "No playlist loaded."[:right_w])

    def display_shortcuts(self, stdscr: curses.window):
        height, width = stdscr.getmaxyx()

        left_col = [
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
        right_col = [
            ("i", "Select input"),
            ("s", "Search"),
            ("f", "Qobuz favorites"),
            ("l", "Load playlist"),
            ("w", "Save playlist"),
            ("c", "Toggle cover art"),
            ("t", "Toggle lyrics"),
            ("PgUp/PgDn", "Scroll lyrics"),
            ("p", "Pretty print"),
            ("b", "Back to player list"),
            ("q", "Quit"),
        ]

        col_w = 30
        div_x_offset = col_w + 3  # position of vertical divider within modal
        modal_w = min(width - 4, col_w * 2 + 7)
        num_rows = max(len(left_col), len(right_col))
        # title + title-sep + entries + footer-sep + copyright + repo + borders
        modal_h = num_rows + 7
        start_y = max(0, (height - modal_h) // 2)
        start_x = max(0, (width - modal_w) // 2)
        div_x = start_x + div_x_offset

        # Top border
        stdscr.addch(start_y, start_x, curses.ACS_ULCORNER)
        stdscr.hline(start_y, start_x + 1, curses.ACS_HLINE, modal_w - 2)
        stdscr.addch(start_y, start_x + modal_w - 1, curses.ACS_URCORNER)

        # Bottom border
        stdscr.addch(start_y + modal_h - 1, start_x, curses.ACS_LLCORNER)
        stdscr.hline(start_y + modal_h - 1, start_x + 1, curses.ACS_HLINE, modal_w - 2)
        try:
            stdscr.addch(start_y + modal_h - 1, start_x + modal_w - 1, curses.ACS_LRCORNER)
        except curses.error:
            pass

        # Side borders and fill interior
        for row in range(1, modal_h - 1):
            stdscr.addch(start_y + row, start_x, curses.ACS_VLINE)
            stdscr.addstr(start_y + row, start_x + 1, " " * (modal_w - 2))
            try:
                stdscr.addch(start_y + row, start_x + modal_w - 1, curses.ACS_VLINE)
            except curses.error:
                pass

        # Title
        stdscr.addstr(start_y + 1, start_x + 2, "Keyboard Shortcuts"[:modal_w - 4], curses.A_BOLD)

        # Horizontal line under title
        title_sep_y = start_y + 2
        stdscr.addch(title_sep_y, start_x, curses.ACS_LTEE)
        stdscr.hline(title_sep_y, start_x + 1, curses.ACS_HLINE, modal_w - 2)
        stdscr.addch(title_sep_y, div_x, curses.ACS_TTEE)
        try:
            stdscr.addch(title_sep_y, start_x + modal_w - 1, curses.ACS_RTEE)
        except curses.error:
            pass

        # Vertical divider between columns
        for i in range(num_rows):
            row = start_y + 3 + i
            try:
                stdscr.addch(row, div_x, curses.ACS_VLINE)
            except curses.error:
                pass

        # Two-column entries
        col2_x = div_x + 1
        for i in range(num_rows):
            row = start_y + 3 + i
            if i < len(left_col):
                k, v = left_col[i]
                line = f"  {k:<10} {v}"
                stdscr.addstr(row, start_x + 2, line[:div_x_offset - 2])
            if i < len(right_col):
                k, v = right_col[i]
                line = f" {k:<10} {v}"
                stdscr.addstr(row, col2_x, line[:modal_w - div_x_offset - 2])

        # Horizontal separator above footer
        sep_y = start_y + 3 + num_rows
        stdscr.addch(sep_y, start_x, curses.ACS_LTEE)
        stdscr.hline(sep_y, start_x + 1, curses.ACS_HLINE, modal_w - 2)
        stdscr.addch(sep_y, div_x, curses.ACS_BTEE)
        try:
            stdscr.addch(sep_y, start_x + modal_w - 1, curses.ACS_RTEE)
        except curses.error:
            pass

        # Footer
        stdscr.addstr(start_y + modal_h - 3, start_x + 2, "(c) written by xir - under GPL"[:modal_w - 4], curses.A_DIM)
        stdscr.addstr(start_y + modal_h - 2, start_x + 2, "https://github.com/xernot/bluxir"[:modal_w - 4], curses.A_DIM)

    def display_source_selection(self, stdscr: curses.window):
        active_player = self.active_player
        height, width = stdscr.getmaxyx()
        max_display_items = height - 10

        stdscr.addstr(3, 2, "UP/DOWN: select, ENTER: play, RIGHT: expand, LEFT: back")
        stdscr.addstr(4, 2, "s: search, /: filter, n/p: next/prev page, +: add fav, -: remove fav, b: back")
        stdscr.addstr(5, 2, "Sort: (t) title  (a) artist  (o) original")
        sort_label = {"original": "Original", "title": "Title", "artist": "Artist"}.get(self.source_sort, "")
        stdscr.addstr(7, 2, f"Select Source:  [sorted by {sort_label}]")

        if not self.current_sources and self.active_player:
            self.current_sources = self.active_player.sources

        if not self.selected_source_index:
            self.selected_source_index = [0]

        if not self.current_sources:
            stdscr.addstr(8, 4, "No sources available.")
            return

        if self.selected_source_index[-1] >= len(self.current_sources):
            self.selected_source_index[-1] = max(0, len(self.current_sources) - 1)
        if self.selected_source_index[-1] < 0:
             self.selected_source_index[-1] = 0

        total_items = len(self.current_sources)

        current_page = max(0, self.selected_source_index[-1] // max_display_items)
        start_index = max(0, current_page * max_display_items)
        end_index = min(start_index + max_display_items, total_items)

        for i in range(start_index, end_index):
            source = self.current_sources[i]
            indent = "  " * (len(self.selected_source_index) - 1)
            prefix = ">" if i == self.selected_source_index[-1] else " "
            expand_indicator = "+" if source.browse_key else " "
            display_index = i - start_index
            label = f"{source.text} - {source.text2}" if source.text2 else source.text
            stdscr.addstr(8 + display_index, 4, f"{indent}{prefix} {expand_indicator} {label}")

        if total_items > max_display_items:
            page_info = f"Page {current_page + 1}/{(total_items + max_display_items - 1) // max_display_items}"
            stdscr.addstr(height - 2, width - len(page_info) - 2, page_info)

    def handle_player_selection(self, key: int) -> Tuple[bool, Optional[BlusoundPlayer], bool]:
        if self.selector_shortcuts_open:
            return False, self.active_player, False
        if key == KEY_UP and self.selected_index > 0:
            self.selected_index -= 1
        elif key == KEY_DOWN and self.selected_index < len(self.players) - 1:
            self.selected_index += 1
        elif key == KEY_ENTER and self.players:
            self.active_player = self.players[self.selected_index]
            try:
                success, status = self.active_player.get_status()
                if success:
                    self.player_status = status
                    self.playlist = self.active_player.get_playlist()
                    set_preference('player_host', self.active_player.host_name)
                    set_preference('player_name', self.active_player.name)
                    return True, self.active_player, False
                else:
                    logger.error(f"Error getting player status: {status}")
                    self.active_player = None
                    self.player_status = None
                    return False, None, False
            except requests.RequestException as e:
                logger.error(f"Error connecting to the player: {e}")
                self.active_player = None
                self.player_status = None
                return False, None, False
        elif key == KEY_QUESTION:
            self.selector_shortcuts_open = not self.selector_shortcuts_open
        return False, self.active_player, False

    def handle_player_control(self, key: int, stdscr: curses.window) -> Tuple[bool, bool]:
        if key == KEY_B:
            return False, False
        elif key == KEY_UP and self.active_player:
            new_volume = min(100, self.player_status.volume + 5) if self.player_status else 5
            success, message = self.active_player.set_volume(new_volume)
            if success and self.player_status:
                self.player_status.volume = new_volume
            self.set_message(message)
        elif key == KEY_DOWN and self.active_player:
            new_volume = max(0, self.player_status.volume - 5) if self.player_status else 0
            success, message = self.active_player.set_volume(new_volume)
            if success and self.player_status:
                self.player_status.volume = new_volume
            self.set_message(message)
        elif key == KEY_SPACE and self.active_player:
            success, message = self.active_player.toggle_play_pause()
            if success:
                self.update_player_status()
            self.set_message(message)
        elif key == KEY_RIGHT and self.active_player:
            success, message = self.active_player.skip()
            if success:
                self.last_update_time = time.time() - 2
            self.set_message(message)
        elif key == KEY_LEFT and self.active_player:
            success, message = self.active_player.back()
            if success:
                self.last_update_time = time.time() - 2
            self.set_message(message)
        elif key == ord('m') and self.active_player and self.player_status:
            success, message = self.active_player.toggle_mute(self.player_status.mute)
            if success:
                self.player_status.mute = not self.player_status.mute
            self.set_message(message)
        elif key == ord('r') and self.active_player and self.player_status:
            success, message = self.active_player.cycle_repeat(self.player_status.repeat)
            if success:
                self.player_status.repeat = {2: 0, 0: 1, 1: 2}[self.player_status.repeat]
            self.set_message(message)
        elif key == ord('x') and self.active_player and self.player_status:
            success, message = self.active_player.toggle_shuffle(self.player_status.shuffle)
            if success:
                self.player_status.shuffle = not self.player_status.shuffle
            self.set_message(message)
        elif key == ord('g') and self.active_player and self.player_status and self.playlist:
            height, width = stdscr.getmaxyx()
            footer_row = height - 2
            stdscr.move(footer_row, 0)
            stdscr.clrtoeol()
            stdscr.addstr(footer_row, 2, f"Go to track (1-{len(self.playlist)}): ", curses.A_BOLD)
            curses.echo()
            curses.curs_set(1)
            stdscr.timeout(-1)
            try:
                input_str = stdscr.getstr(footer_row, 2 + len(f"Go to track (1-{len(self.playlist)}): "), 5).decode('utf-8').strip()
                if input_str.isdigit():
                    track_num = int(input_str)
                    if 1 <= track_num <= len(self.playlist):
                        success, message = self.active_player.play_queue_track(track_num - 1)
                        if success:
                            self.update_player_status()
                        self.set_message(message)
                    else:
                        self.set_message(f"Invalid track number: {track_num}")
                elif input_str:
                    self.set_message("Cancelled")
            finally:
                curses.noecho()
                curses.curs_set(0)
                stdscr.timeout(100)
        elif key == KEY_I:
            self.source_selection_mode = True
            self.selected_source_index = [0]
            self.current_sources = self.active_player.sources
        elif key == KEY_S:
            _search_services = ('Qobuz:', 'TuneIn:')
            searchable = [s for s in self.active_player.sources if s.browse_key in _search_services]
            searchable.sort(key=lambda s: _search_services.index(s.browse_key))
            if searchable:
                self.search_mode = True
                self.search_phase = 'source_select'
                self.searchable_sources = searchable
                self.search_source_index = 0
                self.search_results = []
                self.search_selected_index = 0
                self.search_history = []
                self.active_search_key = None
                self.search_source_name = ""
            else:
                self.set_message("No searchable sources available")
        elif key == KEY_F and self.active_player:
            self.set_message("Loading Qobuz favorites...")
            success, items = self.active_player.browse_path("Qobuz", "Fav", "Album")
            if items:
                self.source_selection_mode = True
                self.current_sources = items
                self.selected_source_index = [0]
                if success:
                    self.set_message(f"Found {len(items)} favorite albums")
                else:
                    self.set_message("Navigate to find your favorites")
            else:
                self.set_message("Qobuz not found in sources")
        elif key == KEY_W and self.active_player:
            height, width = stdscr.getmaxyx()
            footer_row = height - 2
            stdscr.move(footer_row, 0)
            stdscr.clrtoeol()
            name = self.get_input(stdscr, "Save playlist as: ")
            if name:
                success, message = self.active_player.save_playlist(name)
                self.set_message(message)
            else:
                self.set_message("Cancelled")
        elif key == KEY_L and self.active_player:
            playlists = self.active_player.get_playlists()
            if playlists:
                self.source_selection_mode = True
                self.current_sources = playlists
                self.selected_source_index = [0]
                self.set_message(f"Found {len(playlists)} playlists")
            else:
                self.set_message("No saved playlists")
        elif key == ord('c') and self.active_player:
            self.show_cover_art = not self.show_cover_art
        elif key == ord('t') and self.active_player and not self.is_radio:
            self.show_lyrics = not self.show_lyrics
            self.lyrics_scroll = 0
        elif key == curses.KEY_PPAGE and self.show_lyrics:
            self.lyrics_scroll = max(0, self.lyrics_scroll - 5)
        elif key == curses.KEY_NPAGE and self.show_lyrics:
            self.lyrics_scroll += 5
        elif key == ord('+') and self.active_player and self.player_status:
            if not self.player_status.albumid:
                self.set_message("No album info available")
            elif self.player_status.is_favourite:
                self.set_message("Already in favourites")
            else:
                album = self.player_status.album or "this album"
                if self._confirm_prompt(stdscr, f"Add '{album}' to favourites? (y/n)"):
                    success, message = self.active_player.add_album_favourite(self.player_status)
                    self.set_message(message)
                    if success:
                        self.update_player_status()
                else:
                    self.set_message("Cancelled")
        elif key == ord('-') and self.active_player and self.player_status:
            if not self.player_status.albumid:
                self.set_message("No album info available")
            else:
                album = self.player_status.album or "this album"
                if self._confirm_prompt(stdscr, f"Remove '{album}' from favourites? (y/n)"):
                    success, message = self.active_player.remove_album_favourite(self.player_status)
                    self.set_message(message)
                    if success:
                        self.update_player_status()
                else:
                    self.set_message("Cancelled")
        elif key == KEY_QUESTION:
            self.shortcuts_open = not self.shortcuts_open
        elif key == KEY_P:
            self.pretty_print_player_state(stdscr)
        return True, False

    def pretty_print_player_state(self, stdscr: curses.window):
        if self.active_player and self.player_status:
            def serialize_source(source):
                return {
                    "text": source.text,
                    "image": source.image,
                    "browse_key": source.browse_key,
                    "play_url": source.play_url,
                    "input_type": source.input_type,
                    "type": source.type,
                    "children": [serialize_source(child) for child in source.children]
                }

            player_state = {
                "player": {
                    "name": self.active_player.name,
                    "host_name": self.active_player.host_name,
                    "base_url": self.active_player.base_url,
                    "sources": [serialize_source(source) for source in self.active_player.sources]
                },
                "status": self.player_status.__dict__
            }
            pretty_state = json.dumps(player_state, indent=2)

            logger.info(f"Pretty print data:\n{pretty_state}")

            height, width = stdscr.getmaxyx()
            lines = pretty_state.splitlines()

            content_height = len(lines)
            content_width = 0
            if lines:
                content_width = max(len(line) for line in lines)
            content_width = min(content_width, width - 6)

            popup_content_h = min(content_height, height - 6)
            popup_content_w = content_width

            popup_height = popup_content_h + 2
            popup_width = popup_content_w + 2

            popup_start_y = (height - popup_height) // 2
            popup_start_x = (width - popup_width) // 2

            popup_win = curses.newwin(popup_height, popup_width, popup_start_y, popup_start_x)
            popup_win.box()
            popup_win.addstr(0, 2, " Player State (UP/DOWN scroll, 'q' to close) ", curses.A_REVERSE)

            content_pad = curses.newpad(content_height + 1, content_width + 1)
            for i, line_text in enumerate(lines):
                try:
                    content_pad.addstr(i, 0, line_text[:content_width])
                except curses.error:
                    pass

            pad_pos = 0
            popup_win.refresh()

            while True:
                sminrow = popup_start_y + 1
                smincol = popup_start_x + 1
                smaxrow = popup_start_y + popup_height - 2
                smaxcol = popup_start_x + popup_width - 2

                content_pad.refresh(pad_pos, 0, sminrow, smincol, smaxrow, smaxcol)

                key_press = stdscr.getch()

                if key_press == ord('q'):
                    break
                elif key_press == curses.KEY_DOWN:
                    if pad_pos < content_height - popup_content_h:
                         pad_pos += 1
                elif key_press == curses.KEY_UP:
                    if pad_pos > 0:
                        pad_pos -= 1

            stdscr.touchwin()
            stdscr.refresh()

    def handle_source_selection(self, key: int, stdscr: curses.window) -> Tuple[bool, List[int]]:
        height, _ = stdscr.getmaxyx()
        max_display_items = height - 10
        logger.info("Key pressed in source selection: %s", key)

        if key == KEY_B:
            self.source_selection_mode = False
            self.current_sources = []
            self.selected_source_index = [0]
            self.source_sort = 'original'
            self.unsorted_sources = []
            self.source_filter_backup = []
            return False, self.selected_source_index

        if not self.selected_source_index:
            self.selected_source_index = [0]

        if key == KEY_LEFT:
            if len(self.selected_source_index) > 1:
                self.selected_source_index.pop()
                path_sources = self.active_player.sources
                for i in range(len(self.selected_source_index) - 1):
                    idx = self.selected_source_index[i]
                    if idx < len(path_sources) and hasattr(path_sources[idx], 'children'):
                        path_sources = path_sources[idx].children
                    else:
                        path_sources = []
                        break
                self.current_sources = path_sources

                if self.current_sources and self.selected_source_index[-1] >= len(self.current_sources):
                    self.selected_source_index[-1] = max(0, len(self.current_sources) - 1)
                elif not self.current_sources:
                    self.selected_source_index[-1] = 0
            else:
                self.source_selection_mode = False
                self.current_sources = []
                self.selected_source_index = [0]
                return False, self.selected_source_index
            return True, self.selected_source_index

        if not self.current_sources:
            self.set_message("No sources available to navigate.")
            return True, self.selected_source_index

        current_idx_val = self.selected_source_index[-1]
        if current_idx_val >= len(self.current_sources):
            self.selected_source_index[-1] = len(self.current_sources) - 1
        if current_idx_val < 0: self.selected_source_index[-1] = 0

        if key == KEY_UP:
            if self.selected_source_index[-1] > 0:
                self.selected_source_index[-1] -= 1
        elif key == KEY_DOWN:
            if self.selected_source_index[-1] < len(self.current_sources) - 1:
                self.selected_source_index[-1] += 1
        elif key == ord('n'):
            current_page_items = max_display_items if max_display_items > 0 else len(self.current_sources)
            next_page_start_index = ((self.selected_source_index[-1] // current_page_items) + 1) * current_page_items
            if next_page_start_index < len(self.current_sources):
                self.selected_source_index[-1] = next_page_start_index
            else:
                self.selected_source_index[-1] = len(self.current_sources) -1
        elif key == ord('p'):
            current_page_items = max_display_items if max_display_items > 0 else len(self.current_sources)
            prev_page_start_index = ((self.selected_source_index[-1] // current_page_items) - 1) * current_page_items
            if prev_page_start_index >= 0:
                self.selected_source_index[-1] = prev_page_start_index
            else:
                self.selected_source_index[-1] = 0
        elif key == KEY_RIGHT:
            selected_source = self.current_sources[self.selected_source_index[-1]]
            if selected_source.browse_key:
                self.active_player.get_nested_sources(selected_source)
                if selected_source.children:
                    self.current_sources = selected_source.children
                    self.selected_source_index.append(0)
                else:
                    self.set_message(f"No nested sources found for: {selected_source.text}")
            else:
                self.set_message(f"Cannot expand: {selected_source.text}")
        elif key == KEY_ENTER:
            selected_source = self.current_sources[self.selected_source_index[-1]]
            if selected_source.play_url:
                self.set_message(f"Playing: {selected_source.text}")
                success, message = self.active_player.select_input(selected_source)
                if success:
                    self.update_player_status()
                    return False, self.selected_source_index
                self.set_message(message)
            elif selected_source.browse_key:
                self.active_player.get_nested_sources(selected_source)
                if selected_source.children:
                    self.current_sources = selected_source.children
                    self.selected_source_index.append(0)
                else:
                    self.set_message(f"No nested sources found for: {selected_source.text}")
            else:
                self.set_message(f"Cannot play or expand: {selected_source.text}")
        elif key == KEY_S:
            search_key = self.current_sources[0].search_key if self.current_sources else None
            if search_key:
                self.search_mode = True
                self.active_search_key = search_key
                self.search_source_name = ""
                self.search_phase = 'input'
                self.search_results = []
                self.search_selected_index = 0
                self.search_history = []
                return False, self.selected_source_index
            else:
                self.set_message("Search not available for this source")
        elif key == ord('t'):
            if not self.unsorted_sources:
                self.unsorted_sources = list(self.current_sources)
            self.current_sources.sort(key=lambda s: s.text.lower())
            self.source_sort = 'title'
            self.selected_source_index[-1] = 0
        elif key == ord('a'):
            if not self.unsorted_sources:
                self.unsorted_sources = list(self.current_sources)
            self.current_sources.sort(key=lambda s: (s.text2 or '').lower())
            self.source_sort = 'artist'
            self.selected_source_index[-1] = 0
        elif key == ord('o'):
            if self.unsorted_sources:
                self.current_sources = list(self.unsorted_sources)
            self.source_sort = 'original'
            self.selected_source_index[-1] = 0
        elif key == ord('+') and self.current_sources:
            selected = self.current_sources[self.selected_source_index[-1]]
            if not selected.context_menu_key:
                self.set_message("Cannot add this item to favourites")
            elif selected.is_favourite:
                self.set_message("Already in favourites")
            else:
                label = f"{selected.text} - {selected.text2}" if selected.text2 else selected.text
                if self._confirm_prompt(stdscr, f"Add '{label}' to favourites? (y/n)"):
                    success, message = self.active_player.toggle_favourite(selected, add=True)
                    self.set_message(message)
                else:
                    self.set_message("Cancelled")
        elif key == ord('-') and self.current_sources:
            selected = self.current_sources[self.selected_source_index[-1]]
            if not selected.context_menu_key:
                self.set_message("Cannot remove this item from favourites")
            else:
                label = f"{selected.text} - {selected.text2}" if selected.text2 else selected.text
                if self._confirm_prompt(stdscr, f"Remove '{label}' from favourites? (y/n)"):
                    success, message = self.active_player.toggle_favourite(selected, add=False)
                    self.set_message(message)
                    if success:
                        self.current_sources.remove(selected)
                        if self.unsorted_sources and selected in self.unsorted_sources:
                            self.unsorted_sources.remove(selected)
                        if self.source_filter_backup and selected in self.source_filter_backup:
                            self.source_filter_backup.remove(selected)
                        if self.selected_source_index[-1] >= len(self.current_sources):
                            self.selected_source_index[-1] = max(0, len(self.current_sources) - 1)
                else:
                    self.set_message("Cancelled")
        elif key == ord('/') and self.current_sources:
            height, width = stdscr.getmaxyx()
            footer_row = height - 2
            stdscr.move(footer_row, 0)
            stdscr.clrtoeol()
            filter_term = self.get_input(stdscr, "Filter: ")
            if filter_term:
                if not self.source_filter_backup:
                    self.source_filter_backup = list(self.current_sources)
                term = filter_term.lower()
                self.current_sources = [
                    s for s in self.source_filter_backup
                    if term in s.text.lower() or term in (s.text2 or '').lower()
                ]
                self.selected_source_index[-1] = 0
                if self.current_sources:
                    self.set_message(f"Filter: '{filter_term}' ({len(self.current_sources)} matches)")
                else:
                    self.set_message(f"No matches for '{filter_term}'")
                    self.current_sources = list(self.source_filter_backup)
            elif self.source_filter_backup:
                self.current_sources = list(self.source_filter_backup)
                self.source_filter_backup = []
                self.selected_source_index[-1] = 0
                self.set_message("Filter cleared")
        return True, self.selected_source_index

    def main(self, stdscr: curses.window):
        sys.stdout.write("\033]0;bluxir\007")
        sys.stdout.flush()
        stdscr.erase()
        curses.curs_set(0)
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(2, curses.COLOR_BLACK, curses.COLOR_WHITE)

        player_mode: bool = False
        discovery_started: bool = False

        player_host = get_preference('player_host')
        player_name = get_preference('player_name')

        if player_host:
            try:
                player = BlusoundPlayer(host_name=player_host, name=player_name or player_host)
                success, status = player.get_status()
                if success:
                    self.active_player = player
                    self.player_status = status
                    self.playlist = player.get_playlist()
                    self._check_mb_update()
                    self.players = [player]
                    player_mode = True
            except Exception as e:
                logger.error(f"Failed to connect to stored player: {e}")

        if not player_mode:
            self.players = threaded_discover()
            discovery_started = True
            stdscr.addstr(3, 2, "Discovering Blusound players...")
            stdscr.refresh()

        while True:
            # Handle terminal resize
            if curses.is_term_resized(stdscr.getmaxyx()[0], stdscr.getmaxyx()[1]):
                new_h, new_w = stdscr.getmaxyx()
                curses.resizeterm(new_h, new_w)
                stdscr.clear()

            stdscr.erase()

            if not player_mode:
                view = "Player Selection"
            elif self.search_mode:
                view = "Search"
            elif self.source_selection_mode:
                view = "Source Selection"
            else:
                view = "BluOS Player Control"

            self.draw_header(stdscr, view)

            if not player_mode:
                if not discovery_started:
                    self.players = threaded_discover()
                    discovery_started = True
                self.display_player_selection(stdscr)
            else:
                if self.search_mode:
                    if self.search_phase == 'source_select':
                        self.display_search_source_selection(stdscr)
                    elif self.search_phase == 'results':
                        self.display_search_results(stdscr)
                elif not self.source_selection_mode:
                    self.display_player_control(stdscr)
                else:
                    self.display_source_selection(stdscr)

            stdscr.refresh()
            stdscr.timeout(100)
            key = stdscr.getch()

            if key == ord('q'):
                if self._confirm_prompt(stdscr, "Quit bluxir? (y/n)"):
                    break
                continue
            elif not player_mode:
                if self.selector_shortcuts_open:
                    if key != -1:
                        self.selector_shortcuts_open = False
                else:
                    player_mode, self.active_player, _ = self.handle_player_selection(key)
                    if player_mode:
                        self.update_player_status()
            else:
                if self.shortcuts_open:
                    if key != -1:
                        self.shortcuts_open = False
                elif self.search_mode:
                    self.search_mode = self.handle_search(key, stdscr)
                elif not self.source_selection_mode:
                    player_mode, _ = self.handle_player_control(key, stdscr)
                else:
                    self.source_selection_mode, _ = self.handle_source_selection(key, stdscr)

            current_time = time.time()
            if self.player_status and self.player_status.state in ('stream', 'play'):
                if current_time - self.last_progress_time >= 1:
                    self.player_status.secs += 1
                    if self.player_status.totlen > 0:
                        self.player_status.secs = min(self.player_status.secs, self.player_status.totlen)
                    self.last_progress_time = current_time
            if self.active_player and current_time - self.last_update_time >= 3:
                self.update_player_status()
                self.last_update_time = current_time
                self.last_progress_time = current_time

    def get_input(self, stdscr, prompt):
        curses.noecho()
        stdscr.addstr(prompt)
        stdscr.refresh()
        input_str = ""

        while True:
            try:
                ch = stdscr.getch()

                if ch == 10:  # Enter
                    break
                elif ch == 27:  # Escape
                    input_str = ""
                    break
                elif ch in (127, 8, curses.KEY_BACKSPACE):
                    if input_str:
                        input_str = input_str[:-1]
                        y, x = stdscr.getyx()
                        stdscr.addstr(y, x-1, " ")
                        stdscr.move(y, x-1)
                elif ch >= 32 and ch <= 126:
                    input_str += chr(ch)
                    stdscr.addch(ch)
                stdscr.refresh()
            except ValueError:
                pass

        return input_str.strip()

    def handle_search(self, key: int, stdscr: curses.window) -> bool:
        if self.search_phase == 'source_select':
            if key == KEY_UP and self.search_source_index > 0:
                self.search_source_index -= 1
            elif key == KEY_DOWN and self.search_source_index < len(self.searchable_sources) - 1:
                self.search_source_index += 1
            elif key == KEY_ENTER and self.searchable_sources:
                selected = self.searchable_sources[self.search_source_index]
                self.set_message(f"Loading {selected.text}...")
                nested = self.active_player.capture_sources(selected.browse_key)
                search_key = nested[0].search_key if nested else None
                if search_key:
                    self.active_search_key = search_key
                    self.search_source_name = selected.text
                    self.search_phase = 'input'
                else:
                    self.set_message(f"{selected.text} doesn't support search")
                    return False
            elif key == KEY_B:
                return False
            return True

        elif self.search_phase == 'input':
            self.set_message("Enter search term:")
            stdscr.move(3, 2)
            prompt = f"Search {self.search_source_name}: " if self.search_source_name else "Search: "
            search_term = self.get_input(stdscr, prompt)
            if search_term:
                self.set_message(f"Searching for: {search_term}")
                stdscr.refresh()
                self.search_results = self.active_player.search(self.active_search_key, search_term)
                self.search_selected_index = 0
                if self.search_results:
                    self.search_phase = 'results'
                    return True
                else:
                    self.set_message(f"No results for: {search_term}")
                    return False
            else:
                self.set_message("Search cancelled")
                return False

        elif self.search_phase == 'results':
            if key == KEY_UP and self.search_selected_index > 0:
                self.search_selected_index -= 1
            elif key == KEY_DOWN and self.search_selected_index < len(self.search_results) - 1:
                self.search_selected_index += 1
            elif key == KEY_RIGHT and self.search_results:
                selected = self.search_results[self.search_selected_index]
                if selected.browse_key:
                    self.active_player.get_nested_sources(selected)
                    if selected.children:
                        self.search_history.append((self.search_results, self.search_selected_index))
                        self.search_results = selected.children
                        self.search_selected_index = 0
                    else:
                        self.set_message(f"No items in: {selected.text}")
                else:
                    self.set_message(f"Cannot expand: {selected.text}")
            elif key == KEY_ENTER and self.search_results:
                selected = self.search_results[self.search_selected_index]
                if selected.context_menu_key and selected.type != 'album':
                    self._execute_queue_action(selected, stdscr)
                elif selected.play_url:
                    success, message = self.active_player.select_input(selected)
                    self.set_message(message)
                    if success:
                        self.update_player_status()
                        return False
                elif selected.browse_key:
                    self.active_player.get_nested_sources(selected)
                    if selected.children:
                        self.search_history.append((self.search_results, self.search_selected_index))
                        self.search_results = selected.children
                        self.search_selected_index = 0
                    else:
                        self.set_message(f"No items in: {selected.text}")
                else:
                    self.set_message(f"Cannot play: {selected.text}")
            elif key == ord('+') and self.search_results:
                selected = self.search_results[self.search_selected_index]
                if selected.context_menu_key and not selected.is_favourite:
                    label = f"{selected.text} - {selected.text2}" if selected.text2 else selected.text
                    if self._confirm_prompt(stdscr, f"Add '{label}' to favourites? (y/n)"):
                        success, message = self.active_player.toggle_favourite(selected, add=True)
                        self.set_message(message)
                    else:
                        self.set_message("Cancelled")
                else:
                    self.set_message("Cannot add to favourites" if not selected.context_menu_key else "Already in favourites")
            elif key in (KEY_B, KEY_LEFT):
                if self.search_history:
                    self.search_results, self.search_selected_index = self.search_history.pop()
                else:
                    return False
            return True

        return False

    def display_search_source_selection(self, stdscr: curses.window):
        height, width = stdscr.getmaxyx()
        max_display = height - 8

        stdscr.addstr(3, 2, "Select a source to search:")
        stdscr.addstr(4, 2, "UP/DOWN: navigate, ENTER: select, b: back")

        for i, source in enumerate(self.searchable_sources[:max_display]):
            if i == self.search_source_index:
                stdscr.attron(curses.color_pair(2))
            stdscr.addstr(6 + i, 4, f"  {source.text}")
            if i == self.search_source_index:
                stdscr.attroff(curses.color_pair(2))

    def display_search_results(self, stdscr: curses.window):
        height, width = stdscr.getmaxyx()
        max_display_items = height - 8

        stdscr.addstr(3, 2, "Search Results:")
        stdscr.addstr(4, 2, "UP/DOWN: navigate, ENTER: play, RIGHT: expand, b: back")

        if not self.search_results:
            stdscr.addstr(6, 4, "No results found.")
            return

        start = 0
        if self.search_selected_index >= max_display_items:
            start = self.search_selected_index - max_display_items + 1

        for i in range(start, min(start + max_display_items, len(self.search_results))):
            source = self.search_results[i]
            display_i = i - start
            prefix = "+" if source.browse_key else " "
            if i == self.search_selected_index:
                stdscr.attron(curses.color_pair(2))
            label = f"{source.text} - {source.text2}" if source.text2 else source.text
            stdscr.addstr(6 + display_i, 4, f"{prefix} {label}")
            if i == self.search_selected_index:
                stdscr.attroff(curses.color_pair(2))

def signal_handler(sig, frame):
    curses.endwin()
    sys.exit(0)

if __name__ == "__main__":
    cli = BlusoundCLI()
    signal.signal(signal.SIGINT, signal_handler)
    try:
        curses.wrapper(cli.main)
    except Exception as e:
        logger.error(f"Error in main loop: {e}")
        import pdb; pdb.post_mortem()
        raise e
