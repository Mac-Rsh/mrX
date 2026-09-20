#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Syntax - Abstract syntax tree produced by the RPC IDL parser.

Holds declaration-order syntax nodes before semantic resolution. Include
directives are expanded by the preprocessor and do not appear here.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ...htc.common import SourceSpan
from dataclasses import dataclass, field


@dataclass(frozen=True)
class TypeExpr:
    """Base type for parsed type expressions."""

    span: SourceSpan | None = None


@dataclass(frozen=True)
class PrimType(TypeExpr):
    """A built-in scalar type such as `u32` or `bool`."""

    name: str = ""


@dataclass(frozen=True)
class VariableArrayType(TypeExpr):
    """A bounded variable-length array wrapped around an inner type."""

    element: TypeExpr = field(default_factory=TypeExpr)
    max_count: int = 0


@dataclass(frozen=True)
class NamedType(TypeExpr):
    """A reference to a user `enum` or `struct` name."""

    name: str = ""


@dataclass(frozen=True)
class ArrayType(TypeExpr):
    """A fixed-length array wrapped around an inner type."""

    element: TypeExpr = field(default_factory=TypeExpr)
    length: int = 0


@dataclass(frozen=True)
class FieldDecl:
    """A named field in a struct or method parameter list."""

    name: str
    type: TypeExpr
    span: SourceSpan | None = None


@dataclass(frozen=True)
class EnumItemDecl:
    """One enum item, with an optional explicit value."""

    name: str
    value: int | None
    span: SourceSpan | None = None


@dataclass(frozen=True)
class EnumDecl:
    """A parsed `enum` declaration."""

    name: str
    backing: str
    items: list[EnumItemDecl]
    span: SourceSpan | None = None


@dataclass(frozen=True)
class StructDecl:
    """A parsed `struct` declaration."""

    name: str
    fields: list[FieldDecl]
    span: SourceSpan | None = None


@dataclass(frozen=True)
class MethodDecl:
    """A parsed method. `response` is `None` for oneway methods."""

    name: str
    method_id: int | None
    request: list[FieldDecl]
    response: list[FieldDecl] | None
    span: SourceSpan | None = None


@dataclass(frozen=True)
class ServiceDecl:
    """A parsed `service` declaration."""

    name: str
    service_id: int | None
    methods: list[MethodDecl]
    span: SourceSpan | None = None


Declaration = EnumDecl | StructDecl | ServiceDecl


@dataclass
class Document:
    """A complete IDL translation unit after preprocessing."""

    declarations: list[Declaration] = field(default_factory=list)
