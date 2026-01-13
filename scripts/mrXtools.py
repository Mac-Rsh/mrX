#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
mrX script management tool

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from modules import MrModule, MrLogger
from typing import Dict, Type, List
import argparse


class MrXtools:
    """mrXtools - Minimal module runner.

    Loads modules and executes them.
    """

    def __init__(self):
        """Initialize mrXtools."""
        self.logger = MrLogger("mrXtools", MrLogger.LEVEL_ERR)
        self.modules: Dict[str, Type[MrModule]] = MrModule.get_modules()
        self.parser = self._build_parser()

    def _build_parser(self) -> argparse.ArgumentParser:
        """Build the main argument parser.

        Returns:
            Configured ArgumentParser instance.
        """
        module_list = ", ".join(sorted(self.modules.keys()))

        # Build main parser
        parser = argparse.ArgumentParser(
            prog="mrXtools",
            description="mrX build and configuration tools",
            epilog=f"Available modules: {module_list}"
        )
        # Add module name argument
        parser.add_argument(
            "module",
            help="Module name to run"
        )
        # Add module arguments placeholder
        parser.add_argument(
            "module_args",
            nargs=argparse.REMAINDER,
            help="Arguments to pass to the module"
        )
        return parser

    def run(self, argv: List[str] = None) -> None:
        """Run a module.

        Args:
            argv: Command line arguments.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Parse main arguments
        self._parse_arguments(argv)

        # Build module instance
        self._parse_module()

        # Parse module arguments
        self._parse_module_arguments()

        # Run module
        self._run_module()

    def _parse_arguments(self, argv: List[str]) -> None:
        """Parse command line arguments.

        Args:
            argv: Command line arguments.
        """
        self.args = self.parser.parse_args(argv)

    def _parse_module(self) -> None:
        """Build module instance from parsed module name.

        Raises:
            KeyError: If module name is not found.
        """
        # Validate module name
        module = self.args.module
        if module not in self.modules:
            self.logger.err(f"Module '{module}' not found")
            raise KeyError(f"Module '{module}' not found")

        # Instantiate module
        self.module = self.modules[module]()

    def _parse_module_arguments(self) -> None:
        """Parse module-specific arguments."""
        # Build module argument parser
        module = self.module
        parser = argparse.ArgumentParser(
            prog=module.name,
            description=module.description,
            epilog=module.note
        )
        module.add_arguments(parser)

        # Parse module arguments
        args = self.args.module_args
        self.module_args = parser.parse_args(args)

    def _run_module(self) -> None:
        """Execute the module with parsed arguments.

        Raises:
            Exception: If module execution fails.
        """
        try:
            self.module.run(self.module_args)
        except Exception as e:
            self.logger.err(f"Error running module '{self.args.module}': {e}")
            raise


def main() -> None:
    """Main entry point for mrXtools.

    Exits with code 0 on success, non-zero on error.
    """
    tool = MrXtools()

    # Run tool
    tool.run()

    # Exit
    exit(0)


if __name__ == "__main__":
    main()
