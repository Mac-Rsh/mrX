#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Application composition, plugin loading, and top-level dispatch.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from collections.abc import Sequence
from pathlib import Path
from typing import TextIO
import sys

from .builtins import CdCommand, ClearCommand, ExitCommand, HelpCommand
from .builtins import HistoryCommand, ListCommand, PwdCommand, QuitCommand
from .completion import RegistryCompleter
from .console import Console
from .context import CommandContext, SessionState, current_environment
from .context import detect_project_root, read_project_version
from .diagnostics import Diagnostic, DiagnosticLevel
from .history import create_file_history
from .parser import CommandParser
from .plugin import ApplicationServices, load_plugin_catalog
from .registry import CommandRegistry
from .runner import CommandRunner
from .shell import InteractiveShell

PROGRAM = "python ./scripts/cli.py"


class Application:
    """Coordinate mode selection, plugin loading, and command dispatch."""

    def __init__(
            self,
            name: str,
            version: str,
            project_root: Path,
            registry: CommandRegistry,
            parser: CommandParser,
            runner: CommandRunner,
            console: Console,
            session: SessionState,
            startup_diagnostics: Sequence[Diagnostic] = (),
    ) -> None:
        """Create a fully composed command application."""
        self.name = name
        self.version = version
        self.project_root = project_root
        self.registry = registry
        self.parser = parser
        self.runner = runner
        self.console = console
        self.session = session
        self.startup_diagnostics = tuple(startup_diagnostics)

    def run(
            self,
            arguments: Sequence[str],
            input_stream: TextIO = sys.stdin,
    ) -> int:
        """Run one CLI invocation and return its process exit code."""
        argv, verbose, no_color, early_exit = self._parse_global_options(
            list(arguments))
        if no_color:
            self.console.set_color(False)
        if early_exit is not None:
            return early_exit

        if self.startup_diagnostics:
            self.console.diagnostics(self.startup_diagnostics)

        if not argv or argv == ["shell"]:
            if self._is_interactive(input_stream):
                return self._run_shell(verbose)
            self._print_general_help()
            return 0

        return self.execute(argv, verbose=verbose, interactive=False)

    def execute(
            self,
            arguments: Sequence[str],
            verbose: bool = False,
            interactive: bool | None = None,
    ) -> int:
        """Resolve, parse, and execute one command invocation."""
        argv, extra_verbose, no_color, early_exit = self._parse_global_options(
            list(arguments))
        if early_exit is not None:
            return early_exit
        verbose = verbose or extra_verbose
        if interactive is None:
            interactive = False
        if not argv:
            return 0

        console = self.console
        if no_color:
            if interactive:
                console = Console(
                    stdout=self.console.stdout,
                    stderr=self.console.stderr,
                    color=False,
                )
            else:
                self.console.set_color(False)

        resolved = self.registry.resolve(argv)
        if resolved is None:
            console.diagnostic(Diagnostic(
                level=DiagnosticLevel.ERROR,
                code="COMMAND_NOT_FOUND",
                message=f"Unknown command: {argv[0]}",
                hint="Run 'help' or 'list' to see available commands",
            ))
            return 2
        node, remaining = resolved

        outcome = self.parser.parse(node, remaining)
        if outcome.exit_code is not None:
            for message, error in outcome.output:
                console.write(message, error=error, end="")
            return outcome.exit_code
        if outcome.arguments is None:
            console.diagnostic(Diagnostic(
                level=DiagnosticLevel.ERROR,
                code="PARSER_STATE_INVALID",
                message="Command parser returned no arguments",
            ))
            return 1

        context = CommandContext(
            project_root=self.project_root,
            session=self.session,
            environment=current_environment(),
            console=console,
            verbose=verbose,
            interactive=interactive,
        )
        return self.runner.run(node, context, outcome.arguments)

    def _run_shell(self, verbose: bool) -> int:
        """Create the prompt session and run the interactive loop."""
        completer = RegistryCompleter(
            self.registry, self.parser, self.session)
        shell = InteractiveShell(
            application=self,
            session=self.session,
            console=self.console,
            history=create_file_history(),
            completer=completer,
        )
        return shell.run(
            lambda arguments: self.execute(
                arguments, verbose=verbose, interactive=True)
        )

    def _parse_global_options(
            self,
            arguments: list[str],
    ) -> tuple[list[str], bool, bool, int | None]:
        """Consume framework options that precede the command path."""
        verbose = False
        no_color = False
        while arguments:
            option = arguments[0]
            if option == "--no-color":
                no_color = True
                arguments.pop(0)
            elif option == "--verbose":
                verbose = True
                arguments.pop(0)
            else:
                break

        if arguments and arguments[0] in {"--help", "-h"}:
            self._print_general_help()
            return [], verbose, no_color, 0
        if arguments and arguments[0] == "--version":
            self.console.write(f"{self.name} {self.version}")
            return [], verbose, no_color, 0
        return arguments, verbose, no_color, None

    def _print_general_help(self) -> None:
        """Render application usage and visible root commands."""
        self.console.write(f"{self.name} - mrX command line tools")
        self.console.write()
        self.console.write(f"Usage: {PROGRAM} <command> [options]")
        self.console.write(f"       {PROGRAM} [--help] [--version]")
        self.console.write()
        rows = (
            (node.command.spec.name, node.command.spec.description)
            for node in self.registry.roots()
        )
        self.console.table(("Command", "Description"), rows)
        self.console.write()
        self.console.write("Use 'help <command>' for command details.")
        self.console.write(
            "Global options: --no-color, --verbose, --help, --version")

    def _is_interactive(self, input_stream: TextIO) -> bool:
        """Check whether both input and output are attached to a TTY."""
        input_is_tty = bool(getattr(input_stream, "isatty", lambda: False)())
        output_is_tty = bool(
            getattr(self.console.stdout, "isatty", lambda: False)())
        return input_is_tty and output_is_tty


def create_application() -> Application:
    """Compose the CLI application and load first-party plugins."""
    from plugins.catalog import BUILTIN_PLUGINS

    project_root = detect_project_root()
    console = Console()
    registry = CommandRegistry()
    parser = CommandParser(PROGRAM)
    session = SessionState(cwd=Path.cwd())
    diagnostics: list[Diagnostic] = []
    services = ApplicationServices(
        project_root=project_root,
        environment=current_environment(),
        console=console,
    )

    registry.register(HelpCommand(registry, parser, PROGRAM))
    registry.register(ListCommand(registry))
    registry.register(CdCommand(session))
    registry.register(PwdCommand())
    registry.register(ClearCommand())
    registry.register(HistoryCommand())
    registry.register(ExitCommand())
    registry.register(QuitCommand())

    catalog = load_plugin_catalog(services, BUILTIN_PLUGINS)
    diagnostics.extend(catalog.diagnostics)
    for plugin in catalog.plugins:
        try:
            registry.register_plugin(plugin)
        except Exception as exc:
            diagnostics.append(Diagnostic(
                level=DiagnosticLevel.ERROR,
                code="COMMAND_REGISTRATION_FAILED",
                message=f"Failed to register plugin '{plugin.name}': {exc}",
            ))

    return Application(
        name="mrX",
        version=read_project_version(project_root),
        project_root=project_root,
        registry=registry,
        parser=parser,
        runner=CommandRunner(),
        console=console,
        session=session,
        startup_diagnostics=diagnostics,
    )
