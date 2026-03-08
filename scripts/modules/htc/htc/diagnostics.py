#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Diagnostics - Error reporting for Hardware Tree Compiler.

Provides cargo-style error formatting.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass, field
from typing import Optional, List
from enum import Enum


class Level(Enum):
    """Diagnostic severity level."""

    ERROR = "error"
    WARNING = "warning"
    NOTE = "note"

    def __str__(self) -> str:
        return self.value


class ErrorCode(Enum):
    """
    Error codes for HTC compiler.

    Code ranges:
    - E0001-E0099: Lexer errors
    - E0100-E0199: Parser errors
    - E0200-E0299: Semantic errors
    - E0300-E0399: IO errors
    - E0310-E0319: Codegen warnings
    """

    # Lexer errors (E0001-E0099)
    UNTERMINATED_STRING = "E0001"
    UNTERMINATED_COMMENT = "E0002"
    INVALID_NUMBER = "E0003"
    INVALID_ESCAPE = "E0004"
    UNKNOWN_TOKEN = "E0005"

    # Parser errors (E0100-E0199)
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

    # Semantic errors (E0200-E0299)
    UNDEFINED_LABEL = "E0200"
    DUPLICATE_LABEL = "E0201"
    DUPLICATE_NODE = "E0202"
    INVALID_STATUS = "E0203"
    INVALID_PRIO = "E0204"
    CIRCULAR_REFERENCE = "E0205"
    MISSING_COMPATIBLE = "E0206"
    INVALID_PHANDLE = "E0207"
    UNUSED_LABEL = "E0208"
    GENERIC_NOTE = "E0299"

    # IO errors (E0300-E0399)
    FILE_NOT_FOUND = "E0300"
    FILE_READ_ERROR = "E0301"
    FILE_WRITE_ERROR = "E0302"
    INCLUDE_ERROR = "E0303"

    # Codegen warnings (E0310-E0319)
    CHILD_OF_DISABLED_NODE = "E0310"
    REFERENCE_TO_DISABLED = "E0311"
    INVALID_COMPATIBLE_VALUE = "E0312"
    INVALID_ALIAS_VALUE = "E0313"


@dataclass
class SourceSpan:
    """
    A span of source code for error reporting.

    Attributes:
        file_path: Path to source file.
        line: Line number (1-based).
        column: Column number (1-based).
        end_column: End column (exclusive).
    """

    file_path: str
    line: int
    column: int
    end_column: Optional[int] = None

    def __str__(self) -> str:
        return f"{self.file_path}:{self.line}:{self.column}"


@dataclass
class Diagnostic:
    """
    A diagnostic message (error, warning, note, or help).

    Attributes:
        level: Severity level.
        code: Error code.
        message: Primary message.
        span: Source location.
        notes: Additional notes.
    """

    level: Level
    code: ErrorCode
    message: str
    span: Optional[SourceSpan] = None
    notes: List[str] = field(default_factory=list)

    def with_span(
            self,
            file_path: str,
            line: int,
            column: int,
            end_column: Optional[int] = None,
    ) -> "Diagnostic":
        """Set the source span."""
        self.span = SourceSpan(file_path, line, column, end_column)
        return self

    def add_note(self, note: str) -> "Diagnostic":
        """Add a note."""
        self.notes.append(note)
        return self


class Diagnostics:
    """
    Collection of diagnostics with cargo-style formatting.

    Output format:
        error[E0001]: expected identifier, found '='
          --> tests/demo.hts:5:10
           |
        5  |     = "value";
           |
    """

    def __init__(self) -> None:
        """Initialize diagnostics collection."""
        self.diagnostics: List[Diagnostic] = []

    def error(
            self,
            code: ErrorCode,
            message: str,
            span: Optional[SourceSpan] = None,
    ) -> Diagnostic:
        """
        Create and add an error diagnostic.

        Args:
            code: Error code.
            message: Error message.
            span: Source location.

        Returns:
            The created diagnostic for chaining.
        """
        diag = Diagnostic(Level.ERROR, code, message, span)
        self.diagnostics.append(diag)
        return diag

    def warning(
            self,
            code: ErrorCode,
            message: str,
            span: Optional[SourceSpan] = None,
    ) -> Diagnostic:
        """
        Create and add a warning diagnostic.

        Args:
            code: Error code.
            message: Warning message.
            span: Source location.

        Returns:
            The created diagnostic for chaining.
        """
        diag = Diagnostic(Level.WARNING, code, message, span)
        self.diagnostics.append(diag)
        return diag

    def note(self, message: str,
             span: Optional[SourceSpan] = None) -> Diagnostic:
        """
        Create and add a note diagnostic.

        Args:
            message: Note message.
            span: Source location.

        Returns:
            The created diagnostic for chaining.
        """
        diag = Diagnostic(Level.NOTE, ErrorCode.GENERIC_NOTE, message, span)
        self.diagnostics.append(diag)
        return diag

    def has_errors(self) -> bool:
        """Check if any errors have been reported."""
        return any(d.level == Level.ERROR for d in self.diagnostics)

    def _render_diagnostic(self, diag: Diagnostic) -> str:
        """
        Render a single diagnostic in cargo style.

        Args:
            diag: Diagnostic to render.

        Returns:
            Formatted string.
        """
        lines: List[str] = []

        # Header line
        code_str = diag.code.value if diag.level != Level.NOTE else ""
        if code_str:
            # Error & Warning
            header = f"{diag.level.value}[{code_str}]: {diag.message}"
        else:
            # Note
            header = f"{diag.level.value}: {diag.message}"
        lines.append(header)

        # Location line
        if diag.span:
            lines.append(f"  --> {diag.span}")

            # Source snippet
            source = self._load_source_line(diag.span.file_path, diag.span.line)
            if source:
                lines.append("   |")
                lines.append(f"{diag.span.line:2} | {source}")
                lines.append("   |")

        # Additional notes
        for note in diag.notes:
            lines.append(f"    note: {note}")

        return "\n".join(lines)

    @staticmethod
    def _load_source_line(file_path: str, line_num: int) -> Optional[str]:
        """
        Load a specific line from source file.

        Args:
            file_path: Path to source file.
            line_num: Line number (1-based).

        Returns:
            Line content or None if file can't be read.
        """
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                for i, line in enumerate(f, 1):
                    if i == line_num:
                        return line.rstrip("\r\n")
        except (OSError, IOError):
            pass
        return None

    def format_all(self) -> str:
        """
        Format all diagnostics.

        Returns:
            Formatted string with all diagnostics.
        """
        parts = []
        for diag in self.diagnostics:
            parts.append(self._render_diagnostic(diag))
            parts.append("")
        return "\n".join(parts)
