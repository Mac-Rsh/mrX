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
        parser = self._build_parser()
        lines = [
            "Usage: python ./scripts/cli.py htc <input.hts> [output.h]",
            "       python ./scripts/cli.py htc -i <input.hts> [-o <output.h>]",
            "Description: Compile a `.hts` file into a `.h` header.",
            "Aliases: (none)",
            "",
            parser.format_help().rstrip(),
            "Default output: ./include/mrX/mr_hardgen.h",
        ]
        return "\n".join(lines)

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

        # Parse input and output paths
        paths = self._parse_paths(options)
        if paths is None:
            # Missing required input file
            self._print_usage()
            return 1
        input_path, output_path = paths

        # Run compiler pipeline
        diagnostics = Diagnostics()

        # Build preprocessed compilation unit
        unit = self._preprocess(input_path, diagnostics)
        if unit is None:
            self._print_diagnostics(diagnostics)
            return 1

        # Parse preprocessed files into syntax trees
        roots = self._parse_roots(unit, diagnostics)
        if roots is None:
            self._print_diagnostics(diagnostics)
            return 1

        # Build semantic tree from parsed roots
        tree = SemanticBuilder(diagnostics).build(roots)

        # Validate references and aliases
        TreeValidator(diagnostics).validate(tree)

        # Print diagnostics before deciding whether to stop
        self._print_diagnostics(diagnostics)
        if diagnostics.has_errors():
            return 1

        # Render backend header
        header_text = self._wrap_output(
            output_path,
            MacroBuilder.render(MacroBuilder().build(tree)),
        )

        # Write output file
        if not self._write_output(output_path, header_text, diagnostics):
            self._print_diagnostics(diagnostics)
            return 1

        # Report successful compilation
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
            "input_pos",
            nargs="?",
            help="Input '.hts' file path (positional form)",
        )
        parser.add_argument(
            "output_pos",
            nargs="?",
            help="Output '.h' file path (positional form)",
        )
        parser.add_argument(
            "-i",
            "--input",
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
        # Reuse shared parser definition
        parser = cls._build_parser()

        try:
            return parser.parse_args(args)
        except SystemExit:
            # argparse already printed the error or help text
            return None

    @staticmethod
    def _print_usage() -> None:
        """Print short HTC command usage guidance."""
        # Print positional usage form
        print("Usage: python ./scripts/cli.py htc <input.hts> [output.h]")

        # Print flag-based usage form
        print(
            "You can also use: python ./scripts/cli.py htc -i <input.hts> -o <output.h>"
        )

    @classmethod
    def _parse_paths(cls, args: Namespace) -> tuple[Path, Path] | None:
        """Parse input and output paths for one compilation.

        Args:
            args: Parsed HTC command line arguments.

        Returns:
            Normalized input and output paths, or `None` when input is absent.
        """
        # Input file path
        input_arg = cls._select_path_arg(args.input, args.input_pos, "input")
        if input_arg is None:
            return None
        input_path = Path(input_arg).resolve()
        if not input_path.exists():
            print(f"Input file not found: {input_arg}")
            return None

        # Output file path
        output_arg = cls._select_path_arg(args.output, args.output_pos, "output")
        if output_arg is None:
            return None
        output_path = Path(output_arg).resolve()

        return input_path, output_path

    @classmethod
    def _select_path_arg(cls, option: str | None, positional: str | None,
                         name: str) -> str | None:
        """Resolve one path argument from option or positional forms."""
        if option and positional and (option != positional):
            print(
                f"Conflicting {name} paths: option '{option}' and positional '{positional}'"
            )
            return None
        if option:
            return option
        if positional:
            return positional
        if name == "output":
            return cls.DEFAULT_OUTPUT_PATH
        return None

    @staticmethod
    def _print_diagnostics(diagnostics: Diagnostics) -> None:
        """Print collected diagnostics when any are available.

        Args:
            diagnostics: Shared diagnostic sink.
        """
        # Print diagnostics only when something was reported
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
        # Build include guard
        guard = cls._build_header_guard(output_path)

        # Build timestamp
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Build '.h'
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
        body = header_text.strip()
        if body:
            lines.extend([body, ""])
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
        name = output_path.name.upper()
        guard = []
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

        # Preprocess the entry source file
        unit = preprocessor.process(str(input_path))
        if diagnostics.has_errors():
            return None

        # Return collected compilation unit
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
        # Collect one root per preprocessed source file
        roots: list[RootNode] = []

        # Parse each preprocessed source file into a root node
        for source_file in unit.files:
            print(f"Parsing: {source_file.path}")

            # Tokenize source text
            lexer = Lexer(source_file.text, source_file.path, diagnostics)
            tokens = lexer.tokenize()

            # Parse tokens into syntax tree
            parser = Parser(tokens, diagnostics)
            roots.append(parser.parse())

        # Check for any parsing errors
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
            # Ensure parent directory exists before writing
            output_path.parent.mkdir(parents=True, exist_ok=True)

            # Write generated header text as UTF-8
            output_path.write_text(header_text, encoding="utf-8")
        except OSError as exc:
            # Report file write failure through diagnostics
            diagnostics.error(
                ErrorCode.FILE_WRITE_ERROR,
                f"failed to write output file: '{output_path}'",
            ).add_note(str(exc))
            return False

        return True
