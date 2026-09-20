#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Semantic Builder - Resolve parsed IDL syntax into a packed schema.

Assigns protocol IDs, interns bounded variable arrays, and checks names and
layout sizes. C identifiers are left to the header generator.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ..frontend.syntax import (
    ArrayType,
    Document,
    EnumDecl,
    FieldDecl,
    MethodDecl,
    NamedType,
    PrimType,
    ServiceDecl,
    StructDecl,
    TypeExpr,
    VariableArrayType,
)
from .model import (
    ArrayTypeRef,
    EnumItem,
    EnumType,
    EnumTypeRef,
    Field,
    Method,
    ScalarType,
    Schema,
    Service,
    StructType,
    StructTypeRef,
    ResolvedType,
    VariableArrayType as ResolvedVariableArrayType,
    VariableArrayTypeRef,
)
from ...htc.common import Diagnostics, ErrorCode, SourceSpan


class SemanticBuilder:
    """Builds a semantic schema from a parsed IDL document."""

    PAYLOAD_MAX = 0xFFFFFFFF
    RESPONSE_MAX = 0x7FFFFFFF
    ID_MAX = 0xFFFF
    SCALAR_SIZES = {
        "u8": 1, "i8": 1, "bool": 1,
        "u16": 2, "i16": 2,
        "u32": 4, "i32": 4, "f32": 4,
        "u64": 8, "i64": 8, "f64": 8,
    }
    ENUM_RANGES = {
        "u8": (0, 255),
        "u16": (0, 65535),
        "u32": (0, 0xFFFFFFFF),
        "u64": (0, 0xFFFFFFFFFFFFFFFF),
        "i8": (-128, 127),
        "i16": (-32768, 32767),
        "i32": (-2147483648, 2147483647),
        "i64": (-9223372036854775808, 9223372036854775807),
    }

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Initialize builder state for one compilation."""
        self.diagnostics = diagnostics
        self.schema = Schema()
        self._variable_arrays: dict[
            tuple[ResolvedType, int], ResolvedVariableArrayType
        ] = {}
        self._kinds: dict[str, str] = {}
        self._aliases: dict[str, str] = {}
        self._defining: set[str] = set()
        self._service_ids: dict[int, str] = {}
        self._next_service_id = 0

    def build(self, document: Document) -> Schema:
        """Resolve all declarations in source order and return the schema."""
        for declaration in document.declarations:
            if isinstance(declaration, EnumDecl):
                self._build_enum(declaration)
            elif isinstance(declaration, StructDecl):
                self._build_struct(declaration)
            elif isinstance(declaration, ServiceDecl):
                self._build_service(declaration)

        # Record union maxima after all methods are resolved
        self._finish_sizes()
        return self.schema

    def _build_enum(self, decl: EnumDecl) -> None:
        """Resolve one enum declaration into the schema."""
        if not self._claim_user_name(decl.name, "enum", decl.span):
            return
        low, high = self.ENUM_RANGES[decl.backing]
        items: list[EnumItem] = []
        names: set[str] = set()
        values: dict[int, str] = {}
        next_value = 0

        # Assign values, auto-increment when `=` is omitted
        for item in decl.items:
            if item.name in names:
                self._error(ErrorCode.DUPLICATE_LABEL,
                            f"duplicate enum item '{item.name}'", item.span)
                continue
            names.add(item.name)
            value = next_value if item.value is None else item.value
            if value < low or value > high:
                self._error(ErrorCode.INVALID_PROPERTY,
                            f"enum item '{item.name}' value {value} does not "
                            f"fit in {decl.backing}", item.span)
                continue
            if value in values:
                self._error(ErrorCode.DUPLICATE_LABEL,
                            f"enum value {value} is already used by "
                            f"'{values[value]}'", item.span)
                continue
            values[value] = item.name
            items.append(EnumItem(item.name, value, item.span))
            next_value = value + 1

        # Drop a failed enum from the type namespace
        if not items:
            self._kinds.pop(decl.name, None)
            return
        packed_size = self.SCALAR_SIZES[decl.backing]
        self.schema.enums[decl.name] = EnumType(
            decl.name, decl.backing, items, packed_size, decl.span)

    def _build_struct(self, decl: StructDecl) -> None:
        """Resolve one struct declaration into the schema."""
        if not self._claim_user_name(decl.name, "struct", decl.span):
            return

        # Mark in-progress so a field of this type is recursion
        self._defining.add(decl.name)
        fields = self._resolve_fields(decl.fields, f"struct '{decl.name}'")
        self._defining.discard(decl.name)
        if fields is None:
            self._kinds.pop(decl.name, None)
            return
        packed_size = self._sum_sizes(fields, decl.span)
        self.schema.structs[decl.name] = StructType(
            decl.name, fields, packed_size, decl.span)

    def _build_service(self, decl: ServiceDecl) -> None:
        """Resolve one service declaration into the schema."""
        if not self._claim_user_name(decl.name, "service", decl.span):
            return

        # Assign the service ID, then methods
        service_id = self._assign_id(
            decl.service_id, self._next_service_id, self._service_ids,
            decl.name, "service", decl.span)
        if service_id is None:
            self._kinds.pop(decl.name, None)
            return
        if service_id == self.ID_MAX:
            self._error(ErrorCode.INVALID_PROPERTY,
                        f"service ID {service_id} is unavailable", decl.span)
            self._kinds.pop(decl.name, None)
            self._service_ids.pop(service_id, None)
            return
        self._next_service_id = service_id + 1
        methods: list[Method] = []
        method_names: set[str] = set()
        method_ids: dict[int, str] = {}
        next_method_id = 0
        for method_decl in decl.methods:
            if method_decl.name in method_names:
                self._error(ErrorCode.DUPLICATE_NODE,
                            f"duplicate method '{method_decl.name}' in "
                            f"service '{decl.name}'", method_decl.span)
                continue
            method = self._build_method(
                decl.name, method_decl, method_ids, next_method_id)
            if method is None:
                continue
            method_names.add(method.name)
            method_ids[method.method_id] = method.name
            next_method_id = method.method_id + 1
            methods.append(method)

        # Drop a failed service and free its ID
        if not methods:
            self._kinds.pop(decl.name, None)
            self._service_ids.pop(service_id, None)
            return
        self.schema.services[decl.name] = Service(
            decl.name, service_id, methods, decl.span)

    def _build_method(self, service: str, decl: MethodDecl,
                      method_ids: dict[int, str],
                      next_id: int) -> Method | None:
        """Resolve one method, including bounded variable payload types."""
        method_id = self._assign_id(
            decl.method_id, next_id, method_ids, decl.name,
            f"method in service '{service}'", decl.span)
        if method_id is None:
            return None

        # Resolve request, then optional response
        request = self._resolve_fields(
            decl.request, f"{service}.{decl.name} request")
        if request is None:
            return None
        oneway = decl.response is None
        response: list[Field] = []
        if not oneway:
            resolved = self._resolve_fields(
                decl.response, f"{service}.{decl.name} response")
            if resolved is None:
                return None
            response = resolved
        request_size = self._sum_sizes(request, decl.span)
        response_size = 0 if oneway else self._sum_sizes(response, decl.span)
        # Encoders return the byte count through signed mr_err_t. A larger
        # declared response can wrap into a negative RPC error at runtime.
        if response_size > self.RESPONSE_MAX:
            self._error(ErrorCode.INVALID_ARRAY,
                        "response packed size exceeds signed codec result",
                        decl.span)
            return None

        # Reserve `{service}_{method}_request/response` in the type namespace
        request_alias = f"{service}_{decl.name}_request"
        self._claim_alias(request_alias, f"method {service}.{decl.name}",
                          decl.span)
        if not oneway:
            response_alias = f"{service}_{decl.name}_response"
            self._claim_alias(response_alias, f"method {service}.{decl.name}",
                              decl.span)
        return Method(
            decl.name, method_id, oneway, request, response, request_size,
            response_size, decl.span)

    def _resolve_fields(self, decls: list[FieldDecl],
                        owner: str) -> list[Field] | None:
        """Resolve a field list and reject duplicate names."""
        fields: list[Field] = []
        names: set[str] = set()
        ok = True
        for decl in decls:
            # Field names must be unique in this list
            if decl.name in names:
                self._error(ErrorCode.DUPLICATE_LABEL,
                            f"duplicate field '{decl.name}' in {owner}",
                            decl.span)
                ok = False
                continue
            names.add(decl.name)
            resolved = self._resolve_type(decl.type)
            if resolved is None:
                ok = False
                continue
            fields.append(Field(decl.name, resolved, decl.span))
        return None if not ok else fields

    def _resolve_type(self, expr: TypeExpr) -> ResolvedType | None:
        """Resolve one type expression to a packed layout."""
        if isinstance(expr, ArrayType):
            return self._resolve_array(expr)
        if isinstance(expr, VariableArrayType):
            return self._resolve_variable_array(expr)
        if isinstance(expr, PrimType):
            return ScalarType(self.SCALAR_SIZES[expr.name], expr.name)
        if isinstance(expr, NamedType):
            return self._resolve_named(expr)
        self._error(ErrorCode.INVALID_PROPERTY, "unknown type expression",
                    expr.span)
        return None

    def _resolve_array(self, expr: ArrayType) -> ArrayTypeRef | None:
        """Resolve a fixed array and check its packed size."""
        element = self._resolve_type(expr.element)
        if element is None:
            return None

        if expr.length < 1:
            self._error(ErrorCode.INVALID_ARRAY,
                        "array length must be >= 1", expr.span)
            return None
        packed_size = element.packed_size * expr.length
        if packed_size > self.PAYLOAD_MAX:
            self._error(ErrorCode.INVALID_ARRAY,
                        "array packed size exceeds u32 payload_size",
                        expr.span)
            return None
        return ArrayTypeRef(packed_size, element, expr.length)

    def _resolve_variable_array(
            self, expr: VariableArrayType) -> VariableArrayTypeRef | None:
        """Resolve a bounded variable array with a fixed-size element type."""
        element = self._resolve_type(expr.element)
        if element is None:
            return None
        if expr.max_count < 1:
            self._error(ErrorCode.INVALID_ARRAY,
                        "variable array maximum count must be >= 1", expr.span)
            return None
        if not self._is_fixed_type(element):
            self._error(
                ErrorCode.INVALID_ARRAY,
                "variable array element type must have fixed wire size",
                expr.span,
            )
            return None

        size_type = self._size_prefix(expr.max_count)
        if size_type is None:
            self._error(
                ErrorCode.INVALID_ARRAY,
                f"variable array <{expr.max_count}> is larger than a u32 "
                "size prefix",
                expr.span,
            )
            return None
        packed_size = (self.SCALAR_SIZES[size_type] +
                       element.packed_size * expr.max_count)
        if packed_size > self.PAYLOAD_MAX:
            self._error(ErrorCode.INVALID_ARRAY,
                        "variable array packed size exceeds u32 payload_size",
                        expr.span)
            return None

        key = (element, expr.max_count)
        interned = self._variable_arrays.get(key)
        if interned is None:
            index = len(self.schema.variable_arrays)
            interned = ResolvedVariableArrayType(
                index, element, expr.max_count, size_type, packed_size)
            self.schema.variable_arrays[index] = interned
            self._variable_arrays[key] = interned
        return VariableArrayTypeRef(
            interned.packed_size, interned.element, interned.max_count,
            interned.size_type, interned.index)

    def _is_fixed_type(self, resolved: ResolvedType) -> bool:
        """Return True when compact and scatter sizes are always identical."""
        if isinstance(resolved, VariableArrayTypeRef):
            return False
        if isinstance(resolved, ArrayTypeRef):
            return self._is_fixed_type(resolved.element)
        if isinstance(resolved, StructTypeRef):
            struct = self.schema.structs[resolved.name]
            return all(self._is_fixed_type(field.type) for field in struct.fields)
        return isinstance(resolved, (ScalarType, EnumTypeRef))

    def _resolve_named(self, expr: NamedType) -> ResolvedType | None:
        """Resolve a user type name with define-before-use and cycle checks."""
        name = expr.name

        # Recursion, enum, struct, or a service used as a type
        if name in self._defining:
            self._error(ErrorCode.CIRCULAR_REFERENCE,
                        f"recursive struct '{name}' is not allowed", expr.span)
            return None
        if name in self.schema.enums:
            enum = self.schema.enums[name]
            return EnumTypeRef(enum.packed_size, name)
        if name in self.schema.structs:
            struct = self.schema.structs[name]
            return StructTypeRef(struct.packed_size, name)
        if self._kinds.get(name) == "service":
            self._error(ErrorCode.INVALID_PROPERTY,
                        f"'{name}' is a service and cannot be used as a type",
                        expr.span)
            return None
        self._error(ErrorCode.UNDEFINED_LABEL,
                    f"unknown type '{name}'", expr.span)
        return None

    def _size_prefix(self, max_count: int) -> str | None:
        """Return the smallest unsigned integer type that can hold a count."""
        if max_count <= 255:
            return "u8"
        if max_count <= 65535:
            return "u16"
        if max_count <= 0xFFFFFFFF:
            return "u32"
        return None

    def _assign_id(self, explicit: int | None, next_id: int,
                   used: dict[int, str], name: str, kind: str,
                   span: SourceSpan | None) -> int | None:
        """Assign an explicit or sequential protocol ID in `0..65535`."""
        value = next_id if explicit is None else explicit

        # Range and uniqueness in this scope
        if value < 0 or value > self.ID_MAX:
            self._error(ErrorCode.INVALID_PROPERTY,
                        f"{kind} ID {value} is outside 0..{self.ID_MAX}",
                        span)
            return None
        if value in used:
            self._error(ErrorCode.DUPLICATE_NODE,
                        f"{kind} ID {value} is already used by "
                        f"'{used[value]}'", span)
            return None
        used[value] = name
        return value

    def _claim_user_name(self, name: str, kind: str,
                         span: SourceSpan | None) -> bool:
        """Claim an IDL type-namespace name and reject reserved forms."""
        if name in self._kinds:
            self._error(ErrorCode.DUPLICATE_NODE,
                        f"duplicate {kind} '{name}', already a "
                        f"{self._kinds[name]}", span)
            return False
        if name in self._aliases:
            self._error(ErrorCode.DUPLICATE_NODE,
                        f"'{name}' conflicts with {self._aliases[name]}",
                        span)
            return False
        self._kinds[name] = kind
        return True

    def _claim_alias(self, name: str, origin: str,
                     span: SourceSpan | None) -> None:
        """Reserve a generated `{service}_{method}_request` style name."""
        if name in self._kinds:
            self._error(ErrorCode.DUPLICATE_NODE,
                        f"generated name '{name}' from {origin} conflicts "
                        f"with {self._kinds[name]} '{name}'", span)
            return
        if name in self._aliases:
            self._error(ErrorCode.DUPLICATE_NODE,
                        f"generated name '{name}' from {origin} conflicts "
                        f"with {self._aliases[name]}", span)
            return
        self._aliases[name] = origin

    def _sum_sizes(self, fields: list[Field],
                   span: SourceSpan | None) -> int:
        """Sum packed field sizes and reject a u32 payload overflow."""
        total = 0
        for field in fields:
            total += field.type.packed_size
        if total > self.PAYLOAD_MAX:
            self._error(ErrorCode.INVALID_ARRAY,
                        "packed payload exceeds u32 payload_size", span)
            return 0
        return total

    def _finish_sizes(self) -> None:
        """Record the union maxima used later for session buffers."""
        request_max = 0
        response_max = 0
        for service in self.schema.services.values():
            for method in service.methods:
                request_max = max(request_max, method.request_size)
                if not method.oneway:
                    response_max = max(response_max, method.response_size)
        self.schema.request_size_max = request_max
        self.schema.response_size_max = response_max

    def _error(self, code: ErrorCode, message: str,
               span: SourceSpan | None = None) -> None:
        """Emit a semantic diagnostic."""
        diagnostic = self.diagnostics.error(code, message)
        if span is not None:
            diagnostic.with_source_span(span)
