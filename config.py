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

import json
import os
from pathlib import Path

# Project config (player settings, system prompt) - in project root
PROJECT_CONFIG = Path(__file__).parent / 'config.json'

# Private config (API keys) - hidden in home directory
PRIVATE_CONFIG = Path.home() / '.bluxir.json'

def _load_file(path):
    if path.exists():
        with open(path, 'r') as f:
            return json.load(f)
    return {}

def _save_file(path, config):
    with open(path, 'w') as f:
        json.dump(config, f, indent=2)

# Keys that belong in the private config
_PRIVATE_KEYS = {'openai_api_key'}

def get_preference(key, default=None):
    if key in _PRIVATE_KEYS:
        return _load_file(PRIVATE_CONFIG).get(key, default)
    return _load_file(PROJECT_CONFIG).get(key, default)

def set_preference(key, value):
    if key in _PRIVATE_KEYS:
        config = _load_file(PRIVATE_CONFIG)
        config[key] = value
        _save_file(PRIVATE_CONFIG, config)
    else:
        config = _load_file(PROJECT_CONFIG)
        config[key] = value
        _save_file(PROJECT_CONFIG, config)
