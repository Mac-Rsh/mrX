#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Directives - Directive parsing helpers for HTC frontend.

Parses physical source lines into structured preprocessor directive records
without applying any file-system or macro side effects.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass
import re


@dataclass(frozen=True)
class IncludeDirective:
    """Represents a parsed include directive."""

    path: str
    kind: str


@dataclass(frozen=True)
class DefineDirective:
    """Represents a parsed macro definition directive."""

    name: str
    value: str


@dataclass(frozen=True)
class UndefDirective:
    """Represents a parsed macro undefinition directive."""

    name: str


@dataclass(frozen=True)
class ConditionalDirective:
    """Represents a parsed conditional compilation directive."""

    kind: str
    expression: str | None = None


@dataclass(frozen=True)
class UnsupportedDirective:
    """Represents a directive that was recognized but not supported."""

    name: str


class DirectiveParser:
    """Parses one source line into a structured preprocessor directive."""

    DIRECTIVE_INCLUDE = "include"
    DIRECTIVE_DEFINE = "define"
    DIRECTIVE_UNDEF = "undef"
    DIRECTIVE_IF = "if"
    DIRECTIVE_IFDEF = "ifdef"
    DIRECTIVE_IFNDEF = "ifndef"
    DIRECTIVE_ELIF = "elif"
    DIRECTIVE_ELSE = "else"
    DIRECTIVE_ENDIF = "endif"
    INCLUDE_KIND_QUOTE = "quote"
    INCLUDE_KIND_ANGLE = "angle"
    DEFAULT_DEFINE_VALUE = "1"

    HTC_INCLUDE_PATTERN = re.compile(
        r'^/include/\s*"([^"]+)"(?:\s*(?://.*|/\*.*\*/\s*)?)?$')
    C_INCLUDE_PATTERN = re.compile(
        r'^([<"])([^>"]+)[>"](?:\s*(?://.*|/\*.*\*/\s*)?)?$')
    IDENTIFIER_PATTERN = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
    UNSUPPORTED_DIRECTIVE_PATTERN = re.compile(r'^(pragma|error|warning)\b')
    DIRECTIVE_PREFIX = "#"

    def parse(
            self,
            line: str,
    ) -> (IncludeDirective | DefineDirective | UndefDirective |
          ConditionalDirective | UnsupportedDirective | None):
        """Parse one physical line into a recognized preprocessor directive."""
        stripped = line.strip()
        if not stripped:
            return None

        include_match = self.HTC_INCLUDE_PATTERN.match(stripped)
        if include_match is not None:
            return IncludeDirective(include_match.group(1),
                                    self.INCLUDE_KIND_QUOTE)

        if not stripped.startswith(self.DIRECTIVE_PREFIX):
            return None
        payload = stripped[len(self.DIRECTIVE_PREFIX):].lstrip()
        parts = payload.split(None, 1)
        command = parts[0] if parts else ""
        remainder = parts[1] if len(parts) > 1 else ""
        remainder = self._strip_trailing_comment(remainder).strip()

        if command == self.DIRECTIVE_INCLUDE:
            return self._parse_include(remainder)
        if command == self.DIRECTIVE_DEFINE:
            return self._parse_define(remainder)
        if command == self.DIRECTIVE_UNDEF:
            return self._parse_undef(remainder)
        if command in {self.DIRECTIVE_IFDEF, self.DIRECTIVE_IFNDEF}:
            return self._parse_named_conditional(command, remainder)
        if command in {self.DIRECTIVE_IF, self.DIRECTIVE_ELIF}:
            return self._parse_expression_conditional(command, remainder)
        if command in {self.DIRECTIVE_ELSE, self.DIRECTIVE_ENDIF}:
            return ConditionalDirective(command)

        unsupported_match = self.UNSUPPORTED_DIRECTIVE_PATTERN.match(command)
        if unsupported_match is not None:
            return UnsupportedDirective(unsupported_match.group(1))
        return None

    def _parse_include(
            self,
            remainder: str,
    ) -> IncludeDirective | UnsupportedDirective:
        """Parse one include directive payload."""
        include_match = self.C_INCLUDE_PATTERN.match(remainder)
        if include_match is None:
            return UnsupportedDirective(self.DIRECTIVE_INCLUDE)
        marker = include_match.group(1)
        kind = (self.INCLUDE_KIND_ANGLE if marker == "<"
                else self.INCLUDE_KIND_QUOTE)
        return IncludeDirective(include_match.group(2), kind)

    def _parse_define(self,
                      remainder: str) -> DefineDirective | UnsupportedDirective:
        """Parse one define directive payload."""
        name_match = self.IDENTIFIER_PATTERN.match(remainder)
        if name_match is None:
            return UnsupportedDirective(self.DIRECTIVE_DEFINE)
        name = name_match.group(0)
        value = remainder[
                    name_match.end():].strip() or self.DEFAULT_DEFINE_VALUE
        return DefineDirective(name, value)

    def _parse_undef(self,
                     remainder: str) -> UndefDirective | UnsupportedDirective:
        """Parse one undef directive payload."""
        if not remainder or self.IDENTIFIER_PATTERN.fullmatch(
                remainder) is None:
            return UnsupportedDirective(self.DIRECTIVE_UNDEF)
        return UndefDirective(remainder)

    def _parse_named_conditional(
            self, command: str, remainder: str
    ) -> ConditionalDirective | UnsupportedDirective:
        """Parse one identifier-based conditional directive payload."""
        if not remainder or self.IDENTIFIER_PATTERN.fullmatch(
                remainder) is None:
            return UnsupportedDirective(command)
        return ConditionalDirective(command, remainder)

    @staticmethod
    def _parse_expression_conditional(
            command: str, remainder: str
    ) -> ConditionalDirective | UnsupportedDirective:
        """Parse one expression-based conditional directive payload."""
        if not remainder:
            return UnsupportedDirective(command)
        return ConditionalDirective(command, remainder)

    @staticmethod
    def _strip_trailing_comment(text: str) -> str:
        """Strip trailing line or block comments outside string literals."""
        result: list[str] = []
        in_string = False
        index = 0
        while index < len(text):
            char = text[index]
            if in_string and char == "\\" and index + 1 < len(text):
                result.append(char)
                result.append(text[index + 1])
                index += 2
                continue
            if char == '"':
                in_string = not in_string
                result.append(char)
                index += 1
                continue
            if not in_string and text.startswith("//", index):
                break
            if not in_string and text.startswith("/*", index):
                end = text.find("*/", index + 2)
                if end == -1:
                    break
                index = end + 2
                continue
            result.append(char)
            index += 1
        return "".join(result)
