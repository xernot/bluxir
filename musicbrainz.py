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
from constants import (
    LOG_MAX_BYTES, LOG_BACKUP_COUNT, LOG_FORMAT_SHORT, LOG_DATE_FORMAT,
    MUSICBRAINZ_BASE_URL, MUSICBRAINZ_USER_AGENT, MUSICBRAINZ_RATE_LIMIT,
    WIKIPEDIA_API_TEMPLATE, WIKIPEDIA_LANGUAGES,
    OPENAI_API_URL, DEFAULT_OPENAI_MODEL,
    TRACK_INFO_MAX_TOKENS, TRACK_INFO_TEMPERATURE, TRACK_INFO_PROMPT,
    COMBINED_INFO_MAX_TOKENS, COMBINED_INFO_TEMPERATURE, COMBINED_INFO_PROMPT,
    STATION_INFO_MAX_TOKENS, STATION_INFO_TEMPERATURE, STATION_INFO_PROMPT,
    LRCLIB_API_URL,
    HTTP_TIMEOUT_MUSICBRAINZ, HTTP_TIMEOUT_WIKIPEDIA, HTTP_TIMEOUT_OPENAI,
    HTTP_TIMEOUT_OPENAI_COMBINED, HTTP_TIMEOUT_OPENAI_STATION, HTTP_TIMEOUT_LYRICS,
    EXACT_TITLE_SCORE, PARTIAL_TITLE_SCORE, REVERSE_PARTIAL_SCORE,
    WORD_OVERLAP_MULTIPLIER, EXACT_ARTIST_SCORE, PARTIAL_ARTIST_SCORE,
    MB_SCORE_DIVISOR, MB_SEARCH_LIMIT, MB_CACHE_SIZE, TOP_GENRE_COUNT,
)

os.makedirs('logs', exist_ok=True)
log_handler = RotatingFileHandler('logs/musicbrainz.log', maxBytes=LOG_MAX_BYTES, backupCount=LOG_BACKUP_COUNT)
log_handler.setFormatter(logging.Formatter(LOG_FORMAT_SHORT, datefmt=LOG_DATE_FORMAT))
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
logger.addHandler(log_handler)

_MAX_CACHE_SIZE = MB_CACHE_SIZE
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
        if elapsed < MUSICBRAINZ_RATE_LIMIT:
            time.sleep(MUSICBRAINZ_RATE_LIMIT - elapsed)
        _last_request_time = time.time()


def _match_score(release, artist, album):
    """Score how well a release matches the artist and album we're looking for."""
    score = 0
    title = (release.get("title") or "").lower()
    album_lower = album.lower()

    # Exact title match
    if title == album_lower:
        score += EXACT_TITLE_SCORE
    # Album contained in title or vice versa
    elif album_lower in title:
        score += PARTIAL_TITLE_SCORE
    elif title in album_lower:
        score += REVERSE_PARTIAL_SCORE
    else:
        # Check word overlap
        album_words = set(album_lower.split())
        title_words = set(title.split())
        overlap = album_words & title_words
        if overlap:
            score += len(overlap) * WORD_OVERLAP_MULTIPLIER

    # Check artist match
    artist_lower = artist.lower()
    artist_credits = release.get("artist-credit", [])
    for credit in artist_credits:
        credit_name = (credit.get("name") or credit.get("artist", {}).get("name", "")).lower()
        if credit_name == artist_lower:
            score += EXACT_ARTIST_SCORE
        elif artist_lower in credit_name or credit_name in artist_lower:
            score += PARTIAL_ARTIST_SCORE

    # Prefer releases with a score from MusicBrainz
    score += release.get("score", 0) // MB_SCORE_DIVISOR

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

    for lang in WIKIPEDIA_LANGUAGES:
        try:
            url = WIKIPEDIA_API_TEMPLATE.format(lang=lang, title=encoded)
            logger.info(f"Fetching Wikipedia: {url}")
            _rate_limit()
            response = requests.get(
                url,
                headers={"User-Agent": MUSICBRAINZ_USER_AGENT},
                timeout=HTTP_TIMEOUT_WIKIPEDIA
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
        user_prompt = TRACK_INFO_PROMPT.format(title=title, artist=artist)
        messages = []
        if system_prompt:
            messages.append({"role": "system", "content": system_prompt})
        messages.append({"role": "user", "content": user_prompt})
        logger.info(f"OpenAI request: system='{system_prompt}', user='{user_prompt}'")
        response = requests.post(
            OPENAI_API_URL,
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": DEFAULT_OPENAI_MODEL,
                "messages": messages,
                "max_tokens": TRACK_INFO_MAX_TOKENS,
                "temperature": TRACK_INFO_TEMPERATURE,
            },
            timeout=HTTP_TIMEOUT_OPENAI,
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


def _mb_fallback_result(artist, album):
    """Build a result dict from MusicBrainz data when OpenAI is unavailable."""
    mb = get_album_info(artist, album)
    return {
        "year": mb.get("year", "-") if mb else "-",
        "label": mb.get("label", "-") if mb else "-",
        "genre": mb.get("genre", "-") if mb else "-",
        "track_info": None,
    }


def _build_combined_prompt(title, artist, album, system_prompt):
    """Construct the prompt for the combined album+track OpenAI request."""
    track_info_instruction = "a short paragraph (2-4 sentences) about this specific song"
    if system_prompt:
        track_info_instruction += f". {system_prompt}"
    return COMBINED_INFO_PROMPT.format(
        title=title, artist=artist, album=album,
        track_info_instruction=track_info_instruction,
    )


def _parse_combined_response(raw):
    """Parse the JSON response from the combined OpenAI request."""
    import json as _json
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
    return result


def get_combined_info(title, artist, album, api_key, system_prompt=None, model=DEFAULT_OPENAI_MODEL):
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
        logger.info("No API key, falling back to MusicBrainz")
        result = _mb_fallback_result(artist, album)
        _cache_set(cache_key, result)
        return result

    try:
        user_prompt = _build_combined_prompt(title, artist, album, system_prompt)
        messages = [{"role": "user", "content": user_prompt}]
        logger.info(f"Combined AI request — full prompt:\n{user_prompt}")

        logger.info(f"Using model: {model}")
        response = requests.post(
            OPENAI_API_URL,
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": model,
                "messages": messages,
                "max_tokens": COMBINED_INFO_MAX_TOKENS,
                "temperature": COMBINED_INFO_TEMPERATURE,
            },
            timeout=HTTP_TIMEOUT_OPENAI_COMBINED,
        )
        response.raise_for_status()
        data = response.json()
        raw = data["choices"][0]["message"]["content"].strip()
        logger.info(f"Combined AI raw response: {raw[:500]}")

        result = _parse_combined_response(raw)
        logger.info(f"Combined AI parsed: year={result.get('year')}, label={result.get('label')}, genre={result.get('genre')}")
        _cache_set(cache_key, result)
        return result
    except Exception as e:
        logger.error(f"Combined AI error: {e}")
        logger.info("Falling back to MusicBrainz after AI failure")
        result = _mb_fallback_result(artist, album)
        _cache_set(cache_key, result)
        return result


