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
