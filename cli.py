import curses
import time
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

# Set up logging
log_file = 'logs/cli.log'
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
        self.active_search_key: Optional[str] = None
        self.search_source_name: str = ""

    def set_message(self, message: str):
        if message:
            self.header_message = message
            self.header_message_time = time.time()

    def draw_header(self, stdscr: curses.window, view: str):
        height, width = stdscr.getmaxyx()
        # Top line with credit
        stdscr.hline(0, 0, curses.ACS_HLINE, width)
        credit = "written by xir"
        if width > len(credit) + 2:
            stdscr.addstr(0, width - len(credit) - 2, credit)
        # Header
        header = f"Blusound CLI - {view}"
        if self.active_player:
            header += f" - {self.active_player.name}"
        stdscr.addstr(1, 2, header[:width - 4], curses.A_BOLD)
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
                else:
                    logger.error(f"Error updating player status: {status}")
            except requests.RequestException as e:
                logger.error(f"Error updating player status: {e}")

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
        modal_h = len(entries) + 4  # title + blank + entries + footer
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
        footer = "Press any key to close"
        stdscr.addstr(start_y + modal_h - 2, start_x + 2, footer[:modal_w - 4], curses.A_DIM)

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
            stdscr.addstr(stdscr.getmaxyx()[0] - 2, 2, "Press '?' for shortcuts")
            stdscr.addstr(stdscr.getmaxyx()[0] - 1, 2, "Press 's' to search, 'i' to select input")

    def display_summary_view(self, stdscr: curses.window):
        player_status = self.player_status
        height, width = stdscr.getmaxyx()
        labels = ["Status", "Volume", "Now Playing", "Album", "Service", "Progress"]
        max_label_width = max(len(label) for label in labels)

        stdscr.addstr(3, 2, f"{'Status:':<{max_label_width + 1}} {player_status.state}")
        volume_bar = create_volume_bar(player_status.volume)
        stdscr.addstr(4, 2, f"{'Volume:':<{max_label_width + 1}} {volume_bar} {player_status.volume}%")
        stdscr.addstr(5, 2, f"{'Now Playing:':<{max_label_width + 1}} {player_status.name} - {player_status.artist}")
        stdscr.addstr(6, 2, f"{'Album:':<{max_label_width + 1}} {player_status.album}")
        stdscr.addstr(7, 2, f"{'Service:':<{max_label_width + 1}} {player_status.service}")

        if player_status.totlen > 0:
            progress = f"{format_time(player_status.secs)} / {format_time(player_status.totlen)}"
        else:
            progress = format_time(player_status.secs)
        stdscr.addstr(8, 2, f"{'Progress:':<{max_label_width + 1}} {progress}")

        stdscr.hline(9, 0, curses.ACS_HLINE, width)

        # Detail section below separator
        col2_x = width // 2
        detail_left = [
            ("Format", player_status.stream_format or "-"),
            ("Quality", str(player_status.quality) if player_status.quality else "-"),
            ("dB Level", f"{player_status.db:.1f}" if player_status.db else "-"),
            ("Service", player_status.service_name or player_status.service or "-"),
        ]
        repeat_map = {0: "Off", 1: "All", 2: "One"}
        detail_right = [
            ("Shuffle", "On" if player_status.shuffle else "Off"),
            ("Repeat", repeat_map.get(player_status.repeat, "Off")),
            ("Mute", "On" if player_status.mute else "Off"),
            ("Song", str(player_status.song) if player_status.song else "-"),
        ]
        dl = max(len(k) for k, _ in detail_left)
        dr = max(len(k) for k, _ in detail_right)
        for i, ((lk, lv), (rk, rv)) in enumerate(zip(detail_left, detail_right)):
            row = 10 + i
            if row >= height - 3:
                break
            stdscr.addstr(row, 2, f"{lk + ':':<{dl + 2}}", curses.A_DIM)
            stdscr.addstr(row, 2 + dl + 2, lv[:col2_x - dl - 6])
            stdscr.addstr(row, col2_x, f"{rk + ':':<{dr + 2}}", curses.A_DIM)
            stdscr.addstr(row, col2_x + dr + 2, rv[:width - col2_x - dr - 4])

    def display_shortcuts(self, stdscr: curses.window):
        height, width = stdscr.getmaxyx()

        shortcuts = [
            ("UP/DOWN", "Adjust volume"),
            ("SPACE", "Play/Pause"),
            (">/<", "Skip/Previous track"),
            ("i", "Select input"),
            ("s", "Search"),
            ("f", "Qobuz favorites"),
            ("p", "Pretty print"),
            ("b", "Back to player list"),
            ("ESC", "Cancel"),
            ("q", "Quit"),
        ]

        self.draw_modal(stdscr, "Keyboard Shortcuts", shortcuts)

    def display_source_selection(self, stdscr: curses.window):
        active_player = self.active_player
        height, width = stdscr.getmaxyx()
        max_display_items = height - 10

        stdscr.addstr(3, 2, "UP/DOWN: select, ENTER: play, RIGHT: expand, LEFT: back")
        stdscr.addstr(4, 2, "s: search, n: next page, p: previous page, b: back to player control")
        stdscr.addstr(6, 2, "Select Source:")

        if not self.current_sources and self.active_player:
            self.current_sources = self.active_player.sources

        if not self.selected_source_index:
            self.selected_source_index = [0]

        if not self.current_sources:
            stdscr.addstr(7, 4, "No sources available.")
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
            stdscr.addstr(7 + display_index, 4, f"{indent}{prefix} {expand_indicator} {source.text}")

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
                    set_preference('last_player', self.active_player.name)
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
            if success:
                self.update_player_status()
            self.set_message(message)
        elif key == KEY_DOWN and self.active_player:
            new_volume = max(0, self.player_status.volume - 5) if self.player_status else 0
            success, message = self.active_player.set_volume(new_volume)
            if success:
                self.update_player_status()
            self.set_message(message)
        elif key == KEY_SPACE and self.active_player:
            success, message = self.active_player.toggle_play_pause()
            if success:
                self.update_player_status()
            self.set_message(message)
        elif key == KEY_RIGHT and self.active_player:
            success, message = self.active_player.skip()
            if success:
                self.update_player_status()
            self.set_message(message)
        elif key == KEY_LEFT and self.active_player:
            success, message = self.active_player.back()
            if success:
                self.update_player_status()
            self.set_message(message)
        elif key == KEY_I:
            self.source_selection_mode = True
            self.selected_source_index = [0]
            self.current_sources = self.active_player.sources
        elif key == KEY_S:
            searchable = [s for s in self.active_player.sources if s.browse_key]
            if searchable:
                self.search_mode = True
                self.search_phase = 'source_select'
                self.searchable_sources = searchable
                self.search_source_index = 0
                self.search_results = []
                self.search_selected_index = 0
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
                return False, self.selected_source_index
            else:
                self.set_message("Search not available for this source")
        return True, self.selected_source_index

    def main(self, stdscr: curses.window):
        stdscr.erase()
        curses.curs_set(0)
        curses.init_pair(2, curses.COLOR_BLACK, curses.COLOR_WHITE)

        self.players = threaded_discover()
        stdscr.addstr(3, 2, "Discovering Blusound players...")
        stdscr.refresh()

        player_mode: bool = False

        last_player_name = get_preference('last_player')
        if last_player_name:
            for i, player in enumerate(self.players):
                if player.name == last_player_name:
                    self.selected_index = i
                    player_mode, self.active_player, _ = self.handle_player_selection(KEY_ENTER)
                    break

        while True:
            stdscr.erase()

            if not player_mode:
                view = "Player Selection"
            elif self.search_mode:
                view = "Search"
            elif self.source_selection_mode:
                view = "Source Selection"
            else:
                view = "Player Control"

            self.draw_header(stdscr, view)

            if not player_mode:
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
                break
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
            if self.active_player and current_time - self.last_update_time >= 10:
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
                        self.search_results = selected.children
                        self.search_selected_index = 0
                    else:
                        self.set_message(f"No items in: {selected.text}")
                else:
                    self.set_message(f"Cannot expand: {selected.text}")
            elif key == KEY_ENTER and self.search_results:
                selected = self.search_results[self.search_selected_index]
                if selected.play_url:
                    success, message = self.active_player.select_input(selected)
                    self.set_message(message)
                    if success:
                        self.update_player_status()
                        return False
                elif selected.browse_key:
                    self.active_player.get_nested_sources(selected)
                    if selected.children:
                        self.search_results = selected.children
                        self.search_selected_index = 0
                    else:
                        self.set_message(f"No items in: {selected.text}")
                else:
                    self.set_message(f"Cannot play: {selected.text}")
            elif key == KEY_B:
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
            stdscr.addstr(6 + display_i, 4, f"{prefix} {source.text}")
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
