#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Compiler - Programmatic compilation pipeline.

Provides the top-level compilation pipeline: preprocessing, lexing, parsing,
semantic building, validation, and macro backend emission.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from .backend import MacroBuilder
from .common import CompilationUnit, Diagnostics, Document
from .frontend import CppOptions, CppPreprocessor, Lexer, Parser, Preprocessor
from .semantic import HardTree, SemanticBuilder
from .validation import TreeValidator


@dataclass(frozen=True)
class CompileOptions:
    """Inputs for one HTC compilation."""

    input_path: Path
    output_path: Path
    include_dirs: tuple[str, ...] = ()
    use_cpp: bool = True
    strict: bool = False


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
        unit = self._preprocess(options, diagnostics)
        if unit is None:
            return CompileResult(diagnostics=diagnostics)

        document = self._parse_document(unit, diagnostics,
                                        strict=options.strict)
        if document is None:
            return CompileResult(diagnostics=diagnostics, unit=unit)

        tree = SemanticBuilder(diagnostics).build(document)
        TreeValidator(diagnostics).validate(tree)
        if diagnostics.has_errors():
            return CompileResult(diagnostics=diagnostics, unit=unit, tree=tree)

        body = MacroBuilder.render(MacroBuilder().build(tree))
        header_text = self.wrap_output(options.output_path, body)
        return CompileResult(diagnostics=diagnostics, header_text=header_text,
                             unit=unit, tree=tree)

    @staticmethod
    def _parse_document(unit: CompilationUnit,
                        diagnostics: Diagnostics,
                        strict: bool = False) -> Document | None:
        """Parse all preprocessed source files into one DTS document."""
        dts_v1 = False
        roots = []
        memreserves = []
        delete_nodes = []
        for source_file in unit.files:
            lexer = Lexer(source_file.text, source_file.path, diagnostics,
                          source_map=source_file.line_map)
            tokens = lexer.tokenize()
            parser = Parser(tokens, diagnostics, strict=strict)
            parsed = parser.parse()
            dts_v1 = dts_v1 or parsed.dts_v1
            roots.extend(parsed.roots)
            memreserves.extend(parsed.memreserves)
            delete_nodes.extend(parsed.delete_nodes)
        if diagnostics.has_errors():
            return None
        return Document(dts_v1=dts_v1, roots=roots,
                        memreserves=memreserves, delete_nodes=delete_nodes)

    @staticmethod
    def _preprocess(options: CompileOptions,
                    diagnostics: Diagnostics) -> CompilationUnit | None:
        """Preprocess one HTC source file into a compilation unit."""
        if options.use_cpp:
            adapter = CppPreprocessor(diagnostics)
            cpp_options = CppOptions(include_dirs=options.include_dirs)
            unit = adapter.preprocess(str(options.input_path), cpp_options)
            if unit is not None:
                return CompilationUnit(
                    entry_path=str(options.input_path),
                    files=[unit.source_file],
                )
            if diagnostics.has_errors():
                return None

        # Fallback to the built-in limited preprocessor
        preprocessor = Preprocessor(
            [str(options.input_path.parent), *options.include_dirs],
            diagnostics,
        )
        unit = preprocessor.process(str(options.input_path))
        if diagnostics.has_errors():
            return None
        return unit

    @staticmethod
    def build_header_guard(output_path: Path) -> str:
        """Build an include guard macro name from the output file name."""
        name = output_path.name.upper()
        guard = []
        for char in name:
            guard.append(char if char.isalnum() else "_")
        return f"__{''.join(guard)}__"

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
