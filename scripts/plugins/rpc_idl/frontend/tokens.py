#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Tokens - Lexical token definitions for the RPC IDL compiler.

Defines token categories and token records shared by the lexer and parser.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ...htc.common import SourceSpan
from dataclasses import dataclass
from enum import Enum


class TokenType(Enum):
    """Enumerates token kinds recognized by the IDL lexer."""

    IDENTIFIER = "IDENTIFIER"
    STRING = "STRING"
    NUMBER = "NUMBER"
    ASSIGN = "="
    LT = "<"
    GT = ">"
    AT = "@"
    COLON = ":"
    ARROW = "->"
    SEMICOLON = ";"
    LBRACE = "{"
    RBRACE = "}"
    LBRACKET = "["
    RBRACKET = "]"
    LPAREN = "("
    RPAREN = ")"
    COMMA = ","
    EOF = "EOF"


@dataclass(frozen=True)
class Token:
    """Represents one lexical token together with its source span."""

    type: TokenType
    value: str
    span: SourceSpan
