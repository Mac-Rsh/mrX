#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Shared argparse integration for registered commands.

Parsers never call `sys.exit()`. Help and syntax errors are captured and
converted into process exit codes by the application.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import Action, ArgumentParser, Namespace
from argparse import RawDescriptionHelpFormatter
from dataclasses import dataclass

from .registry import CommandNode


class ParserExit(Exception):
    """Internal non-process exit raised by argparse actions."""

    def __init__(self, status: int) -> None:
        """Store the requested parser exit status."""
        super().__init__(status)
        self.status = status


@dataclass(slots=True, frozen=True)
class ParseOutcome:
    """Successful arguments or captured parser output and an exit code."""

    arguments: Namespace | None
    exit_code: int | None = None
    output: tuple[tuple[str, bool], ...] = ()


def make_parser(prog: str, description: str = "") -> ArgumentParser:
    """Create a command parser with a stable help formatter."""
    return ArgumentParser(
        prog=prog,
        description=description,
        formatter_class=RawDescriptionHelpFormatter,
    )


def adopt_parser(parser: ArgumentParser) -> ArgumentParser:
    """Make an argparse parser capture output instead of exiting."""
    parser.output = []  # type: ignore[attr-defined]

    def _print_message(message: str | None, file: object = None) -> None:
        """Capture argparse messages instead of writing them to a stream."""
        del file
        if message:
            parser.output.append((message, False))  # type: ignore[attr-defined]

    def exit_parser(status: int = 0, message: str | None = None) -> None:
        """Raise a captured parser exit instead of calling sys.exit()."""
        if message:
            parser.output.append(
                (message, status != 0))  # type: ignore[attr-defined]
        raise ParserExit(status)

    def error(message: str) -> None:
        """Capture usage plus the argparse error and abort parsing."""
        parser.output.append(
            (parser.format_usage(), True))  # type: ignore[attr-defined]
        parser.output.append((f"{parser.prog}: error: {message}\n",
                              True))  # type: ignore[attr-defined]
        raise ParserExit(2)

    parser._print_message = _print_message  # type: ignore[method-assign]
    parser.exit = exit_parser  # type: ignore[method-assign]
    parser.error = error  # type: ignore[method-assign]
    return parser


class CommandParser:
    """Build and run argument parsers from registered command definitions."""

    def __init__(self, program: str) -> None:
        """Create a parser factory for one CLI program name."""
        self.program = program

    def build(self, node: CommandNode) -> ArgumentParser:
        """Build a non-exiting parser from one command definition."""
        parser = node.command.create_parser(self.program_for(node))
        return adopt_parser(parser)

    def parse(self, node: CommandNode, arguments: list[str]) -> ParseOutcome:
        """Parse arguments for one resolved command node."""
        parser = self.build(node)
        try:
            namespace = parser.parse_args(arguments)
        except ParserExit as exc:
            return ParseOutcome(
                arguments=None,
                exit_code=exc.status,
                output=tuple(getattr(parser, "output", ())),
            )
        return ParseOutcome(arguments=namespace)

    def format_help(self, node: CommandNode) -> str:
        """Render generated help for one command node."""
        return self.build(node).format_help()

    def program_for(self, node: CommandNode) -> str:
        """Return the copy-paste program name for one command path."""
        return " ".join((self.program, *node.path))


def option_actions(parser: ArgumentParser) -> list[Action]:
    """Return option actions declared on one parser."""
    return [action for action in parser._actions if action.option_strings]


def positional_actions(parser: ArgumentParser) -> list[Action]:
    """Return positional actions declared on one parser, excluding help."""
    actions: list[Action] = []
    for action in parser._actions:
        if action.option_strings:
            continue
        if action.dest in {"help"}:
            continue
        actions.append(action)
    return actions


def action_takes_value(action: Action) -> bool:
    """Return whether an option action consumes a following argument."""
    if action.nargs == 0:
        return False
    name = type(action).__name__
    if name in {
        "_StoreTrueAction",
        "_StoreFalseAction",
        "_CountAction",
        "_HelpAction",
        "_VersionAction",
    }:
        return False
    return bool(action.option_strings)


def action_is_repeatable(action: Action) -> bool:
    """Return whether an option may be supplied more than once."""
    name = type(action).__name__
    if name in {"_AppendAction", "_AppendConstAction", "_CountAction"}:
        return True
    return action.nargs in {"*", "+"}
