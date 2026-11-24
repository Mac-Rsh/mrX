#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Preprocessor - Include and macro preprocessing for Hardware Tree Compiler.

Resolves include directives, tracks conditional compilation state, and performs
simple macro expansion while preserving source line structure.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import CompilationUnit, Diagnostics, ErrorCode, IncludeSite, \
    SourceFile
from .conditional_state import ConditionalState
from .macro_expander import MacroExpander
from .resolver import SourceResolver
from .directives import (
    ConditionalDirective,
    DefineDirective,
    DirectiveParser,
    IncludeDirective,
    UndefDirective,
    UnsupportedDirective,
)
from pathlib import Path


class Preprocessor:
    """Builds a preprocessed compilation unit from HTC source files."""

    NEWLINE_CRLF = "\r\n"
    NEWLINE_LF = "\n"

    def __init__(self, include_dirs: list[str],
                 diagnostics: Diagnostics) -> None:
        """Initialize include search paths and shared diagnostics."""
        self.include_dirs = [Path(directory).resolve() for directory in
                             include_dirs]
        self.diagnostics = diagnostics
        self.directive_parser = DirectiveParser()
        self.resolver = SourceResolver(include_dirs, diagnostics)
        self.macro_expander = MacroExpander(diagnostics)

    def process(self, entry_file: str) -> CompilationUnit:
        """Process an entry file and return its flattened compilation unit."""
        entry_path = str(Path(entry_file).resolve())
        output_lines: list[str] = []
        line_map: list[tuple[str, int]] = []
        seen_paths: set[str] = set()
        self._scan(entry_path, output_lines, line_map, seen_paths,
                   active_stack=[], macros={})
        source_file = SourceFile(
            path=entry_path,
            text="".join(output_lines),
            line_map=line_map,
        )
        return CompilationUnit(entry_path=entry_path, files=[source_file])

    def _scan(
            self,
            file_path: str,
            output_lines: list[str],
            line_map: list[tuple[str, int]],
            seen_paths: set[str],
            active_stack: list[str],
            macros: dict[str, str],
            included_from: IncludeSite | None = None,
    ) -> None:
        """Scan one file, apply directives, and inline it into the output."""
        # Resolve the input path first so all later bookkeeping uses one form
        resolved_path = self.resolver.require_file(file_path)
        if resolved_path is None:
            return

        normalized_path = str(resolved_path)

        # Circular include detection runs before file IO
        if self._has_circular_include(normalized_path, active_stack,
                                      resolved_path.name, included_from):
            return

        if normalized_path in seen_paths:
            return
        seen_paths.add(normalized_path)

        # Read source text before scanning physical lines
        raw_text = self._read_source_text(resolved_path, included_from)
        if raw_text is None:
            return

        # Register original source text for accurate diagnostic previews
        self.diagnostics.register_source(normalized_path, raw_text)

        self._scan_text(normalized_path, resolved_path.parent, raw_text,
                        output_lines, line_map, seen_paths, active_stack,
                        macros)

    def _scan_text(
            self,
            normalized_path: str,
            current_dir: Path,
            raw_text: str,
            output_lines: list[str],
            line_map: list[tuple[str, int]],
            seen_paths: set[str],
            active_stack: list[str],
            macros: dict[str, str],
    ) -> None:
        """Scan source text and inline expanded lines with line mapping."""
        conditional_stack = ConditionalState(self.diagnostics)
        next_stack = [*active_stack, normalized_path]

        for line_number, line in enumerate(raw_text.splitlines(keepends=True),
                                           start=1):
            self._scan_line(line, line_number, normalized_path, current_dir,
                            output_lines, line_map, seen_paths,
                            conditional_stack, next_stack, macros)

        conditional_stack.report_unterminated()

    def _scan_line(
            self,
            line: str,
            line_number: int,
            file_path: str,
            current_dir: Path,
            output_lines: list[str],
            line_map: list[tuple[str, int]],
            seen_paths: set[str],
            conditional_stack: ConditionalState,
            active_stack: list[str],
            macros: dict[str, str],
    ) -> None:
        """Process one physical source line during preprocessing."""
        directive = self.directive_parser.parse(line)

        # Conditional directives update nesting state even in skipped regions
        if isinstance(directive, ConditionalDirective):
            self._handle_conditional_directive(
                directive,
                macros,
                conditional_stack,
                file_path,
                line_number,
                line,
            )
            return

        # Skip normal work when the current branch is inactive
        if conditional_stack.is_skipping():
            return

        # Macro definitions mutate preprocessor state
        if isinstance(directive, DefineDirective):
            macros[directive.name] = directive.value
            return

        # Undefinitions remove a name from the macro table
        if isinstance(directive, UndefDirective):
            macros.pop(directive.name, None)
            return

        # Unsupported directives emit diagnostics
        if isinstance(directive, UnsupportedDirective):
            self._report_line_error(
                ErrorCode.INCLUDE_ERROR,
                f"unsupported preprocessor directive: #{directive.name}",
                file_path,
                line_number,
                self._column_of(line, directive.name),
            )
            return

        # Includes inline the target file at this exact point in the stream
        if isinstance(directive, IncludeDirective):
            self._scan_include(directive, file_path, line_number, current_dir,
                               output_lines, line_map, seen_paths,
                               active_stack, macros, line)
            return

        # Ordinary source lines participate in macro expansion and line mapping
        expanded = self.macro_expander.expand_line(
            line, macros, file_path=file_path, line_number=line_number)
        output_lines.append(expanded.rstrip("\r\n") + "\n")
        line_map.append((file_path, line_number))

    def _has_circular_include(self, normalized_path: str,
                              active_stack: list[str], file_name: str,
                              included_from: IncludeSite | None) -> bool:
        """Return True after reporting a circular include when one exists."""
        if normalized_path not in active_stack:
            return False
        self.diagnostics.error(
            ErrorCode.CIRCULAR_INCLUDE,
            f"circular include detected: '{file_name}'",
        ).with_span_site(included_from)
        return True

    def _read_source_text(self, resolved_path: Path,
                          included_from: IncludeSite | None) -> str | None:
        """Read one resolved source file as UTF-8 text."""
        try:
            return resolved_path.read_text(encoding="utf-8")
        except OSError as exc:
            self.diagnostics.error(
                ErrorCode.FILE_READ_ERROR,
                f"failed to read file '{resolved_path}': {exc}",
            ).with_span_site(included_from)
            return None

    def _scan_include(
            self,
            directive: IncludeDirective,
            file_path: str,
            line_number: int,
            current_dir: Path,
            output_lines: list[str],
            line_map: list[tuple[str, int]],
            seen_paths: set[str],
            active_stack: list[str],
            macros: dict[str, str],
            line: str,
    ) -> None:
        """Resolve and recursively scan one include directive."""
        path_column = self._column_of(line, directive.path)
        if directive.path.startswith("/"):
            self._report_line_error(
                ErrorCode.INCLUDE_ERROR,
                f"absolute includes are not allowed: '{directive.path}'",
                file_path,
                line_number,
                path_column,
            )
            return
        if ".." in Path(directive.path).parts:
            self._report_line_error(
                ErrorCode.INCLUDE_ERROR,
                f"path traversal detected in include: '{directive.path}'",
                file_path,
                line_number,
                path_column,
            )
            return

        # Quote includes search the current directory first, then include dirs
        child_path = self.resolver.resolve_include(directive.path, current_dir,
                                                   directive.kind)
        if child_path is None:
            self._report_line_error(
                ErrorCode.FILE_NOT_FOUND,
                f"include file not found: '{directive.path}'",
                file_path,
                line_number,
                path_column,
            )
            return
        self._scan(
            str(child_path),
            output_lines,
            line_map,
            seen_paths,
            active_stack=active_stack,
            macros=macros,
            included_from=IncludeSite(file_path=file_path, line=line_number,
                                      column=path_column),
        )

    def _report_line_error(self, code: ErrorCode, message: str,
                           file_path: str, line_number: int,
                           column: int = 1) -> None:
        """Emit one preprocessor error attached to a source line."""
        self.diagnostics.error(code, message).with_span(file_path, line_number,
                                                        column)

    @staticmethod
    def _column_of(line: str, text: str | None,
                   fallback: int | None = None) -> int:
        """Return a one-based column for text in line with a useful fallback."""
        if text:
            index = line.find(text)
            if index >= 0:
                return index + 1
        if fallback is not None:
            return fallback
        stripped = line.lstrip(" \t")
        if not stripped:
            return 1
        return len(line) - len(stripped) + 1

    @staticmethod
    def _preserve_newline(filtered_lines: list[str], line: str) -> None:
        """Preserve only the original line ending for directive lines."""
        if line.endswith(Preprocessor.NEWLINE_CRLF):
            filtered_lines.append(Preprocessor.NEWLINE_CRLF)
        elif line.endswith(Preprocessor.NEWLINE_LF):
            filtered_lines.append(Preprocessor.NEWLINE_LF)

    def _handle_conditional_directive(
            self,
            directive: ConditionalDirective,
            macros: dict[str, str],
            conditional_stack: ConditionalState,
            file_path: str,
            line_number: int,
            line: str,
    ) -> None:
        """Update conditional compilation state for a directive line."""
        conditional_stack.handle(directive, macros, file_path, line_number,
                                 line)
