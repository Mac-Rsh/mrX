#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Frontend Package - Public frontend exports for the RPC IDL compiler.

Re-exports lexing, parsing, and syntax types.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .syntax import (
    ArrayType,
    BytesType,
    Document,
    EnumDecl,
    EnumItemDecl,
    FieldDecl,
    MethodDecl,
    NamedType,
    PrimType,
    ServiceDecl,
    StringType,
    StructDecl,
    TypeExpr,
)
from .tokens import Token, TokenType
from .parser import Parser
from .lexer import Lexer

__all__ = [
    "ArrayType",
    "BytesType",
    "Document",
    "EnumDecl",
    "EnumItemDecl",
    "FieldDecl",
    "Lexer",
    "MethodDecl",
    "NamedType",
    "Parser",
    "PrimType",
    "ServiceDecl",
    "StringType",
    "StructDecl",
    "Token",
    "TokenType",
    "TypeExpr",
]
