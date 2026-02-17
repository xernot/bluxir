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
import logging
import time
import os
import threading
from collections import OrderedDict
from logging.handlers import RotatingFileHandler

os.makedirs('logs', exist_ok=True)
log_handler = RotatingFileHandler('logs/musicbrainz.log', maxBytes=1024*1024, backupCount=1)
log_handler.setFormatter(logging.Formatter('%(asctime)s - %(levelname)s - %(message)s',
                                           datefmt='%Y-%m-%d %H:%M:%S'))
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
logger.addHandler(log_handler)

MUSICBRAINZ_API = "https://musicbrainz.org/ws/2"
USER_AGENT = "bluxir/1.2 (https://github.com/xernot/bluxir)"

_MAX_CACHE_SIZE = 256
_cache = OrderedDict()
_cache_lock = threading.Lock()
_last_request_time = 0
_rate_lock = threading.Lock()


def _cache_get(key):
    with _cache_lock:
        return _cache.get(key)


def _cache_set(key, value):
    with _cache_lock:
        _cache[key] = value
        if len(_cache) > _MAX_CACHE_SIZE:
            _cache.popitem(last=False)


def _cache_has(key):
    with _cache_lock:
        return key in _cache


def _rate_limit():
    global _last_request_time
    with _rate_lock:
        now = time.time()
        elapsed = now - _last_request_time
        if elapsed < 1.0:
            time.sleep(1.0 - elapsed)
        _last_request_time = time.time()


def _match_score(release, artist, album):
    """Score how well a release matches the artist and album we're looking for."""
    score = 0
    title = (release.get("title") or "").lower()
    album_lower = album.lower()

    # Exact title match
    if title == album_lower:
        score += 100
    # Album contained in title or vice versa
    elif album_lower in title:
        score += 60
    elif title in album_lower:
        score += 50
    else:
        # Check word overlap
        album_words = set(album_lower.split())
        title_words = set(title.split())
        overlap = album_words & title_words
        if overlap:
            score += len(overlap) * 10

    # Check artist match
    artist_lower = artist.lower()
    artist_credits = release.get("artist-credit", [])
    for credit in artist_credits:
        credit_name = (credit.get("name") or credit.get("artist", {}).get("name", "")).lower()
        if credit_name == artist_lower:
            score += 50
        elif artist_lower in credit_name or credit_name in artist_lower:
            score += 30

    # Prefer releases with a score from MusicBrainz
    score += release.get("score", 0) // 10

    return score


def _find_best_release(releases, artist, album):
    """Walk through all releases and find the best match."""
    best = None
    best_score = -1

    for release in releases:
        score = _match_score(release, artist, album)
        title = release.get("title", "")
        artist_names = ", ".join(
            c.get("name", "") for c in release.get("artist-credit", [])
        )
        logger.info(f"  Release: '{title}' by '{artist_names}' - score={score}")

        if score > best_score:
            best_score = score
            best = release

    if best:
        logger.info(f"Best match: '{best.get('title')}' (score={best_score})")
    return best


def get_track_wiki(title):
    """Fetch Wikipedia summary by trying the track title directly."""
    if not title:
        return None

    title = title.strip()
    cache_key = f"wiki|{title}"
    cached = _cache_get(cache_key)
    if cached is not None or _cache_has(cache_key):
        logger.info(f"Wiki cache hit for: {title}")
        return cached

    from urllib.parse import quote
    encoded = quote(title, safe="")

    for lang in ["en", "de"]:
        try:
            url = f"https://{lang}.wikipedia.org/api/rest_v1/page/summary/{encoded}"
            logger.info(f"Fetching Wikipedia: {url}")
            _rate_limit()
            response = requests.get(
                url,
                headers={"User-Agent": USER_AGENT},
                timeout=5
            )
            if response.status_code == 404:
                logger.info(f"Not found on {lang}.wikipedia.org")
                continue
            response.raise_for_status()
            data = response.json()
            extract = data.get("extract", "")
            if extract:
                logger.info(f"Found on {lang}.wikipedia.org: {len(extract)} chars")
                _cache_set(cache_key, extract)
                return extract
        except Exception as e:
            logger.error(f"Wikipedia error ({lang}): {e}")

    _cache_set(cache_key, None)
    return None


def get_track_info_ai(title, artist, api_key, system_prompt=None):
    """Fetch track information from OpenAI."""
    if not title or not api_key:
        return None

    cache_key = f"ai|{title}|{artist}"
    cached = _cache_get(cache_key)
    if cached is not None or _cache_has(cache_key):
        logger.info(f"AI cache hit for: {title} - {artist}")
        return cached

    try:
        user_prompt = f"Tell me about the song \"{title}\" by {artist}."
        messages = []
        if system_prompt:
            messages.append({"role": "system", "content": system_prompt})
        messages.append({"role": "user", "content": user_prompt})
        logger.info(f"OpenAI request: system='{system_prompt}', user='{user_prompt}'")
        response = requests.post(
            "https://api.openai.com/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": "gpt-4o-mini",
                "messages": messages,
                "max_tokens": 200,
                "temperature": 0.7,
            },
            timeout=10,
        )
        response.raise_for_status()
        data = response.json()
        text = data["choices"][0]["message"]["content"].strip()
        logger.info(f"OpenAI response: {len(text)} chars")
        _cache_set(cache_key, text)
        return text
    except Exception as e:
        logger.error(f"OpenAI error: {e}")
        _cache_set(cache_key, None)
        return None


