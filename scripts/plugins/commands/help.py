#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Help command - Display simple dock usage guidance.

Shows short usage guidance for the dock and basic hints for a specific command.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dock import Dock, Plugin, PluginSpec, export_plugin


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
            "Usage: python ./scripts/cli.py help [command]",
            "Description: Show dock help or the help text of a specific command.",
            "Aliases: h, -h, --help",
            "Examples:",
            "- python ./scripts/cli.py help",
            "- python ./scripts/cli.py help list",
        ])

    def run(self, args: list[str]) -> int:
        """Display simple CLI help.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Discover plugins
        plugins = Dock.discover_plugins()

        # Print general help
        if not args:
            self._print_general_help(plugins)
            return 0

        # Get specific plugin
        command_name = args[0]
        command = plugins.get(command_name)
        if command is None:
            print(f"Unknown command: {command_name}")
            print("Use `python ./scripts/cli.py list` to see available commands.")
            return 1

        # Print command help
        self._print_command_help(command_name, command)
        return 0

    @staticmethod
    def _print_general_help(registry: dict[str, Plugin]) -> None:
        """Print basic CLI usage information.

        Args:
            registry: Dock registry containing command names and aliases.
        """
        # Print examples
        print("Examples:")
        print("- python ./scripts/cli.py list")
        print("- python ./scripts/cli.py help list")

    @staticmethod
    def _print_command_help(name: str, plugin: Plugin) -> None:
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
        print(f"Usage: python ./scripts/cli.py {spec.name} [args...]")
        print(f"Description: {spec.description}")
        print(
            f"Aliases: {', '.join(spec.aliases) if spec.aliases else '(none)'}")

        # Show resolved alias
        if name != spec.name:
            print(f"Resolved from alias: {name}")
