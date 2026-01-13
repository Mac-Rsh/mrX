#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX module management

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from functools import lru_cache
from typing import Dict, Type
from datetime import datetime
from pathlib import Path
import importlib
import argparse


class MrLogger:
    """Logger for mrX modules."""

    # Level constants
    _LEVELS = ("ERR", "WRN", "INF", "DBG", "TRC")
    LEVEL_ERR = 0
    LEVEL_WRN = 1
    LEVEL_INF = 2
    LEVEL_DBG = 3
    LEVEL_TRC = 4

    def __init__(self, name: str, level: int = None):
        """Initialize logger.

        Args:
            name: Module name for log output.
            level: Log level. Defaults to LEVEL_TRC.
        """
        self.level = level if level is not None else self.LEVEL_TRC
        self.name = name

    def _output(self, level: int, message: str):
        """Output log with unified format.

        Args:
            level: Log level.
            message: Log message content.
        """
        if level > self.level:
            return
        ts = datetime.now().strftime("%Y-%m-%d-%H:%M:%S")
        print(f"[{ts}] [{self._LEVELS[level]}/{self.name}] {message}")

    def err(self, message: str):
        """Log error level message."""
        self._output(self.LEVEL_ERR, message)

    def wrn(self, message: str):
        """Log warning level message."""
        self._output(self.LEVEL_WRN, message)

    def inf(self, message: str):
        """Log info level message."""
        self._output(self.LEVEL_INF, message)

    def dbg(self, message: str):
        """Log debug level message."""
        self._output(self.LEVEL_DBG, message)

    def trc(self, message: str):
        """Log trace level message."""
        self._output(self.LEVEL_TRC, message)


class MrModule:
    """Base class for all mrX modules."""

    name: str = "Unknown"
    description: str = "Unknown script description"
    note: str = None
    version: str = "1.0.0"
    author: str = "Unknown"
    log_level: int = MrLogger.LEVEL_ERR

    def __init__(self):
        """Initialize module instance."""
        self.logger = MrLogger(self.name, self.log_level)

    def add_arguments(self, parser: argparse.ArgumentParser):
        """Add module-specific arguments.

        Args:
            parser: Argument parser to add arguments to.
        """
        pass

    def run(self, args: argparse.Namespace):
        """Execution entry, override in subclass.

        Args:
            args: Parsed command line arguments.
        """
        pass

    @classmethod
    @lru_cache(maxsize=1)
    def _update_modules(cls):
        """Update modules cache.

        Args:
            cls: The class object.

        Returns:
            None.
        """
        logger = MrLogger("modules", MrLogger.LEVEL_DBG)
        module_dir = Path(__file__).parent

        # Scan all modules
        for dir_path in module_dir.iterdir():
            # Skip non-directory
            if not dir_path.is_dir():
                continue

            # Import module
            try:
                importlib.import_module(f"modules.{dir_path.name}")
            except (ModuleNotFoundError, ImportError) as e:
                logger.dbg(f"Import failed: {dir_path.name} ({e})")

    @classmethod
    @lru_cache(maxsize=1)
    def get_modules(cls) -> Dict[str, Type["MrModule"]]:
        """Scan all script classes inherited from MrModule.

        Args:
            cls: The class object.

        Returns:
            Dictionary mapping script names to classes.
        """
        MrModule._update_modules()

        # Get all the modules from the imported ones
        modules: Dict[str, Type["MrModule"]] = {}
        for module_cls in cls.__subclasses__():
            modules[module_cls.name] = module_cls
        return modules