def get_combined_info(title, artist, album, api_key, system_prompt=None, model="gpt-4o-mini"):
    """Fetch album metadata and track info in a single OpenAI call.

    Returns a dict with keys: year, label, genre, track_info.
    Falls back to MusicBrainz for album metadata if no API key is set.
    """
    cache_key = f"combined|{title}|{artist}|{album}"
    cached = _cache_get(cache_key)
    if cached is not None or _cache_has(cache_key):
        logger.info(f"Combined cache hit for: {title} - {artist} - {album}")
        return cached

    if not api_key:
        # No OpenAI key — fall back to MusicBrainz for album info only
        logger.info("No API key, falling back to MusicBrainz")
        mb = get_album_info(artist, album)
        result = {
            "year": mb.get("year", "-") if mb else "-",
            "label": mb.get("label", "-") if mb else "-",
            "genre": mb.get("genre", "-") if mb else "-",
            "track_info": None,
        }
        _cache_set(cache_key, result)
        return result

    try:
        import json as _json

        track_info_instruction = "a short paragraph (2-4 sentences) about this specific song"
        if system_prompt:
            track_info_instruction += f". {system_prompt}"

        user_prompt = (
            f'Song: "{title}" by {artist}, from the album "{album}".\n\n'
            f"Return a JSON object with these exact keys:\n"
            f'- "year": the original release year of the album (4-digit string, or "-" if unknown)\n'
            f'- "label": the record label that released the album (or "-" if unknown)\n'
            f'- "genre": the genre(s) of this album, comma-separated (or "-" if unknown)\n'
            f'- "track_info": {track_info_instruction}\n\n'
            f'IMPORTANT: Only include facts you are certain about. If you don\'t know the artist or song, '
            f'set "track_info" to "-" rather than guessing. Never invent musician names, studios, or details.\n\n'
            f"Respond ONLY with the JSON object, no markdown, no explanation."
        )
        messages = [{"role": "user", "content": user_prompt}]
        logger.info(f"Combined AI request — full prompt:\n{user_prompt}")

        logger.info(f"Using model: {model}")
        response = requests.post(
            "https://api.openai.com/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": model,
                "messages": messages,
                "max_tokens": 300,
                "temperature": 0.3,
            },
            timeout=15,
        )
        response.raise_for_status()
        data = response.json()
        raw = data["choices"][0]["message"]["content"].strip()
        logger.info(f"Combined AI raw response: {raw[:500]}")

        # Strip markdown code fences if present
        if raw.startswith("```"):
            raw = raw.split("\n", 1)[1] if "\n" in raw else raw[3:]
            if raw.endswith("```"):
                raw = raw[:-3]
            raw = raw.strip()

        result = _json.loads(raw)
        # Ensure all expected keys exist
        for key in ("year", "label", "genre", "track_info"):
            if key not in result:
                result[key] = "-" if key != "track_info" else None

        logger.info(f"Combined AI parsed: year={result.get('year')}, label={result.get('label')}, genre={result.get('genre')}")
        _cache_set(cache_key, result)
        return result
    except Exception as e:
        logger.error(f"Combined AI error: {e}")
        # Fall back to MusicBrainz on OpenAI failure
        logger.info("Falling back to MusicBrainz after AI failure")
        mb = get_album_info(artist, album)
        result = {
            "year": mb.get("year", "-") if mb else "-",
            "label": mb.get("label", "-") if mb else "-",
            "genre": mb.get("genre", "-") if mb else "-",
            "track_info": None,
        }
        _cache_set(cache_key, result)
        return result


def get_station_info(station_name, api_key, model="gpt-4o-mini"):
    """Fetch radio station info from OpenAI."""
    if not station_name or not api_key:
        return None

    cache_key = f"station|{station_name}"
    cached = _cache_get(cache_key)
    if cached is not None or _cache_has(cache_key):
        logger.info(f"Station cache hit for: {station_name}")
        return cached

    try:
        import json as _json

        user_prompt = (
            f'Radio station: "{station_name}".\n\n'
            f"Tell me about this radio station in 3-5 sentences. "
            f"Include: what kind of station it is, what country/region it serves, "
            f"what type of music or content it broadcasts, and any notable facts.\n\n"
            f'IMPORTANT: Only include facts you are certain about. If you don\'t know '
            f'this station, respond with just "-".'
        )
        logger.info(f"Station AI request — full prompt:\n{user_prompt}")
        logger.info(f"Using model: {model}")

        response = requests.post(
            "https://api.openai.com/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": model,
                "messages": [{"role": "user", "content": user_prompt}],
                "max_tokens": 300,
                "temperature": 0.3,
            },
            timeout=15,
        )
        response.raise_for_status()
        data = response.json()
        text = data["choices"][0]["message"]["content"].strip()
        logger.info(f"Station AI response: {text[:500]}")

        if text == "-":
            text = None
        _cache_set(cache_key, text)
        return text
    except Exception as e:
        logger.error(f"Station AI error: {e}")
        _cache_set(cache_key, None)
        return None


