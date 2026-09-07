#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC command - Compile `.hts`/`.htsi` source files into C header output.

Runs the HTC preprocessing, lexing, parsing, semantic validation, and backend
macro emission pipeline for one input source file.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from argparse import ArgumentParser, Namespace
from pathlib import Path

from framework.command import Command, CommandResult, CommandSpec
from framework.completion import DIRECTORY_COMPLETION, FILE_COMPLETION
from framework.completion import attach_path_completion
from framework.context import CommandContext
from .common import Diagnostics, ErrorCode
from .compiler import CompileOptions, HTCCompiler


class HTCCommand(Command):
    """Built-in command for compiling `.hts`/`.htsi` files."""

    DEFAULT_OUTPUT_PATH = "./include/mrX/mr_hardgen.h"

    spec = CommandSpec(
        name="htc",
        description="Compile a `.hts`/`.htsi` file into a `.h` header",
    )

    def configure(self, parser: ArgumentParser) -> None:
        """Declare HTC command arguments."""
        attach_path_completion(parser.add_argument(
            "-i",
            "--input",
            required=True,
            help="Input '.hts' or '.htsi' file path",
        ), FILE_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-o",
            "--output",
            default=None,
            help=f"Output '.h' file path (default: {self.DEFAULT_OUTPUT_PATH})",
        ), FILE_COMPLETION)
        attach_path_completion(parser.add_argument(
            "-I",
            dest="include_dirs",
            action="append",
            default=[],
            help="Additional include search path (repeatable)",
        ), DIRECTORY_COMPLETION)
        parser.add_argument(
            "--no-cpp",
            action="store_true",
            help=("Use the built-in preprocessor instead of an external "
                  "C preprocessor"),
        )
        parser.add_argument(
            "--strict",
            action="store_true",
            help="Warn on HTC extension syntax that is not valid Linux DTS",
        )

    def execute(self, context: CommandContext,
                arguments: Namespace) -> CommandResult:
        """Execute the HTC compilation workflow."""
        input_path = context.resolve_path(arguments.input)
        if not input_path.exists():
            return CommandResult.failure(
                f"Input file not found: {arguments.input}")

        output_path = context.resolve_optional_path(
            arguments.output, self.DEFAULT_OUTPUT_PATH)
        include_dirs = tuple(
            str(context.resolve_path(path))
            for path in (arguments.include_dirs or ())
        )

        context.console.write(f"Parsing: {input_path}")
        result = HTCCompiler().compile(
            CompileOptions(
                input_path,
                output_path,
                include_dirs=include_dirs,
                use_cpp=not arguments.no_cpp,
                strict=bool(arguments.strict),
            ))
        if result.ok and result.header_text is not None:
            if self._write_output(output_path, result.header_text,
                                  result.diagnostics):
                self._print_diagnostics(context, result.diagnostics)
                context.console.write(f"Build: {output_path}")
                return CommandResult.success()

        self._print_diagnostics(context, result.diagnostics)
        return CommandResult(exit_code=1)

    @staticmethod
    def _print_diagnostics(context: CommandContext,
                           diagnostics: Diagnostics) -> None:
        """Write collected compiler diagnostics when any are available."""
        if diagnostics.diagnostics:
            context.console.write(diagnostics.format_all())

    @staticmethod
    def _write_output(
            output_path: Path,
            header_text: str,
            diagnostics: Diagnostics,
    ) -> bool:
        """Write compiled header text to the requested output path."""
        try:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_text(header_text, encoding="utf-8")
        except OSError as exc:
            diagnostics.error(
                ErrorCode.FILE_WRITE_ERROR,
                f"failed to write output file: '{output_path}'",
            ).add_note(str(exc))
            return False
        return True
