#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Conditional State - Conditional compilation state machine.

Tracks nested conditional compilation blocks and branch state for the
preprocessor.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .directives import ConditionalDirective, DirectiveParser
from ..common import Diagnostics, ErrorCode
from .conditions import ConditionEvaluator
from dataclasses import dataclass


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


class ConditionalState:
    """Owns conditional stack transitions for one preprocessed source file."""

    DIRECTIVE_ELIF = DirectiveParser.DIRECTIVE_ELIF
    DIRECTIVE_ELSE = DirectiveParser.DIRECTIVE_ELSE
    DIRECTIVE_ENDIF = DirectiveParser.DIRECTIVE_ENDIF
    DIRECTIVE_IF = DirectiveParser.DIRECTIVE_IF
    DIRECTIVE_IFDEF = DirectiveParser.DIRECTIVE_IFDEF
    DIRECTIVE_IFNDEF = DirectiveParser.DIRECTIVE_IFNDEF

    def __init__(self, diagnostics: Diagnostics) -> None:
        self.diagnostics = diagnostics
        self.evaluator = ConditionEvaluator()
        self.frames: list[ConditionalFrame] = []

    def is_skipping(self) -> bool:
        """Return True when the current conditional stack disables output."""
        return any(not frame.current_active for frame in self.frames)

    def handle(self, directive: ConditionalDirective, macros: dict[str, str],
               file_path: str, line_number: int, line: str) -> None:
        """Update conditional compilation state for a directive line."""
        parent_active = all(frame.current_active for frame in self.frames)
        expression = directive.expression or self.evaluator.DEFAULT_EXPR
        directive_column = self._directive_column(line)
        expression_column = self._column_of(line, expression, directive_column)

        if directive.kind == self.DIRECTIVE_IFDEF:
            self._push(parent_active, directive.expression in macros,
                       file_path, line_number, expression_column)
            return
        if directive.kind == self.DIRECTIVE_IFNDEF:
            self._push(parent_active, directive.expression not in macros,
                       file_path, line_number, expression_column)
            return
        if directive.kind == self.DIRECTIVE_IF:
            matched = self._evaluate(expression, macros, file_path,
                                     line_number, expression_column)
            self._push(parent_active, matched, file_path, line_number,
                       expression_column)
            return

        if directive.kind == self.DIRECTIVE_ELIF:
            self._handle_elif(expression, macros, file_path, line_number,
                              directive_column, expression_column)
            return
        if directive.kind == self.DIRECTIVE_ELSE:
            self._handle_else(file_path, line_number, directive_column)
            return
        if directive.kind == self.DIRECTIVE_ENDIF:
            if self.frames:
                self.frames.pop()
                return
            self._report_unexpected(self.DIRECTIVE_ENDIF, file_path,
                                    line_number, directive_column)

    def report_unterminated(self) -> None:
        """Report an unterminated conditional block when nesting remains."""
        if not self.frames:
            return
        frame = self.frames[-1]
        self._report_line_error(ErrorCode.INCLUDE_ERROR,
                                "unterminated conditional directive block",
                                frame.file_path, frame.line, frame.column)

    def _handle_elif(self, expression: str, macros: dict[str, str],
                     file_path: str, line_number: int, directive_column: int,
                     expression_column: int) -> None:
        if not self.frames:
            self._report_unexpected(self.DIRECTIVE_ELIF, file_path,
                                    line_number, directive_column)
            return
        frame = self.frames[-1]
        if frame.saw_else:
            self._report_line_error(
                ErrorCode.INCLUDE_ERROR,
                f"#{self.DIRECTIVE_ELIF} cannot appear after #{self.DIRECTIVE_ELSE}",
                file_path,
                line_number,
                directive_column,
            )
            return
        matched = self._evaluate(expression, macros, file_path, line_number,
                                 expression_column)
        frame.current_active = frame.parent_active and (
            not frame.branch_taken) and matched
        frame.branch_taken = frame.branch_taken or matched

    def _handle_else(self, file_path: str, line_number: int,
                     directive_column: int) -> None:
        if not self.frames:
            self._report_unexpected(self.DIRECTIVE_ELSE, file_path,
                                    line_number, directive_column)
            return
        frame = self.frames[-1]
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

    def _push(self, parent_active: bool, matched: bool, file_path: str,
              line_number: int, column: int) -> None:
        self.frames.append(
            ConditionalFrame(
                parent_active=parent_active,
                current_active=parent_active and matched,
                branch_taken=matched,
                file_path=file_path,
                line=line_number,
                column=column,
            )
        )

    def _evaluate(self, expression: str, macros: dict[str, str],
                  file_path: str, line_number: int, column: int) -> bool:
        try:
            return self.evaluator.evaluate(expression, macros)
        except ValueError as exc:
            self.diagnostics.error(
                ErrorCode.INCLUDE_ERROR,
                f"invalid conditional expression: {expression}",
            ).with_span(file_path, line_number, column).add_note(str(exc))
            return False

    def _report_unexpected(self, directive_name: str, file_path: str,
                           line_number: int, column: int) -> None:
        self._report_line_error(
            ErrorCode.INCLUDE_ERROR,
            f"unexpected #{directive_name} without matching conditional block",
            file_path,
            line_number,
            column,
        )

    def _report_line_error(self, code: ErrorCode, message: str,
                           file_path: str, line_number: int,
                           column: int = 1) -> None:
        self.diagnostics.error(code, message).with_span(file_path, line_number,
                                                        column)

    @classmethod
    def _directive_column(cls, line: str) -> int:
        return cls._column_of(line, DirectiveParser.DIRECTIVE_PREFIX)

    @staticmethod
    def _column_of(line: str, text: str | None,
                   fallback: int | None = None) -> int:
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
