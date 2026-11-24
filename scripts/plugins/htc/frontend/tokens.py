#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Tokens - Lexical token definitions for Hardware Tree Compiler.

Defines token categories and token records shared by the lexer and parser.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..common import SourceSpan
from dataclasses import dataclass
from enum import Enum


class TokenType(Enum):
    """Enumerates token kinds recognized by the lexer."""

    IDENTIFIER = "IDENTIFIER"
    STRING = "STRING"
    CHAR = "CHAR"
    NUMBER = "NUMBER"
    FLOAT = "FLOAT"
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
    LBRACKET = "["
    RBRACKET = "]"
    LPAREN = "("
    RPAREN = ")"
    COMMA = ","
    PLUS = "+"
    MINUS = "-"
    STAR = "*"
    PERCENT = "%"
    PIPE = "|"
    CARET = "^"
    TILDE = "~"
    BANG = "!"
    QUESTION = "?"
    EOF = "EOF"


@dataclass(frozen=True)
class Token:
    """Represents one lexical token together with its source span."""

    type: TokenType
    value: str
    span: SourceSpan
