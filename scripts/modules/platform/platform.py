#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX platform management module

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..module import MrModule, argparse
from pathlib import Path
from typing import List
import shutil


class Platform(MrModule):
    """Platform management module for platform-specific code switching."""

    name: str = "platform"
    description: str = "Manage platform code"
    note: str = "Example: python mrXtools.py platform"
    version: str = "1.0.0"
    author: str = "MacRsh"

    def add_arguments(self, parser: argparse.ArgumentParser):
        """Add module-specific arguments.

        Args:
            parser: Argument parser to add arguments to.
        """
        parser.add_argument(
            "platform",
            nargs="?",
            help="Platform name (e.g., 'arch,x86,win')"
        )
        # Add list argument
        parser.add_argument(
            "-l", "--list",
            action="store_true",
            help="List available modules"
        )
        parser.add_argument(
            "-s", "--source",
            type=str,
            default="arch",
            help="Source directory path (default: arch)",
        )
        parser.add_argument(
            "-d", "--dest",
            type=str,
            default="./",
            help="Destination directory path (default: ./)",
        )
        parser.add_argument(
            "-i", "--ident",
            type=str,
            default="mrXplatform.json",
            help="Platform identifier name (default: mrXplatform.json)",
        )

    def run(self, args: argparse.Namespace) -> int:
        """Execute platform switching workflow.

        Args:
            args: Parsed command line arguments.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Parse and validate arguments
        self._parse_arguments(args)

        # Scan available platforms
        self._update_platforms()

        # List command
        if args.list:
            self._list_platforms()
            return 0

        # Check if platform name is provided
        if not args.platform:
            raise ValueError("Platform name is required")

        # Activate specified one
        self._use_platform(args.platform)
        return 0

    def _parse_arguments(self, args: argparse.Namespace) -> None:
        """Parse and validate command line arguments."""
        # Source path
        self.source_path = Path(args.source)
        if not self.source_path.exists():
            raise FileNotFoundError(f"Source directory not found: {args.source}")
        # Destination path
        self.dest_path = Path(args.dest)
        if not self.dest_path.exists():
            raise FileNotFoundError(f"Destination directory not found: {args.dest}")
        # Identifier name
        self.ident = args.ident

    def _update_platforms(self) -> None:
        """Scan source_path and find all platform directories."""
        self.platforms = {}
        # Scan source directory
        for platform_path in self.source_path.rglob("*"):
            if not platform_path.is_dir():
                continue
            # Get platform
            ident_file = platform_path / self.ident
            if ident_file.exists():
                rel_path = platform_path.relative_to(self.source_path.parent)
                platform_name = str(rel_path).replace("/", ",").replace("\\", ",")
                self.platforms[platform_name] = platform_path

    def _list_platforms(self) -> None:
        """List all available platforms."""
        MrModule.print("Available platforms:")
        for name in sorted(self.platforms.keys()):
            MrModule.print(f"  {name}")

    def _get_inheritance_chain(self, target_path: Path) -> List[Path]:
        """Get inheritance chain by walking up from target path."""
        current_path = target_path
        chain = []

        # Walk up the directory tree
        while current_path >= self.source_path:
            ident_file = current_path / self.ident
            if ident_file.exists():
                # Add an identifier to the chain
                chain.insert(0, current_path)
            # Break if we reached the source directory
            if current_path == self.source_path:
                break
            current_path = current_path.parent
        return chain

    def _import_platform_level(self, level_path: Path) -> None:
        """Import files from a single platform level."""

        def ignore_files(src, names):
            """Return list of files/directories to ignore during copy."""
            ignored = []
            for name in names:
                # Ignore platform identifier file
                if name == self.ident:
                    ignored.append(name)
                # Ignore subdirectories that have their own platform identifier
                elif (Path(src) / name).is_dir() and (Path(src) / name / self.ident).exists():
                    ignored.append(name)
            return ignored

        # Copy files
        shutil.copytree(level_path, self.dest_path, dirs_exist_ok=True, ignore=ignore_files)

    def _use_platform(self, name: str) -> None:
        """Activate platform with hierarchical inheritance."""
        # Check if platform exists
        if name not in self.platforms:
            raise KeyError(f"Platform '{name}' not found")
        # Get inheritance chain
        chain = self._get_inheritance_chain(self.platforms[name])
        if not chain:
            raise RuntimeError(f"Platform '{name}' has invalid inheritance chain")
        MrModule.print(f"Importing platform '{name}' ({len(chain)} levels):")

        # Import each level
        for level_path in chain:
            # Convert path to platform name format (e.g., "arch/arm/m4" → "arch,arm,m4")
            rel_path = level_path.relative_to(self.source_path.parent)
            level_name = str(rel_path).replace("/", ",").replace("\\", ",")
            self._import_platform_level(level_path)
            MrModule.print(f"  {level_name}")
