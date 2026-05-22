#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Help command - Display simple dock usage guidance.

Shows short usage guidance for the dock and basic hints for a specific command.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dock import Dock, Plugin, PluginSpec, export_plugin

_DIRECT_PREFIX = "python ./scripts/cli.py"


@export_plugin
class HelpCommand:
    """Built-in command for printing simple usage help."""

    spec = PluginSpec(
        name="help",
        description="Show simple usage help.",
        aliases=("h", "-h", "--help"),
    )

    def help(self) -> str:
        """Return detailed help text for the help command.

        Returns:
            Human-readable help text for the command.
        """
        return "\n".join([
            f"Usage: {self._format_command('help [command]')}",
            "Description: Show dock help or the help text of a specific command.",
            "Aliases: h, -h, --help",
            "Examples:",
            f"- {self._format_command('help')}",
            f"- {self._format_command('help list')}",
        ])

    def run(self, args: list[str]) -> int:
        """Display simple CLI help.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Print general help
        if not args:
            self._print_general_help()
            return 0

        # Discover plugins only when a specific command is requested
        plugins = Dock.discover_plugins()

        command_name = args[0]
        command = plugins.get(command_name)
        if command is None:
            print(f"Unknown command: {command_name}")
            print(
                f"Use `{self._format_command('list')}` to see available commands.")
            return 1

        # Print command help
        self._print_command_help(command_name, command)
        return 0

    def _print_general_help(self) -> None:
        """Print basic CLI usage information."""
        print("Examples:")
        print(f"- {self._format_command('list')}")
        print(f"- {self._format_command('help list')}")

    def _print_command_help(self, name: str, plugin: Plugin) -> None:
        """Print simple help for a single command.

        Args:
            name: Name or alias used by the caller.
            plugin: Resolved command instance.
        """
        spec = plugin.spec

        # Print command help
        help_handler = getattr(plugin, "help", None)
        if callable(help_handler):
            print(help_handler())
            if name != spec.name:
                print(f"Resolved from alias: {name}")
            return

        # Print command usage
        print(f"Usage: {self._format_command(f'{spec.name} [args...]')}")
        print(f"Description: {spec.description}")
        print(
            f"Aliases: {', '.join(spec.aliases) if spec.aliases else '(none)'}")

        # Show resolved alias
        if name != spec.name:
            print(f"Resolved from alias: {name}")

    @staticmethod
    def _format_command(args: str) -> str:
        """Format a command for direct CLI usage."""
        return f"{_DIRECT_PREFIX} {args}"
