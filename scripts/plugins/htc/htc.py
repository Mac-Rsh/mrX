#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC command - Compile `.hts` source files into C header output.

Runs the HTC preprocessing, lexing, parsing, semantic validation, and backend
macro emission pipeline for one input source file.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .common import CompilationUnit, Diagnostics, ErrorCode, RootNode
from .frontend import Lexer, Parser, Preprocessor
from argparse import ArgumentParser, Namespace
from dock import PluginSpec, export_plugin
from .validation import TreeValidator
from .semantic import SemanticBuilder
from .backend import MacroBuilder
from datetime import datetime
from pathlib import Path


@export_plugin
class HTCCommand:
    """Built-in command for compiling `.hts` files."""

    DEFAULT_OUTPUT_PATH = "./include/mrX/mr_hardgen.h"

    spec = PluginSpec(
        name="htc",
        description="Compile a `.hts` file into a `.h` header.",
    )

    def help(self) -> str:
        """Return detailed help text for the HTC command.

        Returns:
            Human-readable help text for the command.
        """
        return "\n".join([
            "Usage: python ./scripts/cli.py htc -i <input.hts> [-o <output.h>]",
            "Description: Compile a `.hts` file into a `.h` header.",
            "Aliases: (none)",
            "Examples:",
            "- python ./scripts/cli.py htc -i input.hts",
            "- python ./scripts/cli.py htc -i input.hts -o output.h",
            f"Default output: {self.DEFAULT_OUTPUT_PATH}",
        ])

    def run(self, args: list[str]) -> int:
        """Execute the HTC compilation workflow.

        Args:
            args: Command line arguments passed to the command.

        Returns:
            Exit code (0 for success, non-zero for error).
        """
        # Parse command line arguments
        options = self._parse_args(args)
        if options is None:
            return 1

        # Resolve input and output paths from parsed arguments
        paths = self._parse_paths(options)
        if paths is None:
            # Print a short usage hint when required paths are invalid
            self._print_usage()
            return 1
        input_path, output_path = paths

        # Collect diagnostics across the whole compilation pipeline
        diagnostics = Diagnostics()

        # Preprocess the source file and all included files first
        unit = self._preprocess(input_path, diagnostics)
        if unit is None:
            # Preprocessing already reported the exact failure details
            self._print_diagnostics(diagnostics)
            return 1

        # Parse every preprocessed source file into syntax trees
        roots = self._parse_roots(unit, diagnostics)
        if roots is None:
            # Parsing errors are accumulated in the shared diagnostics sink
            self._print_diagnostics(diagnostics)
            return 1

        # Build the semantic tree from all parsed roots
        tree = SemanticBuilder(diagnostics).build(roots)

        # Validate references, aliases, and tree consistency rules
        TreeValidator(diagnostics).validate(tree)

        # Print diagnostics before deciding whether code generation can continue
        self._print_diagnostics(diagnostics)
        if diagnostics.has_errors():
            return 1

        # Render backend macros and wrap them as a complete header file
        header_text = self._wrap_output(
            output_path,
            MacroBuilder.render(MacroBuilder().build(tree)),
        )

        # Write the generated header to the final output location
        if not self._write_output(output_path, header_text, diagnostics):
            # File system write errors are reported through diagnostics
            self._print_diagnostics(diagnostics)
            return 1

        # Report the generated output file path on success
        print(f"Build: {output_path}")
        return 0

    @classmethod
    def _build_parser(cls) -> ArgumentParser:
        """Build the HTC command line argument parser.

        Returns:
            Configured argument parser for the HTC command.
        """
        parser = ArgumentParser(
            prog="python ./scripts/cli.py htc",
            add_help=True,
        )
        parser.add_argument(
            "-i",
            "--input",
            required=True,
            help="Input '.hts' file path",
        )
        parser.add_argument(
            "-o",
            "--output",
            help=f"Output '.h' file path (default: {cls.DEFAULT_OUTPUT_PATH})",
        )
        return parser

    @classmethod
    def _parse_args(cls, args: list[str]) -> Namespace | None:
        """Parse HTC command line arguments.

        Args:
            args: Raw command line arguments passed to the command.

        Returns:
            Parsed argument namespace, or `None` when parsing fails.
        """
        # Reuse the shared parser definition so help and runtime stay aligned
        parser = cls._build_parser()

        try:
            return parser.parse_args(args)
        except SystemExit:
            # Argparse already printed the help or parse error message
            return None

    @staticmethod
    def _print_usage() -> None:
        """Print short HTC command usage guidance."""
        print(
            "Usage: python ./scripts/cli.py htc -i <input.hts> [-o <output.h>]")

    @classmethod
    def _parse_paths(cls, args: Namespace) -> tuple[Path, Path] | None:
        """Parse input and output paths for one compilation.

        Args:
            args: Parsed HTC command line arguments.

        Returns:
            Normalized input and output paths, or `None` when input is absent.
        """
        # Resolve the input source file path first
        input_path = Path(args.input).resolve()
        if not input_path.exists():
            print(f"Input file not found: {args.input}")
            return None

        # Use the provided output path or fall back to the default header path
        output_path = Path(args.output or cls.DEFAULT_OUTPUT_PATH).resolve()

        return input_path, output_path

    @staticmethod
    def _print_diagnostics(diagnostics: Diagnostics) -> None:
        """Print collected diagnostics when any are available.

        Args:
            diagnostics: Shared diagnostic sink.
        """
        # Only print when at least one diagnostic was collected
        if diagnostics.diagnostics:
            print(diagnostics.format_all())

    @classmethod
    def _wrap_output(cls, output_path: Path, header_text: str) -> str:
        """Wrap rendered macros with a generated header file scaffold.

        Args:
            output_path: Resolved output header file path.
            header_text: Rendered backend macro text.

        Returns:
            Final header file text with include guards and timestamp.
        """
        # Derive the include guard from the target output file name
        guard = cls._build_header_guard(output_path)

        # Stamp the generated file with the local generation time
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Build the header prologue first
        lines = [
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            f"/* Generated at: {timestamp} */",
            "",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif /* __cplusplus */",
            "",
        ]

        # Insert rendered macro content only when the backend produced output
        body = header_text.strip()
        if body:
            lines.extend([body, ""])

        # Finish the epilogue and close the include guard
        lines.extend([
            "#ifdef __cplusplus",
            "}",
            "#endif /* __cplusplus */",
            "",
            f"#endif /* {guard} */",
            "",
        ])
        return "\n".join(lines)

    @staticmethod
    def _build_header_guard(output_path: Path) -> str:
        """Build an include guard macro name from the output file name.

        Args:
            output_path: Resolved output header file path.

        Returns:
            Include guard macro derived from the output file name.
        """
        # Normalize the file name to upper case for the guard macro
        name = output_path.name.upper()
        guard = []

        # Replace non-alphanumeric characters so the result is a valid macro
        for char in name:
            guard.append(char if char.isalnum() else "_")
        return f"__{''.join(guard)}__"

    @staticmethod
    def _preprocess(input_path: Path,
                    diagnostics: Diagnostics) -> CompilationUnit | None:
        """Preprocess one HTC source file into a compilation unit.

        Args:
            input_path: Resolved `.hts` source file path.
            diagnostics: Shared diagnostic sink.

        Returns:
            Compilation unit, or `None` when preprocessing fails.
        """
        # Search includes relative to the input file directory
        preprocessor = Preprocessor([str(input_path.parent)], diagnostics)

        # Preprocess the entry file into a complete compilation unit
        unit = preprocessor.process(str(input_path))
        if diagnostics.has_errors():
            return None

        # Return the collected preprocessed files on success
        return unit

    @staticmethod
    def _parse_roots(unit: CompilationUnit,
                     diagnostics: Diagnostics) -> list[RootNode] | None:
        """Parse all source files from a compilation unit.

        Args:
            unit: Preprocessed compilation unit.
            diagnostics: Shared diagnostic sink.

        Returns:
            Parsed root nodes, or `None` when parsing fails.
        """
        # Collect one parsed root node for each preprocessed source file
        roots: list[RootNode] = []

        # Tokenize and parse every source file in the compilation unit
        for source_file in unit.files:
            print(f"Parsing: {source_file.path}")

            # Convert raw source text into tokens first
            lexer = Lexer(source_file.text, source_file.path, diagnostics)
            tokens = lexer.tokenize()

            # Parse tokens into a syntax tree root
            parser = Parser(tokens, diagnostics)
            roots.append(parser.parse())

        # Stop here when any parse-stage diagnostics were reported
        if diagnostics.has_errors():
            return None

        # Return parsed roots for semantic analysis
        return roots

    @staticmethod
    def _write_output(
            output_path: Path,
            header_text: str,
            diagnostics: Diagnostics,
    ) -> bool:
        """Write compiled header text to the requested output path.

        Args:
            output_path: Resolved output header file path.
            header_text: Rendered header text.
            diagnostics: Shared diagnostic sink.

        Returns:
            `True` when the output file is written successfully, else `False`.
        """
        try:
            # Ensure the target directory exists before writing the file
            output_path.parent.mkdir(parents=True, exist_ok=True)

            # Write the generated header as UTF-8 text
            output_path.write_text(header_text, encoding="utf-8")
        except OSError as exc:
            # Convert file system errors into a structured diagnostic
            diagnostics.error(
                ErrorCode.FILE_WRITE_ERROR,
                f"failed to write output file: '{output_path}'",
            ).add_note(str(exc))
            return False

        return True
