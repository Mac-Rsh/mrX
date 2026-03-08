#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Lexer - Lexical analyzer for Hardware Tree Compiler.

Converts .hts source files into token streams.

Include directives are handled by the preprocessor before tokenization.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .diagnostics import Diagnostics, ErrorCode
from dataclasses import dataclass
from typing import Optional
from enum import Enum
import re


class TokenType(Enum):
    """Token type enumeration."""

    # Identifiers and literals
    IDENTIFIER = "IDENTIFIER"
    STRING = "STRING"
    NUMBER = "NUMBER"
    FLOAT = "FLOAT"

    # Operators
    ASSIGN = "="
    LT = "<"
    GT = ">"
    AMPERSAND = "&"
    AT = "@"
    COLON = ":"
    SLASH = "/"
    SEMICOLON = ";"
    LBRACE = "{"
    RBRACE = "}"
    COMMA = ","

    # Special
    EOF = "EOF"
    UNKNOWN = "UNKNOWN"


@dataclass
class Token:
    """Represents a single lexical token with position information."""

    type: TokenType
    value: str
    line: int
    column: int
    file_path: str = ""

    def __str__(self) -> str:
        return f"Token({self.type.value}, {repr(self.value)}, {self.line}:{self.column})"

    def __repr__(self) -> str:
        return self.__str__()