def get_lyrics(title, artist):
    """Fetch lyrics from LRCLIB (free, no API key needed)."""
    if not title or not artist:
        return None

    cache_key = f"lyrics|{title}|{artist}"
    cached = _cache_get(cache_key)
    if cached is not None or _cache_has(cache_key):
        logger.info(f"Lyrics cache hit for: {title} - {artist}")
        return cached

    try:
        from urllib.parse import quote
        url = f"https://lrclib.net/api/get?artist_name={quote(artist)}&track_name={quote(title)}"
        logger.info(f"Fetching lyrics: {url}")
        response = requests.get(url, headers={"User-Agent": USER_AGENT}, timeout=10)
        if response.status_code == 404:
            logger.info(f"No lyrics found for: {title} - {artist}")
            _cache_set(cache_key, None)
            return None
        response.raise_for_status()
        data = response.json()
        plain = data.get("plainLyrics")
        if plain:
            logger.info(f"Lyrics found: {len(plain)} chars")
            _cache_set(cache_key, plain)
            return plain
        logger.info(f"No plain lyrics in response for: {title} - {artist}")
        _cache_set(cache_key, None)
        return None
    except Exception as e:
        logger.error(f"LRCLIB error: {e}")
        # Don't cache timeouts/network errors — allow retry on next track change
        return None


def get_album_info(artist, album):
    logger.info(f"get_album_info called: artist='{artist}', album='{album}'")

    if not artist or not album:
        logger.info("Skipping: artist or album is empty")
        return None

    cache_key = f"{artist}|{album}"
    cached = _cache_get(cache_key)
    if cached is not None or _cache_has(cache_key):
        logger.info(f"Cache hit for: {cache_key}")
        return cached

    headers = {"User-Agent": USER_AGENT, "Accept": "application/json"}

    try:
        # Search with artist name, fetch multiple results to find best match
        queries = [
            f'artist:"{artist}" AND release:{album}',
            f'artist:{artist} release:{album}',
        ]

        best_release = None
        for query in queries:
            _rate_limit()
            params = {
                "query": query,
                "fmt": "json",
                "limit": 25,
            }
            logger.info(f"Searching MusicBrainz: {query}")
            response = requests.get(
                f"{MUSICBRAINZ_API}/release/",
                params=params, headers=headers, timeout=5
            )
            logger.info(f"Response status: {response.status_code}")
            response.raise_for_status()
            data = response.json()

            releases = data.get("releases", [])
            logger.info(f"Found {len(releases)} releases, scoring matches...")

            if releases:
                best_release = _find_best_release(releases, artist, album)
                if best_release:
                    break

        if not best_release:
            logger.info("No matching release found")
            _cache_set(cache_key, None)
            return None

        release = best_release
        logger.info(f"Using release: title='{release.get('title')}', id='{release.get('id')}', date='{release.get('date')}'")

        info = {
            "year": release.get("date", "")[:4] if release.get("date") else "-",
            "label": "-",
            "country": release.get("country", "-") or "-",
            "type": "-",
            "genre": "-",
        }

        label_info = release.get("label-info", [])
        if label_info and label_info[0].get("label"):
            info["label"] = label_info[0]["label"].get("name", "-")

        rg = release.get("release-group", {})
        if rg:
            info["type"] = rg.get("primary-type", "-") or "-"

        # Fetch tags from release-group
        rg_id = rg.get("id") if rg else None
        if rg_id:
            logger.info(f"Fetching tags for release-group: {rg_id}")
            _rate_limit()
            try:
                rg_response = requests.get(
                    f"{MUSICBRAINZ_API}/release-group/{rg_id}",
                    params={"inc": "tags", "fmt": "json"},
                    headers=headers, timeout=5
                )
                rg_response.raise_for_status()
                rg_data = rg_response.json()

                tags = rg_data.get("tags", [])
                logger.info(f"Found {len(tags)} tags")
                if tags:
                    tags.sort(key=lambda t: t.get("count", 0), reverse=True)
                    info["genre"] = ", ".join(t["name"] for t in tags[:3])

            except Exception as e:
                logger.error(f"Error fetching release-group details: {e}")

        logger.info(f"Final info: {info}")
        _cache_set(cache_key, info)
        return info
    except Exception as e:
        logger.error(f"MusicBrainz error: {e}")
        _cache_set(cache_key, None)
        return None
