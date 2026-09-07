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
    InternedTypeRef,
    ResolvedType,
    ScalarType,
    Schema,
    StructTypeRef,
    Field,
)
from dataclasses import dataclass

PREFIX_WIDTH = {
    "u8": 1,
    "u16": 2,
    "u32": 4,
}
BOOL_INDEX_NAMES = ("i", "j", "k")
# Limits for generated codec n[] (variable) and src[] (moving) arrays
VAR_SLOT_MAX = 256
MOVING_SLOT_MAX = 256


@dataclass(frozen=True)
class FixedSlot:
    """A contiguous run of fixed-width bytes in the packed struct."""

    path: str


@dataclass(frozen=True)
class VarSlot:
    """One interned `string<N>` or `bytes<N>` in the packed struct."""

    kind: str
    max_len: int
    size_type: str
    prefix_size: int
    path: str


Slot = FixedSlot | VarSlot


@dataclass(frozen=True)
class Layout:
    """Scatter layout of one request or response payload."""

    slots: tuple[Slot, ...]

    @property
    def has_variable(self) -> bool:
        """Return True when the payload contains string or bytes data."""
        return self.var_count > 0

    @property
    def var_count(self) -> int:
        """Return the number of interned string or bytes slots."""
        return sum(1 for slot in self.slots if isinstance(slot, VarSlot))

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

    @property
    def strings(self) -> tuple[VarSlot, ...]:
        """Return interned string slots that need a trailing NUL."""
        return tuple(slot for slot in self.slots
                     if isinstance(slot, VarSlot) and slot.kind == "string")

    @classmethod
    def from_fields(cls, fields: list[Field], schema: Schema) -> "Layout":
        """Flatten one payload into scatter slots."""
        raw: list[Slot] = []
        for field in fields:
            _flatten_type(field.type, field.name, raw, schema)
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


def size_prefix(max_len: int) -> tuple[str, int]:
    """Return the size-prefix type and width for `string<N>` / `bytes<N>`."""
    if max_len <= 255:
        return "u8", PREFIX_WIDTH["u8"]
    if max_len <= 65535:
        return "u16", PREFIX_WIDTH["u16"]
    return "u32", PREFIX_WIDTH["u32"]


def _is_fixed(resolved: ResolvedType, schema: Schema) -> bool:
    """Return True when a type has the same compact and scatter size."""
    current = resolved
    while isinstance(current, ArrayTypeRef):
        current = current.element
    if isinstance(current, InternedTypeRef):
        return False
    if isinstance(current, StructTypeRef):
        struct = schema.structs[current.name]
        return all(_is_fixed(field.type, schema) for field in struct.fields)
    return isinstance(current, (ScalarType, EnumTypeRef))


def bool_index_name(depth: int) -> str:
    """Return the C index name used for one bool-array loop depth."""
    if depth < len(BOOL_INDEX_NAMES):
        return BOOL_INDEX_NAMES[depth]
    return f"i{depth}"


def bool_paths(fields: list[Field],
               schema: Schema) -> list[tuple[str, tuple[int, ...]]]:
    """Collect bool accesses and the array lengths that wrap them."""
    found: list[tuple[str, tuple[int, ...]]] = []
    for field in fields:
        _collect_bools(field.type, field.name, [], found, schema)
    return found


def _collect_bools(resolved: ResolvedType, path: str, loops: list[int],
                   found: list[tuple[str, tuple[int, ...]]],
                   schema: Schema) -> None:
    """Walk one type and append every bool path under it."""
    if isinstance(resolved, ArrayTypeRef):
        index = bool_index_name(len(loops))
        _collect_bools(resolved.element, f"{path}[{index}]",
                       [*loops, resolved.length], found, schema)
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
                  slots: list[Slot], schema: Schema) -> None:
    """Append scatter slots for one type."""
    if _is_fixed(resolved, schema):
        slots.append(FixedSlot(path))
        return
    if isinstance(resolved, ArrayTypeRef):
        for index in range(resolved.length):
            _flatten_type(resolved.element, f"{path}[{index}]",
                          slots, schema)
        return
    if isinstance(resolved, InternedTypeRef):
        table = (schema.strings if resolved.kind == "string"
                 else schema.bytes_types)
        interned = table.get(resolved.max_len)
        if interned is not None:
            size_type = interned.size_type
            prefix_size = PREFIX_WIDTH[size_type]
        else:
            size_type, prefix_size = size_prefix(resolved.max_len)
        slots.append(VarSlot(
            resolved.kind, resolved.max_len, size_type, prefix_size, path))
        return
    if isinstance(resolved, StructTypeRef):
        struct = schema.structs[resolved.name]
        for field in struct.fields:
            child = f"{path}.{field.name}" if path else field.name
            _flatten_type(field.type, child, slots, schema)
        return
    slots.append(FixedSlot(path))
