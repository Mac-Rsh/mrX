#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX script management tool

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from typing import Dict, Type, List, Optional
from modules import MrModule
import argparse


class MrXtools:
    """mrXtools - Minimal module runner.

    Loads modules and executes them.
    """

    def __init__(self):
        """Initialize mrXtools."""
        self.modules: Dict[str, Type[MrModule]] = MrModule.get_modules()
        self.parser = self._build_parser()

    @staticmethod
    def _build_parser() -> argparse.ArgumentParser:
        """Build the main argument parser.

        Returns:
            Configured ArgumentParser instance.
        """
        # Build main parser
        parser = argparse.ArgumentParser(
            prog="mrXtools",
            description="mrX build and configuration tools",
        )
        # Add module name argument
        parser.add_argument(
            "module",
            nargs="?",
            help="Module name to run"
        )
        # Add list argument
        parser.add_argument(
            "-l", "--list",
            action="store_true",
            help="List available modules"
        )
        # Add module arguments placeholder
        parser.add_argument(
            "module_args",
            nargs=argparse.REMAINDER,
            help="Arguments to pass to the module"
        )
        return parser

    def run(self, argv: List[str] = None) -> int:
        """Run a module.

        Args:
            argv: Command line arguments.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Parse main arguments
        args = self.parser.parse_args(argv)

        # List command
        if args.list:
            self._list_modules()
            return 0

        # Check if module name is provided
        if not args.module:
            self.parser.print_help()
            return 0

        # Find module
        module = self._find_module(args.module)
        if not module:
            return 1

        # Parse module arguments
        module_args = self._parse_module_arguments(module, args.module_args)

        # Run module
        try:
            print("-" * 50)
            print(f"{module.description}\n")
            print(f"Version: {module.version}")
            print(f"Author: {module.author}")
            print("-" * 50)
            print(f"{module.name}:")
            module.run(module_args)
        except Exception as e:
            print(f"Error running module '{module.name}': {e}")
            return 1
        return 0

    def _list_modules(self) -> None:
        """List all available modules."""
        print("Available modules:")
        for name in sorted(self.modules.keys()):
            module_cls = self.modules[name]
            print(f"  {name:<15} - {module_cls.description}")

    def _find_module(self, module: str) -> Optional[MrModule]:
        """Build module instance from parsed module name.

        Raises:
            KeyError: If module name is not found.
        """
        # Validate module name
        if module not in self.modules:
            print(f"Module '{module}' not found")
            return None
        # Instantiate module
        return self.modules[module]()

    @staticmethod
    def _parse_module_arguments(module: MrModule, args: List[str]) -> argparse.Namespace:
        """Parse module-specific arguments."""
        # Build module argument parser
        parser = argparse.ArgumentParser(
            prog=module.name,
            description=module.description,
            epilog=module.note
        )
        module.add_arguments(parser)
        # Parse module arguments
        return parser.parse_args(args)


def main() -> None:
    """Main entry point for mrXtools.

    Exits with code 0 on success, non-zero on error.
    """
    tool = MrXtools()

    # Run tool
    exit(tool.run())


if __name__ == "__main__":
    main()
