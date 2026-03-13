#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
List command - Display all registered dock commands.

Prints built-in commands and plugins discovered by the dock, including aliases
and optional metadata such as version and author.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dock import Dock, Plugin, PluginSpec, export_plugin


@export_plugin
class ListCommand:
    """Built-in command for listing all available commands."""

    spec = PluginSpec(
        name="list",
        description="List all available commands.",
        aliases=("ls", "-l", "--list"),
    )

    def help(self) -> str:
        """Return detailed help text for the list command.

        Returns:
            Human-readable help text for the command.
        """

        return "\n".join([
            "Usage: python ./scripts/cli.py list",
            "Description: List all registered primary commands.",
            "Aliases: ls, -l, --list",
        ])

    def run(self, args: list[str]) -> int:
        """Display all registered primary commands.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        del args

        # Discover plugins
        plugins = Dock.discover_plugins()

        # Collect primary commands
        plugins = self._collect_primary_commands(plugins)

        # Print commands
        print("Available commands:")
        for plugin in plugins:
            print(self._format_command(plugin))

        return 0

    @staticmethod
    def _collect_primary_commands(registry: dict[str, Plugin]) -> list[Plugin]:
        """Collect only primary command entries from the registry.

        Args:
            registry: Dock registry containing command names and aliases.

        Returns:
            Primary command instances without alias duplicates.
        """
        plugins: list[Plugin] = []

        # Exclude aliases
        for name, plugin in registry.items():
            if plugin.spec.name != name:
                continue
            plugins.append(plugin)
        return plugins

    @staticmethod
    def _format_command(plugin: Plugin) -> str:
        """Format a single command line for terminal output.

        Args:
            plugin: Command plugin instance.

        Returns:
            Human-readable command description line.
        """
        spec = plugin.spec

        # Add aliases
        aliases = f"[{', '.join(spec.aliases)}]" if spec.aliases else ""
        return f"- {spec.name}{aliases}: {spec.description}"
