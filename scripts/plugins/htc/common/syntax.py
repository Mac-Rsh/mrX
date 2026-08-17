#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTC Syntax - Abstract syntax tree definitions for Hardware Tree Compiler.

Defines immutable syntax nodes produced by the parser before semantic
resolution and validation.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from dataclasses import dataclass, field
from .source import SourceSpan
from enum import Enum


class ValueExpr:
    """Base protocol-like type for all parsed value expressions."""

    span: SourceSpan | None


@dataclass(frozen=True)
class EmptyValue(ValueExpr):
    """Represents a property declared without an explicit value."""

    span: SourceSpan | None = None


@dataclass(frozen=True)
class StringValue(ValueExpr):
    """Represents a string literal value."""

    value: str
    span: SourceSpan | None = None


@dataclass(frozen=True)
class NumberValue(ValueExpr):
    """Represents an integer literal with its original source text."""

    value: int
    original: str
    span: SourceSpan | None = None


@dataclass(frozen=True)
class FloatValue(ValueExpr):
    """Represents a floating-point literal with its original source text."""

    value: float
    original: str
    span: SourceSpan | None = None


@dataclass(frozen=True)
class IdentifierValue(ValueExpr):
    """Represents a bare identifier used as a value."""

    name: str
    span: SourceSpan | None = None


@dataclass(frozen=True)
class ReferenceValue(ValueExpr):
    """Represents a label or absolute-path reference value."""

    reference: str
    is_path: bool = False
    span: SourceSpan | None = None


@dataclass(frozen=True)
class ByteStringValue(ValueExpr):
    """Represents a DTS byte string value such as `[00 11 ff]`."""

    values: list[int]
    original: str = ""
    span: SourceSpan | None = None


@dataclass(frozen=True)
class DtsV1Tag:
    """Represents the optional `/dts-v1/;` document tag."""

    span: SourceSpan | None = None


@dataclass(frozen=True)
class MemreserveDecl:
    """Represents a `/memreserve/` declaration."""

    values: list[ValueExpr]
    span: SourceSpan | None = None


@dataclass(frozen=True)
class DeleteNodeDecl:
    """Represents a `/delete-node/ name;` directive."""

    name: str
    span: SourceSpan | None = None


@dataclass(frozen=True)
class DeletePropertyDecl:
    """Represents a `/delete-property/ prop;` directive."""

    name: str
    span: SourceSpan | None = None


class ArrayMode(Enum):
    """Controls how an array value is interpreted by later stages."""

    EXPANDED = "expanded"
    INLINE = "inline"


@dataclass(frozen=True)
class ArrayValue(ValueExpr):
    """Represents a list-style value containing multiple child expressions."""

    elements: list[ValueExpr] = field(default_factory=list)
    mode: ArrayMode = ArrayMode.EXPANDED
    span: SourceSpan | None = None


@dataclass(frozen=True)
class PropertyDecl:
    """Represents a parsed property declaration inside a node."""

    name: str
    value: ValueExpr
    span: SourceSpan | None = None


@dataclass(frozen=True)
class NodeDecl:
    """Represents a parsed hardware tree node and its local contents."""

    name: str
    label: str | None = None
    address: str | None = None
    properties: list[PropertyDecl] = field(default_factory=list)
    children: list["NodeDecl"] = field(default_factory=list)
    deleted_properties: list[DeletePropertyDecl] = field(default_factory=list)
    span: SourceSpan | None = None

    def get_property(self, name: str) -> PropertyDecl | None:
        """
        Get a property by name.

        Args:
            name: Property name.

        Returns:
            PropertyDecl if found, None otherwise.
        """
        for prop in self.properties:
            if prop.name == name:
                return prop
        return None


@dataclass(frozen=True)
class RootNode:
    """Represents the parsed root of a single source file."""

    properties: list[PropertyDecl] = field(default_factory=list)
    deleted_properties: list[DeletePropertyDecl] = field(default_factory=list)
    children: list[NodeDecl] = field(default_factory=list)
    span: SourceSpan | None = None


@dataclass(frozen=True)
class Document:
    """Represents one parsed DTS source document."""

    dts_v1: bool = False
    memreserves: list[MemreserveDecl] = field(default_factory=list)
    roots: list[RootNode] = field(default_factory=list)
    delete_nodes: list[DeleteNodeDecl] = field(default_factory=list)
    span: SourceSpan | None = None
