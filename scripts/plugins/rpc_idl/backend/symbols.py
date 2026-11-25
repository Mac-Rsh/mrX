#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
IDL Backend Symbols - Final C identifier collision checks.

Registers every generated C identifier after semantic analysis so
typedefs, tags, macros, entries, codec helpers, and union members share
one conflict table.

@copyright (c) 2024-2026, MacRsh

@license SPDX-License-Identifier: Apache-2.0
"""

from ...htc.common import Diagnostics, ErrorCode, SourceSpan
from ..semantic.model import Method, Schema, Service
from .layout import Layout, bool_paths
from .names import CNames
from dataclasses import dataclass


@dataclass(frozen=True)
class CSymbol:
    """One generated C identifier and the IDL origin that produced it."""

    ident: str
    origin: str
    span: SourceSpan | None = None


class CSymbolChecker:
    """Registers generated C identifiers and reports collisions."""

    def __init__(self, diagnostics: Diagnostics) -> None:
        """Create an empty identifier table for one compilation."""
        self.diagnostics = diagnostics
        self._used: dict[str, CSymbol] = {}

    def check(self, schema: Schema,
              layouts: dict[tuple[str, str, str], Layout] | None = None) -> None:
        """Register all generated C identifiers and report collisions."""
        if layouts is None:
            layouts = Layout.map_schema(schema)
        self._register_variable_arrays(schema)
        self._register_enums(schema)
        self._register_structs(schema)
        for service in schema.services.values():
            for method in service.methods:
                self._register_method(service, method, schema, layouts)
        self._register_unions()
        self._register_self_desc()

    def _register_variable_arrays(self, schema: Schema) -> None:
        """Register interned variable-array tags and typedefs."""
        for item in schema.variable_arrays.values():
            origin = f"variable array #{item.index}"
            self._add(CNames.variable_array_tag(item.index), origin)
            self._add(CNames.variable_array_typedef(item.index), origin)

    def _register_enums(self, schema: Schema) -> None:
        """Register enum typedefs and item macros."""
        for enum in schema.enums.values():
            origin = f"enum '{enum.name}'"
            self._add(CNames.user_typedef(enum.name), origin, enum.span)
            for item in enum.items:
                ident = CNames.enum_const(enum.name, item.name)
                self._add(ident, f"enum item '{enum.name}.{item.name}'",
                          item.span)

    def _register_structs(self, schema: Schema) -> None:
        """Register struct tags and typedefs."""
        for struct in schema.structs.values():
            origin = f"struct '{struct.name}'"
            self._add(CNames.user_tag(struct.name), origin, struct.span)
            self._add(CNames.user_typedef(struct.name), origin, struct.span)

    def _register_method(self, service: Service, method: Method,
                         schema: Schema,
                         layouts: dict[tuple[str, str, str], Layout]) -> None:
        """Register identifiers produced by one method."""
        origin = f"method {service.name}.{method.name}"
        span = method.span
        request = layouts.get((service.name, method.name, "request"))
        response = layouts.get((service.name, method.name, "response"))
        self._add(CNames.entry_fn(service.name, method.name), origin, span)
        if not method.request:
            self._add(CNames.empty_decode_fn(), "shared empty decode")
        elif (request is not None and (not request.has_variable)
              and (not bool_paths(method.request, schema))):
            self._add(CNames.fixed_decode_fn(method.request_size),
                      "shared fixed decode")
        else:
            self._add(CNames.decode_fn(service.name, method.name), origin,
                      span)
        self._add(CNames.invoke_fn(service.name, method.name), origin, span)
        if method.oneway or (not method.response):
            self._add(CNames.empty_encode_fn(), "shared empty encode")
        elif (response is not None and (not response.has_variable)
              and (not bool_paths(method.response, schema))):
            self._add(CNames.fixed_encode_fn(method.response_size),
                      "shared fixed encode")
        else:
            self._add(CNames.encode_fn(service.name, method.name), origin,
                      span)
        self._add(CNames.handler_fn(service.name, method.name), origin, span)
        self._add(CNames.export_macro(service.name, method.name), origin,
                  span)
        self._add(CNames.union_member(service.name, method.name), origin,
                  span)
        if method.request:
            self._add(CNames.method_request_tag(service.name, method.name),
                      origin, span)
            self._add(CNames.method_request_typedef(service.name,
                                                    method.name),
                      origin, span)
        if not method.oneway and method.response:
            self._add(CNames.method_response_tag(service.name, method.name),
                      origin, span)
            self._add(CNames.method_response_typedef(service.name,
                                                     method.name),
                      origin, span)
        if request is not None and request.has_variable:
            self._add(CNames.unpack_fn(service.name, method.name, "request"),
                      origin, span)
        if response is not None and response.has_variable:
            self._add(CNames.pack_fn(service.name, method.name, "response"),
                      origin, span)

    def _register_unions(self) -> None:
        """Register the session overlay union tags and typedefs."""
        self._add("mr_rpc_idl_request", "request union")
        self._add("mr_rpc_idl_request_t", "request union")
        self._add("mr_rpc_idl_response", "response union")
        self._add("mr_rpc_idl_response_t", "response union")

    def _register_self_desc(self) -> None:
        """Register generated self-description symbols."""
        origin = "self-description runtime"
        for name in (
            "__mr_rpc_self_desc",
            "rpc_decode_self_desc",
            "rpc_self_desc_size",
            "rpc_invoke_self_desc",
            "rpc_encode_self_desc",
        ):
            self._add(name, origin)

    def _add(self, ident: str, origin: str,
             span: SourceSpan | None = None) -> None:
        """Register one identifier, ignoring repeats from the same origin."""
        existing = self._used.get(ident)
        if existing is None:
            self._used[ident] = CSymbol(ident, origin, span)
            return
        if existing.origin == origin:
            return
        diagnostic = self.diagnostics.error(
            ErrorCode.DUPLICATE_NODE,
            f"generated C identifier '{ident}' from {origin} conflicts "
            f"with {existing.origin}",
        )
        if span is not None:
            diagnostic.with_source_span(span)
