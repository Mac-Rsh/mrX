#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX platform management module

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..module import MrModule, MrLogger, argparse
from pathlib import Path
import shutil


class Platform(MrModule):
    """Platform management module for platform-specific code switching."""

    name: str = "platform"
    description: str = "Manage platform code"
    note: str = "Example: python mrXtools.py platform"
    version: str = "1.0.0"
    author: str = "MacRsh"
    log_level = MrLogger.LEVEL_INF

    def add_arguments(self, parser: argparse.ArgumentParser):
        """Add module-specific arguments.

        Args:
            parser: Argument parser to add arguments to.
        """
        parser.add_argument(
            "platform",
            help="Platform name (e.g., 'arch,x86,win')"
        )
        parser.add_argument(
            "--source",
            type=str,
            default="arch",
            help="Source directory path (default: arch)",
        )
        parser.add_argument(
            "--dest",
            type=str,
            default="./",
            help="Destination directory path (default: ./)",
        )
        parser.add_argument(
            "--json",
            type=str,
            default="mrXplatform.json",
            help="Platform identifier JSON name (default: mrXplatform.json)",
        )

    def run(self, args: argparse.Namespace):
        """Execute platform switching workflow.

        Args:
            args: Parsed command line arguments.

        Returns:
            None
        """
        # Parse and validate arguments
        self._parse_arguments(args)

        # Scan available platforms and activate specified one
        self._update_platforms()
        self._use_platform(args.platform)

    def _parse_arguments(self, args: argparse.Namespace) -> None:
        """Parse and validate command line arguments.

        Args:
            args: Parsed command line arguments.

        Returns:
            None

        Raises:
            FileNotFoundError: If source or destination directory does not exist.
        """
        # Validate source directory
        self.source_path = Path(args.source)
        if not self.source_path.exists():
            self.logger.err(f"Source directory not found: {args.source}")
            raise FileNotFoundError(f"Source directory not found: {args.source}")

        # Validate destination directory
        self.dest_path = Path(args.dest)
        if not self.dest_path.exists():
            self.logger.err(f"Destination directory not found: {args.dest}")
            raise FileNotFoundError(f"Destination directory not found: {args.dest}")

        # Store platform identifier JSON filename
        self.json_name = args.json

    def _update_platforms(self) -> None:
        """Scan source_path and find all platform directories.

        A directory is considered a platform if it contains the platform JSON file.

        Populates self.platforms dictionary with:
            Key: comma-separated path (e.g., "arch,x86,win")
            Value: platform Path object
        """
        self.platforms = {}

        # Scan all subdirectories recursively
        for platform_path in self.source_path.rglob("*"):
            # Process only directories
            if not platform_path.is_dir():
                continue

            # Check if platform identifier JSON exists in this directory
            json_file = platform_path / self.json_name
            if json_file.exists():
                # Get relative path from source parent directory
                rel_path = platform_path.relative_to(self.source_path.parent)

                # Create platform name by replacing path separators with commas
                platform_name = str(rel_path).replace("/", ",").replace("\\", ",")

                # Store platform mapping
                self.platforms[platform_name] = platform_path
                self.logger.dbg(f"Found platform: {platform_name} -> {platform_path}")

    def _use_platform(self, name: str) -> None:
        """Activate platform by copying its directory to destination.

        Args:
            name: Platform name (e.g., "arch,x86,win").

        Returns:
            None

        Raises:
            KeyError: If platform name is not found.
        """
        # Validate platform name
        if name not in self.platforms:
            self.logger.err(f"Platform '{name}' not found")
            raise KeyError(f"Platform '{name}' not found")

        # Copy platform directory contents to destination (merge mode)
        shutil.copytree(self.platforms[name], self.dest_path, dirs_exist_ok=True)
        self.logger.inf(f"Platform '{name}' activated successfully")
