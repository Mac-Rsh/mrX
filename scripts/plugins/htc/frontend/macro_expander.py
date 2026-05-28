#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Macro Expander - Object-like macro expansion helpers.

Expands simple object-like macros in source lines during preprocessing.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import Diagnostics, ErrorCode


class MacroExpander:
    """Expands simple object-like macros in source lines."""

    MAX_DEPTH = 16

    def __init__(self, diagnostics: Diagnostics) -> None:
        self.diagnostics = diagnostics

    def expand_line(
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

            if char == '"':
                in_string = not in_string
                result.append(char)
                index += 1
                continue

            if in_string and char == "\\" and index + 1 < len(line):
                result.append(line[index:index + 2])
                index += 2
                continue

            if not in_string and line.startswith("//", index):
                result.append(line[index:])
                break

            if not in_string and self._is_identifier_start(char):
                end = index + 1
                while end < len(line) and self._is_identifier_char(line[end]):
                    end += 1
                name = line[index:end]
                macro_column = column if column is not None else index + 1
                result.append(
                    self.expand_name(name, macros, file_path, line_number,
                                     macro_column, depth))
                index = end
                continue
            result.append(char)
            index += 1
        return "".join(result)

    def expand_name(self, name: str, macros: dict[str, str],
                    file_path: str | None, line_number: int | None,
                    column: int | None, depth: int) -> str:
        """Expand a single macro name with shallow recursion control."""
        if name not in macros:
            return name
        if depth >= self.MAX_DEPTH:
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
        return self.expand_line(value, macros, file_path, line_number, column,
                                depth + 1)

    @staticmethod
    def _is_identifier_start(char: str) -> bool:
        """Return True when a character can start an identifier."""
        return char.isalpha() or char == "_"

    @staticmethod
    def _is_identifier_char(char: str) -> bool:
        """Return True when a character can continue an identifier."""
        return char.isalnum() or char == "_"
