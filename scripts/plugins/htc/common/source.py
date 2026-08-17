#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Source - Source location and compilation unit models.

Provides lightweight data structures used to track file locations, spans,
include origins, and the ordered compilation unit.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass, field


@dataclass(frozen=True)
class SourceLocation:
    """Represents a single point in a source file."""

    file_path: str
    line: int
    column: int

    def __str__(self) -> str:
        """Return a diagnostic-friendly `file:line:column` string."""
        return f"{self.file_path}:{self.line}:{self.column}"


@dataclass(frozen=True)
class SourceSpan:
    """Represents a source range with required start and optional end."""

    start: SourceLocation
    end: SourceLocation | None = None

    @property
    def file_path(self) -> str:
        """Return the file path of the span start."""
        return self.start.file_path

    @property
    def line(self) -> int:
        """Return the line number of the span start."""
        return self.start.line

    @property
    def column(self) -> int:
        """Return the column number of the span start."""
        return self.start.column

    def __str__(self) -> str:
        """Return the start location for compact span rendering."""
        return str(self.start)


@dataclass(frozen=True)
class IncludeSite:
    """Records the location where a file was included from."""

    file_path: str
    line: int
    column: int = 1


@dataclass(frozen=True)
class SourceFile:
    """Stores source text together with include provenance metadata."""

    path: str
    text: str
    included_from: IncludeSite | None = None
    line_map: list[tuple[str, int]] | None = None


@dataclass
class CompilationUnit:
    """Aggregates all source files participating in one compilation."""

    entry_path: str
    files: list[SourceFile] = field(default_factory=list)
    _paths: set[str] = field(default_factory=set, init=False, repr=False)

    def __post_init__(self) -> None:
        """Initialize path indexes for any pre-populated source files."""
        self._paths.update(source_file.path for source_file in self.files)

    def contains(self, path: str) -> bool:
        """Return True when a source path is already in the unit."""
        return path in self._paths

    def append(self, source_file: SourceFile) -> bool:
        """Append a source file once when it is not already in the unit."""
        if source_file.path in self._paths:
            return False
        self.files.append(source_file)
        self._paths.add(source_file.path)
        return True
