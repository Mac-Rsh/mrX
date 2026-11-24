#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Diagnostics - Shared diagnostic reporting utilities.

Defines diagnostic levels, error codes, and formatting helpers used across the
compiler pipeline.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass, field
from enum import Enum

from .source import IncludeSite, SourceLocation, SourceSpan


class Level(Enum):
    """Severity levels for diagnostics."""

    ERROR = "Error"
    WARNING = "Warning"
    NOTE = "Note"


class ErrorCode(Enum):
    """Stable diagnostic codes grouped by compiler stage."""

    UNTERMINATED_STRING = "E0001"
    UNTERMINATED_COMMENT = "E0002"
    INVALID_NUMBER = "E0003"
    INVALID_ESCAPE = "E0004"
    UNKNOWN_TOKEN = "E0005"

    EXPECTED_TOKEN = "E0100"
    UNEXPECTED_EOF = "E0101"
    EXPECTED_IDENTIFIER = "E0102"
    EXPECTED_STRING = "E0103"
    EXPECTED_NUMBER = "E0104"
    EXPECTED_LBRACE = "E0105"
    EXPECTED_RBRACE = "E0106"
    EXPECTED_SEMICOLON = "E0107"
    EXPECTED_ASSIGN = "E0108"
    EXPECTED_SLASH = "E0109"
    EXPECTED_LT = "E0110"
    EXPECTED_GT = "E0111"
    EXPECTED_AMPERSAND = "E0112"
    EXPECTED_AT = "E0113"
    INVALID_NODE_DECL = "E0114"
    INVALID_PROPERTY = "E0115"
    INVALID_ARRAY = "E0116"

    UNDEFINED_LABEL = "E0200"
    DUPLICATE_LABEL = "E0201"
    DUPLICATE_NODE = "E0202"
    INVALID_STATUS = "E0203"
    INVALID_PRIO = "E0204"
    CIRCULAR_REFERENCE = "E0205"
    MISSING_COMPATIBLE = "E0206"
    INVALID_PHANDLE = "E0207"
    UNUSED_LABEL = "E0208"
    DUPLICATE_ALIAS = "E0209"

    FILE_NOT_FOUND = "E0300"
    FILE_READ_ERROR = "E0301"
    FILE_WRITE_ERROR = "E0302"
    INCLUDE_ERROR = "E0303"
    CIRCULAR_INCLUDE = "E0304"

    CHILD_OF_DISABLED_NODE = "E0310"
    REFERENCE_TO_DISABLED = "E0311"
    INVALID_COMPATIBLE_VALUE = "E0312"
    INVALID_ALIAS_VALUE = "E0313"
    INVALID_INLINE_ARRAY = "E0314"
    GENERIC_NOTE = "E0399"


@dataclass
class Diagnostic:
    """Represents one compiler diagnostic with optional span and notes."""

    level: Level
    code: ErrorCode
    message: str
    span: SourceSpan | None = None
    notes: list[str] = field(default_factory=list)

    def with_span(
            self,
            file_path: str,
            line: int,
            column: int,
            end_column: int | None = None,
    ) -> "Diagnostic":
        """Attach a span built from raw location fields."""
        start = SourceLocation(file_path=file_path, line=line, column=column)
        end = None
        if end_column is not None:
            end = SourceLocation(file_path=file_path, line=line,
                                 column=end_column)
        self.span = SourceSpan(start, end)
        return self

    def with_source_span(self, span: SourceSpan) -> "Diagnostic":
        """Attach an existing source span to the diagnostic."""
        self.span = span
        return self

    def with_span_site(self, site: IncludeSite | None) -> "Diagnostic":
        """Attach a span from an include site when available."""
        if site is not None:
            self.with_span(site.file_path, site.line, site.column)
        return self

    def add_note(self, note: str) -> "Diagnostic":
        """Append a supplemental note to the diagnostic."""
        self.notes.append(note)
        return self


