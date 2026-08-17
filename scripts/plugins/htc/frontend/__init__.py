#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Frontend Package - Public frontend exports for Hardware Tree Compiler.

Re-exports preprocessing, lexing, parsing, and token model types.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .conditional_state import ConditionalState
from .macro_expander import MacroExpander
from .preprocessor import Preprocessor
from .resolver import SourceResolver
from .tokens import Token, TokenType
from .lexer import Lexer
from .parser import Parser
from .cpp import CppOptions, CppPreprocessor, PreprocessedUnit

__all__ = [
    "ConditionalState",
    "CppOptions",
    "CppPreprocessor",
    "PreprocessedUnit",
    "Lexer",
    "MacroExpander",
    "Parser",
    "Preprocessor",
    "SourceResolver",
    "Token",
    "TokenType",
]