class Lexer:
    """
    Hardware Tree lexical analyzer.

    Responsibilities:
    - Convert input character stream into token stream
    - Handle comments (ignored)
    - Track line and column numbers for error reporting

    Note: Include directives are handled by the preprocessor before lexing.
    """

    # Null character for peek operations
    _NULL_CHAR: str = "\0"

    # Regular expression patterns for token matching
    _HEX_PATTERN: re.Pattern[str] = re.compile(r"0[xX][0-9a-fA-F]+")
    _FLOAT_PATTERN: re.Pattern[str] = re.compile(r"\d+\.\d+")
    _DECIMAL_PATTERN: re.Pattern[str] = re.compile(r"\d+")
    # Identifier: starts with letter/underscore, can contain hyphens (e.g., "xfer-ops-mode")
    _IDENTIFIER_PATTERN: re.Pattern[str] = re.compile(
        r"[a-zA-Z_][a-zA-Z0-9_\-,]*")

    # Single-character operator mappings
    _OPERATOR_MAP: dict[str, TokenType] = {
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
        ",": TokenType.COMMA,
    }

    # Whitespace characters to skip
    _WHITESPACE_CHARS: str = " \t\r"

    def __init__(
            self,
            source: str,
            file_path: str = "",
            diagnostics: Optional[Diagnostics] = None,
    ) -> None:
        """
        Initialize the lexer.

        Args:
            source: Source code string to tokenize.
            file_path: File path for error reporting.
            diagnostics: Optional Diagnostics instance for error reporting.
        """
        self.source = source
        self.file_path = file_path
        self.diagnostics = diagnostics
        self.pos = 0
        self.line = 1
        self.column = 1
        self.tokens: list[Token] = []

    def tokenize(self) -> list[Token]:
        """
        Perform lexical analysis and return token list.

        Returns:
            Token list ending with EOF token.
        """
        while self.pos < len(self.source):
            # Skip spaces
            self._skip_spaces()
            if self.pos >= len(self.source):
                break

            char = self._peek_char()

            # Newline
            if self._at_newline(char):
                self._advance()
                self.line += 1
                self.column = 1
                continue

            # Skip comments
            if self._at_comment_start(char):
                self._skip_comment()
                continue

            # String
            if self._at_quote(char):
                token = self._read_string()
                if token:
                    self.tokens.append(token)
                continue

            # Number
            if self._at_number(char):
                token = self._read_number()
                if token:
                    self.tokens.append(token)
                continue

            # Operator
            if self._is_operator(char):
                self.tokens.append(
                    Token(
                        type=self._OPERATOR_MAP[char],
                        value=char,
                        line=self.line,
                        column=self.column,
                        file_path=self.file_path,
                    )
                )
                self._advance()
                continue

            # Identifier
            if self._at_identifier_start(char):
                token = self._read_identifier()
                if token:
                    self.tokens.append(token)
                continue

            # Unknown character
            if self.diagnostics:
                self.diagnostics.error(
                    ErrorCode.UNKNOWN_TOKEN,
                    "unknown character",
                ).with_span(
                    self.file_path,
                    self.line,
                    self.column,
                )
            self.tokens.append(
                Token(
                    type=TokenType.UNKNOWN,
                    value=char,
                    line=self.line,
                    column=self.column,
                    file_path=self.file_path,
                )
            )
            self._advance()

        # Append EOF token
        self.tokens.append(
            Token(
                type=TokenType.EOF,
                value="",
                line=self.line,
                column=self.column,
                file_path=self.file_path,
            )
        )

        return self.tokens

    def _advance(self, n: int = 1) -> None:
        """Advance position by n characters."""
        self.pos += n
        self.column += n

    def _peek_char(self, offset: int = 0) -> str:
        """
        Peek at character at specified offset.

        Args:
            offset: Offset from current position.

        Returns:
            Character at offset or null character if out of bounds.
        """
        idx = self.pos + offset
        return self._NULL_CHAR if idx >= len(self.source) else self.source[idx]

    def _skip_spaces(self) -> None:
        """Skip space characters (space, tab, carriage return)."""
        while self.pos < len(self.source):
            char = self.source[self.pos]
            if char in self._WHITESPACE_CHARS:
                self._advance()
            else:
                break

    def _skip_comment(self) -> None:
        """Skip /* */ style block comments."""
        # Only track start position if diagnostics available
        start_line = self.line if self.diagnostics else None
        start_column = self.column if self.diagnostics else None

        self._advance(2)  # Skip opening /*

        while self.pos < len(self.source):
            if self.source[self.pos] == "\n":
                self.line += 1
                self.column = 1
                self.pos += 1
            elif self.source[self.pos] == "*" and self._peek_char(1) == "/":
                self._advance(2)
                return
            else:
                self._advance()

        if self.diagnostics:
            self.diagnostics.error(
                ErrorCode.UNTERMINATED_COMMENT,
                "unterminated comment",
            ).with_span(
                self.file_path,
                start_line,
                start_column,
            )

    def _read_string(self) -> Optional[Token]:
        """
        Read string literal " ... ".

        Returns:
            STRING token if valid, UNKNOWN token if unterminated.
        """
        start_line, start_column = self.line, self.column
        start_pos = self.pos
        self._advance()  # Skip opening quote

        while self.pos < len(self.source):
            char = self.source[self.pos]
            if char == "\n":
                self.line += 1
                self.column = 1
                self.pos += 1
            elif char == "\"":
                value = self.source[start_pos + 1:self.pos]
                self._advance()
                return Token(
                    type=TokenType.STRING,
                    value=value,
                    line=start_line,
                    column=start_column,
                    file_path=self.file_path,
                )
            else:
                self._advance()

        if self.diagnostics:
            self.diagnostics.error(
                ErrorCode.UNTERMINATED_STRING,
                "unterminated string literal",
            ).with_span(
                self.file_path,
                start_line,
                start_column,
            )

        return Token(
            type=TokenType.UNKNOWN,
            value=self.source[start_pos + 1:],
            line=start_line,
            column=start_column,
            file_path=self.file_path,
        )

    def _read_number(self) -> Optional[Token]:
        """
        Read number (decimal/hexadecimal/float), with optional leading minus.

        Returns:
            NUMBER or FLOAT token, or None if not a valid number.
        """
        start_line, start_column, start_pos = self.line, self.column, self.pos

        # Handle negative sign
        negative_prefix = ""
        if self.source[self.pos] == "-":
            negative_prefix = "-"
            self._advance()

        # Try patterns in order: hex -> float -> decimal
        for pattern, token_type in [
            (self._HEX_PATTERN, TokenType.NUMBER),
            (self._FLOAT_PATTERN, TokenType.FLOAT),
            (self._DECIMAL_PATTERN, TokenType.NUMBER),
        ]:
            match = pattern.match(self.source[self.pos:])
            if match:
                value = negative_prefix + match.group()
                self._advance(len(match.group()))
                return Token(
                    type=token_type,
                    value=value,
                    line=start_line,
                    column=start_column,
                    file_path=self.file_path,
                )

        # Not a number, rollback
        self.pos, self.column = start_pos, start_column
        return None

    def _read_identifier(self) -> Optional[Token]:
        """
        Read identifier.

        Returns:
            IDENTIFIER token or None if failed.
        """
        start_line = self.line
        start_column = self.column

        match = self._IDENTIFIER_PATTERN.match(self.source[self.pos:])
        if match:
            value = match.group()
            self._advance(len(value))

            return Token(
                type=TokenType.IDENTIFIER,
                value=value,
                line=start_line,
                column=start_column,
                file_path=self.file_path,
            )

        return None

    @staticmethod
    def _at_newline(char: str) -> bool:
        """Check if character is a newline."""
        return char == "\n"

    def _at_comment_start(self, char: str) -> bool:
        """Check if position starts a block comment (/*)."""
        return char == "/" and self._peek_char(1) == "*"

    @staticmethod
    def _at_quote(char: str) -> bool:
        """Check if character starts a string literal (\")."""
        return char == "\""

    def _at_number(self, char: str) -> bool:
        """Check if position starts a number literal."""
        next_char = self._peek_char(1)
        return char.isdigit() or (char == "-" and next_char != "\0" and next_char.isdigit())

    def _is_operator(self, char: str) -> bool:
        """Check if character is a single-character operator."""
        return char in self._OPERATOR_MAP

    @staticmethod
    def _at_identifier_start(char: str) -> bool:
        """Check if character is valid identifier start (letter or underscore)."""
        return char.isalpha() or char == "_"


