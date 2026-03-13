#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Lexer - Lexical analyzer for Hardware Tree Compiler.

Converts preprocessed source text into a flat stream of tokens while keeping
source spans for later diagnostics.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import Diagnostics, ErrorCode, SourceLocation, SourceSpan
from .tokens import Token, TokenType
import re


class Lexer:
    """Tokenizes HTC source text into parser-ready lexical tokens."""

    _EOF_CHAR = "\0"
    _PREFIXED_NUMBER_PATTERN = re.compile(r"0[bBoOxX][A-Za-z0-9_]*")
    _FLOAT_PATTERN = re.compile(r"\d+\.\d+")
    _DECIMAL_PATTERN = re.compile(r"\d+")
    _IDENTIFIER_PATTERN = re.compile(
        r"[A-Za-z_][A-Za-z0-9_-]*(?:,[A-Za-z0-9_-]+)*")
    _OPERATORS = {
        "=": TokenType.ASSIGN,
        "<": TokenType.LT,
        ">": TokenType.GT,
        "&": TokenType.AMPERSAND,
        "@": TokenType.AT,
        ":": TokenType.COLON,
        "/": TokenType.SLASH,
        ";": TokenType.SEMICOLON,
        "{": TokenType.LBRACE,
        "}": TokenType.RBRACE,
        "[": TokenType.LBRACKET,
        "]": TokenType.RBRACKET,
        ",": TokenType.COMMA,
    }

    def __init__(self, source: str, file_path: str,
                 diagnostics: Diagnostics | None = None) -> None:
        """Initialize lexer state for a single source file."""
        self.source = source
        self.file_path = file_path
        self.diagnostics = diagnostics
        self.pos = 0
        self.line = 1
        self.column = 1
        self.tokens: list[Token] = []

    def tokenize(self) -> list[Token]:
        """Scan the full source buffer and return the token list."""
        while self.pos < len(self.source):
            char = self._peek()

            # Skip useless characters
            if char in " \t\r":
                self._advance()
                continue
            if char == "\n":
                self._newline()
                continue
            if char == "/" and self._peek(1) in {"/", "*"}:
                self._skip_comment()
                continue

            # String
            if char == '"':
                self.tokens.append(self._read_string())
                continue

            # Number
            if char.isdigit() or (char == "-" and self._peek(1).isdigit()):
                token = self._read_number()
                if token is not None:
                    self.tokens.append(token)
                    continue

            # Operator
            if char in self._OPERATORS:
                self.tokens.append(
                    Token(self._OPERATORS[char], char, self._span(1)))
                self._advance()
                continue

            # Identifier
            if char.isalpha() or char == "_":
                self.tokens.append(self._read_identifier())
                continue

            # Unknown
            self._error(ErrorCode.UNKNOWN_TOKEN, "unknown character")
            self._advance()

        # EOF
        eof_location = SourceLocation(self.file_path, self.line, self.column)
        self.tokens.append(Token(TokenType.EOF, "", SourceSpan(eof_location)))
        return self.tokens

    def _read_string(self) -> Token:
        """Read a double-quoted string literal token."""
        start = self._location()
        self._advance()
        chars: list[str] = []
        while self.pos < len(self.source):
            char = self._peek()
            if char == '"':
                self._advance()
                return Token(TokenType.STRING, "".join(chars),
                             SourceSpan(start, self._location()))
            if char == "\n":
                chars.append(char)
                self._newline()
                continue
            chars.append(char)
            self._advance()

        self._error(ErrorCode.UNTERMINATED_STRING,
                    "unterminated string literal", SourceSpan(start))
        return Token(TokenType.STRING, "".join(chars),
                     SourceSpan(start, self._location()))

    def _read_number(self) -> Token | None:
        """Read a broad numeric token and defer semantic validation."""
        start = self._location()
        negative = ""
        if self._peek() == "-":
            negative = "-"
            self._advance()
        chunk = self.source[self.pos:]

        for pattern, token_type in (
                (self._PREFIXED_NUMBER_PATTERN, TokenType.NUMBER),
                (self._FLOAT_PATTERN, TokenType.FLOAT),
                (self._DECIMAL_PATTERN, TokenType.NUMBER)):
            match = pattern.match(chunk)
            if match is not None:
                raw = negative + match.group(0)
                self._advance(len(match.group(0)))
                return Token(token_type, raw,
                             SourceSpan(start, self._location()))

        self._error(ErrorCode.INVALID_NUMBER, "invalid numeric literal",
                    SourceSpan(start))
        return None

    def _read_identifier(self) -> Token:
        """Read an identifier token."""
        start = self._location()
        match = self._IDENTIFIER_PATTERN.match(self.source[self.pos:])
        assert match is not None
        value = match.group(0)
        self._advance(len(value))
        return Token(TokenType.IDENTIFIER, value,
                     SourceSpan(start, self._location()))

    def _skip_comment(self) -> None:
        """Skip either a line comment or a block comment."""
        if self._peek(1) == "/":
            self._advance(2)
            while self.pos < len(self.source) and self._peek() != "\n":
                self._advance()
            return

        start = self._location()
        self._advance(2)
        while self.pos < len(self.source):
            if self._peek() == "\n":
                self._newline()
                continue
            if self._peek() == "*" and self._peek(1) == "/":
                self._advance(2)
                return
            self._advance()
        self._error(ErrorCode.UNTERMINATED_COMMENT, "unterminated comment",
                    SourceSpan(start))

    def _span(self, width: int) -> SourceSpan:
        """Build a span at the current position with a fixed width."""
        start = self._location()
        end = SourceLocation(self.file_path, self.line, self.column + width)
        return SourceSpan(start, end)

    def _location(self) -> SourceLocation:
        """Return the current source location."""
        return SourceLocation(self.file_path, self.line, self.column)

    def _peek(self, offset: int = 0) -> str:
        """Peek a source character by relative offset."""
        index = self.pos + offset
        if index >= len(self.source):
            return self._EOF_CHAR
        return self.source[index]

    def _advance(self, width: int = 1) -> None:
        """Advance the lexer cursor by a character width."""
        self.pos += width
        self.column += width

    def _newline(self) -> None:
        """Advance to the next physical source line."""
        self.pos += 1
        self.line += 1
        self.column = 1

    def _error(self, code: ErrorCode, message: str,
               span: SourceSpan | None = None) -> None:
        """Emit a lexer diagnostic when a sink is available."""
        if self.diagnostics is None:
            return
        diagnostic = self.diagnostics.error(code, message)
        if span is not None:
            diagnostic.with_source_span(span)
        else:
            diagnostic.with_span(self.file_path, self.line, self.column)
