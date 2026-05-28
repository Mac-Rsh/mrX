#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Compiler - Programmatic compilation pipeline.

Provides the top-level compilation pipeline: preprocessing, lexing, parsing,
semantic building, validation, and macro backend emission.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .common import CompilationUnit, Diagnostics, RootNode
from .frontend import Lexer, Parser, Preprocessor
from .semantic import HardTree, SemanticBuilder
from .validation import TreeValidator
from .backend import MacroBuilder
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


@dataclass(frozen=True)
class CompileOptions:
    """Inputs for one HTC compilation."""

    input_path: Path
    output_path: Path


@dataclass(frozen=True)
class CompileResult:
    """Result of one HTC compilation attempt."""

    diagnostics: Diagnostics
    header_text: str | None = None
    unit: CompilationUnit | None = None
    tree: HardTree | None = None

    @property
    def ok(self) -> bool:
        """Return True when compilation produced header text without errors."""
        return self.header_text is not None and not self.diagnostics.has_errors()


class HTCCompiler:
    """Runs preprocessing, parsing, semantic validation, and macro emission."""

    def compile(self, options: CompileOptions) -> CompileResult:
        """Compile one input source into a generated header text."""
        diagnostics = Diagnostics()
        unit = self._preprocess(options.input_path, diagnostics)
        if unit is None:
            return CompileResult(diagnostics=diagnostics)

        roots = self._parse_roots(unit, diagnostics)
        if roots is None:
            return CompileResult(diagnostics=diagnostics, unit=unit)

        tree = SemanticBuilder(diagnostics).build(roots)
        TreeValidator(diagnostics).validate(tree)
        if diagnostics.has_errors():
            return CompileResult(diagnostics=diagnostics, unit=unit, tree=tree)

        body = MacroBuilder.render(MacroBuilder().build(tree))
        header_text = self.wrap_output(options.output_path, body)
        return CompileResult(diagnostics=diagnostics, header_text=header_text,
                             unit=unit, tree=tree)

    @staticmethod
    def _preprocess(input_path: Path,
                    diagnostics: Diagnostics) -> CompilationUnit | None:
        """Preprocess one HTC source file into a compilation unit."""
        preprocessor = Preprocessor([str(input_path.parent)], diagnostics)
        unit = preprocessor.process(str(input_path))
        if diagnostics.has_errors():
            return None
        return unit

    @staticmethod
    def _parse_roots(unit: CompilationUnit,
                     diagnostics: Diagnostics) -> list[RootNode] | None:
        """Parse all source files from a compilation unit."""
        roots: list[RootNode] = []
        for source_file in unit.files:
            print(f"Parsing: {source_file.path}")
            lexer = Lexer(source_file.text, source_file.path, diagnostics)
            tokens = lexer.tokenize()
            parser = Parser(tokens, diagnostics)
            roots.append(parser.parse())
        if diagnostics.has_errors():
            return None
        return roots

    @classmethod
    def wrap_output(cls, output_path: Path, header_text: str) -> str:
        """Wrap rendered macros with a generated header file scaffold."""
        guard = cls.build_header_guard(output_path)
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
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
    def build_header_guard(output_path: Path) -> str:
        """Build an include guard macro name from the output file name."""
        name = output_path.name.upper()
        guard = []
        for char in name:
            guard.append(char if char.isalnum() else "_")
        return f"__{''.join(guard)}__"
