#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Layout - Scatter slots for compact payload movement.

Flattens a method payload into fixed and variable slots so the codec
generator can expand compact wire data into the packed struct, then pack
it back.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..semantic.model import (
    ArrayTypeRef,
    EnumTypeRef,
    ResolvedType,
    ScalarType,
    Schema,
    StructTypeRef,
    VariableArrayTypeRef,
    Field,
)
from dataclasses import dataclass
from collections.abc import Iterator

PREFIX_WIDTH = {
    "u8": 1,
    "u16": 2,
    "u32": 4,
}
BOOL_INDEX_NAMES = ("i", "j", "k")
BoolBound = int | tuple[str, int]
# Limits for generated codec n[] (variable) and src[] (moving) arrays
VAR_SLOT_MAX = 256
MOVING_SLOT_MAX = 256


class LayoutLimitError(ValueError):
    """A payload exceeds the generated codec's slot budget."""


@dataclass(frozen=True)
class FixedSlot:
    """A contiguous run of fixed-width bytes in the packed struct."""

    path: str


@dataclass(frozen=True)
class VarSlot:
    """One bounded variable-length array in the packed struct."""

    max_count: int
    size_type: str
    prefix_size: int
    element_size: int
    path: str


Slot = FixedSlot | VarSlot


@dataclass(frozen=True)
class Layout:
    """Scatter layout of one request or response payload."""

    slots: tuple[Slot, ...]

    @property
    def has_variable(self) -> bool:
        """Return True when the payload contains variable-length data."""
        return self.var_count > 0

    @property
    def var_count(self) -> int:
        """Return the number of variable-length slots."""
        return sum(1 for slot in self.slots if isinstance(slot, VarSlot))

    @property
    def variable_only(self) -> bool:
        """Return True when every payload slot is variable-length data."""
        return bool(self.slots) and all(
            isinstance(slot, VarSlot) for slot in self.slots)

    @property
    def moving_slots(self) -> tuple[Slot, ...]:
        """Return slots that sit after a variable field and must be moved."""
        moving: list[Slot] = []
        seen_var = False
        for slot in self.slots:
            if seen_var:
                moving.append(slot)
            if isinstance(slot, VarSlot):
                seen_var = True
        return tuple(moving)

    @classmethod
    def from_fields(cls, fields: list[Field], schema: Schema) -> "Layout":
        """Flatten one payload into scatter slots."""
        raw: list[Slot] = []
        variables = 0
        moving = 0
        for field in fields:
            for slot in _flatten_type(field.type, field.name, schema):
                if variables:
                    moving += 1
                if isinstance(slot, VarSlot):
                    variables += 1
                if variables > VAR_SLOT_MAX:
                    raise LayoutLimitError(
                        f"requires more than {VAR_SLOT_MAX} variable slots")
                if moving > MOVING_SLOT_MAX:
                    raise LayoutLimitError(
                        f"requires more than {MOVING_SLOT_MAX} moving slots")
                raw.append(slot)
        return cls(tuple(raw))

    @classmethod
    def map_schema(cls, schema: Schema) -> dict[tuple[str, str, str], "Layout"]:
        """Build scatter layouts for every non-empty method payload."""
        layouts: dict[tuple[str, str, str], Layout] = {}
        for service in schema.services.values():
            for method in service.methods:
                if method.request:
                    layouts[(service.name, method.name, "request")] = (
                        cls.from_fields(method.request, schema))
                if method.oneway or not method.response:
                    continue
                layouts[(service.name, method.name, "response")] = (
                    cls.from_fields(method.response, schema))
        return layouts


def _is_fixed(resolved: ResolvedType, schema: Schema) -> bool:
    """Return True when a type has the same compact and scatter size."""
    if isinstance(resolved, VariableArrayTypeRef):
        return False
    if isinstance(resolved, ArrayTypeRef):
        return _is_fixed(resolved.element, schema)
    if isinstance(resolved, StructTypeRef):
        struct = schema.structs[resolved.name]
        return all(_is_fixed(field.type, schema) for field in struct.fields)
    return isinstance(resolved, (ScalarType, EnumTypeRef))


def bool_index_name(depth: int) -> str:
    """Return the C index name used for one bool-array loop depth."""
    if depth < len(BOOL_INDEX_NAMES):
        return BOOL_INDEX_NAMES[depth]
    return f"i{depth}"


def bool_paths(fields: list[Field],
               schema: Schema) -> list[tuple[str, tuple[BoolBound, ...]]]:
    """Collect bool accesses and fixed or runtime array loop bounds."""
    found: list[tuple[str, tuple[BoolBound, ...]]] = []
    for field in fields:
        _collect_bools(field.type, field.name, [], found, schema)
    return found


def _collect_bools(resolved: ResolvedType, path: str,
                   loops: list[BoolBound],
                   found: list[tuple[str, tuple[BoolBound, ...]]],
                   schema: Schema) -> None:
    """Walk one type and append every bool path under it."""
    if isinstance(resolved, ArrayTypeRef):
        index = bool_index_name(len(loops))
        _collect_bools(resolved.element, f"{path}[{index}]",
                       [*loops, resolved.length], found, schema)
        return
    if isinstance(resolved, VariableArrayTypeRef):
        index = bool_index_name(len(loops))
        bound: BoolBound = (path, resolved.max_count)
        _collect_bools(resolved.element, f"{path}.data[{index}]",
                       [*loops, bound], found, schema)
        return
    if isinstance(resolved, StructTypeRef):
        struct = schema.structs[resolved.name]
        for field in struct.fields:
            child = f"{path}.{field.name}" if path else field.name
            _collect_bools(field.type, child, loops, found, schema)
        return
    if isinstance(resolved, ScalarType) and resolved.name == "bool":
        found.append((path, tuple(loops)))


def _flatten_type(resolved: ResolvedType, path: str,
                  schema: Schema) -> Iterator[Slot]:
    """Yield slots lazily so limits stop oversized array expansion early."""
    if _is_fixed(resolved, schema):
        yield FixedSlot(path)
        return
    if isinstance(resolved, ArrayTypeRef):
        for index in range(resolved.length):
            yield from _flatten_type(resolved.element, f"{path}[{index}]",
                                     schema)
        return
    if isinstance(resolved, VariableArrayTypeRef):
        yield VarSlot(
            resolved.max_count,
            resolved.size_type,
            PREFIX_WIDTH[resolved.size_type],
            resolved.element.packed_size,
            path,
        )
        return
    if isinstance(resolved, StructTypeRef):
        struct = schema.structs[resolved.name]
        for field in struct.fields:
            child = f"{path}.{field.name}" if path else field.name
            yield from _flatten_type(field.type, child, schema)
        return
    yield FixedSlot(path)
