#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Persistent interactive command history.

History is stored in the operating-system user state directory, never inside
the Git repository.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from pathlib import Path

from prompt_toolkit.history import FileHistory, History, InMemoryHistory
from platformdirs import user_state_dir

APP_NAME = "mrX"
APP_AUTHOR = "MacRsh"
HISTORY_FILENAME = "cli.history"


def history_path() -> Path:
    """Return the persistent CLI history file path."""
    directory = Path(user_state_dir(APP_NAME, APP_AUTHOR))
    return directory / HISTORY_FILENAME


def create_file_history() -> History:
    """Create a prompt_toolkit history object backed by the state file."""
    path = history_path()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        return FileHistory(str(path))
    except OSError:
        return InMemoryHistory()


def load_history_lines() -> list[str]:
    """Load stored history strings, oldest first."""
    path = history_path()
    try:
        if not path.is_file():
            return []
        history = FileHistory(str(path))
        return list(reversed(list(history.load_history_strings())))
    except OSError:
        return []
