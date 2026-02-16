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

import requests
import xml.etree.ElementTree as ET
import time
import threading
import logging
from logging.handlers import RotatingFileHandler
from dataclasses import dataclass
from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
from typing import List, Dict, Tuple, Optional, Union
from dataclasses import dataclass, field
import os

# Ensure logs directory exists
os.makedirs('logs', exist_ok=True)

# Set up logging
log_file = 'logs/cli.log'
log_handler = RotatingFileHandler(log_file, maxBytes=1024*1024, backupCount=1)
log_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                                  datefmt='%Y-%m-%d %H:%M:%S')
log_handler.setFormatter(log_formatter)
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
logger.addHandler(log_handler)

@dataclass
class PlayerStatus:
    etag: str = ''
    album: str = ''
    artist: str = ''
    name: str = ''
    state: str = ''
    volume: int = 0
    service: str = ''
    inputId: str = ''
    can_move_playback: bool = False
    can_seek: bool = False
    cursor: int = 0
    db: float = 0.0
    fn: str = ''
    image: str = ''
    indexing: int = 0
    mid: int = 0
    mode: int = 0
    mute: bool = False
    pid: int = 0
    prid: int = 0
    quality: str = ''
    repeat: int = 0
    service_icon: str = ''
    service_name: str = ''
    shuffle: bool = False
    sid: int = 0
    sleep: str = ''
    song: int = 0
    stream_format: str = ''
    sync_stat: int = 0
    title1: str = ''
    title2: str = ''
    title3: str = ''
    totlen: int = 0
    secs: int = 0
    albumid: str = ''
    artistid: str = ''
    is_favourite: bool = False

@dataclass
class PlayerSource:
    text: str
    image: str
    browse_key: Optional[str]
    play_url: Optional[str]
    input_type: Optional[str]
    type: str
    text2: str = ''
    context_menu_key: Optional[str] = None
    is_favourite: bool = False
    search_key: Optional[str] = None
    children: List['PlayerSource'] = field(default_factory=list)

def _safe_find(element, tag, default=''):
    """Safely find an XML element's text, returning default if not found."""
    found = element.find(tag)
    return found.text if found is not None else default


def _safe_int(value, default=0):
    """Safely convert a value to int, returning default on failure."""
    try:
        return int(value)
    except (ValueError, TypeError):
        return default


