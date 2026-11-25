#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Human-readable terminal output backed by Rich.

Business commands must depend on this interface, not on Rich directly.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from collections.abc import Iterable, Sequence
from typing import TextIO
import os
import sys

from rich.console import Console as RichConsole
from rich.table import Table
from rich.text import Text
from rich import box

from .diagnostics import Diagnostic, DiagnosticLevel

_LEVEL_STYLES = {
    DiagnosticLevel.INFO: "cyan",
    DiagnosticLevel.WARNING: "yellow",
    DiagnosticLevel.ERROR: "red",
}


class Console:
    """Render consistent human-readable output to stdout and stderr."""

    def __init__(
            self,
            stdout: TextIO = sys.stdout,
            stderr: TextIO = sys.stderr,
            color: bool | None = None,
    ) -> None:
        """Create a console using the requested output streams."""
        self.stdout = stdout
        self.stderr = stderr
        self._color_override = color
        self._stdout_console = self._make_rich(stdout)
        self._stderr_console = self._make_rich(stderr)

    def set_color(self, enabled: bool) -> None:
        """Enable or disable color for subsequent output."""
        self._color_override = enabled
        self._stdout_console = self._make_rich(self.stdout)
        self._stderr_console = self._make_rich(self.stderr)

    def write(self, message: str = "", error: bool = False,
              end: str = "\n") -> None:
        """Write unclassified output to the selected stream."""
        rich = self._stderr_console if error else self._stdout_console
        rich.print(message, end=end, markup=False, highlight=False,
                   overflow="fold")

    def info(self, message: str) -> None:
        """Write an informational message."""
        self._write_level(DiagnosticLevel.INFO, message, error=False)

    def success(self, message: str) -> None:
        """Write a successful completion message."""
        text = Text()
        text.append("OK", style=self._style("green"))
        text.append(f" {message}")
        self._stdout_console.print(text, overflow="fold", highlight=False)

    def warning(self, message: str) -> None:
        """Write a warning message."""
        self._write_level(DiagnosticLevel.WARNING, message, error=False)

    def error(self, message: str) -> None:
        """Write an error message."""
        self._write_level(DiagnosticLevel.ERROR, message, error=True)

    def diagnostic(self, diagnostic: Diagnostic) -> None:
        """Render one structured diagnostic and its optional guidance."""
        error = diagnostic.level is DiagnosticLevel.ERROR
        rich = self._stderr_console if error else self._stdout_console
        prefix = Text()
        label = f"{diagnostic.level.value.upper()}[{diagnostic.code}]"
        prefix.append(label, style=self._style(_LEVEL_STYLES[diagnostic.level]))
        prefix.append(f": {diagnostic.message}")
        rich.print(prefix, overflow="fold", highlight=False)
        if diagnostic.hint:
            self.write(f"  Hint: {diagnostic.hint}", error=error)
        for detail in diagnostic.details:
            self.write(f"  Note: {detail}", error=error)

    def diagnostics(self, diagnostics: Iterable[Diagnostic]) -> None:
        """Render a sequence of structured diagnostics."""
        for diagnostic in diagnostics:
            self.diagnostic(diagnostic)

    def table(self, headers: Sequence[str],
              rows: Iterable[Sequence[str]]) -> None:
        """Render a table that respects terminal width and Unicode width."""
        materialized = [tuple(str(cell) for cell in row) for row in rows]
        if not headers:
            return
        table = Table(
            box=box.SIMPLE,
            header_style=self._style("bold"),
            show_edge=False,
            pad_edge=False,
            expand=True,
            highlight=False,
        )
        for header in headers:
            table.add_column(str(header), overflow="fold", no_wrap=False)
        for row in materialized:
            if len(row) != len(headers):
                raise ValueError("Table row width does not match its header")
            table.add_row(*row)
        self._stdout_console.print(table)

    def clear(self) -> None:
        """Clear the stdout terminal when attached to a TTY."""
        self._stdout_console.clear()

    def _write_level(self, level: DiagnosticLevel, message: str,
                     error: bool) -> None:
        """Write one prefixed level message."""
        rich = self._stderr_console if error else self._stdout_console
        text = Text()
        text.append(level.value.upper(),
                    style=self._style(_LEVEL_STYLES[level], stream=rich.file))
        text.append(f" {message}")
        rich.print(text, overflow="fold", highlight=False)

    def _style(self, style: str, stream: TextIO | None = None) -> str:
        """Return a Rich style name when color is enabled."""
        if not self._color_enabled(stream):
            return ""
        return style

    def _color_enabled(self, stream: TextIO | None = None) -> bool:
        """Check whether colored output is enabled for the target stream."""
        if self._color_override is not None:
            return self._color_override
        if os.environ.get("NO_COLOR", "") != "":
            return False
        target = self.stdout if stream is None else stream
        return bool(getattr(target, "isatty", lambda: False)())

    def _make_rich(self, stream: TextIO) -> RichConsole:
        """Build one Rich console bound to a stream."""
        no_color = not self._color_enabled(stream)
        return RichConsole(
            file=stream,
            no_color=no_color,
            color_system=None if no_color else "auto",
            highlight=False,
            soft_wrap=True,
            emoji=False,
        )
