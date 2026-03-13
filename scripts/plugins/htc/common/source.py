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


@dataclass
class CompilationUnit:
    """Aggregates all source files participating in one compilation."""

    entry_path: str
    files: list[SourceFile] = field(default_factory=list)
    include_graph: dict[str, list[str]] = field(default_factory=dict)

    def append(self, source_file: SourceFile) -> None:
        """Append a source file and initialize its include graph node."""
        self.files.append(source_file)
        self.include_graph.setdefault(source_file.path, [])

    def add_edge(self, parent: str, child: str) -> None:
        """Record an include relationship between two source files."""
        self.include_graph.setdefault(parent, []).append(child)
        self.include_graph.setdefault(child, [])