class Diagnostics:
    """Collects and formats diagnostics emitted across compiler stages."""

    POINTER_MARKER = "^"

    def __init__(self) -> None:
        """Create an empty diagnostic collection."""
        self.diagnostics: list[Diagnostic] = []
        self.sources: dict[str, str] = {}

    def register_source(self, file_path: str, text: str) -> None:
        """Cache source text for stable diagnostic previews."""
        self.sources[file_path] = text

    def error(self, code: ErrorCode, message: str) -> Diagnostic:
        """Create and store an error diagnostic."""
        return self._add(Level.ERROR, code, message)

    def warning(self, code: ErrorCode, message: str) -> Diagnostic:
        """Create and store a warning diagnostic."""
        return self._add(Level.WARNING, code, message)

    def _add(self, level: Level, code: ErrorCode, message: str) -> Diagnostic:
        """Create, store, and return one diagnostic entry."""
        diagnostic = Diagnostic(level, code, message)
        self.diagnostics.append(diagnostic)
        return diagnostic

    def has_errors(self) -> bool:
        """Return True when at least one error diagnostic exists."""
        return any(
            diagnostic.level == Level.ERROR for diagnostic in self.diagnostics)

    def format_all(self) -> str:
        """Render all collected diagnostics as a single string."""
        return "\n\n".join(
            self._render(diagnostic) for diagnostic in self.diagnostics)

    def _render(self, diagnostic: Diagnostic) -> str:
        """Render one diagnostic using cargo-style formatting."""
        header = (
            f"{diagnostic.level.value}[{diagnostic.code.value}]: "
            f"{diagnostic.message}"
        )
        lines = [header]

        # Inline source preview is optional when a span is available
        if diagnostic.span is not None:
            lines.append(f"   --> {diagnostic.span}")
            lines.extend(self._source_preview(diagnostic.span, self.sources))

        # Notes are rendered after the main message body
        for note in diagnostic.notes:
            lines.append(f"     note: {note}")
        return "\n".join(lines)

    @staticmethod
    def _source_preview(span: SourceSpan, sources: dict[str, str]) -> list[str]:
        """Build inline source preview lines for a diagnostic span."""
        source_line = Diagnostics._load_source_line(span.file_path, span.line,
                                                    sources)
        if source_line is None:
            return []
        line_str = str(span.line)
        gutter_width = max(4, len(line_str))
        gutter_pad = " " * (gutter_width + 1)
        return [
            f"{gutter_pad}|",
            f"{span.line:>{gutter_width}} | {source_line}",
            f"{gutter_pad}| {Diagnostics._pointer_line(source_line, span)}",
        ]

    @staticmethod
    def _pointer_line(source_line: str, span: SourceSpan) -> str:
        """Build the source pointer line preserving tabs and token width."""
        start_col = max(span.column, 1)
        prefix_len = start_col - 1
        prefix_chars: list[str] = []
        for idx in range(prefix_len):
            if idx < len(source_line) and source_line[idx] == "\t":
                prefix_chars.append("\t")
            else:
                prefix_chars.append(" ")
        width = 1
        if (span.end is not None
                and span.end.file_path == span.file_path
                and span.end.line == span.line
                and span.end.column > start_col):
            width = span.end.column - start_col
        return "".join(prefix_chars) + Diagnostics.POINTER_MARKER * width

    @staticmethod
    def _load_source_line(file_path: str, line_number: int,
                          sources: dict[str, str]) -> str | None:
        """Load one source line for inline diagnostic display."""
        cached = sources.get(file_path)
        if cached is not None:
            lines = cached.splitlines()
            if 1 <= line_number <= len(lines):
                return lines[line_number - 1]
            return None
        try:
            with open(file_path, "r", encoding="utf-8") as handle:
                for index, line in enumerate(handle, start=1):
                    if index == line_number:
                        return line.rstrip("\r\n")
        except OSError:
            return None
        return None
