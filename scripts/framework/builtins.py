#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Framework-owned commands registered explicitly by the application.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace

from .completion import DIRECTORY_COMPLETION, attach_path_completion
from .completion import CompletionItem, CompletionRequest
from .command import Command, CommandResult, CommandSpec
from .registry import CommandRegistry
from .context import CommandContext, SessionState
from .parser import CommandParser
from .history import load_history_lines

_SHELL_HINT = (
    "Interactive commands: help, list, history, clear, pwd, cd, exit, quit"
)


class ListCommand(Command):
    """List every visible command registered in the application."""

    spec = CommandSpec(
        name="list",
        description="List available commands",
        aliases=("ls",),
        category="shell",
    )

    def __init__(self, registry: CommandRegistry) -> None:
        """Bind the list command to the application registry."""
        self.registry = registry

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Render all visible command paths and descriptions."""
        del arguments
        rows = (
            (
                " ".join(node.path),
                ", ".join(node.command.spec.aliases) or "-",
                node.command.spec.description,
            )
            for node in self.registry.iter_nodes()
        )
        context.console.table(("Command", "Aliases", "Description"), rows)
        return CommandResult.success()


class HelpCommand(Command):
    """Display generated application or command help."""

    spec = CommandSpec(
        name="help",
        description="Show application or command help",
        aliases=("h",),
        category="shell",
    )

    def __init__(
            self,
            registry: CommandRegistry,
            parser: CommandParser,
            program: str,
    ) -> None:
        """Bind help rendering to the registry and parser."""
        self.registry = registry
        self.parser = parser
        self.program = program

    def configure(self, parser: ArgumentParser) -> None:
        """Declare an optional nested command path."""
        parser.add_argument(
            "command",
            nargs="*",
            help="Command path to describe",
        )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Render help for the requested command path."""
        if not arguments.command:
            self._print_general_help(context)
            return CommandResult.success()

        node = self.registry.find(arguments.command)
        if node is None:
            return CommandResult.failure(
                message=f"Unknown command path: {' '.join(arguments.command)}",
                code="COMMAND_NOT_FOUND",
                hint="Run 'list' to see available commands",
                exit_code=2,
            )
        context.console.write(self.parser.format_help(node), end="")
        return CommandResult.success()

    def complete(self, context: CommandContext,
                 request: CompletionRequest) -> list[CompletionItem]:
        """Complete registered commands and subcommands for `help`."""
        del context
        if request.tokens:
            parent = self.registry.find(request.tokens)
            if parent is None:
                return []
            nodes = sorted(
                (child for child in parent.children.values()
                 if not child.command.spec.hidden),
                key=lambda child: child.command.spec.name,
            )
        else:
            nodes = list(self.registry.roots())
        items: list[CompletionItem] = []
        for node in nodes:
            spec = node.command.spec
            items.append(CompletionItem(
                value=spec.name,
                description=spec.description,
                kind="command",
            ))
            for alias in spec.aliases:
                items.append(CompletionItem(
                    value=alias,
                    description=f"alias of {spec.name}",
                    kind="alias",
                ))
        return items

    def _print_general_help(self, context: CommandContext) -> None:
        """Render application usage and visible root commands."""
        context.console.write("mrX command line tools")
        context.console.write()
        context.console.write(
            f"Usage: {self.program} <command> [options]")
        context.console.write(
            f"       {self.program} [--help] [--version]")
        context.console.write()
        rows = (
            (node.command.spec.name, node.command.spec.description)
            for node in self.registry.roots()
        )
        context.console.table(("Command", "Description"), rows)
        context.console.write()
        context.console.write("Use 'help <command>' for command details.")
        if context.interactive:
            context.console.write(_SHELL_HINT)
        context.console.write(
            "Global options: --no-color, --verbose, --help, --version")


class CdCommand(Command):
    """Change the session working directory without calling os.chdir()."""

    spec = CommandSpec(
        name="cd",
        description="Change the session working directory",
        category="shell",
    )

    def __init__(self, session: SessionState) -> None:
        """Bind cd to the shared session state."""
        self.session = session

    def configure(self, parser: ArgumentParser) -> None:
        """Declare the target directory."""
        action = parser.add_argument(
            "directory",
            help="Directory to become the session working directory",
        )
        attach_path_completion(action, DIRECTORY_COMPLETION)

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Update the session cwd after validating the directory."""
        path = context.resolve_path(arguments.directory)
        if not path.exists():
            return CommandResult.failure(
                message=f"Directory not found: {arguments.directory}",
                code="DIRECTORY_NOT_FOUND",
            )
        if not path.is_dir():
            return CommandResult.failure(
                message=f"Not a directory: {arguments.directory}",
                code="NOT_A_DIRECTORY",
            )
        self.session.cwd = path
        return CommandResult.success()


class PwdCommand(Command):
    """Print the session working directory."""

    spec = CommandSpec(
        name="pwd",
        description="Print the session working directory",
        category="shell",
    )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Write the current session cwd."""
        del arguments
        context.console.write(str(context.cwd))
        return CommandResult.success()


class ClearCommand(Command):
    """Clear the terminal screen."""

    spec = CommandSpec(
        name="clear",
        description="Clear the terminal screen",
        category="shell",
    )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Clear stdout when possible."""
        del arguments
        context.console.clear()
        return CommandResult.success()


class HistoryCommand(Command):
    """Show persistent interactive command history."""

    spec = CommandSpec(
        name="history",
        description="Show persistent command history",
        category="shell",
    )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Render stored history lines."""
        del arguments
        lines = load_history_lines()
        if not lines:
            context.console.write("No command history")
            return CommandResult.success()
        width = len(str(len(lines)))
        for index, line in enumerate(lines, start=1):
            context.console.write(f"{index:>{width}}  {line}")
        return CommandResult.success()


class ExitCommand(Command):
    """Leave the interactive shell, or exit a one-shot invocation."""

    spec = CommandSpec(
        name="exit",
        description="Exit the interactive shell",
        category="shell",
    )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Request shell exit while preserving the last command's status."""
        del arguments
        if context.interactive:
            context.session.exit_requested = True
            return CommandResult(exit_code=context.session.last_exit_code)
        return CommandResult.success()


class QuitCommand(ExitCommand):
    """Alias command for leaving the interactive shell."""

    spec = CommandSpec(
        name="quit",
        description="Exit the interactive shell",
        aliases=("q",),
        category="shell",
    )
