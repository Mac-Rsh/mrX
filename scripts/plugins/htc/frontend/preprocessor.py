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
from .directives import (
    ConditionalDirective,
    DefineDirective,
    DirectiveParser,
    IncludeDirective,
    UndefDirective,
    UnsupportedDirective,
)
from .conditions import ConditionEvaluator
from dataclasses import dataclass
from pathlib import Path


@dataclass
class ConditionalFrame:
    """Tracks active state for one nested conditional compilation block."""

    parent_active: bool
    current_active: bool
    branch_taken: bool
    file_path: str
    line: int
    column: int
    saw_else: bool = False


class Preprocessor:
    """Builds a preprocessed compilation unit from HTC source files."""

    NEWLINE_CRLF = "\r\n"
    NEWLINE_LF = "\n"
    DIRECTIVE_ELIF = DirectiveParser.DIRECTIVE_ELIF
    DIRECTIVE_ELSE = DirectiveParser.DIRECTIVE_ELSE
    DIRECTIVE_ENDIF = DirectiveParser.DIRECTIVE_ENDIF
    DIRECTIVE_IF = DirectiveParser.DIRECTIVE_IF
    DIRECTIVE_IFDEF = DirectiveParser.DIRECTIVE_IFDEF
    DIRECTIVE_IFNDEF = DirectiveParser.DIRECTIVE_IFNDEF
    INCLUDE_KIND_QUOTE = DirectiveParser.INCLUDE_KIND_QUOTE
    MAX_MACRO_EXPANSION_DEPTH = 16

    def __init__(self, include_dirs: list[str],
                 diagnostics: Diagnostics) -> None:
        """Initialize include search paths and shared diagnostics."""
        self.include_dirs = [Path(directory).resolve() for directory in
                             include_dirs]
        self.diagnostics = diagnostics
        self.directive_parser = DirectiveParser()
        self.condition_evaluator = ConditionEvaluator()

    def process(self, entry_file: str) -> CompilationUnit:
        """Process an entry file and return its compilation unit."""
        entry_path = str(Path(entry_file).resolve())
        unit = CompilationUnit(entry_path=entry_path)
        self._scan(entry_path, unit, active_stack=[], macros={})
        return unit

    def _scan(
            self,
            file_path: str,
            unit: CompilationUnit,
            active_stack: list[str],
            macros: dict[str, str],
            included_from: IncludeSite | None = None,
    ) -> None:
        """Scan one file, apply directives, and append it to the unit."""
        # Resolve the input path first so all later bookkeeping uses one form
        resolved_path = self._require_file(file_path)
        if resolved_path is None:
            return

        normalized_path = str(resolved_path)

        # Circular include detection runs before file IO
        if self._has_circular_include(normalized_path, active_stack,
                                      resolved_path.name, included_from):
            return

        unit.include_graph.setdefault(normalized_path, [])

        # Read source text before scanning physical lines
        raw_text = self._read_source_text(resolved_path, included_from)
        if raw_text is None:
            return

        self._scan_text(normalized_path, resolved_path.parent, raw_text, unit,
                        active_stack, macros, included_from)

    def _scan_text(
            self,
            normalized_path: str,
            current_dir: Path,
            raw_text: str,
            unit: CompilationUnit,
            active_stack: list[str],
            macros: dict[str, str],
            included_from: IncludeSite | None,
    ) -> None:
        """Scan resolved source text and append the filtered file to the unit."""
        # Keep the file slot stable so include order is preserved in the unit
        filtered_lines: list[str] = []
        conditional_stack: list[ConditionalFrame] = []
        unit.append(SourceFile(path=normalized_path, text="",
                               included_from=included_from))
        source_index = len(unit.files) - 1
        next_stack = [*active_stack, normalized_path]

        for line_number, line in enumerate(raw_text.splitlines(keepends=True),
                                           start=1):
            self._scan_line(line, line_number, normalized_path, current_dir,
                            filtered_lines, conditional_stack, next_stack,
                            unit, macros)

        unit.files[source_index] = SourceFile(
            path=normalized_path,
            text="".join(filtered_lines),
            included_from=included_from,
        )
        self._report_unterminated_conditionals(normalized_path, raw_text,
                                               conditional_stack)

    def _scan_line(
            self,
            line: str,
            line_number: int,
            file_path: str,
            current_dir: Path,
            filtered_lines: list[str],
            conditional_stack: list[ConditionalFrame],
            active_stack: list[str],
            unit: CompilationUnit,
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
            self._preserve_newline(filtered_lines, line)
            return

        # Skip normal work when the current branch is inactive
        if self._is_skipping(conditional_stack):
            self._preserve_newline(filtered_lines, line)
            return

        # Macro definitions mutate preprocessor state but preserve line layout
        if isinstance(directive, DefineDirective):
            macros[directive.name] = directive.value
            self._preserve_newline(filtered_lines, line)
            return

        # Undefinitions remove a name from the macro table
        if isinstance(directive, UndefDirective):
            macros.pop(directive.name, None)
            self._preserve_newline(filtered_lines, line)
            return

        # Unsupported directives still preserve original line endings
        if isinstance(directive, UnsupportedDirective):
            self._report_line_error(
                ErrorCode.INCLUDE_ERROR,
                f"unsupported preprocessor directive: #{directive.name}",
                file_path,
                line_number,
                self._column_of(line, directive.name),
            )
            self._preserve_newline(filtered_lines, line)
            return

        # Includes recurse into additional files and record graph edges
        if isinstance(directive, IncludeDirective):
            self._preserve_newline(filtered_lines, line)
            self._scan_include(directive, file_path, line_number, current_dir,
                               unit, active_stack, macros, line)
            return

        # Ordinary source lines only participate in macro expansion
        filtered_lines.append(
            self._expand_macros_in_line(line, macros, file_path=file_path,
                                        line_number=line_number))

    def _require_file(self, file_path: str) -> Path | None:
        """Resolve and validate one source file path."""
        resolved_path = self._resolve_file(Path(file_path))
        if resolved_path is not None:
            return resolved_path
        self.diagnostics.error(ErrorCode.FILE_NOT_FOUND,
                               f"file not found: '{file_path}'")
        return None

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
            unit: CompilationUnit,
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
        child_path = self._resolve_include(directive.path, current_dir,
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
            unit,
            active_stack=active_stack,
            macros=macros,
            included_from=IncludeSite(file_path=file_path, line=line_number,
                                      column=path_column),
        )
        unit.add_edge(file_path, str(child_path))

    def _report_unterminated_conditionals(
            self,
            _file_path: str,
            _raw_text: str,
            conditional_stack: list[ConditionalFrame],
    ) -> None:
        """Report an unterminated conditional block when nesting remains."""
        if not conditional_stack:
            return
        frame = conditional_stack[-1]
        self._report_line_error(
            ErrorCode.INCLUDE_ERROR,
            "unterminated conditional directive block",
            frame.file_path,
            frame.line,
            frame.column,
        )

    def _report_line_error(self, code: ErrorCode, message: str,
                           file_path: str, line_number: int,
                           column: int = 1) -> None:
        """Emit one preprocessor error attached to a source line."""
        self.diagnostics.error(code, message).with_span(file_path, line_number,
                                                        column)

    @classmethod
    def _directive_column(cls, line: str) -> int:
        """Return the source column for a preprocessor directive marker."""
        return cls._column_of(line, DirectiveParser.DIRECTIVE_PREFIX)

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
    def _resolve_file(path: Path) -> Path | None:
        """Resolve a path and ensure it points to a readable file."""
        try:
            resolved = path.resolve()
        except OSError:
            return None
        return resolved if resolved.is_file() else None

    @staticmethod
    def _preserve_newline(filtered_lines: list[str], line: str) -> None:
        """Preserve only the original line ending for directive lines."""
        if line.endswith(Preprocessor.NEWLINE_CRLF):
            filtered_lines.append(Preprocessor.NEWLINE_CRLF)
        elif line.endswith(Preprocessor.NEWLINE_LF):
            filtered_lines.append(Preprocessor.NEWLINE_LF)

    @staticmethod
    def _is_skipping(conditional_stack: list[ConditionalFrame]) -> bool:
        """Return True when the current conditional stack disables output."""
        return any(not frame.current_active for frame in conditional_stack)

    def _handle_conditional_directive(
            self,
            directive: ConditionalDirective,
            macros: dict[str, str],
            conditional_stack: list[ConditionalFrame],
            file_path: str,
            line_number: int,
            line: str,
    ) -> None:
        """Update conditional compilation state for a directive line."""
        parent_active = all(frame.current_active for frame in conditional_stack)
        expression = directive.expression or self.condition_evaluator.DEFAULT_EXPR
        directive_column = self._directive_column(line)
        expression_column = self._column_of(line, expression, directive_column)

        # Opening directives push a new nesting frame
        if directive.kind == self.DIRECTIVE_IFDEF:
            self._push_conditional_frame(conditional_stack, parent_active,
                                         directive.expression in macros,
                                         file_path, line_number,
                                         expression_column)
            return
        if directive.kind == self.DIRECTIVE_IFNDEF:
            self._push_conditional_frame(conditional_stack, parent_active,
                                         directive.expression not in macros,
                                         file_path, line_number,
                                         expression_column)
            return
        if directive.kind == self.DIRECTIVE_IF:
            matched = self._evaluate_condition(expression, macros, file_path,
                                               line_number, expression_column)
            self._push_conditional_frame(conditional_stack, parent_active,
                                         matched, file_path, line_number,
                                         expression_column)
            return

        # Mid-block directives mutate the current frame in place
        if directive.kind == self.DIRECTIVE_ELIF:
            if not conditional_stack:
                self._report_unexpected_conditional(
                    self.DIRECTIVE_ELIF,
                    file_path,
                    line_number,
                    directive_column,
                )
                return
            frame = conditional_stack[-1]
            if frame.saw_else:
                self._report_line_error(
                    ErrorCode.INCLUDE_ERROR,
                    f"#{self.DIRECTIVE_ELIF} cannot appear after #{self.DIRECTIVE_ELSE}",
                    file_path,
                    line_number,
                    directive_column,
                )
                return
            matched = self._evaluate_condition(expression, macros, file_path,
                                               line_number, expression_column)
            frame.current_active = frame.parent_active and (
                not frame.branch_taken) and matched
            frame.branch_taken = frame.branch_taken or matched
            return
        if directive.kind == self.DIRECTIVE_ELSE:
            if not conditional_stack:
                self._report_unexpected_conditional(
                    self.DIRECTIVE_ELSE,
                    file_path,
                    line_number,
                    directive_column,
                )
                return
            frame = conditional_stack[-1]
            if frame.saw_else:
                self._report_line_error(
                    ErrorCode.INCLUDE_ERROR,
                    f"duplicate #{self.DIRECTIVE_ELSE} in conditional block",
                    file_path,
                    line_number,
                    directive_column,
                )
                return
            frame.saw_else = True
            frame.current_active = frame.parent_active and not frame.branch_taken
            frame.branch_taken = True
            return

        # Closing directives pop one frame when possible
        if directive.kind == self.DIRECTIVE_ENDIF:
            if conditional_stack:
                conditional_stack.pop()
                return
            self._report_unexpected_conditional(
                self.DIRECTIVE_ENDIF,
                file_path,
                line_number,
                directive_column,
            )

    def _report_unexpected_conditional(self, directive_name: str,
                                       file_path: str,
                                       line_number: int,
                                       column: int) -> None:
        """Report a conditional directive that appears without a matching block."""
        self._report_line_error(
            ErrorCode.INCLUDE_ERROR,
            f"unexpected #{directive_name} without matching conditional block",
            file_path,
            line_number,
            column,
        )

    @staticmethod
    def _push_conditional_frame(
            conditional_stack: list[ConditionalFrame],
            parent_active: bool,
            matched: bool,
            file_path: str,
            line_number: int,
            column: int,
    ) -> None:
        """Push a new conditional frame onto the nesting stack."""
        conditional_stack.append(
            ConditionalFrame(
                parent_active=parent_active,
                current_active=parent_active and matched,
                branch_taken=matched,
                file_path=file_path,
                line=line_number,
                column=column,
            )
        )

    def _evaluate_condition(
            self,
            expression: str,
            macros: dict[str, str],
            file_path: str,
            line_number: int,
            column: int,
    ) -> bool:
        """Evaluate a `#if` or `#elif` expression against current macros."""
        try:
            return self.condition_evaluator.evaluate(expression, macros)
        except ValueError as exc:
            self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                f"invalid conditional expression: {expression}",
            ).with_span(file_path, line_number, column).add_note(str(exc))
            return False

    def _expand_macros_in_line(
            self,
            line: str,
            macros: dict[str, str],
            file_path: str | None = None,
            line_number: int | None = None,
            column: int | None = None,
            depth: int = 0,
    ) -> str:
        """Expand simple object-like macros in a normal source line."""
        result: list[str] = []
        index = 0
        in_string = False
        while index < len(line):
            char = line[index]

            # String literals are copied through without macro expansion
            if char == '"':
                in_string = not in_string
                result.append(char)
                index += 1
                continue

            if in_string and char == "\\" and index + 1 < len(line):
                result.append(line[index:index + 2])
                index += 2
                continue

            # Line comments terminate expansion for the remainder of the line
            if not in_string and line.startswith("//", index):
                result.append(line[index:])
                break

            # Identifiers are the only expandable macro candidates
            if not in_string and self._is_identifier_start(char):
                end = index + 1
                while end < len(line) and self._is_identifier_char(line[end]):
                    end += 1
                name = line[index:end]
                macro_column = column if column is not None else index + 1
                result.append(
                    self._expand_macro_name(name, macros, file_path,
                                            line_number, macro_column, depth))
                index = end
                continue
            result.append(char)
            index += 1
        return "".join(result)

    def _expand_macro_name(self, name: str, macros: dict[str, str],
                           file_path: str | None, line_number: int | None,
                           column: int | None, depth: int) -> str:
        """Expand a single macro name with shallow recursion control."""
        if name not in macros:
            return name
        if depth >= self.MAX_MACRO_EXPANSION_DEPTH:
            diagnostic = self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                f"macro expansion depth exceeded near '{name}'",
            )
            if file_path is not None and line_number is not None:
                diagnostic.with_span(file_path, line_number, column or 1)
            return name
        value = macros[name]
        if not value:
            return ""
        return self._expand_macros_in_line(value, macros, file_path,
                                           line_number, column, depth + 1)

    @staticmethod
    def _is_identifier_start(char: str) -> bool:
        """Return True when a character can start an identifier."""
        return char.isalpha() or char == "_"

    @staticmethod
    def _is_identifier_char(char: str) -> bool:
        """Return True when a character can continue an identifier."""
        return char.isalnum() or char == "_"

    def _resolve_include(self, raw_path: str, current_dir: Path,
                         include_kind: str) -> Path | None:
        """Resolve an include path according to quote or angle search rules."""
        # Absolute paths are intentionally rejected
        if raw_path.startswith("/"):
            self.diagnostics.error(ErrorCode.INCLUDE_ERROR,
                                   f"absolute includes are not allowed: '{raw_path}'")
            return None

        # Reject parent traversal before searching candidate directories
        if ".." in Path(raw_path).parts:
            self.diagnostics.error(ErrorCode.INCLUDE_ERROR,
                                   f"path traversal detected in include: '{raw_path}'")
            return None

        # Quote includes look in the current directory before shared include dirs
        candidates: list[Path] = []
        if include_kind == self.INCLUDE_KIND_QUOTE:
            candidates.append(current_dir / raw_path)
        candidates.extend(
            directory / raw_path for directory in self.include_dirs)
        for candidate in candidates:
            resolved = self._resolve_file(candidate)
            if resolved is not None:
                return resolved
        return None
