#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Common Package - Shared common models for Hardware Tree Compiler.

Exposes diagnostics, source location data, and syntax nodes used throughout the
compiler pipeline.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from .source import CompilationUnit, IncludeSite, SourceFile, SourceLocation, \
    SourceSpan
from .diagnostics import Diagnostic, Diagnostics, ErrorCode, Level
from .status import (
    STATUS_DEFER,
    STATUS_DISABLED,
    STATUS_EARLY,
    STATUS_LAST,
    STATUS_OKAY,
    STATUS_PRIO,
    STATUS_PRIO_PREFIX,
    STATUS_PRIO_SUFFIX,
    VALID_STATUS_VALUES,
    parse_priority_status,
)
from .syntax import (
    ArrayMode,
    ArrayValue,
    ByteStringValue,
    DeleteNodeDecl,
    DeletePropertyDecl,
    Document,
    DocumentOperation,
    DtsV1Tag,
    EmptyValue,
    FloatValue,
    IdentifierValue,
    MemreserveDecl,
    NodeDecl,
    NodeOverrideDecl,
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
    "ByteStringValue",
    "DeleteNodeDecl",
    "DeletePropertyDecl",
    "Document",
    "DocumentOperation",
    "DtsV1Tag",
    "MemreserveDecl",
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
    "NodeOverrideDecl",
    "NumberValue",
    "PropertyDecl",
    "ReferenceValue",
    "RootNode",
    "SourceFile",
    "SourceLocation",
    "SourceSpan",
    "STATUS_DEFER",
    "STATUS_DISABLED",
    "STATUS_EARLY",
    "STATUS_LAST",
    "STATUS_OKAY",
    "STATUS_PRIO",
    "STATUS_PRIO_PREFIX",
    "STATUS_PRIO_SUFFIX",
    "StringValue",
    "VALID_STATUS_VALUES",
    "ValueExpr",
    "parse_priority_status",
]
