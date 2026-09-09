#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Semantic Model - Resolved schema produced by the IDL builder.

Records interned string/bytes types, user types, services, and packed
storage sizes. Header emission reads this model later and assigns C
identifiers then.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ...htc.common import SourceSpan
from dataclasses import dataclass, field
from collections import OrderedDict


@dataclass(frozen=True)
class ResolvedType:
    """A type after name resolution, with packed storage size.

    This is the C packed-struct size used for scatter storage, not the
    compact on-wire size. Interned `string<N>` includes a trailing NUL.
    """

    packed_size: int


@dataclass(frozen=True)
class ScalarType(ResolvedType):
    """A built-in scalar type."""

    name: str = ""


@dataclass(frozen=True)
class EnumTypeRef(ResolvedType):
    """A resolved reference to a user enum."""

    name: str = ""


@dataclass(frozen=True)
class StructTypeRef(ResolvedType):
    """A resolved reference to a user struct."""

    name: str = ""


@dataclass(frozen=True)
class InternedTypeRef(ResolvedType):
    """A resolved `string<N>` or `bytes<N>` interned type."""

    kind: str = ""
    max_len: int = 0


@dataclass(frozen=True)
class ArrayTypeRef(ResolvedType):
    """A resolved fixed-length array."""

    element: ResolvedType = field(default_factory=lambda: ResolvedType(0))
    length: int = 0


@dataclass(frozen=True)
class Field:
    """A named field with a resolved type."""

    name: str
    type: ResolvedType
    span: SourceSpan | None = None


@dataclass(frozen=True)
class EnumItem:
    """One enum item with its assigned integer value."""

    name: str
    value: int
    span: SourceSpan | None = None


@dataclass
class EnumType:
    """A resolved enum type."""

    name: str
    backing: str
    items: list[EnumItem]
    packed_size: int
    span: SourceSpan | None = None


@dataclass
class InternedType:
    """One interned `string<N>` or `bytes<N>` layout."""

    kind: str
    max_len: int
    size_type: str
    packed_size: int


@dataclass
class StructType:
    """A resolved struct type."""

    name: str
    fields: list[Field]
    packed_size: int
    span: SourceSpan | None = None


@dataclass
class Method:
    """A resolved service method."""

    name: str
    method_id: int
    oneway: bool
    request: list[Field]
    response: list[Field]
    request_size: int
    response_size: int
    span: SourceSpan | None = None


@dataclass
class Service:
    """A resolved service."""

    name: str
    service_id: int
    methods: list[Method]
    span: SourceSpan | None = None


@dataclass
class Schema:
    """Complete semantic IDL schema for one compilation."""

    enums: OrderedDict[str, EnumType] = field(default_factory=OrderedDict)
    structs: OrderedDict[str, StructType] = field(default_factory=OrderedDict)
    services: OrderedDict[str, Service] = field(default_factory=OrderedDict)
    strings: OrderedDict[int, InternedType] = field(default_factory=OrderedDict)
    bytes_types: OrderedDict[int, InternedType] = field(
        default_factory=OrderedDict)
    request_size_max: int = 0
    response_size_max: int = 0
