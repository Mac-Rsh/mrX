#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Common Package - Shared common models for Hardware Tree Compiler.

Exposes diagnostics, source location data, and syntax nodes used throughout the
compiler pipeline.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .diagnostics import Diagnostic, Diagnostics, ErrorCode, Level
from .source import CompilationUnit, IncludeSite, SourceFile, SourceLocation, \
    SourceSpan
from .syntax import (
    ArrayMode,
    ArrayValue,
    EmptyValue,
    FloatValue,
    IdentifierValue,
    NodeDecl,
    NumberValue,
    PropertyDecl,
    ReferenceValue,
    RootNode,
    StringValue,
    ValueExpr,
)

__all__ = [
    "ArrayMode",
    "ArrayValue",
    "CompilationUnit",
    "Diagnostic",
    "Diagnostics",
    "EmptyValue",
    "ErrorCode",
    "FloatValue",
    "IdentifierValue",
    "IncludeSite",
    "Level",
    "NodeDecl",
    "NumberValue",
    "PropertyDecl",
    "ReferenceValue",
    "RootNode",
    "SourceFile",
    "SourceLocation",
    "SourceSpan",
    "StringValue",
    "ValueExpr",
]