class BlusoundPlayer:
    def __init__(self, host_name, name):
        self.host_name = host_name
        self.name = name
        self.base_url = f"http://{self.host_name}:11000"
        self.sources: List[PlayerSource] = []
        logger.info(f"Initialized BlusoundPlayer: {self.name} at {self.host_name}")
        self.fetch_player_name()
        self.initialize_sources()

    def fetch_player_name(self):
        try:
            response = self.request("/SyncStatus")
            root = ET.fromstring(response.text)
            name = root.get('name', '')
            if name:
                self.name = name
                logger.info(f"Player name: {self.name}")
        except requests.RequestException as e:
            logger.error(f"Error fetching player name: {e}")

    def request(self, url: str, params: Optional[Dict] = None) -> requests.Response:
        full_url = f"{self.base_url}{url}"
        logger.debug(f"Sending request to: {full_url}")
        logger.debug(f"Request params: {params}")
        response = requests.get(full_url, params=params, timeout=5)
        logger.debug(f"Response status code: {response.status_code}")
        logger.debug(f"Response content: {response.text[:500]}")
        response.raise_for_status()
        return response

    @staticmethod
    def _make_source(item, search_key=None) -> PlayerSource:
        """Create a PlayerSource from an XML item element."""
        return PlayerSource(
            text=item.get('text', '').strip(),
            image=item.get('image', ''),
            browse_key=item.get('browseKey'),
            play_url=item.get('playURL'),
            input_type=item.get('inputType'),
            type=item.get('type', ''),
            text2=item.get('text2', ''),
            context_menu_key=item.get('contextMenuKey'),
            is_favourite=item.get('isFavourite') == 'true',
            search_key=search_key,
        )

    def capture_sources(self, browse_key: Optional[str] = None) -> List[PlayerSource]:
        url = "/Browse"
        params = {"key": browse_key} if browse_key else None
        try:
            response = self.request(url, params)
            root = ET.fromstring(response.text)
            sources = []

            # Capture the search_key from the <browse> element
            browse_search_key = root.get('searchKey')

            # Find items both at root level and inside <category> elements
            for item in root.iter('item'):
                sources.append(self._make_source(item, browse_search_key))
            logger.info(f"Captured {len(sources)} sources for {self.name}")
            return sources
        except requests.RequestException as e:
            logger.error(f"Error capturing sources for {self.name}: {str(e)}")
            return []

    def capture_all_sources(self, browse_key: Optional[str] = None) -> List[PlayerSource]:
        """Fetch all pages of browse results by following nextKey pagination."""
        all_sources = []
        current_key = browse_key
        while True:
            url = "/Browse"
            params = {"key": current_key} if current_key else None
            try:
                response = self.request(url, params)
                root = ET.fromstring(response.text)
                browse_search_key = root.get('searchKey')

                for item in root.iter('item'):
                    all_sources.append(self._make_source(item, browse_search_key))

                next_key = root.get('nextKey')
                logger.info(f"Fetched {len(all_sources)} sources so far, nextKey={next_key}")
                if not next_key:
                    break
                current_key = next_key
            except requests.RequestException as e:
                logger.error(f"Error fetching all sources: {str(e)}")
                break
        logger.info(f"Total sources fetched: {len(all_sources)}")
        return all_sources

    def get_nested_sources(self, source: PlayerSource) -> None:
        if source.browse_key:
            nested_sources = self.capture_all_sources(source.browse_key)
            if nested_sources:
                source.children = nested_sources
            else:
                logger.warning(f"No nested sources found for {source.text}")

    def initialize_sources(self) -> None:
        max_retries = 3
        retry_delay = 1  # seconds
        for attempt in range(max_retries):
            self.sources = self.capture_sources()
            if self.sources:
                logger.info(f"Initialized {len(self.sources)} sources for {self.name} after {attempt + 1} attempt(s).")
                return
            logger.warning(f"No sources found for {self.name}. Attempt {attempt + 1}/{max_retries}. Retrying in {retry_delay}s...")
            time.sleep(retry_delay)
        logger.error(f"Failed to initialize sources for {self.name} after {max_retries} attempts.")
        # self.sources will remain empty if all retries fail

    def get_status(self, timeout: Optional[int] = None, etag: Optional[str] = None) -> Tuple[bool, Union[PlayerStatus, str]]:
        url = "/Status"
        params = {}
        if timeout:
            params['timeout'] = timeout
        if etag:
            params['etag'] = etag

        logger.debug(f"Getting status for {self.name}")
        try:
            response = self.request(url, params)
            root = ET.fromstring(response.text)
            
            status = PlayerStatus(
                etag=root.get('etag', ''),
                album=_safe_find(root, 'album'),
                artist=_safe_find(root, 'artist'),
                name=_safe_find(root, 'title1'),
                state=_safe_find(root, 'state'),
                volume=_safe_int(_safe_find(root, 'volume')),
                service=_safe_find(root, 'service'),
                inputId=_safe_find(root, 'inputId'),
                can_move_playback=_safe_find(root, 'canMovePlayback') == 'true',
                can_seek=_safe_int(_safe_find(root, 'canSeek')) == 1,
                cursor=_safe_int(_safe_find(root, 'cursor')),
                db=float(_safe_find(root, 'db', '0')),
                fn=_safe_find(root, 'fn'),
                image=_safe_find(root, 'image'),
                indexing=_safe_int(_safe_find(root, 'indexing')),
                mid=_safe_int(_safe_find(root, 'mid')),
                mode=_safe_int(_safe_find(root, 'mode')),
                mute=_safe_int(_safe_find(root, 'mute')) == 1,
                pid=_safe_int(_safe_find(root, 'pid')),
                prid=_safe_int(_safe_find(root, 'prid')),
                quality=_safe_find(root, 'quality'),
                repeat=_safe_int(_safe_find(root, 'repeat')),
                service_icon=_safe_find(root, 'serviceIcon'),
                service_name=_safe_find(root, 'serviceName'),
                shuffle=_safe_int(_safe_find(root, 'shuffle')) == 1,
                sid=_safe_int(_safe_find(root, 'sid')),
                sleep=_safe_find(root, 'sleep'),
                song=_safe_int(_safe_find(root, 'song')),
                stream_format=_safe_find(root, 'streamFormat'),
                sync_stat=_safe_int(_safe_find(root, 'syncStat')),
                title1=_safe_find(root, 'title1'),
                title2=_safe_find(root, 'title2'),
                title3=_safe_find(root, 'title3'),
                totlen=_safe_int(_safe_find(root, 'totlen')),
                secs=_safe_int(_safe_find(root, 'secs')),
                albumid=_safe_find(root, 'albumid'),
                artistid=_safe_find(root, 'artistid'),
                is_favourite=_safe_find(root, 'isFavourite') == '1',
            )
            logger.info(f"Status for {self.name}: {status}")
            return True, status
        except requests.RequestException as e:
            logger.error(f"Error getting status for {self.name}: {str(e)}")
            return False, str(e)

    def set_volume(self, volume: int) -> Tuple[bool, str]:
        url = "/Volume"
        params = {'level': volume}
        logger.info(f"Setting volume for {self.name} to {volume}")
        try:
            self.request(url, params)
            return True, "Volume set successfully"
        except requests.RequestException as e:
            logger.error(f"Error setting volume for {self.name}: {str(e)}")
            return False, str(e)

    def toggle_play_pause(self) -> Tuple[bool, str]:
        url = "/Pause"
        params = {'toggle': 1}
        logger.info(f"Toggling play/pause for {self.name}")
        try:
            self.request(url, params)
            return True, "Playback toggled successfully"
        except requests.RequestException as e:
            logger.error(f"Error toggling play/pause for {self.name}: {str(e)}")
            return False, str(e)

    def skip(self) -> Tuple[bool, str]:
        url = "/Skip"
        logger.info(f"Skipping track on {self.name}")
        try:
            self.request(url)
            return True, "Skipped to next track successfully"
        except requests.RequestException as e:
            logger.error(f"Error skipping track on {self.name}: {str(e)}")
            return False, str(e)

    def back(self) -> Tuple[bool, str]:
        url = "/Back"
        logger.info(f"Going back a track on {self.name}")
        try:
            self.request(url)
            return True, "Went back to previous track successfully"
        except requests.RequestException as e:
            logger.error(f"Error going back a track on {self.name}: {str(e)}")
            return False, str(e)

    def select_input(self, source: PlayerSource) -> Tuple[bool, str]:
        if source.play_url:
            url = source.play_url
            params = None
        elif source.browse_key:
            url = "/Browse"
            params = {'key': source.browse_key}
        else:
            return False, "Invalid source"
        logger.info(f"Selecting source for {self.name}: {source.text}")

        try:
            self.request(url, params)
            return True, f"{source.text} selected successfully"
        except requests.RequestException as e:
            logger.error(f"Error selecting source for {self.name}: {str(e)}")
            return False, str(e)

    def toggle_favourite(self, source: PlayerSource, add: bool) -> Tuple[bool, str]:
        """Add or remove an album from favourites via context menu."""
        if not source.context_menu_key:
            return False, "No context menu available for this item"

        try:
            # Fetch context menu actions
            response = self.request("/Browse", {"key": source.context_menu_key})
            root = ET.fromstring(response.text)

            for item in root.findall('item'):
                text = item.get('text', '')
                action_url = item.get('actionURL')
                logger.info(f"Context menu item: '{text}' actionURL={action_url} browseKey={item.get('browseKey')}")

                # Match "Favourite" menu item - actionURL tells us what it does
                if 'favourite' in text.lower():
                    if action_url:
                        # actionURL is a direct endpoint path (e.g. /AddFavourite?... or /RemoveFavourite?...)
                        self.request(action_url)
                        source.is_favourite = add
                        return True, f"{'Added to' if add else 'Removed from'} favourites: {source.text}"
                    browse_key = item.get('browseKey')
                    if browse_key:
                        self.request("/Browse", {"key": browse_key})
                        source.is_favourite = add
                        return True, f"{'Added to' if add else 'Removed from'} favourites: {source.text}"
                    return False, f"No action URL for: {text}"

            # Log all available actions for debugging
            available = [item.get('text', '') for item in root.findall('item')]
            logger.info(f"Available context menu actions: {available}")
            return False, f"'Favourite' not found in context menu"
        except requests.RequestException as e:
            logger.error(f"Error toggling favourite: {str(e)}")
            return False, str(e)

    def add_album_favourite(self, status: PlayerStatus) -> Tuple[bool, str]:
        """Add the currently playing album to favourites using the API."""
        if not status.albumid or not status.service:
            return False, "No album info available"
        try:
            self.request("/AddFavourite", {
                "albumid": status.albumid,
                "service": status.service,
            })
            return True, f"Added to favourites: {status.album}"
        except requests.RequestException as e:
            logger.error(f"Error adding favourite: {e}")
            return False, str(e)

    def remove_album_favourite(self, status: PlayerStatus) -> Tuple[bool, str]:
        """Remove the currently playing album from favourites via context menu."""
        if not status.albumid or not status.service or not status.artistid:
            return False, "No album info available"
        from urllib.parse import quote
        cm_key = f"{status.service}:CM/{status.service}-Album?albumid={status.albumid}&artist={quote(status.artist)}&artistid={status.artistid}"
        try:
            response = self.request("/Browse", {"key": cm_key})
            root = ET.fromstring(response.text)
            for item in root.findall('item'):
                text = item.get('text', '')
                action_url = item.get('actionURL')
                if 'favourite' in text.lower() and action_url:
                    self.request(action_url)
                    return True, f"Removed from favourites: {status.album}"
            return False, "Favourite action not found in context menu"
        except requests.RequestException as e:
            logger.error(f"Error removing favourite: {e}")
            return False, str(e)

    def get_queue_actions(self, source: PlayerSource) -> Dict[str, str]:
        """Fetch queue actions (play now, add next, add last) from context menu."""
        if not source.context_menu_key:
            return {}

        try:
            response = self.request("/Browse", {"key": source.context_menu_key})
            root = ET.fromstring(response.text)

            action_map = {
                "play now": "play_now",
                "add next": "add_next",
                "add last": "add_last",
            }
            actions = {}
            for item in root.findall('item'):
                text = item.get('text', '').lower()
                action_url = item.get('actionURL')
                if action_url:
                    for menu_text, key in action_map.items():
                        if menu_text in text:
                            actions[key] = action_url
            logger.info(f"Queue actions for '{source.text}': {list(actions.keys())}")
            return actions
        except requests.RequestException as e:
            logger.error(f"Error fetching queue actions: {e}")
            return {}

    def save_playlist(self, name: str) -> Tuple[bool, str]:
        """Save the current play queue as a named playlist."""
        try:
            response = self.request("/Save", {"name": name})
            root = ET.fromstring(response.text)
            entries = root.findtext("entries", "0")
            return True, f"Saved '{name}' ({entries} tracks)"
        except requests.RequestException as e:
            logger.error(f"Error saving playlist: {e}")
            return False, str(e)

    def get_playlists(self) -> List[PlayerSource]:
        """List all saved playlists."""
        return self.capture_sources("playlists")

    def delete_playlist(self, name: str) -> Tuple[bool, str]:
        """Delete a saved playlist."""
        try:
            self.request("/Delete", {"name": name})
            return True, f"Deleted '{name}'"
        except requests.RequestException as e:
            logger.error(f"Error deleting playlist: {e}")
            return False, str(e)

    def get_playlist(self) -> list:
        url = "/Playlist"
        try:
            response = self.request(url)
            root = ET.fromstring(response.text)
            entries = []
            for song in root.findall('song'):
                title = song.findtext('title', '') or song.get('title', '')
                artist = song.findtext('art', '') or song.get('art', '')
                album = song.findtext('alb', '') or song.get('alb', '')
                entries.append({
                    'title': title.strip(),
                    'artist': artist.strip(),
                    'album': album.strip(),
                })
            return entries
        except requests.RequestException as e:
            logger.error(f"Error getting playlist: {e}")
            return []

    def browse_path(self, *names: str) -> Tuple[bool, List['PlayerSource']]:
        """Navigate the browse hierarchy by matching item names at each level.
        Returns (True, items) if full path reached, or (False, items_at_failed_level)
        so the user can continue navigating manually."""
        current_key = None
        for depth, name in enumerate(names):
            items = self.capture_sources(current_key)
            if not items:
                return False, []
            match = None
            name_lower = name.lower()
            for item in items:
                if name_lower in item.text.lower():
                    match = item
                    break
            if not match or not match.browse_key:
                logger.info(f"browse_path: '{name}' not found at depth {depth}. Available: {[i.text for i in items]}")
                return False, items
            current_key = match.browse_key
        results = self.capture_all_sources(current_key)
        return bool(results), results

    def search(self, search_key: str, search_string: str) -> List[PlayerSource]:
        url = "/Browse"
        params = {'key': search_key, 'q': search_string}
        logger.info(f"Searching for '{search_string}' with key '{search_key}' on {self.name}")
        try:
            response = self.request(url, params)
            root = ET.fromstring(response.text)
            sources = []
            for item in root.iter('item'):
                text = item.get('text', '').strip()
                browse_key = item.get('browseKey')
                if text == "Library" and browse_key:
                    library_response = self.request(url, {'key': browse_key})
                    library_root = ET.fromstring(library_response.text)
                    for library_item in library_root.iter('item'):
                        sources.append(self._make_source(library_item, library_root.get('searchKey')))
                else:
                    sources.append(self._make_source(item, root.get('searchKey')))
            logger.info(f"Found {len(sources)} results for search '{search_string}' on {self.name}")
            return sources
        except requests.RequestException as e:
            logger.error(f"Error searching on {self.name}: {str(e)}")
            return []

class MyListener(ServiceListener):
    def __init__(self):
        self.players = []

    def add_service(self, zeroconf: Zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        ipv4 = [addr for addr in info.parsed_addresses() if addr.count('.') == 3][0]
        player = BlusoundPlayer(host_name=ipv4, name=info.server)
        self.players.append(player)
        logger.info(f"Discovered new player: {player.name} at {player.host_name}")

    def remove_service(self, zeroconf, type, name):
        self.players = [p for p in self.players if p.name != name]
        logger.info(f"Removed player: {name}")

    def update_service(self, zeroconf, type, name):
        logger.info(f"Updated service: {name}")

def discover(players):
    logger.info("Starting discovery process")
    zeroconf = Zeroconf()
    listener = MyListener()
    ServiceBrowser(zeroconf, "_musc._tcp.local.", listener)
    try:
        while True:
            time.sleep(1)
            players[:] = listener.players
    finally:
        zeroconf.close()
        logger.info("Discovery process ended")

def threaded_discover():
    logger.info("Starting threaded discovery")
    players = []
    discovery_thread = threading.Thread(target=discover, args=(players,), daemon=True)
    discovery_thread.start()
    return players
