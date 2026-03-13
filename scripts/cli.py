#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Scripts package entry - Command line dispatcher for the plugin dock.

Receives command line arguments, discovers registered plugins, and forwards
remaining arguments to the selected command implementation.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dock import Dock
import sys


def main() -> int:
    """Run the package command line entry point.

    Returns:
        Exit code (0 for success, non-zero for error).
    """
    # Parse command line arguments
    args = sys.argv[1:]
    if not args:
        print("Usage: python ./scripts/cli.py <command> [args...]")
        return 1
    command = args[0]

    # Discover all plugins
    try:
        plugins = Dock.discover_plugins()
    except Exception as exc:
        print(f"Failed to discover commands: {exc}")
        return 1

    # Lookup specified plugin
    plugin = plugins.get(command)
    if plugin is None:
        print(f"Unknown command: {command}")
        print("Use `python ./scripts/cli.py list` to see available commands.")
        return 1

    # Run plugin
    try:
        return plugin.run(args[1:])
    except Exception as exc:
        print(f"Failed to run command: {command} ({exc})")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
