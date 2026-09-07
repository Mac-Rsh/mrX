#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Command execution and result rendering.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import Namespace
import traceback

from .command import CommandResult
from .context import CommandContext
from .diagnostics import Diagnostic, DiagnosticLevel
from .registry import CommandNode


class CommandRunner:
    """Execute one parsed command and convert outcomes to an exit code."""

    def run(
            self,
            node: CommandNode,
            context: CommandContext,
            arguments: Namespace,
    ) -> int:
        """Run one command and convert all outcomes to an exit code."""
        command = node.command
        try:
            result = command.execute(context, arguments)
            if not isinstance(result, CommandResult):
                raise TypeError(
                    f"Command {' '.join(node.path)} did not return "
                    "CommandResult"
                )
        except KeyboardInterrupt:
            context.cancellation.cancel()
            context.console.diagnostic(Diagnostic(
                level=DiagnosticLevel.WARNING,
                code="COMMAND_INTERRUPTED",
                message="Command interrupted by user",
            ))
            return 130
        except Exception as exc:
            details = (traceback.format_exc(),) if context.verbose else ()
            context.console.diagnostic(Diagnostic(
                level=DiagnosticLevel.ERROR,
                code="UNEXPECTED_ERROR",
                message=str(exc) or exc.__class__.__name__,
                hint="Place --verbose before the command to show the traceback",
                details=details,
            ))
            return 1

        self._render_result(context, result)
        return result.exit_code

    @staticmethod
    def _render_result(context: CommandContext, result: CommandResult) -> None:
        """Render one normalized command result."""
        context.console.diagnostics(result.diagnostics)
        if result.message:
            if result.exit_code == 0:
                context.console.write(result.message)
            elif not result.diagnostics:
                context.console.error(result.message)
