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

_cache = {}
_last_request_time = 0


def _rate_limit():
    global _last_request_time
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
    if cache_key in _cache:
        logger.info(f"Wiki cache hit for: {title}")
        return _cache[cache_key]

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
                _cache[cache_key] = extract
                return extract
        except Exception as e:
            logger.error(f"Wikipedia error ({lang}): {e}")

    _cache[cache_key] = None
    return None


def get_track_info_ai(title, artist, api_key, system_prompt=None):
    """Fetch track information from OpenAI."""
    if not title or not api_key:
        return None

    cache_key = f"ai|{title}|{artist}"
    if cache_key in _cache:
        logger.info(f"AI cache hit for: {title} - {artist}")
        return _cache[cache_key]

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
        _cache[cache_key] = text
        return text
    except Exception as e:
        logger.error(f"OpenAI error: {e}")
        _cache[cache_key] = None
        return None


def get_album_info(artist, album):
    logger.info(f"get_album_info called: artist='{artist}', album='{album}'")

    if not artist or not album:
        logger.info("Skipping: artist or album is empty")
        return None

    cache_key = f"{artist}|{album}"
    if cache_key in _cache:
        logger.info(f"Cache hit for: {cache_key}")
        return _cache[cache_key]

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
            _cache[cache_key] = None
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
        _cache[cache_key] = info
        return info
    except Exception as e:
        logger.error(f"MusicBrainz error: {e}")
        _cache[cache_key] = None
        return None