def get_station_info(station_name, api_key, model=DEFAULT_OPENAI_MODEL):
    """Fetch radio station info from OpenAI."""
    if not station_name or not api_key:
        return None

    cache_key = f"station|{station_name}"
    cached = _cache_get(cache_key)
    if cached is not None or _cache_has(cache_key):
        logger.info(f"Station cache hit for: {station_name}")
        return cached

    try:
        user_prompt = STATION_INFO_PROMPT.format(station_name=station_name)
        logger.info(f"Station AI request — full prompt:\n{user_prompt}")
        logger.info(f"Using model: {model}")

        response = requests.post(
            OPENAI_API_URL,
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": model,
                "messages": [{"role": "user", "content": user_prompt}],
                "max_tokens": STATION_INFO_MAX_TOKENS,
                "temperature": STATION_INFO_TEMPERATURE,
            },
            timeout=HTTP_TIMEOUT_OPENAI_STATION,
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
        url = f"{LRCLIB_API_URL}?artist_name={quote(artist)}&track_name={quote(title)}"
        logger.info(f"Fetching lyrics: {url}")
        response = requests.get(url, headers={"User-Agent": MUSICBRAINZ_USER_AGENT}, timeout=HTTP_TIMEOUT_LYRICS)
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


def _query_musicbrainz(artist, album, headers):
    """Search MusicBrainz for the best matching release."""
    queries = [
        f'artist:"{artist}" AND release:{album}',
        f'artist:{artist} release:{album}',
    ]
    for query in queries:
        _rate_limit()
        params = {
            "query": query,
            "fmt": "json",
            "limit": MB_SEARCH_LIMIT,
        }
        logger.info(f"Searching MusicBrainz: {query}")
        response = requests.get(
            f"{MUSICBRAINZ_BASE_URL}/release/",
            params=params, headers=headers, timeout=HTTP_TIMEOUT_MUSICBRAINZ
        )
        logger.info(f"Response status: {response.status_code}")
        response.raise_for_status()
        data = response.json()

        releases = data.get("releases", [])
        logger.info(f"Found {len(releases)} releases, scoring matches...")

        if releases:
            best = _find_best_release(releases, artist, album)
            if best:
                return best
    return None


def _extract_album_metadata(release, headers):
    """Parse album metadata from a MusicBrainz release, including genre tags."""
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

    rg_id = rg.get("id") if rg else None
    if rg_id:
        logger.info(f"Fetching tags for release-group: {rg_id}")
        _rate_limit()
        try:
            rg_response = requests.get(
                f"{MUSICBRAINZ_BASE_URL}/release-group/{rg_id}",
                params={"inc": "tags", "fmt": "json"},
                headers=headers, timeout=HTTP_TIMEOUT_MUSICBRAINZ
            )
            rg_response.raise_for_status()
            rg_data = rg_response.json()

            tags = rg_data.get("tags", [])
            logger.info(f"Found {len(tags)} tags")
            if tags:
                tags.sort(key=lambda t: t.get("count", 0), reverse=True)
                info["genre"] = ", ".join(t["name"] for t in tags[:TOP_GENRE_COUNT])
        except Exception as e:
            logger.error(f"Error fetching release-group details: {e}")

    return info


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

    headers = {"User-Agent": MUSICBRAINZ_USER_AGENT, "Accept": "application/json"}

    try:
        best_release = _query_musicbrainz(artist, album, headers)
        if not best_release:
            logger.info("No matching release found")
            _cache_set(cache_key, None)
            return None

        logger.info(f"Using release: title='{best_release.get('title')}', id='{best_release.get('id')}', date='{best_release.get('date')}'")
        info = _extract_album_metadata(best_release, headers)
        logger.info(f"Final info: {info}")
        _cache_set(cache_key, info)
        return info
    except Exception as e:
        logger.error(f"MusicBrainz error: {e}")
        _cache_set(cache_key, None)
        return None
