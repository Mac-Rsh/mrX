#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Echo command - Print input arguments back to the terminal.

Echoes all provided command line arguments in their original order as a single
space-separated line.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dock import PluginSpec, export_plugin


@export_plugin
class EchoCommand:
    """Built-in command for echoing input arguments."""

    spec = PluginSpec(
        name="echo",
        description="Print the provided arguments.",
    )

    def help(self) -> str:
        """Return detailed help text for the echo command.

        Returns:
            Human-readable help text for the command.
        """

        return "\n".join([
            "Usage: python ./scripts/cli.py echo [text...]",
            "Description: Print the provided arguments as one line.",
            "Aliases: (none)",
            "Examples:",
            "- python ./scripts/cli.py echo hello world",
        ])

    def run(self, args: list[str]) -> int:
        """Print the provided command line arguments.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Print command line arguments
        print(" ".join(args))
        return 0