def tokenize_string(
        source: str,
        file_path: str = "",
        diagnostics: Optional[Diagnostics] = None,
) -> list[Token]:
    """
    Convenience function to tokenize a string.

    Args:
        source: Source code string.
        file_path: File path for error reporting.
        diagnostics: Optional Diagnostics instance for error reporting.

    Returns:
        Token list ending with EOF token.
    """
    lexer = Lexer(source, file_path, diagnostics)
    return lexer.tokenize()


def tokenize_file(
        file_path: str,
        diagnostics: Optional[Diagnostics] = None,
) -> list[Token]:
    """
    Convenience function to tokenize a file.

    Args:
        file_path: Source file path.
        diagnostics: Optional Diagnostics instance for error reporting.

    Returns:
        Token list ending with EOF token.

    Raises:
        FileNotFoundError: If the file does not exist.
        IOError: If the file cannot be read.
    """
    with open(file_path, "r", encoding="utf-8") as f:
        source = f.read()
    return tokenize_string(source, file_path, diagnostics)


def merge_streams(token_streams: list[list[Token]]) -> list[Token]:
    """
    Merge multiple token streams into a single stream.

    This function combines token streams from multiple files into a single
    stream for parsing. It:
    - Concatenates all token streams in order
    - Removes intermediate EOF tokens (one EOF per file)
    - Keeps only the final EOF token

    Args:
        token_streams: List of token streams to merge.

    Returns:
        Merged token stream with single EOF at the end.
    """
    if not token_streams:
        return [Token(TokenType.EOF, "", 1, 1, "")]

    merged = []
    for i, stream in enumerate(token_streams):
        if i < len(token_streams) - 1:
            # Remove EOF from non-last streams
            merged.extend(stream[:-1] if stream and stream[
                -1].type == TokenType.EOF else stream)
        else:
            # Last stream: keep EOF
            merged.extend(stream)

    return merged
