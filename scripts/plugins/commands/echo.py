#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Echo command used as a minimal local plugin implementation.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace

from framework.command import Command, CommandResult, CommandSpec
from framework.context import CommandContext


class EchoCommand(Command):
    """Print positional text through the shared command context."""

    spec = CommandSpec(
        name="echo",
        description="Print the provided arguments",
    )

    def configure(self, parser: ArgumentParser) -> None:
        """Declare repeatable positional text values."""
        parser.add_argument("text", nargs="*", help="Text to print")

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Print the parsed text values in their original order."""
        context.console.write(" ".join(arguments.text))
        return CommandResult.success()
