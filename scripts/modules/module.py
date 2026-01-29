#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX module management

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from functools import lru_cache
from typing import Dict, Type
from pathlib import Path
import importlib
import argparse


class MrModule:
    """Base class for all mrX modules."""

    name: str = "Unknown"
    description: str = "Unknown script description"
    note: str = None
    version: str = "1.0.0"
    author: str = "Unknown"

    def add_arguments(self, parser: argparse.ArgumentParser):
        """Add module-specific arguments.

        Args:
            parser: Argument parser to add arguments to.
        """
        pass

    def run(self, args: argparse.Namespace) -> int:
        """Execution entry, override in subclass.

        Args:
            args: Parsed command line arguments.

        Returns:
            Exit code (0 for success, non-zero for error).
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
        module_dir = Path(__file__).parent
        # Scan all modules
        for dir_path in module_dir.iterdir():
            # Skip non-directory
            if not dir_path.is_dir():
                continue
            # Import module
            try:
                importlib.import_module(f"modules.{dir_path.name}")
            except (ModuleNotFoundError, ImportError):
                pass

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

    @classmethod
    def print(cls, string: str):
        print(f"  {string}")
