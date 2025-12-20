#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
@copyright (c) 2024-2025, MacRsh

@license SPDX-License-Identifier: Apache-2.0

@date 2024-09-06        MacRsh        First version
"""

from dataclasses import dataclass
from typing import Any, Optional
from enum import Enum, auto


class TokenType(Enum):
    """
    A class representing a token type.
    """

    STRING = auto()  # "string"
    INTEGER = auto()  # 123, 0xff
    FLOAT = auto()  # 1.23
    IDENTIFIER = auto()  # node_name
    LABEL = auto()  # label_name
    REFERENCE = auto()  # &label, &{/path}

    SLASH = auto()  # /
    LBRACE = auto()  # {
    RBRACE = auto()  # }
    SEMICOLON = auto()  # ;
    EQUALS = auto()  # =
    LT = auto()  # <
    GT = auto()  # >
    LSQB = auto()  # [
    RSQB = auto()  # ]
    AMPERSAND = auto()  # &
    COLON = auto()  # :
    COMMA = auto()  # ,
    AT = auto()  # @

    EOF = auto()  # EOF
    INCLUDE = auto()  # /include/
    COMMENT = auto()  # //


@dataclass
class Token:
    """
    A class representing a token.
    """

    type: TokenType
    value: Any
    line: int
    column: int

    def __repr__(self):
        return f"Token({self.type.name}, {repr(self.value)}, line={self.line}, column={self.column})"


class Lexer:
    """
    A class for lexical analysis.
    """

    def __init__(self, source: str):
        self.current_char = source[0] if source else None
        self.source = source
        self.column = 1
        self.line = 1
        self.pos = 0

    def error(self, message: str):
        """
        Exception raised when an error occurs during lexical analysis.
        """
        raise Exception(
            f"Lexer error at line {self.line}, column {self.column}: {message}")

    def advance(self, n: int = 1):
        """
        Advances to the next character.

        Args:
            n (int): The number of characters to advance.
        """
        while self.current_char is not None and n > 0:
            # Next line
            if self.current_char == '\n':
                self.column = 1
                self.line += 1
            else:
                self.column += 1
            # Next char
            self.pos += 1
            if self.pos < len(self.source):
                self.current_char = self.source[self.pos]
            else:
                self.current_char = None
            n -= 1

    def peek(self, offset: int = 1) -> Optional[str]:
        """
        Peek at the character at the given offset.

        Args:
            offset (int): The offset to peek.
        Returns:
             The character at the given offset.
        """
        peek_pos = self.pos + offset
        if peek_pos < len(self.source):
            return self.source[peek_pos]
        else:
            return None

    def skip_whitespace(self):
        """
        Skips whitespace characters.
        """
        while self.current_char is not None and self.current_char.isspace():
            self.advance()

    def skip_comment(self):
        """
        Skips comment characters.
        """
        # Handle '//' comments
        if self.current_char == '/' and self.peek() == '/':
            while self.current_char is not None and self.current_char != '\n':
                self.advance()
        # Handle '/**/' comments
        elif self.current_char == '/' and self.peek() == '*':
            self.advance(2)
            while self.current_char is not None:
                if self.current_char == '*' and self.peek() == '/':
                    self.advance(2)
                    break
                self.advance()

    def number(self) -> Token:
        """
        Handles number tokens.

        Returns:
            Token: The number token.
        """
        start_column = self.column
        start_line = self.line
        result = ''

        # Handle hex
        if self.current_char == '0' and self.peek() in 'xX':
            # 0x
            result += self.current_char
            result += self.peek()
            self.advance(2)
            # Hex digits
            while self.current_char is not None and self.current_char.isdigit() \
                    or self.current_char.lower() in 'abcdef':
                result += self.current_char
                self.advance()
            return Token(TokenType.INTEGER, int(result, 16), start_line,
                         start_column)
        # Handle decimal
        while self.current_char is not None and self.current_char.isdigit():
            result += self.current_char
            self.advance()
        # Handle float
        if self.current_char == '.' and self.peek().isdigit():
            result += self.current_char
            self.advance()
            while self.current_char is not None and self.current_char.isdigit():
                result += self.current_char
                self.advance()
            return Token(TokenType.FLOAT, float(result), start_line,
                         start_column)
        # Integer
        return Token(TokenType.INTEGER, int(result), start_line, start_column)

    def string(self) -> Token:
        """
        Handles string tokens.

        Returns:
            Token: The string token.
        """
        start_column = self.column
        start_line = self.line
        result = ''

        # String start
        while self.current_char is not None and self.current_char != '"':
            # Escape
            if self.current_char == '\\':
                self.advance()
                if self.current_char == 'n':
                    result += '\n'
                elif self.current_char == 't':
                    result += '\t'
                elif self.current_char == 'r':
                    result += '\r'
                elif self.current_char == '\\':
                    result += '\\'
                elif self.current_char == '"':
                    result += '"'
                else:
                    result += self.current_char
            else:
                result += self.current_char
            self.advance()
        # String end
        if self.current_char != '"':
            self.error("Unterminated string")
        self.advance()
        return Token(TokenType.STRING, result, start_line, start_column)

    def identifier_or_include(self) -> Token:
        """
        Handles identifier or include tokens.

        Returns:
            Token: The identifier or include token.
        """
        start_column = self.column
        start_line = self.line
        result = ''

        # Handle include
        if self.current_char == '/' and self.peek() == 'i' and self.source[
            self.pos:self.pos + 9] == '/include/':
            for _ in range(9):
                result += self.current_char
                self.advance()
            return Token(TokenType.INCLUDE, result, start_line, start_column)
        # Handle identifier
        while self.current_char is not None and (
                self.current_char.isalnum() or self.current_char in '_-@.,*#'):
            result += self.current_char
            self.advance()
        # Handle label
        if self.current_char == ':':
            return Token(TokenType.LABEL, result, start_line, start_column)
        # Handle reference
        if self.current_char == '&' and result == '':
            self.advance()
            path = ''
            # Path reference
            if self.current_char == '{':
                path += self.current_char
                self.advance()
                # Path end
                brace_count = 1
                while self.current_char is not None and brace_count > 0:
                    path += self.current_char
                    if self.current_char == '{':
                        brace_count += 1
                    elif self.current_char == '}':
                        brace_count -= 1
                    self.advance()
                if self.current_char == '}':
                    self.advance()
            else:
                # Label reference
                while self.current_char is not None and (
                        self.current_char.isalnum() or self.current_char in
                        '_-'):
                    path += self.current_char
                    self.advance()
            return Token(TokenType.REFERENCE, path, start_line, start_column)
        # Identifier
        return Token(TokenType.IDENTIFIER, result, start_line, start_column)

    def get_next_token(self) -> Token:
        """
        Gets the next token.

        Returns:
            Token: The next token.
        """
        while self.current_char is not None:
            # Whitespace
            if self.current_char.isspace():
                self.skip_whitespace()
                continue
            # Comments
            if self.current_char == '/' and self.peek() in '/*':
                self.skip_comment()
                continue
            # Single character
            start_column = self.column
            start_line = self.line
            if self.current_char == '/':
                self.advance()
                return Token(TokenType.SLASH, '/', start_line, start_column)
            elif self.current_char == '{':
                self.advance()
                return Token(TokenType.LBRACE, '{', start_line, start_column)
            elif self.current_char == '}':
                self.advance()
                return Token(TokenType.RBRACE, '}', start_line, start_column)
            elif self.current_char == ';':
                self.advance()
                return Token(TokenType.SEMICOLON, ';', start_line, start_column)
            elif self.current_char == '=':
                self.advance()
                return Token(TokenType.EQUALS, '=', start_line, start_column)
            elif self.current_char == '<':
                self.advance()
                return Token(TokenType.LT, '<', start_line, start_column)
            elif self.current_char == '>':
                self.advance()
                return Token(TokenType.GT, '>', start_line, start_column)
            elif self.current_char == '[':
                self.advance()
                return Token(TokenType.LSQB, '[', start_line, start_column)
            elif self.current_char == ']':
                self.advance()
                return Token(TokenType.RSQB, ']', start_line, start_column)
            elif self.current_char == '@':
                self.advance()
                return Token(TokenType.AT, '@', start_line, start_column)
            elif self.current_char == ',':
                self.advance()
                return Token(TokenType.COMMA, ',', start_line, start_column)
            elif self.current_char == '&':
                self.advance()
                return Token(TokenType.AMPERSAND, '&', start_line, start_column)
            elif self.current_char == ':':
                self.advance()
                return Token(TokenType.COLON, ':', start_line, start_column)
            # String
            elif self.current_char == '"':
                return self.string()
            # Number
            elif self.current_char.isdigit():
                return self.number()
            # Identifier or include
            elif self.current_char.isalpha() or self.current_char in '/&':
                return self.identifier_or_include()
            else:
                self.error(f"Unexpected character'{self.current_char}'")
        # EOF
        return Token(TokenType.EOF, None, self.line, self.column)

    def tokenize(self) -> list[Token]:
        """
        Tokenizes the source code.

        Returns:
            list[Token]: The list of tokens.
        """
        tokens = []
        while True:
            token = self.get_next_token()
            tokens.append(token)
            if token.type == TokenType.EOF:
                break
        return tokens
