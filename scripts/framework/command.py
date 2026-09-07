#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Command contracts and structured execution results.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

from argparse import ArgumentParser, Namespace
from collections.abc import Iterable
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import TYPE_CHECKING

from .diagnostics import Diagnostic, DiagnosticLevel

if TYPE_CHECKING:
    from .context import CommandContext
    from .completion import CompletionItem, CompletionRequest


@dataclass(slots=True, frozen=True)
class CommandSpec:
    """Stable metadata used to register and display a command."""

    name: str
    description: str
    aliases: tuple[str, ...] = ()
    version: str = ""
    hidden: bool = False
    category: str = ""


@dataclass(slots=True, frozen=True)
class CommandResult:
    """Normalized result returned by every command implementation."""

    exit_code: int = 0
    message: str = ""
    data: object | None = None
    diagnostics: tuple[Diagnostic, ...] = ()

    @classmethod
    def success(cls, message: str = "",
                data: object | None = None) -> "CommandResult":
        """Build a successful command result."""
        return cls(message=message, data=data)

    @classmethod
    def failure(
            cls,
            message: str,
            code: str = "COMMAND_FAILED",
            hint: str = "",
            exit_code: int = 1,
    ) -> "CommandResult":
        """Build a failed command result with one error diagnostic."""
        diagnostic = Diagnostic(
            level=DiagnosticLevel.ERROR,
            code=code,
            message=message,
            hint=hint,
        )
        return cls(exit_code=exit_code, diagnostics=(diagnostic,))


class Command(ABC):
    """Base contract implemented by built-in and plugin commands."""

    spec: CommandSpec

    def create_parser(self, prog: str) -> ArgumentParser:
        """Build the argparse parser for this command.

        Arguments:
            prog: Program name used in generated help text.

        Returns:
            Parser that declares this command's arguments exactly once.
        """
        from .parser import make_parser

        parser = make_parser(prog, self.spec.description)
        self.configure(parser)
        return parser

    def configure(self, parser: ArgumentParser) -> None:
        """Declare command arguments on a shared parser."""
        del parser

    def subcommands(self) -> Iterable["Command"]:
        """Return direct child commands exposed below this command."""
        return ()

    @abstractmethod
    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Execute the command and return a normalized result."""
        raise NotImplementedError

    def complete(
            self,
            context: CommandContext,
            request: CompletionRequest,
    ) -> Iterable[CompletionItem]:
        """Return extra completions, or an empty iterable to use argparse."""
        del context, request
        return ()
